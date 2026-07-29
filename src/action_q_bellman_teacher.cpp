#include "old_school/action_q_bellman_teacher.hpp"

#include "old_school/fq0_information_set.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::action_q_bellman_teacher {
namespace {

namespace bellman = fq0_bellman;
namespace information = fq0_information_set;

constexpr std::string_view kRootSeedScope =
    "old-school-aq1-bl0-root-v1";
constexpr std::string_view kSuccessorSeedScope =
    "old-school-aq1-bl0-successor-v1";

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

void add_accounting(
    MacroAccounting& destination,
    const MacroAccounting& source) {
    destination.transitions += source.transitions;
    destination.terminal_transitions +=
        source.terminal_transitions;
    destination.boundary_transitions +=
        source.boundary_transitions;
    destination.critic_leaves += source.critic_leaves;
    destination.actions_applied += source.actions_applied;
    destination.priority_actions_applied +=
        source.priority_actions_applied;
    destination.phase_transitions +=
        source.phase_transitions;
    destination.turn_advances += source.turn_advances;
}

void account_transition(
    MacroAccounting& accounting,
    const LearnedPriorityMacroTransition& transition,
    bool critic_leaf) {
    if (transition.disposition ==
            LearnedPriorityMacroDisposition::Incomplete ||
        transition.exhausted_limit !=
            LearnedPriorityMacroLimit::None ||
        transition.actions_applied < 1 ||
        transition.priority_actions_applied < 1 ||
        transition.priority_actions_applied >
            transition.actions_applied) {
        throw std::runtime_error(
            "AQ1-BL0 received an incomplete or unaccounted "
            "Priority macro-transition");
    }
    ++accounting.transitions;
    accounting.actions_applied +=
        transition.actions_applied;
    accounting.priority_actions_applied +=
        transition.priority_actions_applied;
    accounting.phase_transitions +=
        transition.phase_transitions;
    accounting.turn_advances +=
        transition.turn_advances;
    switch (transition.disposition) {
    case LearnedPriorityMacroDisposition::Terminal:
        ++accounting.terminal_transitions;
        if (critic_leaf) {
            throw std::logic_error(
                "AQ1-BL0 terminal transition was marked as a "
                "critic leaf");
        }
        break;
    case LearnedPriorityMacroDisposition::PriorityBoundary:
        ++accounting.boundary_transitions;
        accounting.critic_leaves +=
            static_cast<std::size_t>(critic_leaf);
        break;
    case LearnedPriorityMacroDisposition::Incomplete:
        throw std::logic_error(
            "AQ1-BL0 incomplete transition escaped validation");
    }
}

void require_complete_boundary(
    const LearnedPriorityMacroTransition& transition,
    std::string_view coordinate) {
    if (transition.disposition !=
            LearnedPriorityMacroDisposition::PriorityBoundary ||
        transition.terminal_result.has_value() ||
        !transition.context.valid ||
        transition.context.decision_player >=
            transition.state.players.size() ||
        transition.legal_actions.size() < 2) {
        throw std::logic_error(
            std::string(coordinate) +
            ": macro did not stop at a nontrivial Priority "
            "boundary");
    }
    const auto authoritative =
        legal_priority_actions(
            transition.state,
            transition.context.decision_player,
            transition.context.sorcery_actions);
    if (authoritative != transition.legal_actions) {
        throw std::logic_error(
            std::string(coordinate) +
            ": macro returned a stale legal-action set");
    }
}

std::uint64_t indexed_seed(
    std::uint64_t root_seed,
    information::SeedDomain domain,
    std::string_view scope,
    std::string_view information_set,
    information::SeedBank bank,
    std::size_t world) {
    return information::derive_indexed_seed(
        root_seed,
        {
            .domain = domain,
            .scope = std::string(scope),
            .group = std::string(information_set),
            .bank = bank,
            .block = 0,
            .world = world,
        });
}

WorldSeeds root_world_seeds(
    std::uint64_t root_seed,
    std::string_view root_information_set,
    std::size_t world) {
    return {
        .world_index = world,
        .determinization_seed = indexed_seed(
            root_seed,
            information::SeedDomain::RootDeterminization,
            kRootSeedScope, root_information_set,
            information::SeedBank::Root, world),
        .macro_seed = indexed_seed(
            root_seed,
            information::SeedDomain::RootMacroTransition,
            kRootSeedScope, root_information_set,
            information::SeedBank::Root, world),
    };
}

WorldSeeds successor_world_seeds(
    std::uint64_t root_seed,
    std::string_view successor_information_set,
    information::SeedBank bank,
    std::size_t world) {
    const bool selection =
        bank == information::SeedBank::A;
    if (!selection &&
        bank != information::SeedBank::B) {
        throw std::invalid_argument(
            "AQ1-BL0 successor bank must be A or B");
    }
    return {
        .world_index = world,
        .determinization_seed = indexed_seed(
            root_seed,
            selection
                ? information::SeedDomain::
                      SuccessorSelectionDeterminization
                : information::SeedDomain::
                      SuccessorEvaluationDeterminization,
            kSuccessorSeedScope, successor_information_set,
            bank, world),
        .macro_seed = indexed_seed(
            root_seed,
            selection
                ? information::SeedDomain::
                      SuccessorSelectionMacroTransition
                : information::SeedDomain::
                      SuccessorEvaluationMacroTransition,
            kSuccessorSeedScope, successor_information_set,
            bank, world),
    };
}

std::vector<information::CanonicalActionRow>
canonical_root_actions(
    const information::InformationSetKey& key,
    std::span<const PriorityAction> candidates) {
    auto authoritative =
        information::descriptor_canonical_action_rows(key);
    if (candidates.size() != authoritative.size()) {
        throw std::invalid_argument(
            "AQ1-BL0 candidates are not the complete legal set");
    }

    std::vector<information::CanonicalActionRow> supplied;
    supplied.reserve(candidates.size());
    for (const PriorityAction& action : candidates) {
        supplied.push_back({
            .descriptor =
                probes::stable_priority_action_descriptor(action),
            .action = action,
        });
    }
    std::sort(
        supplied.begin(), supplied.end(),
        [](const auto& first, const auto& second) {
            return first.descriptor < second.descriptor;
        });
    for (std::size_t index = 0;
         index < supplied.size(); ++index) {
        if (supplied[index].descriptor.empty() ||
            supplied[index] != authoritative[index]) {
            throw std::invalid_argument(
                "AQ1-BL0 candidates are not an exact legal-set "
                "permutation");
        }
    }
    return authoritative;
}

struct InternalRootTransition {
    std::size_t world_index = 0;
    bool terminal = false;
    double terminal_root_owner_value = 0.0;
    std::string successor_information_set_fingerprint;
    std::size_t successor_owner = 0;
};

struct InternalRootAction {
    std::string descriptor;
    PriorityAction action;
    std::vector<InternalRootTransition> transitions;
    MacroAccounting accounting;
};

struct Representative {
    information::InformationSetKey key;
    GameState state;
    LearnedDecisionContext context;
    std::vector<PriorityAction> actions;
    std::size_t owner = 0;
};

std::string bank_stream_key(
    std::uint64_t root_seed,
    std::string_view information_set,
    information::SeedBank bank) {
    return "aq1-bl0/" + std::to_string(root_seed) + "/" +
           std::string(information_set) + "/" +
           (bank == information::SeedBank::A ? "A" : "B");
}

SuccessorBank evaluate_successor_bank(
    const Representative& representative,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t root_seed,
    std::string_view information_set,
    information::SeedBank bank) {
    const auto rows =
        information::descriptor_canonical_action_rows(
            representative.key);
    SuccessorBank result{
        .name =
            bank == information::SeedBank::A ? "A" : "B",
        .stream_key =
            bank_stream_key(root_seed, information_set, bank),
    };

    std::vector<GameState> sampled_worlds;
    sampled_worlds.reserve(kSuccessorWorlds);
    result.worlds.reserve(kSuccessorWorlds);
    for (std::size_t world = 0;
         world < kSuccessorWorlds; ++world) {
        const WorldSeeds seeds =
            successor_world_seeds(
                root_seed, information_set, bank, world);
        result.worlds.push_back(seeds);
        sampled_worlds.push_back(
            sample_determinization(
                representative.state, original_decks,
                representative.owner,
                seeds.determinization_seed));
    }

    result.actions.reserve(rows.size());
    for (const information::CanonicalActionRow& row : rows) {
        bellman::ActionSamples samples{
            .descriptor = row.descriptor,
            .sample_stream_key = result.stream_key,
        };
        samples.world_indices.reserve(kSuccessorWorlds);
        samples.samples.reserve(kSuccessorWorlds);
        for (std::size_t world = 0;
             world < kSuccessorWorlds; ++world) {
            const auto transition =
                advance_learned_priority_macro_transition(
                    sampled_worlds[world], original_decks,
                    representative.owner,
                    representative.context.sorcery_actions,
                    representative.context.phase,
                    representative.context.consecutive_passes,
                    row.action, model,
                    result.worlds[world].macro_seed);
            if (transition.disposition ==
                LearnedPriorityMacroDisposition::Incomplete) {
                throw std::runtime_error(
                    "AQ1-BL0 successor macro exhausted a fixed "
                    "bound");
            }

            double value = 0.5;
            const bool terminal =
                transition.disposition ==
                LearnedPriorityMacroDisposition::Terminal;
            if (terminal) {
                if (!transition.terminal_result.has_value()) {
                    throw std::logic_error(
                        "AQ1-BL0 terminal successor omitted its "
                        "game result");
                }
                value =
                    information::terminal_root_owner_value(
                        *transition.terminal_result,
                        representative.owner);
            } else {
                require_complete_boundary(
                    transition, "AQ1-BL0 successor");
                const auto critic =
                    information::evaluate_legacy_leaf_critic(
                        transition.state,
                        representative.owner,
                        transition.context, model);
                if (!critic.legacy_bit_identity) {
                    throw std::logic_error(
                        "AQ1-BL0 successor critic changed Legacy "
                        "C16 bits");
                }
                value = critic.value;
            }
            if (!probability(value)) {
                throw std::logic_error(
                    "AQ1-BL0 successor leaf is not a "
                    "probability");
            }
            account_transition(
                result.accounting, transition, !terminal);
            samples.world_indices.push_back(world);
            samples.samples.push_back(value);
        }
        result.actions.push_back(std::move(samples));
    }
    return result;
}

SuccessorEvaluation evaluate_successor(
    const Representative& representative,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t root_seed,
    std::string information_set) {
    SuccessorEvaluation result{
        .information_set_fingerprint =
            std::move(information_set),
        .successor_owner = representative.owner,
    };
    const auto rows =
        information::descriptor_canonical_action_rows(
            representative.key);
    result.actions.reserve(rows.size());
    for (const auto& row : rows) {
        result.actions.push_back(row.action);
    }
    result.bank_a = evaluate_successor_bank(
        representative, original_decks, model, root_seed,
        result.information_set_fingerprint,
        information::SeedBank::A);
    result.bank_b = evaluate_successor_bank(
        representative, original_decks, model, root_seed,
        result.information_set_fingerprint,
        information::SeedBank::B);
    result.cross_fit = bellman::cross_fit_v0(
        result.bank_a.actions, result.bank_b.actions);
    return result;
}

const SuccessorEvaluation& require_successor_evaluation(
    const std::vector<SuccessorEvaluation>& evaluations,
    std::string_view fingerprint) {
    const auto found = std::lower_bound(
        evaluations.begin(), evaluations.end(), fingerprint,
        [](const SuccessorEvaluation& evaluation,
           std::string_view key) {
            return evaluation.information_set_fingerprint < key;
        });
    if (found == evaluations.end() ||
        found->information_set_fingerprint != fingerprint) {
        throw std::logic_error(
            "AQ1-BL0 successor evaluation is missing");
    }
    return *found;
}

void validate_macro_accounting(
    const MacroAccounting& accounting,
    bool successor) {
    if (accounting.transitions == 0 ||
        accounting.terminal_transitions +
                accounting.boundary_transitions !=
            accounting.transitions ||
        accounting.actions_applied <
            accounting.transitions ||
        accounting.priority_actions_applied <
            accounting.transitions ||
        accounting.priority_actions_applied >
            accounting.actions_applied ||
        (successor
             ? accounting.critic_leaves !=
                   accounting.boundary_transitions
             : accounting.critic_leaves != 0)) {
        throw std::invalid_argument(
            "AQ1-BL0 macro accounting does not cross-sum");
    }
}

void validate_bank(
    const RootTargets& targets,
    const SuccessorEvaluation& evaluation,
    const SuccessorBank& bank,
    information::SeedBank expected_bank) {
    const std::string expected_name =
        expected_bank == information::SeedBank::A ? "A" : "B";
    if (bank.name != expected_name ||
        bank.stream_key !=
            bank_stream_key(
                targets.root_seed,
                evaluation.information_set_fingerprint,
                expected_bank) ||
        bank.worlds.size() != kSuccessorWorlds ||
        bank.actions.size() != evaluation.actions.size()) {
        throw std::invalid_argument(
            "AQ1-BL0 successor bank dimensions drifted");
    }
    for (std::size_t world = 0;
         world < bank.worlds.size(); ++world) {
        if (bank.worlds[world] !=
            successor_world_seeds(
                targets.root_seed,
                evaluation.information_set_fingerprint,
                expected_bank, world)) {
            throw std::invalid_argument(
                "AQ1-BL0 successor bank seed drifted");
        }
    }
    for (std::size_t action = 0;
         action < bank.actions.size(); ++action) {
        const bellman::ActionSamples& row =
            bank.actions[action];
        if (row.descriptor !=
                probes::stable_priority_action_descriptor(
                    evaluation.actions[action]) ||
            row.sample_stream_key != bank.stream_key ||
            row.world_indices.size() != kSuccessorWorlds ||
            row.samples.size() != kSuccessorWorlds) {
            throw std::invalid_argument(
                "AQ1-BL0 successor action bank drifted");
        }
        for (std::size_t world = 0;
             world < kSuccessorWorlds; ++world) {
            if (row.world_indices[world] != world ||
                !probability(row.samples[world])) {
                throw std::invalid_argument(
                    "AQ1-BL0 successor sample coordinate is "
                    "invalid");
            }
        }
        if (action > 0 &&
            bank.actions[action - 1].descriptor >=
                row.descriptor) {
            throw std::invalid_argument(
                "AQ1-BL0 successor actions are not canonical");
        }
    }
    validate_macro_accounting(bank.accounting, true);
    if (bank.accounting.transitions !=
        bank.actions.size() * kSuccessorWorlds) {
        throw std::invalid_argument(
            "AQ1-BL0 successor macro count drifted");
    }
}

} // namespace

