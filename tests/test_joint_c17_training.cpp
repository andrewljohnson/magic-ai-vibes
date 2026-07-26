#include "old_school/joint_c17_training.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace training = old_school::joint_c17_training;
namespace runner = old_school::joint_c17_runner;

namespace {

std::size_t tests_run = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void test(std::string_view name, Function&& function) {
    ++tests_run;
    try {
        function();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << name << ": "
                  << error.what() << '\n';
        throw;
    }
}

bool throws_with(
    const auto& function, std::string_view expected) {
    try {
        function();
    } catch (const std::exception& error) {
        return std::string_view(error.what()).find(expected) !=
               std::string_view::npos;
    }
    return false;
}

struct Fixture {
    std::filesystem::path root;
    old_school::LearnedJointC17Config config;
    std::filesystem::path parent;
    std::filesystem::path label_cache;
    std::filesystem::path target;

    Fixture() {
        root =
            std::filesystem::absolute(
                std::filesystem::path("build") /
                ("test-joint-c17-training-" +
                 std::to_string(
                     static_cast<unsigned long long>(
                         ::getpid()))))
                .lexically_normal();
        std::filesystem::remove_all(root);

        config.training_games = 1;
        config.parent_training_seed = 424242;
        config.parent_generations = 2;
        config.shard_seed = 0x7A17C18ULL;
        config.balanced_blocks = 1;
        config.max_game_turns = 12;

        const auto parent_artifact =
            old_school::
                train_learned_value_challenger_artifact(
                    config.training_games,
                    config.parent_training_seed,
                    config.parent_generations);
        config.required_parent_fingerprint =
            old_school::learned_model_fingerprint(
                parent_artifact.model());
        parent =
            root /
            std::filesystem::path(
                runner::kParentArtifactPath);
        target =
            root /
            std::filesystem::path(
                runner::kCanonicalArtifactPath);
        old_school::
            write_learned_value_challenger_artifact_atomic(
                parent.string(), parent_artifact);
        label_cache =
            root /
            std::filesystem::path(
                runner::kLabelCacheArtifactPath);
        std::filesystem::create_directories(
            label_cache.parent_path());
        std::filesystem::copy_file(
            std::filesystem::path(
                runner::kLabelCacheArtifactPath),
            label_cache);
    }

    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    training::testing::MiniatureTrainingRequest request()
        const {
        return {
            .logical_root = root,
            .config = config,
        };
    }
};

void write_sentinel(
    const std::filesystem::path& path,
    std::string_view text) {
    std::filesystem::create_directories(
        path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error(
            "cannot create sentinel fixture");
    }
    output.write(
        text.data(),
        static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error(
            "cannot write sentinel fixture");
    }
}

} // namespace

