#include "old_school/fq0_bellman_audit.hpp"

#include "old_school/fq0_bellman_science.hpp"
#include "old_school/fq0_dominance_transition.hpp"
#include "old_school/oc1_action_scoring.hpp"
#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::fq0_bellman_audit {
namespace {

namespace science = fq0_bellman_science;
namespace scoring = oc1_action_scoring;

static_assert(kC16RankingSeedTag == scoring::kProductionTag);
static_assert(
    kC16RankingSeedBase == scoring::kProductionSeedBase);
static_assert(
    kC16RankingWorlds == scoring::kProductionWorlds);
static_assert(
    kC16RankingHorizonTurns ==
    scoring::kProductionHorizonTurns);
static_assert(
    kC16RankingRolloutsPerWorld ==
    scoring::kProductionRolloutsPerWorld);
static_assert(
    kC16RankingBlendShallowPrior ==
    scoring::kProductionBlendShallowPrior);
static_assert(
    kC16RankingThreads ==
    scoring::kProductionEvaluationThreads);
static_assert(
    kC16RankingContinuationEpsilon ==
    scoring::kValueContinuationEpsilon);
static_assert(
    kC16RankingPriorityResidualWeight ==
    scoring::kValuePriorityResidualWeight);
static_assert(
    kC16RankingPassDominance ==
    scoring::kValuePassDominance);
static_assert(
    kC16RankingContinuationController ==
    scoring::kContinuationController);

void require(bool condition, std::string message) {
    if (!condition) {
        throw std::runtime_error(std::move(message));
    }
}

bool bit_equal(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool probability(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

void require_execution(
    const science::ExecutionMetadata& execution,
    std::size_t workers, std::string_view coordinate) {
    require(
        execution.workers_requested == workers &&
            execution.maximum_workers_started == workers &&
            execution.parallel_batches > 0 &&
            execution.indexed_tasks > 0,
        std::string(coordinate) +
            ": indexed execution accounting is incomplete");
}

ScopeKind map_scope_kind(science::ScopeKind kind) {
    switch (kind) {
        case science::ScopeKind::Full:
            return ScopeKind::FullK64;
        case science::ScopeKind::Block:
            return ScopeKind::BlockK8;
    }
    throw std::runtime_error("FQ0 science scope kind is invalid");
}

LeafSampleEvidence take_leaf_sample(science::LeafSample source) {
    return {
        .world_index = source.world_index,
        .determinization_seed = source.determinization_seed,
        .macro_seed = source.macro_seed,
        .score_bits =
            std::bit_cast<std::uint64_t>(source.score),
        .redacted_leaf_hash =
            std::move(source.redacted_leaf_hash),
        .terminal = source.terminal,
        .forced_action_applied =
            source.forced_action_applied,
        .critic_evaluated = source.critic_evaluated,
        .contextual_score_bits =
            source.contextual_score_bits,
        .legacy_score_bits = source.legacy_score_bits,
        .actions_applied = source.actions_applied,
        .priority_actions_applied =
            source.priority_actions_applied,
        .phase_transitions = source.phase_transitions,
        .turn_advances = source.turn_advances,
    };
}

GroupActionEvidence take_group_action(
    science::GroupAction source) {
    GroupActionEvidence result{
        .descriptor = std::move(source.descriptor),
        .action = source.action,
        .feature_row_id = std::move(source.feature_row_id),
        .policy_features =
            std::move(source.policy_features),
        .canonical_consequence_fingerprint =
            std::move(
                source.canonical_consequence_fingerprint),
    };
    result.samples.reserve(source.samples.size());
    for (science::LeafSample& sample : source.samples) {
        result.samples.push_back(
            take_leaf_sample(std::move(sample)));
    }
    return result;
}

GroupBankEvidence take_group_bank(science::GroupBank source) {
    GroupBankEvidence result{
        .bank = std::move(source.bank),
        .stream_key = std::move(source.stream_key),
    };
    result.actions.reserve(source.actions.size());
    for (science::GroupAction& action : source.actions) {
        result.actions.push_back(
            take_group_action(std::move(action)));
    }
    return result;
}

RootTransitionParticleEvidence take_root_transition(
    science::RootTransition source) {
    return {
        .world_index = source.world_index,
        .determinization_seed = source.determinization_seed,
        .macro_seed = source.macro_seed,
        .redacted_result_hash =
            std::move(source.redacted_result_hash),
        .terminal = source.terminal,
        .terminal_root_owner_value_bits =
            source.terminal
                ? std::bit_cast<std::uint64_t>(
                      source.terminal_root_owner_value)
                : 0,
        .successor_information_set_fingerprint =
            std::move(
                source
                    .successor_information_set_fingerprint),
        .successor_owner = source.successor_owner,
        .forced_root_action_applied =
            source.forced_action_applied,
        .successful_disposition = true,
        .actions_applied = source.actions_applied,
        .priority_actions_applied =
            source.priority_actions_applied,
        .phase_transitions = source.phase_transitions,
        .turn_advances = source.turn_advances,
    };
}

SuccessorGroupEvidence take_successor_group(
    science::SuccessorGroup source) {
    return {
        .information_set_fingerprint =
            std::move(source.information_set_fingerprint),
        .successor_owner = source.successor_owner,
        .relation = source.relation,
        .root_world_indices =
            std::move(source.root_world_indices),
        .representative_root_world =
            source.representative_root_world,
        .representative_root_action_descriptor =
            std::move(
                source
                    .representative_root_action_descriptor),
        .bank_a = take_group_bank(
            std::move(source.bank_a)),
        .bank_b = take_group_bank(
            std::move(source.bank_b)),
        .cross_fit = std::move(source.cross_fit),
    };
}

ScopeEvidence take_scope(science::Scope source) {
    ScopeEvidence result{
        .kind = map_scope_kind(source.kind),
        .block = source.block,
        .root_world_indices =
            std::move(source.root_world_indices),
        .terminals = std::move(source.terminals),
        .target = source.target,
        .exact_particle_partition =
            source.exact_particle_partition,
    };
    result.groups.reserve(source.groups.size());
    for (science::SuccessorGroup& group : source.groups) {
        result.groups.push_back(
            take_successor_group(std::move(group)));
    }
    return result;
}

RootActionEvidence take_root_action(
    science::RootAction source) {
    RootActionEvidence result{
        .descriptor = std::move(source.descriptor),
        .action = source.action,
        .feature_row_id = std::move(source.feature_row_id),
        .target = source.target,
        .policy_features =
            std::move(source.policy_features),
        .canonical_consequence_fingerprint =
            std::move(
                source.canonical_consequence_fingerprint),
    };
    result.root_transitions.reserve(
        source.root_transitions.size());
    for (science::RootTransition& transition :
         source.root_transitions) {
        result.root_transitions.push_back(
            take_root_transition(std::move(transition)));
    }
    result.scopes.reserve(source.scopes.size());
    for (science::Scope& scope : source.scopes) {
        result.scopes.push_back(
            take_scope(std::move(scope)));
    }
    return result;
}

RootEvidence take_root(science::Root source) {
    RootEvidence result{
        .stable_id = std::move(source.stable_id),
        .manifest_information_action_fingerprint =
            std::move(
                source
                    .manifest_information_action_fingerprint),
        .root_deck = source.root_deck,
        .root_player = source.root_player,
        .exact_support = std::move(source.exact_support),
    };
    result.actions.reserve(source.actions.size());
    for (science::RootAction& action : source.actions) {
        result.actions.push_back(
            take_root_action(std::move(action)));
    }
    return result;
}

SuccessorFeatureScopeEvidence take_feature_scope(
    science::SuccessorFeatureScope source) {
    return {
        .kind = map_scope_kind(source.kind),
        .block = source.block,
        .bank_a = take_group_bank(
            std::move(source.bank_a)),
        .bank_b = take_group_bank(
            std::move(source.bank_b)),
    };
}

SuccessorFeatureEvaluationEvidence take_feature_evaluation(
    science::SuccessorFeatureEvaluation source) {
    SuccessorFeatureEvaluationEvidence result{
        .root_stable_id =
            std::move(source.root_stable_id),
        .information_set_fingerprint =
            std::move(source.information_set_fingerprint),
        .successor_owner = source.successor_owner,
        .representative_root_world =
            source.representative_root_world,
        .representative_root_action_descriptor =
            std::move(
                source
                    .representative_root_action_descriptor),
    };
    result.scopes.reserve(source.scopes.size());
    for (science::SuccessorFeatureScope& scope :
         source.scopes) {
        result.scopes.push_back(
            take_feature_scope(std::move(scope)));
    }
    return result;
}

ScientificEvidence take_construction_core(
    science::Construction source,
    const ac1_teacher_audit::Manifest& canonical_manifest) {
    require(
        source.manifest == canonical_manifest,
        "FQ0 construction manifest is not the bound "
        "canonical manifest");
    require(
        !source.model_fingerprint.empty() &&
            source.semantic_sha256.size() == 64,
        "FQ0 construction identity or semantic digest is "
        "missing");
    ScientificEvidence result{
        .manifest = canonical_manifest,
        .model_fingerprint =
            std::move(source.model_fingerprint),
        .feature_rows = std::move(source.feature_rows),
        .feature_collisions =
            std::move(source.feature_collisions),
        .roots_by_deck = source.roots_by_deck,
    };
    result.roots.reserve(source.roots.size());
    for (science::Root& root : source.roots) {
        result.roots.push_back(
            take_root(std::move(root)));
    }
    result.successor_feature_evaluations.reserve(
        source.successor_feature_evaluations.size());
    for (science::SuccessorFeatureEvaluation& evaluation :
         source.successor_feature_evaluations) {
        result.successor_feature_evaluations.push_back(
            take_feature_evaluation(
                std::move(evaluation)));
    }
    return result;
}

const ac1_teacher_audit::ManifestRoot& manifest_root_for(
    const ac1_teacher_audit::Manifest& manifest,
    std::string_view stable_id) {
    const auto found = std::find_if(
        manifest.roots.begin(), manifest.roots.end(),
        [&](const ac1_teacher_audit::ManifestRoot& root) {
            return root.probe.stable_id == stable_id;
        });
    require(
        found != manifest.roots.end(),
        "FQ0 construction root is absent from its manifest");
    return *found;
}

const RootEvidence& evidence_root_for(
    const ScientificEvidence& evidence,
    std::string_view stable_id) {
    const auto found = std::lower_bound(
        evidence.roots.begin(), evidence.roots.end(),
        stable_id,
        [](const RootEvidence& root,
           std::string_view key) {
            return root.stable_id < key;
        });
    require(
        found != evidence.roots.end() &&
            found->stable_id == stable_id,
        "FQ0 evidence is missing a requested root");
    return *found;
}

RootEvidence& evidence_root_for(
    ScientificEvidence& evidence,
    std::string_view stable_id) {
    return const_cast<RootEvidence&>(
        evidence_root_for(
            static_cast<const ScientificEvidence&>(evidence),
            stable_id));
}

const RootActionEvidence& evidence_action_for(
    const RootEvidence& root, std::string_view descriptor) {
    const auto found = std::lower_bound(
        root.actions.begin(), root.actions.end(), descriptor,
        [](const RootActionEvidence& action,
           std::string_view key) {
            return action.descriptor < key;
        });
    require(
        found != root.actions.end() &&
            found->descriptor == descriptor,
        root.stable_id +
            ": FQ0 evidence is missing a requested action");
    return *found;
}

bool snapshot_matches_requirement(
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    std::error_code path_error;
    const std::filesystem::path expected =
        std::filesystem::absolute(
            std::filesystem::path(kModelArtifactPath),
            path_error)
            .lexically_normal();
    return !path_error &&
           std::filesystem::path(snapshot.path)
                   .lexically_normal() == expected &&
           snapshot.byte_size == kModelArtifactBytes &&
           snapshot.sha256 == kModelArtifactSha256;
}

void require_evidence_destination_absent() {
    const std::array<std::filesystem::path, 2> paths{
        std::filesystem::path(kEvidencePath),
        std::filesystem::path(
            std::string(kEvidencePath) + ".tmp"),
    };
    for (const std::filesystem::path& path : paths) {
        std::error_code error;
        const std::filesystem::file_status status =
            std::filesystem::symlink_status(path, error);
        if (error &&
            error != std::errc::no_such_file_or_directory) {
            throw std::runtime_error(
                "cannot inspect FQ0 evidence destination '" +
                path.string() + "': " + error.message());
        }
        require(
            !std::filesystem::exists(status),
            "FQ0 evidence destination or temporary already "
            "exists");
    }
}

std::shared_ptr<const LearnedModel> load_frozen_model() {
    const LearnedValueChallengerArtifact artifact =
        load_learned_value_challenger_artifact(
            std::string(kModelArtifactPath),
            kModelTrainingGames, kModelTrainingSeed,
            kModelGenerations);
    const std::shared_ptr<const LearnedModel> model =
        artifact.model();
    require(
        model &&
            learned_model_fingerprint(model) ==
                kModelFingerprint &&
            learned_critic_schema(model) ==
                LearnedCriticSchema::LegacyStateOnly,
        "FQ0 frozen C16 model identity or critic schema "
        "drifted");
    return model;
}

int exact_order(double first, double second) {
    if (bit_equal(first, second)) {
        return 0;
    }
    require(
        first != second,
        "FQ0 ranking contains equal numeric scores with "
        "different IEEE encodings");
    return first < second ? -1 : 1;
}

RankingSummaryEvidence rank_against_c16(
    const ScientificEvidence& evidence,
    std::shared_ptr<const LearnedModel> model) {
    RankingSummaryEvidence summary;
    summary.roots.reserve(evidence.roots.size());
    std::array<double, kDeckCount> pairwise_sums{};
    std::array<std::size_t, kDeckCount>
        support_changed_counts{};
    std::array<std::size_t, kDeckCount> root_counts{};

    for (const RootEvidence& root : evidence.roots) {
        const ac1_teacher_audit::ManifestRoot& manifest_root =
            manifest_root_for(
                evidence.manifest, root.stable_id);
        const scoring::DecisionScore score =
            scoring::score_production(
                manifest_root.probe, model);
        const scoring::AppliedRecipe expected_recipe{
            .seed_source = scoring::SeedSource::Derived,
            .seed_tag = std::string(kC16RankingSeedTag),
            .seed_base = kC16RankingSeedBase,
            .resolved_seed =
                probe_runner::reference_seed_for_probe(
                    kC16RankingSeedTag, root.stable_id,
                    kC16RankingSeedBase),
            .worlds = kC16RankingWorlds,
            .horizon_turns =
                kC16RankingHorizonTurns,
            .rollouts_per_world =
                kC16RankingRolloutsPerWorld,
            .blend_shallow_prior =
                kC16RankingBlendShallowPrior,
            .evaluation_threads =
                kC16RankingThreads,
            .value_mirror = true,
            .value_continuation_epsilon =
                kC16RankingContinuationEpsilon,
            .value_priority_residual_weight =
                kC16RankingPriorityResidualWeight,
            .value_pass_dominance =
                kC16RankingPassDominance,
            .value_continuation_controller =
                kC16RankingContinuationController,
        };
        const std::size_t samples_per_action =
            kC16RankingWorlds *
            kC16RankingRolloutsPerWorld;
        const std::size_t expected_evaluations =
            root.actions.size() * samples_per_action;
        require(
            score.stable_id == root.stable_id &&
                score.decision_kind ==
                    probes::DecisionKind::Priority &&
                score.score_mode ==
                    scoring::ScoreMode::
                        ProductionPrioritySearch &&
                score.recipe == expected_recipe &&
                !score.deterministic_selection &&
                score.actions.size() ==
                    root.actions.size() &&
                score.selected_support ==
                    scoring::exact_max_support(
                        score.actions) &&
                score.accounting.sampled_worlds ==
                    kC16RankingWorlds &&
                score.accounting.rollout_evaluations ==
                    expected_evaluations &&
                score.accounting.terminal_evaluations <=
                    expected_evaluations &&
                score.accounting
                        .bootstrapped_evaluations ==
                    expected_evaluations -
                        score.accounting
                            .terminal_evaluations &&
                std::all_of(
                    score.actions.begin(),
                    score.actions.end(),
                    [&](const scoring::DescriptorScore&
                            action) {
                        return action.raw_samples.size() ==
                               samples_per_action;
                    }),
            root.stable_id +
                ": C16 production score or applied recipe "
                "is incomplete");

        C16RootRankingEvidence ranking{
            .stable_id = root.stable_id,
            .exact_support = score.selected_support,
        };
        ranking.actions.reserve(score.actions.size());
        std::size_t changed_pairs = 0;
        const std::size_t pairs =
            score.actions.size() *
            (score.actions.size() - 1) / 2;
        for (std::size_t action = 0;
             action < score.actions.size(); ++action) {
            require(
                score.actions[action].descriptor ==
                        root.actions[action].descriptor &&
                    probability(
                        score.actions[action].raw_score),
                root.stable_id +
                    ": C16 score/action order drifted");
            ranking.actions.push_back({
                .descriptor =
                    score.actions[action].descriptor,
                .score_bits =
                    std::bit_cast<std::uint64_t>(
                        score.actions[action].raw_score),
            });
        }
        for (std::size_t first = 0;
             first < score.actions.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < score.actions.size(); ++second) {
                changed_pairs +=
                    exact_order(
                        score.actions[first].raw_score,
                        score.actions[second].raw_score) !=
                            exact_order(
                                root.actions[first]
                                    .target.full,
                                root.actions[second]
                                    .target.full)
                        ? 1U
                        : 0U;
            }
        }
        const double pairwise_fraction =
            pairs == 0
                ? 0.0
                : static_cast<double>(changed_pairs) /
                      static_cast<double>(pairs);
        ranking.pairwise_change_fraction_bits =
            std::bit_cast<std::uint64_t>(
                pairwise_fraction);
        ranking.support_changed =
            ranking.exact_support != root.exact_support;
        const std::size_t deck =
            static_cast<std::size_t>(root.root_deck);
        require(
            deck < kDeckCount,
            root.stable_id +
                ": C16 ranking has an invalid deck");
        pairwise_sums[deck] += pairwise_fraction;
        support_changed_counts[deck] +=
            ranking.support_changed ? 1U : 0U;
        ++root_counts[deck];
        summary.roots.push_back(std::move(ranking));
    }

    double equal_deck_pairwise = 0.0;
    double equal_deck_support = 0.0;
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        require(
            root_counts[deck] > 0,
            "FQ0 C16 summary has an empty deck");
        const double pairwise =
            pairwise_sums[deck] /
            static_cast<double>(root_counts[deck]);
        const double support =
            static_cast<double>(
                support_changed_counts[deck]) /
            static_cast<double>(root_counts[deck]);
        summary.decks[deck] = {
            .deck = static_cast<DeckId>(deck),
            .roots = root_counts[deck],
            .support_changed_roots =
                support_changed_counts[deck],
            .mean_pairwise_change_fraction_bits =
                std::bit_cast<std::uint64_t>(pairwise),
            .support_changed_fraction_bits =
                std::bit_cast<std::uint64_t>(support),
        };
        equal_deck_pairwise += pairwise;
        equal_deck_support += support;
    }
    equal_deck_pairwise /=
        static_cast<double>(kDeckCount);
    equal_deck_support /=
        static_cast<double>(kDeckCount);
    summary.equal_deck_pairwise_change_fraction_bits =
        std::bit_cast<std::uint64_t>(
            equal_deck_pairwise);
    summary.equal_deck_support_changed_fraction_bits =
        std::bit_cast<std::uint64_t>(
            equal_deck_support);
    summary.complete = true;
    return summary;
}

bool typed_x_zero(
    std::string_view stable_id,
    const PriorityAction& action) {
    if (stable_id ==
        "control.blue.braingeyser-x0.v1") {
        return action.kind ==
                   PriorityActionKind::CastBraingeyser &&
               action.card == CardId::Braingeyser &&
               action.x_value == 0;
    }
    if (stable_id == "ru.disintegrate-player-x.v3" ||
        stable_id ==
            "validation.ru.disintegrate-hold-x0.v1") {
        return action.kind ==
                   PriorityActionKind::CastDisintegrate &&
               action.card == CardId::Disintegrate &&
               action.x_value == 0;
    }
    return false;
}

bool support_excludes_typed_x_zero(
    const RootEvidence& root) {
    return std::none_of(
        root.exact_support.begin(),
        root.exact_support.end(),
        [&](const std::string& descriptor) {
            return typed_x_zero(
                root.stable_id,
                evidence_action_for(root, descriptor).action);
        });
}

void add_contrast(
    ScientificEvidence& evidence, GateRole role,
    std::string name, std::string stable_id,
    std::string positive_descriptor,
    std::string negative_descriptor) {
    const RootEvidence& root =
        evidence_root_for(evidence, stable_id);
    const RootActionEvidence& positive =
        evidence_action_for(root, positive_descriptor);
    const RootActionEvidence& negative =
        evidence_action_for(root, negative_descriptor);
    const fq0_bellman::BlockContrast contrast =
        fq0_bellman::summarize_block_contrast(
            positive.target, negative.target);

    bool support_condition = true;
    bool directional = false;
    switch (role) {
        case GateRole::Primary:
            support_condition =
                root.exact_support ==
                std::vector<std::string>{"pass"};
            directional = true;
            break;
        case GateRole::LiveForceGuard:
            support_condition =
                root.exact_support ==
                std::vector<std::string>{
                    positive_descriptor};
            directional = true;
            break;
        case GateRole::GrowthTargetGuard:
            support_condition =
                std::find(
                    root.exact_support.begin(),
                    root.exact_support.end(),
                    negative_descriptor) ==
                root.exact_support.end();
            directional = true;
            break;
        case GateRole::ProductiveCounterGuard:
            support_condition =
                root.exact_support ==
                std::vector<std::string>{
                    positive_descriptor};
            break;
        case GateRole::RedundantCounterGuard:
            support_condition =
                root.exact_support ==
                std::vector<std::string>{"pass"};
            break;
        case GateRole::XZeroGuard:
            support_condition =
                support_excludes_typed_x_zero(root);
            break;
        case GateRole::Descriptive:
            directional = true;
            break;
        case GateRole::DominanceConsistencyGuard:
        case GateRole::IncomparableControlGuard:
            throw std::logic_error(
                "FQ0 dominance role cannot be a contrast");
    }
    const bool robust =
        fq0_bellman::passes_directional_gate(
            contrast, kPrimaryMinimumPositiveBlocks);
    evidence.contrasts.push_back({
        .role = role,
        .name = std::move(name),
        .stable_id = std::move(stable_id),
        .positive_descriptor =
            std::move(positive_descriptor),
        .negative_descriptor =
            std::move(negative_descriptor),
        .contrast = contrast,
        .support_condition = support_condition,
        .directional_passed =
            support_condition &&
            (directional ? robust : true),
    });
}

void build_contrasts(ScientificEvidence& evidence) {
    add_contrast(
        evidence, GateRole::Primary,
        "primary-pass-over-sick-bear-growth",
        "field.green.second-main-sick-bear-growth.v1",
        "pass",
        "growth-own-summoning-sick-grizzly-bears");
    add_contrast(
        evidence, GateRole::LiveForceGuard,
        "live-force-spike-over-pass",
        "control.blue.force-spike-live-gray-ogre.v1",
        "force-spike-gray-ogre", "pass");
    add_contrast(
        evidence, GateRole::GrowthTargetGuard,
        "own-treefolk-growth-over-opponent-air-growth",
        "field.green.begin-combat-growth-tapped-air.v1",
        "growth-own-ironroot-treefolk",
        "growth-opponent-tapped-air-elemental");

    const std::array<
        std::pair<std::string_view, std::string_view>, 3>
        productive{{
            {"blue.counter-fire-elemental.v3",
             "counter-fire-elemental"},
            {"blue.counter-lethal-bolt.v3",
             "counter-lethal-lightning-bolt"},
            {"blue.counter-war.v3",
             "counter-opponent-counterspell"},
        }};
    for (const auto& [stable_id, descriptor] :
         productive) {
        add_contrast(
            evidence, GateRole::ProductiveCounterGuard,
            std::string(stable_id) +
                ".productive-counter-support",
            std::string(stable_id),
            std::string(descriptor), "pass");
    }
    add_contrast(
        evidence, GateRole::RedundantCounterGuard,
        "redundant-same-target-counter-selects-pass",
        "control.blue.counter-redundant-same-target.v1",
        "pass", "counter-same-air-elemental");

    const std::array<std::string_view, 3> x_zero_roots{
        "control.blue.braingeyser-x0.v1",
        "ru.disintegrate-player-x.v3",
        "validation.ru.disintegrate-hold-x0.v1",
    };
    for (const std::string_view stable_id : x_zero_roots) {
        const RootEvidence& root =
            evidence_root_for(evidence, stable_id);
        for (const RootActionEvidence& action :
             root.actions) {
            if (!typed_x_zero(stable_id, action.action)) {
                continue;
            }
            add_contrast(
                evidence, GateRole::XZeroGuard,
                std::string(stable_id) + "." +
                    action.descriptor +
                    ".pass-over-x-zero",
                std::string(stable_id), "pass",
                action.descriptor);
        }
    }

    add_contrast(
        evidence, GateRole::Descriptive,
        "payable-force-spike-pass-minus-spike",
        "control.blue.force-spike-payable-gray-ogre.v1",
        "pass", "force-spike-gray-ogre");
    add_contrast(
        evidence, GateRole::Descriptive,
        "intervening-counter-counter-minus-pass",
        "control.blue.counter-same-target-after-intervening-counter.v1",
        "counter-opponent-counterspell", "pass");

    std::sort(
        evidence.contrasts.begin(),
        evidence.contrasts.end(),
        [](const ContrastEvidence& first,
           const ContrastEvidence& second) {
            return std::tie(
                       first.role, first.stable_id,
                       first.name) <
                   std::tie(
                       second.role, second.stable_id,
                       second.name);
        });
}

bool unordered_descriptors_are(
    std::string_view first, std::string_view second,
    std::string_view expected_first,
    std::string_view expected_second) {
    return (first == expected_first &&
            second == expected_second) ||
           (first == expected_second &&
            second == expected_first);
}

bool primary_dominance_pair(
    std::string_view stable_id, std::string_view first,
    std::string_view second) {
    return stable_id ==
               "field.green.second-main-sick-bear-growth.v1" &&
           unordered_descriptors_are(
               first, second, "pass",
               "growth-own-summoning-sick-grizzly-bears");
}

bool redundant_counter_dominance_pair(
    std::string_view stable_id, std::string_view first,
    std::string_view second) {
    return stable_id ==
               "control.blue.counter-redundant-same-target.v1" &&
           unordered_descriptors_are(
               first, second, "pass",
               "counter-same-air-elemental");
}

bool named_incomparable_pair(
    std::string_view stable_id, std::string_view first,
    std::string_view second) {
    const auto matches =
        [&](std::string_view required_stable_id,
            std::string_view nonpass) {
            return stable_id == required_stable_id &&
                   unordered_descriptors_are(
                       first, second, "pass", nonpass);
        };
    return matches(
               "control.blue.force-spike-live-gray-ogre.v1",
               "force-spike-gray-ogre") ||
           matches(
               "control.blue.force-spike-payable-gray-ogre.v1",
               "force-spike-gray-ogre") ||
           matches(
               "blue.counter-fire-elemental.v3",
               "counter-fire-elemental") ||
           matches(
               "blue.counter-lethal-bolt.v3",
               "counter-lethal-lightning-bolt") ||
           matches(
               "blue.counter-war.v3",
               "counter-opponent-counterspell") ||
           matches(
               "control.blue.counter-redundant-same-target.v1",
               "counter-own-counterspell");
}

fq0_dominance::Orientation pass_dominates_orientation(
    const science::RootAction& first,
    const science::RootAction& second) {
    require(
        first.action.kind == PriorityActionKind::Pass ||
            second.action.kind == PriorityActionKind::Pass,
        "FQ0 registered Pass dominance pair lacks Pass");
    return first.action.kind == PriorityActionKind::Pass
               ? fq0_dominance::Orientation::
                     FirstDominatesSecond
               : fq0_dominance::Orientation::
                     SecondDominatesFirst;
}

std::size_t manifest_candidate_index(
    const probes::DecisionProbe& probe,
    const science::RootAction& action) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [&](const probes::Candidate& candidate) {
            const auto* priority =
                std::get_if<PriorityAction>(
                    &candidate.action);
            return candidate.descriptor ==
                       action.descriptor &&
                   priority != nullptr &&
                   *priority == action.action;
        });
    require(
        found != probe.candidates.end(),
        probe.stable_id +
            ": FQ0 dominance action is absent from the "
            "frozen manifest");
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

