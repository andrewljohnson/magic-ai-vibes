#pragma once

#include "old_school/joint_c17_execution.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::joint_c17_orchestration {

inline constexpr std::size_t kCanonicalHoldoutGeneration = 18;
inline constexpr std::size_t kCanonicalHoldoutBalancedBlocks = 5;
inline constexpr std::size_t kCanonicalMaximumGameTurns = 500;

// Deliberately unlicensed until the one-shot training publication has been
// independently inspected. The post-fit identity-only source patch must set
// both values from the no-replace publication snapshot. Standalone
// evaluation fails before loading any model or opening an evaluation seed
// while either placeholder remains unset.
inline constexpr std::uintmax_t kCanonicalBundleByteSize = 6225107;
inline constexpr std::string_view kCanonicalBundleSha256 =
    "adb9404bbdd57c92c2a6a3ad759c385d8d9a81389db02faf2b74e0695afca1ba";

bool canonical_bundle_identity_is_pinned();

struct DirectStagePlan {
    joint_c17_eval::DirectPanelRole role =
        joint_c17_eval::DirectPanelRole::TreatmentVsControl;
    std::uint64_t seed = 0;
    std::size_t repetitions = 0;

    bool operator==(const DirectStagePlan&) const = default;
};

struct CanonicalStagePlan {
    std::uint64_t holdout_seed = 0;
    std::size_t holdout_generation = 0;
    std::size_t holdout_balanced_blocks = 0;
    std::size_t maximum_game_turns = 0;
    std::size_t training_games = 0;
    std::array<DirectStagePlan, 3> direct{};
    std::array<
        std::uint64_t,
        joint_c17_eval::kFixedSeedPanelCount>
        fixed_seed_panel_seeds{};
    std::size_t fixed_seed_panel_repetitions = 0;
    std::size_t mixed_field_games_per_matchup = 0;

    bool operator==(const CanonicalStagePlan&) const = default;
};

// Read-only description of the compiled, preregistered schedule. The
// source-private stage producers use this canonical value directly; callers
// cannot supply a modified plan or invoke a reserved stage out of order.
const CanonicalStagePlan& canonical_stage_plan();

struct HeldoutStageResult {
    terminal_weight_eval::HoldoutReport evidence;
    joint_c17_eval::HeldoutGateReport gate;
};

struct DeepReferenceStageResult {
    probe_runner::ValueProbePairAgainstLabelsReport evidence;
    probe_runner::ForceSpikePolicyControlReport force_spike;
    joint_c17_eval::DeepReferenceGateReport gate;
};

struct FieldRegressionStageResult {
    probe_runner::FieldRegressionReport evidence;
    joint_c17_eval::FieldRegressionGateReport gate;
};

struct DirectGameplayStageResult {
    joint_c17_eval::DirectPanelEvidence evidence;
    joint_c17_eval::DirectGameplayGateReport gate;
};

struct FixedSeedPanelsStageResult {
    std::vector<joint_c17_eval::DirectPanelEvidence> evidence;
    joint_c17_eval::FixedSeedPanelSetGateReport gate;
};

struct MixedFieldStageResult {
    std::vector<joint_c17_eval::MixedFieldSeedPanelEvidence>
        evidence;
    joint_c17_eval::MixedFieldGateReport gate;
};

struct CanonicalEvaluationEvidence {
    std::optional<HeldoutStageResult> heldout;
    std::optional<DeepReferenceStageResult> deep_reference;
    std::optional<FieldRegressionStageResult> field_regression;
    std::optional<DirectGameplayStageResult>
        treatment_vs_control;
    std::optional<DirectGameplayStageResult>
        treatment_vs_parent;
    std::optional<DirectGameplayStageResult>
        treatment_vs_handcoded;
    std::optional<FixedSeedPanelsStageResult>
        fixed_seed_panels;
    std::optional<joint_c17_eval::FinalDirectGateReport>
        final_direct_pool;
    std::optional<MixedFieldStageResult> mixed_field;
};

struct CanonicalEvaluationResult {
    joint_c17_runner::SealedRunReport sealed;
    CanonicalEvaluationEvidence evidence;
};

enum class EvaluationDisposition : std::uint8_t {
    Accepted,
    ScientificRejection,
    InfrastructureFailure,
};

std::string_view evaluation_disposition_name(
    EvaluationDisposition disposition);
int evaluation_exit_code(EvaluationDisposition disposition);

struct ProductionEvaluationOutcome {
    EvaluationDisposition disposition =
        EvaluationDisposition::InfrastructureFailure;
    std::optional<CanonicalEvaluationResult> result;
    std::string infrastructure_error;
};

// CLI-facing, noninjectable entrypoint. It resolves only the compiled
// canonical artifact paths, loads and validates the immutable context, runs
// the sealed evaluation, and converts load/runtime exceptions into an
// explicit infrastructure disposition. A scientific gate miss remains
// distinguishable from broken evidence.
ProductionEvaluationOutcome run_production_evaluation(
    std::ostream& progress);

void write_human_report(
    const ProductionEvaluationOutcome& outcome,
    std::ostream& output);
void write_tsv_report(
    const ProductionEvaluationOutcome& outcome,
    std::ostream& output);

} // namespace old_school::joint_c17_orchestration
