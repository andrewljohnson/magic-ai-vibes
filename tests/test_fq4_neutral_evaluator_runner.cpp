#include "old_school/fq4_neutral_evaluator_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace bundle = old_school::fq4_dev_bundle;
namespace candidate =
    old_school::fq4_dev_candidate_artifact;
namespace evaluator =
    old_school::fq4_neutral_evaluator;
namespace neutral =
    old_school::fq4_neutral_supplement;
namespace runner =
    old_school::fq4_neutral_evaluator_runner;

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
            std::cerr
                << "FAIL " << name << ": "
                << error.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr
                << failed_ << " test(s) failed; "
                << passed_ << " passed\n";
            return 1;
        }
        std::cout
            << passed_
            << " FQ4 neutral evaluator runner tests passed\n";
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
            std::string(expected) + "': " +
            error.what());
    }
    throw std::runtime_error(
        "operation did not throw '" +
        std::string(expected) + "'");
}

old_school::artifact_integrity::RegularFileSnapshot
snapshot(
    const std::filesystem::path& path,
    std::uint64_t bytes, std::string sha256) {
    return {
        .path = path.string(),
        .physical_path = path.string(),
        .byte_size = bytes,
        .sha256 = std::move(sha256),
        .device = 7,
        .inode = 11,
        .link_count = 1,
        .modification_seconds = 13,
        .modification_nanoseconds = 17,
        .change_seconds = 19,
        .change_nanoseconds = 23,
    };
}

evaluator::Report passing_evaluation() {
    evaluator::Report report;
    report.parent_fingerprint =
        std::string(bundle::kParentModelFingerprint);
    report.positive_only_candidate_fingerprint =
        std::string(
            evaluator::
                kRequiredPositiveOnlyCandidateFingerprint);
    report.anchored_candidate_fingerprint =
        std::string(64, 'a');
    report.anchored_training.accounting
        .training_input_sha256 =
            std::string(64, 'b');
    auto& fit =
        report.anchored_evaluation.metrics.fit;
    auto& check =
        report.anchored_evaluation.metrics.check;
    fit.positive_roots = 88;
    fit.positive_options = 548;
    fit.repairs = 30;
    fit.candidate_support_violations
        .violating_roots = 3;
    fit.deck_balanced_target_to_parent_kl = 0.4;
    fit.deck_balanced_target_to_candidate_kl = 0.1;
    check.positive_roots = 94;
    check.positive_options = 571;
    check.repairs = 37;
    check.deck_balanced_target_to_parent_kl = 0.5;
    check.deck_balanced_target_to_candidate_kl = 0.05;
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        auto& fit_deck = fit.decks[deck];
        fit_deck.positive_roots = deck + 1;
        fit_deck.positive_options = 10 + deck;
        fit_deck.repairs = deck;
        fit_deck.target_to_parent_kl =
            0.2 + static_cast<double>(deck) / 100.0;
        fit_deck.target_to_candidate_kl =
            0.1 + static_cast<double>(deck) / 100.0;
        auto& check_deck = check.decks[deck];
        check_deck.positive_roots = deck + 2;
        check_deck.positive_options = 20 + deck;
        check_deck.repairs = deck + 1;
        check_deck.target_to_parent_kl =
            0.3 + static_cast<double>(deck) / 100.0;
        check_deck.target_to_candidate_kl =
            0.05 + static_cast<double>(deck) / 100.0;

        auto& neutral_deck =
            report.neutral_check.decks[deck];
        neutral_deck.rows = 32;
        neutral_deck.options = 64 + deck;
        neutral_deck
            .baseline_parent_to_candidate_kl =
                0.08 + static_cast<double>(deck) / 100.0;
        neutral_deck
            .anchored_parent_to_candidate_kl =
                0.02 + static_cast<double>(deck) / 1000.0;
        neutral_deck
            .baseline_exact_support_changes =
                4 + deck;
        neutral_deck
            .anchored_exact_support_changes =
                1;
    }
    report.neutral_check.rows = 160;
    report.neutral_check.options = 330;
    report.neutral_check.baseline_equal_deck_kl =
        0.10;
    report.neutral_check.anchored_equal_deck_kl =
        0.02;
    report.neutral_check
        .baseline_exact_support_changes = 30;
    report.neutral_check
        .anchored_exact_support_changes = 5;
    report.neutral_check.finite_probabilities = true;
    return report;
}

