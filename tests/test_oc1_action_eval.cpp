#include "old_school/oc1_action_eval.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace action_eval = old_school::oc1_action_eval;

namespace {

using old_school::DeckId;
using old_school::probe_eval::CandidateSamples;

class TestRunner {
  public:
    void run(
        std::string_view name,
        const std::function<void()>& test) {
        try {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
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
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) + ": expected " +
            std::to_string(expected) + ", got " +
            std::to_string(actual));
    }
}

template <typename Function>
void expect_invalid(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

action_eval::ReferenceRoot make_root(
    std::string stable_id, DeckId deck,
    std::vector<CandidateSamples> candidates) {
    return action_eval::make_reference_root(
        std::move(stable_id), deck, std::move(candidates));
}

action_eval::RootPolicyMetrics make_metrics(
    std::string stable_id, DeckId deck, double regret,
    double top_one_fraction) {
    constexpr double kBest = 0.8;
    return {
        .stable_id = std::move(stable_id),
        .root_deck = deck,
        .support = action_eval::make_action_support({"a"}),
        .support_mean = kBest - regret,
        .best_candidate_mean = kBest,
        .regret = regret,
        .top_one_fraction = top_one_fraction,
    };
}

void test_canonical_factories_and_validation() {
    const action_eval::ReferenceRoot root = make_root(
        "canonical", DeckId::Green,
        {
            CandidateSamples{"z", {0.2, 0.3}},
            CandidateSamples{"a", {0.8, 0.7}},
        });
    expect(
        root.candidates[0].key == "a" &&
            root.candidates[1].key == "z",
        "reference candidates were not canonicalized");

    const action_eval::ActionSupport support =
        action_eval::make_action_support({"z", "a"});
    expect(
        support.actions ==
            std::vector<std::string>({"a", "z"}),
        "action support was not canonicalized");

    action_eval::ReferenceRoot reordered = root;
    std::swap(
        reordered.candidates[0], reordered.candidates[1]);
    expect_invalid(
        [&] {
            action_eval::validate_reference_root(reordered);
        },
        "noncanonical decoded reference root was accepted");
    expect_invalid(
        [] {
            static_cast<void>(
                action_eval::make_action_support({"a", "a"}));
        },
        "duplicate support action was silently deduplicated");
    expect_invalid(
        [] {
            static_cast<void>(make_root(
                "duplicate", DeckId::Green,
                {
                    CandidateSamples{"a", {0.2, 0.3}},
                    CandidateSamples{"a", {0.4, 0.5}},
                }));
        },
        "duplicate reference candidate was accepted");
    expect_invalid(
        [] {
            static_cast<void>(make_root(
                "unaligned", DeckId::Green,
                {
                    CandidateSamples{"a", {0.2, 0.3}},
                    CandidateSamples{"b", {0.4, 0.5, 0.6}},
                }));
        },
        "unaligned reference worlds were accepted");
    expect_invalid(
        [] {
            static_cast<void>(make_root(
                "range", DeckId::Green,
                {
                    CandidateSamples{"a", {0.2, 1.1}},
                    CandidateSamples{"b", {0.4, 0.5}},
                }));
        },
        "out-of-range reference probability was accepted");
    expect_invalid(
        [&] {
            static_cast<void>(
                action_eval::uniform_support_values(
                    root,
                    action_eval::make_action_support(
                        {"unknown"})));
        },
        "unknown support action was accepted");
}

void test_fractional_ties_and_multi_action_supports() {
    const action_eval::ReferenceRoot root = make_root(
        "fractional", DeckId::Red,
        {
            CandidateSamples{"c", {0.3, 0.3, 0.3, 0.3}},
            CandidateSamples{"b", {0.9, 0.9, 0.9, 0.9}},
            CandidateSamples{"a", {0.9, 0.9, 0.9, 0.9}},
        });
    const action_eval::ActionSupport all =
        action_eval::make_action_support({"c", "a", "b"});
    const action_eval::ActionSupport tied_best =
        action_eval::exact_best_set(root);
    expect(
        tied_best.actions ==
            std::vector<std::string>({"a", "b"}),
        "exact tied maximum set is wrong");

    const std::vector<double> values =
        action_eval::uniform_support_values(root, all);
    for (const double value : values) {
        expect_near(
            value, 0.7, 1.0e-12,
            "uniform three-action world value");
    }
    const action_eval::RootPolicyMetrics metrics =
        action_eval::evaluate_support(
            root, all, tied_best);
    expect_near(
        metrics.support_mean, 0.7, 1.0e-12,
        "multi-action support mean");
    expect_near(
        metrics.regret, 0.2, 1.0e-12,
        "multi-action support regret");
    expect_near(
        metrics.top_one_fraction, 2.0 / 3.0, 1.0e-12,
        "fractional top-one tie credit");
}

void test_raw_covariance_drives_support_standard_error() {
    const action_eval::ReferenceRoot root = make_root(
        "covariance", DeckId::Blue,
        {
            CandidateSamples{"a", {0.8, 0.4, 0.8, 0.4}},
            CandidateSamples{"b", {0.4, 0.8, 0.4, 0.8}},
            CandidateSamples{"c", {0.4, 0.4, 0.4, 0.4}},
        });
    const action_eval::ActionSupport combined =
        action_eval::make_action_support({"a", "b"});
    const action_eval::ActionSupport c =
        action_eval::make_action_support({"c"});
    const action_eval::PairedEstimate combined_minus_c =
        action_eval::paired_support_difference(
            root, combined, c);
    const action_eval::PairedEstimate a_minus_c =
        action_eval::paired_support_difference(
            root, action_eval::make_action_support({"a"}), c);
    const action_eval::PairedEstimate b_minus_c =
        action_eval::paired_support_difference(
            root, action_eval::make_action_support({"b"}), c);

    expect_near(
        combined_minus_c.mean, 0.2, 1.0e-12,
        "uniform support contrast mean");
    expect_near(
        combined_minus_c.standard_error, 0.0, 1.0e-12,
        "negative covariance did not cancel support uncertainty");
    expect(
        a_minus_c.standard_error > 0.0 &&
            b_minus_c.standard_error > 0.0,
        "fixture did not prove pair SEs alone are insufficient");
}

void test_robust_effect_boundaries_are_literal() {
    expect(
        action_eval::kRobustMinimumEffect == 0.03 &&
            action_eval::kNormal95CriticalValue == 1.96,
        "frozen robust-effect constants changed");
    expect(
        action_eval::is_robust_positive(
            {.mean = 0.03,
             .standard_error = 0.0,
             .lower_95 = 0.03}),
        "exact 0.03 effect with positive lower bound failed");
    expect(
        !action_eval::is_robust_positive(
            {.mean = 0.03,
             .standard_error = 0.01,
             .lower_95 = 0.0}),
        "exact-zero lower bound incorrectly passed");
    expect(
        !action_eval::is_robust_positive(
            {.mean = std::nextafter(0.03, 0.0),
             .standard_error = 0.0,
             .lower_95 = 0.03}),
        "sub-0.03 effect incorrectly passed");
    expect(
        action_eval::is_robust_positive(
            {.mean = 0.03,
             .standard_error = 0.01,
             .lower_95 =
                 std::nextafter(0.0, 1.0)}),
        "strictly positive lower bound did not pass");

    const action_eval::ReferenceRoot root = make_root(
        "interval", DeckId::White,
        {
            CandidateSamples{"a", {0.8, 0.4, 0.8, 0.4}},
            CandidateSamples{"b", {0.4, 0.4, 0.4, 0.4}},
        });
    const action_eval::PairedEstimate estimate =
        action_eval::paired_support_difference(
            root, action_eval::make_action_support({"a"}),
            action_eval::make_action_support({"b"}));
    expect(
        estimate.lower_95 ==
            estimate.mean -
                1.96 * estimate.standard_error,
        "paired lower bound did not use exact 1.96");
}

void test_joint_dominance_and_nondominated_set() {
    const action_eval::ReferenceRoot actor = make_root(
        "joint", DeckId::RUAggro,
        {
            CandidateSamples{"a", {0.8, 0.8, 0.8, 0.8}},
            CandidateSamples{"b", {0.4, 0.4, 0.4, 0.4}},
            CandidateSamples{"c", {0.1, 0.1, 0.1, 0.1}},
        });
    const action_eval::ReferenceRoot c16 = make_root(
        "joint", DeckId::RUAggro,
        {
            CandidateSamples{"a", {0.4, 0.4, 0.4, 0.4}},
            CandidateSamples{"b", {0.8, 0.8, 0.8, 0.8}},
            CandidateSamples{"c", {0.1, 0.1, 0.1, 0.1}},
        });

    expect(
        !action_eval::joint_dominance(
             actor, c16, "a", "b")
             .first_dominates_second &&
            !action_eval::joint_dominance(
                 actor, c16, "b", "a")
                 .first_dominates_second,
        "reference sign reversal manufactured joint dominance");
    expect(
        action_eval::joint_dominance(
            actor, c16, "a", "c")
            .first_dominates_second,
        "jointly robust action did not dominate inferior action");
    expect(
        action_eval::joint_robust_best_set(actor, c16).actions ==
            std::vector<std::string>({"a", "b"}),
        "nondominated joint robust-best set is wrong");
}

void test_entire_support_containment() {
    const action_eval::ActionSupport subset =
        action_eval::make_action_support({"b", "a"});
    const action_eval::ActionSupport superset =
        action_eval::make_action_support({"c", "b", "a"});
    const action_eval::ActionSupport partial =
        action_eval::make_action_support({"a"});
    expect(
        action_eval::entire_support_contained(
            subset, superset),
        "complete support containment failed");
    expect(
        !action_eval::entire_support_contained(
            subset, partial),
        "partial overlap passed entire-support containment");
}

void test_equal_root_summaries_and_regret_boundary() {
    const std::vector<action_eval::RootPolicyMetrics> roots = {
        make_metrics("green-a", DeckId::Green, 0.0, 1.0),
        make_metrics("green-b", DeckId::Green, 0.2, 0.5),
        make_metrics("red-a", DeckId::Red, 0.4, 0.0),
    };
    const action_eval::EqualRootSummary summary =
        action_eval::summarize_equal_roots(roots);
    const action_eval::EqualRootSummary reordered =
        action_eval::summarize_equal_roots(
            {roots[2], roots[0], roots[1]});
    expect(
        summary == reordered,
        "canonical root order changed a summary");
    expect(
        summary.root_count == 3 &&
            summary.by_deck[0].root_count == 2 &&
            summary.by_deck[1].root_count == 1,
        "pooled or deck root counts are wrong");
    expect_near(
        summary.mean_regret, 0.2, 1.0e-12,
        "pooled summary is not root-weighted");
    expect_near(
        summary.by_deck[0].mean_regret, 0.1, 1.0e-12,
        "Green equal-root mean regret");
    expect_near(
        summary.mean_top_one_fraction, 0.5, 1.0e-12,
        "pooled top-one summary");

    constexpr double kControlRegret = 0.1;
    const double exact_boundary =
        kControlRegret +
        action_eval::kPerDeckRegretAllowance;
    const std::vector<action_eval::RootPolicyMetrics> control = {
        make_metrics(
            "green", DeckId::Green, kControlRegret, 0.5),
        make_metrics(
            "red", DeckId::Red, kControlRegret, 0.5),
    };
    std::vector<action_eval::RootPolicyMetrics> candidate = {
        make_metrics(
            "green", DeckId::Green, exact_boundary, 0.5),
        make_metrics("red", DeckId::Red, 0.0, 0.5),
    };
    const action_eval::EqualRootComparison boundary =
        action_eval::compare_equal_roots(control, candidate);
    expect(
        boundary.every_deck_regret_within_allowance &&
            boundary.passed,
        "exact +0.010 per-deck regret boundary did not pass");

    candidate[0].regret =
        std::nextafter(exact_boundary,
                       std::numeric_limits<double>::infinity());
    candidate[0].support_mean =
        candidate[0].best_candidate_mean -
        candidate[0].regret;
    expect(
        !action_eval::compare_equal_roots(control, candidate)
             .every_deck_regret_within_allowance,
        "regret above +0.010 boundary incorrectly passed");
}

void test_material_regression_requires_both_references() {
    const action_eval::ReferenceRoot actor = make_root(
        "material", DeckId::Blue,
        {
            CandidateSamples{"a", {0.8, 0.8, 0.8, 0.8}},
            CandidateSamples{"b", {0.4, 0.4, 0.4, 0.4}},
            CandidateSamples{"c", {0.6, 0.6, 0.6, 0.6}},
            CandidateSamples{"d", {0.4, 0.4, 0.4, 0.4}},
        });
    const action_eval::ReferenceRoot c16_weak = make_root(
        "material", DeckId::Blue,
        {
            CandidateSamples{"a", {0.6, 0.6, 0.6, 0.6}},
            CandidateSamples{"b", {0.58, 0.58, 0.58, 0.58}},
            CandidateSamples{"c", {0.6, 0.6, 0.6, 0.6}},
            CandidateSamples{"d", {0.58, 0.58, 0.58, 0.58}},
        });
    const action_eval::ReferenceRoot c16_strong = make_root(
        "material", DeckId::Blue,
        {
            CandidateSamples{"a", {0.6, 0.6, 0.6, 0.6}},
            CandidateSamples{"b", {0.5, 0.5, 0.5, 0.5}},
            CandidateSamples{"c", {0.6, 0.6, 0.6, 0.6}},
            CandidateSamples{"d", {0.5, 0.5, 0.5, 0.5}},
        });
    const action_eval::ActionSupport control =
        action_eval::make_action_support({"a", "c"});
    const action_eval::ActionSupport candidate =
        action_eval::make_action_support({"b", "d"});

    const auto one_reference =
        action_eval::material_regression(
            actor, c16_weak, control, candidate);
    expect(
        action_eval::is_robust_positive(
            one_reference.actor_control_minus_candidate) &&
            !action_eval::is_robust_positive(
                one_reference.c16_control_minus_candidate) &&
            !one_reference.material_under_both,
        "one-reference regression incorrectly became material");
    expect(
        action_eval::material_regression(
            actor, c16_strong, control, candidate)
            .material_under_both,
        "two-reference arbitrary-support regression did not fire");
}

void test_total_regret_and_matching_root_guards() {
    const std::vector<action_eval::RootPolicyMetrics> control = {
        make_metrics("one", DeckId::Green, 0.1, 0.5),
        make_metrics("two", DeckId::Blue, 0.2, 0.5),
    };
    std::vector<action_eval::RootPolicyMetrics> candidate =
        control;
    expect_near(
        action_eval::total_regret(control), 0.3, 1.0e-12,
        "DVR total regret");
    expect(
        action_eval::total_regret_no_worse(
            control, candidate),
        "equal DVR total regret did not pass");
    candidate[1].regret += 0.001;
    candidate[1].support_mean =
        candidate[1].best_candidate_mean -
        candidate[1].regret;
    expect(
        !action_eval::total_regret_no_worse(
            control, candidate),
        "worse DVR total regret incorrectly passed");

    candidate = control;
    candidate[1].stable_id = "different";
    expect_invalid(
        [&] {
            static_cast<void>(
                action_eval::compare_equal_roots(
                    control, candidate));
        },
        "mismatched root coverage was accepted");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "canonical factories and validation",
        test_canonical_factories_and_validation);
    runner.run(
        "fractional ties and multi-action supports",
        test_fractional_ties_and_multi_action_supports);
    runner.run(
        "raw covariance for support uncertainty",
        test_raw_covariance_drives_support_standard_error);
    runner.run(
        "literal robust-effect boundaries",
        test_robust_effect_boundaries_are_literal);
    runner.run(
        "joint dominance and nondominated set",
        test_joint_dominance_and_nondominated_set);
    runner.run(
        "entire-support containment",
        test_entire_support_containment);
    runner.run(
        "equal-root summaries and +0.010 boundary",
        test_equal_root_summaries_and_regret_boundary);
    runner.run(
        "dual-reference arbitrary-support material gate",
        test_material_regression_requires_both_references);
    runner.run(
        "DVR total regret and matching roots",
        test_total_regret_and_matching_root_guards);
    return runner.finish();
}
