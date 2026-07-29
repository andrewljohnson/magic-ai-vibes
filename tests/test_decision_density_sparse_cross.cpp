#include "old_school/decision_density_sparse_cross.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq20 =
    old_school::decision_density_sparse_cross;
namespace aq19 =
    old_school::decision_density_bilinear;

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
            std::cout << "[FAIL] " << name << ": "
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

void expect_near(
    double actual, double expected, double tolerance,
    std::string_view message) {
    if (!std::isfinite(actual) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) + " actual=" +
            std::to_string(actual) + " expected=" +
            std::to_string(expected));
    }
}

std::size_t option_count(
    aq19::priority::WidthStratum width) {
    switch (width) {
    case aq19::priority::WidthStratum::B2:
        return 2;
    case aq19::priority::WidthStratum::B3:
        return 3;
    case aq19::priority::WidthStratum::B4Plus:
        return 4;
    }
    throw std::runtime_error(
        "synthetic width is invalid");
}

aq19::Root make_root(
    old_school::DeckId deck,
    aq19::priority::WidthStratum width,
    std::size_t serial,
    std::string group) {
    aq19::Root root{
        .stable_root_id =
            "aq20-root-" + std::to_string(serial),
        .physical_game_group = std::move(group),
        .deck = deck,
        .width = width,
    };
    root.state[0] = 1.0;
    root.state[1] =
        0.25 +
        0.05 * static_cast<double>(serial % 7);
    root.state[2] =
        (serial % 2 == 0) ? -0.75 : 0.5;
    const std::size_t count = option_count(width);
    for (std::size_t action = 0;
         action < count; ++action) {
        aq19::Option option{
            .canonical_ordinal = action,
            .base_aggregate_score =
                0.35 +
                0.015 *
                    static_cast<double>(action),
            .teacher_aggregate_score =
                0.22 +
                0.11 *
                    static_cast<double>(action) +
                0.003 *
                    static_cast<double>(
                        (serial + action) % 3),
        };
        const double primary =
            static_cast<double>(action) -
            static_cast<double>(serial % 3);
        option.action_features[0] = primary;
        option.action_features[1] = -primary;
        for (std::size_t feature = 2;
             feature < 24; ++feature) {
            const std::size_t modulus =
                2 + feature % 5;
            option.action_features[feature] =
                static_cast<double>(
                    ((serial + 1) *
                         (action + feature + 1)) %
                    modulus) -
                0.5 *
                    static_cast<double>(modulus);
        }
        for (std::size_t world = 0;
             world < aq19::labels::kWorlds; ++world) {
            option
                .common_world_teacher_samples[world] =
                std::clamp(
                    option.teacher_aggregate_score +
                        0.001 *
                            (static_cast<double>(world) -
                             3.5),
                    0.0, 1.0);
        }
        root.options.push_back(std::move(option));
    }
    aq19::validate_root(root);
    return root;
}

aq19::Dataset make_dataset(
    std::size_t repetitions = 4) {
    std::vector<aq19::Root> roots;
    std::size_t serial = 0;
    for (std::size_t repetition = 0;
         repetition < repetitions; ++repetition) {
        const std::string group =
            "aq20-group-" +
            std::to_string(repetition);
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            for (std::size_t width = 0;
                 width <
                     aq19::priority::kWidthStrata;
                 ++width) {
                roots.push_back(
                    make_root(
                        static_cast<
                            old_school::DeckId>(deck),
                        static_cast<
                            aq19::priority::
                                WidthStratum>(width),
                        serial++, group));
            }
        }
    }
    return aq19::testing::make_dataset(
        std::move(roots));
}

bool rows_bit_identical(
    const std::vector<std::vector<double>>& left,
    const std::vector<std::vector<double>>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t root = 0;
         root < left.size(); ++root) {
        if (left[root].size() !=
            right[root].size()) {
            return false;
        }
        for (std::size_t action = 0;
             action < left[root].size(); ++action) {
            if (std::bit_cast<std::uint64_t>(
                    left[root][action]) !=
                std::bit_cast<std::uint64_t>(
                    right[root][action])) {
                return false;
            }
        }
    }
    return true;
}

