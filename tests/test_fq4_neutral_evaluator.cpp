#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_evaluator.hpp"
#include "old_school/fq4_neutral_evaluator.hpp"
#include "old_school/fq4_neutral_supplement.hpp"

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
#include <utility>
#include <vector>

namespace bundle = old_school::fq4_dev_bundle;
namespace dev = old_school::fq4_dev_evaluator;
namespace evaluator = old_school::fq4_neutral_evaluator;
namespace neutral = old_school::fq4_neutral_supplement;

namespace {

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

void expect_close(
    double actual, double expected, double tolerance,
    std::string_view message) {
    if (!std::isfinite(actual) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) +
            ": actual=" + std::to_string(actual) +
            " expected=" + std::to_string(expected));
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

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

neutral::NeutralRow make_neutral_row(
    bundle::Split split, std::size_t deck,
    double marker) {
    neutral::NeutralRow row{
        .locator = {
            .root = {
                .rank = {
                    .split = split,
                    .owner_deck =
                        static_cast<std::uint8_t>(
                            deck),
                },
            },
        },
        .pass_index = 0,
    };
    const std::array<double, 2> base{
        0.20 + marker / 1000.0,
        0.40 + marker / 1000.0,
    };
    const std::array<double, 2> residual{
        0.01, -0.02,
    };
    for (std::size_t action = 0;
         action < base.size(); ++action) {
        row.actions.push_back({
            .is_pass = action == 0,
            .dominance = {},
            .base_score_bits = bits(base[action]),
            .parent_residual_bits =
                bits(residual[action]),
            .features = {
                {
                    .index =
                        static_cast<std::uint16_t>(
                            action),
                    .value_bits =
                        bits(marker +
                             static_cast<double>(
                                 action + 1)),
                },
            },
        });
    }
    return row;
}

old_school::LearnedValuePriorityTrainingExample
positive_example(double marker) {
    return {
        .options = {{marker}},
        .base_scores = {marker / 10.0},
        .target_probabilities = {1.0},
        .weight = 1.0,
    };
}

void test_order_weights_target_and_check_isolation() {
    const std::vector<neutral::NeutralRow> source{
        make_neutral_row(
            bundle::Split::Fit, 1, 10.0),
        make_neutral_row(
            bundle::Split::Check, 2, 99.0),
        make_neutral_row(
            bundle::Split::Fit, 0, 20.0),
    };
    const evaluator::PreparedNeutralCorpus prepared =
        evaluator::testing::prepare_rows(source);
    expect(
        prepared.fit.size() == 2 &&
            prepared.check.size() == 1,
        "neutral split partition drifted");
    const std::vector<
        old_school::LearnedValuePriorityTrainingExample>
        positive{
            positive_example(100.0),
            positive_example(200.0),
        };
    const evaluator::TrainingBatch anchored =
        evaluator::testing::build_training_batch(
            positive, prepared, true);
    expect(
        anchored.examples.size() == 4 &&
            anchored.positive_examples == 2 &&
            anchored.neutral_examples == 2,
        "anchored batch census drifted");
    expect(
        anchored.examples[0].options ==
                positive[0].options &&
            anchored.examples[0].base_scores ==
                positive[0].base_scores &&
            anchored.examples[0]
                    .target_probabilities ==
                positive[0].target_probabilities &&
            anchored.examples[0].weight ==
                positive[0].weight &&
            anchored.examples[1].options ==
                positive[1].options &&
            anchored.examples[1].base_scores ==
                positive[1].base_scores &&
            anchored.examples[1]
                    .target_probabilities ==
                positive[1].target_probabilities &&
            anchored.examples[1].weight ==
                positive[1].weight,
        "positive examples were not first in original order");
    expect(
        anchored.examples[2].options[0][0] == 11.0 &&
            anchored.examples[3].options[0][0] == 21.0,
        "neutral FIT artifact order drifted");
    expect(
        anchored.examples[2].weight ==
                static_cast<double>(
                    evaluator::
                        kPositiveFitExamplesByDeck[1]) /
                    32.0 &&
            anchored.examples[3].weight ==
                static_cast<double>(
                    evaluator::
                        kPositiveFitExamplesByDeck[0]) /
                    32.0,
        "per-deck P[d]/32 weights drifted");

    const auto& target =
        anchored.examples[2].target_probabilities;
    const double first_score = 0.21 + 0.01;
    const double second_score = 0.41 - 0.02;
    const double first_exp =
        std::exp(
            (first_score - second_score) /
            dev::kPolicyTemperature);
    const double first_softmax =
        first_exp / (first_exp + 1.0);
    const double expected_first =
        dev::kBehaviorPrimaryWeight *
            first_softmax +
        (1.0 - dev::kBehaviorPrimaryWeight) /
            2.0;
    expect_close(
        target[0], expected_first, 1.0e-15,
        "neutral behavior target math drifted");
    expect_close(
        std::accumulate(
            target.begin(), target.end(), 0.0),
        1.0, 1.0e-15,
        "neutral target is not normalized");

    const evaluator::TrainingBatch control =
        evaluator::testing::build_training_batch(
            positive, prepared, false);
    expect(
        control.examples.size() == positive.size() &&
            control.examples[0].options ==
                positive[0].options &&
            control.examples[1].options ==
                positive[1].options &&
            control.neutral_examples == 0 &&
            control.accounting.check_examples == 0 &&
            control.accounting
                    .background_only_examples == 0,
        "omitted-neutral control admitted neutral or CHECK rows");

    std::vector<neutral::NeutralRow> changed_check =
        source;
    changed_check[1].actions[0].features[0]
        .value_bits = bits(1234.0);
    const auto changed_check_batch =
        evaluator::testing::build_training_batch(
            positive,
            evaluator::testing::prepare_rows(
                changed_check),
            true);
    expect(
        changed_check_batch.accounting
                .training_input_sha256 ==
            anchored.accounting.training_input_sha256,
        "neutral CHECK mutation crossed the update boundary");

    std::vector<neutral::NeutralRow> changed_fit =
        source;
    changed_fit[0].actions[0].features[0]
        .value_bits = bits(4321.0);
    const auto changed_fit_batch =
        evaluator::testing::build_training_batch(
            positive,
            evaluator::testing::prepare_rows(
                changed_fit),
            true);
    expect(
        changed_fit_batch.accounting
                .training_input_sha256 !=
            anchored.accounting.training_input_sha256,
        "neutral FIT mutation did not reach the update boundary");
}

void test_full_batch_census_order_and_loss_mass() {
    std::vector<neutral::NeutralRow> source;
    source.reserve(2 * evaluator::kNeutralRowsPerSplit);
    for (const bundle::Split split :
         {bundle::Split::Fit, bundle::Split::Check}) {
        for (std::size_t deck = 0;
             deck < bundle::kDeckCount; ++deck) {
            for (std::size_t row = 0;
                 row < evaluator::kNeutralRowsPerDeck;
                 ++row) {
                source.push_back(
                    make_neutral_row(
                        split, deck,
                        static_cast<double>(
                            1000U *
                                static_cast<std::size_t>(
                                    split ==
                                    bundle::Split::Check) +
                            100U * deck + row)));
            }
        }
    }
    std::vector<
        old_school::LearnedValuePriorityTrainingExample>
        positive;
    positive.reserve(evaluator::kPositiveFitExamples);
    for (std::size_t index = 0;
         index < evaluator::kPositiveFitExamples;
         ++index) {
        positive.push_back(
            positive_example(
                10000.0 +
                static_cast<double>(index)));
    }

    const evaluator::TrainingBatch batch =
        evaluator::testing::build_training_batch(
            positive,
            evaluator::testing::prepare_rows(source),
            true);
    expect(
        batch.examples.size() ==
                evaluator::kPositiveFitExamples +
                    evaluator::kNeutralRowsPerSplit &&
            batch.accounting.fit_examples ==
                batch.examples.size() &&
            batch.positive_examples ==
                evaluator::kPositiveFitExamples &&
            batch.neutral_examples ==
                evaluator::kNeutralRowsPerSplit &&
            batch.accounting.check_examples == 0,
        "full anchored batch census drifted");
    for (std::size_t index = 0;
         index < positive.size(); ++index) {
        expect(
            batch.examples[index].options ==
                    positive[index].options &&
                batch.examples[index].weight == 1.0,
            "full batch did not retain positive-first order");
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        expect(
            batch.neutral_examples_by_deck[deck] ==
                    evaluator::kNeutralRowsPerDeck &&
                batch.neutral_loss_mass_by_deck[deck] ==
                    static_cast<double>(
                        evaluator::
                            kPositiveFitExamplesByDeck[
                                deck]),
            "full batch per-deck 1:1 loss mass drifted");
        for (std::size_t row = 0;
             row < evaluator::kNeutralRowsPerDeck;
             ++row) {
            const std::size_t index =
                evaluator::kPositiveFitExamples +
                deck * evaluator::kNeutralRowsPerDeck +
                row;
            const double marker =
                100.0 * static_cast<double>(deck) +
                static_cast<double>(row);
            expect(
                batch.examples[index].options[0][0] ==
                        marker + 1.0 &&
                    batch.examples[index].weight ==
                        static_cast<double>(
                            evaluator::
                                kPositiveFitExamplesByDeck[
                                    deck]) /
                            static_cast<double>(
                                evaluator::
                                    kNeutralRowsPerDeck),
                "full batch neutral artifact order or weight drifted");
        }
    }
}

void test_legacy_control_hash_is_reproduced_exactly() {
    const bundle::Bundle source =
        bundle::load_published();
    const dev::PreparedCorpus corpus =
        dev::prepare(source);
    const auto positive =
        dev::fit_examples(corpus);
    const evaluator::TrainingBatch control =
        evaluator::testing::build_training_batch(
            positive, {}, false);
    expect(
        control.positive_examples ==
                evaluator::kPositiveFitExamples &&
            control.positive_options ==
                evaluator::kPositiveFitOptions &&
            control.accounting.fit_examples ==
                evaluator::kPositiveFitExamples &&
            control.accounting.fit_options ==
                evaluator::kPositiveFitOptions &&
            control.accounting.optimizer ==
                dev::kOptimizer &&
            control.accounting.training_input_sha256 ==
                evaluator::
                    kRequiredPositiveOnlyTrainingInputSha256,
        "extended control did not reproduce the exact DEV1 input");
}

std::vector<evaluator::NeutralScoreTriplet>
passing_neutral_scores() {
    std::vector<evaluator::NeutralScoreTriplet> rows;
    rows.reserve(evaluator::kNeutralRowsPerSplit);
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        for (std::size_t row = 0;
             row < evaluator::kNeutralRowsPerDeck;
             ++row) {
            evaluator::NeutralScoreTriplet value{
                .owner_deck =
                    static_cast<std::uint8_t>(
                        deck),
                .parent_combined_scores =
                    {0.60, 0.40},
                .baseline_combined_scores =
                    {0.40, 0.60},
                .anchored_combined_scores =
                    {0.60, 0.40},
            };
            if (deck == 0 && row == 0) {
                value.parent_combined_scores =
                    {-0.0, +0.0};
                value.baseline_combined_scores =
                    {+0.0, +0.0};
                value.anchored_combined_scores =
                    {-0.0, +0.0};
            }
            rows.push_back(std::move(value));
        }
    }
    return rows;
}

