#include "old_school/fq4_priority_fit.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_dominance.hpp"
#include "old_school/fq0_dominance_transition.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/oc1_action_scoring.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::fq4_priority_fit {
namespace {

namespace dominance = fq0_dominance;
namespace transition = fq0_dominance_transition;
namespace information = fq0_information_set;
namespace scoring = oc1_action_scoring;
namespace probe_data = probes;
namespace integrity = artifact_integrity;

constexpr std::uint64_t kRegisteredRootSeedBase =
    202607262351ULL;
constexpr std::size_t kRegisteredBlockCount = 8;
constexpr double kConstraintRatio =
    1.105170185988091368035982909368728415202;

struct RootContract {
    std::string_view stable_id;
    std::string_view information_action_fingerprint;
    std::size_t legal_actions;
    std::string_view pass_descriptor;
    std::span<const std::string_view> descriptors;
    std::span<const std::string_view> dominated_descriptors;
};

constexpr std::string_view kGreenDescriptors[] = {
    "growth-own-summoning-sick-grizzly-bears",
    "pass",
};
constexpr std::string_view kGreenDominated[] = {
    "growth-own-summoning-sick-grizzly-bears",
};
constexpr std::string_view kBlueDescriptors[] = {
    "braingeyser-x0-opponent",
    "braingeyser-x0-self",
    "braingeyser-x1-opponent",
    "braingeyser-x1-self",
    "pass",
};
constexpr std::string_view kBlueDominated[] = {
    "braingeyser-x0-opponent",
    "braingeyser-x0-self",
};
constexpr std::string_view kRuDescriptors[] = {
    "kind-0.card-0.x-0",
    "kind-2.card-14.x-0",
    "kind-7.card-17.x-0.target-player-0",
    "kind-7.card-17.x-0.target-player-1",
    "kind-7.card-17.x-1.target-player-0",
    "kind-7.card-17.x-1.target-player-1",
    "kind-7.card-17.x-2.target-player-0",
    "kind-7.card-17.x-2.target-player-1",
};
constexpr std::string_view kRuDominated[] = {
    "kind-7.card-17.x-0.target-player-0",
    "kind-7.card-17.x-0.target-player-1",
};

constexpr RootContract kRootContracts[] = {
    {
        "field.green.second-main-sick-bear-growth.v1",
        "6bf340aaaca49e8a",
        2,
        "pass",
        kGreenDescriptors,
        kGreenDominated,
    },
    {
        "control.blue.braingeyser-x0.v1",
        "a68cd5b38da84990",
        5,
        "pass",
        kBlueDescriptors,
        kBlueDominated,
    },
    {
        "validation.ru.disintegrate-hold-x0.v1",
        "04d02e0ea36d34be",
        8,
        "kind-0.card-0.x-0",
        kRuDescriptors,
        kRuDominated,
    },
};

constexpr std::size_t registered_constraint_count() {
    std::size_t result = 0;
    for (const RootContract& contract : kRootContracts) {
        result += contract.dominated_descriptors.size();
    }
    return result;
}

static_assert(
    std::size(kRootContracts) == kExpectedTrainingRoots);
static_assert(
    registered_constraint_count() ==
    kExpectedDominanceConstraints);

struct ControlContract {
    std::string_view name;
    std::string_view stable_id;
};

constexpr ControlContract kControlContracts[] = {
    {
        "live Force Spike",
        "control.blue.force-spike-live-gray-ogre.v1",
    },
    {
        "useful Giant Growth",
        "green.bolt-on-bear-response.v3",
    },
    {
        "productive Counterspell blue.counter-fire-elemental.v3",
        "blue.counter-fire-elemental.v3",
    },
    {
        "productive Counterspell blue.counter-lethal-bolt.v3",
        "blue.counter-lethal-bolt.v3",
    },
    {
        "productive Counterspell blue.counter-war.v3",
        "blue.counter-war.v3",
    },
    {
        "payable Force Spike collateral",
        "control.blue.force-spike-payable-gray-ogre.v1",
    },
    {
        "redundant same-target Counterspell",
        "control.blue.counter-redundant-same-target.v1",
    },
    {
        "development RU X=0 and positive-X collateral",
        "ru.disintegrate-player-x.v3",
    },
    {
        "control.blue.braingeyser-x0.v1 productive-sibling collateral",
        "control.blue.braingeyser-x0.v1",
    },
    {
        "validation.ru.disintegrate-hold-x0.v1 productive-sibling collateral",
        "validation.ru.disintegrate-hold-x0.v1",
    },
};

static_assert(
    std::size(kControlContracts) == kExpectedControls);

struct CanonicalRoot {
    probe_data::DecisionProbe probe;
    std::vector<std::string> descriptors;
    std::vector<PriorityAction> actions;
};

struct DiscoveredRoot {
    CanonicalRoot canonical;
    std::size_t pass_index = 0;
    std::vector<std::size_t> dominated_indices;
    std::vector<std::size_t> strict_world_counts;
};

struct PolicyScores {
    scoring::DecisionScore production;
    std::vector<double> logits;
    std::vector<double> combined;
    std::vector<std::string> exact_support;
};

struct TrainingInput {
    DiscoveredRoot root;
    PolicyScores parent;
    ReverseKlProjection projection;
    LearnedValuePriorityTrainingExample example;
    std::vector<double> latent_parent;
};

bool same_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool bit_identical(const std::vector<double>& first,
                   const std::vector<double>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index) {
        if (!same_double(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool bit_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index) {
        if (!bit_identical(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

void digest_u64(
    integrity::Sha256Accumulator& digest,
    std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size();
         ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >>
             (8U * static_cast<unsigned>(
                        bytes.size() - 1 - index))) &
            0xffU);
    }
    digest.update(bytes);
}

void digest_bool(
    integrity::Sha256Accumulator& digest, bool value) {
    digest_u64(digest, value ? 1U : 0U);
}

void digest_string(
    integrity::Sha256Accumulator& digest,
    std::string_view value) {
    digest_u64(digest, value.size());
    digest.update(value);
}

void digest_double(
    integrity::Sha256Accumulator& digest, double value) {
    digest_u64(
        digest, std::bit_cast<std::uint64_t>(value));
}

void digest_double_vector(
    integrity::Sha256Accumulator& digest,
    const std::vector<double>& values) {
    digest_u64(digest, values.size());
    for (const double value : values) {
        digest_double(digest, value);
    }
}

void digest_production_score(
    integrity::Sha256Accumulator& digest,
    const scoring::DecisionScore& score) {
    digest_string(digest, score.stable_id);
    digest_u64(
        digest,
        static_cast<std::uint64_t>(score.decision_kind));
    digest_u64(
        digest,
        static_cast<std::uint64_t>(score.score_mode));
    const auto& recipe = score.recipe;
    digest_u64(
        digest,
        static_cast<std::uint64_t>(recipe.seed_source));
    digest_string(digest, recipe.seed_tag);
    digest_u64(digest, recipe.seed_base);
    digest_bool(digest, recipe.resolved_seed.has_value());
    if (recipe.resolved_seed.has_value()) {
        digest_u64(digest, *recipe.resolved_seed);
    }
    digest_u64(digest, recipe.worlds);
    digest_u64(digest, recipe.horizon_turns);
    digest_u64(digest, recipe.rollouts_per_world);
    digest_bool(digest, recipe.blend_shallow_prior);
    digest_u64(digest, recipe.evaluation_threads);
    digest_bool(digest, recipe.value_mirror);
    digest_double(
        digest, recipe.value_continuation_epsilon);
    digest_double(
        digest, recipe.value_priority_residual_weight);
    digest_bool(digest, recipe.value_pass_dominance);
    digest_u64(
        digest,
        static_cast<std::uint64_t>(
            recipe.value_continuation_controller));
    digest_u64(digest, score.actions.size());
    for (const auto& row : score.actions) {
        digest_string(digest, row.descriptor);
        digest_double_vector(digest, row.raw_samples);
        digest_double(digest, row.raw_score);
    }
    digest_u64(digest, score.selected_support.size());
    for (const std::string& descriptor :
         score.selected_support) {
        digest_string(digest, descriptor);
    }
    digest_bool(digest, score.deterministic_selection);
    digest_u64(
        digest, score.accounting.sampled_worlds);
    digest_u64(
        digest, score.accounting.rollout_evaluations);
    digest_u64(
        digest, score.accounting.terminal_evaluations);
    digest_u64(
        digest,
        score.accounting.bootstrapped_evaluations);
}

std::string training_input_sha256(
    const std::vector<TrainingInput>& inputs) {
    integrity::Sha256Accumulator digest;
    digest_string(
        digest,
        "old-school-fq4-d0b-training-input-v1");
    digest_u64(digest, inputs.size());
    for (const TrainingInput& input : inputs) {
        const CanonicalRoot& root =
            input.root.canonical;
        digest_string(digest, root.probe.stable_id);
        digest_string(
            digest,
            probe_data::bsr_information_action_fingerprint(
                root.probe));
        digest_u64(digest, root.descriptors.size());
        for (const std::string& descriptor :
             root.descriptors) {
            digest_string(digest, descriptor);
        }
        digest_u64(digest, input.root.pass_index);
        digest_u64(
            digest,
            input.root.dominated_indices.size());
        for (const std::size_t index :
             input.root.dominated_indices) {
            digest_u64(digest, index);
        }
        digest_u64(
            digest,
            input.root.strict_world_counts.size());
        for (const std::size_t count :
             input.root.strict_world_counts) {
            digest_u64(digest, count);
        }
        digest_production_score(
            digest, input.parent.production);
        digest_double_vector(
            digest, input.parent.logits);
        digest_double_vector(
            digest, input.parent.combined);
        digest_double_vector(
            digest, input.latent_parent);
        digest_double_vector(
            digest, input.projection.probabilities);
        digest_u64(
            digest,
            input.projection
                .active_dominated_indices.size());
        for (const std::size_t index :
             input.projection
                 .active_dominated_indices) {
            digest_u64(digest, index);
        }
        digest_u64(
            digest, input.example.options.size());
        for (const std::vector<double>& option :
             input.example.options) {
            digest_double_vector(digest, option);
        }
        digest_double_vector(
            digest, input.example.base_scores);
        digest_double_vector(
            digest,
            input.example.target_probabilities);
        digest_double(digest, input.example.weight);
    }
    return digest.finish();
}

bool sorcery_actions_for(TurnPhase phase) {
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
            "FQ4-D0 Priority root is in a declaration phase");
    }
    throw std::invalid_argument(
        "FQ4-D0 Priority root has an invalid phase");
}

std::vector<probe_data::DecisionProbe> all_fixture_probes() {
    std::vector<probe_data::DecisionProbe> result;
    const auto append =
        [&](std::vector<probe_data::DecisionProbe> source) {
            result.insert(
                result.end(),
                std::make_move_iterator(source.begin()),
                std::make_move_iterator(source.end()));
        };
    append(probe_data::make_probe_dev_v3());
    append(probe_data::make_probe_validation_v1());
    append(probe_data::make_force_spike_policy_controls_v1());
    append(probe_data::make_counter_composition_controls_v1());
    append(probe_data::make_braingeyser_x_zero_control_v1());
    append(probe_data::make_field_regressions_v1());
    return result;
}

const probe_data::DecisionProbe& require_probe(
    const std::vector<probe_data::DecisionProbe>& corpus,
    std::string_view stable_id) {
    const probe_data::DecisionProbe* found = nullptr;
    for (const auto& probe : corpus) {
        if (probe.stable_id != stable_id) {
            continue;
        }
        if (found != nullptr) {
            throw std::logic_error(
                "FQ4-D0 fixture corpus contains duplicate root '" +
                std::string(stable_id) + "'");
        }
        found = &probe;
    }
    if (found == nullptr) {
        throw std::logic_error(
            "FQ4-D0 fixture corpus is missing root '" +
            std::string(stable_id) + "'");
    }
    return *found;
}

const PriorityAction& priority_action(
    const probe_data::Candidate& candidate,
    std::string_view stable_id) {
    const auto* action =
        std::get_if<PriorityAction>(&candidate.action);
    if (action == nullptr) {
        throw std::logic_error(
            std::string(stable_id) +
            ": FQ4-D0 root contains a non-Priority candidate");
    }
    return *action;
}

CanonicalRoot canonicalize_root(
    const probe_data::DecisionProbe& source) {
    if (source.decision_kind !=
        probe_data::DecisionKind::Priority) {
        throw std::logic_error(
            source.stable_id +
            ": FQ4-D0 requires a Priority root");
    }
    const auto validation = probe_data::validate_probe(source);
    if (!validation.ok()) {
        throw std::logic_error(
            source.stable_id +
            ": FQ4-D0 root failed probe validation");
    }

    CanonicalRoot result{.probe = source};
    std::sort(
        result.probe.candidates.begin(),
        result.probe.candidates.end(),
        [](const auto& first, const auto& second) {
            return first.descriptor < second.descriptor;
        });
    const bool sorcery =
        sorcery_actions_for(result.probe.phase);
    const std::vector<PriorityAction> legal =
        legal_priority_actions(
            result.probe.state, result.probe.root_player,
            sorcery);
    if (legal.size() != result.probe.candidates.size() ||
        legal.size() < 2) {
        throw std::logic_error(
            source.stable_id +
            ": FQ4-D0 candidate list is not the complete "
            "nontrivial legal set");
    }

    std::vector<PriorityAction> seen;
    for (const auto& candidate : result.probe.candidates) {
        if (candidate.descriptor.empty() ||
            (!result.descriptors.empty() &&
             result.descriptors.back() ==
                 candidate.descriptor)) {
            throw std::logic_error(
                source.stable_id +
                ": FQ4-D0 candidate descriptors are empty or "
                "duplicated");
        }
        const PriorityAction& action =
            priority_action(candidate, source.stable_id);
        if (std::find(legal.begin(), legal.end(), action) ==
                legal.end() ||
            std::find(seen.begin(), seen.end(), action) !=
                seen.end()) {
            throw std::logic_error(
                source.stable_id +
                ": FQ4-D0 candidates do not biject onto legal "
                "actions");
        }
        result.descriptors.push_back(candidate.descriptor);
        result.actions.push_back(action);
        seen.push_back(action);
    }
    return result;
}

std::size_t require_descriptor_index(
    const CanonicalRoot& root, std::string_view descriptor) {
    const auto found = std::find(
        root.descriptors.begin(), root.descriptors.end(),
        descriptor);
    if (found == root.descriptors.end()) {
        throw std::logic_error(
            root.probe.stable_id +
            ": FQ4-D0 expected descriptor is missing: " +
            std::string(descriptor));
    }
    return static_cast<std::size_t>(
        found - root.descriptors.begin());
}

std::uint64_t registered_world_seed(
    const RootContract& contract, std::size_t world) {
    return information::derive_indexed_seed(
        kRegisteredRootSeedBase,
        {
            .domain =
                information::SeedDomain::
                    RootDeterminization,
            .scope = std::string(contract.stable_id),
            .group = std::string(
                contract.information_action_fingerprint),
            .bank = information::SeedBank::Root,
            .block = kRegisteredBlockCount,
            .world = world,
        });
}

DiscoveredRoot discover_dominance(
    const probe_data::DecisionProbe& source,
    const RootContract& contract) {
    if (probe_data::bsr_information_action_fingerprint(source) !=
        contract.information_action_fingerprint) {
        throw std::logic_error(
            std::string(contract.stable_id) +
            ": FQ4-D0 information/action fingerprint drifted");
    }
    DiscoveredRoot result{
        .canonical = canonicalize_root(source),
    };
    if (result.canonical.probe.stable_id !=
            contract.stable_id ||
        result.canonical.actions.size() !=
            contract.legal_actions) {
        throw std::logic_error(
            std::string(contract.stable_id) +
            ": FQ4-D0 legal-action census drifted");
    }
    std::vector<std::string> expected_descriptors;
    expected_descriptors.reserve(contract.descriptors.size());
    for (const std::string_view descriptor :
         contract.descriptors) {
        expected_descriptors.emplace_back(descriptor);
    }
    if (result.canonical.descriptors !=
        expected_descriptors) {
        throw std::logic_error(
            std::string(contract.stable_id) +
            ": FQ4-D0 complete descriptor census drifted");
    }
    for (std::size_t index = 0;
         index < result.canonical.actions.size(); ++index) {
        if (result.canonical.actions[index].kind ==
            PriorityActionKind::Pass) {
            result.pass_index = index;
        }
    }
    const std::size_t pass_count =
        static_cast<std::size_t>(std::count_if(
            result.canonical.actions.begin(),
            result.canonical.actions.end(),
            [](const PriorityAction& action) {
                return action.kind ==
                       PriorityActionKind::Pass;
            }));
    if (pass_count != 1) {
        throw std::logic_error(
            std::string(contract.stable_id) +
            ": FQ4-D0 root does not have exactly one Pass");
    }
    if (result.canonical.descriptors[
            result.pass_index] !=
        contract.pass_descriptor) {
        throw std::logic_error(
            std::string(contract.stable_id) +
            ": FQ4-D0 Pass descriptor drifted");
    }

    result.strict_world_counts.assign(
        result.canonical.actions.size(), 0);
    const std::string information_fingerprint(
        contract.information_action_fingerprint);
    for (std::size_t world = 0;
         world < kDominanceWorlds; ++world) {
        const GameState sampled = sample_determinization(
            result.canonical.probe.state,
            result.canonical.probe.original_decks,
            result.canonical.probe.root_player,
            registered_world_seed(contract, world));
        const dominance::Settlement pass =
            transition::advance_to_next_first_main(
                result.canonical.probe, sampled,
                result.pass_index, information_fingerprint);
        if (!pass.complete() ||
            pass.unresolved_transient_choice_effect()) {
            throw std::logic_error(
                std::string(contract.stable_id) +
                ": FQ4-D0 Pass settlement is incomplete");
        }
        for (std::size_t action = 0;
             action < result.canonical.actions.size();
             ++action) {
            if (action == result.pass_index) {
                continue;
            }
            const dominance::Settlement candidate =
                transition::advance_to_next_first_main(
                    result.canonical.probe, sampled, action,
                    information_fingerprint);
            const dominance::Comparison comparison =
                dominance::compare(
                    pass, candidate,
                    result.canonical.probe.root_player);
            if (!candidate.complete()) {
                throw std::logic_error(
                    std::string(contract.stable_id) +
                    ": FQ4-D0 candidate settlement is incomplete "
                    "at world " +
                    std::to_string(world) + " for " +
                    result.canonical.descriptors[action]);
            }
            if (comparison.orientation ==
                dominance::Orientation::
                    FirstDominatesSecond) {
                ++result.strict_world_counts[action];
            }
        }
    }
    for (std::size_t action = 0;
         action < result.strict_world_counts.size(); ++action) {
        if (result.strict_world_counts[action] ==
            kDominanceWorlds) {
            result.dominated_indices.push_back(action);
        }
    }

    std::vector<std::string> actual;
    actual.reserve(result.dominated_indices.size());
    for (const std::size_t index :
         result.dominated_indices) {
        actual.push_back(
            result.canonical.descriptors[index]);
    }
    std::vector<std::string> expected;
    expected.reserve(
        contract.dominated_descriptors.size());
    for (const std::string_view descriptor :
         contract.dominated_descriptors) {
        expected.emplace_back(descriptor);
    }
    std::sort(expected.begin(), expected.end());
    if (actual != expected) {
        throw std::logic_error(
            std::string(contract.stable_id) +
            ": FQ4-D0 generic dominance discovery census "
            "drifted");
    }
    return result;
}

std::vector<double> softmax_scores(
    const std::vector<double>& scores) {
    if (scores.empty() ||
        !std::all_of(
            scores.begin(), scores.end(),
            [](double value) {
                return std::isfinite(value);
            })) {
        throw std::invalid_argument(
            "FQ4-D0 softmax requires finite scores");
    }
    const double maximum =
        *std::max_element(scores.begin(), scores.end());
    std::vector<double> probabilities;
    probabilities.reserve(scores.size());
    long double sum = 0.0L;
    for (const double score : scores) {
        const double probability =
            std::exp(
                (score - maximum) /
                kPolicyTemperature);
        probabilities.push_back(probability);
        sum += static_cast<long double>(probability);
    }
    for (double& probability : probabilities) {
        probability =
            static_cast<double>(
                static_cast<long double>(probability) /
                sum);
    }
    return probabilities;
}

bool production_recipe_exact(
    const scoring::DecisionScore& score) {
    const auto& recipe = score.recipe;
    return score.decision_kind ==
               probe_data::DecisionKind::Priority &&
           score.score_mode ==
               scoring::ScoreMode::
                   ProductionPrioritySearch &&
           recipe.seed_source ==
               scoring::SeedSource::Derived &&
           recipe.seed_tag == scoring::kProductionTag &&
           recipe.seed_base ==
               scoring::kProductionSeedBase &&
           recipe.resolved_seed.has_value() &&
           recipe.worlds == scoring::kProductionWorlds &&
           recipe.horizon_turns ==
               scoring::kProductionHorizonTurns &&
           recipe.rollouts_per_world ==
               scoring::kProductionRolloutsPerWorld &&
           recipe.blend_shallow_prior ==
               scoring::kProductionBlendShallowPrior &&
           recipe.evaluation_threads ==
               scoring::kProductionEvaluationThreads &&
           recipe.value_mirror &&
           same_double(
               recipe.value_continuation_epsilon, 0.0) &&
           same_double(
               recipe.value_priority_residual_weight, 0.0) &&
           !recipe.value_pass_dominance &&
           recipe.value_continuation_controller ==
               LearnedContinuationController::Legacy &&
           score.accounting.sampled_worlds ==
               scoring::kProductionWorlds &&
           score.accounting.rollout_evaluations ==
               score.actions.size() *
                   scoring::kProductionWorlds *
                   scoring::kProductionRolloutsPerWorld &&
           score.accounting.terminal_evaluations +
                   score.accounting.bootstrapped_evaluations ==
               score.accounting.rollout_evaluations;
}

PolicyScores evaluate_policy(
    const CanonicalRoot& root,
    const std::shared_ptr<const LearnedModel>& model,
    const CanonicalRoot* paired_visible_root = nullptr) {
    PolicyScores result{
        .production =
            paired_visible_root == nullptr
                ? scoring::score_production(root.probe, model)
                : scoring::score_production_hidden_clone(
                      paired_visible_root->probe,
                      root.probe, model),
    };
    if (!production_recipe_exact(result.production) ||
        result.production.actions.size() !=
            root.actions.size()) {
        throw std::logic_error(
            root.probe.stable_id +
            ": FQ4-D0 production scoring recipe or row "
            "census drifted");
    }
    std::vector<double> base;
    base.reserve(result.production.actions.size());
    for (std::size_t index = 0;
         index < result.production.actions.size(); ++index) {
        const auto& row = result.production.actions[index];
        if (row.descriptor != root.descriptors[index]) {
            throw std::logic_error(
                root.probe.stable_id +
                ": FQ4-D0 production rows lost descriptor "
                "canonical order");
        }
        base.push_back(row.raw_score);
    }
    const auto residual =
        diagnose_learned_value_priority_residual(
            root.probe.state, root.probe.root_player,
            sorcery_actions_for(root.probe.phase),
            root.probe.phase,
            root.probe.consecutive_passes, root.actions,
            model, kResidualWeight);
    if (residual.policy_logits.size() != base.size() ||
        residual.residuals.size() != base.size()) {
        throw std::logic_error(
            root.probe.stable_id +
            ": FQ4-D0 Priority residual row count drifted");
    }
    result.logits = residual.policy_logits;
    result.combined = base;
    for (std::size_t index = 0;
         index < result.combined.size(); ++index) {
        result.combined[index] += residual.residuals[index];
    }
    const double maximum =
        *std::max_element(
            result.combined.begin(), result.combined.end());
    for (std::size_t index = 0;
         index < result.combined.size(); ++index) {
        if (result.combined[index] == maximum) {
            result.exact_support.push_back(
                root.descriptors[index]);
        }
    }
    return result;
}

std::vector<std::vector<double>> policy_options(
    const CanonicalRoot& root) {
    std::vector<std::vector<double>> result;
    result.reserve(root.actions.size());
    for (const PriorityAction& action : root.actions) {
        result.push_back(
            learned_priority_policy_features(
                root.probe.state, root.probe.root_player,
                action,
                sorcery_actions_for(root.probe.phase),
                root.probe.phase,
                root.probe.consecutive_passes));
    }
    return result;
}

std::vector<double> behavior_target(
    const std::vector<double>& projected) {
    std::vector<double> result = projected;
    const double uniform =
        (1.0 - kSearchChoiceWeight) /
        static_cast<double>(result.size());
    for (double& probability : result) {
        probability =
            kSearchChoiceWeight * probability + uniform;
    }
    return result;
}

TrainingInput make_training_input(
    const probe_data::DecisionProbe& probe,
    const RootContract& contract,
    const std::shared_ptr<const LearnedModel>& parent,
    const CanonicalRoot* paired_visible_root = nullptr) {
    TrainingInput result{
        .root = discover_dominance(probe, contract),
    };
    result.parent =
        evaluate_policy(
            result.root.canonical, parent,
            paired_visible_root);
    result.latent_parent =
        softmax_scores(result.parent.combined);
    std::vector<StarConstraint> constraints;
    constraints.reserve(
        result.root.dominated_indices.size());
    for (const std::size_t dominated :
         result.root.dominated_indices) {
        constraints.push_back({
            .pass_index = result.root.pass_index,
            .dominated_index = dominated,
        });
    }
    result.projection = reverse_kl_i_projection(
        result.latent_parent, constraints,
        kConstraintRatio);
    std::vector<double> base;
    base.reserve(result.parent.production.actions.size());
    for (const auto& row :
         result.parent.production.actions) {
        base.push_back(row.raw_score);
    }
    result.example = {
        .options = policy_options(result.root.canonical),
        .base_scores = std::move(base),
        .target_probabilities =
            behavior_target(
                result.projection.probabilities),
        .weight = 1.0,
    };
    return result;
}

LearnedValuePriorityHeadUpdateConfig optimizer_config(
    std::size_t epochs = kD0bAnchorEpochs) {
    return {
        .batch_size = 3,
        .epochs = epochs,
        .learning_rate = 0.001,
        .beta1 = 0.9,
        .beta2 = 0.999,
        .epsilon = 1.0e-8,
        .global_gradient_norm_clip = 5.0,
        .seed = kOptimizerSeed,
        .residual_weight = kResidualWeight,
        .policy_temperature = kPolicyTemperature,
    };
}

std::vector<LearnedValuePriorityTrainingExample>
training_examples(
    const std::vector<TrainingInput>& inputs) {
    std::vector<LearnedValuePriorityTrainingExample> result;
    result.reserve(inputs.size());
    for (const TrainingInput& input : inputs) {
        result.push_back(input.example);
    }
    return result;
}

GameState hidden_repartition(
    const GameState& state, std::size_t observer) {
    if (observer >= state.players.size()) {
        throw std::invalid_argument(
            "FQ4-D0 hidden observer must be zero or one");
    }
    GameState result = state;
    PlayerState& hidden = result.players[1 - observer];
    for (std::size_t hand = 0;
         hand < hidden.hand.size(); ++hand) {
        for (std::size_t library = 0;
             library < hidden.library.size(); ++library) {
            if (hidden.hand[hand] ==
                hidden.library[library]) {
                continue;
            }
            std::swap(
                hidden.hand[hand],
                hidden.library[library]);
            if (observe_game_state(result, observer) !=
                observe_game_state(state, observer)) {
                throw std::logic_error(
                    "FQ4-D0 hidden repartition changed owner "
                    "observation");
            }
            return result;
        }
    }
    throw std::logic_error(
        "FQ4-D0 hidden repartition has no distinct card swap");
}

double score_for(
    const CanonicalRoot& root, const PolicyScores& scores,
    std::string_view descriptor) {
    return scores.combined[
        require_descriptor_index(root, descriptor)];
}

bool support_contains(
    const PolicyScores& scores,
    std::string_view descriptor) {
    return std::find(
               scores.exact_support.begin(),
               scores.exact_support.end(),
               descriptor) != scores.exact_support.end();
}

void record_parent_behavior_contract(
    FitReport& report, bool condition,
    std::string_view message) {
    if (!condition) {
        report.infrastructure_failures.push_back(
            "FQ4-D0 frozen parent behavior contract drift: " +
            std::string(message));
    }
}

void add_scientific_failure(
    FitReport& report, std::string message) {
    report.scientific_failures.push_back(
        std::move(message));
}

struct EvaluatedControl {
    CanonicalRoot root;
    PolicyScores parent;
    PolicyScores candidate;
};

std::string_view registered_control_fingerprint(
    std::string_view stable_id) {
    constexpr std::pair<std::string_view, std::string_view>
        fingerprints[] = {
            {
                "control.blue.force-spike-live-gray-ogre.v1",
                "b792d7434096d2cc",
            },
            {
                "control.blue.force-spike-payable-gray-ogre.v1",
                "8e24d4696a7c2ad5",
            },
            {
                "green.bolt-on-bear-response.v3",
                "f21baf227fe0161f",
            },
            {
                "blue.counter-fire-elemental.v3",
                "6c90355960714c47",
            },
            {
                "blue.counter-lethal-bolt.v3",
                "30ff11b9ec056b21",
            },
            {
                "blue.counter-war.v3",
                "fc276ae226a9f512",
            },
            {
                "control.blue.counter-redundant-same-target.v1",
                "faf53e39aba9e69b",
            },
            {
                "ru.disintegrate-player-x.v3",
                "6345aec096735eb9",
            },
        };
    const auto found = std::find_if(
        std::begin(fingerprints), std::end(fingerprints),
        [&](const auto& row) {
            return row.first == stable_id;
        });
    if (found == std::end(fingerprints)) {
        throw std::logic_error(
            "FQ4-D0 control has no registered fixture fingerprint: " +
            std::string(stable_id));
    }
    return found->second;
}

EvaluatedControl evaluate_control(
    const probe_data::DecisionProbe& probe,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    FitReport& report) {
    if (probe_data::bsr_information_action_fingerprint(probe) !=
        registered_control_fingerprint(probe.stable_id)) {
        throw std::logic_error(
            probe.stable_id +
            ": FQ4-D0 registered control fingerprint drifted");
    }
    EvaluatedControl result{
        .root = canonicalize_root(probe),
    };
    result.parent =
        evaluate_policy(result.root, parent);
    result.candidate =
        evaluate_policy(result.root, candidate);
    const bool identical =
        scoring::bit_identical(
            result.parent.production,
            result.candidate.production);
    report.all_production_base_and_accounting_bit_identical =
        report.all_production_base_and_accounting_bit_identical &&
        identical;
    if (!identical) {
        throw std::logic_error(
            probe.stable_id +
            ": FQ4-D0 candidate changed immutable production "
            "base scores or accounting");
    }
    return result;
}

ControlReport finish_control(
    FitReport& report, const EvaluatedControl& evaluated,
    std::string name, bool parent_contract,
    bool candidate_gate, bool enforce_parent_contract) {
    if (enforce_parent_contract) {
        record_parent_behavior_contract(
            report, parent_contract, name);
    }
    ControlReport control{
        .name = std::move(name),
        .stable_id = evaluated.root.probe.stable_id,
        .information_action_fingerprint =
            probe_data::bsr_information_action_fingerprint(
                evaluated.root.probe),
        .descriptors = evaluated.root.descriptors,
        .parent_exact_support =
            evaluated.parent.exact_support,
        .candidate_exact_support =
            evaluated.candidate.exact_support,
        .passed = candidate_gate,
    };
    if (!candidate_gate) {
        add_scientific_failure(
            report, "control failed: " + control.name);
    }
    return control;
}

bool same_sign(double first, double second) {
    return (first < 0.0 && second < 0.0) ||
           (first == 0.0 && second == 0.0) ||
           (first > 0.0 && second > 0.0);
}

void evaluate_named_controls(
    const std::vector<probe_data::DecisionProbe>& corpus,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    FitReport& report, bool enforce_parent_contract) {
    const auto support_is =
        [](const PolicyScores& scores,
           std::initializer_list<std::string_view> expected) {
            std::vector<std::string> values;
            values.reserve(expected.size());
            for (const std::string_view item : expected) {
                values.emplace_back(item);
            }
            std::sort(values.begin(), values.end());
            return scores.exact_support == values;
        };

    {
        const auto evaluated = evaluate_control(
            require_probe(
                corpus,
                kControlContracts[0].stable_id),
            parent, candidate, report);
        const double parent_delta =
            score_for(
                evaluated.root, evaluated.parent,
                "force-spike-gray-ogre") -
            score_for(evaluated.root, evaluated.parent, "pass");
        const double candidate_delta =
            score_for(
                evaluated.root, evaluated.candidate,
                "force-spike-gray-ogre") -
            score_for(
                evaluated.root, evaluated.candidate, "pass");
        report.controls.push_back(finish_control(
            report, evaluated,
            std::string(kControlContracts[0].name),
            parent_delta > 0.0 &&
                support_is(
                    evaluated.parent,
                    {"force-spike-gray-ogre"}),
            candidate_delta > 0.0 &&
                support_is(
                    evaluated.candidate,
                    {"force-spike-gray-ogre"}),
            enforce_parent_contract));
    }
    {
        const auto evaluated = evaluate_control(
            require_probe(
                corpus,
                kControlContracts[1].stable_id),
            parent, candidate, report);
        constexpr std::string_view growth =
            "growth-own-grizzly-bears";
        const double parent_delta =
            score_for(
                evaluated.root, evaluated.parent, growth) -
            score_for(
                evaluated.root, evaluated.parent, "pass");
        const double candidate_delta =
            score_for(
                evaluated.root, evaluated.candidate, growth) -
            score_for(
                evaluated.root, evaluated.candidate, "pass");
        const bool parent_contract =
            parent_delta > 0.0 &&
            support_is(
                evaluated.parent,
                {"growth-own-grizzly-bears"});
        const bool candidate_gate =
            candidate_delta > 0.0 &&
            support_is(
                evaluated.candidate,
                {"growth-own-grizzly-bears"});
        report.controls.push_back(finish_control(
            report, evaluated,
            std::string(kControlContracts[1].name),
            parent_contract, candidate_gate,
            enforce_parent_contract));
    }

    struct ProductiveCounterControl {
        std::size_t contract_index;
        std::string_view selected;
    };
    constexpr ProductiveCounterControl
        productive_counters[] = {
            {
                2,
                "counter-fire-elemental",
            },
            {
                3,
                "counter-lethal-lightning-bolt",
            },
            {
                4,
                "counter-opponent-counterspell",
            },
        };
    for (const auto& control :
         productive_counters) {
        const auto evaluated = evaluate_control(
            require_probe(
                corpus,
                kControlContracts[
                    control.contract_index].stable_id),
            parent, candidate, report);
        report.controls.push_back(finish_control(
            report, evaluated,
            std::string(
                kControlContracts[
                    control.contract_index].name),
            support_is(
                evaluated.parent, {control.selected}),
            support_is(
                evaluated.candidate, {control.selected}),
            enforce_parent_contract));
    }

    {
        const auto evaluated = evaluate_control(
            require_probe(
                corpus,
                kControlContracts[5].stable_id),
            parent, candidate, report);
        const double parent_delta =
            score_for(
                evaluated.root, evaluated.parent, "pass") -
            score_for(
                evaluated.root, evaluated.parent,
                "force-spike-gray-ogre");
        const double candidate_delta =
            score_for(
                evaluated.root, evaluated.candidate, "pass") -
            score_for(
                evaluated.root, evaluated.candidate,
                "force-spike-gray-ogre");
        report.controls.push_back(finish_control(
            report, evaluated,
            std::string(kControlContracts[5].name),
            parent_delta < 0.0 &&
                support_is(
                    evaluated.parent,
                    {"force-spike-gray-ogre"}),
            same_sign(parent_delta, candidate_delta) &&
                evaluated.parent.exact_support ==
                    evaluated.candidate.exact_support,
            enforce_parent_contract));
    }
    {
        const auto evaluated = evaluate_control(
            require_probe(
                corpus,
                kControlContracts[6].stable_id),
            parent, candidate, report);
        report.controls.push_back(finish_control(
            report, evaluated,
            std::string(kControlContracts[6].name),
            support_is(evaluated.parent, {"pass"}),
            support_is(evaluated.candidate, {"pass"}),
            enforce_parent_contract));
    }

    {
        const auto evaluated = evaluate_control(
            require_probe(
                corpus, kControlContracts[7].stable_id),
            parent, candidate, report);
        const std::size_t pass =
            require_descriptor_index(evaluated.root, "pass");
        bool parent_contract = true;
        bool candidate_gate =
            evaluated.parent.exact_support ==
            evaluated.candidate.exact_support;
        for (const std::string_view descriptor :
             {"disintegrate-x0-self-player",
              "disintegrate-x0-opponent-player"}) {
            const std::size_t action =
                require_descriptor_index(
                    evaluated.root, descriptor);
            parent_contract =
                parent_contract &&
                evaluated.parent.combined[pass] >
                    evaluated.parent.combined[action] &&
                !support_contains(
                    evaluated.parent, descriptor);
            candidate_gate =
                candidate_gate &&
                evaluated.candidate.combined[pass] >
                    evaluated.candidate.combined[action] &&
                !support_contains(
                    evaluated.candidate, descriptor);
        }
        for (int x = 1; x <= 3; ++x) {
            for (const std::string_view target :
                 {"self-player", "opponent-player"}) {
                const std::string descriptor =
                    "disintegrate-x" +
                    std::to_string(x) + "-" +
                    std::string(target);
                const std::size_t action =
                    require_descriptor_index(
                        evaluated.root, descriptor);
                const double parent_delta =
                    evaluated.parent.combined[action] -
                    evaluated.parent.combined[pass];
                const double candidate_delta =
                    evaluated.candidate.combined[action] -
                    evaluated.candidate.combined[pass];
                parent_contract =
                    parent_contract &&
                    parent_delta != 0.0;
                candidate_gate =
                    candidate_gate &&
                    same_sign(
                        parent_delta, candidate_delta) &&
                    support_contains(
                        evaluated.parent, descriptor) ==
                        support_contains(
                            evaluated.candidate,
                            descriptor);
            }
        }
        report.controls.push_back(finish_control(
            report, evaluated,
            std::string(kControlContracts[7].name),
            parent_contract, candidate_gate,
            enforce_parent_contract));
    }
}

void evaluate_positive_x_training_controls(
    const std::vector<TrainingInput>& inputs,
    const std::shared_ptr<const LearnedModel>& candidate,
    FitReport& report, bool enforce_parent_contract) {
    for (const TrainingInput& input : inputs) {
        const std::string& stable_id =
            input.root.canonical.probe.stable_id;
        const ControlContract* control_contract = nullptr;
        std::vector<std::string_view> productive_siblings;
        if (stable_id ==
            kControlContracts[8].stable_id) {
            control_contract = &kControlContracts[8];
            productive_siblings = {
                "braingeyser-x1-opponent",
                "braingeyser-x1-self",
            };
        } else if (
            stable_id ==
            kControlContracts[9].stable_id) {
            control_contract = &kControlContracts[9];
            productive_siblings = {
                "kind-7.card-17.x-1.target-player-0",
                "kind-7.card-17.x-1.target-player-1",
                "kind-7.card-17.x-2.target-player-0",
                "kind-7.card-17.x-2.target-player-1",
            };
        } else {
            continue;
        }
        const PolicyScores candidate_scores =
            evaluate_policy(input.root.canonical, candidate);
        const std::size_t pass = input.root.pass_index;
        bool parent_contract = true;
        bool candidate_gate =
            input.parent.exact_support ==
            candidate_scores.exact_support;
        for (const std::string_view descriptor :
             productive_siblings) {
            const std::size_t action =
                require_descriptor_index(
                    input.root.canonical, descriptor);
            const double parent_delta =
                input.parent.combined[action] -
                input.parent.combined[pass];
            const double candidate_delta =
                candidate_scores.combined[action] -
                candidate_scores.combined[pass];
            parent_contract =
                parent_contract && parent_delta != 0.0;
            candidate_gate =
                candidate_gate &&
                same_sign(parent_delta, candidate_delta) &&
                support_contains(
                    input.parent, descriptor) ==
                    support_contains(
                        candidate_scores, descriptor);
        }
        const EvaluatedControl evaluated{
            .root = input.root.canonical,
            .parent = input.parent,
            .candidate = candidate_scores,
        };
        report.controls.push_back(finish_control(
            report, evaluated,
            std::string(control_contract->name),
            parent_contract, candidate_gate,
            enforce_parent_contract));
    }
}

TrainingRootReport finish_training_root(
    const TrainingInput& input,
    const PolicyScores& candidate,
    FitReport& report) {
    const bool base_identical =
        scoring::bit_identical(
            input.parent.production,
            candidate.production);
    if (!base_identical) {
        throw std::logic_error(
            input.root.canonical.probe.stable_id +
            ": FQ4-D0 fit changed production base scores or "
            "accounting");
    }
    TrainingRootReport result{
        .stable_id =
            input.root.canonical.probe.stable_id,
        .information_action_fingerprint =
            probe_data::bsr_information_action_fingerprint(
                input.root.canonical.probe),
        .descriptors =
            input.root.canonical.descriptors,
        .pass_index = input.root.pass_index,
        .immutable_base_scores =
            input.example.base_scores,
        .parent_combined_scores =
            input.parent.combined,
        .candidate_combined_scores =
            candidate.combined,
        .parent_latent_probabilities =
            input.latent_parent,
        .projected_latent_probabilities =
            input.projection.probabilities,
        .behavior_target_probabilities =
            input.example.target_probabilities,
        .parent_exact_support =
            input.parent.exact_support,
        .candidate_exact_support =
            candidate.exact_support,
        .production_recipe_exact =
            production_recipe_exact(
                input.parent.production) &&
            production_recipe_exact(candidate.production),
        .production_base_and_accounting_bit_identical =
            base_identical,
    };
    for (const std::size_t dominated :
         input.root.dominated_indices) {
        const double parent_margin =
            input.parent.combined[input.root.pass_index] -
            input.parent.combined[dominated];
        const double candidate_margin =
            candidate.combined[input.root.pass_index] -
            candidate.combined[dominated];
        const bool active =
            std::find(
                input.projection
                    .active_dominated_indices.begin(),
                input.projection
                    .active_dominated_indices.end(),
                dominated) !=
            input.projection
                .active_dominated_indices.end();
        result.constraints.push_back({
            .descriptor =
                input.root.canonical.descriptors[dominated],
            .action_index = dominated,
            .strict_worlds =
                input.root.strict_world_counts[dominated],
            .active_projection_constraint = active,
            .parent_margin = parent_margin,
            .candidate_margin = candidate_margin,
        });
        ++report.discovered_constraints;
        if (parent_margin < kGateScoreMargin) {
            ++report.parent_margins_below_gate;
        }
        if (candidate_margin >= kGateScoreMargin) {
            ++report.candidate_margins_at_gate;
        } else {
            add_scientific_failure(
                report,
                result.stable_id + ":" +
                    result.constraints.back().descriptor +
                    " did not reach the +0.005 margin");
        }
        if (support_contains(
                candidate,
                input.root.canonical.descriptors[dominated])) {
            add_scientific_failure(
                report,
                result.stable_id + ":" +
                    result.constraints.back().descriptor +
                    " remains in exact-max support");
        }
    }
    return result;
}

bool policy_scores_bit_identical(
    const PolicyScores& first,
    const PolicyScores& second) {
    return scoring::bit_identical(
               first.production, second.production) &&
           bit_identical(first.logits, second.logits) &&
           bit_identical(first.combined, second.combined) &&
           first.exact_support == second.exact_support;
}

bool training_input_bit_identical(
    const TrainingInput& visible,
    const TrainingInput& hidden) {
    return
        visible.root.canonical.probe.stable_id ==
            hidden.root.canonical.probe.stable_id &&
        visible.root.canonical.descriptors ==
            hidden.root.canonical.descriptors &&
        visible.root.canonical.actions ==
            hidden.root.canonical.actions &&
        visible.root.pass_index == hidden.root.pass_index &&
        visible.root.dominated_indices ==
            hidden.root.dominated_indices &&
        visible.root.strict_world_counts ==
            hidden.root.strict_world_counts &&
        policy_scores_bit_identical(
            visible.parent, hidden.parent) &&
        visible.projection.active_dominated_indices ==
            hidden.projection.active_dominated_indices &&
        bit_identical(
            visible.projection.probabilities,
            hidden.projection.probabilities) &&
        bit_identical(
            visible.latent_parent,
            hidden.latent_parent) &&
        bit_identical(
            visible.example.options,
            hidden.example.options) &&
        bit_identical(
            visible.example.base_scores,
            hidden.example.base_scores) &&
        bit_identical(
            visible.example.target_probabilities,
            hidden.example.target_probabilities) &&
        same_double(
            visible.example.weight,
            hidden.example.weight);
}

std::vector<TrainingInput> make_hidden_inputs(
    const std::vector<TrainingInput>& visible_inputs,
    const std::shared_ptr<const LearnedModel>& parent) {
    if (visible_inputs.size() !=
        std::size(kRootContracts)) {
        throw std::logic_error(
            "FQ4-D0 hidden-clone input census drifted");
    }
    std::vector<TrainingInput> result;
    result.reserve(visible_inputs.size());
    for (std::size_t index = 0;
         index < visible_inputs.size(); ++index) {
        const CanonicalRoot& visible =
            visible_inputs[index].root.canonical;
        probe_data::DecisionProbe hidden_probe =
            visible.probe;
        hidden_probe.state =
            hidden_repartition(
                visible.probe.state,
                visible.probe.root_player);
        result.push_back(
            make_training_input(
                hidden_probe, kRootContracts[index],
                parent, &visible));
    }
    return result;
}

bool exact_hidden_invariance(
    const std::vector<TrainingInput>& visible_inputs,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    const std::vector<TrainingInput> hidden_inputs =
        make_hidden_inputs(visible_inputs, parent);
    for (std::size_t index = 0;
         index < visible_inputs.size(); ++index) {
        const TrainingInput& visible =
            visible_inputs[index];
        const TrainingInput& hidden =
            hidden_inputs[index];
        if (!training_input_bit_identical(
                visible, hidden)) {
            return false;
        }
        const PolicyScores candidate_visible =
            evaluate_policy(
                visible.root.canonical, candidate);
        const PolicyScores candidate_hidden =
            evaluate_policy(
                hidden.root.canonical, candidate,
                &visible.root.canonical);
        if (!policy_scores_bit_identical(
                candidate_visible,
                candidate_hidden)) {
            return false;
        }
    }

    const auto hidden_candidate =
        update_learned_value_priority_head(
            parent, training_examples(hidden_inputs),
            optimizer);
    return
        learned_model_fingerprint(hidden_candidate) ==
            learned_model_fingerprint(candidate) &&
        learned_model_component_fingerprints(
            hidden_candidate) ==
            learned_model_component_fingerprints(
                candidate);
}

std::vector<TrainingInput> make_inputs(
    const std::vector<probe_data::DecisionProbe>& corpus,
    const std::shared_ptr<const LearnedModel>& parent,
    bool reverse_candidate_inputs) {
    std::vector<TrainingInput> result;
    result.reserve(std::size(kRootContracts));
    for (const RootContract& contract : kRootContracts) {
        probe_data::DecisionProbe probe =
            require_probe(corpus, contract.stable_id);
        if (reverse_candidate_inputs) {
            std::reverse(
                probe.candidates.begin(),
                probe.candidates.end());
        }
        result.push_back(
            make_training_input(
                probe, contract, parent));
    }
    return result;
}

FitReport fit_impl(
    std::shared_ptr<const LearnedModel> parent,
    bool require_exact_fingerprint,
    bool enforce_parent_behavior_contract,
    const LearnedValuePriorityHeadUpdateConfig& optimizer =
        optimizer_config(),
    std::size_t expected_parent_margins_below_gate =
        kExpectedParentMarginsBelowGate) {
    if (!parent) {
        throw std::invalid_argument(
            "FQ4-D0 requires a frozen Value parent");
    }
    const std::string original_parent_fingerprint =
        learned_model_fingerprint(parent);
    if (require_exact_fingerprint &&
        original_parent_fingerprint !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "FQ4-D0 requires the exact frozen C16 model");
    }
    const auto corpus = all_fixture_probes();
    const std::vector<TrainingInput> inputs =
        make_inputs(corpus, parent, false);
    const auto examples = training_examples(inputs);
    const auto candidate =
        update_learned_value_priority_head(
            parent, examples, optimizer);
    const auto repeat =
        update_learned_value_priority_head(
            parent, examples, optimizer);

