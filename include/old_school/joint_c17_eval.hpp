#pragma once

#include "old_school/probe_runner.hpp"
#include "old_school/terminal_weight_eval.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::joint_c17_eval {

inline constexpr double kMaximumDeckLossDelta = 0.005;
inline constexpr double kMaterialSignedBias = 0.05;
inline constexpr double kRuSignedBiasFloor = 0.010;
inline constexpr double kMaximumDeckRegretIncrease = 0.01;
inline constexpr std::size_t
    kMaximumStableBestSetLossesPerDeck = 1;
inline constexpr std::size_t kExpectedProbeCount = 20;
inline constexpr std::size_t kExpectedProbesPerDeck = 4;

// Sealed C17-J1 gameplay schedule. A repetition contains four games for
// same-deck pairings and two games for distinct ordered deck pairings.
inline constexpr std::size_t kDirectPanelRepetitions = 34;
inline constexpr std::size_t kDirectPanelGames = 2'040;
inline constexpr std::size_t kDirectPanelGamesPerDeck = 408;
inline constexpr std::size_t kDirectPanelDiagonalGames = 136;
inline constexpr std::size_t kDirectPanelOffDiagonalGames = 68;
inline constexpr std::size_t kDirectPanelQuadrantGames = 102;
inline constexpr std::size_t kDirectPanelQuartets = 510;
inline constexpr std::size_t kFixedSeedPanelCount = 8;
inline constexpr std::size_t kFixedSeedPanelRepetitions = 5;
inline constexpr std::size_t kFixedSeedPanelGames = 300;
inline constexpr std::size_t kFixedSeedPanelGamesPerDeck = 60;
inline constexpr std::size_t kFixedSeedPanelDiagonalGames = 20;
inline constexpr std::size_t kFixedSeedPanelOffDiagonalGames = 10;
inline constexpr std::size_t kFixedSeedPanelQuadrantGames = 15;
inline constexpr std::size_t kFixedSeedPanelQuartets = 75;
inline constexpr std::array<std::uint64_t, kFixedSeedPanelCount>
    kFixedSeedPanelSeeds = {
        101, 202, 303, 404, 505, 606, 707, 808,
};
inline constexpr std::size_t kFinalDirectPanelCount =
    1 + kFixedSeedPanelCount;
inline constexpr std::size_t kFinalDirectRepetitions = 74;
inline constexpr std::size_t kFinalDirectGames = 4'440;
inline constexpr std::size_t kFinalDirectGamesPerDeck = 888;
inline constexpr std::size_t kFinalDirectDiagonalGames = 296;
inline constexpr std::size_t kFinalDirectOffDiagonalGames = 148;
inline constexpr std::size_t kFinalDirectQuadrantGames = 222;
inline constexpr std::size_t kMixedFieldGamesPerMatchup = 100;
inline constexpr std::size_t
    kMixedFieldGamesPerBotPairPerMatchup = 8;
inline constexpr std::size_t kMixedFieldGamesPerSeed = 1'000;
inline constexpr std::size_t kMixedFieldTotalGames = 8'000;
inline constexpr std::size_t
    kMixedFieldGamesPerDeckPolicyPerSeed = 80;
inline constexpr std::size_t
    kMixedFieldPlayDrawGamesPerDeckPolicyPerSeed = 40;
inline constexpr std::size_t kMixedFieldGamesPerDeckPolicy = 640;
inline constexpr std::size_t
    kMixedFieldPlayDrawGamesPerDeckPolicy = 320;
inline constexpr double kMixedFieldLiftTolerance = 1e-12;
inline constexpr std::string_view kFrozenC16EvidencePolicyToken =
    "frozen-c16-k8";
inline constexpr std::string_view kHandcodedEvidencePolicyToken =
    "HandcodedPolicy";
