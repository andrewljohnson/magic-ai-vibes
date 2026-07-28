#include "old_school/fq0_bellman_science.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::fq0_bellman_science {
namespace {

namespace information = fq0_information_set;
namespace bellman = fq0_bellman;

struct Recipe {
    std::uint64_t root_seed_base = 0;
    std::uint64_t bank_a_seed_base = 0;
    std::uint64_t bank_b_seed_base = 0;
    std::size_t root_worlds = 0;
    std::size_t successor_worlds = 0;
    std::size_t workers = 0;
};

constexpr Recipe kProductionRecipe{
    .root_seed_base = kProductionRootSeedBase,
    .bank_a_seed_base = kProductionBankASeedBase,
    .bank_b_seed_base = kProductionBankBSeedBase,
    .root_worlds = kProductionRootWorlds,
    .successor_worlds = kProductionSuccessorWorlds,
    .workers = kProductionWorkers,
};

class IndexedExecutor {
  public:
    IndexedExecutor(
        std::size_t workers, ExecutionMetadata& metadata)
        : worker_count_(workers), metadata_(metadata) {
        if (workers == 0) {
            throw std::invalid_argument(
                "FQ0 construction requires at least one worker");
        }
        metadata_.workers_requested = workers;
        threads_.reserve(workers);
        for (std::size_t index = 0; index < workers; ++index) {
            threads_.emplace_back([this] { worker_loop(); });
        }
        std::unique_lock lock(mutex_);
        ready_condition_.wait(
            lock, [&] { return ready_workers_ == worker_count_; });
        metadata_.maximum_workers_started = ready_workers_;
    }

    IndexedExecutor(const IndexedExecutor&) = delete;
    IndexedExecutor& operator=(const IndexedExecutor&) = delete;

    ~IndexedExecutor() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            ++generation_;
        }
        work_condition_.notify_all();
        for (std::thread& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void for_indices(
        std::size_t count,
        std::function<void(std::size_t)> operation) {
        if (count == 0) {
            return;
        }
        {
            std::lock_guard lock(mutex_);
            if (active_) {
                throw std::logic_error(
                    "FQ0 indexed executor does not permit nested batches");
            }
            active_ = true;
            operation_ = std::move(operation);
            task_count_ = count;
            next_index_.store(0, std::memory_order_relaxed);
            cancel_.store(false, std::memory_order_relaxed);
            completed_workers_ = 0;
            exception_ = nullptr;
            ++generation_;
            ++metadata_.parallel_batches;
            metadata_.indexed_tasks += count;
        }
        work_condition_.notify_all();

        std::unique_lock lock(mutex_);
        done_condition_.wait(
            lock,
            [&] { return completed_workers_ == worker_count_; });
        operation_ = {};
        active_ = false;
        const std::exception_ptr exception = exception_;
        lock.unlock();
        if (exception) {
            std::rethrow_exception(exception);
        }
    }

  private:
    void worker_loop() {
        std::size_t observed_generation = 0;
        {
            std::lock_guard lock(mutex_);
            ++ready_workers_;
            ready_condition_.notify_one();
        }

        while (true) {
            {
                std::unique_lock lock(mutex_);
                work_condition_.wait(lock, [&] {
                    return stopping_ ||
                           generation_ != observed_generation;
                });
                if (stopping_) {
                    return;
                }
                observed_generation = generation_;
            }

            while (!cancel_.load(std::memory_order_relaxed)) {
                const std::size_t index =
                    next_index_.fetch_add(
                        1, std::memory_order_relaxed);
                if (index >= task_count_) {
                    break;
                }
                try {
                    operation_(index);
                } catch (...) {
                    cancel_.store(true, std::memory_order_relaxed);
                    std::lock_guard lock(mutex_);
                    if (!exception_) {
                        exception_ = std::current_exception();
                    }
                    break;
                }
            }

            {
                std::lock_guard lock(mutex_);
                ++completed_workers_;
                if (completed_workers_ == worker_count_) {
                    done_condition_.notify_one();
                }
            }
        }
    }

    std::size_t worker_count_ = 0;
    ExecutionMetadata& metadata_;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable ready_condition_;
    std::condition_variable work_condition_;
    std::condition_variable done_condition_;
    std::size_t ready_workers_ = 0;
    std::size_t generation_ = 0;
    std::size_t completed_workers_ = 0;
    std::size_t task_count_ = 0;
    std::atomic<std::size_t> next_index_{0};
    std::atomic<bool> cancel_{false};
    std::function<void(std::size_t)> operation_;
    std::exception_ptr exception_;
    bool active_ = false;
    bool stopping_ = false;
};

template <typename Operation>
auto indexed_map(
    IndexedExecutor& executor, std::size_t count,
    Operation operation) {
    using Result =
        std::invoke_result_t<Operation&, std::size_t>;
    std::vector<std::optional<Result>> slots(count);
    executor.for_indices(
        count,
        [&](std::size_t index) {
            slots[index].emplace(operation(index));
        });
    std::vector<Result> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (!slots[index].has_value()) {
            throw std::logic_error(
                "FQ0 indexed executor left an empty result slot");
        }
        result.push_back(std::move(*slots[index]));
    }
    return result;
}

void require_probability(
    double value, std::string_view coordinate) {
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::logic_error(
            std::string(coordinate) +
            ": expected a finite probability");
    }
}

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument(
            "FQ0 root has an invalid deck");
    }
    return index;
}

bool sorcery_actions_for_phase(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
    case TurnPhase::SecondMain:
        return true;
    case TurnPhase::BeginCombat:
    case TurnPhase::EndCombat:
        return false;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        throw std::invalid_argument(
            "FQ0 Priority root is in a declaration phase");
    }
    throw std::invalid_argument(
        "FQ0 Priority root has an invalid phase");
}

LearnedDecisionContext root_context(
    const probes::DecisionProbe& probe) {
    if (probe.root_player >= probe.state.players.size()) {
        throw std::invalid_argument(
            probe.stable_id +
            ": FQ0 root player must be zero or one");
    }
    return {
        .valid = true,
        .phase = probe.phase,
        .decision_player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
        .sorcery_actions =
            sorcery_actions_for_phase(probe.phase),
    };
}

GameState canonical_root_state(
    const probes::DecisionProbe& probe) {
    GameState state = probe.state;
    if (probe.root_player >= state.players.size()) {
        throw std::invalid_argument(
            probe.stable_id +
            ": cannot canonicalize an invalid root player");
    }
    std::sort(
        state.players[probe.root_player].hand.begin(),
        state.players[probe.root_player].hand.end());
    return state;
}

struct HiddenClone {
    GameState state;
    bool eligible = false;
    bool changed = false;
};

HiddenClone hidden_repartition_clone(
    const GameState& state, std::size_t observer) {
    if (observer >= state.players.size()) {
        throw std::invalid_argument(
            "FQ0 hidden repartition observer must be zero or one");
    }
    HiddenClone result{.state = state};
    const std::size_t opponent = 1 - observer;
    PlayerState& hidden_player =
        result.state.players[opponent];
    for (std::size_t hand_index = 0;
         hand_index < hidden_player.hand.size() &&
         !result.eligible;
         ++hand_index) {
        for (std::size_t library_index = 0;
             library_index < hidden_player.library.size();
             ++library_index) {
            if (hidden_player.hand[hand_index] ==
                hidden_player.library[library_index]) {
                continue;
            }
            std::swap(
                hidden_player.hand[hand_index],
                hidden_player.library[library_index]);
            result.eligible = true;
            break;
        }
    }
    result.changed = result.state != state;
    if (result.eligible != result.changed) {
        throw std::logic_error(
            "FQ0 hidden repartition eligibility/change "
            "classification is inconsistent");
    }
    if (observe_game_state(result.state, observer) !=
        observe_game_state(state, observer)) {
        throw std::logic_error(
            "FQ0 hidden repartition changed the observer's "
            "information");
    }
    return result;
}

struct CanonicalRootAction {
    std::string descriptor;
    PriorityAction action;
};

std::vector<CanonicalRootAction>
authoritative_root_actions(
    const probes::DecisionProbe& probe,
    const GameState& canonical_state,
    const LearnedDecisionContext& context) {
    if (probe.decision_kind !=
        probes::DecisionKind::Priority) {
        throw std::invalid_argument(
            probe.stable_id +
            ": FQ0 accepts only Priority roots");
    }
    const std::vector<PriorityAction> authoritative =
        legal_priority_actions(
            canonical_state, probe.root_player,
            context.sorcery_actions);
    if (authoritative.size() < 2 ||
        probe.candidates.size() != authoritative.size()) {
        throw std::invalid_argument(
            probe.stable_id +
            ": candidate list is not the complete nontrivial "
            "rules-authoritative Priority action set");
    }

    std::vector<CanonicalRootAction> rows;
    rows.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr || candidate.descriptor.empty()) {
            throw std::invalid_argument(
                probe.stable_id +
                ": FQ0 candidate is not a named Priority action");
        }
        if (std::find(
                authoritative.begin(), authoritative.end(),
                *action) == authoritative.end()) {
            throw std::invalid_argument(
                probe.stable_id +
                ": FQ0 candidate is not rules-authoritative");
        }
        if (std::find_if(
                rows.begin(), rows.end(),
                [&](const CanonicalRootAction& existing) {
                    return existing.action == *action;
                }) != rows.end()) {
            throw std::invalid_argument(
                probe.stable_id +
                ": FQ0 candidate repeats an action");
        }
        rows.push_back({
            .descriptor = candidate.descriptor,
            .action = *action,
        });
    }
    std::sort(
        rows.begin(), rows.end(),
        [](const CanonicalRootAction& first,
           const CanonicalRootAction& second) {
            return first.descriptor < second.descriptor;
        });
    for (std::size_t index = 1; index < rows.size(); ++index) {
        if (rows[index - 1].descriptor ==
            rows[index].descriptor) {
            throw std::invalid_argument(
                probe.stable_id +
                ": FQ0 candidate descriptors are not unique");
        }
    }
    return rows;
}

std::uint64_t indexed_seed(
    std::uint64_t base, information::SeedDomain domain,
    std::string_view scope, std::string_view group,
    information::SeedBank bank, std::size_t block,
    std::size_t world) {
    return information::derive_indexed_seed(
        base,
        {
            .domain = domain,
            .scope = std::string(scope),
            .group = std::string(group),
            .bank = bank,
            .block = block,
            .world = world,
        });
}

std::string scoped_successor_information_set(
    std::string_view root_stable_id,
    std::string_view successor_fingerprint) {
    return "successor/" + std::string(root_stable_id) + "/" +
           std::string(successor_fingerprint);
}

std::string feature_row_id(
    std::string_view information_set,
    std::string_view descriptor) {
    return "feature/" + std::string(information_set) + "/" +
           std::string(descriptor);
}

std::string legal_set_id(std::string_view information_set) {
    return "legal/" + std::string(information_set);
}

std::string common_world_key(
    std::string_view root_stable_id,
    std::string_view information_set, bool root) {
    if (root) {
        return "worlds/root/" +
               std::string(root_stable_id);
    }
    return "worlds/successor/" +
           std::string(information_set);
}

std::string stream_key(
    const Recipe& recipe, std::string_view root_stable_id,
    std::string_view successor_fingerprint,
    information::SeedBank bank, ScopeKind kind,
    std::size_t block) {
    const std::string bank_name =
        bank == information::SeedBank::A ? "A" : "B";
    std::string scope;
    if (kind == ScopeKind::Full) {
        scope = "full-k" +
                std::to_string(recipe.root_worlds);
    } else {
        scope = "block-k" +
                std::to_string(
                    recipe.root_worlds /
                    bellman::kBlockCount) +
                "-" + std::to_string(block);
    }
    return "fq0-stream/" + std::string(root_stable_id) +
           "/" + std::string(successor_fingerprint) +
           "/" + bank_name + "/" + scope;
}

struct Representative {
    information::InformationSetKey key;
    GameState state;
    LearnedDecisionContext context;
    std::vector<PriorityAction> legal_actions;
    std::size_t owner = 0;
    std::size_t root_world = 0;
    std::string root_action_descriptor;
};

struct BankPair {
    GroupBank bank_a;
    GroupBank bank_b;
    bellman::CrossFitValue cross_fit;
};

std::vector<bellman::ActionSamples> action_samples(
    const GroupBank& bank) {
    std::vector<bellman::ActionSamples> result;
    result.reserve(bank.actions.size());
    for (const GroupAction& action : bank.actions) {
        bellman::ActionSamples row{
            .descriptor = action.descriptor,
            .sample_stream_key = bank.stream_key,
        };
        row.world_indices.reserve(action.samples.size());
        row.samples.reserve(action.samples.size());
        for (const LeafSample& sample : action.samples) {
            row.world_indices.push_back(sample.world_index);
            row.samples.push_back(sample.score);
        }
        result.push_back(std::move(row));
    }
    return result;
}

std::map<std::string, double> symmetric_action_targets(
    const BankPair& pair) {
    if (pair.cross_fit.bank_a.size() !=
        pair.cross_fit.bank_b.size()) {
        throw std::logic_error(
            "FQ0 peer banks have different action counts");
    }
    std::map<std::string, double> targets;
    for (std::size_t index = 0;
         index < pair.cross_fit.bank_a.size(); ++index) {
        const bellman::ActionMean& first =
            pair.cross_fit.bank_a[index];
        const bellman::ActionMean& second =
            pair.cross_fit.bank_b[index];
        if (first.descriptor != second.descriptor) {
            throw std::logic_error(
                "FQ0 peer bank action identities drifted");
        }
        const double target =
            0.5 * (first.value + second.value);
        require_probability(target, "successor feature target");
        targets.emplace(first.descriptor, target);
    }
    return targets;
}

