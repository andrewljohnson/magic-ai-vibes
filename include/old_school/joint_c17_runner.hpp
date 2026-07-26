#pragma once

#include "old_school/artifact_integrity.hpp"
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
inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::uintmax_t kParentArtifactByteSize =
    3'111'437;
inline constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kLabelCacheArtifactPath =
    "data/old-school-probe-dev-v3-k64-h8-c17-j1.labels.tsv";
inline constexpr std::uintmax_t kLabelCacheArtifactByteSize =
    276'387;
inline constexpr std::string_view kLabelCacheArtifactSha256 =
    "949ea2fda448fa76b31a61927721629cfba9e6addee2da383cfbb68450b04770";

// Expectation builders for validators and tests only. Production evaluation
// must use LearnedJointC17Artifact::control_deployment() and
// treatment_deployment(), then validate those returned deployments below.
// These helpers are not an alternate deployment authority.
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

struct TrainingArtifactSnapshot {
    std::string requested_path;
    std::string resolved_path;
    bool exists = false;
    bool regular_file = false;
    std::uintmax_t byte_size = 0;
    std::string sha256;
    // True only when the snapshotter computed sha256 from the bytes at
    // `resolved_path`. Digest-shaped caller text alone leaves this false.
    bool sha256_verified = false;

    bool operator==(
        const TrainingArtifactSnapshot&) const = default;
};

// Training/publication evidence is intentionally separate from evaluation
// integrity. Target absence and atomic no-replace are publication properties;
// a later sealed evaluation is expected to find the frozen target present.
struct TrainingPublicationEvidence {
    TrainingArtifactSnapshot before;
    TrainingArtifactSnapshot after;
    bool atomic_no_replace_confirmed = false;

    bool operator==(
        const TrainingPublicationEvidence&) const = default;
};

struct TrainingPublicationGateReport {
    bool canonical_paths_exact = false;
    bool before_snapshot_valid = false;
    bool target_absent_before = false;
    bool after_snapshot_valid = false;
    bool atomic_no_replace_confirmed = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const TrainingPublicationGateReport&) const = default;
};

TrainingPublicationGateReport validate_training_publication(
    const TrainingPublicationEvidence& evidence);

inline constexpr std::string_view kJointBundleArtifactRole =
    "joint-c17-bundle";
inline constexpr std::string_view kParentModelArtifactRole =
    "parent-model";
inline constexpr std::string_view kLabelCacheArtifactRole =
    "deep-reference-label-cache";

struct EvaluationArtifactRequirement {
    std::string role;
    std::string requested_path;
    std::uintmax_t expected_byte_size = 0;
    std::string expected_sha256;

    bool operator==(
        const EvaluationArtifactRequirement&) const = default;
};

// Production snapshot adapter. run_sealed_evaluation invokes this internally;
// callers cannot replace its filesystem trust boundary.
artifact_integrity::RegularFileSnapshot
snapshot_evaluation_artifact(
    std::string_view requested_path);

struct EvaluationArtifactIntegrityRequest {
    std::vector<EvaluationArtifactRequirement> artifacts;
};

// `bundle_byte_size` and `bundle_sha256` must come from the trusted
// publication snapshot. The returned request pins those values alongside the
// frozen parent and label-cache identities above.
EvaluationArtifactIntegrityRequest
make_evaluation_artifact_integrity_request(
    std::uintmax_t bundle_byte_size,
    std::string bundle_sha256);

std::optional<EvaluationArtifactIntegrityRequest>
evaluation_artifact_integrity_from_publication(
    const TrainingPublicationEvidence& publication);

struct CapturedEvaluationArtifact {
    std::string role;
    std::string requested_path;
    std::uintmax_t expected_byte_size = 0;
    std::string expected_sha256;
    artifact_integrity::RegularFileSnapshot resolved;

    bool operator==(
        const CapturedEvaluationArtifact&) const = default;
};

struct ArtifactSetCaptureReport {
    bool snapshotter_present = false;
    bool manifest_valid = false;
    bool required_roles_exact = false;
    bool canonical_bundle_present = false;
    bool all_snapshots_captured = false;
    std::vector<CapturedEvaluationArtifact> artifacts;
    std::vector<std::string> failures;

    bool operator==(
        const ArtifactSetCaptureReport&) const = default;
};

struct EvaluationArtifactIntegrityGateReport {
    ArtifactSetCaptureReport before;
    ArtifactSetCaptureReport after;
    bool preflight_passed = false;
    bool postflight_passed = false;
    bool artifact_set_unchanged = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const EvaluationArtifactIntegrityGateReport&) const =
        default;
};

// Uses snapshot_evaluation_artifact internally. Its returned absolute path
// remains available as resolved evidence while the logical path is retained
// separately.
ArtifactSetCaptureReport capture_evaluation_artifact_set(
    const EvaluationArtifactIntegrityRequest& request);

EvaluationArtifactIntegrityGateReport
validate_evaluation_artifact_integrity(
    ArtifactSetCaptureReport before,
    ArtifactSetCaptureReport after);

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

struct FrozenDeploymentEvidence {
    LearnedJointC17Deployment control;
    LearnedJointC17Deployment treatment;
    std::string artifact_control_fingerprint;
    std::string artifact_treatment_fingerprint;
};

struct FrozenDeploymentGateReport {
    bool control_exact = false;
    bool treatment_exact = false;
    bool model_bindings_exact = false;
    bool model_fingerprints_computed = false;
    bool fingerprints_bound = false;
    std::string computed_control_fingerprint;
    std::string computed_treatment_fingerprint;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const FrozenDeploymentGateReport&) const = default;
};

FrozenDeploymentGateReport validate_frozen_deployments(
    const FrozenDeploymentEvidence& deployments,
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
    ArtifactPostflight,
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
    EvaluationArtifactIntegrityRequest artifact_integrity;
    joint_c17_eval::JointC17ExpectedModelFingerprints
        model_fingerprints;
    FrozenDeploymentEvidence deployments;
    StageCallbacks stages;
};

struct SealedRunReport {
    RunnerDisposition disposition =
        RunnerDisposition::InfrastructureFailure;
    RunnerStage terminal_stage = RunnerStage::Preflight;
    EvaluationArtifactIntegrityGateReport artifact_integrity;
    ModelFingerprintGateReport model_fingerprints;
    FrozenDeploymentGateReport deployments;
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

namespace testing {

// Explicit unit-test seam. Production orchestration must call the overload
// above and cannot inject a synthetic artifact snapshotter.
using ArtifactSnapshotter = std::function<
    artifact_integrity::RegularFileSnapshot(
        std::string_view requested_path)>;

ArtifactSetCaptureReport capture_evaluation_artifact_set(
    const EvaluationArtifactIntegrityRequest& request,
    const ArtifactSnapshotter& snapshotter);

SealedRunReport run_sealed_evaluation(
    const SealedRunRequest& request,
    const ArtifactSnapshotter& snapshotter);

} // namespace testing

} // namespace old_school::joint_c17_runner
