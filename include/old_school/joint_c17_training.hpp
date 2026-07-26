#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/game.hpp"
#include "old_school/joint_c17_eval.hpp"
#include "old_school/joint_c17_runner.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace old_school::joint_c17_training {

struct TrainingValidationGateReport {
    bool request_exact = false;
    bool parent_byte_identity_verified = false;
    bool parent_coordinates_exact = false;
    bool parent_fingerprint_exact = false;
    bool label_cache_byte_identity_verified = false;
    bool target_resolved_path_exact = false;
    bool target_missing_before_training = false;
    bool trained_report_exact = false;
    bool trained_model_fingerprints_exact = false;
    bool trained_deployments_exact = false;
    bool prerequisites_unchanged_before_publication = false;
    bool publication_gate_passed = false;
    bool published_bytes_pinned = false;
    bool reloaded_report_identical = false;
    bool reloaded_model_fingerprints_identical = false;
    bool reloaded_deployments_exact = false;
    bool parent_unchanged = false;
    bool label_cache_unchanged = false;
    bool published_artifact_unchanged = false;
    bool canonical_runner_gates_passed = false;
    bool evaluation_manifest_ready = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(
        const TrainingValidationGateReport&) const = default;
};

// Complete evidence returned only after an absent target has been published,
// pinned by a true SHA-256 snapshot, reloaded through the bounded artifact
// parser, and checked against the in-memory fit.
struct TrainingPublicationResult {
    joint_c17_runner::TrainingPublicationEvidence publication;
    joint_c17_runner::TrainingPublicationGateReport
        publication_gate;
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot
        parent_before_publication;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot label_cache_before;
    artifact_integrity::RegularFileSnapshot
        label_cache_before_publication;
    artifact_integrity::RegularFileSnapshot label_cache_after;
    artifact_integrity::RegularFileSnapshot published_after_reload;
    LearnedJointC17Report report;
    joint_c17_eval::JointC17ExpectedModelFingerprints
        model_fingerprints;
    joint_c17_runner::ModelFingerprintGateReport
        model_fingerprint_gate;
    std::optional<
        joint_c17_runner::FrozenDeploymentGateReport>
        canonical_deployment_gate;
    std::optional<
        joint_c17_runner::EvaluationArtifactIntegrityRequest>
        evaluation_integrity;
    TrainingValidationGateReport gate;
};

// Production is deliberately argument-free apart from progress reporting.
// It fixes the parent, destination, T800/S424242/C16 coordinates, raw shard
// seed 202607261145, and the complete LearnedJointC17Config recipe.
// The returned bundle size/SHA and evaluation manifest are publication
// evidence, not permission for same-process evaluation: record them, pin
// them as reviewed constants in an identity-only source change, rebuild, and
// only then run the separately sealed evaluator.
TrainingPublicationResult
train_and_publish_canonical_joint_c17(std::ostream& progress);

namespace testing {

// A relocated logical filesystem root is the only path seam. The parent and
// target retain their canonical relative spellings below it. The recipe must
// be a small, nonreserved fixture (T1..4, C2..4, one balanced block, at most
// 16 turns); models, reports, deployments, and snapshotters are never
// injectable.
struct MiniatureTrainingRequest {
    std::filesystem::path logical_root;
    LearnedJointC17Config config;
};

TrainingPublicationResult
train_and_publish_miniature_joint_c17(
    const MiniatureTrainingRequest& request,
    std::ostream& progress);

} // namespace testing

} // namespace old_school::joint_c17_training