    FitReport report{
        .candidate = candidate,
        .parent_fingerprint =
            original_parent_fingerprint,
        .candidate_fingerprint =
            learned_model_fingerprint(candidate),
        .training_input_sha256 =
            training_input_sha256(inputs),
        .parent_components =
            learned_model_component_fingerprints(parent),
        .candidate_components =
            learned_model_component_fingerprints(candidate),
        .all_production_base_and_accounting_bit_identical =
            true,
    };
    report.parent_immutable =
        learned_model_fingerprint(parent) ==
        original_parent_fingerprint;
    if (!report.parent_immutable) {
        throw std::logic_error(
            "FQ4-D0 mutated its immutable parent");
    }
    const bool nonpriority_identical =
        report.parent_components.critic ==
            report.candidate_components.critic &&
        report.parent_components.attack ==
            report.candidate_components.attack &&
        report.parent_components.block ==
            report.candidate_components.block &&
        report.parent_components.damage_order ==
            report.candidate_components.damage_order;
    if (!nonpriority_identical) {
        throw std::logic_error(
            "FQ4-D0 changed a frozen non-Priority component");
    }
    report.only_priority_component_changed =
        report.parent_components.priority !=
        report.candidate_components.priority;
    if (!report.only_priority_component_changed) {
        add_scientific_failure(
            report, "Priority component did not change");
    }
    report.repeated_fit_bit_identical =
        learned_model_fingerprint(repeat) ==
            report.candidate_fingerprint &&
        learned_model_component_fingerprints(repeat) ==
            report.candidate_components;
    if (!report.repeated_fit_bit_identical) {
        throw std::logic_error(
            "FQ4-D0 repeated fit was not bit-identical");
    }