struct InjectedFixture {
    std::map<std::string, std::size_t> snapshots;
    bool drift_neutral_after = false;
    bool loaded_dev1 = false;
    bool prepared_dev1 = false;
    bool loaded_parent = false;
    bool loaded_candidate = false;
    bool made_contract = false;
    bool loaded_neutral = false;
    bool evaluated = false;
    evaluator::Report evaluation = passing_evaluation();

    runner::testing::Dependencies dependencies() {
        const runner::FixedCoordinates coordinates =
            runner::testing::fixed_coordinates();
        return {
            .snapshot =
                [this, coordinates](
                    const std::filesystem::path& path) {
                    const std::string key = path.string();
                    const std::size_t call =
                        ++snapshots[key];
                    if (path == coordinates.dev1.path) {
                        return snapshot(
                            path, coordinates.dev1.bytes,
                            coordinates.dev1.sha256);
                    }
                    if (path == coordinates.parent.path) {
                        return snapshot(
                            path, coordinates.parent.bytes,
                            coordinates.parent.sha256);
                    }
                    if (path ==
                        coordinates
                            .positive_candidate.path) {
                        return snapshot(
                            path,
                            coordinates
                                .positive_candidate.bytes,
                            coordinates
                                .positive_candidate.sha256);
                    }
                    if (path ==
                        coordinates
                            .neutral_artifact.path) {
                        return snapshot(
                            path,
                            coordinates.neutral_artifact.bytes,
                            drift_neutral_after &&
                                    call > 1
                                ? std::string(64, 'e')
                                : coordinates
                                      .neutral_artifact.sha256);
                    }
                    throw std::runtime_error(
                        "unexpected snapshot path");
                },
            .load_dev1 =
                [this, coordinates](
                    const std::filesystem::path& path) {
                    expect(
                        path == coordinates.dev1.path,
                        "DEV1 loader received a path override");
                    loaded_dev1 = true;
                    return bundle::Bundle{};
                },
            .prepare_dev1 =
                [this](const bundle::Bundle&) {
                    prepared_dev1 = true;
                    return old_school::fq4_dev_evaluator::
                        PreparedCorpus{};
                },
            .load_parent =
                [this, coordinates](
                    const std::filesystem::path& path) {
                    expect(
                        path == coordinates.parent.path,
                        "parent loader received a path override");
                    loaded_parent = true;
                    return std::shared_ptr<
                        const old_school::LearnedModel>{};
                },
            .load_positive_candidate =
                [this, coordinates](
                    const std::filesystem::path& path,
                    std::shared_ptr<
                        const old_school::LearnedModel>,
                    const candidate::Contract& contract,
                    const candidate::FileIdentity& identity) {
                    expect(
                        path ==
                            coordinates
                                .positive_candidate.path &&
                            contract ==
                                candidate::
                                    production_contract() &&
                            identity.bytes ==
                                runner::
                                    kPositiveCandidateBytes &&
                            identity.sha256 ==
                                runner::
                                    kPositiveCandidateSha256,
                        "candidate loader coordinates drifted");
                    loaded_candidate = true;
                    return std::shared_ptr<
                        const old_school::LearnedModel>{};
                },
            .make_neutral_contract =
                [this](const bundle::Manifest&) {
                    made_contract = true;
                    return neutral::Contract{};
                },
            .load_neutral =
                [this, coordinates](
                    const std::filesystem::path& path,
                    const neutral::Contract&,
                    const neutral::FileIdentity& identity) {
                    expect(
                        path ==
                                coordinates
                                    .neutral_artifact.path &&
                            identity.bytes ==
                                runner::
                                    kNeutralArtifactBytes &&
                            identity.sha256 ==
                                runner::
                                    kNeutralArtifactSha256,
                        "neutral loader did not receive its "
                        "literal pinned identity");
                    loaded_neutral = true;
                    return neutral::Artifact{};
                },
            .evaluate =
                [this](
                    const old_school::fq4_dev_evaluator::
                        PreparedCorpus&,
                    const neutral::Artifact&,
                    std::shared_ptr<
                        const old_school::LearnedModel>,
                    std::shared_ptr<
                        const old_school::LearnedModel>) {
                    evaluated = true;
                    return evaluation;
                },
        };
    }
};

