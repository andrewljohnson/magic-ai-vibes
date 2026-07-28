#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_candidate_artifact.hpp"
#include "old_school/fq4_dev_evaluator.hpp"
#include "old_school/fq4_neutral_evaluator.hpp"
#include "old_school/fq4_neutral_supplement.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

namespace old_school::fq4_neutral_candidate_publisher {

inline constexpr std::string_view kFamily =
    "FQ4-DEV5-NEUTRAL-ANCHORED";
inline constexpr std::string_view kEnvironment =
    "old-school-environment-v3-cleanup-discard;"
    "dev1-bytes=2250909;"
    "dev1-sha256="
    "0911fc2eb8b14ddc9165543eb1e4c4ed"
    "b0b058256a58dedf61f6c4ea4ca859df;"
    "neutral-bytes=661475;"
    "neutral-sha256="
    "47d94823f043971f6f9f0aa5f552bfae"
    "210af9615d8f6dc7392e52dad3eaa105";
static_assert(kEnvironment.size() == 238);
inline constexpr std::string_view kArtifactPath =
    "data/"
    "old-school-fq4-dev5-neutral-anchored-candidate-v1."
    "fq4candidate";
inline constexpr std::string_view kFitInputSha256 =
    "a13c2bca589a42d020fcb7abfa1826fa"
    "e5a9745be41602442fa7e7bc1d768fef";
inline constexpr std::string_view kCandidateFingerprint =
    "22834a951e8338568be93561a34c6b1df"
    "588faa71feb9d184ab62021b03b2171";
inline constexpr std::uint64_t kPositiveCandidateBytes =
    237'282;
inline constexpr std::string_view
    kPositiveCandidateSha256 =
        "aca8ba9c337a5b41d0cf624f7ec46ab6"
        "52c7bebc1b5c2c29fa844b900c467f63";
inline constexpr std::uint64_t kNeutralArtifactBytes =
    661'475;
inline constexpr std::string_view kNeutralArtifactSha256 =
    "47d94823f043971f6f9f0aa5f552bfae"
    "210af9615d8f6dc7392e52dad3eaa105";

const fq4_dev_candidate_artifact::Contract&
production_contract();
std::filesystem::path production_artifact_path();
std::filesystem::path production_temporary_path();

struct RunReport {
    artifact_integrity::RegularFileSnapshot executable_before;
    artifact_integrity::RegularFileSnapshot executable_after;
    artifact_integrity::RegularFileSnapshot dev1_before;
    artifact_integrity::RegularFileSnapshot dev1_after;
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot
        positive_candidate_before;
    artifact_integrity::RegularFileSnapshot
        positive_candidate_after;
    artifact_integrity::RegularFileSnapshot neutral_before;
    artifact_integrity::RegularFileSnapshot neutral_after;
    artifact_integrity::RegularFileSnapshot artifact_published;
    artifact_integrity::RegularFileSnapshot artifact_reloaded;
    fq4_dev_candidate_artifact::Report artifact;
    fq4_neutral_evaluator::Report first_evaluation;
    fq4_neutral_evaluator::Report second_evaluation;
};

// Runs the sole fixed no-replace publication recipe. The executable path is
// provenance only; all scientific inputs and the destination are fixed.
RunReport publish_fixed_candidate(
    const std::filesystem::path& executable,
    std::string_view producer_commit);

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, std::string_view producer_commit);

namespace testing {

struct FrozenInput {
    std::filesystem::path path;
    std::uint64_t bytes = 0;
    std::string sha256;

    bool operator==(const FrozenInput&) const = default;
};

struct Recipe {
    std::filesystem::path executable_path;
    std::string producer_commit;
    FrozenInput dev1;
    FrozenInput parent;
    FrozenInput positive_candidate;
    FrozenInput neutral;
    std::filesystem::path destination_path;
    std::filesystem::path temporary_path;
    fq4_dev_candidate_artifact::Contract contract;
};

struct LoadedPositiveCandidate {
    std::shared_ptr<const LearnedModel> model;
    fq4_dev_candidate_artifact::Report report;
};

struct LoadedNeutral {
    fq4_neutral_supplement::Artifact artifact;
    fq4_neutral_supplement::FileIdentity identity;
};

struct ReloadedCandidate {
    std::shared_ptr<const LearnedModel> model;
    fq4_dev_candidate_artifact::Report report;
};

struct Dependencies {
    std::function<artifact_integrity::RegularFileSnapshot(
        const std::filesystem::path&)>
        snapshot;
    std::function<bool(const std::filesystem::path&)>
        path_absent;
    std::function<fq4_dev_bundle::Bundle()>
        load_dev1;
    std::function<fq4_dev_evaluator::PreparedCorpus(
        const fq4_dev_bundle::Bundle&)>
        prepare_dev1;
    std::function<std::shared_ptr<const LearnedModel>()>
        load_parent;
    std::function<LoadedPositiveCandidate(
        std::shared_ptr<const LearnedModel>)>
        load_positive_candidate;
    std::function<LoadedNeutral(
        const fq4_dev_bundle::Manifest&)>
        load_neutral;
    std::function<fq4_neutral_evaluator::Report(
        const fq4_dev_evaluator::PreparedCorpus&,
        const fq4_neutral_supplement::Artifact&,
        std::shared_ptr<const LearnedModel>,
        std::shared_ptr<const LearnedModel>)>
        evaluate;
    std::function<fq4_dev_candidate_artifact::Report(
        const std::filesystem::path&,
        std::shared_ptr<const LearnedModel>,
        std::shared_ptr<const LearnedModel>,
        const fq4_dev_candidate_artifact::Contract&)>
        publish;
    std::function<ReloadedCandidate(
        const std::filesystem::path&,
        std::shared_ptr<const LearnedModel>,
        const fq4_dev_candidate_artifact::Contract&,
        const fq4_dev_candidate_artifact::FileIdentity&)>
        reload;
};

const Recipe& fixed_recipe();
bool publication_coordinate_absent(
    const std::filesystem::path& path);
void validate_frozen_contract(
    const fq4_dev_candidate_artifact::Contract& contract);

RunReport publish_candidate(
    const Recipe& recipe,
    const Dependencies& dependencies);

using FixedPublisher =
    std::function<RunReport(
        const std::filesystem::path&, std::string_view)>;

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, std::string_view producer_commit,
    const FixedPublisher& publisher);

} // namespace testing

} // namespace old_school::fq4_neutral_candidate_publisher
