#include "old_school/decision_density_priority.hpp"

#include "old_school/artifact_integrity.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace priority =
    old_school::decision_density_priority;

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
            "AQ17-DBC6 parent artifact identity drifted");
    }
    return snapshot;
}

void require_parent_artifact_unchanged(
    const old_school::artifact_integrity::
        RegularFileSnapshot& expected) {
    if (parent_artifact_snapshot() != expected) {
        throw std::runtime_error(
            "AQ17-DBC6 parent artifact changed during selection");
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
            old_school::decision_density_census::
                kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ17-DBC6 loaded parent fingerprint drifted");
    }
    return parent;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (!priority::parse_command(arguments).has_value()) {
        priority::print_usage(std::cerr);
        return 2;
    }
    try {
        const auto artifact = parent_artifact_snapshot();
        const auto parent = load_parent(artifact);
        const priority::RunReport report =
            priority::run(parent);
        require_parent_artifact_unchanged(artifact);
        priority::print_report(std::cout, report);
        require_parent_artifact_unchanged(artifact);
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=decision_density_priority_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