struct DominancePairBuilder {
    std::size_t first_action = 0;
    std::size_t second_action = 0;
    DominancePairEvidence evidence;
};

bool dominance_pair_passed(
    GateRole role, std::size_t matching_worlds,
    bool monotonic, bool all_settlements_valid) {
    return matching_worlds == kRootWorlds &&
           monotonic &&
           (role == GateRole::Descriptive ||
            all_settlements_valid);
}

std::vector<DominancePairEvidence>
build_exhaustive_dominance(
    const science::Construction& construction,
    const ac1_teacher_audit::Manifest& manifest) {
    std::vector<DominancePairEvidence> result;
    for (const science::Root& root : construction.roots) {
        const ac1_teacher_audit::ManifestRoot& manifest_root =
            manifest_root_for(manifest, root.stable_id);
        require(
            root.sampled_worlds.size() == kRootWorlds &&
                root.actions.size() >= 2,
            root.stable_id +
                ": FQ0 dominance root census is incomplete");
        std::vector<std::size_t> candidate_indices;
        candidate_indices.reserve(root.actions.size());
        for (const science::RootAction& action :
             root.actions) {
            candidate_indices.push_back(
                manifest_candidate_index(
                    manifest_root.probe, action));
        }

        std::vector<DominancePairBuilder> pairs;
        for (std::size_t first = 0;
             first < root.actions.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < root.actions.size(); ++second) {
                pairs.push_back({
                    .first_action = first,
                    .second_action = second,
                    .evidence = {
                        .stable_id = root.stable_id,
                        .first_descriptor =
                            root.actions[first].descriptor,
                        .second_descriptor =
                            root.actions[second].descriptor,
                        .required_worlds = kRootWorlds,
                    },
                });
                pairs.back().evidence.worlds.reserve(
                    kRootWorlds);
            }
        }

        for (std::size_t world_index = 0;
             world_index < root.sampled_worlds.size();
             ++world_index) {
            const science::RootSampledWorld& sampled =
                root.sampled_worlds[world_index];
            require(
                sampled.world_index == world_index,
                root.stable_id +
                    ": dominance sampled-world order drifted");
            const science::HiddenRepartitionDiagnostic hidden =
                science::hidden_repartition(
                    sampled.state, root.root_player);
            require(
                hidden.eligible == hidden.changed,
                root.stable_id +
                    ": dominance hidden repartition "
                    "eligibility/change disagreed");

            // Settlements are cached once per action within this world, used
            // by every unordered pair, then released before the next world.
            // This bounds memory while avoiding O(action-pairs) transition
            // duplication.
            std::vector<fq0_dominance::Settlement> settlements;
            std::vector<fq0_dominance::Settlement>
                hidden_settlements;
            settlements.reserve(root.actions.size());
            hidden_settlements.reserve(root.actions.size());
            for (std::size_t action = 0;
                 action < root.actions.size(); ++action) {
                settlements.push_back(
                    fq0_dominance_transition::
                        advance_to_next_first_main(
                            manifest_root.probe,
                            sampled.state,
                            candidate_indices[action],
                            root
                                .manifest_information_action_fingerprint));
                hidden_settlements.push_back(
                    fq0_dominance_transition::
                        advance_to_next_first_main(
                            manifest_root.probe,
                            hidden.state,
                            candidate_indices[action],
                            root
                                .manifest_information_action_fingerprint));
            }

            for (DominancePairBuilder& pair : pairs) {
                const science::RootAction& first =
                    root.actions[pair.first_action];
                const science::RootAction& second =
                    root.actions[pair.second_action];
                DominanceWorldEvidence world{
                    .world_index = world_index,
                    .determinization_seed =
                        sampled.determinization_seed,
                    .common_world_key =
                        binding::dominance_common_world_key(
                            root.stable_id,
                            root
                                .manifest_information_action_fingerprint,
                            world_index),
                    .comparison = fq0_dominance::compare(
                        settlements[pair.first_action],
                        settlements[pair.second_action],
                        root.root_player),
                };
                world.orientation =
                    world.comparison.orientation;
                require(
                    world.comparison.root_information_equal,
                    root.stable_id +
                        ": dominance settlements escaped "
                        "their root information set");
                DominanceWorldEvidence hidden_world = world;
                hidden_world.comparison =
                    fq0_dominance::compare(
                        hidden_settlements[
                            pair.first_action],
                        hidden_settlements[
                            pair.second_action],
                        root.root_player);
                hidden_world.orientation =
                    hidden_world.comparison.orientation;
                const std::string baseline =
                    binding::
                        dominance_comparison_payload_sha256(
                            root.stable_id,
                            first.descriptor,
                            second.descriptor, world);
                const std::string comparison =
                    binding::
                        dominance_comparison_payload_sha256(
                            root.stable_id,
                            first.descriptor,
                            second.descriptor,
                            hidden_world);
                const std::string coordinate =
                    root.stable_id + "/" +
                    first.descriptor + "/" +
                    second.descriptor + "/" +
                    std::to_string(world_index);
                world.hidden_repartition_witness =
                    binding::make_witness(
                        "dominance-hidden", coordinate,
                        baseline, comparison);
                world.hidden_repartition_bit_identical =
                    baseline == comparison;
                require(
                    world.hidden_repartition_bit_identical,
                    coordinate +
                        ": dominance comparison changed "
                        "under hidden repartition");
                pair.evidence.worlds.push_back(
                    std::move(world));
            }
        }

        for (DominancePairBuilder& pair : pairs) {
            const science::RootAction& first =
                root.actions[pair.first_action];
            const science::RootAction& second =
                root.actions[pair.second_action];
            const bool primary = primary_dominance_pair(
                root.stable_id, first.descriptor,
                second.descriptor);
            const bool x_zero =
                ((first.action.kind ==
                      PriorityActionKind::Pass &&
                  typed_x_zero(
                      root.stable_id, second.action)) ||
                 (second.action.kind ==
                      PriorityActionKind::Pass &&
                  typed_x_zero(
                      root.stable_id, first.action)));
            const bool redundant =
                redundant_counter_dominance_pair(
                    root.stable_id, first.descriptor,
                    second.descriptor);
            const bool incomparable =
                named_incomparable_pair(
                    root.stable_id, first.descriptor,
                    second.descriptor);
            const fq0_dominance::Orientation observed =
                pair.evidence.worlds.front().orientation;
            const bool uniform = std::all_of(
                pair.evidence.worlds.begin(),
                pair.evidence.worlds.end(),
                [&](const DominanceWorldEvidence& world) {
                    return world.orientation == observed;
                });
            const bool uniform_strict =
                uniform &&
                observed !=
                    fq0_dominance::Orientation::
                        Incomparable;

            if (primary) {
                pair.evidence.role = GateRole::Primary;
                pair.evidence.required_orientation =
                    pass_dominates_orientation(
                        first, second);
            } else if (x_zero) {
                pair.evidence.role = GateRole::XZeroGuard;
                pair.evidence.required_orientation =
                    pass_dominates_orientation(
                        first, second);
            } else if (incomparable) {
                pair.evidence.role =
                    GateRole::IncomparableControlGuard;
                pair.evidence.required_orientation =
                    fq0_dominance::Orientation::
                        Incomparable;
            } else if (redundant) {
                pair.evidence.role =
                    GateRole::DominanceConsistencyGuard;
                pair.evidence.required_orientation =
                    pass_dominates_orientation(
                        first, second);
            } else if (uniform_strict) {
                pair.evidence.role =
                    GateRole::DominanceConsistencyGuard;
                pair.evidence.required_orientation =
                    observed;
            } else {
                pair.evidence.role = GateRole::Descriptive;
                pair.evidence.required_orientation =
                    uniform
                        ? observed
                        : fq0_dominance::Orientation::
                              Incomparable;
            }

            const bool all_settlements_valid =
                std::all_of(
                    pair.evidence.worlds.begin(),
                    pair.evidence.worlds.end(),
                    [](const DominanceWorldEvidence&
                           world) {
                        return world.comparison.first.valid &&
                               world.comparison.second.valid;
                    });
            pair.evidence.matching_worlds =
                static_cast<std::size_t>(std::count_if(
                    pair.evidence.worlds.begin(),
                    pair.evidence.worlds.end(),
                    [&](const DominanceWorldEvidence& world) {
                        return world.orientation ==
                               pair.evidence
                                   .required_orientation;
                    }));
            bool monotonic = true;
            if (pair.evidence.required_orientation !=
                fq0_dominance::Orientation::Incomparable) {
                const bool first_dominates =
                    pair.evidence.required_orientation ==
                    fq0_dominance::Orientation::
                        FirstDominatesSecond;
                const fq0_bellman::BlockContrast check =
                    fq0_bellman::summarize_block_contrast(
                        first_dominates
                            ? first.target
                            : second.target,
                        first_dominates
                            ? second.target
                            : first.target);
                monotonic =
                    check.delta64 >= 0.0 &&
                    check.nonnegative_blocks >=
                        kPrimaryMinimumPositiveBlocks;
            }
            pair.evidence.passed =
                dominance_pair_passed(
                    pair.evidence.role,
                    pair.evidence.matching_worlds,
                    monotonic, all_settlements_valid);
            result.push_back(
                std::move(pair.evidence));
        }
    }
    return result;
}

