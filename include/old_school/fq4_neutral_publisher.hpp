#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_neutral_supplement.hpp"

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>

namespace old_school::fq4_neutral_publisher {

struct RunReport {
    artifact_integrity::RegularFileSnapshot executable_before;
    artifact_integrity::RegularFileSnapshot executable_after;
    artifact_integrity::RegularFileSnapshot bundle_before;
    artifact_integrity::RegularFileSnapshot bundle_after;
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot artifact_published;
    artifact_integrity::RegularFileSnapshot artifact_reloaded;
    fq4_neutral_supplement::PublicationReport publication;
    fq4_neutral_supplement::Artifact materialized;

    bool operator==(const RunReport&) const = default;
};

RunReport publish_fixed(
    const std::filesystem::path& executable,
    std::string_view producer_commit);

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, std::string_view producer_commit);

namespace testing {

struct Recipe {
    std::filesystem::path executable_path;
    std::filesystem::path bundle_path;
    std::filesystem::path parent_path;
    std::filesystem::path destination_path;
    std::filesystem::path temporary_path;
    std::string producer_commit;
};

struct Dependencies {
    std::function<artifact_integrity::RegularFileSnapshot(
        const std::filesystem::path&)>
        snapshot;
    std::function<bool(const std::filesystem::path&)>
        path_absent;
    std::function<fq4_neutral_supplement::Artifact(
        const std::filesystem::path&, std::string_view)>
        materialize;
    std::function<void(
        const fq4_neutral_supplement::Artifact&)>
        validate_materialized;
    std::function<fq4_neutral_supplement::PublicationReport(
        const fq4_neutral_supplement::Artifact&)>
        publish;
    std::function<fq4_neutral_supplement::Artifact(
        const fq4_neutral_supplement::Contract&,
        const fq4_neutral_supplement::FileIdentity&)>
        reload;
};

RunReport publish(
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

} // namespace old_school::fq4_neutral_publisher