inline constexpr std::size_t kFieldRegressionFixtureCount = 6;
inline constexpr std::array<
    std::string_view, kFieldRegressionFixtureCount>
    kRequiredFieldRegressionIds = {
        "field.ru.life20-flying-men-chump-air.v1",
        "field.ru.life4-flying-men-chump-air.v1",
        "field.green.second-main-sick-bear-growth.v1",
        "field.green.begin-combat-growth-tapped-air.v1",
        "field.green.attack-after-growth-opponent-air.v1",
        "field.green.attack-after-growth-opponent-air-untapped-control.v1",
};
// Engine-authoritative public-information fingerprints after forcing each
// candidate, in the exact fixture/candidate order above. Empty third entries
// belong to two-candidate fixtures.
inline constexpr std::array<
    std::array<std::string_view, 3>,
    kFieldRegressionFixtureCount>
    kRequiredFieldConsequenceFingerprints = {{
        {"71f6061d9c180a72", "8a617a324e455b46", ""},
        {"9d74b264bfc72bc2", "deb2198a662497f6", ""},
        {"216537bf58d559b1", "07c27a0586041841", ""},
        {"49b3946ed4010e9c", "a580f40b518904c0",
         "5d585ea7709e0940"},
        {"f381220c6830f135", "3e9a95c739f666a6", ""},
        {"40d0e7728a336bfc", "832d2f985357d3b7", ""},
    }};

inline constexpr std::array<std::string_view, 4>
    kRequiredStableBlueProbeIds = {
        "blue.counter-fire-elemental.v3",
        "blue.counter-lethal-bolt.v3",
        "blue.counter-war.v3",
        "blue.force-spike-tapped-out-gray-ogre.v3",
};
inline constexpr std::string_view kLiveForceSpikeProbeId =
    "control.blue.force-spike-live-gray-ogre.v1";
inline constexpr std::string_view kPayableForceSpikeProbeId =
    "control.blue.force-spike-payable-gray-ogre.v1";
inline constexpr std::string_view kPassCandidateKey = "pass";
inline constexpr std::string_view kForceSpikeCandidateKey =
    "force-spike-gray-ogre";

struct HeldoutGateReport {
    bool accounting_exact = false;
    bool inputs_finite = false;
    bool pooled_losses_improved = false;
    bool every_deck_loss_guard = false;
    bool green_bias_strictly_shrank = false;
    bool blue_bias_strictly_shrank = false;
    bool ru_bias_guard = false;
    bool no_new_material_bias = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const HeldoutGateReport&) const = default;
};

// Interprets model slot 1 as the paired control, model slot 2 as the
// treatment, and treatment_comparisons[0] as treatment minus control. This
// deliberately reuses the clustered terminal-weight report representation;
// it does not recompute or de-cluster any estimate.
HeldoutGateReport evaluate_heldout_gate(
    const terminal_weight_eval::HoldoutReport& report);

struct StableBestSetDeckReport {
    DeckId root_deck = DeckId::Green;
    std::size_t eligible_probes = 0;
    std::size_t control_agreements = 0;
    std::size_t treatment_agreements = 0;
    std::size_t lost_agreements = 0;
    bool passed = false;

    bool operator==(
        const StableBestSetDeckReport&) const = default;
};

// A probe is eligible when one reference-best candidate stably beats every
// candidate outside the reference-best set. Stability is evaluated from the
// paired common-world delta, not from independent candidate errors.
bool is_stable_best_set_probe(
    const probe_eval::ProbeLabel& label);

struct ForceSpikeSelectionGateReport {
    bool identities_exact = false;
    bool live_uniquely_selects_force_spike = false;
    bool payable_uniquely_selects_pass = false;
    bool hidden_repartition_passed = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const ForceSpikeSelectionGateReport&) const = default;
};

ForceSpikeSelectionGateReport evaluate_force_spike_selection_gate(
    const probe_runner::ForceSpikePolicyControlReport& report);

inline constexpr std::size_t kCommonStateCriticCount = 2;
inline constexpr std::size_t kCommonStateControlIndex = 0;
inline constexpr std::size_t kCommonStateTreatmentIndex = 1;