void test_binary64_kl_and_bitwise_support() {
    const evaluator::NeutralDriftMetrics metrics =
        evaluator::measure_neutral_check(
            passing_neutral_scores());
    expect(
        metrics.rows ==
                evaluator::kNeutralRowsPerSplit &&
            metrics.baseline_exact_support_changes ==
                evaluator::kNeutralRowsPerSplit &&
            metrics.anchored_exact_support_changes == 0,
        "bitwise exact-maximum support census drifted");
    expect(
        metrics.baseline_equal_deck_kl > 0.0 &&
            metrics.anchored_equal_deck_kl == 0.0,
        "neutral KL direction or accumulation drifted");
    for (const auto& deck : metrics.decks) {
        expect(
            deck.rows ==
                    evaluator::kNeutralRowsPerDeck &&
                deck.anchored_parent_to_candidate_kl ==
                    0.0 &&
                deck.anchored_exact_support_changes ==
                    0,
            "per-deck neutral drift cross-sum drifted");
    }

    auto out_of_order = passing_neutral_scores();
    std::swap(out_of_order[0], out_of_order[32]);
    expect_rejected(
        [&] {
            (void)evaluator::measure_neutral_check(
                out_of_order);
        },
        "out-of-order neutral CHECK rows passed");

    std::vector<evaluator::NeutralScoreTriplet>
        asymmetric;
    asymmetric.reserve(evaluator::kNeutralRowsPerSplit);
    const std::vector<double> parent_scores{
        0.70, 0.20, -0.10};
    const std::vector<double> candidate_scores{
        -0.30, 0.40, 0.90};
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        for (std::size_t row = 0;
             row < evaluator::kNeutralRowsPerDeck;
             ++row) {
            asymmetric.push_back({
                .owner_deck =
                    static_cast<std::uint8_t>(deck),
                .parent_combined_scores =
                    parent_scores,
                .baseline_combined_scores =
                    candidate_scores,
                .anchored_combined_scores =
                    parent_scores,
            });
        }
    }
    const auto behavior =
        [](const std::vector<double>& scores) {
            const double maximum =
                *std::max_element(
                    scores.begin(), scores.end());
            std::vector<double> result;
            result.reserve(scores.size());
            double total = 0.0;
            for (const double score : scores) {
                const double value =
                    std::exp(
                        (score - maximum) /
                        dev::kPolicyTemperature);
                result.push_back(value);
                total += value;
            }
            for (double& value : result) {
                value =
                    dev::kBehaviorPrimaryWeight *
                        value / total +
                    (1.0 -
                     dev::kBehaviorPrimaryWeight) /
                        static_cast<double>(
                            result.size());
            }
            return result;
        };
    const std::vector<double> parent_behavior =
        behavior(parent_scores);
    const std::vector<double> candidate_behavior =
        behavior(candidate_scores);
    double expected_forward_kl = 0.0;
    double reversed_kl = 0.0;
    for (std::size_t index = 0;
         index < parent_behavior.size(); ++index) {
        expected_forward_kl +=
            parent_behavior[index] *
            std::log(
                parent_behavior[index] /
                candidate_behavior[index]);
        reversed_kl +=
            candidate_behavior[index] *
            std::log(
                candidate_behavior[index] /
                parent_behavior[index]);
    }
    expect(
        std::abs(expected_forward_kl - reversed_kl) >
            1.0e-3,
        "asymmetric KL oracle is accidentally symmetric");
    const evaluator::NeutralDriftMetrics asymmetric_metrics =
        evaluator::measure_neutral_check(asymmetric);
    expect_close(
        asymmetric_metrics.baseline_equal_deck_kl,
        expected_forward_kl, 1.0e-14,
        "neutral metric reversed the declared KL direction");
}

