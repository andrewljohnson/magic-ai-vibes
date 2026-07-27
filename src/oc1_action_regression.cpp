#include "old_school/oc1_action_regression.hpp"

#include "old_school/output_calibration.hpp"
#include "old_school/output_calibration_artifact.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace old_school::oc1_action_regression {
namespace {

namespace action_eval = oc1_action_eval;
namespace action_scoring = oc1_action_scoring;
namespace calibration = output_calibration;

constexpr std::array<DeckId, kDeckCount> kDecks = {
    DeckId::Green,
    DeckId::Red,
    DeckId::Blue,
    DeckId::White,
    DeckId::RUAggro,
};

constexpr std::array<std::string_view, kDeckCount> kDeckNames = {
    "Green",
    "Red",
    "Blue",
    "White",
    "RU Aggro",
};

constexpr std::array<std::string_view, kFocusedFamilyCount>
    kFocusedFamilyNames = {
        "force-spike",
        "counter-composition",
        "braingeyser-x0",
        "disintegrate-x0",
        "field-regressions",
        "attack-regression",
        "dvr2-replays",
    };

constexpr std::string_view kLiveForceSpikeId =
    "control.blue.force-spike-live-gray-ogre.v1";
constexpr std::string_view kPayableForceSpikeId =
    "control.blue.force-spike-payable-gray-ogre.v1";
constexpr std::string_view kRedundantCounterId =
    "control.blue.counter-redundant-same-target.v1";
constexpr std::string_view kInterveningCounterId =
    "control.blue.counter-same-target-after-intervening-counter.v1";
constexpr std::string_view kBraingeyserId =
    "control.blue.braingeyser-x0.v1";
constexpr std::string_view kDisintegrateId =
    "validation.ru.disintegrate-hold-x0.v1";

class CanonicalWriter {
  public:
    void text(std::string_view value) {
        unsigned_integer(value.size());
        bytes_.append(value.data(), value.size());
    }

    void boolean(bool value) {
        unsigned_integer(value ? 1U : 0U);
    }

    template <typename Integer>
    void unsigned_integer(Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        const std::uint64_t widened =
            static_cast<std::uint64_t>(
                static_cast<Unsigned>(value));
        for (std::size_t byte = 0; byte < sizeof(widened);
             ++byte) {
            bytes_.push_back(static_cast<char>(
                (widened >> (8U * byte)) & 0xffU));
        }
    }

    template <typename Enum>
    void enumeration(Enum value) {
        static_assert(std::is_enum_v<Enum>);
        unsigned_integer(
            static_cast<std::underlying_type_t<Enum>>(value));
    }

    void real(double value) {
        unsigned_integer(std::bit_cast<std::uint64_t>(value));
    }

    std::string hash() const {
        return artifact_integrity::sha256_string(bytes_);
    }

