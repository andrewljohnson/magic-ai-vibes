#include "old_school/decision_density_labels.hpp"

#include "old_school/artifact_integrity.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

namespace labels = old_school::decision_density_labels;
namespace integrity = old_school::artifact_integrity;

integrity::RegularFileSnapshot parent_artifact_snapshot() {
    const auto snapshot =
        integrity::snapshot_regular_file(
            std::string(labels::kParentArtifactPath));
    if (snapshot.byte_size !=
            labels::kParentArtifactBytes ||
        snapshot.sha256 !=
            labels::kParentArtifactSha256) {
        throw std::runtime_error(
            "AQ18 parent artifact identity drifted");
    }
    return snapshot;
}

void require_parent_artifact_unchanged(
    const integrity::RegularFileSnapshot& expected) {
    if (parent_artifact_snapshot() != expected) {
        throw std::runtime_error(
            "AQ18 parent artifact changed during labeling");
    }
}

std::shared_ptr<const old_school::LearnedModel>
load_parent(
    const integrity::RegularFileSnapshot& before) {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(labels::kParentArtifactPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    require_parent_artifact_unchanged(before);
    if (old_school::learned_model_fingerprint(parent) !=
        labels::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ18 loaded parent fingerprint drifted");
    }
    return parent;
}

void require_absent_cache() {
    std::error_code status_error;
    const auto status =
        std::filesystem::symlink_status(
            std::filesystem::path(
                labels::kProductionCachePath),
            status_error);
    if (status_error &&
        status_error !=
            std::errc::no_such_file_or_directory) {
        throw std::runtime_error(
            "cannot inspect AQ18 cache destination: " +
            status_error.message());
    }
    if (!status_error &&
        status.type() !=
            std::filesystem::file_type::not_found) {
        throw std::runtime_error(
            "AQ18 cache destination already exists");
    }
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    if (!labels::parse_command(arguments).has_value()) {
        labels::print_usage(std::cerr);
        return 2;
    }

    try {
        require_absent_cache();
        const auto parent_snapshot =
            parent_artifact_snapshot();
        const auto parent =
            load_parent(parent_snapshot);
        const labels::RunReport report =
            labels::run_and_publish(parent);
        require_parent_artifact_unchanged(
            parent_snapshot);
        labels::print_report(std::cout, report);
        require_parent_artifact_unchanged(
            parent_snapshot);
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=decision_density_labels_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
