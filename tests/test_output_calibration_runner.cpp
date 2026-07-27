#include "old_school/output_calibration_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

namespace oc = old_school::output_calibration;

class TestRunner {
  public:
    void run(
        std::string_view name,
        const std::function<void()>& test) {
        try {
            test();
            ++passed_;
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr << failed_ << " test(s) failed; "
                      << passed_ << " passed\n";
            return 1;
        }
        std::cout << passed_
                  << " output-calibration-runner tests passed\n";
        return 0;
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
void expect_throws_contains(
    Function&& function, std::string_view expected) {
    try {
        std::forward<Function>(function)();
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).find(expected) !=
            std::string_view::npos) {
            return;
        }
        throw std::runtime_error(
            "exception did not contain '" +
            std::string(expected) + "': " + error.what());
    }
    throw std::runtime_error(
        "operation did not throw '" +
        std::string(expected) + "'");
}

inline constexpr std::uint64_t kSyntheticParentSeed =
    0x0C1A770001ULL;
inline constexpr std::uint64_t kSyntheticFitSeed =
    0x0C1A770002ULL;
inline constexpr std::uint64_t kSyntheticHoldoutSeed =
    0x0C1A770003ULL;

std::filesystem::path test_directory() {
    return "build/test-output-calibration-runner";
}

void remove_file(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

struct ParentFixture {
    std::filesystem::path path;
    oc::ParentArtifactIdentity identity;
};

const ParentFixture& parent_fixture() {
    static const ParentFixture fixture = [] {
        const std::filesystem::path path =
            test_directory() / "synthetic-parent.bin";
        remove_file(path);
        const auto parent =
            old_school::
                train_learned_value_challenger_artifact(
                    1, kSyntheticParentSeed, 1);
        old_school::
            write_learned_value_challenger_artifact_atomic(
                path.string(), parent);
        const auto snapshot =
            old_school::artifact_integrity::
                snapshot_regular_file(path);
        return ParentFixture{
            .path = path,
            .identity =
                {
                    .byte_size = snapshot.byte_size,
                    .sha256 = snapshot.sha256,
                    .model_fingerprint =
                        old_school::
                            learned_model_fingerprint(
                                parent.model()),
                    .training_games = 1,
                    .training_seed =
                        kSyntheticParentSeed,
                    .generations = 1,
                },
        };
    }();
    return fixture;
}

oc::testing::Recipe synthetic_recipe(
    const std::filesystem::path& output) {
    const oc::CollectionConfig fit{
        .seed = kSyntheticFitSeed,
        .generation = 3,
        .balanced_blocks = 1,
        .max_game_turns = 1,
        .pilot_training_games = 1,
        .worker_count = 1,
    };
    const oc::CollectionConfig holdout{
        .seed = kSyntheticHoldoutSeed,
        .generation = 4,
        .balanced_blocks = 1,
        .max_game_turns = 1,
        .pilot_training_games = 1,
        .worker_count = 1,
    };
    constexpr std::size_t games =
        old_school::learned_iteration::
            kBalancedScheduleGames;
    return {
        .parent_path = parent_fixture().path.string(),
        .parent = parent_fixture().identity,
        .fit = fit,
        .holdout = holdout,
        .optimizer = {},
        .gate =
            {
                .expected_fit = fit,
                .expected_holdout = holdout,
                .expected_physical_games = games,
                .expected_perspectives_per_deck =
                    2 * games / old_school::kDeckCount,
                .deck_loss_guard =
                    oc::kDeckLossGuard,
                .other_deck_bias_guard =
                    oc::kOtherDeckBiasGuard,
                .material_bias_threshold =
                    oc::kMaterialBiasThreshold,
            },
        .output_path = output.string(),
    };
}

bool valid_digest(std::string_view digest) {
    if (digest.size() != 64) {
        return false;
    }
    for (const char character : digest) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

struct DeterministicFixture {
    oc::OutputCalibrationRunReport first;
    oc::OutputCalibrationRunReport second;
    std::string first_output;
    std::string second_output;
};

const DeterministicFixture& deterministic_fixture() {
    static const DeterministicFixture fixture = [] {
        const std::filesystem::path first_path =
            test_directory() / "deterministic-a.bin";
        const std::filesystem::path second_path =
            test_directory() / "deterministic-b.bin";
        remove_file(first_path);
        remove_file(second_path);
        std::ostringstream first_output;
        std::ostringstream second_output;
        const auto first = oc::testing::
            run_output_calibration(
                synthetic_recipe(first_path), first_output);
        const auto second = oc::testing::
            run_output_calibration(
                synthetic_recipe(second_path), second_output);
        return DeterministicFixture{
            .first = first,
            .second = second,
            .first_output = first_output.str(),
            .second_output = second_output.str(),
        };
    }();
    return fixture;
}

void test_injected_pipeline_is_deterministic() {
    const auto& fixture = deterministic_fixture();
    const auto& first = fixture.first;
    const auto& second = fixture.second;
    expect(
        first.artifact == second.artifact,
        "two injected runs changed the artifact report");
    expect(
        first.holdout_accounting ==
            second.holdout_accounting,
        "two injected runs changed holdout accounting");
    expect(
        first.holdout_hashes == second.holdout_hashes,
        "two injected runs changed holdout hashes");
    expect(
        first.scientific == second.scientific,
        "two injected runs changed scientific metrics");
    expect(
        first.integrity == second.integrity,
        "two injected runs changed integrity evidence");
    expect(
        first.gate == second.gate,
        "two injected runs changed the scientific verdict");
    expect(
        first.artifact_published.byte_size ==
                second.artifact_published.byte_size &&
            first.artifact_published.sha256 ==
                second.artifact_published.sha256,
        "two injected publications changed artifact bytes");
    expect(
        first.parent_before == first.parent_after &&
            second.parent_before == second.parent_after,
        "runner changed the synthetic parent");
    expect(
        first.artifact_published ==
                first.artifact_reloaded &&
            first.artifact_published ==
                first.artifact_after,
        "runner did not hold its first artifact immutable");
    expect(
        second.artifact_published ==
                second.artifact_reloaded &&
            second.artifact_published ==
                second.artifact_after,
        "runner did not hold its second artifact immutable");
    expect(
        first.integrity.passed(),
        "deterministic injected run failed integrity");
    expect(
        valid_digest(
            first.original_fit_parameters_hash) &&
            first.original_fit_parameters_hash ==
                first.repartitioned_fit_parameters_hash,
        "fit hidden parameter hashes are not equal digests");
    expect(
        valid_digest(
            first.original_scientific_report_hash) &&
            first.original_scientific_report_hash ==
                first.repartitioned_scientific_report_hash,
        "hidden scientific report hashes are not equal digests");
    expect(
        fixture.first_output.find("Pooled") !=
                std::string::npos &&
            fixture.first_output.find("Green") !=
                std::string::npos &&
            fixture.first_output.find("Red") !=
                std::string::npos &&
            fixture.first_output.find("Blue") !=
                std::string::npos &&
            fixture.first_output.find("White") !=
                std::string::npos &&
            fixture.first_output.find("RU Aggro") !=
                std::string::npos,
        "runner output omitted a scientific scope");
}

void test_no_overwrite_is_infrastructure_failure() {
    const auto& fixture = deterministic_fixture();
    const auto before =
        old_school::artifact_integrity::
            snapshot_regular_file(
                fixture.first.output_path);
    std::ostringstream output;
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::testing::run_output_calibration(
                    synthetic_recipe(
                        fixture.first.output_path),
                    output));
        },
        "already exists");
    const auto after =
        old_school::artifact_integrity::
            snapshot_regular_file(
                fixture.first.output_path);
    expect(
        before == after,
        "no-overwrite failure changed published bytes");
    expect(
        output.str().empty(),
        "no-overwrite preflight performed expensive work");
}

