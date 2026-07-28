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

namespace old_school::fq4_neutral_evaluator_runner {

inline constexpr std::uint64_t kPositiveCandidateBytes =
    237'282;
inline constexpr std::string_view
    kPositiveCandidateSha256 =
        "aca8ba9c337a5b41d0cf624f7ec46ab6"
        "52c7bebc1b5c2c29fa844b900c467f63";
inline constexpr std::uint64_t kParentArtifactBytes =
    3'111'437;
inline constexpr std::uint64_t kNeutralArtifactBytes =
    661'475;
inline constexpr std::string_view kNeutralArtifactSha256 =
    "47d94823f043971f6f9f0aa5f552bfae"
    "210af9615d8f6dc7392e52dad3eaa105";

struct FrozenSource {
    std::filesystem::path path;
    std::uint64_t bytes = 0;
    std::string sha256;

    bool operator==(const FrozenSource&) const = default;
};

struct FixedCoordinates {
    FrozenSource dev1;
    FrozenSource parent;
    FrozenSource positive_candidate;
    FrozenSource neutral_artifact;

    bool operator==(const FixedCoordinates&) const = default;
};

struct RunReport {
    FixedCoordinates coordinates;
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
    fq4_neutral_supplement::FileIdentity neutral_identity;
    fq4_neutral_evaluator::Report evaluation;
};

// Runs the one fixed production evaluation. It opens no source-game seed and
// exposes no recipe, path, identity, or training override.
RunReport run_fixed();

std::string format_report(const RunReport& report);

// The production command accepts no arguments.
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

namespace testing {

struct Dependencies {
    std::function<artifact_integrity::RegularFileSnapshot(
        const std::filesystem::path&)>
        snapshot;
    std::function<fq4_dev_bundle::Bundle(
        const std::filesystem::path&)>
        load_dev1;
    std::function<fq4_dev_evaluator::PreparedCorpus(
        const fq4_dev_bundle::Bundle&)>
        prepare_dev1;
    std::function<std::shared_ptr<const LearnedModel>(
        const std::filesystem::path&)>
        load_parent;
    std::function<std::shared_ptr<const LearnedModel>(
        const std::filesystem::path&,
        std::shared_ptr<const LearnedModel>,
        const fq4_dev_candidate_artifact::Contract&,
        const fq4_dev_candidate_artifact::FileIdentity&)>
        load_positive_candidate;
    std::function<fq4_neutral_supplement::Contract(
        const fq4_dev_bundle::Manifest&)>
        make_neutral_contract;
    std::function<fq4_neutral_supplement::Artifact(
        const std::filesystem::path&,
        const fq4_neutral_supplement::Contract&,
        const fq4_neutral_supplement::FileIdentity&)>
        load_neutral;
    std::function<fq4_neutral_evaluator::Report(
        const fq4_dev_evaluator::PreparedCorpus&,
        const fq4_neutral_supplement::Artifact&,
        std::shared_ptr<const LearnedModel>,
        std::shared_ptr<const LearnedModel>)>
        evaluate;
};

const FixedCoordinates& fixed_coordinates();

RunReport run_fixed(
    const Dependencies& dependencies);

using FixedRunner = std::function<RunReport()>;

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, const FixedRunner& runner);

} // namespace testing

} // namespace old_school::fq4_neutral_evaluator_runner