    for (const TrainingInput& input : inputs) {
        const PolicyScores candidate_scores =
            evaluate_policy(
                input.root.canonical, candidate);
        report.roots.push_back(
            finish_training_root(
                input, candidate_scores, report));
    }
    if (report.discovered_constraints !=
        kExpectedDominanceConstraints) {
        throw std::logic_error(
            "FQ4-D0 dominance constraint census drifted");
    }
    if (enforce_parent_behavior_contract) {
        record_parent_behavior_contract(
            report,
            report.parent_margins_below_gate ==
                expected_parent_margins_below_gate,
            "registered parent margin census below +0.005");
    }

    evaluate_named_controls(
        corpus, parent, candidate, report,
        enforce_parent_behavior_contract);
    evaluate_positive_x_training_controls(
        inputs, candidate, report,
        enforce_parent_behavior_contract);
    report.every_control_passed =
        std::all_of(
            report.controls.begin(),
            report.controls.end(),
            [](const ControlReport& control) {
                return control.passed;
            });
    report.hidden_repartition_bit_identical =
        exact_hidden_invariance(
            inputs, parent, candidate, optimizer);
    if (!report.hidden_repartition_bit_identical) {
        throw std::logic_error(
            "FQ4-D0 hidden-repartition invariance failed");
    }