dev::EvaluationMetrics exact_positive_metrics() {
    dev::EvaluationMetrics result{
        .parent_anchor_rows = 192,
        .parent_anchor_actions = 1141,
        .parent_anchors_exact = true,
    };
    result.fit.positive_roots =
        evaluator::kPositiveFitExamples;
    result.fit.positive_options =
        evaluator::kPositiveFitOptions;
    result.fit.repairs =
        evaluator::kRequiredFitRepairs;
    result.fit.regressions = 0;
    result.fit.candidate_support_violations = {
        .violating_roots =
            evaluator::
                kRequiredFitSupportViolations,
        .positive_roots =
            evaluator::kPositiveFitExamples,
    };
    result.check.positive_roots =
        evaluator::kPositiveCheckExamples;
    result.check.positive_options =
        evaluator::kPositiveCheckOptions;
    result.check.repairs =
        evaluator::kRequiredCheckRepairs;
    result.check.regressions = 0;
    result.check.candidate_support_violations = {
        .violating_roots = 0,
        .positive_roots =
            evaluator::kPositiveCheckExamples,
    };
    result.check.candidate_classes.values = {
        evaluator::kPositiveCheckExamples,
        0, 0, 0,
    };
    constexpr std::array<std::array<std::size_t, 4>, 5>
        kCheckParentClasses{{
            {8, 3, 8, 1},
            {5, 0, 0, 0},
            {20, 1, 10, 0},
            {0, 7, 0, 0},
            {23, 3, 5, 0},
        }};
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        dev::DeckMetrics& fit =
            result.fit.decks[deck];
        fit.positive_roots =
            evaluator::
                kPositiveFitExamplesByDeck[deck];
        fit.candidate_support_violations = {
            .violating_roots =
                evaluator::
                    kPositiveFitSupportViolationCeilings[
                        deck],
            .positive_roots =
                evaluator::
                    kPositiveFitExamplesByDeck[deck],
        };
        fit.regressions = 0;

        dev::DeckMetrics& check =
            result.check.decks[deck];
        check.positive_roots =
            evaluator::
                kPositiveCheckExamplesByDeck[deck];
        check.parent_classes.values =
            kCheckParentClasses[deck];
        check.candidate_classes.values = {
            evaluator::
                kPositiveCheckExamplesByDeck[deck],
            0, 0, 0,
        };
        check.candidate_support_violations = {
            .violating_roots = 0,
            .positive_roots =
                evaluator::
                    kPositiveCheckExamplesByDeck[deck],
        };
        check.repairs =
            kCheckParentClasses[deck][1] +
            kCheckParentClasses[deck][2];
        check.regressions = 0;
    }
    return result;
}