aq20::Metrics gate_metrics(
    double pair_bce, double listwise, double regret,
    double top_one, double stable_pair,
    double deck_regret) {
    aq20::Metrics metrics;
    metrics.pairs.equal_deck_pair_bce = pair_bce;
    metrics.ranking.equal_deck_listwise_cross_entropy =
        listwise;
    metrics.ranking.equal_deck_mean_regret = regret;
    metrics.ranking
        .equal_deck_top_one_expected_agreement = top_one;
    metrics.ranking.equal_deck_stable_pair_agreement =
        stable_pair;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        metrics.ranking.decks[deck].deck =
            static_cast<old_school::DeckId>(deck);
        metrics.ranking.decks[deck].mean_regret =
            deck_regret;
    }
    return metrics;
}

aq20::OofGateInputs passing_oof_gate_inputs() {
    aq20::OofGateInputs inputs{
        .c16_train =
            gate_metrics(
                0.51, 1.01, 0.11, 0.49, 0.49,
                0.11),
        .aq19_train =
            gate_metrics(
                0.50, 1.00, 0.10, 0.50, 0.50,
                0.10),
        .candidate_train =
            gate_metrics(
                0.49, 0.99, 0.09, 0.51, 0.51,
                0.09),
        .c16_oof =
            gate_metrics(
                0.51, 1.01, 0.11, 0.49, 0.49,
                0.11),
        .aq19_oof =
            gate_metrics(
                0.50, 1.00, 0.10, 0.50, 0.50,
                0.10),
        .candidate_oof =
            gate_metrics(
                0.4999, 0.99, 0.0994, 0.51, 0.51,
                0.09),
        .changed_exact_max_roots = 6,
        .frozen_inputs_exact = true,
        .support_identity_exact = true,
        .comparator_reproduced = true,
        .full_fit_complete = true,
        .fold_local_preparation = true,
        .grouped_oof_exact = true,
        .repeated_fits_bit_identical = true,
    };
    const aq20::Metrics fold_parent =
        gate_metrics(
            0.50, 1.00, 0.10, 0.50, 0.50,
            0.10);
    const aq20::Metrics fold_candidate =
        gate_metrics(
            0.49, 0.99, 0.09, 0.51, 0.51,
            0.09);
    inputs.aq19_folds.fill(fold_parent);
    inputs.candidate_folds.fill(fold_candidate);
    return inputs;
}

bool has_failure(
    const aq20::Gate& gate, std::string_view failure) {
    return std::find(
               gate.failures.begin(), gate.failures.end(),
               failure) != gate.failures.end();
}

void test_command_contract() {
    const std::array<std::string_view, 1> accepted{
        "--offline-report"};
    expect(
        aq20::parse_command(accepted),
        "offline report was not accepted");
    const std::array<std::string_view, 2> knob{
        "--offline-report", "--terms=8"};
    expect(
        !aq20::parse_command(knob),
        "an undeclared knob was accepted");
}

void test_finite_difference_and_positive_diagonal() {
    const aq19::Dataset dataset = make_dataset();
    const aq20::testing::ColumnSpec candidate{
        .state_feature = 1,
        .action_feature = 7,
        .sigma = 1.25,
    };
    const aq20::testing::DerivativeProbe analytic =
        aq20::testing::derivative_probe(
            dataset, {}, candidate);
    expect(
        analytic.diagonal > 0.0 &&
            std::isfinite(analytic.diagonal),
        "Gauss-Newton diagonal was not positive");
    constexpr double epsilon = 1.0e-6;
    const aq20::Term positive{
        .state_feature = candidate.state_feature,
        .action_feature = candidate.action_feature,
        .sigma = candidate.sigma,
        .beta = epsilon,
    };
    const aq20::Term negative{
        .state_feature = candidate.state_feature,
        .action_feature = candidate.action_feature,
        .sigma = candidate.sigma,
        .beta = -epsilon,
    };
    const double numerical =
        (aq20::testing::objective(
             dataset,
             std::span<const aq20::Term>(
                 &positive, 1)) -
         aq20::testing::objective(
             dataset,
             std::span<const aq20::Term>(
                 &negative, 1))) /
        (2.0 * epsilon);
    expect_near(
        analytic.gradient, numerical, 2.0e-8,
        "analytic gradient disagreed with finite "
        "difference");
}