    const auto reversed_inputs =
        make_inputs(corpus, parent, true);
    const auto reversed_candidate =
        update_learned_value_priority_head(
            parent, training_examples(reversed_inputs),
            optimizer);
    report.action_order_bit_identical =
        learned_model_fingerprint(reversed_candidate) ==
            report.candidate_fingerprint &&
        learned_model_component_fingerprints(
            reversed_candidate) ==
            report.candidate_components;
    if (!report.action_order_bit_identical) {
        throw std::logic_error(
            "FQ4-D0 legal-action input reversal changed the fit");
    }
    if (!report.all_production_base_and_accounting_bit_identical) {
        throw std::logic_error(
            "FQ4-D0 production base/accounting identity failed");
    }
    return report;
}

bool finite_vector(const std::vector<double>& values) {
    return std::all_of(
        values.begin(), values.end(),
        [](double value) {
            return std::isfinite(value);
        });
}

bool exact_support_is_valid(
    const std::vector<std::string>& support,
    const std::vector<std::string>& descriptors) {
    return !support.empty() &&
           std::is_sorted(
               support.begin(), support.end()) &&
           std::adjacent_find(
               support.begin(), support.end()) ==
               support.end() &&
           std::all_of(
               support.begin(), support.end(),
               [&](const std::string& descriptor) {
                   return std::find(
                              descriptors.begin(),
                              descriptors.end(),
                              descriptor) !=
                          descriptors.end();
               });
}

