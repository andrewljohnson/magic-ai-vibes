#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_neutral_publisher.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace bundle = old_school::fq4_dev_bundle;
namespace integrity = old_school::artifact_integrity;
namespace neutral =
    old_school::fq4_neutral_supplement;
namespace publisher =
    old_school::fq4_neutral_publisher;

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, "
                  << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

std::string digest(char character) {
    return std::string(64, character);
}

integrity::RegularFileSnapshot snapshot(
    const std::filesystem::path& path,
    std::uintmax_t bytes, const std::string& sha256) {
    return {
        .path = path.string(),
        .physical_path = path.string(),
        .byte_size = bytes,
        .sha256 = sha256,
        .device = 7,
        .inode = 11,
        .link_count = 1,
        .modification_seconds = 13,
        .modification_nanoseconds = 17,
        .change_seconds = 19,
        .change_nanoseconds = 23,
    };
}

struct Fixture {
    std::filesystem::path executable =
        "private-executable-name";
    std::filesystem::path bundle_path =
        "private-bundle-name";
    std::filesystem::path parent_path =
        "private-parent-name";
    std::filesystem::path destination =
        "private-destination-name";
    std::filesystem::path temporary =
        neutral::testing::temporary_path_for(
            destination);
    std::string commit = std::string(40, 'a');

    neutral::Artifact materialized;
    neutral::PublicationReport publication;

    bool destination_exists = false;
    bool temporary_exists = false;
    bool bad_initial_bundle = false;
    bool drift_bundle = false;
    bool wrong_materialized_commit = false;
    bool wrong_materialized_executable = false;
    bool wrong_publication_identity = false;
    bool wrong_reloaded_artifact = false;
    bool drift_published_file = false;
    bool leave_temporary_after_publish = false;

    std::size_t executable_snapshots = 0;
    std::size_t bundle_snapshots = 0;
    std::size_t parent_snapshots = 0;
    std::size_t artifact_snapshots = 0;
    std::size_t materialize_calls = 0;
    std::size_t validate_calls = 0;
    std::size_t publish_calls = 0;
    std::size_t reload_calls = 0;
    std::vector<std::string> events;

    Fixture() {
        materialized.manifest.producer_commit = commit;
        materialized.manifest
            .producer_executable_sha256 =
                bundle::parse_sha256(digest('9'));
        materialized.manifest
            .selected_order_sha256 =
                bundle::parse_sha256(digest('7'));
        materialized.manifest.accounting
            .canonical_neutral.score_calls = 41;
        materialized.manifest.accounting
            .hidden_clone.score_calls = 43;
        materialized.manifest.accounting.fits = 0;
        materialized.manifest.accounting
            .candidate_rollout_evaluations = 0;
        materialized.manifest.accounting
            .gameplay_evaluation_seeds = 0;
        materialized.manifest.accounting
            .distinct_hidden_controls = {{
                {{1, 2, 3, 4, 5}},
                {{6, 7, 8, 9, 10}},
            }};
        materialized.manifest.accounting
            .nondistinct_hidden_controls = {{
                {{10, 9, 8, 7, 6}},
                {{5, 4, 3, 2, 1}},
            }};
        materialized.rows.resize(3);
        publication = {
            .artifact = {
                .bytes = 107,
                .sha256 = digest('d'),
            },
            .manifest = materialized.manifest,
        };
    }

    publisher::testing::Recipe recipe() const {
        return {
            .executable_path = executable,
            .bundle_path = bundle_path,
            .parent_path = parent_path,
            .destination_path = destination,
            .temporary_path = temporary,
            .producer_commit = commit,
        };
    }

