#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/output_calibration.hpp"
#include "old_school/output_calibration_artifact.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace old_school::output_calibration {

inline constexpr std::string_view kCanonicalParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::uintmax_t
    kCanonicalParentArtifactByteSize = 3111437;
inline constexpr std::string_view
    kCanonicalParentArtifactSha256 =
        "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kCanonicalParentModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::size_t kCanonicalParentTrainingGames = 800;
inline constexpr std::uint64_t kCanonicalParentTrainingSeed = 424242;
inline constexpr std::size_t kCanonicalParentGenerations = 16;
inline constexpr std::string_view kCanonicalOutputArtifactPath =
    "build/model-cache/"
    "old-school-value-output-calibration-v1-c16-t800-p424242-"
    "f202607261927.bin";

struct OutputCalibrationRunReport {
    std::string output_path;
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot artifact_published;
    artifact_integrity::RegularFileSnapshot artifact_reloaded;
    artifact_integrity::RegularFileSnapshot artifact_after;
    OutputCalibrationArtifactReport artifact;
    CollectionAccounting holdout_accounting;
    CorpusHashes holdout_hashes;
    HoldoutReport scientific;
    IntegrityEvidence integrity;
    GateReport gate;
    std::string original_fit_parameters_hash;
    std::string repartitioned_fit_parameters_hash;
    std::string original_scientific_report_hash;
    std::string repartitioned_scientific_report_hash;

    bool operator==(
        const OutputCalibrationRunReport&) const = default;
};

// Runs the one-shot production recipe. The output path is the only
// configurable coordinate and must not already exist.
OutputCalibrationRunReport run_output_calibration(
    const std::string& output_path, std::ostream& progress);
OutputCalibrationRunReport run_output_calibration(
    std::ostream& progress);

int output_calibration_exit_code(const GateReport& gate);

// Production command-line entrypoint. It accepts either no argument or one
// new output path. Seed, model, optimizer, collection, and gate overrides are
// deliberately unavailable.
int run_output_calibration_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

namespace testing {

// Test-only injection seam. It still exercises the production parent
// verifier, collectors, calibrator, artifact writer/loader, snapshots, and
// scientific gate. Tests may only replace the complete frozen recipe with
// their own non-production artifact and seed coordinates.
struct Recipe {
    std::string parent_path;
    ParentArtifactIdentity parent;
    CollectionConfig fit;
    CollectionConfig holdout;
    LearnedOutputCalibrationConfig optimizer;
    GateConfig gate;
    std::string output_path;
};

OutputCalibrationRunReport run_output_calibration(
    const Recipe& recipe, std::ostream& progress);

Recipe canonical_recipe(const std::string& output_path);

} // namespace testing

} // namespace old_school::output_calibration
