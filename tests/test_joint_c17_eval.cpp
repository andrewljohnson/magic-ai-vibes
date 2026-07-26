#include "old_school/joint_c17_eval.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace joint = old_school::joint_c17_eval;
namespace probe = old_school::probe_eval;
namespace runner = old_school::probe_runner;
namespace terminal = old_school::terminal_weight_eval;
using old_school::DeckId;

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
                  << " joint-C17 evaluator tests passed\n";
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

terminal::ClusteredEstimate estimate(
    double mean, double lower, double upper) {
    return {
        .records = 20,
        .clusters = 10,
        .mean = mean,
        .standard_error = 0.001,
        .confidence_lower_95 = lower,
        .confidence_upper_95 = upper,
    };
}

terminal::HoldoutReport passing_holdout_report() {
    terminal::HoldoutReport report;
    report.pooled.records = 10;
    report.pooled.perspectives =
        old_school::kDeckCount *
        terminal::kHoldoutPerspectivesPerDeck;
    report.pooled.physical_games =
        terminal::kHoldoutPhysicalGames;
    report.pooled.treatment_comparisons[0].brier_delta =
        estimate(-0.01, -0.02, -0.001);
    report.pooled.treatment_comparisons[0]
        .soft_log_loss_delta =
        estimate(-0.02, -0.03, -0.001);

    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& scope = report.by_deck[deck];
        scope.records = 2;
        scope.perspectives =
            terminal::kHoldoutPerspectivesPerDeck;
        scope.treatment_comparisons[0].brier_delta =
            estimate(
                joint::kMaximumDeckLossDelta,
                -0.01, 0.02);
        scope.treatment_comparisons[0]
            .soft_log_loss_delta =
            estimate(
                joint::kMaximumDeckLossDelta,
                -0.01, 0.02);
        scope.models[1].signed_bias =
            estimate(0.02, -0.01, 0.05);
        scope.models[2].signed_bias =
            estimate(0.01, -0.01, 0.03);
    }
    report
        .by_deck[static_cast<std::size_t>(DeckId::RUAggro)]
        .models[1]
        .signed_bias =
        estimate(0.005, -0.01, 0.02);
    report
        .by_deck[static_cast<std::size_t>(DeckId::RUAggro)]
        .models[2]
        .signed_bias =
        estimate(
            joint::kRuSignedBiasFloor, -0.01, 0.03);
    return report;
}

probe::ProbeLabel stable_label(
    std::string stable_id, DeckId deck,
    double delta = 0.04, double paired_se = 0.01,
    bool reverse_pair = false) {
    probe::ProbeLabel label{
        .stable_id = std::move(stable_id),
        .root_deck = deck,
        .candidates =
            {
                {
                    .key = "best",
                    .q = 0.60,
                    .standard_error = 0.01,
                },
                {
                    .key = "other",
                    .q = 0.60 - delta,
                    .standard_error = 0.01,
                },
            },
        .pairs = {},
        .reference_best_set = {"best"},
        .reference_value = 0.60,
    };
    if (reverse_pair) {
        label.pairs.push_back({
            .first = "other",
            .second = "best",
            .delta_q = -delta,
            .paired_standard_error = paired_se,
        });
    } else {
        label.pairs.push_back({
            .first = "best",
            .second = "other",
            .delta_q = delta,
            .paired_standard_error = paired_se,
        });
    }
    return label;
}

runner::ValueProbeDecisionDetail decision_for(
    const probe::ProbeLabel& label, std::string selected) {
    return {
        .stable_id = label.stable_id,
        .root_deck = label.root_deck,
        .selected_keys = {std::move(selected)},
        .deterministic_selection = true,
        .reference_best_set = label.reference_best_set,
        .critic_prediction = 0.50,
    };
}

std::vector<probe::ProbeLabel> passing_labels() {
    std::vector<probe::ProbeLabel> labels;
    for (const auto stable_id :
         joint::kRequiredStableBlueProbeIds) {
        labels.push_back(stable_label(
            std::string(stable_id), DeckId::Blue));
    }
    for (std::size_t index = 0; index < 4; ++index) {
        labels.push_back(stable_label(
            "green.stable.v" + std::to_string(index + 1),
            DeckId::Green));
        labels.push_back(stable_label(
            "red.stable.v" + std::to_string(index + 1),
            DeckId::Red));
        labels.push_back(stable_label(
            "white.stable.v" + std::to_string(index + 1),
            DeckId::White));
        labels.push_back(stable_label(
            "ru.stable.v" + std::to_string(index + 1),
            DeckId::RUAggro));
    }
    return labels;
}