  private:
    std::string bytes_;
};

std::size_t deck_index(DeckId deck) {
    const auto found =
        std::find(kDecks.begin(), kDecks.end(), deck);
    if (found == kDecks.end()) {
        throw std::invalid_argument(
            "OC1-AR1 root deck is outside the five-deck environment");
    }
    return static_cast<std::size_t>(
        std::distance(kDecks.begin(), found));
}

void append_strings(
    CanonicalWriter& writer,
    const std::vector<std::string>& values) {
    writer.unsigned_integer(values.size());
    for (const std::string& value : values) {
        writer.text(value);
    }
}

void append_snapshot(
    CanonicalWriter& writer,
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    writer.text(snapshot.path);
    writer.text(snapshot.physical_path);
    writer.unsigned_integer(snapshot.byte_size);
    writer.text(snapshot.sha256);
    writer.unsigned_integer(snapshot.device);
    writer.unsigned_integer(snapshot.inode);
    writer.unsigned_integer(snapshot.link_count);
    writer.unsigned_integer(snapshot.modification_seconds);
    writer.unsigned_integer(snapshot.modification_nanoseconds);
    writer.unsigned_integer(snapshot.change_seconds);
    writer.unsigned_integer(snapshot.change_nanoseconds);
}

void append_hidden_counts(
    CanonicalWriter& writer, const HiddenCounts& counts) {
    writer.unsigned_integer(counts.attempted);
    writer.unsigned_integer(counts.changed);
    writer.unsigned_integer(counts.unchanged);
}

void append_snapshot_set(
    CanonicalWriter& writer,
    const ArtifactSnapshotSet& snapshots) {
    append_snapshot(writer, snapshots.parent);
    append_snapshot(writer, snapshots.candidate);
    append_snapshot(writer, snapshots.actor_cache);
    append_snapshot(writer, snapshots.dvr);
}

void append_hidden_audit(
    CanonicalWriter& writer, const HiddenAudit& hidden) {
    append_hidden_counts(writer, hidden.pooled);
    for (const auto& counts : hidden.balanced_by_deck) {
        append_hidden_counts(writer, counts);
    }
    for (const auto& counts : hidden.focused_by_family) {
        append_hidden_counts(writer, counts);
    }
    writer.boolean(hidden.owner_observations_identical);
    writer.boolean(hidden.typed_actions_identical);
    writer.boolean(
        hidden.information_fingerprints_identical);
    writer.boolean(hidden.raw_scores_identical);
    writer.boolean(hidden.supports_identical);
    writer.boolean(hidden.accounting_identical);
    writer.boolean(hidden.nonvacuous);
    writer.boolean(hidden.passed);
}

void append_cache_metadata(
    CanonicalWriter& writer,
    const probe_runner::ProbeCacheMetadata& metadata) {
    writer.text(metadata.schema);
    writer.text(metadata.algorithm);
    writer.text(metadata.semantic_revision);
    writer.text(metadata.environment_revision);
    writer.text(metadata.corpus_id);
    writer.unsigned_integer(metadata.reference_seed);
    writer.unsigned_integer(metadata.production_policy_seed);
    writer.unsigned_integer(metadata.training_seed);
    writer.unsigned_integer(metadata.training_games);
    writer.unsigned_integer(metadata.worlds);
    writer.unsigned_integer(metadata.horizon_turns);
    writer.unsigned_integer(metadata.rollouts_per_world);
    writer.unsigned_integer(metadata.probe_count);
    writer.text(metadata.reference_model_fingerprint);
    writer.text(metadata.information_set_fingerprint);
}

void append_candidate_samples(
    CanonicalWriter& writer,
    const probe_eval::CandidateSamples& samples) {
    writer.text(samples.key);
    writer.unsigned_integer(samples.q_samples.size());
    for (const double value : samples.q_samples) {
        writer.real(value);
    }
}

void append_reference_root(
    CanonicalWriter& writer,
    const action_eval::ReferenceRoot& root) {
    writer.text(root.stable_id);
    writer.enumeration(root.root_deck);
    writer.unsigned_integer(root.candidates.size());
    for (const auto& candidate : root.candidates) {
        append_candidate_samples(writer, candidate);
    }
}

void append_support(
    CanonicalWriter& writer,
    const action_eval::ActionSupport& support) {
    append_strings(writer, support.actions);
}

void append_paired_estimate(
    CanonicalWriter& writer,
    const action_eval::PairedEstimate& estimate) {
    writer.real(estimate.mean);
    writer.real(estimate.standard_error);
    writer.real(estimate.lower_95);
}

void append_root_metrics(
    CanonicalWriter& writer,
    const action_eval::RootPolicyMetrics& metrics) {
    writer.text(metrics.stable_id);
    writer.enumeration(metrics.root_deck);
    append_support(writer, metrics.support);
    writer.real(metrics.support_mean);
    writer.real(metrics.best_candidate_mean);
    writer.real(metrics.regret);
    writer.real(metrics.top_one_fraction);
}

void append_deck_summary(
    CanonicalWriter& writer,
    const action_eval::DeckPolicySummary& summary) {
    writer.enumeration(summary.root_deck);
    writer.unsigned_integer(summary.root_count);
    writer.real(summary.mean_regret);
    writer.real(summary.mean_top_one_fraction);
}

void append_equal_root_summary(
    CanonicalWriter& writer,
    const action_eval::EqualRootSummary& summary) {
    writer.unsigned_integer(summary.root_count);
    writer.real(summary.mean_regret);
    writer.real(summary.mean_top_one_fraction);
    for (const auto& deck : summary.by_deck) {
        append_deck_summary(writer, deck);
    }
}

void append_equal_root_comparison(
    CanonicalWriter& writer,
    const action_eval::EqualRootComparison& comparison) {
    append_equal_root_summary(writer, comparison.control);
    append_equal_root_summary(writer, comparison.candidate);
    writer.boolean(comparison.pooled_regret_no_worse);
    writer.boolean(comparison.pooled_top_one_no_lower);
    writer.boolean(
        comparison.every_deck_regret_within_allowance);
    writer.boolean(comparison.passed);
}

void append_material_regression(
    CanonicalWriter& writer,
    const action_eval::DualReferenceMaterialRegression& value) {
    append_paired_estimate(
        writer, value.actor_control_minus_candidate);
    append_paired_estimate(
        writer, value.c16_control_minus_candidate);
    writer.boolean(value.material_under_both);
}

void append_recipe(
    CanonicalWriter& writer,
    const action_scoring::AppliedRecipe& recipe) {
    writer.enumeration(recipe.seed_source);
    writer.text(recipe.seed_tag);
    writer.unsigned_integer(recipe.seed_base);
    writer.boolean(recipe.resolved_seed.has_value());
    if (recipe.resolved_seed.has_value()) {
        writer.unsigned_integer(*recipe.resolved_seed);
    }
    writer.unsigned_integer(recipe.worlds);
    writer.unsigned_integer(recipe.horizon_turns);
    writer.unsigned_integer(recipe.rollouts_per_world);
    writer.boolean(recipe.blend_shallow_prior);
    writer.unsigned_integer(recipe.evaluation_threads);
    writer.boolean(recipe.value_mirror);
    writer.real(recipe.value_continuation_epsilon);
    writer.real(recipe.value_priority_residual_weight);
    writer.boolean(recipe.value_pass_dominance);
    writer.enumeration(recipe.value_continuation_controller);
}

void append_accounting(
    CanonicalWriter& writer,
    const action_scoring::EvaluationAccounting& accounting) {
    writer.unsigned_integer(accounting.sampled_worlds);
    writer.unsigned_integer(accounting.rollout_evaluations);
    writer.unsigned_integer(accounting.terminal_evaluations);
    writer.unsigned_integer(accounting.bootstrapped_evaluations);
}

void append_decision_score(
    CanonicalWriter& writer,
    const action_scoring::DecisionScore& score) {
    writer.text(score.stable_id);
    writer.enumeration(score.decision_kind);
    writer.enumeration(score.score_mode);
    append_recipe(writer, score.recipe);
    writer.unsigned_integer(score.actions.size());
    for (const auto& action : score.actions) {
        writer.text(action.descriptor);
        writer.unsigned_integer(action.raw_samples.size());
        for (const double sample : action.raw_samples) {
            writer.real(sample);
        }
        writer.real(action.raw_score);
    }
    append_strings(writer, score.selected_support);
    writer.boolean(score.deterministic_selection);
    append_accounting(writer, score.accounting);
}

void append_bsr_score(
    CanonicalWriter& writer,
    const probes::BsrRootScore& score) {
    writer.text(score.stable_id);
    writer.text(score.information_action_fingerprint);
    writer.unsigned_integer(score.action_count);
    writer.unsigned_integer(score.actual_action_index);
    writer.text(score.actual_action_descriptor);
    writer.text(score.reference_model_fingerprint);
    writer.unsigned_integer(score.reference_seed_base);
    writer.unsigned_integer(score.scout_seed);
    writer.unsigned_integer(score.confirmation_seed);
    writer.unsigned_integer(score.scout_worlds);
    writer.unsigned_integer(score.confirmation_worlds);
    writer.unsigned_integer(score.horizon_turns);
    writer.unsigned_integer(score.rollouts_per_world);
    writer.unsigned_integer(score.evaluation_threads);
    append_strings(writer, score.scout_best_actions);
    append_strings(writer, score.confirmation_best_actions);
    writer.unsigned_integer(score.action_means.size());
    for (const auto& action : score.action_means) {
        writer.text(action.descriptor);
        writer.real(action.scout_mean);
        writer.real(action.confirmation_mean);
    }
    writer.real(score.scout_actual_mean);
    writer.real(score.scout_best_mean);
    writer.real(score.confirmation_actual_mean);
    writer.real(score.confirmation_best_mean);
    writer.real(score.confirmation_regret);
    writer.real(score.paired_standard_error);
    writer.real(score.paired_lower_95);
    writer.unsigned_integer(score.sampled_worlds);
    writer.unsigned_integer(score.rollout_evaluations);
    writer.unsigned_integer(score.terminal_evaluations);
    writer.unsigned_integer(score.bootstrapped_evaluations);
    writer.boolean(score.scout_confirmation_best_set_stable);
    writer.boolean(score.actual_outside_best_sets);
    writer.boolean(score.diagnostic_stable_mistake);
    writer.boolean(score.practical_high_cost_mistake);
    writer.boolean(score.descriptor_order_invariant);
    writer.boolean(score.hidden_repartition_eligible);
    writer.boolean(score.hidden_repartition_bit_identical);
    writer.boolean(score.accounting_passed);
}

std::string bsr_score_hash(const probes::BsrRootScore& score) {
    CanonicalWriter writer;
    append_bsr_score(writer, score);
    return writer.hash();
}

void append_balanced_decision(
    CanonicalWriter& writer,
    const BalancedDecisionEvidence& decision) {
    writer.text(decision.stable_id);
    writer.enumeration(decision.root_deck);
    writer.text(decision.information_action_fingerprint);
    append_reference_root(writer, decision.actor_reference);
    append_support(writer, decision.actor_best_set);
    append_reference_root(writer, decision.c16_reference);
    append_support(writer, decision.c16_best_set);
    append_decision_score(writer, decision.c16_deployment);
    append_decision_score(writer, decision.candidate_deployment);
    append_root_metrics(writer, decision.actor_c16_metrics);
    append_root_metrics(
        writer, decision.actor_candidate_metrics);
    append_root_metrics(writer, decision.c16_c16_metrics);
    append_root_metrics(
        writer, decision.c16_candidate_metrics);
    append_support(writer, decision.joint_robust_best_set);
    append_material_regression(
        writer, decision.material_regression);
    writer.boolean(decision.joint_robust_stable);
    writer.boolean(decision.c16_agrees_with_joint_set);
    writer.boolean(
        decision.candidate_preserves_joint_agreement);
    writer.boolean(decision.reference_sign_reversal);
    writer.boolean(decision.descriptor_order_invariant);
}

void append_balanced_report(
    CanonicalWriter& writer,
    const BalancedReport& report) {
    std::vector<const BalancedDecisionEvidence*> decisions;
    decisions.reserve(report.decisions.size());
    for (const auto& decision : report.decisions) {
        decisions.push_back(&decision);
    }
    std::sort(
        decisions.begin(), decisions.end(),
        [](const auto* left, const auto* right) {
            return left->stable_id < right->stable_id;
        });
    writer.unsigned_integer(decisions.size());
    for (const auto* decision : decisions) {
        append_balanced_decision(writer, *decision);
    }
    append_equal_root_comparison(writer, report.actor_metrics);
    for (const std::size_t count :
         report.joint_stable_roots_by_deck) {
        writer.unsigned_integer(count);
    }
    for (const std::size_t count : report.roots_by_deck) {
        writer.unsigned_integer(count);
    }
    writer.unsigned_integer(report.c16_stable_agreements);
    writer.unsigned_integer(report.lost_stable_agreements);
    writer.unsigned_integer(report.changed_support_roots);
    writer.unsigned_integer(report.material_regressions);
    writer.unsigned_integer(report.reference_sign_reversals);
    writer.unsigned_integer(report.growth_timing_roots);
    writer.unsigned_integer(
        report.lost_growth_stable_agreements);
    writer.boolean(report.exact_root_census);
    writer.boolean(report.all_decks_have_joint_stable_root);
    writer.boolean(report.stable_agreements_preserved);
    writer.boolean(
        report.growth_stable_agreements_preserved);
    writer.boolean(report.no_material_regression);
    writer.boolean(report.passed);
}

void append_focused_decision(
    CanonicalWriter& writer,
    const FocusedDecision& decision) {
    writer.text(decision.family);
    writer.text(decision.stable_id);
    writer.enumeration(decision.root_deck);
    writer.text(decision.information_action_fingerprint);
    append_decision_score(writer, decision.scout_reference);
    append_decision_score(
        writer, decision.confirmation_reference);
    append_decision_score(writer, decision.c16_deployment);
    append_decision_score(writer, decision.candidate_deployment);
    append_support(writer, decision.scout_best_set);
    append_support(writer, decision.confirmation_best_set);
    writer.boolean(decision.reference_stable);
    writer.boolean(decision.reference_required);
    writer.boolean(
        decision.parent_reference_agreement_preserved);
    writer.boolean(decision.behavior_contract_passed);
    writer.boolean(decision.descriptor_order_invariant);
    writer.text(decision.disposition);
}

void append_focused_report(
    CanonicalWriter& writer, const FocusedReport& report) {
    std::vector<const FocusedDecision*> decisions;
    decisions.reserve(report.decisions.size());
    for (const auto& decision : report.decisions) {
        decisions.push_back(&decision);
    }
    std::sort(
        decisions.begin(), decisions.end(),
        [](const auto* left, const auto* right) {
            return left->stable_id < right->stable_id;
        });
    writer.unsigned_integer(decisions.size());
    for (const auto* decision : decisions) {
        append_focused_decision(writer, *decision);
    }
    writer.unsigned_integer(report.stable_references);
    writer.unsigned_integer(report.behavior_contracts);
    writer.unsigned_integer(report.failed_contracts);
    writer.unsigned_integer(
        report.descriptive_parent_reference_losses);
    writer.unsigned_integer(
        report.lost_parent_reference_agreements);
    writer.unsigned_integer(
        report.inconclusive_required_references);
    writer.boolean(report.exact_family_census);
    writer.boolean(report.passed);
}

void append_dvr_decision(
    CanonicalWriter& writer, const DvrDecision& decision) {
    writer.text(decision.stable_id);
    writer.text(decision.dvr1_record_fingerprint);
    writer.text(decision.information_action_fingerprint);
    append_bsr_score(writer, decision.stored_reference);
    append_bsr_score(writer, decision.reproduced_reference);
    append_decision_score(
        writer, decision.confirmation_reference);
    append_decision_score(writer, decision.c16_deployment);
    append_decision_score(writer, decision.candidate_deployment);
    append_root_metrics(
        writer, decision.c16_confirmation_metrics);
    append_root_metrics(
        writer, decision.candidate_confirmation_metrics);
    append_paired_estimate(
        writer, decision.c16_minus_candidate);
    writer.boolean(decision.bit_exact_reproduction);
    writer.boolean(decision.material_regression);
}

void append_dvr_report(
    CanonicalWriter& writer, const DvrReport& report) {
    std::vector<const DvrDecision*> decisions;
    decisions.reserve(report.decisions.size());
    for (const auto& decision : report.decisions) {
        decisions.push_back(&decision);
    }
    std::sort(
        decisions.begin(), decisions.end(),
        [](const auto* left, const auto* right) {
            return left->stable_id < right->stable_id;
        });
    writer.unsigned_integer(decisions.size());
    for (const auto* decision : decisions) {
        append_dvr_decision(writer, *decision);
    }
    writer.real(report.c16_total_confirmation_regret);
    writer.real(report.candidate_total_confirmation_regret);
    writer.unsigned_integer(report.bit_exact_reproductions);
    writer.unsigned_integer(report.material_regressions);
    writer.boolean(report.exact_root_census);
    writer.boolean(report.total_regret_no_worse);
    writer.boolean(report.passed);
}

void append_mechanical_consequences(
    CanonicalWriter& writer,
    const std::vector<MechanicalConsequenceRoot>& roots) {
    std::vector<const MechanicalConsequenceRoot*> ordered;
    ordered.reserve(roots.size());
    for (const auto& root : roots) {
        ordered.push_back(&root);
    }
    std::sort(
        ordered.begin(), ordered.end(),
        [](const auto* left, const auto* right) {
            return left->stable_id < right->stable_id;
        });
    writer.unsigned_integer(ordered.size());
    for (const auto* root : ordered) {
        writer.text(root->stable_id);
        append_strings(writer, root->descriptors);
        append_strings(
            writer, root->public_consequence_hashes);
        append_strings(
            writer,
            root->hidden_public_consequence_hashes);
        writer.boolean(root->hidden_identity_changed);
        writer.boolean(root->passed);
    }
}

void append_scientific_evidence(
    CanonicalWriter& writer,
    const ScientificEvidence& evidence) {
    writer.text("old-school-oc1-action-regression-v1");
    writer.text(kParentArtifactPath);
    writer.unsigned_integer(kParentArtifactBytes);
    writer.text(kParentArtifactSha256);
    writer.text(kCandidateArtifactPath);
    writer.unsigned_integer(kCandidateArtifactBytes);
    writer.text(kCandidateArtifactSha256);
    writer.text(kActorCachePath);
    writer.unsigned_integer(kActorCacheBytes);
    writer.text(kActorCacheSha256);
    writer.text(kDvrArtifactPath);
    writer.unsigned_integer(kDvrArtifactBytes);
    writer.text(kDvrArtifactSha256);
    writer.text(action_scoring::kBalancedReferenceTag);
    writer.text(action_scoring::kFocusedScoutTag);
    writer.text(action_scoring::kFocusedConfirmationTag);
    writer.text(action_scoring::kProductionTag);
    writer.unsigned_integer(action_scoring::kReferenceSeedBase);
    writer.unsigned_integer(action_scoring::kProductionSeedBase);
    writer.unsigned_integer(action_scoring::kReferenceWorlds);
    writer.unsigned_integer(
        action_scoring::kReferenceHorizonTurns);
    writer.unsigned_integer(
        action_scoring::kReferenceRolloutsPerWorld);
    writer.boolean(
        action_scoring::kReferenceBlendShallowPrior);
    writer.unsigned_integer(
        action_scoring::kReferenceEvaluationThreads);
    writer.unsigned_integer(action_scoring::kProductionWorlds);
    writer.unsigned_integer(
        action_scoring::kProductionHorizonTurns);
    writer.unsigned_integer(
        action_scoring::kProductionRolloutsPerWorld);
    writer.boolean(
        action_scoring::kProductionBlendShallowPrior);
    writer.unsigned_integer(
        action_scoring::kProductionEvaluationThreads);
    writer.enumeration(action_scoring::kContinuationVariant);
    writer.real(action_scoring::kValueContinuationEpsilon);
    writer.real(
        action_scoring::kValuePriorityResidualWeight);
    writer.boolean(action_scoring::kValuePassDominance);
    writer.enumeration(
        action_scoring::kContinuationController);
    writer.real(action_eval::kNormal95CriticalValue);
    writer.real(action_eval::kRobustMinimumEffect);
    writer.real(action_eval::kPerDeckRegretAllowance);
    writer.text(evidence.parent_model_fingerprint);
    writer.text(evidence.candidate_model_fingerprint);
    append_cache_metadata(
        writer, evidence.actor_cache_metadata);
    writer.text(evidence.actor_cache_contents_hash);
    writer.text(evidence.dvr_bundle_contents_hash);
    append_mechanical_consequences(
        writer, evidence.mechanical_consequences);
    writer.boolean(evidence.mechanical_consequences_passed);
    append_balanced_report(writer, evidence.balanced);
    append_focused_report(writer, evidence.focused);
    append_dvr_report(writer, evidence.dvr);
    writer.boolean(
        evidence.mechanical_consequences_passed &&
        evidence.balanced.passed &&
        evidence.focused.passed &&
        evidence.dvr.passed);
}

void append_gate(
    CanonicalWriter& writer, const GateReport& gate) {
    writer.boolean(gate.infrastructure_failure);
    writer.boolean(gate.integrity_passed);
    writer.boolean(gate.balanced_passed);
    writer.boolean(gate.focused_passed);
    writer.boolean(gate.dvr_passed);
    writer.boolean(gate.passed);
    append_strings(writer, gate.failures);
}

std::string cache_contents_hash(
    const probe_runner::ProbeLabelCacheContents& contents) {
    CanonicalWriter writer;
    append_cache_metadata(writer, contents.metadata);
    writer.unsigned_integer(contents.samples.size());
    for (const auto& root : contents.samples) {
        writer.text(root.stable_id);
        writer.enumeration(root.root_deck);
        writer.unsigned_integer(root.candidates.size());
        for (const auto& candidate : root.candidates) {
            append_candidate_samples(writer, candidate);
        }
    }
    return writer.hash();
}

std::string dvr_bundle_contents_hash(
    const dvr2_replay_bundle::ReplayBundle& bundle) {
    CanonicalWriter writer;
    writer.text(bundle.artifact_path);
    writer.text(bundle.artifact_sha256);
    writer.text(bundle.payload_sha256);
    writer.text(bundle.model_artifact_path);
    writer.text(bundle.model_artifact_sha256);
    writer.text(bundle.model_fingerprint);
    writer.unsigned_integer(bundle.selected_roots);
    writer.unsigned_integer(bundle.stable_disagreements);
    writer.unsigned_integer(bundle.stable_agreements);
    writer.unsigned_integer(bundle.unstable_best_sets);
    writer.unsigned_integer(bundle.invalid_invariance);
    writer.unsigned_integer(bundle.replays.size());
    for (const auto& replay : bundle.replays) {
        writer.unsigned_integer(replay.root_index);
        writer.unsigned_integer(replay.source.seed_base);
        writer.unsigned_integer(replay.source.seed_base_index);
        writer.unsigned_integer(replay.source.schedule_index);
        writer.unsigned_integer(replay.source.pairing_index);
        writer.unsigned_integer(replay.source.game_seed);
        writer.unsigned_integer(replay.source.owner_seat);
        writer.boolean(replay.source.owner_on_play);
        writer.unsigned_integer(replay.source.starting_player);
        writer.unsigned_integer(replay.source.trace_ordinal);
        writer.enumeration(replay.owner_deck);
        writer.enumeration(replay.opponent_deck);
        writer.text(replay.stratum);
        writer.text(replay.information_action_fingerprint);
        writer.text(replay.production_action_descriptor);
        append_bsr_score(writer, replay.reference_score);
        writer.text(replay.serialized_dvr1);
        writer.text(replay.dvr1_record_fingerprint);
        writer.text(
            probes::bsr_information_action_fingerprint(
                replay.probe));
    }
    return writer.hash();
}

void require(
    bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void require_no_errors(
    const std::vector<std::string>& errors,
    std::string_view corpus) {
    if (!errors.empty()) {
        throw std::runtime_error(
            std::string(corpus) + " is invalid: " +
            errors.front());
    }
}

artifact_integrity::RegularFileSnapshot frozen_snapshot(
    const FileRequirement& requirement) {
    const auto snapshot =
        artifact_integrity::snapshot_regular_file(
            requirement.path);
    if (!testing::snapshot_matches_requirement(
            snapshot, requirement)) {
        throw std::runtime_error(
            "frozen input does not match exact byte/SHA/path "
            "requirement: " +
            requirement.path);
    }
    return snapshot;
}

FileRequirement parent_requirement() {
    return {
        .path = std::string(kParentArtifactPath),
        .byte_size = kParentArtifactBytes,
        .sha256 = std::string(kParentArtifactSha256),
    };
}

FileRequirement candidate_requirement() {
    return {
        .path = std::string(kCandidateArtifactPath),
        .byte_size = kCandidateArtifactBytes,
        .sha256 = std::string(kCandidateArtifactSha256),
    };
}

FileRequirement actor_requirement() {
    return {
        .path = std::string(kActorCachePath),
        .byte_size = kActorCacheBytes,
        .sha256 = std::string(kActorCacheSha256),
    };
}

FileRequirement dvr_requirement() {
    return {
        .path = std::string(kDvrArtifactPath),
        .byte_size = kDvrArtifactBytes,
        .sha256 = std::string(kDvrArtifactSha256),
    };
}

ArtifactSnapshotSet frozen_snapshot_set() {
    return {
        .parent = frozen_snapshot(parent_requirement()),
        .candidate =
            frozen_snapshot(candidate_requirement()),
        .actor_cache =
            frozen_snapshot(actor_requirement()),
        .dvr = frozen_snapshot(dvr_requirement()),
    };
}

probe_runner::ProbeCacheMetadata expected_actor_metadata(
    const std::vector<probes::DecisionProbe>& balanced) {
    probe_runner::ProbeScoreConfig config;
    config.training_games = kParentTrainingGames;
    config.training_seed = kParentTrainingSeed;
    config.reference_worlds = action_scoring::kReferenceWorlds;
    config.reference_horizon_turns =
        action_scoring::kReferenceHorizonTurns;
    config.reference_rollouts_per_world =
        action_scoring::kReferenceRolloutsPerWorld;
    const auto metadata =
        probe_runner::make_probe_cache_metadata(
            config, balanced, kActorModelFingerprint);
    require(
        metadata.information_set_fingerprint ==
            kActorInformationFingerprint,
        "balanced corpus information-set fingerprint drifted");
    return metadata;
}

void validate_actor_contents(
    const probe_runner::ProbeLabelCacheContents& contents,
    const probe_runner::ProbeCacheMetadata& expected) {
    require(
        contents.metadata == expected,
        "Actor cache metadata is not exact");
    require(
        contents.samples.size() == kBalancedRootCount,
        "Actor cache root census is not 20");
    std::array<std::size_t, kDeckCount> by_deck{};
    std::size_t candidate_count = 0;
    std::size_t sample_count = 0;
    std::string previous;
    for (const auto& root : contents.samples) {
        require(
            !root.stable_id.empty(),
            "Actor cache contains an empty stable ID");
        require(
            previous.empty() || previous < root.stable_id,
            "Actor cache roots are not in stable-ID order");
        previous = root.stable_id;
        ++by_deck[deck_index(root.root_deck)];
        for (const auto& candidate : root.candidates) {
            require(
                candidate.q_samples.size() ==
                    kActorRowsPerAction,
                "Actor cache action does not contain 64 raw rows");
            ++candidate_count;
            sample_count += candidate.q_samples.size();
            for (const double sample : candidate.q_samples) {
                require(
                    std::isfinite(sample),
                    "Actor cache contains a non-finite raw row");
            }
        }
    }
    require(
        std::all_of(
            by_deck.begin(), by_deck.end(),
            [](std::size_t count) {
                return count == kBalancedRootsPerDeck;
            }),
        "Actor cache is not exactly four roots per deck");
    require(
        candidate_count == kActorCandidateCount &&
            sample_count == kActorRawSampleCount,
        "Actor cache candidate/raw-row census drifted");
}

std::vector<std::string> canonical_descriptors(
    const probes::DecisionProbe& probe) {
    std::vector<std::string> result;
    result.reserve(probe.candidates.size());
    for (const auto& candidate : probe.candidates) {
        result.push_back(candidate.descriptor);
    }
    std::sort(result.begin(), result.end());
    require(
        std::adjacent_find(result.begin(), result.end()) ==
            result.end(),
        probe.stable_id + " has duplicate action descriptors");
    return result;
}

const probe_runner::ProbeReferenceSamples& find_actor_samples(
    const probe_runner::ProbeLabelCacheContents& cache,
    std::string_view stable_id) {
    const auto found = std::lower_bound(
        cache.samples.begin(), cache.samples.end(), stable_id,
        [](const auto& value, std::string_view id) {
            return value.stable_id < id;
        });
    if (found == cache.samples.end() ||
        found->stable_id != stable_id) {
        throw std::runtime_error(
            "Actor cache is missing balanced root " +
            std::string(stable_id));
    }
    return *found;
}

action_eval::ReferenceRoot actor_reference_root(
    const probe_runner::ProbeReferenceSamples& samples) {
    return action_eval::make_reference_root(
        samples.stable_id, samples.root_deck,
        samples.candidates);
}

action_eval::ReferenceRoot score_reference_root(
    const action_scoring::DecisionScore& score,
    DeckId deck) {
    std::vector<probe_eval::CandidateSamples> candidates;
    candidates.reserve(score.actions.size());
    for (const auto& action : score.actions) {
        require(
            !action.raw_samples.empty(),
            score.stable_id +
                " reference action has no common-world rows");
        candidates.push_back({
            .key = action.descriptor,
            .q_samples = action.raw_samples,
        });
    }
    return action_eval::make_reference_root(
        score.stable_id, deck, std::move(candidates));
}

action_eval::ActionSupport support_from_score(
    const action_scoring::DecisionScore& score) {
    return action_eval::make_action_support(
        score.selected_support);
}

action_eval::ActionSupport support_from_strings(
    const std::vector<std::string>& values) {
    return action_eval::make_action_support(values);
}

bool has_reference_sign_reversal(
    const action_eval::ReferenceRoot& actor,
    const action_eval::ReferenceRoot& c16) {
    for (std::size_t first = 0;
         first < actor.candidates.size(); ++first) {
        for (std::size_t second = first + 1;
             second < actor.candidates.size(); ++second) {
            const auto first_support =
                action_eval::make_action_support(
                    {actor.candidates[first].key});
            const auto second_support =
                action_eval::make_action_support(
                    {actor.candidates[second].key});
            const double actor_delta =
                action_eval::paired_support_difference(
                    actor, first_support, second_support)
                    .mean;
            const double c16_delta =
                action_eval::paired_support_difference(
                    c16, first_support, second_support)
                    .mean;
            if ((actor_delta < 0.0 && c16_delta > 0.0) ||
                (actor_delta > 0.0 && c16_delta < 0.0)) {
                return true;
            }
        }
    }
    return false;
}

bool focused_reference_is_stable(
    const action_eval::ReferenceRoot& scout,
    const action_eval::ReferenceRoot& confirmation,
    const action_eval::ActionSupport& scout_best,
    const action_eval::ActionSupport& confirmation_best) {
    if (scout_best != confirmation_best ||
        scout_best.actions.size() >= scout.candidates.size()) {
        return false;
    }
    for (const std::string& best : scout_best.actions) {
        for (const auto& candidate : scout.candidates) {
            if (std::binary_search(
                    scout_best.actions.begin(),
                    scout_best.actions.end(),
                    candidate.key)) {
                continue;
            }
            const auto best_support =
                action_eval::make_action_support({best});
            const auto outside_support =
                action_eval::make_action_support(
                    {candidate.key});
            if (!action_eval::is_robust_positive(
                    action_eval::paired_support_difference(
                        scout, best_support,
                        outside_support)) ||
                !action_eval::is_robust_positive(
                    action_eval::paired_support_difference(
                        confirmation, best_support,
                        outside_support))) {
                return false;
            }
        }
    }
    return true;
}

bool support_equals(
    const action_eval::ActionSupport& support,
    std::initializer_list<std::string_view> expected) {
    std::vector<std::string> values;
    values.reserve(expected.size());
    for (const std::string_view value : expected) {
        values.emplace_back(value);
    }
    return support ==
           action_eval::make_action_support(std::move(values));
}

bool support_contains(
    const action_eval::ActionSupport& support,
    std::string_view descriptor) {
    return std::binary_search(
        support.actions.begin(), support.actions.end(),
        descriptor);
}

const probes::Candidate& candidate_by_descriptor(
    const probes::DecisionProbe& probe,
    std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [descriptor](const probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            probe.stable_id + " is missing selected descriptor " +
            std::string(descriptor));
    }
    return *found;
}

bool excludes_zero_x_actions(
    const probes::DecisionProbe& probe,
    const action_eval::ActionSupport& support,
    PriorityActionKind kind) {
    bool has_positive_x = false;
    for (const auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr || action->kind != kind) {
            continue;
        }
        has_positive_x =
            has_positive_x || action->x_value > 0;
    }
    if (!has_positive_x) {
        return false;
    }
    for (const std::string& descriptor : support.actions) {
        const auto* action = std::get_if<PriorityAction>(
            &candidate_by_descriptor(probe, descriptor).action);
        if (action != nullptr && action->kind == kind &&
            action->x_value == 0) {
            return false;
        }
    }
    return true;
}