GroupBank evaluate_bank(
    const Recipe& recipe, IndexedExecutor& executor,
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const Representative& representative,
    std::string_view information_set_fingerprint,
    ScopeKind kind, std::size_t block,
    information::SeedBank bank,
    std::shared_ptr<const LearnedModel> model) {
    const bool selection_bank =
        bank == information::SeedBank::A;
    const std::uint64_t seed_base =
        selection_bank ? recipe.bank_a_seed_base
                       : recipe.bank_b_seed_base;
    const information::SeedDomain determinization_domain =
        selection_bank
            ? information::SeedDomain::
                  SuccessorSelectionDeterminization
            : information::SeedDomain::
                  SuccessorEvaluationDeterminization;
    const information::SeedDomain macro_domain =
        selection_bank
            ? information::SeedDomain::
                  SuccessorSelectionMacroTransition
            : information::SeedDomain::
                  SuccessorEvaluationMacroTransition;
    const std::size_t seed_block =
        kind == ScopeKind::Full
            ? bellman::kBlockCount
            : block;
    const auto rows =
        information::descriptor_canonical_action_rows(
            representative.key);
    const std::string scoped_information =
        scoped_successor_information_set(
            manifest_root.probe.stable_id,
            information_set_fingerprint);

    struct SampledSuccessorWorld {
        std::uint64_t determinization_seed = 0;
        std::uint64_t macro_seed = 0;
        GameState state;
    };
    const auto worlds = indexed_map(
        executor, recipe.successor_worlds,
        [&](std::size_t world) {
            const std::uint64_t determinization_seed =
                indexed_seed(
                    seed_base, determinization_domain,
                    manifest_root.probe.stable_id,
                    information_set_fingerprint, bank,
                    seed_block, world);
            const std::uint64_t macro_seed =
                indexed_seed(
                    seed_base, macro_domain,
                    manifest_root.probe.stable_id,
                    information_set_fingerprint, bank,
                    seed_block, world);
            return SampledSuccessorWorld{
                .determinization_seed =
                    determinization_seed,
                .macro_seed = macro_seed,
                .state = sample_determinization(
                    representative.state,
                    manifest_root.probe.original_decks,
                    representative.owner,
                    determinization_seed),
            };
        });

    const std::size_t action_count = rows.size();
    const auto flat_samples = indexed_map(
        executor, action_count * recipe.successor_worlds,
        [&](std::size_t flat_index) {
            const std::size_t action_index =
                flat_index / recipe.successor_worlds;
            const std::size_t world =
                flat_index % recipe.successor_worlds;
            const information::CanonicalActionRow& row =
                rows[action_index];
            const SampledSuccessorWorld& sampled =
                worlds[world];
            const LearnedPriorityMacroTransition transition =
                advance_learned_priority_macro_transition(
                    sampled.state,
                    manifest_root.probe.original_decks,
                    representative.owner,
                    representative.context.sorcery_actions,
                    representative.context.phase,
                    representative.context.consecutive_passes,
                    row.action, model, sampled.macro_seed);
            if (transition.disposition ==
                LearnedPriorityMacroDisposition::Incomplete) {
                throw std::runtime_error(
                    manifest_root.probe.stable_id +
                    ": FQ0 successor macro-transition "
                    "exhausted a fixed bound");
            }

            LeafSample sample{
                .world_index = world,
                .determinization_seed =
                    sampled.determinization_seed,
                .macro_seed = sampled.macro_seed,
                .terminal =
                    transition.disposition ==
                    LearnedPriorityMacroDisposition::Terminal,
                .actions_applied =
                    transition.actions_applied,
                .priority_actions_applied =
                    transition.priority_actions_applied,
                .phase_transitions =
                    transition.phase_transitions,
                .turn_advances =
                    transition.turn_advances,
                .forced_action_applied =
                    transition.actions_applied >= 1 &&
                    transition
                            .priority_actions_applied >=
                        1 &&
                    transition
                            .priority_actions_applied <=
                        transition.actions_applied,
            };
            if (!sample.forced_action_applied) {
                throw std::logic_error(
                    "FQ0 successor macro did not account "
                    "for its forced action");
            }
            if (sample.terminal) {
                if (!transition.terminal_result.has_value()) {
                    throw std::logic_error(
                        "FQ0 terminal successor lacks a result");
                }
                sample.score =
                    information::terminal_root_owner_value(
                        *transition.terminal_result,
                        representative.owner);
                sample.redacted_leaf_hash =
                    information::
                        redacted_leaf_consequence_sha256(
                            transition.state,
                            representative.owner, {},
                            transition.terminal_result);
            } else {
                if (transition.disposition !=
                        LearnedPriorityMacroDisposition::
                            PriorityBoundary ||
                    !transition.context.valid ||
                    transition.legal_actions.size() < 2) {
                    throw std::logic_error(
                        "FQ0 nonterminal successor did not stop "
                        "at a nontrivial Priority boundary");
                }
                const information::LegacyLeafCriticEvaluation
                    critic =
                        information::
                            evaluate_legacy_leaf_critic(
                                transition.state,
                                representative.owner,
                                transition.context, model);
                sample.score = critic.value;
                sample.contextual_score_bits =
                    critic.contextual_bits;
                sample.legacy_score_bits =
                    critic.legacy_bits;
                sample.critic_evaluated = true;
                sample.contextual_legacy_critic_bit_identical =
                    critic.legacy_bit_identity;
                sample.redacted_leaf_hash =
                    information::
                        redacted_leaf_consequence_sha256(
                            transition.state,
                            representative.owner,
                            transition.context);
            }
            require_probability(sample.score, "successor leaf");
            if (sample.terminal) {
                if (sample.critic_evaluated ||
                    sample.contextual_score_bits != 0 ||
                    sample.legacy_score_bits != 0 ||
                    sample
                        .contextual_legacy_critic_bit_identical) {
                    throw std::logic_error(
                        "FQ0 terminal leaf contains critic "
                        "evidence");
                }
            } else if (
                !sample.critic_evaluated ||
                sample.contextual_score_bits !=
                    std::bit_cast<std::uint64_t>(
                        sample.score) ||
                sample.legacy_score_bits !=
                    sample.contextual_score_bits ||
                !sample
                     .contextual_legacy_critic_bit_identical) {
                throw std::logic_error(
                    "FQ0 critic leaf did not retain exact "
                    "contextual/legacy score bits");
            }
            return sample;
        });

    GroupBank result{
        .bank = selection_bank ? "A" : "B",
        .stream_key = stream_key(
            recipe, manifest_root.probe.stable_id,
            information_set_fingerprint, bank, kind, block),
    };
    result.actions.reserve(action_count);
    for (std::size_t action_index = 0;
         action_index < action_count; ++action_index) {
        const information::CanonicalActionRow& row =
            rows[action_index];
        GroupAction action{
            .descriptor = row.descriptor,
            .action = row.action,
            .feature_row_id = feature_row_id(
                scoped_information, row.descriptor),
            .policy_features =
                learned_priority_policy_features(
                    representative.state,
                    representative.owner, row.action,
                    representative.context.sorcery_actions,
                    representative.context.phase,
                    representative.context.consecutive_passes),
            .canonical_consequence_fingerprint =
                information::
                    canonical_priority_consequence_sha256(
                        representative.state,
                        representative.owner,
                        representative.context, row.action),
        };
        action.samples.reserve(recipe.successor_worlds);
        const std::size_t offset =
            action_index * recipe.successor_worlds;
        for (std::size_t world = 0;
             world < recipe.successor_worlds; ++world) {
            action.samples.push_back(
                flat_samples[offset + world]);
        }
        result.actions.push_back(std::move(action));
    }
    return result;
}

BankPair evaluate_bank_pair(
    const Recipe& recipe, IndexedExecutor& executor,
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const Representative& representative,
    std::string_view information_set_fingerprint,
    ScopeKind kind, std::size_t block,
    std::shared_ptr<const LearnedModel> model) {
    BankPair result;
    result.bank_a = evaluate_bank(
        recipe, executor, manifest_root, representative,
        information_set_fingerprint, kind, block,
        information::SeedBank::A, model);
    result.bank_b = evaluate_bank(
        recipe, executor, manifest_root, representative,
        information_set_fingerprint, kind, block,
        information::SeedBank::B, model);
    if (result.bank_a.actions.size() !=
        result.bank_b.actions.size()) {
        throw std::logic_error(
            "FQ0 successor banks have different legal sets");
    }
    for (std::size_t index = 0;
         index < result.bank_a.actions.size(); ++index) {
        const GroupAction& first =
            result.bank_a.actions[index];
        const GroupAction& second =
            result.bank_b.actions[index];
        if (first.descriptor != second.descriptor ||
            first.action != second.action ||
            first.feature_row_id != second.feature_row_id ||
            first.policy_features != second.policy_features ||
            first.canonical_consequence_fingerprint !=
                second.canonical_consequence_fingerprint) {
            throw std::logic_error(
                "FQ0 successor bank action identity drifted");
        }
    }
    result.cross_fit = bellman::cross_fit_v0(
        action_samples(result.bank_a),
        action_samples(result.bank_b));
    return result;
}

std::vector<std::size_t> scope_worlds(
    const Recipe& recipe, ScopeKind kind,
    std::size_t block) {
    if (kind == ScopeKind::Full) {
        std::vector<std::size_t> result(
            recipe.root_worlds);
        std::iota(result.begin(), result.end(), 0);
        return result;
    }
    if (block >= bellman::kBlockCount) {
        throw std::out_of_range(
            "FQ0 block index is out of range");
    }
    const std::size_t width =
        recipe.root_worlds / bellman::kBlockCount;
    std::vector<std::size_t> result(width);
    std::iota(
        result.begin(), result.end(), block * width);
    return result;
}

using BankCacheKey =
    std::tuple<std::string, ScopeKind, std::size_t>;
using BankCache = std::map<BankCacheKey, BankPair>;

const BankPair& cached_bank_pair(
    const BankCache& cache, std::string_view fingerprint,
    ScopeKind kind, std::size_t block) {
    const auto found = cache.find({
        std::string(fingerprint), kind, block});
    if (found == cache.end()) {
        throw std::logic_error(
            "FQ0 successor bank cache missed a coordinate");
    }
    return found->second;
}

Scope build_scope(
    const Recipe& recipe, const RootAction& action,
    std::size_t root_owner, ScopeKind kind,
    std::size_t block, const BankCache& cache) {
    Scope scope{
        .kind = kind,
        .block = block,
        .root_world_indices =
            scope_worlds(recipe, kind, block),
    };
    std::map<std::string, std::vector<std::size_t>>
        group_members;
    for (const std::size_t world :
         scope.root_world_indices) {
        const RootTransition& transition =
            action.root_transitions.at(world);
        if (transition.terminal) {
            scope.terminals.push_back({
                .world_index = world,
                .root_owner_value =
                    transition.terminal_root_owner_value,
            });
        } else {
            group_members[
                transition
                    .successor_information_set_fingerprint]
                .push_back(world);
        }
    }

    std::vector<bellman::TerminalParticle> local_terminals;
    local_terminals.reserve(scope.terminals.size());
    for (const bellman::TerminalParticle& terminal :
         scope.terminals) {
        const auto found = std::lower_bound(
            scope.root_world_indices.begin(),
            scope.root_world_indices.end(),
            terminal.world_index);
        if (found == scope.root_world_indices.end() ||
            *found != terminal.world_index) {
            throw std::logic_error(
                "FQ0 terminal particle escaped its scope");
        }
        local_terminals.push_back({
            .world_index = static_cast<std::size_t>(
                found - scope.root_world_indices.begin()),
            .root_owner_value =
                terminal.root_owner_value,
        });
    }

    std::vector<bellman::SuccessorGroup> backed_groups;
    backed_groups.reserve(group_members.size());
    for (const auto& [fingerprint, members] :
         group_members) {
        const RootTransition& representative =
            action.root_transitions.at(members.front());
        for (const std::size_t world : members) {
            const RootTransition& transition =
                action.root_transitions.at(world);
            if (transition.successor_owner !=
                    representative.successor_owner ||
                transition
                        .successor_information_set_fingerprint !=
                    fingerprint) {
                throw std::logic_error(
                    "FQ0 successor information group is "
                    "internally inconsistent");
            }
        }
        const BankPair& pair = cached_bank_pair(
            cache, fingerprint, kind, block);
        const bellman::OwnerRelation relation =
            representative.successor_owner == root_owner
                ? bellman::OwnerRelation::SameOwner
                : bellman::OwnerRelation::OpponentOwner;
        SuccessorGroup group{
            .information_set_fingerprint = fingerprint,
            .successor_owner =
                representative.successor_owner,
            .relation = relation,
            .root_world_indices = members,
            .representative_root_world = members.front(),
            .representative_root_action_descriptor =
                action.descriptor,
            .bank_a = pair.bank_a,
            .bank_b = pair.bank_b,
            .cross_fit = pair.cross_fit,
        };
        scope.groups.push_back(std::move(group));

        bellman::SuccessorGroup backed{
            .fingerprint = fingerprint,
            .mass = members.size(),
            .relation = relation,
            .successor_owner_value =
                pair.cross_fit.value,
        };
        backed.world_indices.reserve(members.size());
        for (const std::size_t world : members) {
            const auto found = std::lower_bound(
                scope.root_world_indices.begin(),
                scope.root_world_indices.end(), world);
            backed.world_indices.push_back(
                static_cast<std::size_t>(
                    found -
                    scope.root_world_indices.begin()));
        }
        backed_groups.push_back(std::move(backed));
    }
    scope.target = bellman::back_up_root_target(
        scope.root_world_indices.size(), local_terminals,
        backed_groups);
    scope.exact_particle_partition =
        scope.target.particles ==
        scope.root_world_indices.size();
    if (!scope.exact_particle_partition) {
        throw std::logic_error(
            "FQ0 scope did not partition all root particles");
    }
    return scope;
}

std::vector<bellman::ActionMean> root_action_means(
    const std::vector<RootAction>& actions) {
    std::vector<bellman::ActionMean> result;
    result.reserve(actions.size());
    for (const RootAction& action : actions) {
        result.push_back({
            .descriptor = action.descriptor,
            .value = action.target.full,
        });
    }
    std::sort(
        result.begin(), result.end(),
        [](const bellman::ActionMean& first,
           const bellman::ActionMean& second) {
            return first.descriptor < second.descriptor;
        });
    return result;
}