struct CommonStateCriticMetrics {
    std::size_t probe_count = 0;
    double brier = 0.0;
    double soft_log_loss = 0.0;
    double signed_bias = 0.0;
    double ece = 0.0;

    bool operator==(
        const CommonStateCriticMetrics&) const = default;
};

struct CommonStateCriticScope {
    std::array<
        CommonStateCriticMetrics,
        kCommonStateCriticCount>
        models{};

    bool operator==(
        const CommonStateCriticScope&) const = default;
};

struct CommonStateCriticReport {
    bool accounting_exact = false;
    bool predictions_valid = false;
    bool metrics_finite = false;
    CommonStateCriticScope pooled;
    std::array<CommonStateCriticScope, kDeckCount> by_deck{};

    bool operator==(
        const CommonStateCriticReport&) const = default;
};

// Scores both critics against the same state label: the frozen Actor
// reference's maximum candidate mean Q (`ProbeLabel::reference_value`).
// This is intentionally distinct from selected-action-Q calibration.
CommonStateCriticReport score_common_state_critics(
    std::span<const probe_eval::ProbeLabel> labels,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        control_decisions,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        treatment_decisions);

struct DeepReferenceGateReport {
    bool accounting_exact = false;
    bool metrics_finite = false;
    bool pooled_regret_no_worse = false;
    bool pooled_top_one_no_lower = false;
    bool every_deck_regret_guard = false;
    bool stable_best_set_loss_guard = false;
    bool required_blue_probes_exact = false;
    bool required_blue_selections_passed = false;
    bool hidden_repartition_passed = false;
    CommonStateCriticReport common_state_critics;
    ForceSpikeSelectionGateReport force_spike;
    std::array<StableBestSetDeckReport, kDeckCount> by_deck{};
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const DeepReferenceGateReport&) const = default;
};

// The summaries and decisions must describe the same labels exactly once.
// Stable best-set losses are counted per probe; a treatment gain never
// cancels a loss. `hidden_repartition_passed` represents the caller's exact
// retained-action/score/selection/hash invariance check.
DeepReferenceGateReport evaluate_deep_reference_gate(
    const probe_eval::ProbeMetricSummary& control_metrics,
    const probe_eval::ProbeMetricSummary& treatment_metrics,
    std::span<const probe_eval::ProbeLabel> labels,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        control_decisions,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        treatment_decisions,
    const probe_runner::ForceSpikePolicyControlReport&
        treatment_force_spike,
    bool hidden_repartition_passed);

struct JointC17ExpectedModelFingerprints;

struct FieldRegressionFixtureGateReport {
    std::string stable_id;
    bool identity_exact = false;
    bool reference_valid = false;
    bool deployment_valid = false;
    bool stable_reference_best_set = false;
    bool control_agrees = false;
    bool treatment_agrees = false;
    bool treatment_lost_control_agreement = false;

    bool operator==(
        const FieldRegressionFixtureGateReport&) const = default;
};

struct FieldRegressionGateReport {
    bool metadata_exact = false;
    bool fixture_count_exact = false;
    bool every_fixture_valid = false;
    std::size_t stable_fixture_count = 0;
    std::size_t control_agreements = 0;
    std::size_t treatment_agreements = 0;
    std::size_t treatment_losses = 0;
    std::size_t treatment_gains = 0;
    std::vector<FieldRegressionFixtureGateReport> fixtures;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const FieldRegressionGateReport&) const = default;
};

// Reject-only source-freeze gate. Unstable reference rows cannot reject the
// treatment. A stable row rejects exactly when the control selects a member
// of the reference-best set and the treatment does not; gains never offset
// losses.
FieldRegressionGateReport evaluate_field_regression_gate(
    const probe_runner::FieldRegressionReport& report,
    const JointC17ExpectedModelFingerprints& fingerprints);

using OutcomeCounts = BotBenchmarkOutcomeCounts;
using OutcomeQuadrants = BotBenchmarkOutcomeQuadrants;