std::vector<runner::ValueProbeDecisionDetail>
passing_decisions(
    const std::vector<probe::ProbeLabel>& labels) {
    std::vector<runner::ValueProbeDecisionDetail> decisions;
    decisions.reserve(labels.size());
    for (const auto& label : labels) {
        decisions.push_back(decision_for(label, "best"));
    }
    return decisions;
}

probe::ProbeMetricSummary metrics_for(
    const std::vector<probe::ProbeLabel>& labels,
    double top_one = 0.90, double regret = 0.02) {
    probe::ProbeMetricSummary metrics{
        .probe_count = labels.size(),
        .top1_expected_agreement = top_one,
        .mean_regret = regret,
    };
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& deck_metrics = metrics.by_deck[deck];
        deck_metrics.root_deck =
            static_cast<DeckId>(deck);
        deck_metrics.probe_count =
            static_cast<std::size_t>(std::count_if(
                labels.begin(), labels.end(),
                [deck](const probe::ProbeLabel& label) {
                    return static_cast<std::size_t>(
                               label.root_deck) == deck;
                }));
        deck_metrics.top1_expected_agreement = top_one;
        deck_metrics.mean_regret = regret;
    }
    return metrics;
}

runner::ForceSpikePolicyControlReport
passing_force_spike_report() {
    runner::ForceSpikePolicyControlReport report;
    report.live.stable_id =
        std::string(joint::kLiveForceSpikeProbeId);
    report.live.selected_keys = {
        std::string(joint::kForceSpikeCandidateKey)};
    report.payable.stable_id =
        std::string(joint::kPayableForceSpikeProbeId);
    report.payable.selected_keys = {
        std::string(joint::kPassCandidateKey)};
    report.hidden_repartition_passed = true;
    return report;
}

joint::DeepReferenceGateReport passing_deep_gate() {
    const auto labels = passing_labels();
    const auto control = passing_decisions(labels);
    const auto treatment = passing_decisions(labels);
    const auto control_metrics =
        metrics_for(labels, 0.90, 0.02);
    const auto treatment_metrics =
        metrics_for(labels, 0.90, 0.02);
    return joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, control,
        treatment, passing_force_spike_report(), true);
}

void test_heldout_gate_accepts_exact_inclusive_guards() {
    const auto gate =
        joint::evaluate_heldout_gate(
            passing_holdout_report());
    expect(gate.accounting_exact, "exact held-out accounting");
    expect(gate.inputs_finite, "finite held-out inputs");
    expect(
        gate.pooled_losses_improved,
        "strict pooled CR1 improvement");
    expect(
        gate.every_deck_loss_guard,
        "+0.005 deck boundary must pass");
    expect(
        gate.green_bias_strictly_shrank &&
            gate.blue_bias_strictly_shrank,
        "Green/Blue strict shrink");
    expect(
        gate.ru_bias_guard,
        "RU +0.010 floor equality must pass");
    expect(
        gate.no_new_material_bias,
        "nonmaterial bias must pass");
    expect(gate.passed, "passing held-out report");
}

void test_heldout_pooled_and_deck_boundaries_are_exact() {
    auto report = passing_holdout_report();
    report.pooled.treatment_comparisons[0]
        .brier_delta.confidence_upper_95 = 0.0;
    auto gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.pooled_losses_improved && !gate.passed,
        "pooled upper bound equal to zero must fail");

    report = passing_holdout_report();
    report.by_deck[static_cast<std::size_t>(DeckId::Red)]
        .treatment_comparisons[0]
        .soft_log_loss_delta.mean =
        std::nextafter(
            joint::kMaximumDeckLossDelta,
            std::numeric_limits<double>::infinity());
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.every_deck_loss_guard && !gate.passed,
        "point delta above +0.005 must fail");

    report = passing_holdout_report();
    report.pooled.treatment_comparisons[0]
        .soft_log_loss_delta.confidence_upper_95 =
        std::numeric_limits<double>::quiet_NaN();
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.inputs_finite && !gate.passed,
        "nonfinite held-out metric must fail closed");

    report = passing_holdout_report();
    --report.pooled.physical_games;
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.accounting_exact && !gate.passed,
        "held-out physical-game undercount must fail");

    report = passing_holdout_report();
    --report
          .by_deck[static_cast<std::size_t>(DeckId::White)]
          .perspectives;
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.accounting_exact && !gate.passed,
        "held-out deck perspective undercount must fail");
}