void test_fixed_coordinates_are_exact() {
    const runner::FixedCoordinates& coordinates =
        runner::testing::fixed_coordinates();
    expect(
        coordinates.dev1.path ==
                std::filesystem::path(
                    bundle::kArtifactPath) &&
            coordinates.dev1.bytes ==
                bundle::kPublishedArtifactBytes &&
            coordinates.dev1.sha256 ==
                bundle::kPublishedArtifactSha256,
        "fixed DEV1 coordinates drifted");
    expect(
        coordinates.parent.path ==
                std::filesystem::path(
                    old_school::fq4_dev_evaluator::
                        kParentArtifactPath) &&
            coordinates.parent.bytes ==
                runner::kParentArtifactBytes &&
            coordinates.parent.sha256 ==
                bundle::kParentArtifactSha256,
        "fixed C16 coordinates drifted");
    expect(
        coordinates.positive_candidate.path ==
                candidate::production_artifact_path() &&
            coordinates.positive_candidate.bytes ==
                237'282 &&
            coordinates.positive_candidate.sha256 ==
                "aca8ba9c337a5b41d0cf624f7ec46ab6"
                "52c7bebc1b5c2c29fa844b900c467f63",
        "fixed positive-only candidate coordinates drifted");
    expect(
        coordinates.neutral_artifact.path ==
                neutral::production_artifact_path() &&
            coordinates.neutral_artifact.bytes ==
                661'475 &&
            coordinates.neutral_artifact.sha256 ==
                "47d94823f043971f6f9f0aa5f552bfae"
                "210af9615d8f6dc7392e52dad3eaa105",
        "fixed neutral coordinates drifted");
}

void test_success_uses_literal_pin_and_no_games() {
    InjectedFixture fixture;
    const runner::RunReport report =
        runner::testing::run_fixed(
            fixture.dependencies());
    expect(
        fixture.loaded_dev1 &&
            fixture.prepared_dev1 &&
            fixture.loaded_parent &&
            fixture.loaded_candidate &&
            fixture.made_contract &&
            fixture.loaded_neutral &&
            fixture.evaluated,
        "successful runner skipped an offline boundary");
    expect(
        report.neutral_identity.bytes ==
                runner::kNeutralArtifactBytes &&
            report.neutral_identity.sha256 ==
                runner::kNeutralArtifactSha256 &&
            report.neutral_before ==
                report.neutral_after,
        "neutral literal identity was not pinned");
    for (const auto& [path, calls] :
         fixture.snapshots) {
        static_cast<void>(path);
        expect(
            calls == 2,
            "a fixed source was not snapshotted twice");
    }
}

void test_source_drift_fails_closed() {
    InjectedFixture fixture;
    fixture.drift_neutral_after = true;
    expect_throws_contains(
        [&] {
            static_cast<void>(
                runner::testing::run_fixed(
                    fixture.dependencies()));
        },
        "fixed_source_changed_during_evaluation");
}

void test_gate_failure_fails_closed() {
    InjectedFixture fixture;
    fixture.evaluation.gate.failures.push_back(
        "private scientific detail");
    const runner::RunReport report =
        runner::testing::run_fixed(
            fixture.dependencies());
    expect(
        !report.evaluation.gate.passed() &&
            report.evaluation.gate.failures ==
                std::vector<std::string>{
                    "private scientific detail"},
        "scientific gate failure was discarded");
}