RootTargets score_priority_root(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> candidates,
    std::shared_ptr<const LearnedModel> legacy_c16,
    std::uint64_t root_seed) {
    if (!legacy_c16 ||
        learned_critic_schema(legacy_c16) !=
            LearnedCriticSchema::LegacyStateOnly ||
        !context.valid ||
        context.decision_player >= state.players.size()) {
        throw std::invalid_argument(
            "AQ1-BL0 requires a valid Priority root and Legacy "
            "C16 critic");
    }

    const auto authoritative =
        legal_priority_actions(
            state, context.decision_player,
            context.sorcery_actions);
    const auto root_key =
        information::make_information_set_key(
            state, context, authoritative);
    const auto canonical_actions =
        canonical_root_actions(root_key, candidates);

    RootTargets result{
        .root_seed = root_seed,
        .root_information_set_fingerprint =
            information::information_set_sha256(root_key),
        .root_owner = context.decision_player,
    };

    std::vector<GameState> sampled_root_worlds;
    sampled_root_worlds.reserve(kRootWorlds);
    result.root_worlds.reserve(kRootWorlds);
    for (std::size_t world = 0; world < kRootWorlds; ++world) {
        const WorldSeeds seeds =
            root_world_seeds(
                root_seed,
                result.root_information_set_fingerprint,
                world);
        result.root_worlds.push_back(seeds);
        sampled_root_worlds.push_back(
            sample_determinization(
                state, original_decks,
                result.root_owner,
                seeds.determinization_seed));
    }

    std::vector<InternalRootAction> internal_actions;
    internal_actions.reserve(canonical_actions.size());
    std::map<std::string, Representative> representatives;
    for (const auto& row : canonical_actions) {
        InternalRootAction internal{
            .descriptor = row.descriptor,
            .action = row.action,
        };
        internal.transitions.reserve(kRootWorlds);
        for (std::size_t world = 0;
             world < kRootWorlds; ++world) {
            const auto transition =
                advance_learned_priority_macro_transition(
                    sampled_root_worlds[world], original_decks,
                    result.root_owner, context.sorcery_actions,
                    context.phase, context.consecutive_passes,
                    row.action, legacy_c16,
                    result.root_worlds[world].macro_seed);
            if (transition.disposition ==
                LearnedPriorityMacroDisposition::Incomplete) {
                throw std::runtime_error(
                    "AQ1-BL0 root macro exhausted a fixed bound");
            }
            account_transition(
                internal.accounting, transition, false);

            InternalRootTransition retained{
                .world_index = world,
                .terminal =
                    transition.disposition ==
                    LearnedPriorityMacroDisposition::Terminal,
            };
            if (retained.terminal) {
                if (!transition.terminal_result.has_value()) {
                    throw std::logic_error(
                        "AQ1-BL0 terminal root transition omitted "
                        "its result");
                }
                retained.terminal_root_owner_value =
                    information::terminal_root_owner_value(
                        *transition.terminal_result,
                        result.root_owner);
            } else {
                require_complete_boundary(
                    transition, "AQ1-BL0 root");
                const auto key =
                    information::make_information_set_key(
                        transition.state, transition.context,
                        transition.legal_actions);
                retained
                    .successor_information_set_fingerprint =
                    information::information_set_sha256(key);
                retained.successor_owner =
                    transition.context.decision_player;

                Representative representative{
                    .key = key,
                    .state = transition.state,
                    .context = transition.context,
                    .actions = transition.legal_actions,
                    .owner =
                        transition.context.decision_player,
                };
                const auto [found, inserted] =
                    representatives.emplace(
                        retained
                            .successor_information_set_fingerprint,
                        std::move(representative));
                if (!inserted &&
                    (!(found->second.key == key) ||
                     found->second.owner !=
                         retained.successor_owner)) {
                    throw std::logic_error(
                        "AQ1-BL0 successor information digest "
                        "collision");
                }
            }
            internal.transitions.push_back(
                std::move(retained));
        }
        internal_actions.push_back(std::move(internal));
    }

    result.successor_evaluations.reserve(
        representatives.size());
    for (const auto& [fingerprint, representative] :
         representatives) {
        result.successor_evaluations.push_back(
            evaluate_successor(
                representative, original_decks, legacy_c16,
                root_seed, fingerprint));
    }

    result.actions.reserve(internal_actions.size());
    for (const InternalRootAction& internal :
         internal_actions) {
        RootActionTarget target{
            .descriptor = internal.descriptor,
            .action = internal.action,
            .root_accounting = internal.accounting,
        };
        std::map<std::string, std::vector<std::size_t>>
            group_members;
        std::map<std::string, std::size_t> group_owners;
        for (const InternalRootTransition& transition :
             internal.transitions) {
            if (transition.terminal) {
                target.terminal_particles.push_back({
                    .world_index = transition.world_index,
                    .root_owner_value =
                        transition
                            .terminal_root_owner_value,
                });
            } else {
                group_members[
                    transition
                        .successor_information_set_fingerprint]
                    .push_back(transition.world_index);
                const auto [found, inserted] =
                    group_owners.emplace(
                        transition
                            .successor_information_set_fingerprint,
                        transition.successor_owner);
                if (!inserted &&
                    found->second !=
                        transition.successor_owner) {
                    throw std::logic_error(
                        "AQ1-BL0 grouped different decision "
                        "owners before maximization");
                }
            }
        }

        std::vector<bellman::SuccessorGroup> backup_groups;
        backup_groups.reserve(group_members.size());
        for (const auto& [fingerprint, members] :
             group_members) {
            const auto& evaluation =
                require_successor_evaluation(
                    result.successor_evaluations,
                    fingerprint);
            const std::size_t owner =
                group_owners.at(fingerprint);
            if (evaluation.successor_owner != owner) {
                throw std::logic_error(
                    "AQ1-BL0 successor-owner perspective drifted");
            }
            const bellman::OwnerRelation relation =
                owner == result.root_owner
                    ? bellman::OwnerRelation::SameOwner
                    : bellman::OwnerRelation::OpponentOwner;
            target.successor_groups.push_back({
                .information_set_fingerprint = fingerprint,
                .successor_owner = owner,
                .relation = relation,
                .root_world_indices = members,
                .successor_owner_value =
                    evaluation.cross_fit.value,
            });
            backup_groups.push_back({
                .fingerprint = fingerprint,
                .mass = members.size(),
                .world_indices = members,
                .relation = relation,
                .successor_owner_value =
                    evaluation.cross_fit.value,
            });
        }
        const auto backed = bellman::back_up_root_target(
            kRootWorlds, target.terminal_particles,
            backup_groups);
        if (backed.particles != kRootWorlds ||
            backed.terminal_particles !=
                target.terminal_particles.size() ||
            backed.terminal_particles +
                    backed.same_owner_particles +
                    backed.opponent_owner_particles !=
                kRootWorlds) {
            throw std::logic_error(
                "AQ1-BL0 root particle partition drifted");
        }
        target.value = backed.value;
        result.actions.push_back(std::move(target));
    }

    result.accounting.root_actions =
        result.actions.size();
    result.accounting.root_determinizations =
        result.root_worlds.size();
    for (const RootActionTarget& action : result.actions) {
        result.accounting.root_terminal_particles +=
            action.terminal_particles.size();
        result.accounting.root_boundary_particles +=
            kRootWorlds - action.terminal_particles.size();
        result.accounting.successor_group_occurrences +=
            action.successor_groups.size();
        for (const SuccessorParticleGroup& group :
             action.successor_groups) {
            switch (group.relation) {
            case bellman::OwnerRelation::SameOwner:
                ++result.accounting
                      .same_owner_group_occurrences;
                result.accounting.same_owner_root_particles +=
                    group.root_world_indices.size();
                break;
            case bellman::OwnerRelation::OpponentOwner:
                ++result.accounting
                      .opponent_owner_group_occurrences;
                result.accounting
                    .opponent_owner_root_particles +=
                    group.root_world_indices.size();
                break;
            }
        }
        add_accounting(
            result.accounting.root_macros,
            action.root_accounting);
    }
    result.accounting.unique_successor_information_sets =
        result.successor_evaluations.size();
    result.accounting.successor_determinizations =
        result.successor_evaluations.size() *
        2 * kSuccessorWorlds;
    for (const SuccessorEvaluation& evaluation :
         result.successor_evaluations) {
        result.accounting.successor_actions +=
            evaluation.actions.size();
        add_accounting(
            result.accounting.successor_macros,
            evaluation.bank_a.accounting);
        add_accounting(
            result.accounting.successor_macros,
            evaluation.bank_b.accounting);
    }

    validate_root_targets(result);
    return result;
}