void test_heldout_bias_boundaries_and_inheritance() {
    auto report = passing_holdout_report();
    auto& green =
        report.by_deck[
            static_cast<std::size_t>(DeckId::Green)];
    green.models[2].signed_bias.mean =
        green.models[1].signed_bias.mean;
    auto gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.green_bias_strictly_shrank,
        "equal Green absolute bias must fail");

    report = passing_holdout_report();
    auto& ru =
        report.by_deck[
            static_cast<std::size_t>(DeckId::RUAggro)];
    ru.models[2].signed_bias.mean =
        std::nextafter(
            joint::kRuSignedBiasFloor,
            std::numeric_limits<double>::infinity());
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.ru_bias_guard,
        "RU bias above the floor/control maximum must fail");

    report = passing_holdout_report();
    auto& red =
        report.by_deck[
            static_cast<std::size_t>(DeckId::Red)];
    red.models[1].signed_bias =
        estimate(0.06, 0.01, 0.10);
    red.models[2].signed_bias =
        estimate(0.05, 0.001, 0.10);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        gate.no_new_material_bias && gate.passed,
        "same-sign inherited material bias must pass");

    red.models[1].signed_bias =
        estimate(0.06, -0.01, 0.10);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.no_new_material_bias && !gate.passed,
        "control interval crossing zero is not material");

    red.models[1].signed_bias =
        estimate(-0.06, -0.10, -0.01);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.no_new_material_bias,
        "opposite-sign material bias is not inherited");

    red.models[2].signed_bias =
        estimate(
            joint::kMaterialSignedBias, 0.0, 0.10);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        gate.no_new_material_bias,
        "interval touching zero is not material");
}

void test_stable_best_set_uses_paired_strict_boundaries() {
    auto label = stable_label(
        "boundary", DeckId::Green,
        probe::kStablePairMinimumDelta, 0.01);
    expect(
        joint::is_stable_best_set_probe(label),
        "minimum delta equality with separated CI must pass");

    label.pairs[0].delta_q = std::nextafter(
        probe::kStablePairMinimumDelta, 0.0);
    expect(
        !joint::is_stable_best_set_probe(label),
        "delta below stable minimum must fail");

    label = stable_label(
        "ci-boundary", DeckId::Green, 0.04,
        std::nextafter(
            0.04 / probe::kNormal95CriticalValue,
            std::numeric_limits<double>::infinity()));
    expect(
        !joint::is_stable_best_set_probe(label),
        "paired CI touching/crossing zero must fail");

    label = stable_label(
        "reverse", DeckId::Green, 0.04, 0.01, true);
    expect(
        joint::is_stable_best_set_probe(label),
        "reverse pair orientation must be normalized");

    label.reference_best_set = {"best", "other"};
    expect(
        !joint::is_stable_best_set_probe(label),
        "a best set with no outside candidate is ineligible");
}

