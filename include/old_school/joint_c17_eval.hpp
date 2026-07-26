#pragma once

#include "old_school/probe_runner.hpp"
#include "old_school/terminal_weight_eval.hpp"

#include <array>
#include <cstddef>
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

struct StageOutcomes {
    bool heldout_passed = false;
    std::optional<bool> deep_reference_passed;
    std::optional<bool> treatment_vs_control_passed;
    std::optional<bool> treatment_vs_parent_passed;
    std::optional<bool> treatment_vs_handcoded_passed;
    std::optional<bool> fixed_seed_panel_passed;
    std::optional<bool> mixed_field_passed;

    bool operator==(const StageOutcomes&) const = default;
};

struct StageDecision {
    bool run_deep_reference = false;
    bool run_treatment_vs_control = false;
    bool run_treatment_vs_parent = false;
    bool run_treatment_vs_handcoded = false;
    bool run_fixed_seed_panel = false;
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