Root build_root(
    const Recipe& recipe, IndexedExecutor& executor,
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    std::shared_ptr<const LearnedModel> model,
    std::vector<SuccessorFeatureEvaluation>&
        successor_feature_evaluations,
    std::vector<bellman::FeatureTargetRow>& feature_rows) {
    const probes::DecisionProbe& probe =
        manifest_root.probe;
    const GameState root_state =
        canonical_root_state(probe);
    const LearnedDecisionContext context =
        root_context(probe);
    const std::vector<PriorityAction> authoritative =
        legal_priority_actions(
            root_state, probe.root_player,
            context.sorcery_actions);
    const auto canonical_actions =
        authoritative_root_actions(
            probe, root_state, context);
    const information::InformationSetKey root_key =
        information::make_information_set_key(
            root_state, context, authoritative);
    const std::string canonical_information_fingerprint =
        information::information_set_sha256(root_key);

    Root root{
        .stable_id = probe.stable_id,
        .manifest_information_action_fingerprint =
            manifest_root.information_action_fingerprint,
        .canonical_information_set_fingerprint =
            canonical_information_fingerprint,
        .root_deck = probe.root_deck,
        .root_player = probe.root_player,
    };
    root.sampled_worlds = indexed_map(
        executor, recipe.root_worlds,
        [&](std::size_t world) {
            const std::uint64_t seed = indexed_seed(
                recipe.root_seed_base,
                information::SeedDomain::
                    RootDeterminization,
                probe.stable_id,
                manifest_root
                    .information_action_fingerprint,
                information::SeedBank::Root,
                bellman::kBlockCount, world);
            return RootSampledWorld{
                .world_index = world,
                .determinization_seed = seed,
                .state = sample_determinization(
                    root_state, probe.original_decks,
                    probe.root_player, seed),
            };
        });

    const std::size_t action_count =
        canonical_actions.size();
    const auto flat_transitions = indexed_map(
        executor, action_count * recipe.root_worlds,
        [&](std::size_t flat_index) {
            const std::size_t action_index =
                flat_index / recipe.root_worlds;
            const std::size_t world =
                flat_index % recipe.root_worlds;
            const CanonicalRootAction& candidate =
                canonical_actions[action_index];
            const RootSampledWorld& sampled =
                root.sampled_worlds[world];
            const std::uint64_t macro_seed =
                indexed_seed(
                    recipe.root_seed_base,
                    information::SeedDomain::
                        RootMacroTransition,
                    probe.stable_id,
                    manifest_root
                        .information_action_fingerprint,
                    information::SeedBank::Root,
                    bellman::kBlockCount, world);
            const LearnedPriorityMacroTransition transition =
                advance_learned_priority_macro_transition(
                    sampled.state, probe.original_decks,
                    probe.root_player,
                    context.sorcery_actions, context.phase,
                    context.consecutive_passes,
                    candidate.action, model, macro_seed);
            if (transition.disposition ==
                LearnedPriorityMacroDisposition::Incomplete) {
                throw std::runtime_error(
                    probe.stable_id +
                    ": FQ0 root macro-transition exhausted "
                    "a fixed bound");
            }
            RootTransition result{
                .world_index = world,
                .determinization_seed =
                    sampled.determinization_seed,
                .macro_seed = macro_seed,
                .terminal =
                    transition.disposition ==
                    LearnedPriorityMacroDisposition::Terminal,
                .successor_state = transition.state,
                .successor_context = transition.context,
                .successor_legal_actions =
                    transition.legal_actions,
                .actions_applied =
                    transition.actions_applied,
                .priority_actions_applied =
                    transition.priority_actions_applied,
                .phase_transitions =
                    transition.phase_transitions,
                .turn_advances =
                    transition.turn_advances,
                .forced_action_applied =
                    transition.actions_applied >= 1 &&
                    transition
                            .priority_actions_applied >=
                        1 &&
                    transition
                            .priority_actions_applied <=
                        transition.actions_applied,
            };
            if (!result.forced_action_applied) {
                throw std::logic_error(
                    "FQ0 root macro did not account for "
                    "its forced action");
            }
            if (result.terminal) {
                if (!transition.terminal_result.has_value()) {
                    throw std::logic_error(
                        "FQ0 terminal root transition lacks "
                        "a result");
                }
                result.terminal_root_owner_value =
                    information::terminal_root_owner_value(
                        *transition.terminal_result,
                        probe.root_player);
                result.redacted_result_hash =
                    information::
                        redacted_leaf_consequence_sha256(
                            transition.state,
                            probe.root_player, {},
                            transition.terminal_result);
            } else {
                if (transition.disposition !=
                        LearnedPriorityMacroDisposition::
                            PriorityBoundary ||
                    !transition.context.valid ||
                    transition.legal_actions.size() < 2 ||
                    transition.context.decision_player >=
                        transition.state.players.size()) {
                    throw std::logic_error(
                        "FQ0 nonterminal root transition did "
                        "not stop at a nontrivial Priority "
                        "boundary");
                }
                const auto authoritative_successor =
                    legal_priority_actions(
                        transition.state,
                        transition.context.decision_player,
                        transition.context.sorcery_actions);
                if (authoritative_successor !=
                    transition.legal_actions) {
                    throw std::logic_error(
                        "FQ0 macro-transition returned a stale "
                        "successor legal-action set");
                }
                const auto successor_key =
                    information::make_information_set_key(
                        transition.state,
                        transition.context,
                        transition.legal_actions);
                result
                    .successor_information_set_fingerprint =
                    information::information_set_sha256(
                        successor_key);
                result.successor_owner =
                    transition.context.decision_player;
                result.redacted_result_hash =
                    result
                        .successor_information_set_fingerprint;
            }
            return result;
        });

    const std::string root_information =
        "root/" + probe.stable_id + "/" +
        manifest_root.information_action_fingerprint;
    root.actions.reserve(action_count);
    for (std::size_t action_index = 0;
         action_index < action_count; ++action_index) {
        const CanonicalRootAction& candidate =
            canonical_actions[action_index];
        RootAction action{
            .descriptor = candidate.descriptor,
            .action = candidate.action,
            .feature_row_id = feature_row_id(
                root_information, candidate.descriptor),
            .policy_features =
                learned_priority_policy_features(
                    root_state, probe.root_player,
                    candidate.action,
                    context.sorcery_actions, context.phase,
                    context.consecutive_passes),
            .canonical_consequence_fingerprint =
                information::
                    canonical_priority_consequence_sha256(
                        root_state, probe.root_player,
                        context, candidate.action),
        };
        const std::size_t offset =
            action_index * recipe.root_worlds;
        action.root_transitions.reserve(
            recipe.root_worlds);
        for (std::size_t world = 0;
             world < recipe.root_worlds; ++world) {
            action.root_transitions.push_back(
                flat_transitions[offset + world]);
        }
        root.actions.push_back(std::move(action));
    }

    std::map<std::string, Representative>
        representatives;
    std::map<
        std::string,
        std::vector<
            SuccessorFeatureEvaluation::Member>>
        members_by_information_set;
    for (const RootAction& action : root.actions) {
        for (const RootTransition& transition :
             action.root_transitions) {
            if (transition.terminal) {
                continue;
            }
            const information::InformationSetKey key =
                information::make_information_set_key(
                    transition.successor_state,
                    transition.successor_context,
                    transition.successor_legal_actions);
            const std::string fingerprint =
                information::information_set_sha256(key);
            if (fingerprint !=
                transition
                    .successor_information_set_fingerprint) {
                throw std::logic_error(
                    "FQ0 successor fingerprint changed during "
                    "representative selection");
            }
            members_by_information_set[fingerprint]
                .push_back({
                    .root_action_descriptor =
                        action.descriptor,
                    .root_world =
                        transition.world_index,
                });
            Representative candidate{
                .key = key,
                .state = transition.successor_state,
                .context = transition.successor_context,
                .legal_actions =
                    transition.successor_legal_actions,
                .owner = transition.successor_owner,
                .root_world = transition.world_index,
                .root_action_descriptor =
                    action.descriptor,
            };
            const auto found =
                representatives.find(fingerprint);
            if (found == representatives.end()) {
                representatives.emplace(
                    fingerprint, std::move(candidate));
                continue;
            }
            if (!(found->second.key == key) ||
                found->second.owner !=
                    candidate.owner) {
                throw std::logic_error(
                    "FQ0 information-set digest collision");
            }
            if (std::tie(
                    candidate.root_world,
                    candidate.root_action_descriptor) <
                std::tie(
                    found->second.root_world,
                    found->second
                        .root_action_descriptor)) {
                representatives.erase(found);
                representatives.emplace(
                    fingerprint, std::move(candidate));
            }
        }
    }
    for (auto& [fingerprint, members] :
         members_by_information_set) {
        static_cast<void>(fingerprint);
        std::sort(
            members.begin(), members.end(),
            [](const auto& first, const auto& second) {
                return std::tie(
                           first.root_world,
                           first.root_action_descriptor) <
                       std::tie(
                           second.root_world,
                           second.root_action_descriptor);
            });
        if (std::adjacent_find(
                members.begin(), members.end()) !=
            members.end()) {
            throw std::logic_error(
                "FQ0 successor member catalog repeats a "
                "root transition");
        }
    }

    BankCache bank_cache;
    for (const auto& [fingerprint, representative] :
         representatives) {
        bank_cache.emplace(
            BankCacheKey{
                fingerprint, ScopeKind::Full, 0},
            evaluate_bank_pair(
                recipe, executor, manifest_root,
                representative, fingerprint,
                ScopeKind::Full, 0, model));
        for (std::size_t block = 0;
             block < bellman::kBlockCount; ++block) {
            bank_cache.emplace(
                BankCacheKey{
                    fingerprint, ScopeKind::Block,
                    block},
                evaluate_bank_pair(
                    recipe, executor, manifest_root,
                    representative, fingerprint,
                    ScopeKind::Block, block, model));
        }
    }

    for (RootAction& action : root.actions) {
        action.scopes.reserve(
            bellman::kBlockCount + 1);
        action.scopes.push_back(build_scope(
            recipe, action, probe.root_player,
            ScopeKind::Full, 0, bank_cache));
        action.target.full =
            action.scopes.front().target.value;
        for (std::size_t block = 0;
             block < bellman::kBlockCount; ++block) {
            action.scopes.push_back(build_scope(
                recipe, action, probe.root_player,
                ScopeKind::Block, block, bank_cache));
            action.target.blocks[block] =
                action.scopes.back().target.value;
        }
    }

    root.exact_support = bellman::exact_max_support(
        root_action_means(root.actions));
    const bool unique_root_max =
        root.exact_support.size() == 1;
    for (const RootAction& action : root.actions) {
        feature_rows.push_back({
            .row_id = action.feature_row_id,
            .information_set_id =
                root_information,
            .legal_set_id =
                legal_set_id(root_information),
            .common_world_key =
                common_world_key(
                    probe.stable_id,
                    root_information, true),
            .action_descriptor =
                action.descriptor,
            .features = action.policy_features,
            .canonical_consequence_fingerprint =
                action
                    .canonical_consequence_fingerprint,
            .target = action.target,
            .unique_exact_max =
                unique_root_max &&
                action.descriptor ==
                    root.exact_support.front(),
        });
    }

    for (const auto& [fingerprint, representative] :
         representatives) {
        SuccessorFeatureEvaluation evaluation{
            .root_stable_id = probe.stable_id,
            .information_set_fingerprint = fingerprint,
            .successor_owner = representative.owner,
            .representative_root_world =
                representative.root_world,
            .representative_root_action_descriptor =
                representative.root_action_descriptor,
            .members =
                members_by_information_set.at(
                    fingerprint),
        };
        std::map<std::string, bellman::TargetBlocks>
            targets_by_action;
        evaluation.scopes.reserve(
            bellman::kBlockCount + 1);
        const auto append_scope =
            [&](ScopeKind kind, std::size_t block) {
                const BankPair& pair =
                    cached_bank_pair(
                        bank_cache, fingerprint,
                        kind, block);
                evaluation.scopes.push_back({
                    .kind = kind,
                    .block = block,
                    .bank_a = pair.bank_a,
                    .bank_b = pair.bank_b,
                });
                const auto targets =
                    symmetric_action_targets(pair);
                for (const auto& [descriptor, target] :
                     targets) {
                    bellman::TargetBlocks& row =
                        targets_by_action[descriptor];
                    if (kind == ScopeKind::Full) {
                        row.full = target;
                    } else {
                        row.blocks.at(block) = target;
                    }
                }
            };
        append_scope(ScopeKind::Full, 0);
        for (std::size_t block = 0;
             block < bellman::kBlockCount; ++block) {
            append_scope(ScopeKind::Block, block);
        }

        const GroupBank& full_bank =
            evaluation.scopes.front().bank_a;
        std::vector<bellman::ActionMean>
            feature_means;
        feature_means.reserve(targets_by_action.size());
        for (const auto& [descriptor, target] :
             targets_by_action) {
            feature_means.push_back({
                .descriptor = descriptor,
                .value = target.full,
            });
        }
        const std::vector<std::string> support =
            bellman::exact_max_support(feature_means);
        const bool unique_max = support.size() == 1;
        const std::string scoped_information =
            scoped_successor_information_set(
                probe.stable_id, fingerprint);
        for (const GroupAction& action :
             full_bank.actions) {
            const auto target =
                targets_by_action.find(
                    action.descriptor);
            if (target == targets_by_action.end()) {
                throw std::logic_error(
                    "FQ0 successor feature target is "
                    "missing an action");
            }
            feature_rows.push_back({
                .row_id = action.feature_row_id,
                .information_set_id =
                    scoped_information,
                .legal_set_id =
                    legal_set_id(scoped_information),
                .common_world_key =
                    common_world_key(
                        probe.stable_id,
                        scoped_information, false),
                .action_descriptor =
                    action.descriptor,
                .features = action.policy_features,
                .canonical_consequence_fingerprint =
                    action
                        .canonical_consequence_fingerprint,
                .target = target->second,
                .unique_exact_max =
                    unique_max &&
                    action.descriptor ==
                        support.front(),
            });
        }
        successor_feature_evaluations.push_back(
            std::move(evaluation));
    }
    return root;
}

void validate_recipe(const Recipe& recipe) {
    if (recipe.root_seed_base == 0 ||
        recipe.bank_a_seed_base == 0 ||
        recipe.bank_b_seed_base == 0 ||
        recipe.root_seed_base ==
            recipe.bank_a_seed_base ||
        recipe.root_seed_base ==
            recipe.bank_b_seed_base ||
        recipe.bank_a_seed_base ==
            recipe.bank_b_seed_base ||
        recipe.root_worlds <
            bellman::kBlockCount ||
        recipe.root_worlds %
                bellman::kBlockCount !=
            0 ||
        recipe.successor_worlds == 0 ||
        recipe.workers == 0) {
        throw std::invalid_argument(
            "FQ0 construction recipe is invalid");
    }
}

void validate_manifest_basics(
    const ac1_teacher_audit::Manifest& manifest) {
    if (manifest.roots.empty()) {
        throw std::invalid_argument(
            "FQ0 construction manifest is empty");
    }
    std::set<std::string> stable_ids;
    for (const ac1_teacher_audit::ManifestRoot& root :
         manifest.roots) {
        if (root.probe.stable_id.empty() ||
            root.information_action_fingerprint.empty() ||
            !stable_ids.insert(root.probe.stable_id).second ||
            probes::bsr_information_action_fingerprint(
                root.probe) !=
                root.information_action_fingerprint) {
            throw std::invalid_argument(
                "FQ0 manifest root identity is invalid");
        }
    }
}

void validate_production_inputs(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model) {
    const ac1_teacher_audit::Manifest canonical =
        ac1_teacher_audit::build_manifest();
    if (manifest != canonical ||
        !manifest.exact ||
        manifest.roots.size() !=
            ac1_teacher_audit::
                kPhysicalPriorityRoots ||
        manifest.physical_roots_by_deck !=
            std::array<std::size_t, kDeckCount>{
                6, 4, 8, 4, 4}) {
        throw std::invalid_argument(
            "FQ0 production manifest is not the frozen "
            "26-root five-deck manifest");
    }
    if (!model ||
        learned_model_fingerprint(model) !=
            kProductionModelFingerprint) {
        throw std::invalid_argument(
            "FQ0 production model is not frozen C16");
    }
}

void validate_testing_recipe(
    const testing::ReducedRecipe& recipe) {
    const std::array<std::uint64_t, 3> supplied = {
        recipe.root_seed_base,
        recipe.bank_a_seed_base,
        recipe.bank_b_seed_base,
    };
    const std::array<std::uint64_t, 3> reserved = {
        kProductionRootSeedBase,
        kProductionBankASeedBase,
        kProductionBankBSeedBase,
    };
    for (const std::uint64_t seed : supplied) {
        if (std::find(
                reserved.begin(), reserved.end(),
                seed) != reserved.end()) {
            throw std::invalid_argument(
                "FQ0 reduced recipe uses a reserved "
                "scientific seed");
        }
    }
    if (recipe.root_worlds ==
            kProductionRootWorlds &&
        recipe.successor_worlds ==
            kProductionSuccessorWorlds &&
        recipe.workers ==
            kProductionWorkers) {
        throw std::invalid_argument(
            "FQ0 exact production scale cannot enter the "
            "testing recipe API");
    }
}

class DigestWriter {
  public:
    void text(std::string_view value) {
        integer(value.size());
        append(std::as_bytes(std::span(value)));
    }

