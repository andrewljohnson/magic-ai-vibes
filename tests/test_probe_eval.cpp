#include "alpha/probe_eval.hpp"

#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using alpha::DeckId;
using alpha::probe_eval::CandidateLabel;
using alpha::probe_eval::CandidateSamples;
using alpha::probe_eval::PairLabel;
using alpha::probe_eval::PolicyScore;
using alpha::probe_eval::ProbeLabel;
using alpha::probe_eval::ProbePrediction;

class TestRunner {
  public:
    void run(std::string_view name,
             const std::function<void()>& test) {
        try {
            test();
            ++passed_;
        } catch (const std::exception& exception) {
            ++failed_;
            std::cerr << "FAIL " << name << ": "
                      << exception.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr << failed_ << " test(s) failed; "
                      << passed_ << " passed\n";
            return 1;
        }
        std::cout << passed_
                  << " probe metric tests passed\n";
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

void expect_near(double actual, double expected, double tolerance,
                 std::string_view message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) + ": expected " +
            std::to_string(expected) + ", got " +
            std::to_string(actual));
    }
}

template <typename Function>
void expect_invalid(Function&& function, std::string_view message) {
    bool threw = false;
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, message);
}

ProbeLabel simple_label(std::string id, DeckId deck,
                        double first_q, double second_q,
                        double pair_se = 0.01) {
    return ProbeLabel{
        std::move(id),
        deck,
        {CandidateLabel{"a", first_q, 0.01},
         CandidateLabel{"b", second_q, 0.01}},
        {PairLabel{"a", "b", first_q - second_q, pair_se}},
        {first_q >= second_q ? "a" : "b"},
        std::max(first_q, second_q),
    };
}

void test_label_construction_uses_paired_statistics() {
    const ProbeLabel label = alpha::probe_eval::make_probe_label(
        "paired", DeckId::Green,
        {CandidateSamples{"a", {0.9, 0.7, 0.8, 0.8}},
         CandidateSamples{"b", {0.6, 0.6, 0.7, 0.7}},
         CandidateSamples{"near", {0.78, 0.68, 0.78, 0.78}}});

    expect_near(label.candidates[0].q, 0.8, 1.0e-12,
                "candidate mean");
    expect_near(label.candidates[0].standard_error,
                std::sqrt(1.0 / 600.0), 1.0e-12,
                "candidate sample standard error");
    expect_near(label.pairs[0].delta_q, 0.15, 1.0e-12,
                "paired mean difference");
    expect_near(label.pairs[0].paired_standard_error, 0.05,
                1.0e-12, "paired standard error");
    expect(label.reference_value == label.candidates[0].q,
           "reference value must be the highest candidate mean");
    expect(label.reference_best_set ==
               std::vector<std::string>({"a", "near"}),
           "uncertainty-aware best set is wrong");
}

void test_uniform_argmax_ties_and_metric_formulas() {
    const ProbeLabel label{
        "ties",
        DeckId::Green,
        {CandidateLabel{"a", 0.8, 0.01},
         CandidateLabel{"b", 0.7, 0.01},
         CandidateLabel{"c", 0.6, 0.01}},
        {PairLabel{"a", "b", 0.1, 0.01},
         PairLabel{"a", "c", 0.2, 0.01},
         PairLabel{"b", "c", 0.1, 0.01}},
        {"a", "c"},
        0.8,
    };
    const ProbePrediction prediction{
        "ties",
        {PolicyScore{"c", 3.0}, PolicyScore{"a", 3.0},
         PolicyScore{"b", 3.0}},
        0.9,
    };

    const auto summary =
        alpha::probe_eval::evaluate_probe_predictions(
            {label}, {prediction});
    expect_near(summary.top1_expected_agreement, 2.0 / 3.0,
                1.0e-12, "uniform argmax expected agreement");
    expect_near(summary.stable_pair_agreement, 0.5, 1.0e-12,
                "student pair ties must receive half credit");
    expect_near(summary.mean_regret, 0.1, 1.0e-12,
                "tie-averaged regret");
    expect_near(summary.critic_brier, 0.01, 1.0e-12,
                "critic Brier score");
    expect_near(summary.critic_mse, 0.01, 1.0e-12,
                "critic MSE");
    const double expected_log_loss =
        -0.8 * std::log(0.9) - 0.2 * std::log(0.1);
    expect_near(summary.critic_log_loss, expected_log_loss,
                1.0e-12, "soft-label log loss");
    expect_near(summary.critic_bias, 0.1, 1.0e-12,
                "critic bias");
    expect_near(summary.critic_ece, 0.1, 1.0e-12,
                "five-bin ECE");
}