std::string root_action_coordinate(
    std::string_view stable_id, std::size_t action,
    std::size_t scope, std::size_t group) {
    return std::string(stable_id) + "/action/" +
           std::to_string(action) + "/scope/" +
           std::to_string(scope) + "/group/" +
           std::to_string(group);
}

struct GroupProof {
    std::size_t root = 0;
    std::size_t action = 0;
    std::size_t scope = 0;
    std::size_t group = 0;
    std::vector<
        science::RepresentativeReconstructionWitness>
        representatives;
    science::HiddenRepartitionReconstructionWitness
        hidden_repartition;
};

struct FeatureScopeProof {
    std::size_t evaluation = 0;
    std::size_t scope = 0;
    std::vector<
        science::RepresentativeReconstructionWitness>
        representatives;
    science::RepresentativeReconstructionWitness
        hidden_repartition;
    std::vector<
        science::HiddenRepartitionReconstructionWitness>
        empirical_group_hidden_repartitions;
    std::set<std::pair<std::size_t, std::string>>
        consumed_empirical_hidden_coordinates;
    bool hidden_eligible = false;
    bool hidden_changed = false;
};

struct ReconstructionProofs {
    std::vector<GroupProof> groups;
    std::vector<FeatureScopeProof> feature_scopes;
    std::size_t feature_scope_reconstructions = 0;
    std::size_t group_occurrences_reused = 0;
};