bool behavior_contract(
    const probes::DecisionProbe& probe,
    const action_eval::ActionSupport& support) {
    if (probe.stable_id == kLiveForceSpikeId) {
        return support_equals(
            support, {"force-spike-gray-ogre"});
    }
    if (probe.stable_id == kPayableForceSpikeId) {
        return support_equals(support, {"pass"});
    }
    if (probe.stable_id == kRedundantCounterId) {
        return !support_contains(
                   support, "counter-same-air-elemental") &&
               !support_contains(
                   support, "counter-own-counterspell");
    }
    if (probe.stable_id == kInterveningCounterId) {
        return true;
    }
    if (probe.stable_id == kBraingeyserId) {
        return excludes_zero_x_actions(
            probe, support,
            PriorityActionKind::CastBraingeyser);
    }
    if (probe.stable_id == kDisintegrateId) {
        return excludes_zero_x_actions(
            probe, support,
            PriorityActionKind::CastDisintegrate);
    }

    switch (probe.category) {
    case probes::Category::FieldRULife20FlyingMenChumpAir:
        return support_equals(support, {"no-blocks"});
    case probes::Category::FieldRULife4FlyingMenChumpAir:
        return support_equals(
            support,
            {"block-air-elemental-with-flying-men"});
    case probes::Category::FieldGreenSecondMainSickBearGrowth:
        return support_equals(support, {"pass"});
    case probes::Category::FieldGreenBeginCombatGrowthTappedAir:
        return !support_contains(
            support,
            "growth-opponent-tapped-air-elemental");
    case probes::Category::FieldGreenAttackAfterGrowthTappedAir:
        return support_equals(
            support, {"include-ironroot-treefolk"});
    case probes::Category::
        FieldGreenAttackAfterGrowthUntappedAirControl:
        return support_equals(
            support, {"skip-ironroot-treefolk"});
    case probes::Category::
        DiagnosticRUAttackFlyingIntoLargerFlyingBlocker:
        return support_equals(support, {"no-attack"});
    default:
        throw std::runtime_error(
            probe.stable_id +
            " has no frozen OC1-AR1 behavior contract");
    }
}

