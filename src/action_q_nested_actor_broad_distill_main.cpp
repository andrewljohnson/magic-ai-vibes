#include "old_school/action_q_nested_actor_broad_distill.hpp"
#include "old_school/artifact_integrity.hpp"

#include <exception>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_nested_actor_broad_distill;
namespace diagnostic =
    old_school::action_q_nested_actor_diagnostic;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::uintmax_t kParentArtifactBytes = 3111437;
constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";

old_school::artifact_integrity::RegularFileSnapshot
parent_artifact_snapshot() {
    const auto snapshot =
        old_school::artifact_integrity::
            snapshot_regular_file(
                std::string(kParentArtifactPath));
    if (snapshot.byte_size != kParentArtifactBytes ||
        snapshot.sha256 != kParentArtifactSha256) {
        throw std::runtime_error(
            "AQ4-G4B parent artifact identity drifted");
    }
    return snapshot;
}

void require_parent_artifact_unchanged(
    const old_school::artifact_integrity::
        RegularFileSnapshot& expected) {
    if (parent_artifact_snapshot() != expected) {
        throw std::runtime_error(
            "AQ4-G4B parent artifact changed during the command");
    }
}

std::shared_ptr<const old_school::LearnedModel>
load_parent(
    const old_school::artifact_integrity::
        RegularFileSnapshot& before) {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    require_parent_artifact_unchanged(before);
    if (old_school::learned_model_fingerprint(parent) !=
            aq::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ4-G4B loaded parent fingerprint drifted");
    }
    return parent;
}

int run_preflight(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent,
    const old_school::artifact_integrity::
        RegularFileSnapshot& parent_artifact) {
    const diagnostic::PreflightReport report =
        diagnostic::run_preflight(
            parent, aq::preflight_recipe());
    require_parent_artifact_unchanged(parent_artifact);
    aq::print_preflight(std::cout, report);
    const bool passed =
        aq::kFrozenPreflightDigest.empty()
            ? report.gate_passed()
            : aq::preflight_exact(report);
    return passed ? 0 : 1;
}

int run_census(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent,
    const old_school::artifact_integrity::
        RegularFileSnapshot& parent_artifact) {
    const aq::Census census = aq::collect_census(parent);
    aq::validate_census(census);
    require_parent_artifact_unchanged(parent_artifact);
    aq::print_census(std::cout, census);
    return 0;
}

int run_experiment(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent,
    const old_school::artifact_integrity::
        RegularFileSnapshot& parent_artifact) {
    if (aq::kFrozenCensusManifestHash.empty()) {
        throw std::runtime_error(
            "AQ4-G4B census hash is not frozen; --run is sealed");
    }

    // Reconstruct and authenticate both preregistered boundaries before
    // opening any fresh G4B teacher coordinate.
    const aq::Census census = aq::collect_census(parent);
    aq::require_frozen_census(census);
    require_parent_artifact_unchanged(parent_artifact);
    const diagnostic::PreflightReport preflight =
        diagnostic::run_preflight(
            parent, aq::preflight_recipe());
    require_parent_artifact_unchanged(parent_artifact);
    if (!aq::preflight_exact(preflight)) {
        throw std::runtime_error(
            "AQ4-G4B preflight differs from frozen G1 evidence");
    }
    aq::print_preflight(std::cout, preflight);

    const aq::Corpus corpus =
        aq::collect_corpus(
            parent, census, preflight);
    require_parent_artifact_unchanged(parent_artifact);
    const aq::FitReport fit = aq::fit(corpus, parent);
    require_parent_artifact_unchanged(parent_artifact);
    if (!fit.candidate) {
        throw std::runtime_error(
            "AQ4-G4B fit returned no candidate");
    }
    const aq::OfflineReport offline =
        aq::evaluate_offline(
            corpus, fit, preflight,
            parent, fit.candidate);
    require_parent_artifact_unchanged(parent_artifact);
    aq::print_offline(
        std::cout, corpus, fit, offline);
    if (!offline.gate_passed()) {
        return 1;
    }

    const old_school::BotBenchmarkSummary selector =
        aq::run_selector(
            corpus, preflight,
            parent, fit.candidate, fit, offline);
    require_parent_artifact_unchanged(parent_artifact);
    const aq::SelectorDisposition disposition =
        aq::classify_selector(selector);
    aq::print_selector(
        std::cout, selector, disposition);
    return disposition == aq::SelectorDisposition::Reject
               ? 1
               : 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto command = aq::parse_command(arguments);
    if (!command.has_value()) {
        aq::print_usage(std::cerr);
        return 2;
    }
    try {
        const auto parent_artifact =
            parent_artifact_snapshot();
        const auto parent =
            load_parent(parent_artifact);
        int result = 0;
        if (*command == aq::Command::Preflight) {
            result =
                run_preflight(
                    parent, parent_artifact);
        } else if (*command == aq::Command::Census) {
            result =
                run_census(
                    parent, parent_artifact);
        } else {
            result =
                run_experiment(
                    parent, parent_artifact);
        }
        require_parent_artifact_unchanged(
            parent_artifact);
        return result;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_nested_actor_broad_distill_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