enum class DirectPanelRole : std::uint8_t {
    TreatmentVsControl,
    TreatmentVsParent,
    TreatmentVsHandcodedPrimary,
    TreatmentVsHandcodedFixedSeed,
};

// Evidence not represented by BotConfig itself. The sealed runner supplies
// this alongside the summary; the evaluator binds it to the serialized
// deployment recipe and the model fingerprints below.
struct PolicyRecipeEvidence {
    std::string policy_token;
    std::size_t horizon_turns = 0;
    bool blend_shallow_prior = false;

    bool operator==(const PolicyRecipeEvidence&) const = default;
};

struct JointC17ExpectedModelFingerprints {
    std::string control;
    std::string treatment;

    bool operator==(
        const JointC17ExpectedModelFingerprints&) const = default;
};

struct DirectPanelEvidence {
    DirectPanelRole role =
        DirectPanelRole::TreatmentVsControl;
    PolicyRecipeEvidence challenger_policy;
    PolicyRecipeEvidence baseline_policy;
    BotBenchmarkSummary summary;
};

// Count-only representation used to pool independently evaluated benchmark
// panels without copying policy/model objects or summing unrelated telemetry.
struct BenchmarkCountSummary {
    std::size_t panel_count = 0;
    std::size_t repetitions_per_deck_pairing = 0;
    std::size_t total_games = 0;
    OutcomeCounts challenger;
    OutcomeCounts baseline;
    std::array<OutcomeCounts, kDeckCount> challenger_decks{};
    std::array<OutcomeCounts, kDeckCount> baseline_decks{};
    std::array<std::array<OutcomeCounts, kDeckCount>, kDeckCount>
        challenger_deck_matchups{};
    OutcomeQuadrants challenger_outcome_quadrants{};
    OutcomeQuadrants baseline_outcome_quadrants{};

    bool operator==(const BenchmarkCountSummary&) const = default;
};

struct DirectGameplayGateReport {
    bool identity_exact = false;
    bool accounting_exact = false;
    bool clustered_estimate_valid = false;
    bool rates_finite = false;
    bool aggregate_strict_win = false;
    bool wilson_lower_above_half = false;
    std::array<bool, kDeckCount> challenger_deck_strict_wins{};
    bool every_challenger_deck_strict_win = false;
    double challenger_win_rate_percent = 0.0;
    double wilson_lower_95_percent = 0.0;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const DirectGameplayGateReport&) const = default;
};

// The deck gate is deliberately challenger-perspective: each
// challenger_decks[d].wins is compared with its own losses. Baseline deck
// buckets are accounting data only and never enter the strength predicate.
DirectGameplayGateReport evaluate_direct_gameplay_panel(
    const DirectPanelEvidence& evidence,
    const JointC17ExpectedModelFingerprints& fingerprints);

struct FixedSeedPanelGateReport {
    bool identity_exact = false;
    bool accounting_exact = false;
    bool clustered_estimate_valid = false;
    bool aggregate_non_losing = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const FixedSeedPanelGateReport&) const = default;
};

FixedSeedPanelGateReport evaluate_fixed_seed_panel(
    const DirectPanelEvidence& evidence,
    const JointC17ExpectedModelFingerprints& fingerprints);

struct FixedSeedPanelSetGateReport {
    bool panel_count_exact = false;
    bool seeds_exact = false;
    bool every_panel_passed = false;
    std::vector<FixedSeedPanelGateReport> panels;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const FixedSeedPanelSetGateReport&) const = default;
};

FixedSeedPanelSetGateReport evaluate_fixed_seed_panel_set(
    std::span<const DirectPanelEvidence> panels,
    const JointC17ExpectedModelFingerprints& fingerprints);