bool root_report_matches_contract(
    const TrainingRootReport& root,
    const RootContract& contract) {
    std::vector<std::string> expected_descriptors;
    expected_descriptors.reserve(
        contract.descriptors.size());
    for (const std::string_view descriptor :
         contract.descriptors) {
        expected_descriptors.emplace_back(descriptor);
    }
    if (root.stable_id != contract.stable_id ||
        root.information_action_fingerprint !=
            contract.information_action_fingerprint ||
        root.descriptors != expected_descriptors ||
        root.pass_index >= root.descriptors.size() ||
        root.descriptors[root.pass_index] !=
            contract.pass_descriptor ||
        root.immutable_base_scores.size() !=
            root.descriptors.size() ||
        root.parent_combined_scores.size() !=
            root.descriptors.size() ||
        root.candidate_combined_scores.size() !=
            root.descriptors.size() ||
        root.parent_latent_probabilities.size() !=
            root.descriptors.size() ||
        root.projected_latent_probabilities.size() !=
            root.descriptors.size() ||
        root.behavior_target_probabilities.size() !=
            root.descriptors.size() ||
        !finite_vector(root.immutable_base_scores) ||
        !finite_vector(root.parent_combined_scores) ||
        !finite_vector(root.candidate_combined_scores) ||
        !finite_vector(root.parent_latent_probabilities) ||
        !finite_vector(root.projected_latent_probabilities) ||
        !finite_vector(root.behavior_target_probabilities) ||
        !exact_support_is_valid(
            root.parent_exact_support,
            root.descriptors) ||
        !exact_support_is_valid(
            root.candidate_exact_support,
            root.descriptors) ||
        !root.production_recipe_exact ||
        !root.production_base_and_accounting_bit_identical ||
        root.constraints.size() !=
            contract.dominated_descriptors.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < root.constraints.size(); ++index) {
        const DominanceConstraintReport& row =
            root.constraints[index];
        if (row.descriptor !=
                contract.dominated_descriptors[index] ||
            row.action_index >= root.descriptors.size() ||
            root.descriptors[row.action_index] !=
                row.descriptor ||
            row.strict_worlds != kDominanceWorlds ||
            !std::isfinite(row.parent_margin) ||
            !std::isfinite(row.candidate_margin) ||
            !same_double(
                row.parent_margin,
                root.parent_combined_scores[
                    root.pass_index] -
                    root.parent_combined_scores[
                        row.action_index]) ||
            !same_double(
                row.candidate_margin,
                root.candidate_combined_scores[
                    root.pass_index] -
                    root.candidate_combined_scores[
                        row.action_index])) {
            return false;
        }
    }
    return true;
}