evaluator::IsolationChecks exact_isolation() {
    return {
        .parent_immutable = true,
        .positive_only_candidate_exact = true,
        .omitted_neutral_control_exact = true,
        .parent_anchors_exact = true,
        .hidden_repartition_contract_exact = true,
        .fit_check_isolated = true,
        .nonpriority_components_identical = true,
        .priority_component_changed = true,
    };
}

void test_conjunctive_gate_and_literal_half_thresholds() {
    const dev::EvaluationMetrics positive =
        exact_positive_metrics();
    evaluator::NeutralDriftMetrics neutral_metrics =
        evaluator::measure_neutral_check(
            passing_neutral_scores());
    evaluator::GateReport gate =
        evaluator::evaluate_gate(
            positive, positive, neutral_metrics,
            exact_isolation());
    expect(
        gate.passed() &&
            gate.neutral_per_deck_nonworsening &&
            gate.neutral_kl_halved &&
            gate.neutral_support_changes_halved,
        "valid conjunctive neutral gate did not pass");

    for (auto& deck : neutral_metrics.decks) {
        deck.anchored_parent_to_candidate_kl =
            deck.baseline_parent_to_candidate_kl /
            2.0;
        deck.anchored_exact_support_changes =
            deck.baseline_exact_support_changes /
            2;
    }
    neutral_metrics.anchored_equal_deck_kl = 0.0;
    neutral_metrics.anchored_exact_support_changes = 0;
    for (const auto& deck : neutral_metrics.decks) {
        neutral_metrics.anchored_equal_deck_kl +=
            deck.anchored_parent_to_candidate_kl;
        neutral_metrics.anchored_exact_support_changes +=
            deck.anchored_exact_support_changes;
    }
    neutral_metrics.anchored_equal_deck_kl /=
        static_cast<double>(bundle::kDeckCount);
    gate = evaluator::evaluate_gate(
        positive, positive, neutral_metrics,
        exact_isolation());
    expect(
        gate.passed(),
        "literal equality at both half thresholds failed");

    evaluator::NeutralDriftMetrics deck_worse =
        neutral_metrics;
    deck_worse.decks[0]
        .anchored_parent_to_candidate_kl =
            std::nextafter(
                deck_worse.decks[0]
                    .baseline_parent_to_candidate_kl,
                std::numeric_limits<double>::infinity());
    for (std::size_t deck = 1;
         deck < bundle::kDeckCount; ++deck) {
        deck_worse.decks[deck]
            .anchored_parent_to_candidate_kl = 0.0;
    }
    deck_worse.anchored_equal_deck_kl = 0.0;
    for (const auto& deck : deck_worse.decks) {
        deck_worse.anchored_equal_deck_kl +=
            deck.anchored_parent_to_candidate_kl;
    }
    deck_worse.anchored_equal_deck_kl /=
        static_cast<double>(bundle::kDeckCount);
    gate = evaluator::evaluate_gate(
        positive, positive, deck_worse,
        exact_isolation());
    expect(
        !gate.passed() &&
            !gate.neutral_per_deck_nonworsening &&
            gate.neutral_kl_halved &&
            gate.neutral_support_changes_halved,
        "one worsening deck hid behind passing pooled half gates");

    evaluator::NeutralDriftMetrics above =
        neutral_metrics;
    for (auto& deck : above.decks) {
        deck.anchored_parent_to_candidate_kl =
            std::nextafter(
                deck.baseline_parent_to_candidate_kl /
                    2.0,
                std::numeric_limits<double>::infinity());
    }
    above.anchored_equal_deck_kl = 0.0;
    for (const auto& deck : above.decks) {
        above.anchored_equal_deck_kl +=
            deck.anchored_parent_to_candidate_kl;
    }
    above.anchored_equal_deck_kl /=
        static_cast<double>(bundle::kDeckCount);
    gate = evaluator::evaluate_gate(
        positive, positive, above,
        exact_isolation());
    expect(
        !gate.passed() &&
            !gate.neutral_kl_halved,
        "above-half KL passed through a tolerance");

    evaluator::NeutralDriftMetrics support_fail =
        neutral_metrics;
    ++support_fail.decks[0]
          .anchored_exact_support_changes;
    ++support_fail.anchored_exact_support_changes;
    gate = evaluator::evaluate_gate(
        positive, positive, support_fail,
        exact_isolation());
    expect(
        !gate.passed() &&
            !gate.neutral_support_changes_halved,
        "above-half integer support count passed");

    evaluator::IsolationChecks isolated =
        exact_isolation();
    isolated.fit_check_isolated = false;
    gate = evaluator::evaluate_gate(
        positive, positive, neutral_metrics,
        isolated);
    expect(
        !gate.passed() &&
            !gate.isolation_exact,
        "failed FIT/CHECK isolation passed");
}