void test_bad_parent_fails_closed() {
    const std::filesystem::path output_path =
        test_directory() / "missing-parent-output.bin";
    remove_file(output_path);
    auto recipe = synthetic_recipe(output_path);
    recipe.parent_path =
        (test_directory() / "missing-parent.bin").string();
    remove_file(recipe.parent_path);
    std::ostringstream output;
    expect_throws_contains(
        [&] {
            static_cast<void>(
                oc::testing::run_output_calibration(
                    recipe, output));
        },
        "cannot inspect");
    expect(
        !std::filesystem::exists(output_path),
        "bad-parent failure published an artifact");
}

void test_exit_semantics() {
    oc::GateReport passing;
    passing.integrity_passed = true;
    passing.collection_accounting_exact = true;
    passing.passed = true;
    oc::GateReport rejected;
    rejected.integrity_passed = true;
    rejected.collection_accounting_exact = true;
    rejected.passed = false;
    oc::GateReport infrastructure_failure;
    expect(
        oc::output_calibration_exit_code(passing) == 0,
        "passing scientific gate did not map to exit 0");
    expect(
        oc::output_calibration_exit_code(rejected) == 1,
        "scientific rejection did not map to exit 1");
    expect(
        oc::output_calibration_exit_code(
            infrastructure_failure) == 2,
        "integrity failure did not map to exit 2");

    char program[] = "old-school-output-calibration";
    char first[] = "one";
    char second[] = "two";
    char* argv[] = {program, first, second};
    std::ostringstream output;
    std::ostringstream error;
    expect(
        oc::run_output_calibration_cli(
            3, argv, output, error) == 2,
        "invalid CLI shape did not map to exit 2");
    expect(
        error.str().find("Usage:") != std::string::npos,
        "invalid CLI shape did not print usage");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "injected pipeline is deterministic",
        test_injected_pipeline_is_deterministic);
    runner.run(
        "no overwrite is infrastructure failure",
        test_no_overwrite_is_infrastructure_failure);
    runner.run(
        "bad parent fails closed",
        test_bad_parent_fails_closed);
    runner.run("exit semantics", test_exit_semantics);
    return runner.finish();
}