template <typename ReconstructFeatureScope>
ReconstructionProofs reconstruct_all_information_sets_impl(
    const science::Construction& construction,
    const ac1_teacher_audit::Manifest& manifest,
    std::size_t expected_workers,
    ReconstructFeatureScope&& reconstruct_feature_scope) {
    ReconstructionProofs proofs;
    for (std::size_t evaluation_index = 0;
         evaluation_index <
         construction.successor_feature_evaluations.size();
         ++evaluation_index) {
        const science::SuccessorFeatureEvaluation& evaluation =
            construction.successor_feature_evaluations
                [evaluation_index];
        const auto root = std::lower_bound(
            construction.roots.begin(),
            construction.roots.end(),
            evaluation.root_stable_id,
            [](const science::Root& value,
               std::string_view key) {
                return value.stable_id < key;
            });
        require(
            root != construction.roots.end() &&
                root->stable_id ==
                    evaluation.root_stable_id,
            "FQ0 feature reconstruction root is missing");
        const ac1_teacher_audit::ManifestRoot& manifest_root =
            manifest_root_for(
                manifest, evaluation.root_stable_id);
        const std::size_t root_index =
            static_cast<std::size_t>(
                std::distance(
                    construction.roots.begin(), root));
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            science::GroupReconstructionWitnesses
                witness = reconstruct_feature_scope(
                    construction, root_index,
                    evaluation_index, scope_index,
                    manifest_root, *root, evaluation,
                    evaluation.scopes[scope_index]);
            ++proofs.feature_scope_reconstructions;
            require_execution(
                witness.execution,
                expected_workers,
                evaluation.root_stable_id +
                    "/feature-reconstruction");
            require(
                witness
                        .every_representative_bit_identical &&
                    witness
                        .hidden_repartition_bit_identical &&
                    witness.representatives.size() ==
                        evaluation.members.size(),
                evaluation.root_stable_id +
                    ": feature-only scope did not "
                    "reconstruct every physical member");
            for (std::size_t member = 0;
                 member < evaluation.members.size();
                 ++member) {
                require(
                    witness.representatives[member]
                                .root_action_descriptor ==
                            evaluation.members[member]
                                .root_action_descriptor &&
                        witness.representatives[member]
                                .root_world ==
                            evaluation.members[member]
                                .root_world &&
                        witness.representatives[member]
                            .identity.bit_identical(),
                    evaluation.root_stable_id +
                        ": feature-only member coordinate "
                        "drifted");
            }
            require(
                witness.hidden_repartition
                            .root_action_descriptor ==
                        evaluation
                            .representative_root_action_descriptor &&
                    witness.hidden_repartition.root_world ==
                        evaluation
                            .representative_root_world &&
                    witness.hidden_repartition.identity
                        .bit_identical() &&
                    witness
                            .hidden_repartition_eligible ==
                        witness
                            .hidden_repartition_changed,
                evaluation.root_stable_id +
                    ": feature hidden representative drifted");
            std::optional<
                std::pair<std::size_t, std::string>>
                previous_empirical_hidden;
            for (const auto& hidden :
                 witness
                     .empirical_group_hidden_repartitions) {
                const auto coordinate = std::pair{
                    hidden.representative.root_world,
                    hidden.representative
                        .root_action_descriptor,
                };
                require(
                    (!previous_empirical_hidden.has_value() ||
                     *previous_empirical_hidden <
                         coordinate) &&
                        hidden.bit_identical &&
                        hidden.representative.identity
                            .bit_identical() &&
                        hidden.eligible ==
                            hidden.changed,
                    evaluation.root_stable_id +
                        ": empirical-group hidden "
                        "reconstruction is invalid or "
                        "noncanonical");
                previous_empirical_hidden =
                    coordinate;
            }
            FeatureScopeProof proof{
                .evaluation = evaluation_index,
                .scope = scope_index,
                .representatives =
                    std::move(witness.representatives),
                .hidden_repartition =
                    std::move(
                        witness.hidden_repartition),
                .empirical_group_hidden_repartitions =
                    std::move(
                        witness
                            .empirical_group_hidden_repartitions),
                .hidden_eligible =
                    witness.hidden_repartition_eligible,
                .hidden_changed =
                    witness.hidden_repartition_changed,
            };
            proofs.feature_scopes.push_back(
                std::move(proof));
        }
    }

    const auto feature_evaluation_index_for =
        [&](std::string_view root_stable_id,
            const science::SuccessorGroup& group) {
            std::optional<std::size_t> found;
            for (std::size_t index = 0;
                 index <
                 construction
                     .successor_feature_evaluations.size();
                 ++index) {
                const auto& evaluation =
                    construction
                        .successor_feature_evaluations[index];
                if (evaluation.root_stable_id ==
                        root_stable_id &&
                    evaluation
                            .information_set_fingerprint ==
                        group
                            .information_set_fingerprint &&
                    evaluation.successor_owner ==
                        group.successor_owner) {
                    require(
                        !found.has_value(),
                        std::string(root_stable_id) +
                            ": duplicate successor feature "
                            "evaluation");
                    found = index;
                }
            }
            require(
                found.has_value(),
                std::string(root_stable_id) +
                    ": empirical successor group has no "
                    "feature evaluation");
            return *found;
        };

    for (std::size_t root_index = 0;
         root_index < construction.roots.size();
         ++root_index) {
        const science::Root& root =
            construction.roots[root_index];
        for (std::size_t action_index = 0;
             action_index < root.actions.size();
             ++action_index) {
            const science::RootAction& action =
                root.actions[action_index];
            for (std::size_t scope_index = 0;
                 scope_index < action.scopes.size();
                 ++scope_index) {
                const science::Scope& scope =
                    action.scopes[scope_index];
                for (std::size_t group_index = 0;
                     group_index < scope.groups.size();
                     ++group_index) {
                    const science::SuccessorGroup& group =
                        scope.groups[group_index];
                    const std::size_t evaluation_index =
                        feature_evaluation_index_for(
                            root.stable_id, group);
                    const auto& evaluation =
                        construction
                            .successor_feature_evaluations
                                [evaluation_index];
                    std::optional<std::size_t>
                        feature_scope_index;
                    for (std::size_t index = 0;
                         index < evaluation.scopes.size();
                         ++index) {
                        const auto& feature_scope =
                            evaluation.scopes[index];
                        if (feature_scope.kind ==
                                scope.kind &&
                            feature_scope.block ==
                                scope.block) {
                            require(
                                !feature_scope_index
                                     .has_value(),
                                root.stable_id +
                                    ": duplicate successor "
                                    "feature scope");
                            feature_scope_index = index;
                        }
                    }
                    require(
                        feature_scope_index.has_value(),
                        root.stable_id +
                            ": empirical successor group "
                            "has no matching feature scope");
                    const auto& feature_scope =
                        evaluation.scopes
                            [*feature_scope_index];
                    require(
                        group.bank_a ==
                                feature_scope.bank_a &&
                            group.bank_b ==
                                feature_scope.bank_b,
                        root.stable_id +
                            ": empirical group bank is not "
                            "the cached feature-scope bank");
                    const auto proof_it = std::find_if(
                        proofs.feature_scopes.begin(),
                        proofs.feature_scopes.end(),
                        [&](const FeatureScopeProof& proof) {
                            return proof.evaluation ==
                                       evaluation_index &&
                                   proof.scope ==
                                       *feature_scope_index;
                        });
                    require(
                        proof_it !=
                            proofs.feature_scopes.end(),
                        root.stable_id +
                            ": cached feature-scope proof "
                            "is missing");
                    require(
                        group
                                .representative_root_action_descriptor ==
                            action.descriptor &&
                            std::binary_search(
                                group
                                    .root_world_indices.begin(),
                                group
                                    .root_world_indices.end(),
                                group
                                    .representative_root_world),
                        root.stable_id +
                            ": empirical group "
                            "representative action drifted");
                    const auto hidden_it = std::find_if(
                        proof_it
                            ->empirical_group_hidden_repartitions
                            .begin(),
                        proof_it
                            ->empirical_group_hidden_repartitions
                            .end(),
                        [&](const auto& hidden) {
                            return hidden.representative
                                           .root_action_descriptor ==
                                       action.descriptor &&
                                   hidden.representative
                                           .root_world ==
                                       group
                                           .representative_root_world;
                        });
                    require(
                        hidden_it !=
                                proof_it
                                    ->empirical_group_hidden_repartitions
                                    .end() &&
                            std::find_if(
                                std::next(hidden_it),
                                proof_it
                                    ->empirical_group_hidden_repartitions
                                    .end(),
                                [&](const auto& hidden) {
                                    return hidden
                                                   .representative
                                                   .root_action_descriptor ==
                                               action
                                                   .descriptor &&
                                           hidden
                                                   .representative
                                                   .root_world ==
                                               group
                                                   .representative_root_world;
                                }) ==
                                proof_it
                                    ->empirical_group_hidden_repartitions
                                    .end() &&
                            hidden_it->bit_identical &&
                            hidden_it->representative
                                .identity.bit_identical() &&
                            hidden_it->eligible ==
                                hidden_it->changed &&
                            proof_it
                                ->consumed_empirical_hidden_coordinates
                                .insert({
                                    group
                                        .representative_root_world,
                                    action.descriptor,
                                })
                                .second,
                        root.stable_id +
                            ": exact empirical-group hidden "
                            "witness is missing, duplicated, "
                            "or reused");
                    GroupProof proof{
                        .root = root_index,
                        .action = action_index,
                        .scope = scope_index,
                        .group = group_index,
                        .hidden_repartition = *hidden_it,
                    };
                    proof.representatives.reserve(
                        group.root_world_indices.size());
                    for (const std::size_t world :
                         group.root_world_indices) {
                        const auto member = std::find_if(
                            evaluation.members.begin(),
                            evaluation.members.end(),
                            [&](const auto& candidate) {
                                return candidate
                                               .root_action_descriptor ==
                                           action.descriptor &&
                                       candidate.root_world ==
                                           world;
                            });
                        require(
                            member !=
                                evaluation.members.end(),
                            root.stable_id +
                                ": empirical group member "
                                "is absent from the cached "
                                "feature catalog");
                        require(
                            std::find_if(
                                std::next(member),
                                evaluation.members.end(),
                                [&](const auto& candidate) {
                                    return candidate
                                                   .root_action_descriptor ==
                                               action
                                                   .descriptor &&
                                           candidate
                                                   .root_world ==
                                               world;
                                }) ==
                                evaluation.members.end(),
                            root.stable_id +
                                ": cached feature catalog "
                                "duplicates an empirical "
                                "group member");
                        const auto reconstructed =
                            std::find_if(
                                proof_it
                                    ->representatives.begin(),
                                proof_it
                                    ->representatives.end(),
                                [&](const auto& candidate) {
                                    return candidate
                                                   .root_action_descriptor ==
                                               action
                                                   .descriptor &&
                                           candidate.root_world ==
                                               world;
                                });
                        require(
                            reconstructed !=
                                    proof_it
                                        ->representatives.end() &&
                                std::find_if(
                                    std::next(reconstructed),
                                    proof_it
                                        ->representatives.end(),
                                    [&](const auto& candidate) {
                                        return candidate
                                                       .root_action_descriptor ==
                                                   action
                                                       .descriptor &&
                                               candidate
                                                       .root_world ==
                                                   world;
                                    }) ==
                                    proof_it
                                        ->representatives.end() &&
                                reconstructed->identity
                                    .bit_identical(),
                            root.stable_id +
                                ": exact physical-member "
                                "reconstruction witness is "
                                "missing or duplicated");
                        proof.representatives.push_back(
                            *reconstructed);
                    }
                    proofs.groups.push_back(
                        std::move(proof));
                    ++proofs.group_occurrences_reused;
                }
            }
        }
    }
    for (const FeatureScopeProof& proof :
         proofs.feature_scopes) {
        std::set<std::pair<std::size_t, std::string>>
            expected;
        for (const auto& hidden :
             proof.empirical_group_hidden_repartitions) {
            expected.insert({
                hidden.representative.root_world,
                hidden.representative
                    .root_action_descriptor,
            });
        }
        require(
            expected.size() ==
                    proof
                        .empirical_group_hidden_repartitions
                        .size() &&
                expected ==
                    proof
                        .consumed_empirical_hidden_coordinates,
            "FQ0 empirical-group hidden witness census "
            "was not consumed exactly once");
    }
    return proofs;
}