void test_common_state_critics_use_reference_max_for_both_models() {
    const std::vector<probe::ProbeLabel> labels = {
        stable_label("common-label.v1", DeckId::Green)};
    auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    control.front().selected_keys = {"other"};
    treatment.front().selected_keys = {"other"};
    control.front().critic_prediction = 0.50;
    treatment.front().critic_prediction = 0.70;

    const auto report = joint::score_common_state_critics(
        labels, control, treatment);
    expect(
        report.accounting_exact &&
            report.predictions_valid &&
            report.metrics_finite,
        "common-state critic report validity");
    const auto& control_metrics =
        report.pooled
            .models[joint::kCommonStateControlIndex];
    const auto& treatment_metrics =
        report.pooled
            .models[joint::kCommonStateTreatmentIndex];
    expect(control_metrics.probe_count == 1, "common count");
    expect_near(
        control_metrics.brier, 0.01, 1.0e-12,
        "control Brier uses reference maximum 0.60");
    expect_near(
        control_metrics.signed_bias, -0.10, 1.0e-12,
        "control bias uses common state label");
    expect_near(
        treatment_metrics.brier, 0.01, 1.0e-12,
        "treatment Brier uses same state label");
    expect_near(
        treatment_metrics.signed_bias, 0.10, 1.0e-12,
        "treatment bias uses common state label");
    expect_near(
        control_metrics.soft_log_loss,
        -0.60 * std::log(0.50) -
            0.40 * std::log(0.50),
        1.0e-12, "common-label soft log loss");
    expect_near(
        control_metrics.ece, 0.10, 1.0e-12,
        "common-label calibration");

    treatment.front().critic_prediction =
        std::numeric_limits<double>::quiet_NaN();
    const auto invalid = joint::score_common_state_critics(
        labels, control, treatment);
    expect(
        !invalid.predictions_valid &&
            !invalid.metrics_finite,
        "nonfinite common critic prediction must fail");
}

void test_deep_gate_accepts_equal_metric_boundaries() {
    const auto gate = passing_deep_gate();
    expect(gate.accounting_exact, "deep accounting");
    expect(gate.metrics_finite, "deep metrics finite");
    expect(
        gate.pooled_regret_no_worse &&
            gate.pooled_top_one_no_lower,
        "equal pooled metrics must pass");
    expect(
        gate.every_deck_regret_guard,
        "deck regret guard");
    expect(
        gate.stable_best_set_loss_guard,
        "stable best-set loss guard");
    expect(
        gate.required_blue_probes_exact &&
            gate.required_blue_selections_passed,
        "required Blue probes");
    expect(gate.force_spike.passed, "Force Spike gate");
    expect(
        gate.common_state_critics.accounting_exact &&
            gate.common_state_critics.metrics_finite,
        "common-state critic report");
    expect(gate.passed, "passing deep gate");

    const auto labels = passing_labels();
    const auto decisions = passing_decisions(labels);
    auto control_metrics = metrics_for(labels, 0.90, 0.02);
    auto treatment_metrics = control_metrics;
    treatment_metrics
        .by_deck[static_cast<std::size_t>(DeckId::Red)]
        .mean_regret =
        control_metrics
            .by_deck[static_cast<std::size_t>(DeckId::Red)]
            .mean_regret +
        joint::kMaximumDeckRegretIncrease;
    const auto boundary =
        joint::evaluate_deep_reference_gate(
            control_metrics, treatment_metrics, labels,
            decisions, decisions, passing_force_spike_report(),
            true);
    expect(
        boundary.every_deck_regret_guard,
        "deck regret +0.01 equality must pass");
}

void test_deep_metric_regressions_fail_closed() {
    const auto labels = passing_labels();
    const auto decisions = passing_decisions(labels);
    const auto control_metrics =
        metrics_for(labels, 0.90, 0.02);
    auto treatment_metrics = control_metrics;
    treatment_metrics.mean_regret =
        std::nextafter(
            control_metrics.mean_regret,
            std::numeric_limits<double>::infinity());
    auto gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, decisions,
        decisions, passing_force_spike_report(), true);
    expect(
        !gate.pooled_regret_no_worse && !gate.passed,
        "pooled regret regression must fail");

    treatment_metrics = control_metrics;
    treatment_metrics.top1_expected_agreement =
        std::nextafter(
            control_metrics.top1_expected_agreement, 0.0);
    gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, decisions,
        decisions, passing_force_spike_report(), true);
    expect(
        !gate.pooled_top_one_no_lower && !gate.passed,
        "pooled top-one regression must fail");

    treatment_metrics = control_metrics;
    treatment_metrics
        .by_deck[static_cast<std::size_t>(DeckId::White)]
        .mean_regret =
        std::nextafter(
            control_metrics
                    .by_deck[static_cast<std::size_t>(
                        DeckId::White)]
                    .mean_regret +
                joint::kMaximumDeckRegretIncrease,
            std::numeric_limits<double>::infinity());
    gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, decisions,
        decisions, passing_force_spike_report(), true);
    expect(
        !gate.every_deck_regret_guard && !gate.passed,
        "deck regret above +0.01 must fail");
}