    template <typename Integer>
    void integer(Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        std::uint64_t bits = static_cast<std::uint64_t>(
            static_cast<Unsigned>(value));
        std::array<std::byte, sizeof(Unsigned)> encoded{};
        for (std::size_t index = 0;
             index < sizeof(Unsigned); ++index) {
            encoded[index] = static_cast<std::byte>(
                bits & std::uint64_t{0xff});
            bits >>= 8U;
        }
        append(encoded);
    }

    void boolean(bool value) {
        integer<std::uint8_t>(value ? 1 : 0);
    }

    void real(double value) {
        integer(std::bit_cast<std::uint64_t>(value));
    }

    std::string sha256() {
        flush();
        return hash_.finish();
    }

  private:
    void append(std::span<const std::byte> bytes) {
        while (!bytes.empty()) {
            const std::size_t count = std::min(
                bytes.size(), buffer_.size() - buffer_size_);
            std::memcpy(
                buffer_.data() + buffer_size_,
                bytes.data(), count);
            buffer_size_ += count;
            bytes = bytes.subspan(count);
            if (buffer_size_ == buffer_.size()) {
                flush();
            }
        }
    }

    void flush() {
        if (buffer_size_ == 0) {
            return;
        }
        hash_.update(std::span<const std::byte>(
            buffer_.data(), buffer_size_));
        buffer_size_ = 0;
    }

    artifact_integrity::Sha256Accumulator hash_;
    std::array<std::byte, 64 * 1024> buffer_{};
    std::size_t buffer_size_ = 0;
};

void append_action(
    DigestWriter& writer, const PriorityAction& action) {
    writer.text(
        probes::stable_priority_action_descriptor(action));
}

void append_features(
    DigestWriter& writer,
    std::span<const double> features) {
    writer.integer(features.size());
    for (const double feature : features) {
        writer.real(feature);
    }
}

void append_target(
    DigestWriter& writer,
    const bellman::TargetBlocks& target) {
    writer.real(target.full);
    for (const double block : target.blocks) {
        writer.real(block);
    }
}

void append_bank(
    DigestWriter& writer, const GroupBank& bank,
    bool include_canonical_consequence) {
    writer.text(bank.bank);
    writer.text(bank.stream_key);
    writer.integer(bank.actions.size());
    for (const GroupAction& action : bank.actions) {
        writer.text(action.descriptor);
        append_action(writer, action.action);
        writer.text(action.feature_row_id);
        append_features(writer, action.policy_features);
        if (include_canonical_consequence) {
            writer.text(
                action.canonical_consequence_fingerprint);
        }
        writer.integer(action.samples.size());
        for (const LeafSample& sample : action.samples) {
            writer.integer(sample.world_index);
            writer.integer(sample.determinization_seed);
            writer.integer(sample.macro_seed);
            writer.real(sample.score);
            writer.integer(
                sample.contextual_score_bits);
            writer.integer(sample.legacy_score_bits);
            writer.text(sample.redacted_leaf_hash);
            writer.boolean(sample.terminal);
            writer.boolean(sample.critic_evaluated);
            writer.boolean(
                sample
                    .contextual_legacy_critic_bit_identical);
            writer.integer(sample.actions_applied);
            writer.integer(
                sample.priority_actions_applied);
            writer.integer(sample.phase_transitions);
            writer.integer(sample.turn_advances);
            writer.boolean(sample.forced_action_applied);
        }
    }
}

void append_bank(
    DigestWriter& writer, const GroupBank& bank) {
    append_bank(writer, bank, true);
}

void append_cross_fit(
    DigestWriter& writer,
    const bellman::CrossFitValue& cross_fit) {
    const auto append_means =
        [&](std::span<const bellman::ActionMean> means) {
            writer.integer(means.size());
            for (const bellman::ActionMean& mean : means) {
                writer.text(mean.descriptor);
                writer.real(mean.value);
            }
        };
    const auto append_support =
        [&](std::span<const std::string> support) {
            writer.integer(support.size());
            for (const std::string& descriptor : support) {
                writer.text(descriptor);
            }
        };
    append_means(cross_fit.bank_a);
    append_means(cross_fit.bank_b);
    append_support(cross_fit.support_a);
    append_support(cross_fit.support_b);
    writer.real(cross_fit.a_selected_b_value);
    writer.real(cross_fit.b_selected_a_value);
    writer.real(cross_fit.value);
}

std::string bank_pair_sha256(const BankPair& pair) {
    DigestWriter writer;
    writer.text(
        "old-school-fq0-successor-bank-pair-v1");
    append_bank(writer, pair.bank_a);
    append_bank(writer, pair.bank_b);
    append_cross_fit(writer, pair.cross_fit);
    return writer.sha256();
}

std::string operator_bank_pair_sha256(
    const BankPair& pair) {
    DigestWriter writer;
    writer.text(
        "old-school-fq0-successor-operator-bank-pair-v2");
    append_bank(writer, pair.bank_a, false);
    append_bank(writer, pair.bank_b, false);
    append_cross_fit(writer, pair.cross_fit);
    return writer.sha256();
}

struct EmpiricalGroupRepresentativeCoordinate {
    std::string root_action_descriptor;
    std::size_t root_world = 0;

    bool operator==(
        const EmpiricalGroupRepresentativeCoordinate&) const = default;
};

Representative transition_representative(
    const RootTransition& transition,
    std::string_view root_action_descriptor,
    std::string_view required_fingerprint,
    std::optional<GameState> state_override =
        std::nullopt) {
    if (transition.terminal ||
        transition.successor_context.decision_player >=
            transition.successor_state.players.size()) {
        throw std::invalid_argument(
            "FQ0 reconstruction requires a nonterminal "
            "successor transition");
    }
    GameState state =
        state_override.has_value()
            ? std::move(*state_override)
            : transition.successor_state;
    const information::InformationSetKey key =
        information::make_information_set_key(
            state, transition.successor_context,
            transition.successor_legal_actions);
    if (information::information_set_sha256(key) !=
            required_fingerprint ||
        transition.successor_owner !=
            transition.successor_context.decision_player) {
        throw std::logic_error(
            "FQ0 reconstruction state changed its "
            "information-set identity");
    }
    return {
        .key = key,
        .state = std::move(state),
        .context = transition.successor_context,
        .legal_actions =
            transition.successor_legal_actions,
        .owner = transition.successor_owner,
        .root_world = transition.world_index,
        .root_action_descriptor =
            std::string(root_action_descriptor),
    };
}

GroupReconstructionWitnesses reconstruct_group_impl(
    const Recipe& recipe,
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const RootAction& root_action, const Scope& scope,
    const SuccessorGroup& group,
    std::shared_ptr<const LearnedModel> model) {
    validate_recipe(recipe);
    if (!model ||
        learned_critic_schema(model) !=
            LearnedCriticSchema::LegacyStateOnly ||
        group.root_world_indices.empty() ||
        root_action.root_transitions.size() !=
            recipe.root_worlds ||
        scope.kind !=
            (scope.root_world_indices.size() ==
                     recipe.root_worlds
                 ? ScopeKind::Full
                 : ScopeKind::Block) ||
        (scope.kind == ScopeKind::Block &&
         scope.block >= bellman::kBlockCount)) {
        throw std::invalid_argument(
            "FQ0 group reconstruction input is invalid");
    }
    const BankPair baseline{
        .bank_a = group.bank_a,
        .bank_b = group.bank_b,
        .cross_fit = group.cross_fit,
    };
    const std::string baseline_sha256 =
        operator_bank_pair_sha256(baseline);

    GroupReconstructionWitnesses result;
    IndexedExecutor executor(
        recipe.workers, result.execution);
    result.representatives.reserve(
        group.root_world_indices.size());
    for (const std::size_t world :
         group.root_world_indices) {
        if (!std::binary_search(
                scope.root_world_indices.begin(),
                scope.root_world_indices.end(), world) ||
            world >= root_action.root_transitions.size()) {
            throw std::invalid_argument(
                "FQ0 reconstruction group member escaped "
                "its root scope");
        }
        const RootTransition& transition =
            root_action.root_transitions[world];
        if (transition
                .successor_information_set_fingerprint !=
                group.information_set_fingerprint ||
            transition.successor_owner !=
                group.successor_owner) {
            throw std::invalid_argument(
                "FQ0 reconstruction group membership "
                "disagrees with the root transition");
        }
        const Representative representative =
            transition_representative(
                transition, root_action.descriptor,
                group.information_set_fingerprint);
        const BankPair comparison =
            evaluate_bank_pair(
                recipe, executor, manifest_root,
                representative,
                group.information_set_fingerprint,
                scope.kind, scope.block, model);
        result.representatives.push_back({
            .root_action_descriptor =
                root_action.descriptor,
            .root_world = world,
            .identity = {
                .baseline_sha256 = baseline_sha256,
                .comparison_sha256 =
                    operator_bank_pair_sha256(comparison),
            },
        });
    }
    result.every_representative_bit_identical =
        std::all_of(
            result.representatives.begin(),
            result.representatives.end(),
            [](const RepresentativeReconstructionWitness&
                   witness) {
                return witness.identity.bit_identical();
            });

    if (group.representative_root_world >=
        root_action.root_transitions.size()) {
        throw std::invalid_argument(
            "FQ0 reconstruction representative is out of range");
    }
    const RootTransition& representative_transition =
        root_action.root_transitions[
            group.representative_root_world];
    const HiddenClone hidden =
        hidden_repartition_clone(
            representative_transition.successor_state,
            group.successor_owner);
    result.hidden_repartition_eligible =
        hidden.eligible;
    result.hidden_repartition_changed =
        hidden.changed;
    const Representative hidden_representative =
        transition_representative(
            representative_transition,
            root_action.descriptor,
            group.information_set_fingerprint,
            hidden.state);
    const BankPair hidden_comparison =
        evaluate_bank_pair(
            recipe, executor, manifest_root,
            hidden_representative,
            group.information_set_fingerprint,
            scope.kind, scope.block, model);
    result.hidden_repartition = {
        .root_action_descriptor =
            root_action.descriptor,
        .root_world =
            group.representative_root_world,
        .identity = {
            .baseline_sha256 = baseline_sha256,
            .comparison_sha256 =
                operator_bank_pair_sha256(
                    hidden_comparison),
        },
    };
    result.hidden_repartition_bit_identical =
        result.hidden_repartition.identity.bit_identical();
    if (result.hidden_repartition_eligible &&
        (!result.hidden_repartition_changed ||
         !result.hidden_repartition_bit_identical)) {
        throw std::logic_error(
            "FQ0 eligible hidden repartition did not "
            "change and reconstruct bit-identically");
    }
    return result;
}

const RootAction& find_root_action(
    const Root& root, std::string_view descriptor) {
    const auto found = std::lower_bound(
        root.actions.begin(), root.actions.end(), descriptor,
        [](const RootAction& action, std::string_view key) {
            return action.descriptor < key;
        });
    if (found == root.actions.end() ||
        found->descriptor != descriptor) {
        throw std::invalid_argument(
            "FQ0 feature reconstruction member refers to "
            "a missing root action");
    }
    return *found;
}

std::vector<EmpiricalGroupRepresentativeCoordinate>
empirical_group_representatives_for_scope(
    const Root& root,
    const SuccessorFeatureEvaluation& evaluation,
    const SuccessorFeatureScope& feature_scope) {
    std::vector<EmpiricalGroupRepresentativeCoordinate>
        result;
    for (const RootAction& action : root.actions) {
        const auto scope = std::find_if(
            action.scopes.begin(), action.scopes.end(),
            [&](const Scope& candidate) {
                return candidate.kind ==
                           feature_scope.kind &&
                       candidate.block ==
                           feature_scope.block;
            });
        if (scope == action.scopes.end()) {
            throw std::invalid_argument(
                "FQ0 root action lacks a requested feature "
                "scope");
        }
        for (const SuccessorGroup& group :
             scope->groups) {
            if (group.information_set_fingerprint !=
                evaluation.information_set_fingerprint) {
                continue;
            }
            if (group
                    .representative_root_action_descriptor !=
                    action.descriptor ||
                group.representative_root_world >=
                    action.root_transitions.size()) {
                throw std::invalid_argument(
                    "FQ0 empirical group representative "
                    "coordinate is invalid");
            }
            result.push_back({
                .root_action_descriptor =
                    action.descriptor,
                .root_world =
                    group.representative_root_world,
            });
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const auto& first, const auto& second) {
            return std::tie(
                       first.root_world,
                       first.root_action_descriptor) <
                   std::tie(
                       second.root_world,
                       second.root_action_descriptor);
        });
    result.erase(
        std::unique(result.begin(), result.end()),
        result.end());
    return result;
}