// Merges the one 34-repetition panel and eight 5-repetition panels only when
// every input has exact schedule accounting. All additions are checked;
// malformed input or size_t overflow returns nullopt.
std::optional<BenchmarkCountSummary> merge_final_direct_panels(
    const DirectPanelEvidence& direct_panel,
    std::span<const DirectPanelEvidence> fixed_seed_panels,
    const JointC17ExpectedModelFingerprints& fingerprints);

struct FinalDirectPoolGateReport {
    bool accounting_exact = false;
    bool rates_finite = false;
    bool aggregate_strict_win = false;
    bool wilson_lower_above_half = false;
    std::array<bool, kDeckCount> challenger_deck_strict_wins{};
    bool every_challenger_deck_strict_win = false;
    double challenger_win_rate_percent = 0.0;
    double wilson_lower_95_percent = 0.0;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const FinalDirectPoolGateReport&) const = default;
};

FinalDirectPoolGateReport evaluate_final_direct_pool(
    const BenchmarkCountSummary& summary);

struct FinalDirectGateReport {
    DirectGameplayGateReport primary;
    FixedSeedPanelSetGateReport fixed_seed_panels;
    bool merge_succeeded = false;
    FinalDirectPoolGateReport pooled;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const FinalDirectGateReport&) const = default;
};

FinalDirectGateReport evaluate_final_direct_gate(
    const DirectPanelEvidence& primary,
    std::span<const DirectPanelEvidence> fixed_seed_panels,
    const JointC17ExpectedModelFingerprints& fingerprints);

struct MixedFieldDeckGateReport {
    DeckId deck = DeckId::Green;
    double random_win_rate_percent = 0.0;
    double learned_win_rate_percent = 0.0;
    double learned_lift_percentage_points = 0.0;
    double best_other_lift_percentage_points = 0.0;
    BotKind best_other = BotKind::Random;
    bool rates_finite = false;
    bool learned_lift_is_best = false;

    bool operator==(const MixedFieldDeckGateReport&) const = default;
};

struct MixedFieldGateReport {
    bool panel_count_exact = false;
    bool seeds_exact = false;
    bool policy_identity_exact = false;
    bool accounting_exact = false;
    bool rates_finite = false;
    std::array<MixedFieldDeckGateReport, kDeckCount> by_deck{};
    bool learned_lift_best_on_every_deck = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const MixedFieldGateReport&) const = default;
};

struct MixedFieldSeedPanelEvidence {
    PolicyRecipeEvidence learned_policy;
    TournamentSummary summary;
};

// Random's lift is zero. Learned must tie or beat Random, Monte Carlo, Deep
// Monte Carlo, and Handcrafted on each of all five decks.
MixedFieldGateReport evaluate_mixed_field_pool(
    std::span<const MixedFieldSeedPanelEvidence> panels,
    const JointC17ExpectedModelFingerprints& fingerprints);

struct StageOutcomes {
    bool heldout_passed = false;
    std::optional<bool> deep_reference_passed;
    std::optional<bool> field_regression_passed;
    std::optional<bool> treatment_vs_control_passed;
    std::optional<bool> treatment_vs_parent_passed;
    std::optional<bool> treatment_vs_handcoded_passed;
    std::optional<bool> fixed_seed_panel_passed;
    std::optional<bool> final_direct_pool_passed;
    std::optional<bool> mixed_field_passed;

    bool operator==(const StageOutcomes&) const = default;
};

struct StageDecision {
    bool run_deep_reference = false;
    bool run_field_regression = false;
    bool run_treatment_vs_control = false;
    bool run_treatment_vs_parent = false;
    bool run_treatment_vs_handcoded = false;
    bool run_fixed_seed_panel = false;
    bool run_final_direct_pool = false;
    bool run_mixed_field = false;
    bool complete = false;
    bool passed = false;

    bool operator==(const StageDecision&) const = default;
};

// Each failed or absent prerequisite suppresses all later stages. Outcomes
// supplied for a suppressed stage cannot reopen the sequence.
StageDecision evaluation_stage_decision(
    const StageOutcomes& outcomes);

} // namespace old_school::joint_c17_eval