void test_stability_filter_uses_effect_and_paired_ci() {
    const ProbeLabel label{
        "stability",
        DeckId::Blue,
        {CandidateLabel{"a", 0.60, 0.01},
         CandidateLabel{"b", 0.58, 0.01},
         CandidateLabel{"c", 0.56, 0.01},
         CandidateLabel{"d", 0.52, 0.01}},
        {PairLabel{"a", "b", 0.02, 0.001},
         PairLabel{"a", "c", 0.04, 0.03},
         PairLabel{"a", "d", 0.08, 0.01},
         PairLabel{"b", "c", 0.02, 0.001},
         PairLabel{"b", "d", 0.06, 0.04},
         PairLabel{"c", "d", 0.04, 0.001}},
        {"a"},
        0.60,
    };
    const ProbePrediction prediction{
        "stability",
        {PolicyScore{"a", 1.0}, PolicyScore{"b", 2.0},
         PolicyScore{"c", 3.0}, PolicyScore{"d", 4.0}},
        0.60,
    };

    const auto summary =
        alpha::probe_eval::evaluate_probe_predictions(
            {label}, {prediction});
    expect(summary.stable_pair_count == 2,
           "only effect-sized pairs with CI excluding zero are stable");
    expect_near(summary.stable_pair_agreement, 0.0, 1.0e-12,
                "reversed stable pair ranking");
}

void test_deck_grouping_and_pooled_calibration() {
    const std::vector<ProbeLabel> labels = {
        simple_label("green", DeckId::Green, 0.8, 0.6),
        simple_label("red", DeckId::Red, 0.7, 0.6),
    };
    const std::vector<ProbePrediction> predictions = {
        ProbePrediction{"red",
                        {PolicyScore{"a", 0.0},
                         PolicyScore{"b", 1.0}},
                        0.5},
        ProbePrediction{"green",
                        {PolicyScore{"a", 1.0},
                         PolicyScore{"b", 1.0}},
                        0.9},
    };

    const auto summary =
        alpha::probe_eval::evaluate_probe_predictions(
            labels, predictions);
    expect(summary.probe_count == 2,
           "pooled probe count");
    expect(summary.stable_pair_count == 2,
           "pooled stable pair count");
    expect_near(summary.top1_expected_agreement, 0.25, 1.0e-12,
                "pooled top-one agreement");
    expect_near(summary.stable_pair_agreement, 0.25, 1.0e-12,
                "pooled pair agreement");
    expect_near(summary.mean_regret, 0.1, 1.0e-12,
                "pooled regret");
    expect_near(summary.critic_mse, 0.025, 1.0e-12,
                "pooled critic MSE");
    expect_near(summary.critic_bias, -0.05, 1.0e-12,
                "pooled critic bias");
    expect_near(summary.critic_ece, 0.15, 1.0e-12,
                "pooled five-bin ECE");

    expect(summary.by_deck[0].root_deck == DeckId::Green &&
               summary.by_deck[0].probe_count == 1,
           "Green grouping");
    expect(summary.by_deck[1].root_deck == DeckId::Red &&
               summary.by_deck[1].probe_count == 1,
           "Red grouping");
    expect(summary.by_deck[2].root_deck == DeckId::Blue &&
               summary.by_deck[2].probe_count == 0,
           "empty Blue grouping");
    expect(summary.by_deck[3].root_deck == DeckId::White &&
               summary.by_deck[3].probe_count == 0,
           "empty White grouping");
}