GroupReconstructionWitnesses
reconstruct_feature_scope_impl(
    const Recipe& recipe,
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const Root& root,
    const SuccessorFeatureEvaluation& evaluation,
    const SuccessorFeatureScope& scope,
    std::span<
        const EmpiricalGroupRepresentativeCoordinate>
        empirical_group_representatives,
    std::shared_ptr<const LearnedModel> model) {
    validate_recipe(recipe);
    if (!model ||
        learned_critic_schema(model) !=
            LearnedCriticSchema::LegacyStateOnly ||
        root.stable_id != manifest_root.probe.stable_id ||
        evaluation.root_stable_id != root.stable_id ||
        evaluation.members.empty() ||
        evaluation.successor_owner >=
            manifest_root.probe.state.players.size() ||
        (scope.kind == ScopeKind::Block &&
         scope.block >= bellman::kBlockCount) ||
        scope.bank_a.actions.empty() ||
        scope.bank_a.actions.size() !=
            scope.bank_b.actions.size()) {
        throw std::invalid_argument(
            "FQ0 feature-scope reconstruction input is "
            "invalid");
    }
    const BankPair baseline{
        .bank_a = scope.bank_a,
        .bank_b = scope.bank_b,
        .cross_fit = bellman::cross_fit_v0(
            action_samples(scope.bank_a),
            action_samples(scope.bank_b)),
    };
    const std::string baseline_sha256 =
        operator_bank_pair_sha256(baseline);

    GroupReconstructionWitnesses result;
    IndexedExecutor executor(
        recipe.workers, result.execution);
    result.representatives.reserve(
        evaluation.members.size());
    std::optional<
        SuccessorFeatureEvaluation::Member>
        previous;
    for (const auto& member : evaluation.members) {
        if (previous.has_value() &&
            std::tie(
                previous->root_world,
                previous->root_action_descriptor) >=
                std::tie(
                    member.root_world,
                    member.root_action_descriptor)) {
            throw std::invalid_argument(
                "FQ0 feature member catalog is not "
                "canonical and unique");
        }
        previous = member;
        const RootAction& action =
            find_root_action(
                root,
                member.root_action_descriptor);
        if (action.root_transitions.size() !=
                recipe.root_worlds ||
            member.root_world >=
                action.root_transitions.size()) {
            throw std::invalid_argument(
                "FQ0 feature member world is out of range");
        }
        const RootTransition& transition =
            action.root_transitions[
                member.root_world];
        if (transition.terminal ||
            transition
                    .successor_information_set_fingerprint !=
                evaluation
                    .information_set_fingerprint ||
            transition.successor_owner !=
                evaluation.successor_owner) {
            throw std::invalid_argument(
                "FQ0 feature member catalog disagrees "
                "with its transition");
        }
        const Representative representative =
            transition_representative(
                transition, action.descriptor,
                evaluation
                    .information_set_fingerprint);
        const BankPair comparison =
            evaluate_bank_pair(
                recipe, executor, manifest_root,
                representative,
                evaluation
                    .information_set_fingerprint,
                scope.kind, scope.block, model);
        result.representatives.push_back({
            .root_action_descriptor =
                action.descriptor,
            .root_world = member.root_world,
            .identity = {
                .baseline_sha256 = baseline_sha256,
                .comparison_sha256 =
                    operator_bank_pair_sha256(comparison),
            },
        });
    }
    result.every_representative_bit_identical =
        std::all_of(
            result.representatives.begin(),
            result.representatives.end(),
            [](const RepresentativeReconstructionWitness&
                   witness) {
                return witness.identity.bit_identical();
            });

    const RootAction& canonical_action =
        find_root_action(
            root,
            evaluation
                .representative_root_action_descriptor);
    if (evaluation.representative_root_world >=
        canonical_action.root_transitions.size()) {
        throw std::invalid_argument(
            "FQ0 feature hidden representative is out "
            "of range");
    }
    const RootTransition& canonical_transition =
        canonical_action.root_transitions[
            evaluation.representative_root_world];
    if (canonical_transition
            .successor_information_set_fingerprint !=
        evaluation.information_set_fingerprint) {
        throw std::invalid_argument(
            "FQ0 feature hidden representative changed "
            "information sets");
    }
    const HiddenClone hidden =
        hidden_repartition_clone(
            canonical_transition.successor_state,
            evaluation.successor_owner);
    result.hidden_repartition_eligible =
        hidden.eligible;
    result.hidden_repartition_changed =
        hidden.changed;
    const Representative hidden_representative =
        transition_representative(
            canonical_transition,
            canonical_action.descriptor,
            evaluation
                .information_set_fingerprint,
            hidden.state);
    const BankPair hidden_comparison =
        evaluate_bank_pair(
            recipe, executor, manifest_root,
            hidden_representative,
            evaluation
                .information_set_fingerprint,
            scope.kind, scope.block, model);
    result.hidden_repartition = {
        .root_action_descriptor =
            canonical_action.descriptor,
        .root_world =
            evaluation.representative_root_world,
        .identity = {
            .baseline_sha256 = baseline_sha256,
            .comparison_sha256 =
                operator_bank_pair_sha256(
                    hidden_comparison),
        },
    };
    result.hidden_repartition_bit_identical =
        result.hidden_repartition.identity.bit_identical();
    if (result.hidden_repartition_eligible &&
        (!result.hidden_repartition_changed ||
         !result.hidden_repartition_bit_identical)) {
        throw std::logic_error(
            "FQ0 eligible feature hidden repartition did "
            "not change and reconstruct bit-identically");
    }

    result.empirical_group_hidden_repartitions.reserve(
        empirical_group_representatives.size());
    for (const auto& empirical :
         empirical_group_representatives) {
        const RootAction& action =
            find_root_action(
                root,
                empirical.root_action_descriptor);
        if (empirical.root_world >=
            action.root_transitions.size()) {
            throw std::invalid_argument(
                "FQ0 empirical hidden representative is "
                "out of range");
        }
        const RootTransition& transition =
            action.root_transitions[
                empirical.root_world];
        if (transition.terminal ||
            transition
                    .successor_information_set_fingerprint !=
                evaluation
                    .information_set_fingerprint ||
            transition.successor_owner !=
                evaluation.successor_owner) {
            throw std::invalid_argument(
                "FQ0 empirical hidden representative is "
                "not a member");
        }
        const HiddenClone empirical_hidden =
            hidden_repartition_clone(
                transition.successor_state,
                evaluation.successor_owner);
        const Representative empirical_representative =
            transition_representative(
                transition, action.descriptor,
                evaluation
                    .information_set_fingerprint,
                empirical_hidden.state);
        const BankPair empirical_comparison =
            evaluate_bank_pair(
                recipe, executor, manifest_root,
                empirical_representative,
                evaluation
                    .information_set_fingerprint,
                scope.kind, scope.block, model);
        HiddenRepartitionReconstructionWitness witness{
            .representative = {
                .root_action_descriptor =
                    action.descriptor,
                .root_world =
                    empirical.root_world,
                .identity = {
                    .baseline_sha256 =
                        baseline_sha256,
                    .comparison_sha256 =
                        operator_bank_pair_sha256(
                            empirical_comparison),
                },
            },
            .eligible = empirical_hidden.eligible,
            .changed = empirical_hidden.changed,
        };
        witness.bit_identical =
            witness.representative.identity.bit_identical();
        if (!witness.bit_identical ||
            (witness.eligible && !witness.changed) ||
            (!witness.eligible && witness.changed)) {
            throw std::logic_error(
                "FQ0 empirical-group hidden repartition did "
                "not reconstruct truthfully");
        }
        result.empirical_group_hidden_repartitions.push_back(
            std::move(witness));
    }
    return result;
}

std::string semantic_sha256_impl(
    const Construction& construction) {
    DigestWriter writer;
    writer.text("old-school-fq0-bellman-core-v1");
    writer.text(construction.model_fingerprint);
    writer.integer(construction.roots.size());
    for (const Root& root : construction.roots) {
        writer.text(root.stable_id);
        writer.text(
            root.manifest_information_action_fingerprint);
        writer.text(
            root.canonical_information_set_fingerprint);
        writer.integer(
            static_cast<std::uint8_t>(root.root_deck));
        writer.integer(root.root_player);
        writer.integer(root.sampled_worlds.size());
        for (const RootSampledWorld& world :
             root.sampled_worlds) {
            writer.integer(world.world_index);
            writer.integer(world.determinization_seed);
        }
        writer.integer(root.actions.size());
        for (const RootAction& action : root.actions) {
            writer.text(action.descriptor);
            append_action(writer, action.action);
            writer.text(action.feature_row_id);
            append_target(writer, action.target);
            append_features(writer, action.policy_features);
            writer.text(
                action
                    .canonical_consequence_fingerprint);
            writer.integer(action.root_transitions.size());
            for (const RootTransition& transition :
                 action.root_transitions) {
                writer.integer(transition.world_index);
                writer.integer(
                    transition.determinization_seed);
                writer.integer(transition.macro_seed);
                writer.text(
                    transition.redacted_result_hash);
                writer.boolean(transition.terminal);
                writer.real(
                    transition
                        .terminal_root_owner_value);
                writer.text(
                    transition
                        .successor_information_set_fingerprint);
                writer.integer(transition.successor_owner);
                writer.integer(
                    transition.actions_applied);
                writer.integer(
                    transition
                        .priority_actions_applied);
                writer.integer(
                    transition.phase_transitions);
                writer.integer(
                    transition.turn_advances);
                writer.boolean(
                    transition.forced_action_applied);
            }
            writer.integer(action.scopes.size());
            for (const Scope& scope : action.scopes) {
                writer.integer(
                    static_cast<std::uint8_t>(
                        scope.kind));
                writer.integer(scope.block);
                writer.integer(
                    scope.root_world_indices.size());
                for (const std::size_t world :
                     scope.root_world_indices) {
                    writer.integer(world);
                }
                writer.integer(scope.terminals.size());
                for (const auto& terminal :
                     scope.terminals) {
                    writer.integer(
                        terminal.world_index);
                    writer.real(
                        terminal.root_owner_value);
                }
                writer.integer(scope.groups.size());
                for (const SuccessorGroup& group :
                     scope.groups) {
                    writer.text(
                        group
                            .information_set_fingerprint);
                    writer.integer(
                        group.successor_owner);
                    writer.integer(
                        static_cast<std::uint8_t>(
                            group.relation));
                    writer.integer(
                        group.root_world_indices.size());
                    for (const std::size_t world :
                         group.root_world_indices) {
                        writer.integer(world);
                    }
                    writer.integer(
                        group
                            .representative_root_world);
                    writer.text(
                        group
                            .representative_root_action_descriptor);
                    append_bank(writer, group.bank_a);
                    append_bank(writer, group.bank_b);
                    append_cross_fit(
                        writer, group.cross_fit);
                }
                writer.real(scope.target.value);
                writer.integer(scope.target.particles);
                writer.integer(
                    scope.target.terminal_particles);
                writer.integer(
                    scope.target.same_owner_particles);
                writer.integer(
                    scope.target.opponent_owner_particles);
                writer.boolean(
                    scope.exact_particle_partition);
            }
        }
        writer.integer(root.exact_support.size());
        for (const std::string& descriptor :
             root.exact_support) {
            writer.text(descriptor);
        }
    }

    writer.integer(
        construction
            .successor_feature_evaluations.size());
    for (const SuccessorFeatureEvaluation& evaluation :
         construction.successor_feature_evaluations) {
        writer.text(evaluation.root_stable_id);
        writer.text(
            evaluation
                .information_set_fingerprint);
        writer.integer(evaluation.successor_owner);
        writer.integer(
            evaluation.representative_root_world);
        writer.text(
            evaluation
                .representative_root_action_descriptor);
        writer.integer(evaluation.members.size());
        for (const auto& member : evaluation.members) {
            writer.text(member.root_action_descriptor);
            writer.integer(member.root_world);
        }
        writer.integer(evaluation.scopes.size());
        for (const SuccessorFeatureScope& scope :
             evaluation.scopes) {
            writer.integer(
                static_cast<std::uint8_t>(scope.kind));
            writer.integer(scope.block);
            append_bank(writer, scope.bank_a);
            append_bank(writer, scope.bank_b);
        }
    }

    writer.integer(construction.feature_rows.size());
    for (const bellman::FeatureTargetRow& row :
         construction.feature_rows) {
        writer.text(row.row_id);
        writer.text(row.information_set_id);
        writer.text(row.legal_set_id);
        writer.text(row.common_world_key);
        writer.text(row.action_descriptor);
        append_features(writer, row.features);
        writer.text(
            row.canonical_consequence_fingerprint);
        append_target(writer, row.target);
        writer.boolean(row.unique_exact_max);
    }
    writer.integer(
        construction.feature_collisions.rows);
    writer.integer(
        construction.feature_collisions
            .colliding_feature_classes);
    writer.integer(
        construction.feature_collisions
            .harmful_collisions);
    writer.boolean(
        construction.feature_collisions.passed);
    writer.integer(
        construction.feature_collisions
            .collisions.size());
    for (const bellman::FeatureCollision& collision :
         construction.feature_collisions.collisions) {
        writer.text(collision.first_row_id);
        writer.text(collision.second_row_id);
        writer.integer(
            static_cast<std::uint8_t>(
                collision.target_method));
        writer.real(
            collision.target_separation_lower_95);
        writer.boolean(collision.consequence_conflict);
        writer.boolean(collision.target_conflict);
        writer.boolean(collision.support_conflict);
        writer.boolean(collision.harmful);
    }
    for (const std::size_t roots :
         construction.roots_by_deck) {
        writer.integer(roots);
    }
    return writer.sha256();
}

void validate_completed_transition_accounting(
    std::size_t actions_applied,
    std::size_t priority_actions_applied,
    std::size_t phase_transitions,
    std::size_t turn_advances, bool forced_action_applied,
    std::string_view coordinate) {
    if (!forced_action_applied || actions_applied < 1 ||
        priority_actions_applied < 1 ||
        priority_actions_applied > actions_applied ||
        actions_applied >
            kLearnedPriorityMacroActionBound ||
        phase_transitions >
            kLearnedPriorityMacroPhaseTransitionBound ||
        turn_advances >
            kLearnedPriorityMacroTurnAdvanceBound) {
        throw std::invalid_argument(
            std::string(coordinate) +
            ": invalid completed macro-transition accounting");
    }
}

void validate_production_leaf_sample(
    const LeafSample& sample, std::size_t expected_world,
    std::uint64_t expected_determinization_seed,
    std::uint64_t expected_macro_seed,
    std::string_view coordinate) {
    if (sample.world_index != expected_world ||
        sample.determinization_seed !=
            expected_determinization_seed ||
        sample.macro_seed != expected_macro_seed ||
        sample.redacted_leaf_hash.empty()) {
        throw std::invalid_argument(
            std::string(coordinate) +
            ": leaf sample provenance is invalid");
    }
    validate_completed_transition_accounting(
        sample.actions_applied,
        sample.priority_actions_applied,
        sample.phase_transitions, sample.turn_advances,
        sample.forced_action_applied, coordinate);
    require_probability(sample.score, coordinate);
    if (sample.terminal) {
        if ((sample.score != 0.0 &&
             sample.score != 0.5 &&
             sample.score != 1.0) ||
            sample.critic_evaluated ||
            sample.contextual_score_bits != 0 ||
            sample.legacy_score_bits != 0 ||
            sample
                .contextual_legacy_critic_bit_identical) {
            throw std::invalid_argument(
                std::string(coordinate) +
                ": terminal leaf critic evidence is invalid");
        }
        return;
    }
    const std::uint64_t score_bits =
        std::bit_cast<std::uint64_t>(sample.score);
    if (!sample.critic_evaluated ||
        sample.contextual_score_bits != score_bits ||
        sample.legacy_score_bits != score_bits ||
        !sample.contextual_legacy_critic_bit_identical) {
        throw std::invalid_argument(
            std::string(coordinate) +
            ": critic leaf bits are invalid");
    }
}