    publisher::testing::Dependencies dependencies() {
        publisher::testing::Dependencies result;
        result.snapshot =
            [this](const std::filesystem::path& path) {
                if (path == executable) {
                    events.push_back(
                        "snapshot:executable");
                    ++executable_snapshots;
                    return snapshot(
                        path, 97, digest('9'));
                }
                if (path == bundle_path) {
                    events.push_back("snapshot:bundle");
                    ++bundle_snapshots;
                    const bool wrong =
                        (bad_initial_bundle &&
                         bundle_snapshots == 1) ||
                        (drift_bundle &&
                         bundle_snapshots > 1);
                    return snapshot(
                        path,
                        bundle::kPublishedArtifactBytes,
                        wrong
                            ? digest('8')
                            : std::string(
                                  bundle::
                                      kPublishedArtifactSha256));
                }
                if (path == parent_path) {
                    events.push_back("snapshot:parent");
                    ++parent_snapshots;
                    return snapshot(
                        path, 3'111'437,
                        std::string(
                            bundle::
                                kParentArtifactSha256));
                }
                if (path == destination &&
                    destination_exists) {
                    events.push_back(
                        "snapshot:destination");
                    ++artifact_snapshots;
                    return snapshot(
                        path, publication.artifact.bytes,
                        drift_published_file &&
                                artifact_snapshots > 1
                            ? digest('e')
                            : publication.artifact
                                  .sha256);
                }
                throw std::runtime_error(
                    "unexpected synthetic snapshot");
            };
        result.path_absent =
            [this](const std::filesystem::path& path) {
                if (path == destination) {
                    events.push_back(
                        "absent:destination");
                    return !destination_exists;
                }
                if (path == temporary) {
                    events.push_back(
                        "absent:temporary");
                    return !temporary_exists;
                }
                throw std::runtime_error(
                    "unexpected synthetic absence check");
            };
        result.materialize =
            [this](
                const std::filesystem::path& path,
                std::string_view supplied_commit) {
                events.push_back("materialize");
                ++materialize_calls;
                expect(
                    path == executable &&
                        supplied_commit == commit,
                    "materializer received the wrong source");
                neutral::Artifact result = materialized;
                if (wrong_materialized_commit) {
                    result.manifest.producer_commit =
                        std::string(40, 'b');
                }
                if (wrong_materialized_executable) {
                    result.manifest
                        .producer_executable_sha256 =
                            bundle::parse_sha256(
                                digest('8'));
                }
                return result;
            };
        result.validate_materialized =
            [this](const neutral::Artifact&) {
                events.push_back("validate");
                ++validate_calls;
            };
        result.publish =
            [this](const neutral::Artifact& supplied) {
                events.push_back("publish");
                ++publish_calls;
                expect(
                    supplied.manifest ==
                        materialized.manifest ||
                        wrong_materialized_commit ||
                        wrong_materialized_executable,
                    "publisher received the wrong artifact");
                destination_exists = true;
                temporary_exists =
                    leave_temporary_after_publish;
                neutral::PublicationReport result =
                    publication;
                result.manifest = supplied.manifest;
                if (wrong_publication_identity) {
                    result.artifact.sha256 =
                        digest('e');
                }
                return result;
            };
        result.reload =
            [this](
                const neutral::Contract& contract,
                const neutral::FileIdentity& identity) {
                events.push_back("reload");
                ++reload_calls;
                expect(
                    contract ==
                            materialized
                                .manifest.contract &&
                        identity == publication.artifact,
                    "reloader received the wrong identity");
                neutral::Artifact result =
                    materialized;
                if (wrong_reloaded_artifact) {
                    result.rows.push_back(
                        neutral::NeutralRow{});
                }
                return result;
            };
        return result;
    }
};

void test_coordinates_fail_before_source_work() {
    for (const bool occupy_destination :
         {true, false}) {
        Fixture fixture;
        fixture.destination_exists =
            occupy_destination;
        fixture.temporary_exists =
            !occupy_destination;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "occupied publication coordinate was accepted");
        expect(
            fixture.executable_snapshots == 0 &&
                fixture.bundle_snapshots == 0 &&
                fixture.parent_snapshots == 0 &&
                fixture.materialize_calls == 0 &&
                fixture.validate_calls == 0 &&
                fixture.publish_calls == 0,
            "coordinate preflight ran after source work");
    }
}