ReconstructionProofs reconstruct_all_information_sets(
    const science::Construction& construction,
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model) {
    std::vector<science::ProductionFeatureScopeReconstruction>
        reconstructed =
            science::
                reconstruct_all_production_feature_scopes(
                    construction, model);
    std::map<
        std::tuple<std::size_t, std::size_t, std::size_t>,
        std::size_t>
        by_coordinate;
    for (std::size_t index = 0;
         index < reconstructed.size(); ++index) {
        const auto& row = reconstructed[index];
        require(
            by_coordinate
                .emplace(
                    std::tuple{
                        row.root_index,
                        row.feature_evaluation_index,
                        row.scope_index},
                    index)
                .second,
            "FQ0 bulk feature reconstruction repeated a "
            "coordinate");
    }
    std::vector<bool> consumed(
        reconstructed.size(), false);
    ReconstructionProofs proofs =
        reconstruct_all_information_sets_impl(
        construction, manifest,
        science::kProductionWorkers,
        [&](const science::Construction&,
            std::size_t root_index,
            std::size_t evaluation_index,
            std::size_t scope_index,
            const ac1_teacher_audit::ManifestRoot&
                /*manifest_root*/,
            const science::Root& /*root*/,
            const science::SuccessorFeatureEvaluation&
                /*evaluation*/,
            const science::SuccessorFeatureScope&
                /*scope*/) {
            const auto found = by_coordinate.find(
                std::tuple{
                    root_index, evaluation_index,
                    scope_index});
            require(
                found != by_coordinate.end() &&
                    !consumed[found->second],
                "FQ0 bulk feature reconstruction is "
                "missing or reused");
            consumed[found->second] = true;
            return std::move(
                reconstructed[found->second].witnesses);
        });
    require(
        std::all_of(
            consumed.begin(), consumed.end(),
            [](bool value) { return value; }),
        "FQ0 bulk feature reconstruction contains an "
        "unconsumed coordinate");
    return proofs;
}