bool is_growth_timing_root(
    const probes::DecisionProbe& probe) {
    return probe.category ==
               probes::Category::GreenGrowthSaveBolt ||
           probe.category ==
               probes::Category::GreenGrowthPushCombat ||
           probe.category ==
               probes::Category::GreenGrowthHold;
}

std::size_t focused_family_index(
    std::string_view family) {
    const auto found = std::find(
        kFocusedFamilyNames.begin(),
        kFocusedFamilyNames.end(), family);
    if (found == kFocusedFamilyNames.end()) {
        throw std::logic_error(
            "unknown OC1-AR1 focused family");
    }
    return static_cast<std::size_t>(
        std::distance(kFocusedFamilyNames.begin(), found));
}

struct AuthoredProbe {
    std::string family;
    probes::DecisionProbe probe;
};

struct FrozenInputs {
    std::vector<probes::DecisionProbe> balanced;
    std::vector<AuthoredProbe> focused;
    probe_runner::ProbeLabelCacheContents actor_cache;
    dvr2_replay_bundle::ReplayBundle dvr;
    std::string actor_cache_hash;
    std::string dvr_bundle_hash;
    std::vector<MechanicalConsequenceRoot>
        mechanical_consequences;
};

struct HiddenInputs {
    std::map<std::string, probes::DecisionProbe> probes;
    HiddenAudit audit;
};

std::array<std::size_t, kCardCount> card_counts(
    const std::vector<CardId>& cards) {
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : cards) {
        ++counts[static_cast<std::size_t>(card)];
    }
    return counts;
}

bool opponent_partition_identity_changed(
    const GameState& original, const GameState& exchanged,
    std::size_t observer) {
    if (observer >= original.players.size() ||
        original.players.size() != exchanged.players.size() ||
        original.players.size() != 2) {
        return false;
    }
    const std::size_t opponent = 1 - observer;
    const auto& first = original.players[opponent];
    const auto& second = exchanged.players[opponent];
    return first.hand.size() == second.hand.size() &&
           first.library.size() == second.library.size() &&
           card_counts(first.hand) != card_counts(second.hand);
}

std::size_t candidate_index(
    const probes::DecisionProbe& probe,
    std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [descriptor](const probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            probe.stable_id +
            " is missing frozen consequence descriptor " +
            std::string(descriptor));
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

MechanicalConsequenceRoot check_mechanical_consequence_root(
    const probes::DecisionProbe& probe,
    const std::vector<std::string>& descriptors,
    const std::vector<std::string>& expected_hashes) {
    require(
        descriptors.size() == expected_hashes.size() &&
            descriptors.size() == probe.candidates.size(),
        probe.stable_id +
            " mechanical consequence schema is incomplete");
    const GameState world = sample_determinization(
        probe.state, probe.original_decks,
        probe.root_player, 577215);
    const calibration::HiddenExchange exchange =
        calibration::exchange_opponent_hidden_identity(
            world, probe.root_player);
    require(
        exchange.changed &&
            opponent_partition_identity_changed(
                world, exchange.state, probe.root_player),
        probe.stable_id +
            " mechanical consequence hidden exchange is vacuous");

    MechanicalConsequenceRoot evidence{
        .stable_id = probe.stable_id,
        .descriptors = descriptors,
        .hidden_identity_changed =
            exchange.changed &&
            opponent_partition_identity_changed(
                world, exchange.state, probe.root_player),
    };
    evidence.public_consequence_hashes.reserve(
        descriptors.size());
    evidence.hidden_public_consequence_hashes.reserve(
        descriptors.size());
    for (const std::string& descriptor : descriptors) {
        const std::size_t index =
            candidate_index(probe, descriptor);
        const auto original_settlement =
            probes::settle_dc1_priority_candidate(
                probe, world, index);
        const auto hidden_settlement =
            probes::settle_dc1_priority_candidate(
                probe, exchange.state, index);
        evidence.public_consequence_hashes.push_back(
            probes::dc1_public_consequence_fingerprint(
                probe, original_settlement));
        evidence.hidden_public_consequence_hashes.push_back(
            probes::dc1_public_consequence_fingerprint(
                probe, hidden_settlement));
    }
    evidence.passed =
        evidence.public_consequence_hashes ==
            expected_hashes &&
        evidence.hidden_public_consequence_hashes ==
            expected_hashes;
    require(
        evidence.passed,
        probe.stable_id +
            " frozen public consequence hashes drifted");
    return evidence;
}

std::vector<MechanicalConsequenceRoot>
check_mechanical_consequences(
    const std::vector<AuthoredProbe>& focused) {
    const auto find_probe =
        [&focused](std::string_view stable_id)
            -> const probes::DecisionProbe& {
            const auto found = std::find_if(
                focused.begin(), focused.end(),
                [stable_id](const AuthoredProbe& value) {
                    return value.probe.stable_id == stable_id;
                });
            if (found == focused.end()) {
                throw std::runtime_error(
                    "missing mechanical consequence root " +
                    std::string(stable_id));
            }
            return found->probe;
        };

    std::vector<MechanicalConsequenceRoot> result;
    result.push_back(check_mechanical_consequence_root(
        find_probe(kRedundantCounterId),
        {
            "pass",
            "counter-same-air-elemental",
            "counter-own-counterspell",
        },
        {
            "324576b473223a86",
            "a0a47325a3afed35",
            "5d5911104c4d6fdf",
        }));
    result.push_back(check_mechanical_consequence_root(
        find_probe(kInterveningCounterId),
        {
            "pass",
            "counter-same-air-elemental",
            "counter-own-counterspell",
            "counter-opponent-counterspell",
        },
        {
            "aa6ccab048f0ab1a",
            "8447ddf641a58a45",
            "3184c58f7888fb6b",
            "8447ddf641a58a45",
        }));
    result.push_back(check_mechanical_consequence_root(
        find_probe(kBraingeyserId),
        {
            "pass",
            "braingeyser-x0-self",
            "braingeyser-x0-opponent",
            "braingeyser-x1-self",
            "braingeyser-x1-opponent",
        },
        {
            "de125649ad24344a",
            "46d4f822377eea6d",
            "46d4f822377eea6d",
            "1a8bcf6486c13ae9",
            "2c854eb335381c90",
        }));
    std::sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            return left.stable_id < right.stable_id;
        });
    return result;
}