std::size_t derived_constraint_count(
    const FitReport& report) {
    return std::accumulate(
        report.roots.begin(), report.roots.end(),
        std::size_t{0},
        [](std::size_t total,
           const TrainingRootReport& root) {
            return total + root.constraints.size();
        });
}

std::size_t derived_parent_margins_below_gate(
    const FitReport& report) {
    std::size_t result = 0;
    for (const TrainingRootReport& root :
         report.roots) {
        result += static_cast<std::size_t>(
            std::count_if(
                root.constraints.begin(),
                root.constraints.end(),
                [](const DominanceConstraintReport& row) {
                    return row.parent_margin <
                           kGateScoreMargin;
                }));
    }
    return result;
}

std::size_t derived_candidate_margins_at_gate(
    const FitReport& report) {
    std::size_t result = 0;
    for (const TrainingRootReport& root :
         report.roots) {
        result += static_cast<std::size_t>(
            std::count_if(
                root.constraints.begin(),
                root.constraints.end(),
                [](const DominanceConstraintReport& row) {
                    return row.candidate_margin >=
                           kGateScoreMargin;
                }));
    }
    return result;
}

bool exact_root_report_census(const FitReport& report) {
    if (report.roots.size() !=
        std::size(kRootContracts)) {
        return false;
    }
    for (std::size_t index = 0;
         index < report.roots.size(); ++index) {
        if (!root_report_matches_contract(
                report.roots[index],
                kRootContracts[index])) {
            return false;
        }
    }
    return
        report.discovered_constraints ==
            derived_constraint_count(report) &&
        report.discovered_constraints ==
            kExpectedDominanceConstraints &&
        report.parent_margins_below_gate ==
            derived_parent_margins_below_gate(report) &&
        report.candidate_margins_at_gate ==
            derived_candidate_margins_at_gate(report);
}

bool all_controls_passed(const FitReport& report) {
    return std::all_of(
        report.controls.begin(), report.controls.end(),
        [](const ControlReport& control) {
            return control.passed;
        });
}

bool all_candidate_constraint_rows_pass(
    const FitReport& report) {
    for (const TrainingRootReport& root :
         report.roots) {
        for (const DominanceConstraintReport& row :
             root.constraints) {
            if (row.candidate_margin <
                    kGateScoreMargin ||
                std::find(
                    root.candidate_exact_support.begin(),
                    root.candidate_exact_support.end(),
                    row.descriptor) !=
                    root.candidate_exact_support.end()) {
                return false;
            }
        }
    }
    return true;
}

std::string_view expected_control_report_fingerprint(
    std::size_t index) {
    switch (index) {
    case 0:
        return "b792d7434096d2cc";
    case 1:
        return "f21baf227fe0161f";
    case 2:
        return "6c90355960714c47";
    case 3:
        return "30ff11b9ec056b21";
    case 4:
        return "fc276ae226a9f512";
    case 5:
        return "8e24d4696a7c2ad5";
    case 6:
        return "faf53e39aba9e69b";
    case 7:
        return "6345aec096735eb9";
    case 8:
        return "a68cd5b38da84990";
    case 9:
        return "04d02e0ea36d34be";
    default:
        throw std::logic_error(
            "FQ4-D0 control report index is out of range");
    }
}

std::vector<std::string> expected_control_report_descriptors(
    std::size_t index) {
    switch (index) {
    case 0:
    case 5:
        return {
            "force-spike-gray-ogre",
            "pass",
        };
    case 1:
        return {
            "growth-own-grizzly-bears",
            "pass",
        };
    case 2:
        return {
            "counter-fire-elemental",
            "pass",
        };
    case 3:
        return {
            "counter-lethal-lightning-bolt",
            "pass",
        };
    case 4:
        return {
            "counter-opponent-counterspell",
            "counter-own-air-elemental",
            "pass",
        };
    case 6:
        return {
            "counter-own-counterspell",
            "counter-same-air-elemental",
            "pass",
        };
    case 7:
        return {
            "disintegrate-x0-opponent-player",
            "disintegrate-x0-self-player",
            "disintegrate-x1-opponent-player",
            "disintegrate-x1-self-player",
            "disintegrate-x2-opponent-player",
            "disintegrate-x2-self-player",
            "disintegrate-x3-opponent-player",
            "disintegrate-x3-self-player",
            "pass",
        };
    case 8:
        return std::vector<std::string>(
            std::begin(kBlueDescriptors),
            std::end(kBlueDescriptors));
    case 9:
        return std::vector<std::string>(
            std::begin(kRuDescriptors),
            std::end(kRuDescriptors));
    default:
        throw std::logic_error(
            "FQ4-D0 control report index is out of range");
    }
}

bool exact_control_report_census(
    const FitReport& report) {
    if (report.controls.size() !=
        std::size(kControlContracts)) {
        return false;
    }
    for (std::size_t index = 0;
         index < report.controls.size(); ++index) {
        if (report.controls[index].name !=
                kControlContracts[index].name ||
            report.controls[index].stable_id !=
                kControlContracts[index].stable_id ||
            report.controls[index]
                    .information_action_fingerprint !=
                expected_control_report_fingerprint(index) ||
            report.controls[index].descriptors !=
                expected_control_report_descriptors(index) ||
            !exact_support_is_valid(
                report.controls[index].parent_exact_support,
                report.controls[index].descriptors) ||
            !exact_support_is_valid(
                report.controls[index]
                    .candidate_exact_support,
                report.controls[index].descriptors)) {
            return false;
        }
    }
    return report.every_control_passed ==
           all_controls_passed(report);
}

std::vector<double> flattened_parent_margins(
    const FitReport& fit) {
    std::vector<double> result;
    result.reserve(kExpectedDominanceConstraints);
    for (const TrainingRootReport& root : fit.roots) {
        for (const DominanceConstraintReport& constraint :
             root.constraints) {
            result.push_back(constraint.parent_margin);
        }
    }
    return result;
}

std::vector<double> flattened_candidate_margins(
    const FitReport& fit) {
    std::vector<double> result;
    result.reserve(kExpectedDominanceConstraints);
    for (const TrainingRootReport& root : fit.roots) {
        for (const DominanceConstraintReport& constraint :
             root.constraints) {
            result.push_back(constraint.candidate_margin);
        }
    }
    return result;
}

template <std::size_t Size>
bool exact_margin_bits_match(
    const std::vector<double>& actual,
    const std::array<std::uint64_t, Size>& expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < actual.size();
         ++index) {
        if (std::bit_cast<std::uint64_t>(actual[index]) !=
            expected[index]) {
            return false;
        }
    }
    return true;
}

bool optimizer_is_exact(
    const LearnedValuePriorityHeadUpdateConfig& config,
    std::size_t epochs) {
    return config == optimizer_config(epochs);
}

bool optimizer_only_epochs_differ(
    const LearnedValuePriorityHeadUpdateConfig& anchor,
    const LearnedValuePriorityHeadUpdateConfig& treatment) {
    LearnedValuePriorityHeadUpdateConfig normalized =
        treatment;
    normalized.epochs = anchor.epochs;
    return anchor.epochs == kD0bAnchorEpochs &&
           treatment.epochs == kD0bTreatmentEpochs &&
           normalized == anchor;
}

bool probability_distribution(
    const std::vector<double>& probabilities) {
    if (probabilities.empty()) {
        return false;
    }
    long double total = 0.0L;
    for (const double probability : probabilities) {
        if (!std::isfinite(probability) ||
            probability <= 0.0) {
            return false;
        }
        total += static_cast<long double>(probability);
    }
    return std::abs(total - 1.0L) <= 1.0e-12L;
}

double forward_kl(
    const std::vector<double>& target,
    const std::vector<double>& candidate) {
    if (target.size() != candidate.size() ||
        !probability_distribution(target) ||
        !probability_distribution(candidate)) {
        throw std::logic_error(
            "FQ4-D0b KL requires complete positive distributions");
    }
    long double result = 0.0L;
    for (std::size_t index = 0; index < target.size();
         ++index) {
        result +=
            static_cast<long double>(target[index]) *
            std::log(
                static_cast<long double>(target[index]) /
                static_cast<long double>(candidate[index]));
    }
    const double value = static_cast<double>(result);
    if (!std::isfinite(value) || value < -1.0e-15) {
        throw std::logic_error(
            "FQ4-D0b KL result is invalid");
    }
    return value < 0.0 ? 0.0 : value;
}