void test_clipped_actual_improvement_ordering() {
    const aq20::StageDerivative clipped =
        aq20::coordinate_step(-1.0, 0.01);
    const aq20::StageDerivative ordinary =
        aq20::coordinate_step(-3.1, 2.0);
    expect(
        clipped.valid && clipped.clipped &&
            ordinary.valid && !ordinary.clipped,
        "synthetic clipping precondition failed");
    const double clipped_unapplied =
        -((-1.0 * 25.0) +
          0.5 * 0.01 * 25.0 * 25.0);
    const double ordinary_unapplied =
        -((-3.1 * ordinary.beta) +
          0.5 * 2.0 * ordinary.beta *
              ordinary.beta);
    expect(
        clipped_unapplied > ordinary_unapplied &&
            clipped.actual_improvement <
                ordinary.actual_improvement,
        "actual applied I did not reverse the "
        "unclipped ordering");
    const std::array<aq20::testing::StageCandidate, 2>
        candidates{{
            {
                .state_feature = 0,
                .action_feature = 0,
                .derivative = clipped,
            },
            {
                .state_feature = 1,
                .action_feature = 0,
                .derivative = ordinary,
            },
        }};
    expect(
        aq20::testing::select_stage_candidate(
            candidates) ==
            std::optional<std::size_t>{1},
        "production selection helper did not rank by "
        "actual applied I");
}

void test_sign_deduplication() {
    const aq19::Dataset dataset = make_dataset();
    const std::array<aq20::testing::ColumnSpec, 2>
        columns{{
            {
                .state_feature = 0,
                .action_feature = 0,
                .sigma = 1.0,
            },
            {
                .state_feature = 0,
                .action_feature = 1,
                .sigma = 1.0,
            },
        }};
    const auto prepared =
        aq20::testing::prepare_columns_probe(
            dataset, columns);
    expect(
        prepared.input_coordinates == 2 &&
            prepared.representatives == 1 &&
            prepared.coordinates ==
                std::vector<std::pair<
                    std::size_t,
                    std::size_t>>{{0, 0}},
        "sign-equivalent columns were not deduplicated "
        "to the smallest coordinate");
}

void test_label_blind_and_fold_local_preparation() {
    const aq19::Dataset original = make_dataset();
    const aq19::FoldAssignment folds =
        aq19::assign_grouped_folds(
            original, "synthetic-aq20-corpus");
    const aq19::Dataset fit_before =
        aq19::fold_training_dataset(
            original, folds, 0);
    aq19::Dataset mutated = original;
    for (aq19::Root& root : mutated.roots) {
        const auto assignment = std::find_if(
            folds.group_folds.begin(),
            folds.group_folds.end(),
            [&root](const auto& item) {
                return item.first ==
                       root.physical_game_group;
            });
        if (assignment !=
                folds.group_folds.end() &&
            assignment->second == 0) {
            root.state[1] += 99.0;
            for (aq19::Option& option :
                 root.options) {
                option.action_features[7] -=
                    77.0;
            }
        } else {
            for (aq19::Option& option :
                 root.options) {
                option.base_aggregate_score =
                    0.01;
                option.teacher_aggregate_score =
                    0.99;
                option
                    .common_world_teacher_samples
                    .fill(0.123);
            }
        }
    }
    const aq19::Dataset fit_after =
        aq19::fold_training_dataset(
            mutated, folds, 0);
    const std::array<aq20::testing::ColumnSpec, 2>
        columns{{
            {
                .state_feature = 0,
                .action_feature = 5,
                .sigma = 1.0,
            },
            {
                .state_feature = 1,
                .action_feature = 7,
                .sigma = 2.0,
            },
        }};
    const auto before =
        aq20::testing::prepare_columns_probe(
            fit_before, columns);
    const auto after =
        aq20::testing::prepare_columns_probe(
            fit_after, columns);
    const auto support_before =
        aq20::support::census_partition(
            "SYNTHETIC_FIT", fit_before);
    const auto support_after =
        aq20::support::census_partition(
            "SYNTHETIC_FIT", fit_after);
    expect(
        before == after &&
            support_before == support_after,
        "held-out features or fit labels changed "
        "fold-local eligibility or scale");
}