void test_source_identity_and_drift_fail_closed() {
    {
        Fixture fixture;
        fixture.bad_initial_bundle = true;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "wrong frozen source identity was accepted");
        expect(
            fixture.materialize_calls == 0 &&
                fixture.validate_calls == 0 &&
                fixture.publish_calls == 0,
            "wrong source identity reached materialization");
    }
    {
        Fixture fixture;
        fixture.drift_bundle = true;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "source drift was accepted");
        expect(
            fixture.materialize_calls == 1 &&
                fixture.validate_calls == 1 &&
                fixture.publish_calls == 0 &&
                !fixture.destination_exists,
            "source drift did not fail before publication");
    }
}

void test_materializer_provenance_fails_before_publish() {
    for (const bool wrong_commit : {true, false}) {
        Fixture fixture;
        fixture.wrong_materialized_commit =
            wrong_commit;
        fixture.wrong_materialized_executable =
            !wrong_commit;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "materializer provenance mismatch accepted");
        expect(
            fixture.materialize_calls == 1 &&
                fixture.validate_calls == 1 &&
                fixture.publish_calls == 0 &&
                !fixture.destination_exists,
            "provenance mismatch reached publication");
    }
}

void test_publication_and_reload_tampering_fail_closed() {
    {
        Fixture fixture;
        fixture.wrong_publication_identity = true;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "publication identity mismatch accepted");
        expect(
            fixture.publish_calls == 1 &&
                fixture.reload_calls == 0,
            "publication mismatch reached reload");
    }
    {
        Fixture fixture;
        fixture.wrong_reloaded_artifact = true;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "reloaded artifact mismatch accepted");
        expect(
            fixture.publish_calls == 1 &&
                fixture.reload_calls == 1,
            "reload mismatch did not exercise reload");
    }
    {
        Fixture fixture;
        fixture.drift_published_file = true;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "published-file drift was accepted");
        expect(
            fixture.publish_calls == 1 &&
                fixture.reload_calls == 1,
            "published-file drift was not checked after reload");
    }
    {
        Fixture fixture;
        fixture.leave_temporary_after_publish = true;
        auto dependencies = fixture.dependencies();
        expect_rejected(
            [&] {
                static_cast<void>(
                    publisher::testing::publish(
                        fixture.recipe(),
                        dependencies));
            },
            "leftover temporary coordinate was accepted");
        expect(
            fixture.publish_calls == 1 &&
                fixture.reload_calls == 1,
            "temporary-coordinate failure happened too early");
    }
}

void test_success_has_exact_fail_closed_order() {
    Fixture fixture;
    auto dependencies = fixture.dependencies();
    const publisher::RunReport report =
        publisher::testing::publish(
            fixture.recipe(), dependencies);
    const std::vector<std::string> expected_events{
        "absent:destination",
        "absent:temporary",
        "snapshot:executable",
        "snapshot:bundle",
        "snapshot:parent",
        "materialize",
        "validate",
        "snapshot:executable",
        "snapshot:bundle",
        "snapshot:parent",
        "absent:destination",
        "absent:temporary",
        "publish",
        "snapshot:destination",
        "reload",
        "snapshot:destination",
        "absent:temporary",
        "snapshot:executable",
        "snapshot:bundle",
        "snapshot:parent",
        "absent:destination",
        "absent:temporary",
    };
    expect(
        fixture.events == expected_events,
        "publisher boundary ordering drifted");
    expect(
        fixture.executable_snapshots == 3 &&
            fixture.bundle_snapshots == 3 &&
            fixture.parent_snapshots == 3 &&
            fixture.artifact_snapshots == 2 &&
            fixture.materialize_calls == 1 &&
            fixture.validate_calls == 1 &&
            fixture.publish_calls == 1 &&
            fixture.reload_calls == 1,
        "success used the wrong number of boundary calls");
    expect(
        report.executable_before ==
                report.executable_after &&
            report.bundle_before ==
                report.bundle_after &&
            report.parent_before ==
                report.parent_after &&
            report.artifact_published ==
                report.artifact_reloaded &&
            report.materialized ==
                fixture.materialized &&
            report.publication ==
                fixture.publication,
        "success report lost exact identities");
    expect(
        fixture.destination_exists &&
            !fixture.temporary_exists,
        "success left invalid publication coordinates");
}