void validate_production_bank(
    const Recipe& recipe,
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const RootTransition& representative_transition,
    std::string_view information_set_fingerprint,
    ScopeKind kind, std::size_t block,
    information::SeedBank seed_bank,
    const GroupBank& bank) {
    if (representative_transition.terminal ||
        representative_transition
                .successor_information_set_fingerprint !=
            information_set_fingerprint ||
        representative_transition.successor_owner !=
            representative_transition
                .successor_context.decision_player) {
        throw std::invalid_argument(
            "FQ0 production bank representative is invalid");
    }
    const std::string expected_bank =
        seed_bank == information::SeedBank::A ? "A" : "B";
    const std::string expected_stream = stream_key(
        recipe, manifest_root.probe.stable_id,
        information_set_fingerprint, seed_bank, kind,
        block);
    if (bank.bank != expected_bank ||
        bank.stream_key != expected_stream) {
        throw std::invalid_argument(
            "FQ0 production bank stream provenance is invalid");
    }

    const information::InformationSetKey key =
        information::make_information_set_key(
            representative_transition.successor_state,
            representative_transition.successor_context,
            representative_transition
                .successor_legal_actions);
    if (information::information_set_sha256(key) !=
        information_set_fingerprint) {
        throw std::invalid_argument(
            "FQ0 production bank representative changed "
            "information sets");
    }
    const auto expected_actions =
        information::descriptor_canonical_action_rows(key);
    if (bank.actions.size() != expected_actions.size()) {
        throw std::invalid_argument(
            "FQ0 production bank action census is invalid");
    }
    const bool selection_bank =
        seed_bank == information::SeedBank::A;
    const std::uint64_t seed_base =
        selection_bank ? recipe.bank_a_seed_base
                       : recipe.bank_b_seed_base;
    const information::SeedDomain determinization_domain =
        selection_bank
            ? information::SeedDomain::
                  SuccessorSelectionDeterminization
            : information::SeedDomain::
                  SuccessorEvaluationDeterminization;
    const information::SeedDomain macro_domain =
        selection_bank
            ? information::SeedDomain::
                  SuccessorSelectionMacroTransition
            : information::SeedDomain::
                  SuccessorEvaluationMacroTransition;
    const std::size_t seed_block =
        kind == ScopeKind::Full
            ? bellman::kBlockCount
            : block;
    const std::string scoped_information =
        scoped_successor_information_set(
            manifest_root.probe.stable_id,
            information_set_fingerprint);
    for (std::size_t action_index = 0;
         action_index < expected_actions.size();
         ++action_index) {
        const information::CanonicalActionRow& expected =
            expected_actions[action_index];
        const GroupAction& action =
            bank.actions[action_index];
        const std::vector<double> expected_features =
            learned_priority_policy_features(
                representative_transition.successor_state,
                representative_transition.successor_owner,
                expected.action,
                representative_transition
                    .successor_context.sorcery_actions,
                representative_transition
                    .successor_context.phase,
                representative_transition
                    .successor_context.consecutive_passes);
        const std::string expected_consequence =
            information::canonical_priority_consequence_sha256(
                representative_transition.successor_state,
                representative_transition.successor_owner,
                representative_transition.successor_context,
                expected.action);
        if (action.descriptor != expected.descriptor ||
            action.action != expected.action ||
            action.feature_row_id != feature_row_id(
                scoped_information, expected.descriptor) ||
            action.policy_features != expected_features ||
            action.canonical_consequence_fingerprint !=
                expected_consequence ||
            action.samples.size() !=
                recipe.successor_worlds) {
            throw std::invalid_argument(
                "FQ0 production bank action provenance is invalid");
        }
        for (std::size_t world = 0;
             world < recipe.successor_worlds; ++world) {
            const std::uint64_t determinization_seed =
                indexed_seed(
                    seed_base, determinization_domain,
                    manifest_root.probe.stable_id,
                    information_set_fingerprint, seed_bank,
                    seed_block, world);
            const std::uint64_t macro_seed =
                indexed_seed(
                    seed_base, macro_domain,
                    manifest_root.probe.stable_id,
                    information_set_fingerprint, seed_bank,
                    seed_block, world);
            validate_production_leaf_sample(
                action.samples[world], world,
                determinization_seed, macro_seed,
                "FQ0 production successor sample");
        }
    }
}

void validate_production_bank_pair(
    const Recipe& recipe,
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const RootTransition& representative_transition,
    std::string_view information_set_fingerprint,
    ScopeKind kind, std::size_t block,
    const GroupBank& bank_a, const GroupBank& bank_b,
    const bellman::CrossFitValue* retained_cross_fit) {
    validate_production_bank(
        recipe, manifest_root, representative_transition,
        information_set_fingerprint, kind, block,
        information::SeedBank::A, bank_a);
    validate_production_bank(
        recipe, manifest_root, representative_transition,
        information_set_fingerprint, kind, block,
        information::SeedBank::B, bank_b);
    const bellman::CrossFitValue recomputed =
        bellman::cross_fit_v0(
            action_samples(bank_a), action_samples(bank_b));
    if (retained_cross_fit != nullptr &&
        *retained_cross_fit != recomputed) {
        throw std::invalid_argument(
            "FQ0 production cross-fit value is stale");
    }
}

const ac1_teacher_audit::ManifestRoot&
production_manifest_root(
    const Construction& primary, const Root& root) {
    const auto found = std::find_if(
        primary.manifest.roots.begin(),
        primary.manifest.roots.end(),
        [&](const ac1_teacher_audit::ManifestRoot& candidate) {
            return candidate.probe.stable_id ==
                   root.stable_id;
        });
    if (found == primary.manifest.roots.end()) {
        throw std::invalid_argument(
            "FQ0 production root is absent from the frozen "
            "manifest");
    }
    return *found;
}

void validate_production_root_identity(
    const Recipe& recipe,
    const Root& root,
    const ac1_teacher_audit::ManifestRoot& manifest_root) {
    const probes::DecisionProbe& probe =
        manifest_root.probe;
    const GameState root_state =
        canonical_root_state(probe);
    const LearnedDecisionContext context =
        root_context(probe);
    const std::vector<PriorityAction> authoritative =
        legal_priority_actions(
            root_state, probe.root_player,
            context.sorcery_actions);
    const auto canonical_actions =
        authoritative_root_actions(
            probe, root_state, context);
    const information::InformationSetKey root_key =
        information::make_information_set_key(
            root_state, context, authoritative);
    if (root.stable_id != probe.stable_id ||
        root.manifest_information_action_fingerprint !=
            manifest_root.information_action_fingerprint ||
        root.canonical_information_set_fingerprint !=
            information::information_set_sha256(root_key) ||
        root.root_deck != probe.root_deck ||
        root.root_player != probe.root_player ||
        root.sampled_worlds.size() !=
            recipe.root_worlds ||
        root.actions.size() != canonical_actions.size()) {
        throw std::invalid_argument(
            "FQ0 production root identity is invalid");
    }
    for (std::size_t world = 0;
         world < recipe.root_worlds; ++world) {
        const std::uint64_t expected_seed =
            indexed_seed(
                recipe.root_seed_base,
                information::SeedDomain::
                    RootDeterminization,
                probe.stable_id,
                manifest_root
                    .information_action_fingerprint,
                information::SeedBank::Root,
                bellman::kBlockCount, world);
        const RootSampledWorld& sampled =
            root.sampled_worlds[world];
        if (sampled.world_index != world ||
            sampled.determinization_seed != expected_seed) {
            throw std::invalid_argument(
                "FQ0 production root-world seed provenance "
                "is invalid");
        }
        const auto sampled_actions =
            legal_priority_actions(
                sampled.state, probe.root_player,
                context.sorcery_actions);
        const information::InformationSetKey sampled_key =
            information::make_information_set_key(
                sampled.state, context, sampled_actions);
        if (information::information_set_sha256(
                sampled_key) !=
            root.canonical_information_set_fingerprint) {
            throw std::invalid_argument(
                "FQ0 production root world changed the "
                "root information set");
        }
    }
    for (std::size_t action_index = 0;
         action_index < canonical_actions.size();
         ++action_index) {
        if (root.actions[action_index].descriptor !=
                canonical_actions[action_index].descriptor ||
            root.actions[action_index].action !=
                canonical_actions[action_index].action) {
            throw std::invalid_argument(
                "FQ0 production root action order is invalid");
        }
    }
}

void validate_production_root_action_transitions(
    const Recipe& recipe,
    const Root& root, const RootAction& action,
    const ac1_teacher_audit::ManifestRoot& manifest_root) {
    const probes::DecisionProbe& probe =
        manifest_root.probe;
    const GameState root_state =
        canonical_root_state(probe);
    const LearnedDecisionContext context =
        root_context(probe);
    const std::string root_information =
        "root/" + probe.stable_id + "/" +
        manifest_root.information_action_fingerprint;
    if (action.feature_row_id !=
            feature_row_id(
                root_information, action.descriptor) ||
        action.policy_features !=
            learned_priority_policy_features(
                root_state, probe.root_player,
                action.action, context.sorcery_actions,
                context.phase,
                context.consecutive_passes) ||
        action.canonical_consequence_fingerprint !=
            information::
                canonical_priority_consequence_sha256(
                    root_state, probe.root_player,
                    context, action.action) ||
        action.root_transitions.size() !=
            recipe.root_worlds) {
        throw std::invalid_argument(
            "FQ0 production root-action provenance is invalid");
    }
    for (std::size_t world = 0;
         world < recipe.root_worlds; ++world) {
        const RootTransition& transition =
            action.root_transitions[world];
        const std::uint64_t expected_macro_seed =
            indexed_seed(
                recipe.root_seed_base,
                information::SeedDomain::
                    RootMacroTransition,
                probe.stable_id,
                manifest_root
                    .information_action_fingerprint,
                information::SeedBank::Root,
                bellman::kBlockCount, world);
        if (transition.world_index != world ||
            transition.determinization_seed !=
                root.sampled_worlds[world]
                    .determinization_seed ||
            transition.macro_seed != expected_macro_seed ||
            transition.redacted_result_hash.empty()) {
            throw std::invalid_argument(
                "FQ0 production root transition seed "
                "provenance is invalid");
        }
        validate_completed_transition_accounting(
            transition.actions_applied,
            transition.priority_actions_applied,
            transition.phase_transitions,
            transition.turn_advances,
            transition.forced_action_applied,
            "FQ0 production root transition");
        if (transition.terminal) {
            if ((transition.terminal_root_owner_value != 0.0 &&
                 transition.terminal_root_owner_value != 0.5 &&
                 transition.terminal_root_owner_value != 1.0) ||
                !transition
                     .successor_information_set_fingerprint
                     .empty() ||
                !transition.successor_legal_actions.empty() ||
                transition.successor_context.valid) {
                throw std::invalid_argument(
                    "FQ0 production terminal root transition "
                    "is invalid");
            }
            continue;
        }
        if (!transition.successor_context.valid ||
            transition.successor_context.decision_player !=
                transition.successor_owner ||
            transition.successor_legal_actions.size() < 2) {
            throw std::invalid_argument(
                "FQ0 production successor boundary is invalid");
        }
        const information::InformationSetKey successor_key =
            information::make_information_set_key(
                transition.successor_state,
                transition.successor_context,
                transition.successor_legal_actions);
        const std::string fingerprint =
            information::information_set_sha256(
                successor_key);
        if (fingerprint !=
                transition
                    .successor_information_set_fingerprint ||
            transition.redacted_result_hash != fingerprint) {
            throw std::invalid_argument(
                "FQ0 production successor fingerprint "
                "provenance is invalid");
        }
    }
}

void validate_production_scope(
    const Recipe& recipe,
    const Root& root, const RootAction& action,
    const Scope& scope, ScopeKind expected_kind,
    std::size_t expected_block) {
    if (scope.kind != expected_kind ||
        scope.block != expected_block ||
        scope.root_world_indices !=
            scope_worlds(
                recipe, expected_kind,
                expected_block)) {
        throw std::invalid_argument(
            "FQ0 production root scope identity is invalid");
    }
    std::vector<bellman::TerminalParticle>
        expected_terminals;
    std::map<std::string, std::vector<std::size_t>>
        expected_groups;
    for (const std::size_t world :
         scope.root_world_indices) {
        const RootTransition& transition =
            action.root_transitions.at(world);
        if (transition.terminal) {
            expected_terminals.push_back({
                .world_index = world,
                .root_owner_value =
                    transition.terminal_root_owner_value,
            });
        } else {
            expected_groups[
                transition
                    .successor_information_set_fingerprint]
                .push_back(world);
        }
    }
    if (scope.terminals != expected_terminals ||
        scope.groups.size() != expected_groups.size()) {
        throw std::invalid_argument(
            "FQ0 production scope particle census is invalid");
    }

    std::vector<bellman::TerminalParticle>
        local_terminals;
    for (const auto& terminal : expected_terminals) {
        const auto position = std::lower_bound(
            scope.root_world_indices.begin(),
            scope.root_world_indices.end(),
            terminal.world_index);
        local_terminals.push_back({
            .world_index = static_cast<std::size_t>(
                position -
                scope.root_world_indices.begin()),
            .root_owner_value =
                terminal.root_owner_value,
        });
    }
    std::vector<bellman::SuccessorGroup>
        backed_groups;
    std::size_t group_index = 0;
    for (const auto& [fingerprint, members] :
         expected_groups) {
        const SuccessorGroup& group =
            scope.groups[group_index++];
        const RootTransition& representative =
            action.root_transitions.at(members.front());
        const bellman::OwnerRelation relation =
            representative.successor_owner ==
                    root.root_player
                ? bellman::OwnerRelation::SameOwner
                : bellman::OwnerRelation::OpponentOwner;
        if (group.information_set_fingerprint !=
                fingerprint ||
            group.successor_owner !=
                representative.successor_owner ||
            group.relation != relation ||
            group.root_world_indices != members ||
            group.representative_root_world !=
                members.front() ||
            group
                    .representative_root_action_descriptor !=
                action.descriptor) {
            throw std::invalid_argument(
                "FQ0 production successor group provenance "
                "is invalid");
        }
        for (const std::size_t world : members) {
            const RootTransition& transition =
                action.root_transitions.at(world);
            if (transition.terminal ||
                transition.successor_owner !=
                    representative.successor_owner ||
                transition
                        .successor_information_set_fingerprint !=
                    fingerprint) {
                throw std::invalid_argument(
                    "FQ0 production successor group contains "
                    "a foreign transition");
            }
        }
        const bellman::CrossFitValue recomputed_cross_fit =
            bellman::cross_fit_v0(
                action_samples(group.bank_a),
                action_samples(group.bank_b));
        if (group.cross_fit != recomputed_cross_fit) {
            throw std::invalid_argument(
                "FQ0 production empirical-group cross-fit "
                "is stale");
        }
        bellman::SuccessorGroup backed{
            .fingerprint = fingerprint,
            .mass = members.size(),
            .relation = relation,
            .successor_owner_value =
                group.cross_fit.value,
        };
        for (const std::size_t world : members) {
            const auto position = std::lower_bound(
                scope.root_world_indices.begin(),
                scope.root_world_indices.end(), world);
            backed.world_indices.push_back(
                static_cast<std::size_t>(
                    position -
                    scope.root_world_indices.begin()));
        }
        backed_groups.push_back(std::move(backed));
    }
    const bellman::BackedTarget target =
        bellman::back_up_root_target(
            scope.root_world_indices.size(),
            local_terminals, backed_groups);
    if (scope.target != target ||
        !scope.exact_particle_partition) {
        throw std::invalid_argument(
            "FQ0 production root scope target is stale");
    }
}

void validate_production_root_action(
    const Recipe& recipe,
    const Root& root, const RootAction& action,
    const ac1_teacher_audit::ManifestRoot& manifest_root) {
    validate_production_root_action_transitions(
        recipe, root, action, manifest_root);
    if (action.scopes.size() !=
        bellman::kBlockCount + 1) {
        throw std::invalid_argument(
            "FQ0 production root action lacks Full+8 scopes");
    }
    for (std::size_t scope_index = 0;
         scope_index < action.scopes.size();
         ++scope_index) {
        const ScopeKind kind =
            scope_index == 0 ? ScopeKind::Full
                             : ScopeKind::Block;
        const std::size_t block =
            scope_index == 0 ? 0 : scope_index - 1;
        validate_production_scope(
            recipe, root, action,
            action.scopes[scope_index], kind, block);
        const double expected_target =
            action.scopes[scope_index].target.value;
        const double retained_target =
            scope_index == 0
                ? action.target.full
                : action.target.blocks[block];
        if (std::bit_cast<std::uint64_t>(
                expected_target) !=
            std::bit_cast<std::uint64_t>(
                retained_target)) {
            throw std::invalid_argument(
                "FQ0 production root target does not match "
                "its scope");
        }
    }
}

struct ExpectedFeatureCatalog {
    std::size_t owner = 0;
    std::vector<SuccessorFeatureEvaluation::Member>
        members;
};