aq20::support::CensusReport exact_s0_shell() {
    aq20::support::CensusReport report{
        .fold_manifest =
            std::string(aq19::kRequiredFoldManifest),
        .cross_partition_digest =
            std::string(
                aq20::kRequiredSupportDigest),
        .every_partition_has_16_eligible = true,
    };
    for (std::size_t partition = 0;
         partition <
             aq20::support::kPartitionCount;
         ++partition) {
        report.partitions[partition]
            .active_coordinates =
            aq20::kRequiredActiveCoordinates[
                partition];
        report.partitions[partition]
            .eligible_coordinates =
            aq20::kRequiredEligibleCoordinates[
                partition];
        report.partitions[partition]
            .canonical_table_sha256 =
            aq20::kRequiredSupportTableSha256[
                partition];
    }
    return report;
}

void test_s0_drift_rejection() {
    auto report = exact_s0_shell();
    expect(
        aq20::exact_support_identity(report),
        "exact S0 identity was rejected");
    ++report.partitions[3].eligible_coordinates;
    expect(
        !aq20::exact_support_identity(report),
        "S0 count drift was accepted");
    report = exact_s0_shell();
    report.teacher_fields_read = 1;
    expect(
        !aq20::exact_support_identity(report),
        "S0 teacher-field read was accepted");
}

void test_repeat_identity_and_positive_zero() {
    const aq19::Dataset dataset = make_dataset();
    aq20::Terms terms;
    for (std::size_t term = 0;
         term < aq20::kTermCount; ++term) {
        terms.push_back({
            .state_feature = term % 3,
            .action_feature = term + 2,
            .sigma =
                1.0 +
                0.05 * static_cast<double>(term),
            .beta =
                term == 0
                    ? -0.0
                    : 0.01 *
                          static_cast<double>(term),
        });
    }
    const auto first =
        aq20::testing::residuals(dataset, terms);
    const auto second =
        aq20::testing::residuals(dataset, terms);
    expect(
        rows_bit_identical(first, second),
        "repeated residual scoring was not "
        "bit-identical");
    const old_school::LearnedPrioritySparseCross runtime(
        terms);
    std::vector<std::vector<double>> deployed;
    for (const aq19::Root& root : dataset.roots) {
        std::vector<
            old_school::
                LearnedPrioritySparseCrossAction>
            actions;
        for (const aq19::Option& option :
             root.options) {
            actions.push_back(option.action_features);
        }
        deployed.push_back(
            runtime.residuals(
                root.state, actions,
                aq19::canonical_option_order(root)));
    }
    expect(
        rows_bit_identical(first, deployed),
        "completed direct scorer drifted from runtime");
    const auto parent =
        aq20::testing::residuals(dataset, {});
    for (const auto& row : parent) {
        for (const double value : row) {
            expect(
                std::bit_cast<std::uint64_t>(value) ==
                    UINT64_C(0),
                "empty treatment did not return "
                "positive zero");
        }
    }
}

