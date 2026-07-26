#pragma once

#include "old_school/joint_c17_eval.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::joint_c17_runner {

inline constexpr std::size_t kCanonicalTrainingGames = 800;
inline constexpr std::string_view kCanonicalArtifactFilename =
    "old-school-value-joint-c17-v1-t800-p424242-r202607261145.bin";
inline constexpr std::string_view kCanonicalArtifactPath =
    "build/model-cache/"
    "old-school-value-joint-c17-v1-t800-p424242-r202607261145.bin";

// These builders are the single mechanical source for the policy identities
// consumed by the sealed C17-J1 evaluator. Search-only recipe fields live in
// the accompanying PolicyRecipeEvidence builders.
BotConfig make_control_bot(
    std::shared_ptr<const LearnedModel> model = {});
BotConfig make_treatment_bot(
    std::shared_ptr<const LearnedModel> model = {});
BotConfig make_parent_bot(
    std::shared_ptr<const LearnedModel> model = {});
BotConfig make_handcoded_bot();

joint_c17_eval::PolicyRecipeEvidence make_control_recipe();
joint_c17_eval::PolicyRecipeEvidence make_treatment_recipe();
joint_c17_eval::PolicyRecipeEvidence make_parent_recipe();
joint_c17_eval::PolicyRecipeEvidence make_handcoded_recipe();

// The runner deliberately does not write files. The integration layer takes
// snapshots around the existing atomic no-replace writer and supplies the
// resulting evidence here. This keeps unit tests away from the canonical path
// and makes overwrite protection an explicit sealed-run prerequisite.
struct ArtifactSnapshot {
    std::string path;
    bool exists = false;
    bool regular_file = false;
    std::uintmax_t byte_size = 0;
    std::string sha256;

    bool operator==(const ArtifactSnapshot&) const = default;
};

struct ArtifactPublicationEvidence {
    ArtifactSnapshot before;
    ArtifactSnapshot after;
    bool atomic_no_replace_confirmed = false;

    bool operator==(
        const ArtifactPublicationEvidence&) const = default;
};

struct ArtifactPublicationGateReport {
    bool canonical_paths_exact = false;
    bool before_snapshot_valid = false;
    bool target_absent_before = false;
    bool after_snapshot_valid = false;
    bool atomic_no_replace_confirmed = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const ArtifactPublicationGateReport&) const = default;
};

ArtifactPublicationGateReport
validate_canonical_artifact_publication(
    const ArtifactPublicationEvidence& evidence);

struct ModelFingerprintGateReport {
    bool control_is_sha256 = false;
    bool treatment_is_sha256 = false;
    bool fingerprints_distinct = false;
    bool parent_excluded = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const ModelFingerprintGateReport&) const = default;
};

ModelFingerprintGateReport validate_model_fingerprints(
    const joint_c17_eval::JointC17ExpectedModelFingerprints&
        fingerprints);

enum class RunnerDisposition : std::uint8_t {
    InfrastructureFailure,
    Rejected,
    Passed,
};

enum class RunnerStage : std::uint8_t {
    Preflight,
    Heldout,
    DeepReference,
    FieldRegression,
    TreatmentVsControl,
    TreatmentVsParent,
    TreatmentVsHandcoded,
    FixedSeedPanels,
    FinalDirectPool,
    MixedField,
    Complete,
};

std::string_view runner_stage_name(RunnerStage stage);

struct RunnerStageRecord {
    RunnerStage stage = RunnerStage::Preflight;
    RunnerDisposition disposition =
        RunnerDisposition::InfrastructureFailure;
    std::vector<std::string> failures;

    bool operator==(const RunnerStageRecord&) const = default;
};

// Every callback must return the corresponding report produced by the
// joint_c17_eval layer. Keeping the expensive work behind callbacks makes the
// ordering observable and guarantees a rejected stage cannot accidentally
// launch or salvage a later one.
struct StageCallbacks {
    std::function<joint_c17_eval::HeldoutGateReport()> heldout;
    std::function<joint_c17_eval::DeepReferenceGateReport()>
        deep_reference;
    std::function<joint_c17_eval::FieldRegressionGateReport()>
        field_regression;
    std::function<joint_c17_eval::DirectGameplayGateReport()>
        treatment_vs_control;
    std::function<joint_c17_eval::DirectGameplayGateReport()>
        treatment_vs_parent;
    std::function<joint_c17_eval::DirectGameplayGateReport()>
        treatment_vs_handcoded;
    std::function<joint_c17_eval::FixedSeedPanelSetGateReport()>
        fixed_seed_panels;
    std::function<joint_c17_eval::FinalDirectGateReport()>
        final_direct_pool;
    std::function<joint_c17_eval::MixedFieldGateReport()>
        mixed_field;
};

struct SealedRunRequest {
    ArtifactPublicationEvidence artifact_publication;
    joint_c17_eval::JointC17ExpectedModelFingerprints
        model_fingerprints;
    StageCallbacks stages;
};

struct SealedRunReport {
    RunnerDisposition disposition =
        RunnerDisposition::InfrastructureFailure;
    RunnerStage terminal_stage = RunnerStage::Preflight;
    ArtifactPublicationGateReport artifact_publication;
    ModelFingerprintGateReport model_fingerprints;
    joint_c17_eval::StageOutcomes outcomes;
    joint_c17_eval::StageDecision decision;
    std::vector<RunnerStageRecord> stages;
    std::optional<joint_c17_eval::HeldoutGateReport> heldout;
    std::optional<joint_c17_eval::DeepReferenceGateReport>
        deep_reference;
    std::optional<joint_c17_eval::FieldRegressionGateReport>
        field_regression;
    std::optional<joint_c17_eval::DirectGameplayGateReport>
        treatment_vs_control;
    std::optional<joint_c17_eval::DirectGameplayGateReport>
        treatment_vs_parent;
    std::optional<joint_c17_eval::DirectGameplayGateReport>
        treatment_vs_handcoded;
    std::optional<
        joint_c17_eval::FixedSeedPanelSetGateReport>
        fixed_seed_panels;
    std::optional<joint_c17_eval::FinalDirectGateReport>
        final_direct_pool;
    std::optional<joint_c17_eval::MixedFieldGateReport>
        mixed_field;
};

SealedRunReport run_sealed_evaluation(
    const SealedRunRequest& request);

} // namespace old_school::joint_c17_runner