void note_hidden_probe(
    HiddenInputs& hidden, const probes::DecisionProbe& original,
    std::optional<std::size_t> balanced_deck,
    std::optional<std::size_t> focused_family) {
    probes::DecisionProbe clone = original;
    const calibration::HiddenExchange exchange =
        calibration::exchange_opponent_hidden_identity(
            original.state, original.root_player);
    clone.state = exchange.state;
    const bool changed =
        exchange.changed &&
        opponent_partition_identity_changed(
            original.state, clone.state,
            original.root_player);
    const bool observation_equal =
        observe_game_state(
            original.state, original.root_player) ==
        observe_game_state(clone.state, clone.root_player);
    const bool actions_equal =
        original.candidates == clone.candidates &&
        canonical_descriptors(original) ==
            canonical_descriptors(clone);
    const bool fingerprint_equal =
        probes::bsr_information_action_fingerprint(original) ==
        probes::bsr_information_action_fingerprint(clone);
    const probes::Validation clone_validation =
        probes::validate_probe(clone);
    const bool clone_valid = clone_validation.ok();

    const auto increment =
        [changed](HiddenCounts& counts) {
            ++counts.attempted;
            if (changed) {
                ++counts.changed;
            } else {
                ++counts.unchanged;
            }
        };
    increment(hidden.audit.pooled);
    if (balanced_deck.has_value()) {
        increment(
            hidden.audit
                .balanced_by_deck[*balanced_deck]);
    }
    if (focused_family.has_value()) {
        increment(
            hidden.audit.focused_by_family[
                *focused_family]);
    }
    hidden.audit.owner_observations_identical =
        hidden.audit.owner_observations_identical &&
        observation_equal;
    hidden.audit.typed_actions_identical =
        hidden.audit.typed_actions_identical &&
        actions_equal && clone_valid;
    hidden.audit.information_fingerprints_identical =
        hidden.audit.information_fingerprints_identical &&
        fingerprint_equal;
    const auto [_, inserted] =
        hidden.probes.emplace(
            original.stable_id, std::move(clone));
    require(
        inserted,
        "focused/balanced/DVR stable IDs overlap");
}

HiddenInputs prepare_hidden_inputs(
    const FrozenInputs& inputs) {
    HiddenInputs hidden;
    hidden.audit.owner_observations_identical = true;
    hidden.audit.typed_actions_identical = true;
    hidden.audit.information_fingerprints_identical = true;
    for (const auto& probe : inputs.balanced) {
        note_hidden_probe(
            hidden, probe, deck_index(probe.root_deck),
            std::nullopt);
    }
    for (const auto& authored : inputs.focused) {
        note_hidden_probe(
            hidden, authored.probe, std::nullopt,
            focused_family_index(authored.family));
    }
    for (const auto& replay : inputs.dvr.replays) {
        note_hidden_probe(
            hidden, replay.probe, std::nullopt,
            focused_family_index("dvr2-replays"));
    }
    bool nonvacuous =
        hidden.audit.pooled.attempted ==
            hidden.audit.pooled.changed +
                hidden.audit.pooled.unchanged;
    for (const HiddenCounts& counts :
         hidden.audit.balanced_by_deck) {
        nonvacuous =
            nonvacuous && counts.attempted > 0 &&
            counts.changed > 0 &&
            counts.attempted ==
                counts.changed + counts.unchanged;
    }
    for (const HiddenCounts& counts :
         hidden.audit.focused_by_family) {
        nonvacuous =
            nonvacuous && counts.attempted > 0 &&
            counts.changed > 0 &&
            counts.attempted ==
                counts.changed + counts.unchanged;
    }
    hidden.audit.nonvacuous = nonvacuous;
    return hidden;
}

const probes::DecisionProbe& selected_probe(
    const probes::DecisionProbe& original,
    const HiddenInputs& hidden, bool use_hidden) {
    if (!use_hidden) {
        return original;
    }
    const auto found = hidden.probes.find(original.stable_id);
    if (found == hidden.probes.end()) {
        throw std::logic_error(
            "missing prepared hidden repartition for " +
            original.stable_id);
    }
    return found->second;
}

action_scoring::DecisionScore score_reverse_reference(
    const probes::DecisionProbe& probe,
    const std::shared_ptr<const LearnedModel>& model,
    const action_scoring::SearchRecipe& recipe) {
    probes::DecisionProbe reversed = probe;
    std::reverse(
        reversed.candidates.begin(),
        reversed.candidates.end());
    return action_scoring::score_reference(
        reversed, model, recipe);
}

action_scoring::DecisionScore
score_reverse_reference_with_seed(
    const probes::DecisionProbe& probe,
    const std::shared_ptr<const LearnedModel>& model,
    std::uint64_t seed) {
    probes::DecisionProbe reversed = probe;
    std::reverse(
        reversed.candidates.begin(),
        reversed.candidates.end());
    return action_scoring::score_reference_with_seed(
        reversed, model, seed);
}

bool confirmation_matches_stored_means(
    const action_scoring::DecisionScore& confirmation,
    const probes::BsrRootScore& stored) {
    if (confirmation.actions.size() !=
        stored.action_means.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < confirmation.actions.size(); ++index) {
        if (confirmation.actions[index].descriptor !=
                stored.action_means[index].descriptor ||
            std::bit_cast<std::uint64_t>(
                confirmation.actions[index].raw_score) !=
                std::bit_cast<std::uint64_t>(
                    stored.action_means[index]
                        .confirmation_mean)) {
            return false;
        }
    }
    return true;
}

BalancedReport construct_balanced(
    const FrozenInputs& inputs,
    const HiddenInputs& hidden,
    bool use_hidden,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    BalancedReport report;
    std::vector<action_eval::RootPolicyMetrics> actor_c16;
    std::vector<action_eval::RootPolicyMetrics> actor_candidate;

    for (const probes::DecisionProbe& original :
         inputs.balanced) {
        const probes::DecisionProbe& probe =
            selected_probe(original, hidden, use_hidden);
        const auto& actor_samples =
            find_actor_samples(
                inputs.actor_cache, original.stable_id);
        action_eval::ReferenceRoot actor =
            actor_reference_root(actor_samples);
        const auto actor_label =
            probe_eval::make_probe_label(
                actor_samples.stable_id,
                actor_samples.root_deck,
                actor_samples.candidates);
        const auto actor_best =
            support_from_strings(
                actor_label.reference_best_set);

        const auto c16_reference_score =
            action_scoring::score_reference(
                probe, parent,
                action_scoring::kBalancedReferenceRecipe);
        const auto reversed_reference =
            score_reverse_reference(
                probe, parent,
                action_scoring::kBalancedReferenceRecipe);
        const bool order_invariant =
            action_scoring::bit_identical(
                c16_reference_score, reversed_reference);
        require(
            order_invariant,
            original.stable_id +
                " C16 reference is descriptor-order dependent");
        action_eval::ReferenceRoot c16_reference =
            score_reference_root(
                c16_reference_score, original.root_deck);
        const auto c16_best =
            action_eval::exact_best_set(c16_reference);

        const auto c16_deployment =
            action_scoring::score_production(probe, parent);
        const auto candidate_deployment =
            action_scoring::score_production(
                probe, candidate);
        const auto c16_support =
            support_from_score(c16_deployment);
        const auto candidate_support =
            support_from_score(candidate_deployment);

        const auto actor_c16_metrics =
            action_eval::evaluate_support(
                actor, c16_support, actor_best);
        const auto actor_candidate_metrics =
            action_eval::evaluate_support(
                actor, candidate_support, actor_best);
        const auto c16_c16_metrics =
            action_eval::evaluate_support(
                c16_reference, c16_support, c16_best);
        const auto c16_candidate_metrics =
            action_eval::evaluate_support(
                c16_reference, candidate_support, c16_best);
        const auto joint =
            action_eval::joint_robust_best_set(
                actor, c16_reference);
        const bool stable =
            joint.actions.size() <
            actor.candidates.size();
        const bool parent_agrees =
            stable &&
            action_eval::entire_support_contained(
                c16_support, joint);
        const bool candidate_preserves =
            !parent_agrees ||
            action_eval::entire_support_contained(
                candidate_support, joint);
        const auto material =
            action_eval::material_regression(
                actor, c16_reference, c16_support,
                candidate_support);
        const bool sign_reversal =
            has_reference_sign_reversal(
                actor, c16_reference);

        BalancedDecisionEvidence decision{
            .stable_id = original.stable_id,
            .root_deck = original.root_deck,
            .information_action_fingerprint =
                probes::bsr_information_action_fingerprint(
                    probe),
            .actor_reference = std::move(actor),
            .actor_best_set = actor_best,
            .c16_reference = std::move(c16_reference),
            .c16_best_set = c16_best,
            .c16_deployment = c16_deployment,
            .candidate_deployment = candidate_deployment,
            .actor_c16_metrics = actor_c16_metrics,
            .actor_candidate_metrics =
                actor_candidate_metrics,
            .c16_c16_metrics = c16_c16_metrics,
            .c16_candidate_metrics =
                c16_candidate_metrics,
            .joint_robust_best_set = joint,
            .material_regression = material,
            .joint_robust_stable = stable,
            .c16_agrees_with_joint_set = parent_agrees,
            .candidate_preserves_joint_agreement =
                candidate_preserves,
            .reference_sign_reversal = sign_reversal,
            .descriptor_order_invariant = order_invariant,
        };
        report.decisions.push_back(std::move(decision));
        actor_c16.push_back(actor_c16_metrics);
        actor_candidate.push_back(actor_candidate_metrics);
        const std::size_t deck =
            deck_index(original.root_deck);
        ++report.roots_by_deck[deck];
        if (stable) {
            ++report.joint_stable_roots_by_deck[deck];
        }
        if (parent_agrees) {
            ++report.c16_stable_agreements;
            if (!candidate_preserves) {
                ++report.lost_stable_agreements;
            }
        }
        if (c16_support != candidate_support) {
            ++report.changed_support_roots;
        }
        if (material.material_under_both) {
            ++report.material_regressions;
        }
        if (sign_reversal) {
            ++report.reference_sign_reversals;
        }
        if (is_growth_timing_root(original)) {
            ++report.growth_timing_roots;
            if (parent_agrees && !candidate_preserves) {
                ++report.lost_growth_stable_agreements;
            }
        }
    }

    std::sort(
        report.decisions.begin(), report.decisions.end(),
        [](const auto& left, const auto& right) {
            return left.stable_id < right.stable_id;
        });
    report.actor_metrics =
        action_eval::compare_equal_roots(
            actor_c16, actor_candidate);
    report.exact_root_census =
        report.decisions.size() == kBalancedRootCount &&
        std::all_of(
            report.roots_by_deck.begin(),
            report.roots_by_deck.end(),
            [](std::size_t count) {
                return count == kBalancedRootsPerDeck;
            });
    report.all_decks_have_joint_stable_root =
        std::all_of(
            report.joint_stable_roots_by_deck.begin(),
            report.joint_stable_roots_by_deck.end(),
            [](std::size_t count) { return count > 0; });
    report.stable_agreements_preserved =
        report.lost_stable_agreements == 0;
    report.growth_stable_agreements_preserved =
        report.growth_timing_roots == 3 &&
        report.lost_growth_stable_agreements == 0;
    report.no_material_regression =
        report.material_regressions == 0;
    report.passed =
        report.exact_root_census &&
        report.actor_metrics.passed &&
        report.all_decks_have_joint_stable_root &&
        report.stable_agreements_preserved &&
        report.growth_stable_agreements_preserved &&
        report.no_material_regression;
    return report;
}