std::vector<fq0_bellman::ActionSamples>
bank_action_samples(const GroupBankEvidence& bank) {
    std::vector<fq0_bellman::ActionSamples> result;
    result.reserve(bank.actions.size());
    for (const GroupActionEvidence& action :
         bank.actions) {
        fq0_bellman::ActionSamples row{
            .descriptor = action.descriptor,
            .sample_stream_key = bank.stream_key,
        };
        row.world_indices.reserve(action.samples.size());
        row.samples.reserve(action.samples.size());
        for (const LeafSampleEvidence& sample :
             action.samples) {
            row.world_indices.push_back(
                sample.world_index);
            row.samples.push_back(
                std::bit_cast<double>(
                    sample.score_bits));
        }
        result.push_back(std::move(row));
    }
    return result;
}

fq0_bellman::CrossFitValue recompute_cross_fit(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b) {
    const std::vector<fq0_bellman::ActionSamples>
        samples_a = bank_action_samples(bank_a);
    const std::vector<fq0_bellman::ActionSamples>
        samples_b = bank_action_samples(bank_b);
    return fq0_bellman::cross_fit_v0(
        samples_a, samples_b);
}

BitIdentityEvidence map_reconstruction_identity(
    std::string domain, std::string coordinate,
    const science::BitIdentityWitness& identity,
    std::string_view expected_baseline) {
    require(
        identity.bit_identical() &&
            identity.baseline_sha256 ==
                expected_baseline,
        coordinate +
            ": science reconstruction digest is not "
            "bound to the retained raw banks");
    return binding::make_witness(
        std::move(domain), std::move(coordinate),
        identity.baseline_sha256,
        identity.comparison_sha256);
}

void validate_reconstruction_bindings(
    const ScientificEvidence& evidence);

void attach_reconstruction_proofs(
    ScientificEvidence& evidence,
    const ReconstructionProofs& proofs) {
    for (const GroupProof& proof : proofs.groups) {
        RootEvidence& root = evidence.roots.at(proof.root);
        RootActionEvidence& action =
            root.actions.at(proof.action);
        ScopeEvidence& scope =
            action.scopes.at(proof.scope);
        SuccessorGroupEvidence& group =
            scope.groups.at(proof.group);
        const std::string coordinate =
            root_action_coordinate(
                root.stable_id, proof.action,
                proof.scope, proof.group);
        require(
            proof.hidden_repartition.representative
                        .root_action_descriptor ==
                    action.descriptor &&
                proof.hidden_repartition.representative
                        .root_world ==
                    group.representative_root_world &&
                proof.hidden_repartition.bit_identical &&
                proof.hidden_repartition.representative
                    .identity.bit_identical() &&
                proof.hidden_repartition.eligible ==
                    proof.hidden_repartition.changed &&
                proof.representatives.size() ==
                    group.root_world_indices.size(),
            coordinate +
                ": empirical-group reconstruction proof "
                "does not match its exact representative");
        group.every_representative_reconstructs = true;
        group.hidden_repartition_eligible =
            proof.hidden_repartition.eligible;
        group.hidden_identity_changed =
            proof.hidden_repartition.changed;
        group.hidden_repartition_invariant = true;
        group.complete = true;
        const std::string baseline =
            binding::successor_bank_pair_payload_sha256(
                group.bank_a, group.bank_b,
                group.cross_fit);
        group.representative_reconstruction_witnesses
            .reserve(proof.representatives.size());
        for (std::size_t member = 0;
             member < proof.representatives.size();
             ++member) {
            const auto& representative =
                proof.representatives[member];
            const std::size_t world =
                group.root_world_indices[member];
            require(
                representative.root_world == world &&
                    representative
                            .root_action_descriptor ==
                        action.descriptor,
                coordinate +
                    ": physical-member reconstruction "
                    "coordinate changed during assembly");
            const std::string member_coordinate =
                coordinate + "/member/" +
                std::to_string(world);
            group.representative_reconstruction_witnesses
                .push_back(
                    map_reconstruction_identity(
                        "group-representative",
                        member_coordinate,
                        representative.identity,
                        baseline));
        }
        group.hidden_repartition_witness =
            map_reconstruction_identity(
                "group-hidden",
                binding::hidden_repartition_coordinate(
                    coordinate,
                    proof.hidden_repartition
                        .representative.root_world,
                    proof.hidden_repartition
                        .representative
                        .root_action_descriptor),
                proof.hidden_repartition
                    .representative.identity,
                baseline);
    }
    for (RootEvidence& root : evidence.roots) {
        for (RootActionEvidence& action : root.actions) {
            for (ScopeEvidence& scope : action.scopes) {
                scope.complete =
                    std::all_of(
                        scope.groups.begin(),
                        scope.groups.end(),
                        [](const SuccessorGroupEvidence&
                               group) {
                            return group.complete;
                        });
            }
            action.complete =
                std::all_of(
                    action.scopes.begin(),
                    action.scopes.end(),
                    [](const ScopeEvidence& scope) {
                        return scope.complete;
                    });
        }
    }

    for (const FeatureScopeProof& proof :
         proofs.feature_scopes) {
        SuccessorFeatureEvaluationEvidence& evaluation =
            evidence.successor_feature_evaluations.at(
                proof.evaluation);
        SuccessorFeatureScopeEvidence& scope =
            evaluation.scopes.at(proof.scope);
        const std::string coordinate =
            evaluation.root_stable_id + "/" +
            evaluation.information_set_fingerprint +
            "/feature-scope/" +
            std::to_string(proof.scope);
        require(
            proof.hidden_repartition
                        .root_action_descriptor ==
                    evaluation
                        .representative_root_action_descriptor &&
                proof.hidden_repartition.root_world ==
                    evaluation.representative_root_world &&
                proof.hidden_repartition.identity
                    .bit_identical() &&
                proof.hidden_eligible ==
                    proof.hidden_changed,
            coordinate +
                ": feature-scope hidden proof changed its "
                "canonical representative");
        scope.representative_catalog.reserve(
            proof.representatives.size());
        for (const auto& representative :
             proof.representatives) {
            scope.representative_catalog.push_back({
                .root_world =
                    representative.root_world,
                .root_action_descriptor =
                    representative
                        .root_action_descriptor,
            });
        }
        scope.every_representative_reconstructs = true;
        scope.hidden_repartition_eligible =
            proof.hidden_eligible;
        scope.hidden_identity_changed =
            proof.hidden_changed;
        scope.hidden_repartition_invariant = true;
        scope.complete = true;
        const fq0_bellman::CrossFitValue cross_fit =
            recompute_cross_fit(
                scope.bank_a, scope.bank_b);
        const std::string baseline =
            binding::successor_bank_pair_payload_sha256(
                scope.bank_a, scope.bank_b, cross_fit);
        scope.representative_reconstruction_witnesses
            .reserve(proof.representatives.size());
        for (const auto& representative :
             proof.representatives) {
            const std::string representative_coordinate =
                coordinate + "/member/" +
                std::to_string(
                    representative.root_world) +
                "/" +
                representative.root_action_descriptor;
            scope.representative_reconstruction_witnesses
                .push_back(
                    map_reconstruction_identity(
                        "feature-scope-representative",
                        representative_coordinate,
                        representative.identity,
                        baseline));
        }
        scope.hidden_repartition_witness =
            map_reconstruction_identity(
                "feature-scope-hidden",
                binding::hidden_repartition_coordinate(
                    coordinate,
                    proof.hidden_repartition.root_world,
                    proof.hidden_repartition
                        .root_action_descriptor),
                proof.hidden_repartition.identity,
                baseline);
    }
    for (SuccessorFeatureEvaluationEvidence& evaluation :
         evidence.successor_feature_evaluations) {
        evaluation.complete =
            std::all_of(
                evaluation.scopes.begin(),
                evaluation.scopes.end(),
                [](const SuccessorFeatureScopeEvidence&
                       scope) {
                        return scope.complete;
                    });
    }

    validate_reconstruction_bindings(evidence);
}

