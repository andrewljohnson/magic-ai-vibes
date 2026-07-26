#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/game.hpp"
#include "old_school/joint_c17_eval.hpp"
#include "old_school/joint_c17_runner.hpp"
#include "old_school/probe_runner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::joint_c17_execution {

// Actor reference that owns the frozen Dev-v3 labels. It is intentionally
// distinct from the frozen C16 Value parent used by the paired family.
inline constexpr std::string_view
    kCanonicalLabelReferenceModelFingerprint =
        "dd58d3814f46d6661d40690f6ad7ac73226c2160137b2e42bfadf3e6ac7a1b72";
inline constexpr std::string_view
    kCanonicalLabelInformationSetFingerprint =
        "cf4729a535378a12";
inline constexpr std::size_t kCanonicalLabelWorlds = 64;
inline constexpr std::size_t kCanonicalLabelHorizonTurns = 8;
inline constexpr std::size_t
    kCanonicalLabelRolloutsPerWorld = 1;

struct ArtifactFence {
    artifact_integrity::RegularFileSnapshot before;
    artifact_integrity::RegularFileSnapshot after;

    bool operator==(const ArtifactFence&) const = default;
};

struct CanonicalJointC17Provenance {
    ArtifactFence bundle;
    ArtifactFence parent;
    ArtifactFence label_cache;
    probe_runner::ProbeCacheMetadata label_metadata;
    joint_c17_eval::JointC17ExpectedModelFingerprints
        model_fingerprints;
    std::string parent_model_fingerprint;
    joint_c17_runner::ModelFingerprintGateReport
        model_fingerprint_gate;
    joint_c17_runner::FrozenDeploymentGateReport
        deployment_gate;
    bool five_deck_label_balance_exact = false;
    bool all_bindings_verified = false;
};

class CanonicalJointC17Context;

namespace testing {

struct ExpectedRegularFile {
    std::uintmax_t byte_size = 0;
    std::string sha256;
};

// Test-only coordinates still use the production artifact parsers and the
// real artifact-integrity snapshotter. They permit only filesystem fixture
// relocation and expected-byte overrides; models, deployments, labels, and
// gate reports remain noninjectable.
struct CanonicalArtifactLocations {
    std::filesystem::path bundle;
    std::filesystem::path parent;
    std::filesystem::path label_cache;
    std::optional<ExpectedRegularFile> bundle_identity;
    std::optional<ExpectedRegularFile> parent_identity;
    std::optional<ExpectedRegularFile> label_cache_identity;
    bool require_canonical_path_spellings = false;
};

CanonicalJointC17Context load_canonical_joint_c17_context(
    const CanonicalArtifactLocations& locations,
    std::ostream& progress);

} // namespace testing

// Immutable load-only bridge between frozen bytes and the sealed scheduler.
// There is deliberately no constructor taking caller-supplied models,
// labels, snapshots, deployments, or evaluator reports.
class CanonicalJointC17Context {
  public:
    const CanonicalJointC17Provenance& provenance() const;
    const LearnedJointC17Report& joint_report() const;
    const LearnedJointC17Deployment& control_deployment() const;
    const LearnedJointC17Deployment& treatment_deployment() const;
    std::shared_ptr<const LearnedModel> parent_model() const;
    std::span<const probe_eval::ProbeLabel> labels() const;

  private:
    static CanonicalJointC17Context load_impl(
        const testing::CanonicalArtifactLocations& locations,
        std::ostream& progress);

    CanonicalJointC17Context(
        CanonicalJointC17Provenance provenance,
        LearnedJointC17Artifact joint_artifact,
        LearnedValueChallengerArtifact parent_artifact,
        std::vector<probe_eval::ProbeLabel> labels,
        LearnedJointC17Deployment control,
        LearnedJointC17Deployment treatment);

    CanonicalJointC17Provenance provenance_;
    LearnedJointC17Artifact joint_artifact_;
    LearnedValueChallengerArtifact parent_artifact_;
    std::vector<probe_eval::ProbeLabel> labels_;
    LearnedJointC17Deployment control_;
    LearnedJointC17Deployment treatment_;

    friend CanonicalJointC17Context
    load_canonical_joint_c17_context(std::ostream&);
    friend CanonicalJointC17Context
    testing::load_canonical_joint_c17_context(
        const testing::CanonicalArtifactLocations&,
        std::ostream&);
};

// Production entrypoint. Paths and every supporting artifact identity are
// compiled constants. The joint bundle's byte identity is bootstrapped from
// its real preflight snapshot, then held unchanged through all loads.
CanonicalJointC17Context
load_canonical_joint_c17_context(std::ostream& progress);

} // namespace old_school::joint_c17_execution
