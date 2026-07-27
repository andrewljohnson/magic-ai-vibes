#pragma once

#include "old_school/game.hpp"
#include "old_school/output_calibration.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace old_school::output_calibration {

inline constexpr std::string_view kArtifactFamily =
    "old-school.learned-value-output-calibration.v1";
inline constexpr std::string_view kArtifactEnvironmentSchema =
    "old-school.engine-five-deck-rules-observation.v3";
inline constexpr std::string_view kArtifactOptimizerRecipe =
    "weighted-soft-bce-l2-0.01-full-batch-newton-armijo-v1";
inline constexpr std::string_view kArtifactWeightingRecipe =
    "actor-game-equal-record-weight-1-over-trace-size-v1";

struct ParentArtifactIdentity {
    std::uintmax_t byte_size = 0;
    std::string sha256;
    std::string model_fingerprint;
    std::size_t training_games = 0;
    std::uint64_t training_seed = 0;
    std::size_t generations = 0;

    bool operator==(const ParentArtifactIdentity&) const = default;
};

class OutputCalibrationArtifact;

// Opaque capability proving that a concrete challenger artifact was a
// stable regular file, matched the explicit byte/SHA/model/T/S/G
// requirement, loaded through the challenger-family loader, and matched the
// supplied immutable in-memory parent. It cannot be constructed from raw
// metadata.
class VerifiedParentArtifact {
  public:
    std::shared_ptr<const LearnedModel> model() const;
    const ParentArtifactIdentity& identity() const;
    const std::string& path() const;

  private:
    VerifiedParentArtifact(
        std::shared_ptr<const LearnedModel> model,
        ParentArtifactIdentity identity,
        std::string path);

    std::shared_ptr<const LearnedModel> model_;
    ParentArtifactIdentity identity_;
    std::string path_;

    friend VerifiedParentArtifact
    verify_output_calibration_parent(
        const std::string& path,
        std::shared_ptr<const LearnedModel> supplied_model,
        const ParentArtifactIdentity& requirement);
    friend OutputCalibrationArtifact
    make_output_calibration_artifact(
        const VerifiedParentArtifact& parent,
        const TrainingCorpus& fit_corpus,
        LearnedOutputCalibrationConfig optimizer);
    friend OutputCalibrationArtifact
    load_output_calibration_artifact(
        const std::string& path,
        const VerifiedParentArtifact& exact_parent,
        const CollectionConfig& expected_fit_config,
        LearnedOutputCalibrationConfig expected_optimizer);
};

VerifiedParentArtifact
verify_output_calibration_parent(
    const std::string& path,
    std::shared_ptr<const LearnedModel> supplied_model,
    const ParentArtifactIdentity& requirement);

struct OutputParameterLedger {
    LearnedOutputCalibrationParameters before;
    LearnedOutputCalibrationParameters after;
    std::size_t changed_parameters = 0;
    double maximum_absolute_delta = 0.0;

    bool operator==(const OutputParameterLedger&) const = default;
};

struct HiddenRefitEvidence {
    std::string original_parameters_sha256;
    std::string repartitioned_parameters_sha256;
    std::string original_candidate_fingerprint;
    std::string repartitioned_candidate_fingerprint;
    LearnedOutputCalibrationDiagnostics
        repartitioned_optimizer_diagnostics;

    bool operator==(
        const HiddenRefitEvidence&) const = default;
};

struct OutputCalibrationArtifactReport {
    std::string family;
    std::string environment_schema;
    std::string optimizer_recipe;
    std::string weighting_recipe;
    ParentArtifactIdentity parent;
    CollectionConfig fit_config;
    CollectionAccounting fit_accounting;
    CorpusHashes fit_hashes;
    LearnedOutputCalibrationConfig optimizer;
    LearnedOutputCalibrationDiagnostics optimizer_diagnostics;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    LearnedCriticTensorFingerprints parent_tensors;
    LearnedCriticTensorFingerprints candidate_tensors;
    OutputParameterLedger output_parameters;
    HiddenRefitEvidence hidden_refit;
    std::string candidate_fingerprint;

    bool operator==(
        const OutputCalibrationArtifactReport& other) const {
        return family == other.family &&
               environment_schema == other.environment_schema &&
               optimizer_recipe == other.optimizer_recipe &&
               weighting_recipe == other.weighting_recipe &&
               parent == other.parent &&
               fit_config == other.fit_config &&
               fit_accounting == other.fit_accounting &&
               fit_hashes == other.fit_hashes &&
               optimizer.max_iterations ==
                   other.optimizer.max_iterations &&
               optimizer.l2_tether ==
                   other.optimizer.l2_tether &&
               optimizer.gradient_tolerance ==
                   other.optimizer.gradient_tolerance &&
               optimizer_diagnostics ==
                   other.optimizer_diagnostics &&
               parent_components ==
                   other.parent_components &&
               candidate_components ==
                   other.candidate_components &&
               parent_tensors == other.parent_tensors &&
               candidate_tensors ==
                   other.candidate_tensors &&
               output_parameters ==
                   other.output_parameters &&
               hidden_refit == other.hidden_refit &&
               candidate_fingerprint ==
                   other.candidate_fingerprint;
    }
};

class OutputCalibrationArtifact {
  public:
    std::shared_ptr<const LearnedModel> model() const;
    const OutputCalibrationArtifactReport& report() const;

  private:
    OutputCalibrationArtifact(
        std::shared_ptr<const LearnedModel> model,
        OutputCalibrationArtifactReport report,
        VerifiedParentArtifact parent);

    std::shared_ptr<const LearnedModel> model_;
    OutputCalibrationArtifactReport report_;
    VerifiedParentArtifact parent_;

    friend OutputCalibrationArtifact
    make_output_calibration_artifact(
        const VerifiedParentArtifact& parent,
        const TrainingCorpus& fit_corpus,
        LearnedOutputCalibrationConfig optimizer);
    friend void
    write_output_calibration_artifact_atomic_no_replace(
        const std::string& path,
        const OutputCalibrationArtifact& artifact);
    friend OutputCalibrationArtifact
    load_output_calibration_artifact(
        const std::string& path,
        const VerifiedParentArtifact& exact_parent,
        const CollectionConfig& expected_fit_config,
        LearnedOutputCalibrationConfig expected_optimizer);
};

// Recollects the complete fit corpus from the verified parent, requires it
// to equal the supplied prior construction bit-for-bit, then independently
// runs the declared calibrator twice. No caller-supplied candidate,
// parameter, diagnostic, or provenance hash can enter the artifact.
OutputCalibrationArtifact
make_output_calibration_artifact(
    const VerifiedParentArtifact& parent,
    const TrainingCorpus& fit_corpus,
    LearnedOutputCalibrationConfig optimizer = {});

// Publishes through a synced temporary regular file and an atomic hard link.
// An existing destination, including a symlink, is never replaced.
void write_output_calibration_artifact_atomic_no_replace(
    const std::string& path,
    const OutputCalibrationArtifact& artifact);

// Loads only this artifact family, requires the caller's exact parent
// identity/configuration, reconstructs the candidate solely from the stored
// two-leaf output parameters, and verifies every recorded fingerprint.
OutputCalibrationArtifact
load_output_calibration_artifact(
    const std::string& path,
    const VerifiedParentArtifact& exact_parent,
    const CollectionConfig& expected_fit_config,
    LearnedOutputCalibrationConfig expected_optimizer = {});

std::string output_calibration_cache_path(
    std::size_t training_games,
    std::uint64_t parent_training_seed,
    std::uint64_t fit_seed);

} // namespace old_school::output_calibration