FocusedReport construct_focused(
    const FrozenInputs& inputs,
    const HiddenInputs& hidden,
    bool use_hidden,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    FocusedReport report;
    std::array<std::size_t, kFocusedFamilyCount>
        family_counts{};
    for (const AuthoredProbe& authored : inputs.focused) {
        const probes::DecisionProbe& probe =
            selected_probe(
                authored.probe, hidden, use_hidden);
        const auto scout =
            action_scoring::score_reference(
                probe, parent,
                action_scoring::kFocusedScoutRecipe);
        const auto confirmation =
            action_scoring::score_reference(
                probe, parent,
                action_scoring::kFocusedConfirmationRecipe);
        const auto reversed_scout =
            score_reverse_reference(
                probe, parent,
                action_scoring::kFocusedScoutRecipe);
        const auto reversed_confirmation =
            score_reverse_reference(
                probe, parent,
                action_scoring::kFocusedConfirmationRecipe);
        const bool order_invariant =
            action_scoring::bit_identical(
                scout, reversed_scout) &&
            action_scoring::bit_identical(
                confirmation, reversed_confirmation);
        require(
            order_invariant,
            authored.probe.stable_id +
                " focused reference is descriptor-order dependent");

        const auto scout_root =
            score_reference_root(
                scout, authored.probe.root_deck);
        const auto confirmation_root =
            score_reference_root(
                confirmation, authored.probe.root_deck);
        const auto scout_best =
            action_eval::exact_best_set(scout_root);
        const auto confirmation_best =
            action_eval::exact_best_set(
                confirmation_root);
        const bool stable =
            focused_reference_is_stable(
                scout_root, confirmation_root,
                scout_best, confirmation_best);

        const auto c16_deployment =
            action_scoring::score_production(probe, parent);
        const auto candidate_deployment =
            action_scoring::score_production(
                probe, candidate);
        const auto c16_support =
            support_from_score(c16_deployment);
        const auto candidate_support =
            support_from_score(candidate_deployment);
        const bool required =
            authored.probe.stable_id ==
            kInterveningCounterId;
        const bool parent_agrees =
            stable &&
            action_eval::entire_support_contained(
                c16_support, scout_best);
        const bool preserves =
            !parent_agrees ||
            action_eval::entire_support_contained(
                candidate_support, scout_best);
        const bool behavior =
            behavior_contract(
                authored.probe, candidate_support);

        std::string disposition = "contract-pass";
        if (!behavior) {
            disposition = "behavior-contract-failed";
        } else if (required && !stable) {
            disposition = "required-reference-inconclusive";
        } else if (!preserves) {
            disposition =
                "parent-reference-agreement-lost";
        } else if (!stable) {
            disposition = "reference-descriptive-only";
        }

        report.decisions.push_back({
            .family = authored.family,
            .stable_id = authored.probe.stable_id,
            .root_deck = authored.probe.root_deck,
            .information_action_fingerprint =
                probes::bsr_information_action_fingerprint(
                    probe),
            .scout_reference = scout,
            .confirmation_reference = confirmation,
            .c16_deployment = c16_deployment,
            .candidate_deployment =
                candidate_deployment,
            .scout_best_set = scout_best,
            .confirmation_best_set =
                confirmation_best,
            .reference_stable = stable,
            .reference_required = required,
            .parent_reference_agreement_preserved =
                preserves,
            .behavior_contract_passed = behavior,
            .descriptor_order_invariant =
                order_invariant,
            .disposition = std::move(disposition),
        });
        ++family_counts[
            focused_family_index(authored.family)];
        if (stable) {
            ++report.stable_references;
        }
        ++report.behavior_contracts;
        if (!behavior) {
            ++report.failed_contracts;
        }
        if (!preserves) {
            ++report.descriptive_parent_reference_losses;
        }
        if (required && !preserves) {
            ++report.lost_parent_reference_agreements;
        }
        if (required && !stable) {
            ++report.inconclusive_required_references;
        }
    }
    std::sort(
        report.decisions.begin(), report.decisions.end(),
        [](const auto& left, const auto& right) {
            return left.stable_id < right.stable_id;
        });
    report.exact_family_census =
        testing::focused_authored_census_is_exact(
            family_counts, report.decisions.size());
    report.passed =
        report.exact_family_census &&
        report.failed_contracts == 0 &&
        report.lost_parent_reference_agreements == 0 &&
        report.inconclusive_required_references == 0;
    return report;
}

DvrReport construct_dvr(
    const FrozenInputs& inputs,
    const HiddenInputs& hidden,
    bool use_hidden,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    DvrReport report;
    std::vector<action_eval::RootPolicyMetrics> c16_metrics;
    std::vector<action_eval::RootPolicyMetrics>
        candidate_metrics;
    for (const auto& replay : inputs.dvr.replays) {
        const probes::DecisionProbe& probe =
            selected_probe(
                replay.probe, hidden, use_hidden);
        const probes::BsrReferenceConfig config{
            .seed =
                replay.reference_score.reference_seed_base,
            .scout_worlds =
                replay.reference_score.scout_worlds,
            .confirmation_worlds =
                replay.reference_score.confirmation_worlds,
            .horizon_turns =
                replay.reference_score.horizon_turns,
            .rollouts_per_world =
                replay.reference_score.rollouts_per_world,
            .evaluation_threads =
                replay.reference_score.evaluation_threads,
        };
        const auto reproduced =
            probes::score_bsr_priority_probe(
                probe,
                replay.production_action_descriptor,
                parent, config);
        const bool reproduction =
            reproduced == replay.reference_score &&
            bsr_score_hash(reproduced) ==
                bsr_score_hash(replay.reference_score);
        require(
            reproduction,
            replay.probe.stable_id +
                " did not reproduce sealed DVR2 BSR evidence");

        const auto confirmation =
            action_scoring::score_reference_with_seed(
                probe, parent,
                replay.reference_score.confirmation_seed);
        const auto reversed =
            score_reverse_reference_with_seed(
                probe, parent,
                replay.reference_score.confirmation_seed);
        require(
            action_scoring::bit_identical(
                confirmation, reversed),
            replay.probe.stable_id +
                " DVR confirmation is descriptor-order dependent");
        require(
            confirmation_matches_stored_means(
                confirmation, replay.reference_score) &&
                confirmation.selected_support ==
                    replay.reference_score
                        .confirmation_best_actions,
            replay.probe.stable_id +
                " DVR confirmation does not reproduce stored "
                "means/best set");
        const auto reference_root =
            score_reference_root(
                confirmation, replay.owner_deck);
        const auto best =
            action_eval::exact_best_set(reference_root);
        const auto c16_deployment =
            action_scoring::score_production(probe, parent);
        const auto candidate_deployment =
            action_scoring::score_production(
                probe, candidate);
        const auto c16_support =
            support_from_score(c16_deployment);
        const auto candidate_support =
            support_from_score(candidate_deployment);
        const auto c16_metric =
            action_eval::evaluate_support(
                reference_root, c16_support, best);
        const auto candidate_metric =
            action_eval::evaluate_support(
                reference_root, candidate_support, best);
        const auto difference =
            action_eval::paired_support_difference(
                reference_root, c16_support,
                candidate_support);
        const bool material =
            action_eval::is_robust_positive(difference);

        report.decisions.push_back({
            .stable_id = replay.probe.stable_id,
            .dvr1_record_fingerprint =
                replay.dvr1_record_fingerprint,
            .information_action_fingerprint =
                probes::bsr_information_action_fingerprint(
                    probe),
            .stored_reference = replay.reference_score,
            .reproduced_reference = reproduced,
            .confirmation_reference = confirmation,
            .c16_deployment = c16_deployment,
            .candidate_deployment =
                candidate_deployment,
            .c16_confirmation_metrics = c16_metric,
            .candidate_confirmation_metrics =
                candidate_metric,
            .c16_minus_candidate = difference,
            .bit_exact_reproduction = reproduction,
            .material_regression = material,
        });
        c16_metrics.push_back(c16_metric);
        candidate_metrics.push_back(candidate_metric);
        ++report.bit_exact_reproductions;
        if (material) {
            ++report.material_regressions;
        }
    }
    std::sort(
        report.decisions.begin(), report.decisions.end(),
        [](const auto& left, const auto& right) {
            return left.stable_id < right.stable_id;
        });
    report.c16_total_confirmation_regret =
        action_eval::total_regret(c16_metrics);
    report.candidate_total_confirmation_regret =
        action_eval::total_regret(candidate_metrics);
    report.exact_root_census =
        report.decisions.size() == kDvrRootCount &&
        report.bit_exact_reproductions == kDvrRootCount;
    report.total_regret_no_worse =
        action_eval::total_regret_no_worse(
            c16_metrics, candidate_metrics);
    report.passed =
        report.exact_root_census &&
        report.total_regret_no_worse &&
        report.material_regressions == 0;
    return report;
}

ScientificEvidence construct_scientific(
    const FrozenInputs& inputs,
    const HiddenInputs& hidden,
    bool use_hidden,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    ScientificEvidence evidence{
        .parent_model_fingerprint =
            learned_model_fingerprint(parent),
        .candidate_model_fingerprint =
            learned_model_fingerprint(candidate),
        .actor_cache_metadata =
            inputs.actor_cache.metadata,
        .actor_cache_contents_hash =
            inputs.actor_cache_hash,
        .dvr_bundle_contents_hash =
            inputs.dvr_bundle_hash,
        .mechanical_consequences =
            inputs.mechanical_consequences,
        .mechanical_consequences_passed =
            inputs.mechanical_consequences.size() == 3 &&
            std::all_of(
                inputs.mechanical_consequences.begin(),
                inputs.mechanical_consequences.end(),
                [](const auto& root) {
                    return root.passed &&
                           root.hidden_identity_changed;
                }),
    };
    evidence.balanced =
        construct_balanced(
            inputs, hidden, use_hidden, parent, candidate);
    evidence.focused =
        construct_focused(
            inputs, hidden, use_hidden, parent, candidate);
    evidence.dvr =
        construct_dvr(
            inputs, hidden, use_hidden, parent, candidate);
    return evidence;
}

ConstructionBundleReport construct_bundle(
    const FrozenInputs& inputs,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    ConstructionBundleReport bundle;
    bundle.before = frozen_snapshot_set();
    HiddenInputs hidden = prepare_hidden_inputs(inputs);
    require(
        hidden.audit.owner_observations_identical &&
            hidden.audit.typed_actions_identical &&
            hidden.audit.information_fingerprints_identical &&
            hidden.audit.nonvacuous,
        "hidden-repartition preflight failed before scoring");
    bundle.original =
        construct_scientific(
            inputs, hidden, false, parent, candidate);
    bundle.hidden =
        construct_scientific(
            inputs, hidden, true, parent, candidate);
    bundle.after = frozen_snapshot_set();
    bundle.original_scientific_hash =
        testing::scientific_projection_hash(
            bundle.original);
    bundle.hidden_scientific_hash =
        testing::scientific_projection_hash(bundle.hidden);
    bundle.original_hidden_scientific_bit_identical =
        bundle.original_scientific_hash ==
        bundle.hidden_scientific_hash;
    hidden.audit.raw_scores_identical =
        bundle.original_hidden_scientific_bit_identical;
    hidden.audit.supports_identical =
        bundle.original_hidden_scientific_bit_identical;
    hidden.audit.accounting_identical =
        bundle.original_hidden_scientific_bit_identical;
    hidden.audit.passed =
        hidden.audit.owner_observations_identical &&
        hidden.audit.typed_actions_identical &&
        hidden.audit.information_fingerprints_identical &&
        hidden.audit.raw_scores_identical &&
        hidden.audit.supports_identical &&
        hidden.audit.accounting_identical &&
        hidden.audit.nonvacuous;
    bundle.hidden_audit = hidden.audit;
    bundle.artifacts_unchanged =
        bundle.before == bundle.after;
    bundle.passed =
        bundle.artifacts_unchanged &&
        bundle.original_hidden_scientific_bit_identical &&
        bundle.hidden_audit.passed;
    return bundle;
}