void test_production_artifact_boundary_fails_closed() {
    expect_rejected(
        [] {
            (void)evaluator::prepare(
                neutral::Artifact{});
        },
        "invalid synthetic artifact crossed production validation");

    auto malformed =
        make_neutral_row(
            bundle::Split::Fit, 0, 1.0);
    malformed.actions[1].dominance = {
        .complete =
            static_cast<std::uint8_t>(
                bundle::kWorldCount),
        .strict =
            static_cast<std::uint8_t>(
                bundle::kWorldCount),
    };
    expect_rejected(
        [&] {
            const std::array rows{malformed};
            (void)evaluator::testing::prepare_rows(
                rows);
        },
        "dominance-positive row entered neutral preparation");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "positive-first order, P[d]/32 weights, target math, CHECK isolation",
        test_order_weights_target_and_check_isolation);
    tests.run(
        "full 248-row batch preserves order and five-deck 1:1 loss mass",
        test_full_batch_census_order_and_loss_mass);
    tests.run(
        "omitted-neutral control reproduces exact DEV1 input",
        test_legacy_control_hash_is_reproduced_exactly);
    tests.run(
        "binary64 KL and bitwise exact support",
        test_binary64_kl_and_bitwise_support);
    tests.run(
        "conjunctive gate uses literal half thresholds",
        test_conjunctive_gate_and_literal_half_thresholds);
    tests.run(
        "production artifact and neutral-row boundaries fail closed",
        test_production_artifact_boundary_fails_closed);
    return tests.finish();
}