D0bCheckpointReport make_d0b_checkpoint(
    FitReport fit,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    D0bCheckpointReport result{
        .epochs = optimizer.epochs,
        .optimizer = optimizer,
        .fit = std::move(fit),
    };
    result.root_kl.reserve(result.fit.roots.size());
    long double pooled = 0.0L;
    for (const TrainingRootReport& root :
         result.fit.roots) {
        const std::vector<double> candidate_behavior =
            behavior_target(
                softmax_scores(
                    root.candidate_combined_scores));
        const double kl = forward_kl(
            root.behavior_target_probabilities,
            candidate_behavior);
        result.root_kl.push_back({
            .stable_id = root.stable_id,
            .candidate_behavior_probabilities =
                candidate_behavior,
            .target_to_candidate_kl = kl,
        });
        pooled += static_cast<long double>(kl);
    }
    if (result.root_kl.empty()) {
        throw std::logic_error(
            "FQ4-D0b checkpoint has no KL roots");
    }
    result.pooled_target_to_candidate_kl =
        static_cast<double>(
            pooled /
            static_cast<long double>(
                result.root_kl.size()));
    return result;
}

bool d0b_root_input_bit_identical(
    const TrainingRootReport& anchor,
    const TrainingRootReport& treatment) {
    if (anchor.stable_id != treatment.stable_id ||
        anchor.information_action_fingerprint !=
            treatment.information_action_fingerprint ||
        anchor.descriptors != treatment.descriptors ||
        anchor.pass_index != treatment.pass_index ||
        !bit_identical(
            anchor.immutable_base_scores,
            treatment.immutable_base_scores) ||
        !bit_identical(
            anchor.parent_combined_scores,
            treatment.parent_combined_scores) ||
        !bit_identical(
            anchor.parent_latent_probabilities,
            treatment.parent_latent_probabilities) ||
        !bit_identical(
            anchor.projected_latent_probabilities,
            treatment.projected_latent_probabilities) ||
        !bit_identical(
            anchor.behavior_target_probabilities,
            treatment.behavior_target_probabilities) ||
        anchor.parent_exact_support !=
            treatment.parent_exact_support ||
        anchor.production_recipe_exact !=
            treatment.production_recipe_exact ||
        anchor.constraints.size() !=
            treatment.constraints.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < anchor.constraints.size(); ++index) {
        const DominanceConstraintReport& first =
            anchor.constraints[index];
        const DominanceConstraintReport& second =
            treatment.constraints[index];
        if (first.descriptor != second.descriptor ||
            first.action_index != second.action_index ||
            first.strict_worlds != second.strict_worlds ||
            first.active_projection_constraint !=
                second.active_projection_constraint ||
            !same_double(
                first.parent_margin,
                second.parent_margin)) {
            return false;
        }
    }
    return true;
}

bool d0b_checkpoint_inputs_bit_identical(
    const FitReport& anchor,
    const FitReport& treatment) {
    if (anchor.parent_fingerprint !=
            treatment.parent_fingerprint ||
        anchor.training_input_sha256.size() != 64 ||
        anchor.training_input_sha256 !=
            treatment.training_input_sha256 ||
        anchor.parent_components !=
            treatment.parent_components ||
        anchor.roots.size() != treatment.roots.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < anchor.roots.size(); ++index) {
        if (!d0b_root_input_bit_identical(
                anchor.roots[index],
                treatment.roots[index])) {
            return false;
        }
    }
    return true;
}

bool fit_model_identity_valid(const FitReport& fit) {
    return fit.candidate != nullptr &&
           learned_model_fingerprint(fit.candidate) ==
               fit.candidate_fingerprint &&
           learned_model_component_fingerprints(
               fit.candidate) ==
               fit.candidate_components;
}

bool d0b_checkpoint_kl_valid(
    const D0bCheckpointReport& checkpoint) {
    if (checkpoint.root_kl.size() !=
            checkpoint.fit.roots.size() ||
        checkpoint.root_kl.empty()) {
        return false;
    }
    long double pooled = 0.0L;
    for (std::size_t index = 0;
         index < checkpoint.root_kl.size(); ++index) {
        const TrainingRootReport& root =
            checkpoint.fit.roots[index];
        const D0bRootKlReport& row =
            checkpoint.root_kl[index];
        if (row.stable_id != root.stable_id ||
            row.candidate_behavior_probabilities.size() !=
                root.descriptors.size() ||
            !probability_distribution(
                row.candidate_behavior_probabilities)) {
            return false;
        }
        const std::vector<double> reconstructed =
            behavior_target(
                softmax_scores(
                    root.candidate_combined_scores));
        if (!bit_identical(
                reconstructed,
                row.candidate_behavior_probabilities)) {
            return false;
        }
        const double reconstructed_kl = forward_kl(
            root.behavior_target_probabilities,
            reconstructed);
        if (!same_double(
                reconstructed_kl,
                row.target_to_candidate_kl)) {
            return false;
        }
        pooled +=
            static_cast<long double>(
                row.target_to_candidate_kl);
    }
    const double reconstructed_pooled =
        static_cast<double>(
            pooled /
            static_cast<long double>(
                checkpoint.root_kl.size()));
    return same_double(
        reconstructed_pooled,
        checkpoint.pooled_target_to_candidate_kl);
}

bool d0b_treatment_scientifically_passes(
    const D0bReport& report) {
    return report.scientific_failures.empty() &&
           report.treatment.fit.scientific_failures.empty() &&
           report.treatment.fit.candidate_margins_at_gate ==
               kExpectedDominanceConstraints &&
           all_candidate_constraint_rows_pass(
               report.treatment.fit) &&
           report.treatment.fit
               .only_priority_component_changed &&
           report.treatment.fit.every_control_passed &&
           all_controls_passed(report.treatment.fit) &&
           report.target_kl_strictly_improved &&
           report.treatment
                   .pooled_target_to_candidate_kl <
               report.anchor
                   .pooled_target_to_candidate_kl;
}

D0bReport fit_d0b_impl(
    std::shared_ptr<const LearnedModel> parent,
    bool require_exact_contracts) {
    if (!parent) {
        throw std::invalid_argument(
            "FQ4-D0b requires a frozen Value parent");
    }
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    if (require_exact_contracts &&
        parent_fingerprint != kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "FQ4-D0b requires the exact frozen C16 model");
    }

    const auto anchor_optimizer =
        optimizer_config(kD0bAnchorEpochs);
    const auto treatment_optimizer =
        optimizer_config(kD0bTreatmentEpochs);
    D0bReport report{
        .anchor =
            make_d0b_checkpoint(
                fit_impl(
                    parent, false,
                    require_exact_contracts,
                    anchor_optimizer,
                    kD0bExpectedParentMarginsBelowGate),
                anchor_optimizer),
        .treatment =
            make_d0b_checkpoint(
                fit_impl(
                    parent, false,
                    require_exact_contracts,
                    treatment_optimizer,
                    kD0bExpectedParentMarginsBelowGate),
                treatment_optimizer),
        .exact_contracts_required =
            require_exact_contracts,
    };
    report.parent_margins =
        flattened_parent_margins(report.anchor.fit);
    report.parent_margins_below_gate =
        static_cast<std::size_t>(std::count_if(
            report.parent_margins.begin(),
            report.parent_margins.end(),
            [](double margin) {
                return margin < kGateScoreMargin;
            }));
    report.parent_contract_qualified =
        !require_exact_contracts ||
        (report.anchor.fit.parent_fingerprint ==
             kRequiredParentFingerprint &&
         report.parent_margins_below_gate ==
             kD0bExpectedParentMarginsBelowGate &&
         exact_margin_bits_match(
             report.parent_margins,
             kD0bRequiredParentMarginBits));
    report.anchor_contract_qualified =
        !require_exact_contracts ||
        (report.anchor.fit.candidate_fingerprint ==
             kD0bRequiredAnchorFingerprint &&
         exact_margin_bits_match(
             flattened_candidate_margins(
                 report.anchor.fit),
             kD0bRequiredAnchorMarginBits));
    report.optimizer_only_epochs_differ =
        optimizer_only_epochs_differ(
            report.anchor.optimizer,
            report.treatment.optimizer);
    report.checkpoint_inputs_bit_identical =
        d0b_checkpoint_inputs_bit_identical(
            report.anchor.fit,
            report.treatment.fit);
    report.target_kl_strictly_improved =
        report.treatment.pooled_target_to_candidate_kl <
        report.anchor.pooled_target_to_candidate_kl;

    const auto add_infrastructure =
        [&](bool condition, std::string message) {
            if (!condition) {
                report.infrastructure_failures.push_back(
                    std::move(message));
            }
        };
    add_infrastructure(
        report.parent_contract_qualified,
        "FQ4-D0b frozen parent contract drifted");
    add_infrastructure(
        report.anchor_contract_qualified,
        "FQ4-D0b 256-epoch anchor contract drifted");
    add_infrastructure(
        report.optimizer_only_epochs_differ,
        "FQ4-D0b optimizer fields differ beyond epochs");
    add_infrastructure(
        report.checkpoint_inputs_bit_identical,
        "FQ4-D0b anchor/treatment inputs drifted");

    report.scientific_failures =
        report.treatment.fit.scientific_failures;
    if (!report.target_kl_strictly_improved) {
        report.scientific_failures.push_back(
            "FQ4-D0b pooled target KL did not improve");
    }
    return report;
}

} // namespace