void append_failure(
    GateReport& gate, std::string message) {
    gate.failures.push_back(std::move(message));
}

bool candidate_is_output_only(
    const calibration::OutputCalibrationArtifact& artifact,
    const std::shared_ptr<const LearnedModel>& parent) {
    const auto& report = artifact.report();
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto candidate_components =
        learned_model_component_fingerprints(
            artifact.model());
    const auto parent_tensors =
        learned_critic_tensor_fingerprints(parent);
    const auto candidate_tensors =
        learned_critic_tensor_fingerprints(
            artifact.model());
    return report.parent_components == parent_components &&
           report.candidate_components ==
               candidate_components &&
           report.parent_tensors == parent_tensors &&
           report.candidate_tensors == candidate_tensors &&
           report.output_parameters.changed_parameters == 34 &&
           parent_components.critic !=
               candidate_components.critic &&
           parent_components.priority ==
               candidate_components.priority &&
           parent_components.attack ==
               candidate_components.attack &&
           parent_components.block ==
               candidate_components.block &&
           parent_components.damage_order ==
               candidate_components.damage_order &&
           parent_tensors.input_hidden ==
               candidate_tensors.input_hidden &&
           parent_tensors.output_layer !=
               candidate_tensors.output_layer &&
           parent_tensors.direct_paths ==
               candidate_tensors.direct_paths;
}

FrozenInputs load_and_validate_frozen_inputs(
    const std::vector<probes::DecisionProbe>& balanced,
    std::vector<AuthoredProbe> focused,
    const probe_runner::ProbeCacheMetadata& expected_metadata,
    bool& actor_double_identical,
    bool& dvr_double_identical) {
    const auto mechanical_consequences =
        check_mechanical_consequences(focused);
    const auto actor_first =
        probe_runner::load_probe_label_cache_contents(
            kActorCachePath, expected_metadata, balanced);
    const auto actor_second =
        probe_runner::load_probe_label_cache_contents(
            kActorCachePath, expected_metadata, balanced);
    validate_actor_contents(actor_first, expected_metadata);
    validate_actor_contents(actor_second, expected_metadata);
    const std::string actor_first_hash =
        cache_contents_hash(actor_first);
    const std::string actor_second_hash =
        cache_contents_hash(actor_second);
    actor_double_identical =
        actor_first.metadata == actor_second.metadata &&
        actor_first_hash == actor_second_hash;
    require(
        actor_double_identical,
        "Actor cache double load is not bit-identical");

    const auto dvr_first = dvr2_replay_bundle::load(
        std::filesystem::path(kDvrArtifactPath));
    const auto dvr_second = dvr2_replay_bundle::load(
        std::filesystem::path(kDvrArtifactPath));
    const std::string dvr_first_hash =
        dvr_bundle_contents_hash(dvr_first);
    const std::string dvr_second_hash =
        dvr_bundle_contents_hash(dvr_second);
    dvr_double_identical =
        dvr_first_hash == dvr_second_hash;
    require(
        dvr_double_identical,
        "DVR2 bundle double load is not bit-identical");
    require(
        dvr_first.replays.size() == kDvrRootCount &&
            dvr_first.selected_roots ==
                dvr2_replay_bundle::kRootCount &&
            dvr_first.stable_disagreements == 4 &&
            dvr_first.stable_agreements == 43 &&
            dvr_first.unstable_best_sets == 5 &&
            dvr_first.invalid_invariance == 0,
        "DVR2 strict root census drifted");

    return {
        .balanced = balanced,
        .focused = std::move(focused),
        .actor_cache = actor_first,
        .dvr = dvr_first,
        .actor_cache_hash = actor_first_hash,
        .dvr_bundle_hash = dvr_first_hash,
        .mechanical_consequences =
            mechanical_consequences,
    };
}

std::vector<AuthoredProbe> make_focused_corpus() {
    std::vector<AuthoredProbe> result;
    const auto append =
        [&result](
            std::string family,
            std::vector<probes::DecisionProbe> probes) {
            for (auto& probe : probes) {
                result.push_back({
                    .family = family,
                    .probe = std::move(probe),
                });
            }
        };
    append(
        "force-spike",
        probes::make_force_spike_policy_controls_v1());
    append(
        "counter-composition",
        probes::make_counter_composition_controls_v1());
    append(
        "braingeyser-x0",
        probes::make_braingeyser_x_zero_control_v1());
    append(
        "disintegrate-x0",
        probes::make_probe_validation_v1());
    append(
        "field-regressions",
        probes::make_field_regressions_v1());
    append(
        "attack-regression",
        probes::make_attack_regression_v1());
    return result;
}

void validate_all_corpora(
    const std::vector<probes::DecisionProbe>& balanced,
    const std::vector<AuthoredProbe>& focused) {
    require_no_errors(
        probes::validate_probe_dev_v3(balanced),
        probes::kProbeDevV3);

    const auto collect =
        [&focused](std::string_view family) {
            std::vector<probes::DecisionProbe> result;
            for (const auto& value : focused) {
                if (value.family == family) {
                    result.push_back(value.probe);
                }
            }
            return result;
        };
    require_no_errors(
        probes::validate_force_spike_policy_controls_v1(
            collect("force-spike")),
        probes::kForceSpikePolicyControlsV1);
    require_no_errors(
        probes::validate_counter_composition_controls_v1(
            collect("counter-composition")),
        probes::kCounterCompositionControlsV1);
    require_no_errors(
        probes::validate_braingeyser_x_zero_control_v1(
            collect("braingeyser-x0")),
        probes::kBraingeyserXZeroControlV1);
    require_no_errors(
        probes::validate_probe_validation_v1(
            collect("disintegrate-x0")),
        probes::kProbeValidationV1);
    require_no_errors(
        probes::validate_field_regressions_v1(
            collect("field-regressions")),
        probes::kFieldRegressionsV1);
    require_no_errors(
        probes::validate_attack_regression_v1(
            collect("attack-regression")),
        probes::kAttackRegressionV1);

    const auto force = collect("force-spike");
    const auto live = std::find_if(
        force.begin(), force.end(),
        [](const auto& probe) {
            return probe.stable_id == kLiveForceSpikeId;
        });
    const auto payable = std::find_if(
        force.begin(), force.end(),
        [](const auto& probe) {
            return probe.stable_id == kPayableForceSpikeId;
        });
    const auto balanced_live = std::find_if(
        balanced.begin(), balanced.end(),
        [](const auto& probe) {
            return probe.category ==
                probes::Category::BlueForceSpike;
        });
    require(
        live != force.end() &&
            payable != force.end() &&
            balanced_live != balanced.end() &&
            observe_game_state(
                live->state, live->root_player) ==
                observe_game_state(
                    balanced_live->state,
                    balanced_live->root_player) &&
            live->candidates ==
                balanced_live->candidates,
        "live Force Spike control drifted from DevV3 source");
    require(
        probes::bsr_information_action_fingerprint(*live) ==
                "b792d7434096d2cc" &&
            probes::bsr_information_action_fingerprint(
                *payable) ==
                "8e24d4696a7c2ad5",
        "Force Spike control information/action identity drifted");

    const auto disintegrate = collect("disintegrate-x0");
    require(
        disintegrate.size() == 1 &&
            probes::bsr_information_action_fingerprint(
                disintegrate.front()) ==
                "04d02e0ea36d34be" &&
            probe_runner::corpus_information_set_fingerprint(
                probe_runner::ProbeCorpusKind::ValidationV1,
                disintegrate) ==
                "cac989a21b0d18cb",
        "Disintegrate validation identity/corpus drifted");
}

void print_summary(
    std::ostream& output, const RunReport& report) {
    output << std::fixed << std::setprecision(6);
    output
        << "OC1-AR1 sealed action-regression gate\n"
        << "  parent: "
        << report.scientific.parent_model_fingerprint << '\n'
        << "  candidate: "
        << report.scientific.candidate_model_fingerprint
        << '\n'
        << "  balanced Actor regret C16/OC1: "
        << report.scientific.balanced.actor_metrics.control
               .mean_regret
        << " / "
        << report.scientific.balanced.actor_metrics.candidate
               .mean_regret
        << '\n'
        << "  balanced Actor top1 C16/OC1: "
        << report.scientific.balanced.actor_metrics.control
               .mean_top_one_fraction
        << " / "
        << report.scientific.balanced.actor_metrics.candidate
               .mean_top_one_fraction
        << '\n'
        << "  joint-stable roots by deck:";
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        output << ' ' << kDeckNames[deck] << '='
               << report.scientific.balanced
                      .joint_stable_roots_by_deck[deck];
    }
    output
        << '\n'
        << "  balanced changed/lost/material: "
        << report.scientific.balanced.changed_support_roots
        << " / "
        << report.scientific.balanced
               .lost_stable_agreements
        << " / "
        << report.scientific.balanced.material_regressions
        << '\n'
        << "  focused contracts failed/inconclusive: "
        << report.scientific.focused.failed_contracts
        << " / "
        << report.scientific.focused
               .inconclusive_required_references
        << '\n'
        << "  DVR regret C16/OC1: "
        << report.scientific.dvr
               .c16_total_confirmation_regret
        << " / "
        << report.scientific.dvr
               .candidate_total_confirmation_regret
        << '\n'
        << "  hidden changed/attempted: "
        << report.integrity.hidden.pooled.changed << " / "
        << report.integrity.hidden.pooled.attempted << '\n'
        << "  scientific hash: "
        << report.integrity.first_scientific_hash << '\n'
        << "  full report hash: "
        << report.integrity.full_report_hash << '\n'
        << "  verdict: "
        << (report.gate.passed ? "PASS"
                              : report.gate.infrastructure_failure
                                    ? "INFRASTRUCTURE FAILURE"
                                    : "REJECT/INCONCLUSIVE")
        << '\n';
    for (const std::string& failure : report.gate.failures) {
        output << "    - " << failure << '\n';
    }
}

} // namespace

GateReport evaluate_gate(
    const BalancedReport& balanced,
    const FocusedReport& focused,
    const DvrReport& dvr,
    const IntegrityReport& integrity) {
    GateReport gate{
        .infrastructure_failure = !integrity.passed,
        .integrity_passed = integrity.passed,
        .balanced_passed = balanced.passed,
        .focused_passed = focused.passed,
        .dvr_passed = dvr.passed,
    };
    if (!integrity.passed) {
        append_failure(
            gate, "artifact/integrity gate failed");
    }
    if (!balanced.passed) {
        append_failure(
            gate, "balanced action-regression gate failed");
    }
    if (!focused.passed) {
        append_failure(
            gate, "focused behavior/reference gate failed");
    }
    if (!dvr.passed) {
        append_failure(
            gate, "DVR2 confirmation-regret gate failed");
    }
    gate.passed =
        gate.failures.empty() &&
        !gate.infrastructure_failure;
    return gate;
}