void validate_root_targets(const RootTargets& targets) {
    if (targets.root_information_set_fingerprint.empty() ||
        targets.root_owner >= 2 ||
        targets.root_worlds.size() != kRootWorlds ||
        targets.actions.size() < 2) {
        throw std::invalid_argument(
            "AQ1-BL0 retained root dimensions are invalid");
    }
    for (std::size_t world = 0;
         world < targets.root_worlds.size(); ++world) {
        if (targets.root_worlds[world] !=
            root_world_seeds(
                targets.root_seed,
                targets.root_information_set_fingerprint,
                world)) {
            throw std::invalid_argument(
                "AQ1-BL0 root common-world seed drifted");
        }
    }

    TeacherAccounting observed{
        .root_actions = targets.actions.size(),
        .root_determinizations =
            targets.root_worlds.size(),
    };
    std::vector<bool> referenced(
        targets.successor_evaluations.size(), false);
    for (std::size_t index = 0;
         index < targets.actions.size(); ++index) {
        const RootActionTarget& action =
            targets.actions[index];
        if (action.descriptor.empty() ||
            action.descriptor !=
                probes::stable_priority_action_descriptor(
                    action.action) ||
            !probability(action.value) ||
            (index > 0 &&
             targets.actions[index - 1].descriptor >=
                 action.descriptor)) {
            throw std::invalid_argument(
                "AQ1-BL0 root action identity is not canonical");
        }
        validate_macro_accounting(
            action.root_accounting, false);
        if (action.root_accounting.transitions !=
                kRootWorlds ||
            action.root_accounting.terminal_transitions !=
                action.terminal_particles.size()) {
            throw std::invalid_argument(
                "AQ1-BL0 root action macro count drifted");
        }

        std::vector<bellman::SuccessorGroup> groups;
        groups.reserve(action.successor_groups.size());
        for (std::size_t group_index = 0;
             group_index < action.successor_groups.size();
             ++group_index) {
            const SuccessorParticleGroup& group =
                action.successor_groups[group_index];
            if (group.information_set_fingerprint.empty() ||
                group.root_world_indices.empty() ||
                !probability(group.successor_owner_value) ||
                group.successor_owner >= 2 ||
                group.relation !=
                    (group.successor_owner ==
                             targets.root_owner
                         ? bellman::OwnerRelation::SameOwner
                         : bellman::OwnerRelation::
                               OpponentOwner) ||
                (group_index > 0 &&
                 action.successor_groups[group_index - 1]
                         .information_set_fingerprint >=
                     group.information_set_fingerprint)) {
                throw std::invalid_argument(
                    "AQ1-BL0 successor particle group is "
                    "invalid");
            }
            const auto found = std::lower_bound(
                targets.successor_evaluations.begin(),
                targets.successor_evaluations.end(),
                group.information_set_fingerprint,
                [](const SuccessorEvaluation& evaluation,
                   const std::string& fingerprint) {
                    return evaluation
                               .information_set_fingerprint <
                           fingerprint;
                });
            if (found ==
                    targets.successor_evaluations.end() ||
                found->information_set_fingerprint !=
                    group.information_set_fingerprint ||
                found->successor_owner !=
                    group.successor_owner ||
                !same_bits(
                    found->cross_fit.value,
                    group.successor_owner_value)) {
                throw std::invalid_argument(
                    "AQ1-BL0 successor group has no matching "
                    "owner-perspective evaluation");
            }
            referenced[static_cast<std::size_t>(
                found -
                targets.successor_evaluations.begin())] = true;
            groups.push_back({
                .fingerprint =
                    group.information_set_fingerprint,
                .mass = group.root_world_indices.size(),
                .world_indices =
                    group.root_world_indices,
                .relation = group.relation,
                .successor_owner_value =
                    group.successor_owner_value,
            });
        }
        const auto backed = bellman::back_up_root_target(
            kRootWorlds, action.terminal_particles, groups);
        if (!same_bits(backed.value, action.value) ||
            backed.terminal_particles !=
                action.terminal_particles.size() ||
            action.root_accounting.boundary_transitions !=
                backed.same_owner_particles +
                    backed.opponent_owner_particles) {
            throw std::invalid_argument(
                "AQ1-BL0 backed target or particle accounting "
                "drifted");
        }

        observed.root_terminal_particles +=
            action.terminal_particles.size();
        observed.root_boundary_particles +=
            kRootWorlds - action.terminal_particles.size();
        observed.successor_group_occurrences +=
            action.successor_groups.size();
        for (const SuccessorParticleGroup& group :
             action.successor_groups) {
            switch (group.relation) {
            case bellman::OwnerRelation::SameOwner:
                ++observed.same_owner_group_occurrences;
                observed.same_owner_root_particles +=
                    group.root_world_indices.size();
                break;
            case bellman::OwnerRelation::OpponentOwner:
                ++observed.opponent_owner_group_occurrences;
                observed.opponent_owner_root_particles +=
                    group.root_world_indices.size();
                break;
            }
        }
        add_accounting(
            observed.root_macros,
            action.root_accounting);
    }

    std::string previous_fingerprint;
    for (std::size_t index = 0;
         index < targets.successor_evaluations.size();
         ++index) {
        const SuccessorEvaluation& evaluation =
            targets.successor_evaluations[index];
        if (evaluation.information_set_fingerprint.empty() ||
            evaluation.successor_owner >= 2 ||
            evaluation.actions.size() < 2 ||
            (!previous_fingerprint.empty() &&
             previous_fingerprint >=
                 evaluation
                     .information_set_fingerprint)) {
            throw std::invalid_argument(
                "AQ1-BL0 successor evaluation identity is "
                "invalid");
        }
        previous_fingerprint =
            evaluation.information_set_fingerprint;
        validate_bank(
            targets, evaluation, evaluation.bank_a,
            information::SeedBank::A);
        validate_bank(
            targets, evaluation, evaluation.bank_b,
            information::SeedBank::B);
        if (evaluation.bank_a.stream_key ==
                evaluation.bank_b.stream_key ||
            bellman::cross_fit_v0(
                evaluation.bank_a.actions,
                evaluation.bank_b.actions) !=
                evaluation.cross_fit) {
            throw std::invalid_argument(
                "AQ1-BL0 successor cross-fit drifted");
        }
        observed.successor_actions +=
            evaluation.actions.size();
        add_accounting(
            observed.successor_macros,
            evaluation.bank_a.accounting);
        add_accounting(
            observed.successor_macros,
            evaluation.bank_b.accounting);
    }
    if (!std::all_of(
            referenced.begin(), referenced.end(),
            [](bool value) { return value; })) {
        throw std::invalid_argument(
            "AQ1-BL0 retained an unreferenced successor "
            "evaluation");
    }
    observed.unique_successor_information_sets =
        targets.successor_evaluations.size();
    observed.successor_determinizations =
        targets.successor_evaluations.size() *
        2 * kSuccessorWorlds;
    if (observed != targets.accounting ||
        observed.same_owner_group_occurrences +
                observed.opponent_owner_group_occurrences !=
            observed.successor_group_occurrences ||
        observed.same_owner_root_particles +
                observed.opponent_owner_root_particles !=
            observed.root_boundary_particles ||
        observed.root_terminal_particles +
                observed.same_owner_root_particles +
                observed.opponent_owner_root_particles !=
            targets.actions.size() * kRootWorlds ||
        observed.root_macros.transitions !=
            targets.actions.size() * kRootWorlds) {
        throw std::invalid_argument(
            "AQ1-BL0 aggregate accounting does not cross-sum");
    }
    if (!targets.successor_evaluations.empty()) {
        validate_macro_accounting(
            targets.accounting.successor_macros, true);
    } else if (
        targets.accounting.successor_macros !=
        MacroAccounting{}) {
        throw std::invalid_argument(
            "AQ1-BL0 empty successor set retained macro "
            "accounting");
    }
}

} // namespace old_school::action_q_bellman_teacher
