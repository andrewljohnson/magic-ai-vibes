#include "old_school/fq4_neutral_publisher.hpp"

#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_generator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace old_school::fq4_neutral_publisher {
namespace {

namespace artifact = fq4_neutral_supplement;
namespace bundle = fq4_dev_bundle;
namespace generator = fq4_dev_generator;
namespace integrity = artifact_integrity;

constexpr std::uint64_t kParentArtifactBytes = 3'111'437;

[[noreturn]] void fail(std::string_view category) {
    throw std::runtime_error(std::string(category));
}

bool canonical_commit(std::string_view value) {
    return value.size() == 40 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               }) &&
           std::any_of(
               value.begin(), value.end(),
               [](char character) {
                   return character != '0';
               });
}

bool path_absent(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error ==
        std::make_error_code(
            std::errc::no_such_file_or_directory)) {
        return true;
    }
    if (error) {
        throw std::system_error(
            error,
            "neutral publication coordinate inspection failed");
    }
    return !std::filesystem::exists(status);
}

void validate_recipe(const testing::Recipe& recipe) {
    const std::array<const std::filesystem::path*, 5> paths{
        &recipe.executable_path,
        &recipe.bundle_path,
        &recipe.parent_path,
        &recipe.destination_path,
        &recipe.temporary_path,
    };
    for (const std::filesystem::path* path : paths) {
        if (path->empty() ||
            path->filename().empty() ||
            path->string().find('\0') !=
                std::string::npos) {
            fail("invalid_neutral_publisher_recipe");
        }
    }
    for (std::size_t first = 0;
         first < paths.size(); ++first) {
        for (std::size_t second = first + 1;
             second < paths.size(); ++second) {
            if (paths[first]->lexically_normal() ==
                paths[second]->lexically_normal()) {
                fail("invalid_neutral_publisher_recipe");
            }
        }
    }
    if (!canonical_commit(recipe.producer_commit) ||
        recipe.temporary_path !=
            artifact::testing::temporary_path_for(
                recipe.destination_path)) {
        fail("invalid_neutral_publisher_recipe");
    }
}

void validate_dependencies(
    const testing::Dependencies& dependencies) {
    if (!dependencies.snapshot ||
        !dependencies.path_absent ||
        !dependencies.materialize ||
        !dependencies.validate_materialized ||
        !dependencies.publish ||
        !dependencies.reload) {
        fail("incomplete_neutral_publisher_dependencies");
    }
}

void require_absent(
    const testing::Recipe& recipe,
    const testing::Dependencies& dependencies) {
    if (!dependencies.path_absent(
            recipe.destination_path) ||
        !dependencies.path_absent(
            recipe.temporary_path)) {
        fail("neutral_publication_coordinate_not_new");
    }
}

void require_source_identity(
    const integrity::RegularFileSnapshot& bundle_snapshot,
    const integrity::RegularFileSnapshot& parent_snapshot) {
    if (bundle_snapshot.byte_size !=
            bundle::kPublishedArtifactBytes ||
        bundle_snapshot.sha256 !=
            bundle::kPublishedArtifactSha256 ||
        parent_snapshot.byte_size !=
            kParentArtifactBytes ||
        parent_snapshot.sha256 !=
            bundle::kParentArtifactSha256) {
        fail("neutral_source_identity_mismatch");
    }
}

void require_same_sources(
    const RunReport& report,
    const integrity::RegularFileSnapshot& executable,
    const integrity::RegularFileSnapshot& bundle_snapshot,
    const integrity::RegularFileSnapshot& parent) {
    if (report.executable_before != executable ||
        report.bundle_before != bundle_snapshot ||
        report.parent_before != parent) {
        fail("neutral_source_changed_during_publication");
    }
}

testing::Recipe production_recipe(
    const std::filesystem::path& executable,
    std::string_view producer_commit) {
    return {
        .executable_path = executable,
        .bundle_path =
            std::filesystem::path(bundle::kArtifactPath),
        .parent_path =
            std::filesystem::path(
                generator::kParentArtifactPath),
        .destination_path =
            artifact::production_artifact_path(),
        .temporary_path =
            artifact::production_temporary_path(),
        .producer_commit =
            std::string(producer_commit),
    };
}

testing::Dependencies production_dependencies() {
    return {
        .snapshot =
            [](const std::filesystem::path& path) {
                return integrity::snapshot_regular_file(path);
            },
        .path_absent =
            [](const std::filesystem::path& path) {
                return path_absent(path);
            },
        .materialize =
            [](const std::filesystem::path& executable,
               std::string_view commit) {
                return generator::
                    materialize_neutral_supplement(
                        executable, commit);
            },
        .validate_materialized =
            [](const artifact::Artifact& value) {
                artifact::validate(value);
            },
        .publish =
            [](const artifact::Artifact& value) {
                return artifact::
                    publish_atomic_no_replace(value);
            },
        .reload =
            [](const artifact::Contract& contract,
               const artifact::FileIdentity& identity) {
                return artifact::load_published(
                    contract, identity);
            },
    };
}