RunReport run(std::ostream& progress) {
    progress
        << "OC1-AR1: verifying all immutable inputs before "
           "candidate action scoring...\n";
    IntegrityReport integrity;
    integrity.preflight = frozen_snapshot_set();

    std::vector<probes::DecisionProbe> balanced =
        probes::make_probe_dev_v3();
    std::vector<AuthoredProbe> focused =
        make_focused_corpus();
    validate_all_corpora(balanced, focused);
    const auto actor_metadata =
        expected_actor_metadata(balanced);

    bool actor_double_identical = false;
    bool dvr_double_identical = false;
    FrozenInputs inputs =
        load_and_validate_frozen_inputs(
            balanced, std::move(focused), actor_metadata,
            actor_double_identical,
            dvr_double_identical);
    require(
        inputs.mechanical_consequences.size() == 3 &&
            std::all_of(
                inputs.mechanical_consequences.begin(),
                inputs.mechanical_consequences.end(),
                [](const auto& root) {
                    return root.passed &&
                           root.hidden_identity_changed;
                }),
        "supplemental mechanical consequence preflight failed");
    integrity.raw_actor_double_load_bit_identical =
        actor_double_identical;
    integrity.dvr_double_load_bit_identical =
        dvr_double_identical;

    const auto parent_artifact =
        load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            kParentTrainingGames, kParentTrainingSeed,
            kParentGenerations);
    const auto parent_requirement_identity =
        calibration::ParentArtifactIdentity{
            .byte_size = kParentArtifactBytes,
            .sha256 = std::string(kParentArtifactSha256),
            .model_fingerprint =
                std::string(kParentModelFingerprint),
            .training_games = kParentTrainingGames,
            .training_seed = kParentTrainingSeed,
            .generations = kParentGenerations,
        };
    const auto verified_parent =
        calibration::verify_output_calibration_parent(
            std::string(kParentArtifactPath),
            parent_artifact.model(),
            parent_requirement_identity);
    const auto candidate_artifact =
        calibration::load_output_calibration_artifact(
            std::string(kCandidateArtifactPath),
            verified_parent,
            calibration::canonical_fit_config(),
            {});
    const auto parent = verified_parent.model();
    const auto candidate = candidate_artifact.model();
    integrity.model_identities_verified =
        learned_model_fingerprint(parent) ==
            kParentModelFingerprint &&
        learned_model_fingerprint(candidate) ==
            kCandidateModelFingerprint &&
        candidate_artifact.report().candidate_fingerprint ==
            kCandidateModelFingerprint &&
        candidate_artifact.report().parent ==
            parent_requirement_identity;
    require(
        integrity.model_identities_verified,
        "parent/candidate model identity verification failed");
    integrity.output_calibration_isolated =
        candidate_is_output_only(
            candidate_artifact, parent);
    require(
        integrity.output_calibration_isolated,
        "candidate is not an isolated 34-parameter output calibration");

    progress
        << "OC1-AR1: independently constructing two complete "
           "original/hidden report bundles...\n";
    ConstructionBundleReport first_bundle =
        construct_bundle(inputs, parent, candidate);
    ConstructionBundleReport repeated_bundle =
        construct_bundle(inputs, parent, candidate);
    integrity.first_before = first_bundle.before;
    integrity.first_after = first_bundle.after;
    integrity.repeated_before = repeated_bundle.before;
    integrity.repeated_after = repeated_bundle.after;
    integrity.hidden = first_bundle.hidden_audit;
    integrity.repeated_hidden =
        repeated_bundle.hidden_audit;
    integrity.first_scientific_hash =
        first_bundle.original_scientific_hash;
    integrity.repeated_scientific_hash =
        repeated_bundle.original_scientific_hash;
    integrity.hidden_scientific_hash =
        first_bundle.hidden_scientific_hash;
    integrity.repeated_hidden_scientific_hash =
        repeated_bundle.hidden_scientific_hash;
    integrity.repeated_construction_bit_identical =
        integrity.first_scientific_hash ==
        integrity.repeated_scientific_hash;
    integrity.hidden_scientific_bit_identical =
        first_bundle
            .original_hidden_scientific_bit_identical;
    integrity.repeated_hidden_scientific_bit_identical =
        repeated_bundle
            .original_hidden_scientific_bit_identical;
    integrity.hidden_repeat_bit_identical =
        integrity.hidden_scientific_hash ==
        integrity.repeated_hidden_scientific_hash;
    integrity.hidden_audits_bit_identical =
        integrity.hidden == integrity.repeated_hidden;
    integrity.first_full_construction_hash =
        testing::canonical_construction_bundle_hash(
            first_bundle);
    integrity.repeated_full_construction_hash =
        testing::canonical_construction_bundle_hash(
            repeated_bundle);
    integrity.repeated_full_construction_bit_identical =
        integrity.first_full_construction_hash ==
        integrity.repeated_full_construction_hash;
    integrity.artifacts_unchanged =
        first_bundle.artifacts_unchanged &&
        repeated_bundle.artifacts_unchanged &&
        integrity.preflight == integrity.first_before &&
        integrity.first_after ==
            integrity.repeated_before &&
        integrity.repeated_before ==
            integrity.repeated_after;
    const bool primitive_integrity_passed =
        integrity.model_identities_verified &&
        integrity.output_calibration_isolated &&
        integrity.raw_actor_double_load_bit_identical &&
        integrity.dvr_double_load_bit_identical &&
        integrity.artifacts_unchanged &&
        integrity.repeated_construction_bit_identical &&
        integrity.repeated_full_construction_bit_identical &&
        integrity.hidden_scientific_bit_identical &&
        integrity.repeated_hidden_scientific_bit_identical &&
        integrity.hidden_repeat_bit_identical &&
        integrity.hidden_audits_bit_identical &&
        integrity.hidden.passed &&
        integrity.repeated_hidden.passed &&
        first_bundle.passed &&
        repeated_bundle.passed;
    integrity.passed = primitive_integrity_passed;

    RunReport report{
        .scientific = std::move(first_bundle.original),
        .integrity = std::move(integrity),
    };
    report.gate =
        evaluate_gate(
            report.scientific.balanced,
            report.scientific.focused,
            report.scientific.dvr,
            report.integrity);
    report.integrity.full_report_hash =
        testing::canonical_full_report_hash(report);
    print_summary(progress, report);
    return report;
}

int exit_code(const GateReport& gate) {
    if (gate.infrastructure_failure ||
        !gate.integrity_passed) {
        return 2;
    }
    return gate.passed ? 0 : 1;
}

int run_cli(
    int argc, char*[], std::ostream& output,
    std::ostream& error) {
    if (argc != 1) {
        error
            << "Usage: old-school-oc1-action-regression\n"
            << "This sealed command accepts no paths, seeds, "
               "recipes, or gate overrides.\n";
        return 2;
    }
    try {
        return exit_code(run(output).gate);
    } catch (const std::exception& failure) {
        error << "OC1-AR1 infrastructure failure: "
              << failure.what() << '\n';
        return 2;
    }
}

namespace testing {

void validate_frozen_corpus_preflight() {
    const std::vector<probes::DecisionProbe> balanced =
        probes::make_probe_dev_v3();
    const std::vector<AuthoredProbe> focused =
        make_focused_corpus();
    validate_all_corpora(balanced, focused);
}

bool snapshot_matches_requirement(
    const artifact_integrity::RegularFileSnapshot& snapshot,
    const FileRequirement& requirement) {
    if (requirement.path.empty() ||
        requirement.sha256.empty() ||
        snapshot.byte_size != requirement.byte_size ||
        snapshot.sha256 != requirement.sha256) {
        return false;
    }
    std::filesystem::path expected(requirement.path);
    if (!expected.is_absolute()) {
        expected =
            std::filesystem::absolute(expected);
    }
    return std::filesystem::path(snapshot.path)
               .lexically_normal() ==
           expected.lexically_normal();
}

bool focused_authored_census_is_exact(
    const std::array<std::size_t, kFocusedFamilyCount>&
        family_counts,
    std::size_t authored_root_count) {
    return family_counts[0] == 2 &&
           family_counts[1] == 2 &&
           family_counts[2] == 1 &&
           family_counts[3] == 1 &&
           family_counts[4] == 6 &&
           family_counts[5] == 1 &&
           family_counts[6] == 0 &&
           authored_root_count == 13;
}

std::string scientific_projection_hash(
    const ScientificEvidence& evidence) {
    CanonicalWriter writer;
    append_scientific_evidence(writer, evidence);
    return writer.hash();
}

std::string canonical_construction_report_hash(
    const RunReport& report) {
    CanonicalWriter writer;
    append_scientific_evidence(writer, report.scientific);
    append_snapshot_set(writer, report.integrity.preflight);
    append_snapshot_set(writer, report.integrity.first_before);
    append_snapshot_set(writer, report.integrity.first_after);
    append_snapshot_set(
        writer, report.integrity.repeated_before);
    append_snapshot_set(
        writer, report.integrity.repeated_after);
    append_hidden_audit(writer, report.integrity.hidden);
    append_hidden_audit(
        writer, report.integrity.repeated_hidden);
    writer.text(report.integrity.first_scientific_hash);
    writer.text(report.integrity.repeated_scientific_hash);
    writer.text(report.integrity.hidden_scientific_hash);
    writer.text(
        report.integrity.repeated_hidden_scientific_hash);
    writer.boolean(
        report.integrity.model_identities_verified);
    writer.boolean(
        report.integrity.output_calibration_isolated);
    writer.boolean(
        report.integrity
            .raw_actor_double_load_bit_identical);
    writer.boolean(
        report.integrity.dvr_double_load_bit_identical);
    writer.boolean(report.integrity.artifacts_unchanged);
    writer.boolean(
        report.integrity
            .repeated_construction_bit_identical);
    writer.boolean(
        report.integrity
            .repeated_full_construction_bit_identical);
    writer.boolean(
        report.integrity.hidden_scientific_bit_identical);
    writer.boolean(
        report.integrity
            .repeated_hidden_scientific_bit_identical);
    writer.boolean(
        report.integrity.hidden_repeat_bit_identical);
    writer.boolean(
        report.integrity.hidden_audits_bit_identical);
    writer.boolean(report.integrity.passed);
    append_gate(writer, report.gate);
    return writer.hash();
}

std::string canonical_construction_bundle_hash(
    const ConstructionBundleReport& bundle) {
    CanonicalWriter writer;
    append_snapshot_set(writer, bundle.before);
    append_snapshot_set(writer, bundle.after);
    append_scientific_evidence(writer, bundle.original);
    append_scientific_evidence(writer, bundle.hidden);
    append_hidden_audit(writer, bundle.hidden_audit);
    writer.text(bundle.original_scientific_hash);
    writer.text(bundle.hidden_scientific_hash);
    writer.boolean(
        bundle.original_hidden_scientific_bit_identical);
    writer.boolean(bundle.artifacts_unchanged);
    writer.boolean(bundle.passed);
    return writer.hash();
}

std::string canonical_full_report_hash(
    const RunReport& report) {
    CanonicalWriter writer;
    writer.text(
        canonical_construction_report_hash(report));
    writer.text(
        report.integrity.first_full_construction_hash);
    writer.text(
        report.integrity.repeated_full_construction_hash);
    writer.boolean(
        report.integrity
            .repeated_full_construction_bit_identical);
    return writer.hash();
}

} // namespace testing

} // namespace old_school::oc1_action_regression