void test_cli_arguments_and_redaction() {
    char program[] = "neutral-evaluate";
    char override_argument[] = "--path=/private/input";
    char* invalid_argv[] = {
        program, override_argument,
    };
    bool called = false;
    std::ostringstream output;
    std::ostringstream error;
    expect(
        runner::testing::run_cli(
            2, invalid_argv, output, error,
            [&] {
                called = true;
                return runner::RunReport{};
            }) == 2 &&
            !called &&
            output.str().empty() &&
            error.str().find("Usage:") !=
                std::string::npos,
        "CLI accepted a recipe override");

    char* valid_argv[] = {program};
    output.str("");
    output.clear();
    error.str("");
    error.clear();
    expect(
        runner::testing::run_cli(
            1, valid_argv, output, error,
            []() -> runner::RunReport {
                throw std::runtime_error(
                    "secret path and scientific failure");
            }) == 2 &&
            output.str().empty() &&
            error.str() ==
                "result=ERROR"
                " reason=fixed_neutral_evaluation_failed\n" &&
            error.str().find("secret") ==
                std::string::npos,
        "CLI leaked an internal failure detail");
}

void test_cli_success_prints_concise_scopes() {
    char program[] = "neutral-evaluate";
    char* argv[] = {program};
    runner::RunReport report;
    report.evaluation = passing_evaluation();
    std::ostringstream output;
    std::ostringstream error;
    expect(
        runner::testing::run_cli(
            1, argv, output, error,
            [report] { return report; }) == 0 &&
            error.str().empty(),
        "successful CLI returned an error");
    const std::string text = output.str();
    expect(
        text.find("gate=PASS") !=
                std::string::npos &&
            text.find("neutral_sha256=") !=
                std::string::npos &&
            text.find("gates baseline_positive_contract_exact=") !=
                std::string::npos &&
            text.find("training_sha256=") !=
                std::string::npos &&
            text.find("positive split=FIT scope=pooled") !=
                std::string::npos &&
            text.find("positive split=CHECK scope=pooled") !=
                std::string::npos &&
            text.find("neutral scope=pooled") !=
                std::string::npos &&
            text.find("deck=Green") !=
                std::string::npos &&
            text.find("deck=Red") !=
                std::string::npos &&
            text.find("deck=Blue") !=
                std::string::npos &&
            text.find("deck=White") !=
                std::string::npos &&
            text.find("deck=RU Aggro") !=
                std::string::npos,
        "successful CLI omitted a required metric scope");
}

void test_cli_scientific_failure_retains_metrics() {
    char program[] = "neutral-evaluate";
    char* argv[] = {program};
    runner::RunReport report;
    report.evaluation = passing_evaluation();
    report.evaluation.gate.failures.push_back(
        "private scientific detail");
    std::ostringstream output;
    std::ostringstream error;
    expect(
        runner::testing::run_cli(
            1, argv, output, error,
            [report] { return report; }) == 1 &&
            error.str().empty(),
        "scientific failure did not receive its distinct exit");
    const std::string text = output.str();
    expect(
        text.find("gate=FAIL") !=
                std::string::npos &&
            text.find("positive split=FIT scope=pooled") !=
                std::string::npos &&
            text.find("positive split=CHECK deck=Green") !=
                std::string::npos &&
            text.find("neutral scope=pooled") !=
                std::string::npos &&
            text.find("neutral deck=RU Aggro") !=
                std::string::npos &&
            text.find("private scientific detail") ==
                std::string::npos,
        "scientific failure lost deck metrics or leaked details");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "fixed coordinates",
        test_fixed_coordinates_are_exact);
    tests.run(
        "literal pin and offline success",
        test_success_uses_literal_pin_and_no_games);
    tests.run(
        "source drift",
        test_source_drift_fails_closed);
    tests.run(
        "gate failure",
        test_gate_failure_fails_closed);
    tests.run(
        "CLI arguments and redaction",
        test_cli_arguments_and_redaction);
    tests.run(
        "CLI success output",
        test_cli_success_prints_concise_scopes);
    tests.run(
        "CLI scientific failure output",
        test_cli_scientific_failure_retains_metrics);
    return tests.finish();
}