void validate_reconstruction_bindings(
    const ScientificEvidence& evidence) {
    const auto require_bound =
        [](const BitIdentityEvidence& witness,
           std::string_view domain,
           std::string_view coordinate,
           std::string_view digest) {
            require(
                witness.domain == domain &&
                    witness.coordinate == coordinate &&
                    witness.baseline_sha256 == digest &&
                    witness.comparison_sha256 == digest,
                std::string(coordinate) +
                    ": reconstruction witness was hashed "
                    "before its payload was finalized");
        };
    for (std::size_t root_index = 0;
         root_index < evidence.roots.size();
         ++root_index) {
        const RootEvidence& root =
            evidence.roots[root_index];
        for (std::size_t action_index = 0;
             action_index < root.actions.size();
             ++action_index) {
            const RootActionEvidence& action =
                root.actions[action_index];
            for (std::size_t scope_index = 0;
                 scope_index < action.scopes.size();
                 ++scope_index) {
                const ScopeEvidence& scope =
                    action.scopes[scope_index];
                for (std::size_t group_index = 0;
                     group_index < scope.groups.size();
                     ++group_index) {
                    const SuccessorGroupEvidence& group =
                        scope.groups[group_index];
                    const std::string coordinate =
                        root_action_coordinate(
                            root.stable_id, action_index,
                            scope_index, group_index);
                    const std::string baseline =
                        binding::
                            successor_bank_pair_payload_sha256(
                                group.bank_a,
                                group.bank_b,
                                group.cross_fit);
                    require_bound(
                        group.hidden_repartition_witness,
                        "group-hidden",
                        binding::
                            hidden_repartition_coordinate(
                                coordinate,
                                group
                                    .representative_root_world,
                                group
                                    .representative_root_action_descriptor),
                        baseline);
                    require(
                        group
                                .representative_reconstruction_witnesses
                                .size() ==
                            group.root_world_indices.size(),
                        coordinate +
                            ": reconstruction member "
                            "witness census drifted");
                    for (std::size_t member = 0;
                         member <
                         group.root_world_indices.size();
                         ++member) {
                        const std::size_t world =
                            group.root_world_indices[member];
                        require_bound(
                            group
                                .representative_reconstruction_witnesses
                                    [member],
                            "group-representative",
                            coordinate + "/member/" +
                                std::to_string(world),
                            baseline);
                    }
                }
            }
        }
    }
    for (std::size_t evaluation_index = 0;
         evaluation_index <
         evidence.successor_feature_evaluations.size();
         ++evaluation_index) {
        const auto& evaluation =
            evidence.successor_feature_evaluations
                [evaluation_index];
        for (std::size_t scope_index = 0;
             scope_index < evaluation.scopes.size();
             ++scope_index) {
            const auto& scope =
                evaluation.scopes[scope_index];
            const std::string coordinate =
                evaluation.root_stable_id + "/" +
                evaluation
                    .information_set_fingerprint +
                "/feature-scope/" +
                std::to_string(scope_index);
            const std::string baseline =
                binding::
                    successor_bank_pair_payload_sha256(
                        scope.bank_a, scope.bank_b,
                        recompute_cross_fit(
                            scope.bank_a,
                            scope.bank_b));
            require_bound(
                scope.hidden_repartition_witness,
                "feature-scope-hidden",
                binding::hidden_repartition_coordinate(
                    coordinate,
                    evaluation.representative_root_world,
                    evaluation
                        .representative_root_action_descriptor),
                baseline);
            require(
                scope.representative_reconstruction_witnesses
                        .size() ==
                    scope.representative_catalog.size(),
                coordinate +
                    ": feature reconstruction member "
                    "witness census drifted");
            for (std::size_t member = 0;
                 member <
                 scope.representative_catalog.size();
                 ++member) {
                const auto& representative =
                    scope.representative_catalog[member];
                require_bound(
                    scope
                        .representative_reconstruction_witnesses
                            [member],
                    "feature-scope-representative",
                    coordinate + "/member/" +
                        std::to_string(
                            representative.root_world) +
                        "/" +
                        representative
                            .root_action_descriptor,
                    baseline);
            }
        }
    }
}

void attach_root_and_global_invariance(
    ScientificEvidence& primary,
    const ac1_teacher_audit::Manifest& repeated_manifest,
    std::shared_ptr<const LearnedModel> model) {
    const std::string primary_core =
        binding::scientific_core_payload_sha256(primary);
    primary.primary_core_sha256 = primary_core;

    const std::string primary_manifest =
        binding::manifest_payload_sha256(primary.manifest);
    const std::string comparison_manifest =
        binding::manifest_payload_sha256(
            repeated_manifest);
    primary.invariance.independent_manifest_witness =
        binding::make_witness(
            "manifest", "global", primary_manifest,
            comparison_manifest);

    const auto compare_core =
        [&](science::Construction alternate,
            std::string domain) {
            ScientificEvidence comparison =
                take_construction_core(
                    std::move(alternate),
                    primary.manifest);
            return binding::make_witness(
                std::move(domain), "global",
                primary_core,
                binding::scientific_core_payload_sha256(
                    comparison));
        };

    {
        science::Construction repeated =
            science::construct_production(
                repeated_manifest, model);
        require_execution(
            repeated.execution,
            science::kProductionWorkers,
            "repeat-construction");
        primary.invariance
            .repeated_construction_witness =
            compare_core(
                std::move(repeated), "core-repeat");
    }

    {
        science::Construction descriptor =
            science::
                construct_production_descriptor_order_invariance(
                    model);
        require_execution(
            descriptor.execution,
            science::kProductionWorkers,
            "descriptor-order-construction");
        ScientificEvidence descriptor_evidence =
            take_construction_core(
                std::move(descriptor),
                primary.manifest);
        for (RootEvidence& root : primary.roots) {
            const std::string baseline =
                binding::root_payload_sha256(root);
            const std::string comparison =
                binding::root_payload_sha256(
                    evidence_root_for(
                        descriptor_evidence,
                        root.stable_id));
            root.descriptor_order_witness =
                binding::make_witness(
                    "root-order", root.stable_id,
                    baseline, comparison);
            root.descriptor_order_bit_identical =
                baseline == comparison;
        }
        primary.invariance.descriptor_order_witness =
            binding::make_witness(
                "core-order", "global", primary_core,
                binding::scientific_core_payload_sha256(
                    descriptor_evidence));
    }

    {
        science::Construction single_worker =
            science::
                construct_production_single_worker_invariance(
                    primary.manifest, model);
        require_execution(
            single_worker.execution, 1,
            "single-worker-construction");
        primary.invariance.thread_count_witness =
            compare_core(
                std::move(single_worker), "core-thread");
    }

    {
        science::Construction hidden =
            science::
                construct_production_hidden_repartition_invariance(
                    model);
        require_execution(
            hidden.execution,
            science::kProductionWorkers,
            "hidden-repartition-construction");
        ScientificEvidence hidden_evidence =
            take_construction_core(
                std::move(hidden),
                primary.manifest);
        for (RootEvidence& root : primary.roots) {
            const std::string baseline =
                binding::root_payload_sha256(root);
            const std::string comparison =
                binding::root_payload_sha256(
                    evidence_root_for(
                        hidden_evidence, root.stable_id));
            root.hidden_repartition_witness =
                binding::make_witness(
                    "root-hidden", root.stable_id,
                    baseline, comparison);
            root.hidden_repartition_bit_identical =
                baseline == comparison;
            root.complete =
                root.descriptor_order_bit_identical &&
                root.hidden_repartition_bit_identical &&
                std::all_of(
                    root.actions.begin(), root.actions.end(),
                    [](const RootActionEvidence& action) {
                        return action.complete;
                    });
        }
        primary.invariance.hidden_repartition_witness =
            binding::make_witness(
                "core-hidden", "global", primary_core,
                binding::scientific_core_payload_sha256(
                    hidden_evidence));
    }

    const std::array<std::string, 2> critic =
        binding::critic_stream_payload_sha256(primary);
    primary.invariance.contextual_legacy_critic_witness =
        binding::make_witness(
            "critic-context-vs-legacy", "global",
            critic[0], critic[1]);

    const auto passed =
        [](const BitIdentityEvidence& witness) {
            return !witness.baseline_sha256.empty() &&
                   witness.baseline_sha256 ==
                       witness.comparison_sha256;
        };
    primary.invariance
        .independent_manifest_bit_identical =
        passed(primary.invariance
                   .independent_manifest_witness);
    primary.invariance
        .repeated_construction_bit_identical =
        passed(primary.invariance
                   .repeated_construction_witness);
    primary.invariance.descriptor_order_bit_identical =
        passed(primary.invariance
                   .descriptor_order_witness);
    primary.invariance.thread_count_bit_identical =
        passed(primary.invariance
                   .thread_count_witness);
    primary.invariance.hidden_repartition_bit_identical =
        passed(primary.invariance
                   .hidden_repartition_witness);
    primary.invariance
        .contextual_legacy_critic_bit_identical =
        passed(primary.invariance
                   .contextual_legacy_critic_witness);
    primary.invariance.passed =
        primary.invariance
            .independent_manifest_bit_identical &&
        primary.invariance
            .repeated_construction_bit_identical &&
        primary.invariance
            .descriptor_order_bit_identical &&
        primary.invariance.thread_count_bit_identical &&
        primary.invariance
            .hidden_repartition_bit_identical &&
        primary.invariance
            .contextual_legacy_critic_bit_identical;
}