ReverseKlProjection reverse_kl_i_projection(
    const std::vector<double>& parent_probabilities,
    const std::vector<StarConstraint>& constraints,
    double ratio) {
    if (parent_probabilities.empty() ||
        parent_probabilities.size() > 63 ||
        !std::isfinite(ratio) || ratio <= 1.0) {
        throw std::invalid_argument(
            "reverse-KL projection dimensions or ratio are invalid");
    }
    long double total = 0.0L;
    for (const double probability :
         parent_probabilities) {
        if (!std::isfinite(probability) ||
            probability <= 0.0) {
            throw std::invalid_argument(
                "reverse-KL projection requires strictly "
                "positive finite probabilities");
        }
        total +=
            static_cast<long double>(probability);
    }
    if (std::abs(total - 1.0L) > 1.0e-12L) {
        throw std::invalid_argument(
            "reverse-KL projection probabilities must sum to one");
    }
    if (constraints.empty()) {
        return {
            .probabilities = parent_probabilities,
        };
    }
    const std::size_t pass = constraints.front().pass_index;
    if (pass >= parent_probabilities.size()) {
        throw std::invalid_argument(
            "reverse-KL projection pass index is out of range");
    }
    std::vector<std::size_t> dominated;
    dominated.reserve(constraints.size());
    for (const StarConstraint& constraint : constraints) {
        if (constraint.pass_index != pass ||
            constraint.dominated_index >=
                parent_probabilities.size() ||
            constraint.dominated_index == pass ||
            std::find(
                dominated.begin(), dominated.end(),
                constraint.dominated_index) !=
                dominated.end()) {
            throw std::invalid_argument(
                "reverse-KL projection constraints are malformed");
        }
        dominated.push_back(
            constraint.dominated_index);
    }
    if (dominated.size() >=
        std::numeric_limits<std::uint64_t>::digits) {
        throw std::invalid_argument(
            "reverse-KL projection has too many constraints");
    }
    const bool already_feasible =
        std::all_of(
            dominated.begin(), dominated.end(),
            [&](std::size_t index) {
                return parent_probabilities[pass] >=
                       ratio *
                           parent_probabilities[index];
            });
    if (already_feasible) {
        return {
            .probabilities = parent_probabilities,
        };
    }

    constexpr long double kKktTolerance = 2.0e-14L;
    std::optional<ReverseKlProjection> answer;
    long double answer_objective =
        std::numeric_limits<long double>::infinity();
    const std::uint64_t subset_count =
        std::uint64_t{1} << dominated.size();
    for (std::uint64_t mask = 1;
         mask < subset_count; ++mask) {
        std::vector<std::size_t> active;
        long double log_geometric =
            std::log(
                static_cast<long double>(
                    parent_probabilities[pass]));
        long double inactive_mass = 0.0L;
        for (std::size_t index = 0;
             index < parent_probabilities.size(); ++index) {
            if (index == pass) {
                continue;
            }
            const auto constrained = std::find(
                dominated.begin(), dominated.end(), index);
            const bool is_active =
                constrained != dominated.end() &&
                (mask &
                 (std::uint64_t{1}
                  << static_cast<std::size_t>(
                         constrained -
                         dominated.begin()))) != 0;
            if (is_active) {
                active.push_back(index);
                log_geometric +=
                    std::log(
                        static_cast<long double>(ratio) *
                        static_cast<long double>(
                            parent_probabilities[index])) /
                    static_cast<long double>(ratio);
            } else {
                inactive_mass +=
                    static_cast<long double>(
                        parent_probabilities[index]);
            }
        }
        const long double group_coefficient =
            1.0L +
            static_cast<long double>(active.size()) /
                static_cast<long double>(ratio);
        const long double geometric =
            std::exp(
                log_geometric / group_coefficient);
        const long double scale =
            1.0L /
            (inactive_mass +
             group_coefficient * geometric);
        const long double pass_probability =
            scale * geometric;

        bool kkt = true;
        for (const std::size_t index : dominated) {
            const bool is_active =
                std::find(
                    active.begin(), active.end(),
                    index) != active.end();
            const long double boundary =
                static_cast<long double>(ratio) *
                static_cast<long double>(
                    parent_probabilities[index]);
            if (is_active) {
                kkt =
                    kkt &&
                    geometric <=
                        boundary + kKktTolerance;
            } else {
                kkt =
                    kkt &&
                    geometric + kKktTolerance >=
                        boundary;
            }
        }
        if (!kkt) {
            continue;
        }

        std::vector<double> q(
            parent_probabilities.size());
        for (std::size_t index = 0;
             index < q.size(); ++index) {
            if (index == pass) {
                q[index] =
                    static_cast<double>(
                        pass_probability);
            } else if (
                std::find(
                    active.begin(), active.end(),
                    index) != active.end()) {
                q[index] =
                    static_cast<double>(
                        pass_probability /
                        static_cast<long double>(ratio));
            } else {
                q[index] =
                    static_cast<double>(
                        scale *
                        static_cast<long double>(
                            parent_probabilities[index]));
            }
        }
        long double objective = 0.0L;
        long double q_total = 0.0L;
        for (std::size_t index = 0;
             index < q.size(); ++index) {
            q_total += static_cast<long double>(q[index]);
            objective +=
                static_cast<long double>(q[index]) *
                std::log(
                    static_cast<long double>(q[index]) /
                    static_cast<long double>(
                        parent_probabilities[index]));
        }
        bool feasible =
            std::abs(q_total - 1.0L) <= 1.0e-12L;
        for (const std::size_t index : dominated) {
            feasible =
                feasible &&
                static_cast<long double>(q[pass]) +
                        1.0e-14L >=
                    static_cast<long double>(ratio) *
                        static_cast<long double>(q[index]);
        }
        if (!feasible) {
            continue;
        }
        if (!answer.has_value() ||
            objective < answer_objective) {
            answer = ReverseKlProjection{
                .probabilities = std::move(q),
                .active_dominated_indices =
                    std::move(active),
            };
            answer_objective = objective;
        }
    }
    if (!answer.has_value()) {
        throw std::logic_error(
            "reverse-KL joint active-set projection found no KKT solution");
    }
    return *answer;
}

bool FitReport::infrastructure_valid() const {
    return infrastructure_failures.empty() &&
           candidate != nullptr &&
           exact_root_report_census(*this) &&
           exact_control_report_census(*this) &&
           parent_immutable &&
           repeated_fit_bit_identical &&
           hidden_repartition_bit_identical &&
           action_order_bit_identical &&
           all_production_base_and_accounting_bit_identical;
}

bool FitReport::passed() const {
    return infrastructure_valid() &&
           scientific_failures.empty() &&
           discovered_constraints ==
               kExpectedDominanceConstraints &&
           parent_margins_below_gate ==
               kExpectedParentMarginsBelowGate &&
           candidate_margins_at_gate ==
               kExpectedDominanceConstraints &&
           all_candidate_constraint_rows_pass(*this) &&
           only_priority_component_changed &&
           every_control_passed &&
           all_controls_passed(*this);
}

ExitClassification classify_exit(const FitReport& report) {
    if (!report.infrastructure_valid()) {
        return ExitClassification::InfrastructureFailure;
    }
    return report.passed()
               ? ExitClassification::Pass
               : ExitClassification::ScientificReject;
}

bool D0bReport::infrastructure_valid() const {
    const std::vector<double> anchor_parent_margins =
        flattened_parent_margins(anchor.fit);
    const std::vector<double> treatment_parent_margins =
        flattened_parent_margins(treatment.fit);
    const std::size_t derived_parent_below =
        static_cast<std::size_t>(std::count_if(
            parent_margins.begin(),
            parent_margins.end(),
            [](double margin) {
                return margin < kGateScoreMargin;
            }));
    const bool production_parent_census =
        !exact_contracts_required ||
        parent_margins_below_gate ==
            kD0bExpectedParentMarginsBelowGate;
    const bool production_artifact_contract =
        !exact_contracts_required ||
        (anchor.fit.parent_fingerprint ==
             kRequiredParentFingerprint &&
         treatment.fit.parent_fingerprint ==
             kRequiredParentFingerprint &&
         anchor.fit.candidate_fingerprint ==
             kD0bRequiredAnchorFingerprint);
    const bool production_margin_contract =
        !exact_contracts_required ||
        (exact_margin_bits_match(
             parent_margins,
             kD0bRequiredParentMarginBits) &&
         exact_margin_bits_match(
             flattened_candidate_margins(anchor.fit),
             kD0bRequiredAnchorMarginBits));
    return infrastructure_failures.empty() &&
           parent_contract_qualified &&
           anchor_contract_qualified &&
           anchor.epochs == kD0bAnchorEpochs &&
           treatment.epochs == kD0bTreatmentEpochs &&
           optimizer_is_exact(
               anchor.optimizer, kD0bAnchorEpochs) &&
           optimizer_is_exact(
               treatment.optimizer,
               kD0bTreatmentEpochs) &&
           optimizer_only_epochs_differ &&
           fq4_priority_fit::
               optimizer_only_epochs_differ(
                   anchor.optimizer,
                   treatment.optimizer) &&
           anchor.fit.infrastructure_valid() &&
           treatment.fit.infrastructure_valid() &&
           fit_model_identity_valid(anchor.fit) &&
           fit_model_identity_valid(treatment.fit) &&
           anchor.fit.parent_fingerprint ==
               treatment.fit.parent_fingerprint &&
           anchor.fit.parent_components ==
               treatment.fit.parent_components &&
           anchor.fit.parent_immutable &&
           treatment.fit.parent_immutable &&
           anchor.fit.only_priority_component_changed &&
           treatment.fit.only_priority_component_changed &&
           checkpoint_inputs_bit_identical &&
           d0b_checkpoint_inputs_bit_identical(
               anchor.fit, treatment.fit) &&
           bit_identical(
               parent_margins,
               anchor_parent_margins) &&
           bit_identical(
               anchor_parent_margins,
               treatment_parent_margins) &&
           parent_margins.size() ==
               kExpectedDominanceConstraints &&
           parent_margins_below_gate ==
               derived_parent_below &&
           production_parent_census &&
           production_artifact_contract &&
           production_margin_contract &&
           d0b_checkpoint_kl_valid(anchor) &&
           d0b_checkpoint_kl_valid(treatment);
}

bool D0bReport::passed() const {
    return infrastructure_valid() &&
           d0b_treatment_scientifically_passes(*this);
}

ExitClassification classify_d0b_exit(
    const D0bReport& report) {
    if (!report.infrastructure_valid()) {
        return ExitClassification::InfrastructureFailure;
    }
    return report.passed()
               ? ExitClassification::Pass
               : ExitClassification::ScientificReject;
}

FitReport fit_production(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    return fit_impl(
        std::move(frozen_c16), true, true);
}

D0bReport fit_d0b_production(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    return fit_d0b_impl(
        std::move(frozen_c16), true);
}

namespace testing {

FitReport fit(
    std::shared_ptr<const LearnedModel> value_model) {
    return fit_impl(
        std::move(value_model), false, false);
}

FitReport fit_enforcing_parent_behavior_contract(
    std::shared_ptr<const LearnedModel> value_model) {
    return fit_impl(
        std::move(value_model), false, true);
}

D0bReport fit_d0b(
    std::shared_ptr<const LearnedModel> value_model) {
    return fit_d0b_impl(
        std::move(value_model), false);
}

std::string d0b_two_stage_reset_fingerprint(
    std::shared_ptr<const LearnedModel> value_model) {
    if (!value_model) {
        throw std::invalid_argument(
            "FQ4-D0b reset control requires a Value parent");
    }
    const std::vector<TrainingInput> inputs =
        make_inputs(
            all_fixture_probes(), value_model, false);
    const auto examples = training_examples(inputs);
    const auto anchor =
        update_learned_value_priority_head(
            value_model, examples,
            optimizer_config(kD0bAnchorEpochs));
    const auto reset =
        update_learned_value_priority_head(
            anchor, examples,
            optimizer_config(kD0bAnchorEpochs));
    return learned_model_fingerprint(reset);
}

bool d0b_exact_margin_contract(
    const std::vector<double>& parent_margins,
    const std::vector<double>& anchor_margins) {
    return exact_margin_bits_match(
               parent_margins,
               kD0bRequiredParentMarginBits) &&
           exact_margin_bits_match(
               anchor_margins,
               kD0bRequiredAnchorMarginBits);
}

} // namespace testing

} // namespace old_school::fq4_priority_fit
