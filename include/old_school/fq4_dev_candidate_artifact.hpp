#pragma once

#include "old_school/game.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace old_school::fq4_dev_candidate_artifact {

inline constexpr std::string_view kSchema =
    "old-school-fq4-dev1-priority-candidate-v1";
inline constexpr std::string_view kProductionArtifactPath =
    "data/old-school-fq4-dev1-priority-candidate-v1.fq4candidate";
inline constexpr std::size_t kMaximumArtifactBytes =
    16U * 1024U * 1024U;

struct ParentProvenance {
    std::uint64_t artifact_bytes = 0;
    std::string artifact_sha256;
    std::string model_fingerprint;
    LearnedModelComponentFingerprints components;
    std::uint64_t training_games = 0;
    std::uint64_t training_seed = 0;
    std::uint64_t generation = 0;

    bool operator==(const ParentProvenance&) const = default;
};

struct CorpusProvenance {
    std::uint64_t artifact_bytes = 0;
    std::string artifact_sha256;

    bool operator==(const CorpusProvenance&) const = default;
};

struct FitBoundary {
    std::string input_sha256;
    std::uint64_t examples = 0;
    std::uint64_t options = 0;
    std::uint64_t check_examples = 0;
    std::uint64_t background_only_examples = 0;
    std::uint64_t optimizer_calls = 0;
    LearnedValuePriorityHeadUpdateConfig optimizer;

    bool operator==(const FitBoundary&) const = default;
};

struct DeploymentRecipe {
    LearnedVariant variant =
        LearnedVariant::ValueSearchChampion;
    std::uint64_t training_games = 0;
    std::uint64_t worlds_per_action = 0;
    std::uint64_t horizon_turns = 0;
    std::uint64_t rollouts_per_world = 0;
    std::uint64_t root_search_depth = 0;
    bool shallow_prior = false;
    double root_exploration = 0.0;
    double continuation_epsilon = 0.0;
    double priority_residual_weight = 0.0;
    bool pass_dominance = false;
    LearnedContinuationController continuation_controller =
        LearnedContinuationController::Legacy;
    std::uint64_t max_turns = 0;

    bool operator==(const DeploymentRecipe&) const = default;
};

// Explicit, immutable-by-publication provenance and behavior identity. The
// loader compares the canonical encoded contract byte-for-byte with its
// caller's expected value; binary64 fields therefore include signed zero.
struct Contract {
    std::string family;
    std::string environment;
    ParentProvenance parent;
    CorpusProvenance corpus;
    FitBoundary fit;
    std::string candidate_model_fingerprint;
    std::uint32_t priority_hidden_count = 0;
    std::uint32_t priority_feature_count = 0;
    std::uint64_t priority_parameter_count = 0;
    DeploymentRecipe deployment;

    bool operator==(const Contract&) const = default;
};

struct TensorManifest {
    std::uint32_t hidden_count = 0;
    std::uint32_t feature_count = 0;
    std::uint64_t parameter_count = 0;
    std::string parent_sha256;
    std::string candidate_sha256;
    std::string xor_delta_sha256;

    bool operator==(const TensorManifest&) const = default;
};

struct Manifest {
    Contract contract;
    LearnedModelComponentFingerprints candidate_components;
    TensorManifest tensors;

    bool operator==(const Manifest&) const = default;
};

struct FileIdentity {
    std::uint64_t bytes = 0;
    std::string sha256;

    bool operator==(const FileIdentity&) const = default;
};

struct Report {
    FileIdentity artifact;
    Manifest manifest;

    bool operator==(const Report&) const = default;
};

class LoadedCandidate {
  public:
    const std::shared_ptr<const LearnedModel>& model() const;
    const Manifest& manifest() const;
    const Report& report() const;

  private:
    friend LoadedCandidate load(
        const std::filesystem::path&,
        std::shared_ptr<const LearnedModel>,
        const Contract&, const FileIdentity&);

    std::shared_ptr<const LearnedModel> model_;
    Report report_;
};

// The one preregistered DEV1 candidate contract. It intentionally does not
// include the candidate artifact's eventual byte size or SHA-256; those are
// pinned only after the first no-replace publication.
const Contract& production_contract();

std::filesystem::path production_artifact_path();

// Publication uses this deterministic same-directory temporary. Publishers
// can prove both it and the final destination absent before performing a fit.
std::filesystem::path temporary_path_for(
    const std::filesystem::path& destination);
std::filesystem::path production_temporary_path();

// Encodes the candidate as bit-exact XOR masks against the contract-bound
// parent Priority tensors, then publishes through a same-directory atomic
// no-replace link. Neither the destination nor deterministic temporary may
// already exist, including as a symlink.
Report publish_atomic_no_replace(
    const std::filesystem::path& destination,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const Contract& contract);

// Loads only an artifact with the exact externally pinned file identity,
// exact contract, and exact supplied parent. Reconstruction is exclusively
// through with_learned_priority_head_parameters().
LoadedCandidate load(
    const std::filesystem::path& path,
    std::shared_ptr<const LearnedModel> parent,
    const Contract& expected_contract,
    const FileIdentity& expected_artifact);

} // namespace old_school::fq4_dev_candidate_artifact