int main() {
    Fixture fixture;

    test("miniature publication is pinned and reload-identical", [&] {
        std::ostringstream progress;
        const auto result =
            training::testing::
                train_and_publish_miniature_joint_c17(
                    fixture.request(), progress);
        expect(result.gate.passed, "training gate did not pass");
        expect(
            result.publication_gate.passed,
            "publication gate did not pass");
        expect(
            result.publication.before.requested_path ==
                    runner::kCanonicalArtifactPath &&
                !result.publication.before.exists,
            "missing preflight evidence is not canonical");
        expect(
            result.publication.after.exists &&
                result.publication.after.regular_file &&
                result.publication.after.byte_size > 0 &&
                result.publication.after.sha256.size() == 64 &&
                result.publication.after.sha256_verified &&
                result.publication.atomic_no_replace_confirmed,
            "published byte evidence is incomplete");
        expect(
            result.published_after_reload.sha256 ==
                    result.publication.after.sha256 &&
                result.published_after_reload.byte_size ==
                    result.publication.after.byte_size,
            "published bytes changed across reload");
        expect(
            result.parent_before == result.parent_after,
            "parent changed during fit");
        expect(
            result.parent_before ==
                    result.parent_before_publication &&
                result.label_cache_before ==
                    result.label_cache_before_publication,
            "prerequisites changed before publication");
        expect(
            result.label_cache_before ==
                result.label_cache_after,
            "label cache changed during fit");
        expect(
            result.label_cache_before.byte_size ==
                    runner::kLabelCacheArtifactByteSize &&
                result.label_cache_before.sha256 ==
                    runner::kLabelCacheArtifactSha256,
            "label cache was not pinned before fit");
        expect(
            result.report.training_games ==
                    fixture.config.training_games &&
                result.report.parent_training_seed ==
                    fixture.config.parent_training_seed &&
                result.report.parent_generations ==
                    fixture.config.parent_generations &&
                result.report.shard_seed ==
                    fixture.config.shard_seed &&
                result.report.parent_fingerprint ==
                    fixture.config.required_parent_fingerprint,
            "returned report does not bind miniature request");
        expect(
            result.model_fingerprint_gate.passed &&
                !result.canonical_deployment_gate.has_value() &&
                !result.evaluation_integrity.has_value(),
            "miniature result crossed the production evaluation seam");
        expect(
            progress.str().find(
                "training publication verified and frozen") !=
                std::string::npos,
            "progress did not report verified publication");

        const auto independent =
            old_school::artifact_integrity::
                snapshot_regular_file(fixture.target);
        expect(
            independent.sha256 ==
                    result.publication.after.sha256 &&
                independent.byte_size ==
                    result.publication.after.byte_size,
            "publication did not retain true SHA-256 evidence");
    });

    test("second run cannot replace the frozen target", [&] {
        const auto before =
            old_school::artifact_integrity::
                snapshot_regular_file(fixture.target);
        std::ostringstream progress;
        expect(
            throws_with(
                [&] {
                    static_cast<void>(
                        training::testing::
                            train_and_publish_miniature_joint_c17(
                                fixture.request(), progress));
                },
                "target must be missing before training"),
            "existing target was not rejected");
        const auto after =
            old_school::artifact_integrity::
                snapshot_regular_file(fixture.target);
        expect(before == after, "existing target was changed");
        expect(
            progress.str().find("Training sealed") ==
                std::string::npos,
            "trainer ran after failed absence preflight");
    });

    test("file directory and symlink targets fail before training", [&] {
        const auto run_case =
            [&](std::string_view suffix,
                const auto& make_target,
                std::string_view expected_kind) {
                const std::filesystem::path root =
                    fixture.root.parent_path() /
                    (fixture.root.filename().string() + "-" +
                     std::string(suffix));
                std::filesystem::remove_all(root);
                const auto parent =
                    root /
                    std::filesystem::path(
                        runner::kParentArtifactPath);
                std::filesystem::create_directories(
                    parent.parent_path());
                std::filesystem::copy_file(
                    fixture.parent, parent);
                const auto label_cache =
                    root /
                    std::filesystem::path(
                        runner::kLabelCacheArtifactPath);
                std::filesystem::create_directories(
                    label_cache.parent_path());
                std::filesystem::copy_file(
                    fixture.label_cache, label_cache);
                const auto target =
                    root /
                    std::filesystem::path(
                        runner::kCanonicalArtifactPath);
                make_target(target, root);

                auto request = fixture.request();
                request.logical_root = root;
                std::ostringstream progress;
                expect(
                    throws_with(
                        [&] {
                            static_cast<void>(
                                training::testing::
                                    train_and_publish_miniature_joint_c17(
                                        request, progress));
                        },
                        expected_kind),
                    "nonmissing target kind was not rejected");
                expect(
                    progress.str().find("Training sealed") ==
                        std::string::npos,
                    "trainer ran for a nonmissing target");
                std::filesystem::remove_all(root);
            };

        run_case(
            "file",
            [](const auto& target, const auto&) {
                write_sentinel(target, "keep-file");
            },
            "regular file");
        run_case(
            "directory",
            [](const auto& target, const auto&) {
                std::filesystem::create_directories(target);
            },
            "directory");
        run_case(
            "symlink",
            [](const auto& target, const auto& root) {
                const auto sentinel = root / "sentinel.bin";
                write_sentinel(sentinel, "keep-link-target");
                std::filesystem::create_directories(
                    target.parent_path());
                std::filesystem::create_symlink(
                    sentinel, target);
            },
            "symbolic link");
    });

    test("parent coordinates and fingerprint fail closed", [&] {
        const std::filesystem::path alternate_root =
            fixture.root.parent_path() /
            (fixture.root.filename().string() + "-bad-parent");
        std::filesystem::remove_all(alternate_root);
        const auto alternate_parent =
            alternate_root /
            std::filesystem::path(
                runner::kParentArtifactPath);
        std::filesystem::create_directories(
            alternate_parent.parent_path());
        std::filesystem::copy_file(
            fixture.parent, alternate_parent);
        const auto alternate_labels =
            alternate_root /
            std::filesystem::path(
                runner::kLabelCacheArtifactPath);
        std::filesystem::create_directories(
            alternate_labels.parent_path());
        std::filesystem::copy_file(
            fixture.label_cache, alternate_labels);

        auto wrong_coordinates = fixture.request();
        wrong_coordinates.logical_root = alternate_root;
        ++wrong_coordinates.config.parent_training_seed;
        std::ostringstream coordinate_progress;
        expect(
            throws_with(
                [&] {
                    static_cast<void>(
                        training::testing::
                            train_and_publish_miniature_joint_c17(
                                wrong_coordinates,
                                coordinate_progress));
                },
                "seed mismatch"),
            "wrong parent coordinates were accepted");
        expect(
            coordinate_progress.str().find("Training sealed") ==
                std::string::npos,
            "trainer ran after parent-coordinate failure");

        auto wrong_fingerprint = fixture.request();
        wrong_fingerprint.logical_root = alternate_root;
        wrong_fingerprint.config.required_parent_fingerprint =
            std::string(64, 'a');
        std::ostringstream fingerprint_progress;
        expect(
            throws_with(
                [&] {
                    static_cast<void>(
                        training::testing::
                            train_and_publish_miniature_joint_c17(
                                wrong_fingerprint,
                                fingerprint_progress));
                },
                "coordinates or fingerprint mismatch"),
            "wrong parent fingerprint was accepted");
        expect(
            fingerprint_progress.str().find("Training sealed") ==
                std::string::npos,
            "trainer ran after parent-fingerprint failure");
        std::filesystem::remove_all(alternate_root);
    });

    test("miniature seam rejects production-sized recipes", [&] {
        auto request = fixture.request();
        request.config.training_games =
            runner::kCanonicalTrainingGames;
        std::ostringstream progress;
        expect(
            throws_with(
                [&] {
                    static_cast<void>(
                        training::testing::
                            train_and_publish_miniature_joint_c17(
                                request, progress));
                },
                "bounded nonreserved fixture recipe"),
            "production-sized recipe entered miniature seam");
        expect(
            progress.str().empty(),
            "invalid miniature request touched filesystem");
    });

    test("miniature seam quarantines every reserved seed role", [&] {
        const auto reject =
            [&](std::uint64_t reserved_seed,
                bool use_as_parent_seed) {
                auto request = fixture.request();
                if (use_as_parent_seed) {
                    request.config.parent_training_seed =
                        reserved_seed;
                } else {
                    request.config.shard_seed = reserved_seed;
                }
                std::ostringstream progress;
                expect(
                    throws_with(
                        [&] {
                            static_cast<void>(
                                training::testing::
                                    train_and_publish_miniature_joint_c17(
                                        request, progress));
                        },
                        "bounded nonreserved fixture recipe"),
                    "reserved seed entered miniature seam");
                expect(
                    progress.str().empty(),
                    "reserved miniature request touched filesystem");
            };
        for (const std::uint64_t seed : {
                 old_school::kLearnedJointC17ShardSeed,
                 old_school::kLearnedJointC17HoldoutSeed,
                 old_school::
                     kLearnedJointC17MatchedControlGameplaySeed,
                 old_school::
                     kLearnedJointC17FrozenC16GameplaySeed,
                 old_school::
                     kLearnedJointC17HandcodedGameplaySeed,
                 101ULL,
                 202ULL,
                 303ULL,
                 404ULL,
                 505ULL,
                 606ULL,
                 707ULL,
                 808ULL,
             }) {
            reject(seed, false);
            reject(seed, true);
        }
    });

    test("missing or mutated frozen labels fail before training", [&] {
        const auto run_case =
            [&](std::string_view suffix, bool create_labels,
                std::string_view expected) {
                const std::filesystem::path root =
                    fixture.root.parent_path() /
                    (fixture.root.filename().string() + "-" +
                     std::string(suffix));
                std::filesystem::remove_all(root);
                const auto parent =
                    root /
                    std::filesystem::path(
                        runner::kParentArtifactPath);
                std::filesystem::create_directories(
                    parent.parent_path());
                std::filesystem::copy_file(
                    fixture.parent, parent);
                if (create_labels) {
                    const auto labels =
                        root /
                        std::filesystem::path(
                            runner::kLabelCacheArtifactPath);
                    write_sentinel(labels, "mutated-labels");
                }
                auto request = fixture.request();
                request.logical_root = root;
                std::ostringstream progress;
                expect(
                    throws_with(
                        [&] {
                            static_cast<void>(
                                training::testing::
                                    train_and_publish_miniature_joint_c17(
                                        request, progress));
                        },
                        expected),
                    "bad label prerequisite was accepted");
                expect(
                    progress.str().find("Training sealed") ==
                        std::string::npos,
                    "trainer ran after label prerequisite failure");
                std::filesystem::remove_all(root);
            };

        run_case("missing-labels", false, "cannot inspect");
        run_case(
            "mutated-labels", true,
            "label-cache byte identity mismatch");
    });

    test("nonregular parent is rejected by true snapshotter", [&] {
        const std::filesystem::path root =
            fixture.root.parent_path() /
            (fixture.root.filename().string() +
             "-parent-symlink");
        std::filesystem::remove_all(root);
        const auto real_parent = root / "real-parent.bin";
        std::filesystem::create_directories(root);
        std::filesystem::copy_file(
            fixture.parent, real_parent);
        const auto logical_parent =
            root /
            std::filesystem::path(
                runner::kParentArtifactPath);
        std::filesystem::create_directories(
            logical_parent.parent_path());
        std::filesystem::create_symlink(
            real_parent, logical_parent);
        const auto label_cache =
            root /
            std::filesystem::path(
                runner::kLabelCacheArtifactPath);
        std::filesystem::create_directories(
            label_cache.parent_path());
        std::filesystem::copy_file(
            fixture.label_cache, label_cache);
        auto request = fixture.request();
        request.logical_root = root;
        std::ostringstream progress;
        expect(
            throws_with(
                [&] {
                    static_cast<void>(
                        training::testing::
                            train_and_publish_miniature_joint_c17(
                                request, progress));
                },
                "symlink"),
            "symlinked parent was accepted");
        expect(
            progress.str().find("Training sealed") ==
                std::string::npos,
            "trainer ran after parent snapshot failure");
        std::filesystem::remove_all(root);
    });

    std::cout << tests_run << " tests passed\n";
    return 0;
}
