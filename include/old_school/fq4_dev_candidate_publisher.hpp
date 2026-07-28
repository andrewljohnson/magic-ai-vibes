#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_candidate_artifact.hpp"
#include "old_school/fq4_dev_evaluator.hpp"

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>

namespace old_school::fq4_dev_candidate_publisher {

struct RunReport {
    artifact_integrity::RegularFileSnapshot executable_before;
    artifact_integrity::RegularFileSnapshot executable_after;
    artifact_integrity::RegularFileSnapshot corpus_before;
    artifact_integrity::RegularFileSnapshot corpus_after;
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot artifact_published;
    artifact_integrity::RegularFileSnapshot artifact_reloaded;
    fq4_dev_candidate_artifact::Report artifact;
    fq4_dev_evaluator::FitAccounting first_fit;
    fq4_dev_evaluator::FitAccounting second_fit;

    bool operator==(const RunReport&) const = default;
};

// Executes the one fixed, preregistered DEV1 publication recipe. The
// executable path is observed only so its bytes can be held stable throughout
// the run; every scientific and artifact coordinate is fixed in code.
RunReport publish_fixed_candidate(
    const std::filesystem::path& executable);

// The production command accepts no arguments or recipe overrides.
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

namespace testing {

struct Recipe {
    std::filesystem::path executable_path;
    std::filesystem::path corpus_path;
    std::filesystem::path parent_path;
    std::filesystem::path destination_path;
    std::filesystem::path temporary_path;
    fq4_dev_candidate_artifact::Contract contract;
};

struct ReloadedCandidate {
    std::shared_ptr<const LearnedModel> model;
    fq4_dev_candidate_artifact::Report report;
};

// Test-only boundary around filesystem observation, frozen source loading,
// fitting, and artifact I/O. Production constructs this table exclusively
// from the concrete fixed APIs.
struct Dependencies {
    std::function<artifact_integrity::RegularFileSnapshot(
        const std::filesystem::path&)>
        snapshot;
    std::function<bool(const std::filesystem::path&)>
        path_absent;
    std::function<fq4_dev_evaluator::PreparedCorpus()>
        load_corpus;
    std::function<std::shared_ptr<const LearnedModel>()>
        load_parent;
    std::function<fq4_dev_evaluator::CandidateFit(
        const fq4_dev_evaluator::PreparedCorpus&,
        std::shared_ptr<const LearnedModel>)>
        fit_candidate;
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

RunReport publish_candidate(
    const Recipe& recipe, const Dependencies& dependencies);

void validate_frozen_contract(
    const fq4_dev_candidate_artifact::Contract& contract);

using FixedPublisher =
    std::function<RunReport(const std::filesystem::path&)>;

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, const FixedPublisher& publisher);

} // namespace testing

} // namespace old_school::fq4_dev_candidate_publisher