void test_cli_requires_no_arguments() {
    std::size_t calls = 0;
    const publisher::testing::FixedPublisher fixed =
        [&calls](
            const std::filesystem::path&,
            std::string_view) {
            ++calls;
            return publisher::RunReport{};
        };
    char program[] = "neutral-publisher";
    char extra[] = "unexpected";
    char* argv[] = {program, extra};
    std::ostringstream output;
    std::ostringstream error;
    const int code = publisher::testing::run_cli(
        2, argv, output, error, std::string(40, 'a'),
        fixed);
    expect(
        code == 2 && calls == 0 &&
            output.str().empty() &&
            error.str() ==
                "Usage: "
                "old-school-fq4-dev5-neutral-publish\n",
        "CLI accepted an argument or emitted non-generic usage");
}

void test_cli_no_argument_output_is_aggregate_only() {
    Fixture fixture;
    std::size_t calls = 0;
    const publisher::testing::FixedPublisher fixed =
        [&](const std::filesystem::path& executable,
            std::string_view commit) {
            ++calls;
            expect(
                executable ==
                        std::filesystem::path(
                            "private-cli-executable") &&
                    commit == fixture.commit,
                "CLI changed fixed publisher inputs");
            publisher::RunReport result;
            result.materialized = fixture.materialized;
            result.publication = fixture.publication;
            return result;
        };
    char program[] = "private-cli-executable";
    char* argv[] = {program};
    std::ostringstream output;
    std::ostringstream error;
    const int code = publisher::testing::run_cli(
        1, argv, output, error, fixture.commit, fixed);
    const std::string expected =
        "schema=" + std::string(neutral::kSchema) +
        " result=PUBLISHED"
        " artifact_bytes=107"
        " artifact_sha256=" + digest('d') +
        " selected_order_sha256=" + digest('7') +
        " rows=3"
        " canonical_score_calls=41"
        " hidden_score_calls=43"
        " distinct_hidden_controls=55"
        " nondistinct_hidden_controls=55"
        " fits=0"
        " candidate_rollouts=0"
        " gameplay_seeds=0\n";
    expect(
        code == 0 && calls == 1 &&
            error.str().empty() &&
            output.str() == expected,
        "no-argument CLI output was not exact aggregate output");
    expect(
        output.str().find(fixture.commit) ==
                std::string::npos &&
            output.str().find(
                fixture.executable.string()) ==
                std::string::npos &&
            output.str().find(
                fixture.bundle_path.string()) ==
                std::string::npos &&
            output.str().find(
                fixture.parent_path.string()) ==
                std::string::npos &&
            output.str().find("hand") ==
                std::string::npos &&
            output.str().find("card") ==
                std::string::npos &&
            output.str().find("descriptor") ==
                std::string::npos,
        "CLI output leaked private or row-level data");
}

void test_cli_suppresses_failure_details() {
    const publisher::testing::FixedPublisher fixed =
        [](
            const std::filesystem::path&,
            std::string_view) -> publisher::RunReport {
            throw std::runtime_error(
                "private source and hand contents");
        };
    char program[] = "neutral-publisher";
    char* argv[] = {program};
    std::ostringstream output;
    std::ostringstream error;
    const int code = publisher::testing::run_cli(
        1, argv, output, error, std::string(40, 'a'),
        fixed);
    expect(
        code == 2 && output.str().empty() &&
            error.str() ==
                "result=ERROR"
                " reason="
                "fixed_neutral_publication_failed\n",
        "CLI failure leaked exception details");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "coordinates fail before source work",
        test_coordinates_fail_before_source_work);
    tests.run(
        "source identity and drift fail closed",
        test_source_identity_and_drift_fail_closed);
    tests.run(
        "materializer provenance fails before publish",
        test_materializer_provenance_fails_before_publish);
    tests.run(
        "publication and reload tampering fail closed",
        test_publication_and_reload_tampering_fail_closed);
    tests.run(
        "success has exact fail-closed order",
        test_success_has_exact_fail_closed_order);
    tests.run(
        "CLI requires no arguments",
        test_cli_requires_no_arguments);
    tests.run(
        "CLI no-argument output is aggregate only",
        test_cli_no_argument_output_is_aggregate_only);
    tests.run(
        "CLI suppresses failure details",
        test_cli_suppresses_failure_details);
    return tests.finish();
}