void test_repeated_fit_identity() {
    const aq19::Dataset dataset = make_dataset();
    std::vector<aq20::testing::ColumnSpec> columns;
    for (std::size_t coordinate = 0;
         coordinate < 22; ++coordinate) {
        columns.push_back({
            .state_feature = coordinate % 3,
            .action_feature = coordinate + 2,
            .sigma =
                1.0 +
                0.03 *
                    static_cast<double>(coordinate),
            .root_support =
                aq20::support::kMinimumRootSupport,
            .group_support =
                aq20::support::kMinimumGroupSupport,
            .maximum_group_leverage = 0.05,
        });
    }
    const aq20::FitReport first =
        aq20::testing::fit_with_columns(
            dataset, columns);
    const aq20::FitReport second =
        aq20::testing::fit_with_columns(
            dataset, columns);
    expect(
        first.completed && second.completed &&
            first.completed_stages ==
                aq20::kTermCount &&
            first.terms.size() ==
                aq20::kTermCount &&
            first.term_sha256 ==
                second.term_sha256 &&
            old_school::
                    learned_priority_sparse_cross_canonical_bytes(
                        first.terms) ==
                old_school::
                    learned_priority_sparse_cross_canonical_bytes(
                        second.terms) &&
            rows_bit_identical(
                first.residuals,
                second.residuals),
        "repeated synthetic sparse fits were not "
        "bit-identical");
}

void test_oof_gate_is_conjunctive_and_fail_closed() {
    const aq20::OofGateInputs passing =
        passing_oof_gate_inputs();
    const aq20::Gate accepted =
        aq20::evaluate_oof_gate(passing);
    expect(
        accepted.passed && accepted.failures.empty(),
        "fully conforming OOF evidence was rejected");

    aq20::OofGateInputs broken = passing;
    broken.candidate_oof.pairs.equal_deck_pair_bce =
        0.49998;
    broken.candidate_oof.ranking.equal_deck_mean_regret =
        0.0996;
    broken.candidate_oof.ranking.decks[0].mean_regret =
        0.101;
    broken.repeated_fits_bit_identical = false;
    const aq20::Gate rejected =
        aq20::evaluate_oof_gate(broken);
    expect(
        !rejected.passed &&
            has_failure(
                rejected,
                "OOF pair BCE improvement is below "
                "0.000025") &&
            has_failure(
                rejected,
                "OOF regret improvement is below 0.0005") &&
            has_failure(
                rejected,
                "OOF Green regret exceeds its better "
                "comparator") &&
            has_failure(
                rejected,
                "repeated full/fold fits were not "
                "bit-identical"),
        "OOF gate did not retain every failed conjunct");

    broken = passing;
    broken.candidate_oof.pairs.equal_deck_pair_bce =
        std::numeric_limits<double>::quiet_NaN();
    const aq20::Gate nonfinite =
        aq20::evaluate_oof_gate(broken);
    expect(
        !nonfinite.passed &&
            has_failure(
                nonfinite,
                "OOF pair BCE improvement is below "
                "0.000025"),
        "OOF gate did not fail closed on NaN evidence");
}

void test_dev_gate_is_conjunctive_and_fail_closed() {
    const aq20::DevGateInputs passing{
        .c16 =
            gate_metrics(
                0.50, 1.00, 0.10, 0.50, 0.50,
                0.10),
        .aq19 =
            gate_metrics(
                0.49, 0.99, 0.09, 0.51, 0.51,
                0.09),
        .candidate =
            gate_metrics(
                0.48, 0.98, 0.08, 0.52, 0.52,
                0.08),
    };
    const aq20::Gate accepted =
        aq20::evaluate_dev_gate(passing);
    expect(
        accepted.passed && accepted.failures.empty(),
        "fully conforming DEV evidence was rejected");

    aq20::DevGateInputs broken = passing;
    broken.candidate.pairs.equal_deck_pair_bce =
        0.495;
    broken.candidate.ranking
        .equal_deck_listwise_cross_entropy = 0.995;
    broken.candidate.ranking
        .decks[static_cast<std::size_t>(
            old_school::DeckId::Blue)]
        .mean_regret = 0.095;
    const aq20::Gate rejected =
        aq20::evaluate_dev_gate(broken);
    expect(
        !rejected.passed &&
            has_failure(
                rejected,
                "DEV pair BCE exceeds its better "
                "comparator") &&
            has_failure(
                rejected,
                "DEV listwise CE exceeds its better "
                "comparator") &&
            has_failure(
                rejected,
                "DEV Blue regret exceeds its better "
                "comparator"),
        "DEV gate did not retain every failed conjunct");

    broken = passing;
    broken.candidate.ranking
        .equal_deck_stable_pair_agreement =
        std::numeric_limits<double>::quiet_NaN();
    const aq20::Gate nonfinite =
        aq20::evaluate_dev_gate(broken);
    expect(
        !nonfinite.passed &&
            has_failure(
                nonfinite,
                "DEV stable-pair is below its better "
                "comparator"),
        "DEV gate did not fail closed on NaN evidence");
}