void write_report(
    const RunReport& report, std::ostream& output) {
    const auto& manifest =
        report.materialized.manifest;
    std::uint64_t distinct = 0;
    std::uint64_t nondistinct = 0;
    for (const auto& split :
         manifest.accounting.distinct_hidden_controls) {
        for (const std::uint16_t count : split) {
            distinct += count;
        }
    }
    for (const auto& split :
         manifest.accounting
             .nondistinct_hidden_controls) {
        for (const std::uint16_t count : split) {
            nondistinct += count;
        }
    }
    output
        << "schema=" << artifact::kSchema
        << " result=PUBLISHED"
        << " artifact_bytes="
        << report.publication.artifact.bytes
        << " artifact_sha256="
        << report.publication.artifact.sha256
        << " selected_order_sha256="
        << bundle::format_sha256(
               manifest.selected_order_sha256)
        << " rows=" << report.materialized.rows.size()
        << " canonical_score_calls="
        << manifest.accounting
               .canonical_neutral.score_calls
        << " hidden_score_calls="
        << manifest.accounting.hidden_clone.score_calls
        << " distinct_hidden_controls=" << distinct
        << " nondistinct_hidden_controls="
        << nondistinct
        << " fits=" << manifest.accounting.fits
        << " candidate_rollouts="
        << manifest.accounting
               .candidate_rollout_evaluations
        << " gameplay_seeds="
        << manifest.accounting
               .gameplay_evaluation_seeds
        << '\n';
}

} // namespace

namespace testing {

RunReport publish(
    const Recipe& recipe,
    const Dependencies& dependencies) {
    validate_recipe(recipe);
    validate_dependencies(dependencies);
    require_absent(recipe, dependencies);

    RunReport result;
    result.executable_before =
        dependencies.snapshot(recipe.executable_path);
    result.bundle_before =
        dependencies.snapshot(recipe.bundle_path);
    result.parent_before =
        dependencies.snapshot(recipe.parent_path);
    require_source_identity(
        result.bundle_before, result.parent_before);

    result.materialized =
        dependencies.materialize(
            recipe.executable_path,
            recipe.producer_commit);
    dependencies.validate_materialized(
        result.materialized);
    if (result.materialized.manifest.producer_commit !=
            recipe.producer_commit ||
        result.materialized.manifest
                .producer_executable_sha256 !=
            bundle::parse_sha256(
                result.executable_before.sha256)) {
        fail("neutral_materializer_provenance_mismatch");
    }

    require_same_sources(
        result,
        dependencies.snapshot(recipe.executable_path),
        dependencies.snapshot(recipe.bundle_path),
        dependencies.snapshot(recipe.parent_path));
    require_absent(recipe, dependencies);

    result.publication =
        dependencies.publish(result.materialized);
    result.artifact_published =
        dependencies.snapshot(recipe.destination_path);
    if (result.publication.artifact.bytes == 0 ||
        result.publication.artifact.bytes !=
            result.artifact_published.byte_size ||
        result.publication.artifact.sha256 !=
            result.artifact_published.sha256 ||
        result.publication.manifest !=
            result.materialized.manifest) {
        fail("neutral_published_artifact_identity_mismatch");
    }

    const artifact::Artifact reloaded =
        dependencies.reload(
            result.materialized.manifest.contract,
            result.publication.artifact);
    result.artifact_reloaded =
        dependencies.snapshot(recipe.destination_path);
    if (reloaded != result.materialized ||
        result.artifact_reloaded !=
            result.artifact_published ||
        !dependencies.path_absent(recipe.temporary_path)) {
        fail("neutral_reloaded_artifact_mismatch");
    }

    result.executable_after =
        dependencies.snapshot(recipe.executable_path);
    result.bundle_after =
        dependencies.snapshot(recipe.bundle_path);
    result.parent_after =
        dependencies.snapshot(recipe.parent_path);
    require_same_sources(
        result, result.executable_after,
        result.bundle_after, result.parent_after);
    if (dependencies.path_absent(
            recipe.destination_path) ||
        !dependencies.path_absent(
            recipe.temporary_path)) {
        fail("neutral_published_coordinate_mismatch");
    }
    return result;
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, std::string_view producer_commit,
    const FixedPublisher& publisher) {
    if (argc != 1 || argv == nullptr ||
        argv[0] == nullptr ||
        std::string_view(argv[0]).empty() ||
        !canonical_commit(producer_commit) ||
        !publisher) {
        error
            << "Usage: old-school-fq4-dev5-neutral-publish\n";
        return 2;
    }
    try {
        const RunReport report =
            publisher(
                std::filesystem::path(argv[0]),
                producer_commit);
        write_report(report, output);
        output.flush();
        return output.good() ? 0 : 2;
    } catch (const std::exception&) {
        error
            << "result=ERROR"
               " reason=fixed_neutral_publication_failed\n";
        return 2;
    }
}

} // namespace testing

RunReport publish_fixed(
    const std::filesystem::path& executable,
    std::string_view producer_commit) {
    return testing::publish(
        production_recipe(executable, producer_commit),
        production_dependencies());
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, std::string_view producer_commit) {
    return testing::run_cli(
        argc, argv, output, error, producer_commit,
        [](const std::filesystem::path& executable,
           std::string_view commit) {
            return publish_fixed(executable, commit);
        });
}

} // namespace old_school::fq4_neutral_publisher