std::map<std::string, ExpectedFeatureCatalog>
expected_feature_catalogs(const Root& root) {
    std::map<std::string, ExpectedFeatureCatalog> result;
    for (const RootAction& action : root.actions) {
        for (const RootTransition& transition :
             action.root_transitions) {
            if (transition.terminal) {
                continue;
            }
            auto [position, inserted] = result.try_emplace(
                transition
                    .successor_information_set_fingerprint,
                ExpectedFeatureCatalog{
                    .owner = transition.successor_owner,
                });
            if (!inserted &&
                position->second.owner !=
                    transition.successor_owner) {
                throw std::invalid_argument(
                    "FQ0 production feature catalog mixes "
                    "successor owners");
            }
            position->second.members.push_back({
                .root_action_descriptor =
                    action.descriptor,
                .root_world = transition.world_index,
            });
        }
    }
    for (auto& [fingerprint, catalog] : result) {
        static_cast<void>(fingerprint);
        std::sort(
            catalog.members.begin(),
            catalog.members.end(),
            [](const auto& first, const auto& second) {
                return std::tie(
                           first.root_world,
                           first.root_action_descriptor) <
                       std::tie(
                           second.root_world,
                           second.root_action_descriptor);
            });
        if (catalog.members.empty() ||
            std::adjacent_find(
                catalog.members.begin(),
                catalog.members.end()) !=
                catalog.members.end()) {
            throw std::invalid_argument(
                "FQ0 production feature member catalog "
                "is invalid");
        }
    }
    return result;
}

void validate_production_feature_evaluation(
    const Recipe& recipe,
    const Root& root,
    const SuccessorFeatureEvaluation& evaluation,
    const ExpectedFeatureCatalog& expected,
    const ac1_teacher_audit::ManifestRoot& manifest_root) {
    if (evaluation.root_stable_id != root.stable_id ||
        evaluation.successor_owner != expected.owner ||
        evaluation.members != expected.members ||
        evaluation.representative_root_world !=
            expected.members.front().root_world ||
        evaluation
                .representative_root_action_descriptor !=
            expected.members.front()
                .root_action_descriptor ||
        evaluation.scopes.size() !=
            bellman::kBlockCount + 1) {
        throw std::invalid_argument(
            "FQ0 production feature evaluation provenance "
            "is invalid");
    }
    const RootAction& representative_action =
        find_root_action(
            root,
            evaluation
                .representative_root_action_descriptor);
    if (evaluation.representative_root_world >=
        representative_action.root_transitions.size()) {
        throw std::invalid_argument(
            "FQ0 production feature representative is "
            "out of range");
    }
    const RootTransition& representative =
        representative_action.root_transitions[
            evaluation.representative_root_world];
    if (representative.terminal ||
        representative
                .successor_information_set_fingerprint !=
            evaluation.information_set_fingerprint ||
        representative.successor_owner !=
            evaluation.successor_owner) {
        throw std::invalid_argument(
            "FQ0 production feature representative is "
            "not a member");
    }
    for (std::size_t scope_index = 0;
         scope_index < evaluation.scopes.size();
         ++scope_index) {
        const ScopeKind expected_kind =
            scope_index == 0 ? ScopeKind::Full
                             : ScopeKind::Block;
        const std::size_t expected_block =
            scope_index == 0 ? 0 : scope_index - 1;
        const SuccessorFeatureScope& scope =
            evaluation.scopes[scope_index];
        if (scope.kind != expected_kind ||
            scope.block != expected_block) {
            throw std::invalid_argument(
                "FQ0 production feature scope is not Full+8");
        }
        validate_production_bank_pair(
            recipe, manifest_root, representative,
            evaluation.information_set_fingerprint,
            scope.kind, scope.block, scope.bank_a,
            scope.bank_b, nullptr);
    }
}

void append_feature_row(
    DigestWriter& writer,
    const bellman::FeatureTargetRow& row) {
    writer.text(row.row_id);
    writer.text(row.information_set_id);
    writer.text(row.legal_set_id);
    writer.text(row.common_world_key);
    writer.text(row.action_descriptor);
    append_features(writer, row.features);
    writer.text(row.canonical_consequence_fingerprint);
    append_target(writer, row.target);
    writer.boolean(row.unique_exact_max);
}

std::string feature_rows_sha256(
    std::span<const bellman::FeatureTargetRow> rows) {
    DigestWriter writer;
    writer.text("old-school-fq0-feature-row-catalog-v1");
    writer.integer(rows.size());
    for (const bellman::FeatureTargetRow& row : rows) {
        append_feature_row(writer, row);
    }
    return writer.sha256();
}

std::string collision_analysis_sha256(
    const bellman::FeatureCollisionAnalysis& analysis) {
    DigestWriter writer;
    writer.text("old-school-fq0-feature-collision-analysis-v1");
    writer.integer(analysis.rows);
    writer.integer(analysis.colliding_feature_classes);
    writer.integer(analysis.collisions.size());
    for (const bellman::FeatureCollision& collision :
         analysis.collisions) {
        writer.text(collision.first_row_id);
        writer.text(collision.second_row_id);
        writer.integer(
            static_cast<std::uint8_t>(
                collision.target_method));
        writer.real(collision.target_separation_lower_95);
        writer.boolean(collision.consequence_conflict);
        writer.boolean(collision.target_conflict);
        writer.boolean(collision.support_conflict);
        writer.boolean(collision.harmful);
    }
    writer.integer(analysis.harmful_collisions);
    writer.boolean(analysis.passed);
    return writer.sha256();
}

std::vector<bellman::FeatureTargetRow>
rederive_feature_rows(const Construction& construction) {
    std::vector<bellman::FeatureTargetRow> rows;
    for (const Root& root : construction.roots) {
        const std::string root_information =
            "root/" + root.stable_id + "/" +
            root.manifest_information_action_fingerprint;
        const std::vector<std::string> support =
            bellman::exact_max_support(
                root_action_means(root.actions));
        const bool unique_root_max = support.size() == 1;
        for (const RootAction& action : root.actions) {
            rows.push_back({
                .row_id = feature_row_id(
                    root_information, action.descriptor),
                .information_set_id = root_information,
                .legal_set_id =
                    legal_set_id(root_information),
                .common_world_key =
                    common_world_key(
                        root.stable_id,
                        root_information, true),
                .action_descriptor = action.descriptor,
                .features = action.policy_features,
                .canonical_consequence_fingerprint =
                    action
                        .canonical_consequence_fingerprint,
                .target = action.target,
                .unique_exact_max =
                    unique_root_max &&
                    action.descriptor == support.front(),
            });
        }
    }

    for (const SuccessorFeatureEvaluation& evaluation :
         construction.successor_feature_evaluations) {
        std::map<std::string, bellman::TargetBlocks>
            targets_by_action;
        for (const SuccessorFeatureScope& scope :
             evaluation.scopes) {
            const BankPair pair{
                .bank_a = scope.bank_a,
                .bank_b = scope.bank_b,
                .cross_fit = bellman::cross_fit_v0(
                    action_samples(scope.bank_a),
                    action_samples(scope.bank_b)),
            };
            const auto targets =
                symmetric_action_targets(pair);
            for (const auto& [descriptor, target] :
                 targets) {
                bellman::TargetBlocks& blocks =
                    targets_by_action[descriptor];
                if (scope.kind == ScopeKind::Full) {
                    blocks.full = target;
                } else {
                    blocks.blocks.at(scope.block) =
                        target;
                }
            }
        }
        if (evaluation.scopes.empty()) {
            throw std::invalid_argument(
                "FQ0 feature-row derivation lacks a Full scope");
        }
        const GroupBank& full_bank =
            evaluation.scopes.front().bank_a;
        std::vector<bellman::ActionMean> means;
        means.reserve(targets_by_action.size());
        for (const auto& [descriptor, target] :
             targets_by_action) {
            means.push_back({
                .descriptor = descriptor,
                .value = target.full,
            });
        }
        const std::vector<std::string> support =
            bellman::exact_max_support(means);
        const bool unique_max = support.size() == 1;
        const std::string scoped_information =
            scoped_successor_information_set(
                evaluation.root_stable_id,
                evaluation.information_set_fingerprint);
        for (const GroupAction& action :
             full_bank.actions) {
            const auto target =
                targets_by_action.find(action.descriptor);
            if (target == targets_by_action.end()) {
                throw std::invalid_argument(
                    "FQ0 feature-row derivation lost an action");
            }
            rows.push_back({
                .row_id = feature_row_id(
                    scoped_information,
                    action.descriptor),
                .information_set_id =
                    scoped_information,
                .legal_set_id =
                    legal_set_id(scoped_information),
                .common_world_key =
                    common_world_key(
                        evaluation.root_stable_id,
                        scoped_information, false),
                .action_descriptor = action.descriptor,
                .features = action.policy_features,
                .canonical_consequence_fingerprint =
                    action
                        .canonical_consequence_fingerprint,
                .target = target->second,
                .unique_exact_max =
                    unique_max &&
                    action.descriptor == support.front(),
            });
        }
    }
    std::sort(
        rows.begin(), rows.end(),
        [](const bellman::FeatureTargetRow& first,
           const bellman::FeatureTargetRow& second) {
            return first.row_id < second.row_id;
        });
    for (std::size_t index = 1; index < rows.size(); ++index) {
        if (rows[index - 1].row_id >= rows[index].row_id) {
            throw std::invalid_argument(
                "FQ0 rederived feature rows are not unique");
        }
    }
    return rows;
}

struct FeatureCoordinate {
    std::size_t root_index = 0;
    std::size_t evaluation_index = 0;
    std::size_t scope_index = 0;
    std::vector<EmpiricalGroupRepresentativeCoordinate>
        empirical_group_representatives;
};

std::vector<FeatureCoordinate> validate_complete_preflight_impl(
    const Construction& primary,
    const ac1_teacher_audit::Manifest& expected_manifest,
    std::shared_ptr<const LearnedModel> model,
    const Recipe& recipe) {
    validate_recipe(recipe);
    validate_manifest_basics(expected_manifest);
    if (!model ||
        learned_critic_schema(model) !=
            LearnedCriticSchema::LegacyStateOnly ||
        learned_model_fingerprint(model) !=
            primary.model_fingerprint ||
        primary.manifest != expected_manifest ||
        primary.roots.size() !=
            expected_manifest.roots.size() ||
        primary.execution.workers_requested !=
            recipe.workers ||
        primary.execution.maximum_workers_started !=
            recipe.workers ||
        primary.execution.parallel_batches == 0 ||
        primary.execution.indexed_tasks == 0 ||
        primary.semantic_sha256.empty() ||
        semantic_sha256_impl(primary) !=
            primary.semantic_sha256) {
        throw std::invalid_argument(
            "FQ0 reconstruction input is not the expected "
            "complete construction");
    }

    std::array<std::size_t, kDeckCount> manifest_census{};
    for (const ac1_teacher_audit::ManifestRoot& manifest_root :
         expected_manifest.roots) {
        ++manifest_census[
            deck_index(manifest_root.probe.root_deck)];
    }
    if (manifest_census !=
            expected_manifest.physical_roots_by_deck ||
        primary.roots_by_deck != manifest_census) {
        throw std::invalid_argument(
            "FQ0 construction deck census is stale");
    }

    std::string previous_root;
    for (const Root& root : primary.roots) {
        if ((!previous_root.empty() &&
             previous_root >= root.stable_id)) {
            throw std::invalid_argument(
                "FQ0 construction roots are not canonical");
        }
        previous_root = root.stable_id;
        const ac1_teacher_audit::ManifestRoot& manifest_root =
            production_manifest_root(primary, root);
        validate_production_root_identity(
            recipe, root, manifest_root);
        for (const RootAction& action : root.actions) {
            validate_production_root_action(
                recipe, root, action, manifest_root);
        }
        if (root.exact_support !=
            bellman::exact_max_support(
                root_action_means(root.actions))) {
            throw std::invalid_argument(
                "FQ0 root exact support is stale");
        }
    }

    std::optional<std::pair<std::string, std::string>>
        previous_evaluation;
    for (const SuccessorFeatureEvaluation& evaluation :
         primary.successor_feature_evaluations) {
        const auto coordinate = std::pair{
            evaluation.root_stable_id,
            evaluation.information_set_fingerprint,
        };
        if (previous_evaluation.has_value() &&
            *previous_evaluation >= coordinate) {
            throw std::invalid_argument(
                "FQ0 feature evaluations are not canonical "
                "and unique");
        }
        previous_evaluation = coordinate;
    }

    std::vector<FeatureCoordinate> coordinates;
    std::vector<bool> evaluation_seen(
        primary.successor_feature_evaluations.size(),
        false);
    std::map<
        std::pair<std::string, std::string>,
        std::size_t>
        evaluation_by_information_set;
    for (std::size_t root_index = 0;
         root_index < primary.roots.size();
         ++root_index) {
        const Root& root = primary.roots[root_index];
        const ac1_teacher_audit::ManifestRoot& manifest_root =
            production_manifest_root(primary, root);
        const auto expected =
            expected_feature_catalogs(root);
        std::size_t root_evaluations = 0;
        for (std::size_t evaluation_index = 0;
             evaluation_index <
             primary
                 .successor_feature_evaluations.size();
             ++evaluation_index) {
            const SuccessorFeatureEvaluation& evaluation =
                primary.successor_feature_evaluations[
                    evaluation_index];
            if (evaluation.root_stable_id !=
                root.stable_id) {
                continue;
            }
            const auto key = std::pair{
                evaluation.root_stable_id,
                evaluation.information_set_fingerprint,
            };
            if (!evaluation_by_information_set
                     .emplace(key, evaluation_index)
                     .second) {
                throw std::invalid_argument(
                    "FQ0 feature census repeats an "
                    "information set");
            }
            const auto found = expected.find(
                evaluation
                    .information_set_fingerprint);
            if (found == expected.end()) {
                throw std::invalid_argument(
                    "FQ0 feature census contains a foreign "
                    "information set");
            }
            validate_production_feature_evaluation(
                recipe, root, evaluation, found->second,
                manifest_root);
            evaluation_seen[evaluation_index] = true;
            ++root_evaluations;
            for (std::size_t scope_index = 0;
                 scope_index <
                 evaluation.scopes.size();
                 ++scope_index) {
                coordinates.push_back({
                    .root_index = root_index,
                    .evaluation_index =
                        evaluation_index,
                    .scope_index = scope_index,
                });
            }
        }
        if (root_evaluations != expected.size()) {
            throw std::invalid_argument(
                "FQ0 feature census is incomplete");
        }
    }
    if (std::find(
            evaluation_seen.begin(), evaluation_seen.end(),
            false) != evaluation_seen.end() ||
        coordinates.size() !=
            primary.successor_feature_evaluations.size() *
                (bellman::kBlockCount + 1)) {
        throw std::invalid_argument(
            "FQ0 feature census contains an unbound "
            "evaluation");
    }

    // The construction caches each successor bank once in the feature
    // catalog and copies it into every empirical root group. Every copy must
    // remain bit-identical to that canonical cache coordinate.
    std::map<
        std::tuple<std::size_t, std::size_t, std::size_t>,
        std::size_t>
        coordinate_indices;
    for (std::size_t index = 0;
         index < coordinates.size(); ++index) {
        const FeatureCoordinate& coordinate =
            coordinates[index];
        coordinate_indices.emplace(
            std::tuple{
                coordinate.root_index,
                coordinate.evaluation_index,
                coordinate.scope_index,
            },
            index);
    }
    for (std::size_t root_index = 0;
         root_index < primary.roots.size();
         ++root_index) {
        const Root& root = primary.roots[root_index];
        for (const RootAction& action : root.actions) {
            for (const Scope& scope : action.scopes) {
                for (const SuccessorGroup& group :
                     scope.groups) {
                    const auto found =
                        evaluation_by_information_set.find({
                            root.stable_id,
                            group
                                .information_set_fingerprint,
                        });
                    if (found ==
                        evaluation_by_information_set.end()) {
                        throw std::invalid_argument(
                            "FQ0 empirical group lacks a "
                            "canonical feature evaluation");
                    }
                    const SuccessorFeatureEvaluation&
                        evaluation =
                            primary
                                .successor_feature_evaluations[
                                    found->second];
                    const std::size_t scope_index =
                        scope.kind == ScopeKind::Full
                            ? 0
                            : scope.block + 1;
                    if (scope_index >=
                        evaluation.scopes.size()) {
                        throw std::invalid_argument(
                            "FQ0 empirical group scope is "
                            "out of range");
                    }
                    const auto coordinate =
                        coordinate_indices.find(
                            std::tuple{
                                root_index,
                                found->second,
                                scope_index,
                            });
                    if (coordinate ==
                        coordinate_indices.end()) {
                        throw std::invalid_argument(
                            "FQ0 empirical group lacks a "
                            "feature reconstruction coordinate");
                    }
                    coordinates[coordinate->second]
                        .empirical_group_representatives
                        .push_back({
                            .root_action_descriptor =
                                group
                                    .representative_root_action_descriptor,
                            .root_world =
                                group
                                    .representative_root_world,
                        });
                    const SuccessorFeatureScope&
                        canonical_scope =
                            evaluation.scopes[scope_index];
                    const BankPair empirical{
                        .bank_a = group.bank_a,
                        .bank_b = group.bank_b,
                        .cross_fit = group.cross_fit,
                    };
                    const BankPair canonical{
                        .bank_a = canonical_scope.bank_a,
                        .bank_b = canonical_scope.bank_b,
                        .cross_fit =
                            bellman::cross_fit_v0(
                                action_samples(
                                    canonical_scope.bank_a),
                                action_samples(
                                    canonical_scope.bank_b)),
                    };
                    if (empirical.bank_a != canonical.bank_a ||
                        empirical.bank_b != canonical.bank_b ||
                        empirical.cross_fit !=
                            canonical.cross_fit ||
                        bank_pair_sha256(empirical) !=
                            bank_pair_sha256(canonical)) {
                        throw std::invalid_argument(
                            "FQ0 repeated successor bank copy "
                            "is not bit-identical");
                    }
                }
            }
        }
    }
    for (FeatureCoordinate& coordinate : coordinates) {
        auto& representatives =
            coordinate.empirical_group_representatives;
        std::sort(
            representatives.begin(), representatives.end(),
            [](const auto& first, const auto& second) {
                return std::tie(
                           first.root_world,
                           first.root_action_descriptor) <
                       std::tie(
                           second.root_world,
                           second.root_action_descriptor);
            });
        representatives.erase(
            std::unique(
                representatives.begin(),
                representatives.end()),
            representatives.end());
        const Root& coordinate_root =
            primary.roots.at(coordinate.root_index);
        const SuccessorFeatureEvaluation&
            coordinate_evaluation =
                primary
                    .successor_feature_evaluations.at(
                        coordinate.evaluation_index);
        const SuccessorFeatureScope& coordinate_scope =
            coordinate_evaluation.scopes.at(
                coordinate.scope_index);
        if (representatives !=
            empirical_group_representatives_for_scope(
                coordinate_root, coordinate_evaluation,
                coordinate_scope)) {
            throw std::invalid_argument(
                "FQ0 empirical-group representative catalog "
                "is incomplete");
        }
        if (representatives.empty()) {
            // A feature-only block may legitimately have zero empirical
            // mass, but its information set must occur in at least one
            // empirical group in another scope.
            const FeatureCoordinate& full_coordinate =
                coordinates.at(
                    coordinate_indices.at(
                        std::tuple{
                            coordinate.root_index,
                            coordinate.evaluation_index,
                            std::size_t{0},
                        }));
            if (coordinate.scope_index == 0 ||
                full_coordinate
                    .empirical_group_representatives.empty()) {
                throw std::invalid_argument(
                    "FQ0 feature evaluation has no empirical "
                    "group representative");
            }
        }
    }

    const std::vector<bellman::FeatureTargetRow> expected_rows =
        rederive_feature_rows(primary);
    for (std::size_t index = 1;
         index < primary.feature_rows.size(); ++index) {
        if (primary.feature_rows[index - 1].row_id >=
            primary.feature_rows[index].row_id) {
            throw std::invalid_argument(
                "FQ0 retained feature rows are not canonical "
                "and unique");
        }
    }
    if (primary.feature_rows.size() !=
            expected_rows.size() ||
        feature_rows_sha256(primary.feature_rows) !=
            feature_rows_sha256(expected_rows)) {
        throw std::invalid_argument(
            "FQ0 retained feature-row catalog is stale");
    }
    const bellman::FeatureCollisionAnalysis
        expected_collisions =
            bellman::analyze_global_feature_collisions(
                expected_rows);
    if (collision_analysis_sha256(
            primary.feature_collisions) !=
        collision_analysis_sha256(expected_collisions)) {
        throw std::invalid_argument(
            "FQ0 retained feature-collision analysis is stale");
    }
    return coordinates;
}