void test_unopened_conditional_paths() {
    const aq20::Gate rejected{
        .passed = false,
        .failures = {"synthetic"},
    };
    const aq20::Gate passed{.passed = true};
    const auto at_oof =
        aq20::authorize_path(
            rejected, std::nullopt, std::nullopt);
    expect(
        at_oof.stage ==
                aq20::EvidenceStage::OofRejected &&
            !at_oof.dev_candidate_opened &&
            !at_oof.counter_gate_opened &&
            !at_oof.c16_selector_seed_opened &&
            !at_oof.aq19_selector_seed_opened &&
            at_oof.gameplay_games == 0,
        "OOF rejection opened downstream evidence");
    const auto pending =
        aq20::authorize_path(
            passed, passed, std::nullopt);
    expect(
        pending.stage ==
                aq20::EvidenceStage::CounterPending &&
            pending.dev_candidate_opened &&
            !pending.counter_gate_opened &&
            !pending.c16_selector_seed_opened &&
            !pending.aq19_selector_seed_opened,
        "DEV pass opened the counter or selector "
        "without an injected result");
    const auto dev_rejected =
        aq20::authorize_path(
            passed, rejected, std::nullopt);
    expect(
        dev_rejected.stage ==
                aq20::EvidenceStage::DevRejected &&
            dev_rejected.dev_candidate_opened &&
            !dev_rejected.counter_gate_opened &&
            !dev_rejected.c16_selector_seed_opened &&
            !dev_rejected.aq19_selector_seed_opened,
        "DEV rejection opened downstream evidence");
    const auto counter_rejected =
        aq20::authorize_path(
            passed, passed, false);
    expect(
        counter_rejected.stage ==
                aq20::EvidenceStage::
                    CounterRejected &&
            counter_rejected.counter_gate_opened &&
            !counter_rejected
                 .c16_selector_seed_opened &&
            !counter_rejected
                 .aq19_selector_seed_opened,
        "counter rejection opened a selector seed");
    const auto authorized =
        aq20::authorize_path(
            passed, passed, true);
    expect(
        authorized.stage ==
                aq20::EvidenceStage::
                    SelectorsAuthorized &&
            authorized.counter_gate_opened &&
            !authorized.c16_selector_seed_opened &&
            !authorized.aq19_selector_seed_opened &&
            authorized.gameplay_games == 0,
        "counter pass opened a selector seed");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run("command contract", test_command_contract);
    tests.run(
        "finite difference and positive diagonal",
        test_finite_difference_and_positive_diagonal);
    tests.run(
        "clipped actual-I ordering",
        test_clipped_actual_improvement_ordering);
    tests.run(
        "sign deduplication",
        test_sign_deduplication);
    tests.run(
        "label-blind fold-local preparation",
        test_label_blind_and_fold_local_preparation);
    tests.run(
        "S0 drift rejection",
        test_s0_drift_rejection);
    tests.run(
        "repeat identity and positive zero",
        test_repeat_identity_and_positive_zero);
    tests.run(
        "repeated fit identity",
        test_repeated_fit_identity);
    tests.run(
        "OOF gate conjunction and fail-closed behavior",
        test_oof_gate_is_conjunctive_and_fail_closed);
    tests.run(
        "DEV gate conjunction and fail-closed behavior",
        test_dev_gate_is_conjunctive_and_fail_closed);
    tests.run(
        "unopened conditional paths",
        test_unopened_conditional_paths);
    return tests.finish();
}