void finalize_scientific_verdict(
    ScientificEvidence& evidence) {
    const auto primary_contrast = std::find_if(
        evidence.contrasts.begin(),
        evidence.contrasts.end(),
        [](const ContrastEvidence& contrast) {
            return contrast.role == GateRole::Primary;
        });
    const auto primary_dominance = std::find_if(
        evidence.dominance_pairs.begin(),
        evidence.dominance_pairs.end(),
        [](const DominancePairEvidence& pair) {
            return pair.role == GateRole::Primary;
        });
    require(
        primary_contrast != evidence.contrasts.end() &&
            primary_dominance !=
                evidence.dominance_pairs.end(),
        "FQ0 primary contrast or dominance row is missing");

    bool contrast_guards = true;
    for (const ContrastEvidence& contrast :
         evidence.contrasts) {
        if (contrast.role != GateRole::Primary &&
            contrast.role != GateRole::Descriptive) {
            contrast_guards =
                contrast_guards &&
                contrast.directional_passed;
        }
    }
    bool dominance_guards = true;
    for (const DominancePairEvidence& pair :
         evidence.dominance_pairs) {
        if (pair.role ==
                GateRole::DominanceConsistencyGuard ||
            pair.role == GateRole::XZeroGuard ||
            pair.role ==
                GateRole::IncomparableControlGuard) {
            dominance_guards =
                dominance_guards && pair.passed;
        }
    }
    evidence.primary_passed =
        primary_contrast->directional_passed &&
        primary_dominance->passed;
    evidence.reject_only_guards_passed =
        contrast_guards && dominance_guards;
    evidence.passed =
        evidence.primary_passed &&
        evidence.reject_only_guards_passed &&
        evidence.feature_collisions.passed;

    const bool roots_complete =
        !evidence.roots.empty() &&
        std::all_of(
            evidence.roots.begin(),
            evidence.roots.end(),
            [](const RootEvidence& root) {
                return root.complete;
            });
    const bool feature_scopes_complete =
        !evidence.successor_feature_evaluations.empty() &&
        std::all_of(
            evidence
                .successor_feature_evaluations.begin(),
            evidence
                .successor_feature_evaluations.end(),
            [](const SuccessorFeatureEvaluationEvidence&
                   evaluation) {
                return evaluation.complete;
            });
    const bool dominance_complete =
        !evidence.dominance_pairs.empty() &&
        std::all_of(
            evidence.dominance_pairs.begin(),
            evidence.dominance_pairs.end(),
            [](const DominancePairEvidence& pair) {
                return pair.required_worlds ==
                           kRootWorlds &&
                       pair.worlds.size() ==
                           kRootWorlds;
            });
    evidence.complete =
        evidence.manifest.exact &&
        evidence.model_fingerprint ==
            kModelFingerprint &&
        !evidence.primary_core_sha256.empty() &&
        roots_complete && feature_scopes_complete &&
        !evidence.contrasts.empty() &&
        dominance_complete &&
        !evidence.feature_rows.empty() &&
        evidence.c16_ranking_changes.complete;
}

std::string format_real(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6) << value;
    return output.str();
}

std::string render_summary_preamble(
    const RunReport& report) {
    std::ostringstream output;
    const auto primary = std::find_if(
        report.scientific.contrasts.begin(),
        report.scientific.contrasts.end(),
        [](const ContrastEvidence& contrast) {
            return contrast.role == GateRole::Primary;
        });
    output
        << "FQ0-T0 "
        << (report.gate.passed ? "PASS" : "REJECT")
        << ": complete frozen Bellman evidence\n"
        << "  roots=" << report.scientific.roots.size()
        << " feature_rows="
        << report.scientific.feature_rows.size()
        << " harmful_collisions="
        << report.scientific.feature_collisions
               .harmful_collisions
        << '\n';
    if (primary != report.scientific.contrasts.end()) {
        output
            << "  primary_delta64="
            << format_real(primary->contrast.delta64)
            << " lower95="
            << format_real(
                   primary->contrast.lower_95)
            << " positive_blocks="
            << primary->contrast.positive_blocks << "/"
            << kBlocks << '\n';
    }
    output
        << "  artifact_sha256="
        << report.integrity.model_after.sha256 << '\n';
    return output.str();
}

void emit_summary_noexcept(
    std::ostream& output, std::string_view preamble,
    const RunReport& report) noexcept {
    try {
        output
            << preamble
            << "  evidence=" << report.publication.path
            << " bytes=" << report.publication.byte_size
            << " sha256=" << report.publication.sha256
            << '\n'
            << "  exit_code=" << exit_code(report.gate)
            << '\n';
    } catch (...) {
        // The evidence is already atomically committed. A reporting stream
        // failure must not reinterpret a complete exit-0/1 result as an
        // infrastructure failure or attempt a second publication.
    }
}

} // namespace

RunReport run(std::ostream& progress) {
    require_evidence_destination_absent();
    progress
        << "FQ0-T0: verifying immutable C16 and frozen "
           "manifest...\n";

    RunReport report;
    report.integrity.model_before =
        artifact_integrity::snapshot_regular_file(
            std::string(kModelArtifactPath));
    report.integrity.artifact_requirement_matched =
        snapshot_matches_requirement(
            report.integrity.model_before);
    require(
        report.integrity.artifact_requirement_matched,
        "FQ0 immutable model artifact requirement failed");

    const std::shared_ptr<const LearnedModel> model =
        load_frozen_model();
    report.integrity.model_components_before =
        learned_model_component_fingerprints(model);
    const ac1_teacher_audit::Manifest manifest =
        ac1_teacher_audit::build_manifest();
    const ac1_teacher_audit::Manifest repeated_manifest =
        ac1_teacher_audit::build_manifest();
    require(
        manifest == repeated_manifest,
        "FQ0 independent manifest reconstruction drifted");

    progress
        << "FQ0-T0: constructing K64/K8 Bellman core...\n";
    science::Construction construction =
        science::construct_production(manifest, model);
    require_execution(
        construction.execution,
        science::kProductionWorkers,
        "primary-construction");
    require(
        construction.model_fingerprint ==
                kModelFingerprint &&
            construction.manifest == manifest,
        "FQ0 primary construction identity drifted");

    progress
        << "FQ0-T0: reconstructing every successor "
           "information set...\n";
    const ReconstructionProofs reconstruction =
        reconstruct_all_information_sets(
            construction, manifest, model);

    progress
        << "FQ0-T0: evaluating exhaustive cached "
           "next-boundary dominance...\n";
    std::vector<DominancePairEvidence> dominance =
        build_exhaustive_dominance(
            construction, manifest);

    report.scientific =
        take_construction_core(
            std::move(construction), manifest);
    report.scientific.dominance_pairs =
        std::move(dominance);
    attach_reconstruction_proofs(
        report.scientific, reconstruction);
    build_contrasts(report.scientific);
    report.scientific.c16_ranking_changes =
        rank_against_c16(report.scientific, model);
    attach_root_and_global_invariance(
        report.scientific, repeated_manifest, model);
    finalize_scientific_verdict(report.scientific);

    report.integrity.model_after =
        artifact_integrity::snapshot_regular_file(
            std::string(kModelArtifactPath));
    report.integrity.model_components_after =
        learned_model_component_fingerprints(model);
    report.integrity.artifact_unchanged =
        report.integrity.model_before ==
        report.integrity.model_after;
    report.integrity.model_identity_matched =
        learned_model_fingerprint(model) ==
            kModelFingerprint;
    report.integrity.passed =
        report.integrity.artifact_requirement_matched &&
        report.integrity.artifact_unchanged &&
        report.integrity.model_identity_matched &&
        report.integrity.model_components_before ==
            report.integrity.model_components_after;
    report.gate =
        evaluate_gate(report.scientific, report.integrity);
    if (report.integrity.passed &&
        report.scientific.complete &&
        !report.gate.infrastructure_failure) {
        const std::string summary_preamble =
            render_summary_preamble(report);
        report.publication =
            publish_evidence_atomic_no_replace(report);
        emit_summary_noexcept(
            progress, summary_preamble, report);
    }
    return report;
}

int run_cli(
    int argc, char*[], std::ostream& output,
    std::ostream& error) {
    if (argc != 1) {
        error
            << "Usage: old-school-fq0-bellman-audit\n"
            << "This sealed command accepts no paths, seeds, "
               "recipes, or gate overrides.\n";
        return 2;
    }
    try {
        return exit_code(run(output).gate);
    } catch (const std::exception& failure) {
        error
            << "FQ0-T0 infrastructure failure: "
            << failure.what() << '\n';
        return 2;
    }
}

namespace testing {

ScientificEvidence take_reduced_construction_core_for_test(
    science::Construction construction,
    const ac1_teacher_audit::Manifest& manifest) {
    return take_construction_core(
        std::move(construction), manifest);
}

std::tuple<ScientificEvidence, std::size_t, std::size_t>
reconstruct_reduced_information_sets_for_test(
    const science::Construction& construction,
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model,
    science::testing::ReducedRecipe recipe) {
    std::size_t callbacks = 0;
    const ReconstructionProofs proofs =
        reconstruct_all_information_sets_impl(
            construction, manifest, recipe.workers,
            [&](const science::Construction&,
                std::size_t, std::size_t, std::size_t,
                const ac1_teacher_audit::ManifestRoot&
                    manifest_root,
                const science::Root& root,
                const science::SuccessorFeatureEvaluation&
                    evaluation,
                const science::SuccessorFeatureScope&
                    scope) {
                ++callbacks;
                return science::testing::
                    reconstruct_feature_scope(
                        manifest_root, root, evaluation,
                        scope, model, recipe);
            });
    require(
        callbacks ==
            proofs.feature_scope_reconstructions,
        "reconstruction callback accounting drifted");
    ScientificEvidence evidence =
        take_construction_core(construction, manifest);
    attach_reconstruction_proofs(evidence, proofs);
    return {
        std::move(evidence),
        proofs.feature_scope_reconstructions,
        proofs.group_occurrences_reused,
    };
}

void validate_reconstruction_bindings_for_test(
    const ScientificEvidence& evidence) {
    validate_reconstruction_bindings(evidence);
}

bool dominance_pair_passes_for_test(
    GateRole role, std::size_t matching_worlds,
    bool monotonic, bool all_settlements_valid) {
    return dominance_pair_passed(
        role, matching_worlds, monotonic,
        all_settlements_valid);
}

} // namespace testing

} // namespace old_school::fq0_bellman_audit