Construction construct(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model,
    const Recipe& recipe) {
    validate_recipe(recipe);
    validate_manifest_basics(manifest);
    if (!model) {
        throw std::invalid_argument(
            "FQ0 construction requires a model");
    }
    if (learned_critic_schema(model) !=
        LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "FQ0 construction requires a legacy-state C16 "
            "critic");
    }

    Construction result{
        .manifest = manifest,
        .model_fingerprint =
            learned_model_fingerprint(model),
    };
    IndexedExecutor executor(
        recipe.workers, result.execution);

    std::vector<const ac1_teacher_audit::ManifestRoot*>
        canonical_roots;
    canonical_roots.reserve(manifest.roots.size());
    for (const auto& root : manifest.roots) {
        canonical_roots.push_back(&root);
    }
    std::sort(
        canonical_roots.begin(), canonical_roots.end(),
        [](const auto* first, const auto* second) {
            return first->probe.stable_id <
                   second->probe.stable_id;
        });

    result.roots.reserve(canonical_roots.size());
    for (const auto* manifest_root : canonical_roots) {
        result.roots.push_back(build_root(
            recipe, executor, *manifest_root, model,
            result.successor_feature_evaluations,
            result.feature_rows));
        ++result.roots_by_deck[
            deck_index(manifest_root->probe.root_deck)];
    }
    std::sort(
        result.successor_feature_evaluations.begin(),
        result.successor_feature_evaluations.end(),
        [](const SuccessorFeatureEvaluation& first,
           const SuccessorFeatureEvaluation& second) {
            return std::tie(
                       first.root_stable_id,
                       first
                           .information_set_fingerprint) <
                   std::tie(
                       second.root_stable_id,
                       second
                           .information_set_fingerprint);
        });
    std::sort(
        result.feature_rows.begin(),
        result.feature_rows.end(),
        [](const bellman::FeatureTargetRow& first,
           const bellman::FeatureTargetRow& second) {
            return first.row_id < second.row_id;
        });
    for (std::size_t index = 1;
         index < result.feature_rows.size(); ++index) {
        if (result.feature_rows[index - 1].row_id ==
            result.feature_rows[index].row_id) {
            throw std::logic_error(
                "FQ0 construction emitted duplicate feature "
                "row IDs");
        }
    }
    result.feature_collisions =
        bellman::analyze_global_feature_collisions(
            result.feature_rows);
    result.semantic_sha256 =
        semantic_sha256_impl(result);
    return result;
}

} // namespace

Construction construct_production(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> frozen_c16) {
    validate_production_inputs(manifest, frozen_c16);
    return construct(
        manifest, std::move(frozen_c16),
        kProductionRecipe);
}

Construction construct_production_single_worker_invariance(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> frozen_c16) {
    validate_production_inputs(manifest, frozen_c16);
    Recipe recipe = kProductionRecipe;
    recipe.workers = 1;
    return construct(
        manifest, std::move(frozen_c16), recipe);
}

Construction construct_production_descriptor_order_invariance(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    ac1_teacher_audit::Manifest manifest =
        ac1_teacher_audit::build_manifest();
    validate_production_inputs(manifest, frozen_c16);
    for (auto& root : manifest.roots) {
        std::reverse(
            root.probe.candidates.begin(),
            root.probe.candidates.end());
    }
    return construct(
        manifest, std::move(frozen_c16),
        kProductionRecipe);
}

Construction construct_production_hidden_repartition_invariance(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    ac1_teacher_audit::Manifest manifest =
        ac1_teacher_audit::build_manifest();
    validate_production_inputs(manifest, frozen_c16);
    for (auto& root : manifest.roots) {
        HiddenClone hidden = hidden_repartition_clone(
            root.probe.state, root.probe.root_player);
        root.probe.state = std::move(hidden.state);
    }
    return construct(
        manifest, std::move(frozen_c16),
        kProductionRecipe);
}

std::vector<ProductionFeatureScopeReconstruction>
reconstruct_all_production_feature_scopes(
    const Construction& primary,
    std::shared_ptr<const LearnedModel> frozen_c16) {
    validate_production_inputs(
        primary.manifest, frozen_c16);
    const std::vector<FeatureCoordinate> coordinates =
        validate_complete_preflight_impl(
            primary, primary.manifest, frozen_c16,
            kProductionRecipe);

    std::vector<ProductionFeatureScopeReconstruction> result;
    result.reserve(coordinates.size());
    for (const FeatureCoordinate& coordinate :
         coordinates) {
        const Root& root =
            primary.roots[coordinate.root_index];
        const ac1_teacher_audit::ManifestRoot& manifest_root =
            production_manifest_root(primary, root);
        const SuccessorFeatureEvaluation& evaluation =
            primary.successor_feature_evaluations[
                coordinate.evaluation_index];
        result.push_back({
            .root_index = coordinate.root_index,
            .feature_evaluation_index =
                coordinate.evaluation_index,
            .scope_index = coordinate.scope_index,
            .witnesses =
                reconstruct_feature_scope_impl(
                    kProductionRecipe, manifest_root,
                    root, evaluation,
                    evaluation.scopes[
                        coordinate.scope_index],
                    coordinate
                        .empirical_group_representatives,
                    frozen_c16),
        });
    }
    return result;
}

HiddenRepartitionDiagnostic hidden_repartition(
    const GameState& state, std::size_t observer) {
    HiddenClone hidden =
        hidden_repartition_clone(state, observer);
    return {
        .state = std::move(hidden.state),
        .eligible = hidden.eligible,
        .changed = hidden.changed,
    };
}

namespace testing {

std::string digest_writer_framing_fixture_sha256() {
    DigestWriter writer;
    writer.text(
        "old-school-fq0-digest-writer-framing-fixture-v1");
    writer.integer<std::uint8_t>(0xa5U);
    writer.integer<std::uint16_t>(0xb60cU);
    writer.integer<std::uint32_t>(0xd70e1f20U);
    writer.integer<std::uint64_t>(0xe80123456789abcdULL);
    writer.integer<std::size_t>(0x01020304U);
    writer.boolean(false);
    writer.boolean(true);
    writer.real(1.25);
    writer.real(-0.0);
    writer.text(std::string_view("left\0right", 10));
    std::string boundary_text(64 * 1024 + 257, '\0');
    for (std::size_t index = 0;
         index < boundary_text.size(); ++index) {
        boundary_text[index] = static_cast<char>(
            (index * 37U + 11U) & 0xffU);
    }
    writer.text(boundary_text);
    return writer.sha256();
}

SuccessorBankPairDigests successor_bank_pair_digests(
    const GroupBank& bank_a, const GroupBank& bank_b,
    const bellman::CrossFitValue& cross_fit) {
    const BankPair pair{
        .bank_a = bank_a,
        .bank_b = bank_b,
        .cross_fit = cross_fit,
    };
    return {
        .full_v1 = bank_pair_sha256(pair),
        .operator_v2 =
            operator_bank_pair_sha256(pair),
    };
}

Construction construct_reduced(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe) {
    validate_testing_recipe(recipe);
    return construct(
        manifest, std::move(model),
        {
            .root_seed_base = recipe.root_seed_base,
            .bank_a_seed_base = recipe.bank_a_seed_base,
            .bank_b_seed_base = recipe.bank_b_seed_base,
            .root_worlds = recipe.root_worlds,
            .successor_worlds =
                recipe.successor_worlds,
            .workers = recipe.workers,
        });
}

GroupReconstructionWitnesses reconstruct_group(
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const RootAction& root_action, const Scope& scope,
    const SuccessorGroup& group,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe) {
    validate_testing_recipe(recipe);
    return reconstruct_group_impl(
        {
            .root_seed_base = recipe.root_seed_base,
            .bank_a_seed_base = recipe.bank_a_seed_base,
            .bank_b_seed_base = recipe.bank_b_seed_base,
            .root_worlds = recipe.root_worlds,
            .successor_worlds =
                recipe.successor_worlds,
            .workers = recipe.workers,
        },
        manifest_root, root_action, scope, group,
        std::move(model));
}

GroupReconstructionWitnesses reconstruct_feature_scope(
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const Root& root,
    const SuccessorFeatureEvaluation& evaluation,
    const SuccessorFeatureScope& scope,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe) {
    validate_testing_recipe(recipe);
    return reconstruct_feature_scope_impl(
        {
            .root_seed_base = recipe.root_seed_base,
            .bank_a_seed_base = recipe.bank_a_seed_base,
            .bank_b_seed_base = recipe.bank_b_seed_base,
            .root_worlds = recipe.root_worlds,
            .successor_worlds =
                recipe.successor_worlds,
            .workers = recipe.workers,
        },
        manifest_root, root, evaluation, scope,
        empirical_group_representatives_for_scope(
            root, evaluation, scope),
        std::move(model));
}

HiddenRepartitionDiagnostic hidden_repartition(
    const GameState& state, std::size_t observer) {
    return fq0_bellman_science::hidden_repartition(
        state, observer);
}

void validate_complete_preflight(
    const Construction& construction,
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe) {
    validate_testing_recipe(recipe);
    static_cast<void>(
        validate_complete_preflight_impl(
            construction, manifest, std::move(model),
            {
                .root_seed_base = recipe.root_seed_base,
                .bank_a_seed_base =
                    recipe.bank_a_seed_base,
                .bank_b_seed_base =
                    recipe.bank_b_seed_base,
                .root_worlds = recipe.root_worlds,
                .successor_worlds =
                    recipe.successor_worlds,
                .workers = recipe.workers,
            }));
}

std::string semantic_sha256(
    const Construction& construction) {
    return semantic_sha256_impl(construction);
}

} // namespace testing

} // namespace old_school::fq0_bellman_science