void test_stable_losses_are_per_probe_and_not_net() {
    auto labels = passing_labels();
    auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    for (auto& decision : treatment) {
        if (decision.stable_id == "green.stable.v1") {
            decision.selected_keys = {"other"};
        }
    }
    auto control_metrics = metrics_for(labels);
    auto treatment_metrics = control_metrics;
    auto gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, control,
        treatment, passing_force_spike_report(), true);
    const auto green_index =
        static_cast<std::size_t>(DeckId::Green);
    expect(
        gate.by_deck[green_index].lost_agreements == 1 &&
            gate.stable_best_set_loss_guard,
        "one lost probe per deck must pass");

    for (auto& decision : treatment) {
        if (decision.stable_id == "green.stable.v2") {
            decision.selected_keys = {"other"};
        }
        if (decision.stable_id == "ru.stable.v1") {
            decision.selected_keys = {"best"};
        }
    }
    auto& ru_control = *std::find_if(
        control.begin(), control.end(),
        [](const runner::ValueProbeDecisionDetail& decision) {
            return decision.stable_id == "ru.stable.v1";
        });
    ru_control.selected_keys = {"other"};
    gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, control,
        treatment, passing_force_spike_report(), true);
    expect(
        gate.by_deck[green_index].lost_agreements == 2,
        "losses must count per probe");
    expect(
        gate.by_deck[static_cast<std::size_t>(
            DeckId::RUAggro)]
                .control_agreements == 3 &&
            gate.by_deck[static_cast<std::size_t>(
                DeckId::RUAggro)]
                .treatment_agreements == 4,
        "treatment gain fixture");
    expect(
        !gate.stable_best_set_loss_guard && !gate.passed,
        "gain on another probe must not cancel losses");
}

void test_required_blue_probes_and_selections_are_conjunctive() {
    auto labels = passing_labels();
    const auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    treatment.front().selected_keys = {"other"};
    const auto metrics = metrics_for(labels);
    auto gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        gate.required_blue_probes_exact &&
            !gate.required_blue_selections_passed &&
            !gate.passed,
        "wrong required Blue selection must fail");

    treatment = passing_decisions(labels);
    labels.front().pairs.front().delta_q = 0.01;
    gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        !gate.required_blue_probes_exact && !gate.passed,
        "unstable required Blue probe must fail");

    labels = passing_labels();
    labels.push_back(labels.front());
    const auto duplicate_metrics = metrics_for(labels);
    const auto duplicate_decisions =
        passing_decisions(labels);
    gate = joint::evaluate_deep_reference_gate(
        duplicate_metrics, duplicate_metrics, labels,
        duplicate_decisions, duplicate_decisions,
        passing_force_spike_report(), true);
    expect(
        !gate.accounting_exact &&
            !gate.required_blue_probes_exact &&
            !gate.passed,
        "duplicate required Blue identity must fail");
}

void test_force_spike_gate_requires_unique_exact_selections() {
    auto report = passing_force_spike_report();
    auto gate =
        joint::evaluate_force_spike_selection_gate(report);
    expect(gate.passed, "passing Force Spike controls");

    report.live.selected_keys.push_back("pass");
    gate = joint::evaluate_force_spike_selection_gate(report);
    expect(
        !gate.live_uniquely_selects_force_spike &&
            !gate.passed,
        "live tie must fail unique selection");

    report = passing_force_spike_report();
    report.payable.selected_keys = {
        std::string(joint::kForceSpikeCandidateKey)};
    gate = joint::evaluate_force_spike_selection_gate(report);
    expect(
        !gate.payable_uniquely_selects_pass && !gate.passed,
        "payable Force Spike selection must fail");

    report = passing_force_spike_report();
    report.live.stable_id = "wrong";
    report.hidden_repartition_passed = false;
    gate = joint::evaluate_force_spike_selection_gate(report);
    expect(
        !gate.identities_exact &&
            !gate.hidden_repartition_passed &&
            !gate.passed,
        "identity and hidden checks are conjunctive");
}