void test_log_loss_clamps_zero_and_one_predictions() {
    const ProbeLabel label =
        simple_label("clamp", DeckId::White, 1.0, 0.2);
    const ProbePrediction prediction{
        "clamp",
        {PolicyScore{"a", 1.0}, PolicyScore{"b", 0.0}},
        1.0,
    };
    const auto summary =
        alpha::probe_eval::evaluate_probe_predictions(
            {label}, {prediction});
    expect(std::isfinite(summary.critic_log_loss),
           "boundary log loss must be finite");
    expect(summary.critic_log_loss < 1.0e-9,
           "correct boundary probability should have near-zero loss");
}

void test_invalid_label_schemas_are_rejected() {
    ProbeLabel missing_pair =
        simple_label("invalid", DeckId::Red, 0.7, 0.4);
    missing_pair.pairs.clear();
    expect_invalid(
        [&]() {
            alpha::probe_eval::validate_probe_label(missing_pair);
        },
        "missing pair schema was accepted");

    ProbeLabel wrong_delta =
        simple_label("invalid", DeckId::Red, 0.7, 0.4);
    wrong_delta.pairs[0].delta_q = 0.2;
    expect_invalid(
        [&]() {
            alpha::probe_eval::validate_probe_label(wrong_delta);
        },
        "inconsistent pair delta was accepted");

    ProbeLabel duplicate_best =
        simple_label("invalid", DeckId::Red, 0.7, 0.4);
    duplicate_best.reference_best_set = {"a", "a"};
    expect_invalid(
        [&]() {
            alpha::probe_eval::validate_probe_label(duplicate_best);
        },
        "duplicate best-set key was accepted");

    const ProbeLabel duplicate_id =
        simple_label("same", DeckId::Green, 0.7, 0.4);
    expect_invalid(
        [&]() {
            alpha::probe_eval::validate_probe_labels(
                {duplicate_id, duplicate_id});
        },
        "duplicate stable IDs were accepted");
}

void test_invalid_predictions_and_samples_are_rejected() {
    const ProbeLabel label =
        simple_label("prediction", DeckId::Blue, 0.7, 0.4);
    expect_invalid(
        [&]() {
            alpha::probe_eval::evaluate_probe_predictions(
                {label},
                {ProbePrediction{
                    "prediction",
                    {PolicyScore{"a", 1.0},
                     PolicyScore{"a", 0.0}},
                    0.7}});
        },
        "duplicate prediction keys were accepted");
    expect_invalid(
        [&]() {
            alpha::probe_eval::evaluate_probe_predictions(
                {label},
                {ProbePrediction{
                    "prediction",
                    {PolicyScore{"a", 1.0},
                     PolicyScore{"b", 0.0}},
                    std::numeric_limits<double>::quiet_NaN()}});
        },
        "nonfinite critic value was accepted");
    expect_invalid(
        []() {
            (void)alpha::probe_eval::make_probe_label(
                "unaligned", DeckId::Green,
                {CandidateSamples{"a", {0.2, 0.3}},
                 CandidateSamples{"b", {0.4}}});
        },
        "unaligned paired samples were accepted");
    expect_invalid(
        []() {
            (void)alpha::probe_eval::make_probe_label(
                "range", DeckId::Green,
                {CandidateSamples{"a", {0.2, 1.1}},
                 CandidateSamples{"b", {0.4, 0.5}}});
        },
        "out-of-range Q sample was accepted");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run("paired label construction",
               test_label_construction_uses_paired_statistics);
    runner.run("uniform argmax ties and formulas",
               test_uniform_argmax_ties_and_metric_formulas);
    runner.run("paired stability filtering",
               test_stability_filter_uses_effect_and_paired_ci);
    runner.run("deck grouping and calibration",
               test_deck_grouping_and_pooled_calibration);
    runner.run("log-loss boundary clamp",
               test_log_loss_clamps_zero_and_one_predictions);
    runner.run("invalid label schemas",
               test_invalid_label_schemas_are_rejected);
    runner.run("invalid predictions and samples",
               test_invalid_predictions_and_samples_are_rejected);
    return runner.finish();
}