void test_deep_gate_rejects_missing_or_nonfinite_rows() {
    auto labels = passing_labels();
    auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    auto metrics = metrics_for(labels);

    treatment.pop_back();
    auto gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        !gate.accounting_exact && !gate.passed,
        "missing decision must fail accounting");

    treatment = passing_decisions(labels);
    metrics.mean_regret =
        std::numeric_limits<double>::infinity();
    gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        !gate.metrics_finite && !gate.passed,
        "nonfinite deep metric must fail");

    metrics = metrics_for(labels);
    gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), false);
    expect(
        !gate.hidden_repartition_passed && !gate.passed,
        "hidden repartition must be conjunctive");
}

void test_stage_decision_suppresses_every_later_stage() {
    joint::StageOutcomes outcomes{
        .heldout_passed = false,
        .deep_reference_passed = true,
        .treatment_vs_control_passed = true,
        .treatment_vs_parent_passed = true,
        .treatment_vs_handcoded_passed = true,
        .fixed_seed_panel_passed = true,
        .mixed_field_passed = true,
    };
    auto decision =
        joint::evaluation_stage_decision(outcomes);
    expect(
        !decision.run_deep_reference &&
            !decision.run_treatment_vs_control &&
            !decision.run_mixed_field &&
            !decision.complete && !decision.passed,
        "held-out failure must suppress supplied later wins");

    outcomes = {.heldout_passed = true};
    decision = joint::evaluation_stage_decision(outcomes);
    expect(
        decision.run_deep_reference &&
            !decision.run_treatment_vs_control,
        "absent deep result must suppress gameplay");

    outcomes.deep_reference_passed = true;
    outcomes.treatment_vs_control_passed = true;
    outcomes.treatment_vs_parent_passed = false;
    outcomes.treatment_vs_handcoded_passed = true;
    outcomes.fixed_seed_panel_passed = true;
    outcomes.mixed_field_passed = true;
    decision = joint::evaluation_stage_decision(outcomes);
    expect(
        decision.run_treatment_vs_parent &&
            !decision.run_treatment_vs_handcoded &&
            !decision.run_fixed_seed_panel &&
            !decision.run_mixed_field,
        "parent failure must suppress all later stages");

    outcomes.treatment_vs_parent_passed = true;
    decision = joint::evaluation_stage_decision(outcomes);
    expect(
        decision.run_treatment_vs_handcoded &&
            decision.run_fixed_seed_panel &&
            decision.run_mixed_field &&
            decision.complete && decision.passed,
        "all passing stages must complete");

    outcomes.mixed_field_passed = false;
    decision = joint::evaluation_stage_decision(outcomes);
    expect(
        decision.complete && !decision.passed,
        "final rejection is complete but not passing");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "held-out exact inclusive guards",
        test_heldout_gate_accepts_exact_inclusive_guards);
    tests.run(
        "held-out pooled/deck boundaries",
        test_heldout_pooled_and_deck_boundaries_are_exact);
    tests.run(
        "held-out bias inheritance",
        test_heldout_bias_boundaries_and_inheritance);
    tests.run(
        "stable best-set paired boundaries",
        test_stable_best_set_uses_paired_strict_boundaries);
    tests.run(
        "common-state critic labels",
        test_common_state_critics_use_reference_max_for_both_models);
    tests.run(
        "deep equal metric boundaries",
        test_deep_gate_accepts_equal_metric_boundaries);
    tests.run(
        "deep metric regressions",
        test_deep_metric_regressions_fail_closed);
    tests.run(
        "per-probe stable losses",
        test_stable_losses_are_per_probe_and_not_net);
    tests.run(
        "required Blue probes",
        test_required_blue_probes_and_selections_are_conjunctive);
    tests.run(
        "Force Spike exact selections",
        test_force_spike_gate_requires_unique_exact_selections);
    tests.run(
        "deep accounting and finiteness",
        test_deep_gate_rejects_missing_or_nonfinite_rows);
    tests.run(
        "no-salvage stage suppression",
        test_stage_decision_suppresses_every_later_stage);
    return tests.finish();
}
