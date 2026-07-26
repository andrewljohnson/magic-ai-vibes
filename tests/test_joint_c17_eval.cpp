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
using old_school::BotBenchmarkSummary;
using old_school::BotKind;
using old_school::DeckId;
using old_school::TournamentSummary;

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

template <typename Destination, typename Source>
void add_stats(
    Destination& destination, const Source& source,
    bool invert = false) {
    destination.games += source.games;
    destination.wins +=
        invert ? source.losses : source.wins;
    destination.losses +=
        invert ? source.wins : source.losses;
    destination.draws += source.draws;
}

void recompute_benchmark_totals(
    BotBenchmarkSummary& summary) {
    summary.challenger_stats = {};
    summary.baseline_stats = {};
    summary.challenger_decks = {};
    summary.baseline_decks = {};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount; ++opponent) {
            const auto& cell =
                summary.challenger_deck_matchups
                    [deck][opponent];
            add_stats(
                summary.challenger_decks[deck], cell);
            add_stats(summary.challenger_stats, cell);
            add_stats(
                summary.baseline_decks[opponent],
                cell, true);
            add_stats(summary.baseline_stats, cell, true);
        }
    }
    summary.total_games = summary.challenger_stats.games;
}

BotBenchmarkSummary benchmark_fixture(
    std::size_t repetitions,
    std::size_t diagonal_wins,
    std::size_t off_diagonal_wins) {
    BotBenchmarkSummary summary;
    summary.repetitions_per_deck_pairing = repetitions;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount; ++opponent) {
            auto& cell =
                summary.challenger_deck_matchups
                    [deck][opponent];
            cell.games =
                (deck == opponent ? 4U : 2U) *
                repetitions;
            cell.wins =
                deck == opponent
                    ? diagonal_wins
                    : off_diagonal_wins;
            cell.losses = cell.games - cell.wins;
        }
    }
    recompute_benchmark_totals(summary);
    return summary;
}

BotBenchmarkSummary passing_direct_panel() {
    return benchmark_fixture(
        joint::kDirectPanelRepetitions, 84, 37);
}

BotBenchmarkSummary passing_fixed_seed_panel() {
    return benchmark_fixture(
        joint::kFixedSeedPanelRepetitions, 10, 5);
}

void transfer_cell_wins(
    BotBenchmarkSummary& summary, std::size_t row,
    std::size_t from_column, std::size_t to_column,
    std::size_t wins) {
    auto& from =
        summary.challenger_deck_matchups
            [row][from_column];
    auto& to =
        summary.challenger_deck_matchups
            [row][to_column];
    from.wins -= wins;
    from.losses += wins;
    to.wins += wins;
    to.losses -= wins;
    recompute_benchmark_totals(summary);
}

void recompute_count_totals(
    joint::BenchmarkCountSummary& summary) {
    summary.challenger = {};
    summary.baseline = {};
    summary.challenger_decks = {};
    summary.baseline_decks = {};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount; ++opponent) {
            const auto& cell =
                summary.challenger_deck_matchups
                    [deck][opponent];
            add_stats(summary.challenger_decks[deck], cell);
            add_stats(summary.challenger, cell);
            add_stats(
                summary.baseline_decks[opponent],
                cell, true);
            add_stats(summary.baseline, cell, true);
        }
    }
    summary.total_games = summary.challenger.games;
}

std::vector<BotBenchmarkSummary> passing_fixed_seed_panels() {
    return std::vector<BotBenchmarkSummary>(
        joint::kFixedSeedPanelCount,
        passing_fixed_seed_panel());
}

joint::BenchmarkCountSummary passing_final_pool() {
    const auto direct = passing_direct_panel();
    const auto fixed = passing_fixed_seed_panels();
    const auto merged =
        joint::merge_final_direct_panels(direct, fixed);
    expect(merged.has_value(), "passing panels must merge");
    return *merged;
}

TournamentSummary passing_mixed_field_pool() {
    TournamentSummary summary;
    summary.total_games = joint::kMixedFieldTotalGames;
    constexpr std::array<std::size_t, old_school::kBotKindCount>
        wins = {200, 280, 320, 340, 460};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t bot = 0;
             bot < old_school::kBotKindCount; ++bot) {
            auto& stats = summary.deck_bots[deck][bot];
            stats.games =
                joint::kMixedFieldGamesPerDeckPolicy;
            stats.wins = wins[bot];
            stats.losses = stats.games - stats.wins;
        }
    }
    return summary;
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

void test_direct_gameplay_gate_uses_challenger_own_deck_outcomes() {
    auto summary = passing_direct_panel();
    transfer_cell_wins(summary, 0, 0, 1, 30);
    for (std::size_t deck = 1;
         deck < old_school::kDeckCount; ++deck) {
        transfer_cell_wins(summary, deck, 0, deck, 10);
    }

    const auto green =
        static_cast<std::size_t>(DeckId::Green);
    expect(
        summary.challenger_decks[green].wins <
            summary.baseline_decks[green].wins,
        "fixture must trigger the historical baseline-deck trap");
    const auto gate =
        joint::evaluate_direct_gameplay_panel(summary);
    expect(gate.accounting_exact, "direct exact accounting");
    expect(gate.rates_finite, "direct finite rates");
    expect(
        gate.aggregate_strict_win &&
            gate.wilson_lower_above_half,
        "direct aggregate strength gates");
    expect(
        gate.challenger_deck_strict_wins[green] &&
            gate.every_challenger_deck_strict_win,
        "deck wins must be compared with that challenger's losses");
    expect(gate.passed, "skewed baseline buckets must not reject");
}

void test_direct_gameplay_strict_wilson_and_count_boundaries() {
    auto gate =
        joint::evaluate_direct_gameplay_panel(
            passing_direct_panel());
    expect(gate.passed, "passing direct fixture");

    auto summary = benchmark_fixture(
        joint::kDirectPanelRepetitions, 68, 34);
    gate = joint::evaluate_direct_gameplay_panel(summary);
    expect(
        gate.accounting_exact &&
            !gate.aggregate_strict_win &&
            !gate.every_challenger_deck_strict_win &&
            !gate.passed,
        "direct equality must fail strict win gates");

    summary = benchmark_fixture(
        joint::kDirectPanelRepetitions, 69, 34);
    gate = joint::evaluate_direct_gameplay_panel(summary);
    expect(
        gate.accounting_exact &&
            gate.aggregate_strict_win &&
            gate.every_challenger_deck_strict_win &&
            !gate.wilson_lower_above_half &&
            !gate.passed,
        "small strict edge must still fail Wilson lower bound");

    summary = passing_direct_panel();
    --summary.repetitions_per_deck_pairing;
    gate = joint::evaluate_direct_gameplay_panel(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "33 repetitions must fail exact accounting");

    summary = passing_direct_panel();
    --summary.total_games;
    gate = joint::evaluate_direct_gameplay_panel(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "2,039 reported games must fail exact accounting");

    summary = passing_direct_panel();
    auto& diagonal_zero =
        summary.challenger_deck_matchups[0][0];
    auto& diagonal_one =
        summary.challenger_deck_matchups[1][1];
    auto& off_zero_one =
        summary.challenger_deck_matchups[0][1];
    auto& off_one_zero =
        summary.challenger_deck_matchups[1][0];
    --diagonal_zero.games;
    --diagonal_zero.losses;
    --diagonal_one.games;
    --diagonal_one.losses;
    ++off_zero_one.games;
    ++off_zero_one.losses;
    ++off_one_zero.games;
    ++off_one_zero.losses;
    recompute_benchmark_totals(summary);
    gate = joint::evaluate_direct_gameplay_panel(summary);
    expect(
        summary.total_games == joint::kDirectPanelGames &&
            !gate.accounting_exact && !gate.passed,
        "wrong diagonal/off-diagonal matrix must fail");

    summary = passing_direct_panel();
    summary.total_games =
        std::numeric_limits<std::size_t>::max();
    summary.challenger_stats.games =
        std::numeric_limits<std::size_t>::max();
    summary.challenger_stats.wins =
        std::numeric_limits<std::size_t>::max();
    summary.challenger_stats.losses = 1;
    gate = joint::evaluate_direct_gameplay_panel(summary);
    expect(
        !gate.accounting_exact &&
            !gate.rates_finite && !gate.passed,
        "overflowing outcome counts must fail closed");
}

void test_fixed_seed_panels_are_exact_and_non_losing() {
    const auto tie_panel = passing_fixed_seed_panel();
    auto gate = joint::evaluate_fixed_seed_panel(tie_panel);
    expect(
        gate.accounting_exact &&
            gate.aggregate_non_losing && gate.passed,
        "fixed panel equality must pass non-losing gate");

    auto losing_panel = tie_panel;
    auto& cell =
        losing_panel.challenger_deck_matchups[0][0];
    --cell.wins;
    ++cell.losses;
    recompute_benchmark_totals(losing_panel);
    gate = joint::evaluate_fixed_seed_panel(losing_panel);
    expect(
        gate.accounting_exact &&
            !gate.aggregate_non_losing && !gate.passed,
        "149-151 fixed panel must fail");

    auto malformed_panel = tie_panel;
    auto& diagonal_zero =
        malformed_panel.challenger_deck_matchups[0][0];
    auto& diagonal_one =
        malformed_panel.challenger_deck_matchups[1][1];
    auto& off_zero_one =
        malformed_panel.challenger_deck_matchups[0][1];
    auto& off_one_zero =
        malformed_panel.challenger_deck_matchups[1][0];
    --diagonal_zero.games;
    --diagonal_zero.losses;
    --diagonal_one.games;
    --diagonal_one.losses;
    ++off_zero_one.games;
    ++off_zero_one.losses;
    ++off_one_zero.games;
    ++off_one_zero.losses;
    recompute_benchmark_totals(malformed_panel);
    gate = joint::evaluate_fixed_seed_panel(malformed_panel);
    expect(
        malformed_panel.total_games ==
                joint::kFixedSeedPanelGames &&
            !gate.accounting_exact && !gate.passed,
        "19/11 fixed matrix cells must fail");

    malformed_panel = tie_panel;
    --malformed_panel.total_games;
    gate = joint::evaluate_fixed_seed_panel(malformed_panel);
    expect(
        !gate.accounting_exact && !gate.passed,
        "299 reported fixed games must fail");

    malformed_panel = tie_panel;
    --malformed_panel.repetitions_per_deck_pairing;
    gate = joint::evaluate_fixed_seed_panel(malformed_panel);
    expect(
        !gate.accounting_exact && !gate.passed,
        "four fixed repetitions must fail");

    auto panels = passing_fixed_seed_panels();
    auto set_gate =
        joint::evaluate_fixed_seed_panel_set(panels);
    expect(
        set_gate.panel_count_exact &&
            set_gate.panels.size() ==
                joint::kFixedSeedPanelCount &&
            set_gate.every_panel_passed &&
            set_gate.passed,
        "all eight fixed panels must pass");

    panels.pop_back();
    set_gate = joint::evaluate_fixed_seed_panel_set(panels);
    expect(
        !set_gate.panel_count_exact && !set_gate.passed,
        "seven fixed panels must fail");

    panels = passing_fixed_seed_panels();
    panels[3] = losing_panel;
    set_gate = joint::evaluate_fixed_seed_panel_set(panels);
    expect(
        set_gate.panel_count_exact &&
            !set_gate.every_panel_passed &&
            !set_gate.passed,
        "one losing evaluation seed must fail the set");
}

void test_final_pool_merge_and_exact_accounting() {
    const auto direct = passing_direct_panel();
    auto fixed = passing_fixed_seed_panels();
    const auto merged =
        joint::merge_final_direct_panels(direct, fixed);
    expect(merged.has_value(), "exact final panels must merge");
    expect(
        merged->panel_count ==
                joint::kFinalDirectPanelCount &&
            merged->repetitions_per_deck_pairing ==
                joint::kFinalDirectRepetitions &&
            merged->total_games ==
                joint::kFinalDirectGames,
        "final aggregate counts");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            merged->challenger_decks[deck].games ==
                joint::kFinalDirectGamesPerDeck,
            "888 games per challenger deck");
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount;
             ++opponent) {
            const auto expected =
                deck == opponent
                    ? joint::kFinalDirectDiagonalGames
                    : joint::kFinalDirectOffDiagonalGames;
            expect(
                merged->challenger_deck_matchups
                    [deck][opponent]
                        .games == expected,
                "final matrix cell count");
        }
    }
    expect(
        joint::evaluate_final_direct_pool(*merged).passed,
        "passing final pool");

    fixed.pop_back();
    expect(
        !joint::merge_final_direct_panels(
             direct, fixed)
             .has_value(),
        "seven panels must not merge");

    fixed = passing_fixed_seed_panels();
    --fixed[2].repetitions_per_deck_pairing;
    expect(
        !joint::merge_final_direct_panels(
             direct, fixed)
             .has_value(),
        "malformed fixed panel must not merge");
}

void test_final_pool_strength_and_matrix_boundaries() {
    auto summary = passing_final_pool();
    auto gate =
        joint::evaluate_final_direct_pool(summary);
    expect(
        gate.accounting_exact &&
            gate.aggregate_strict_win &&
            gate.wilson_lower_above_half &&
            gate.every_challenger_deck_strict_win &&
            gate.passed,
        "final pool passing strength gates");

    auto wilson_failure = summary;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount;
             ++opponent) {
            auto& cell =
                wilson_failure.challenger_deck_matchups
                    [deck][opponent];
            if (deck == opponent) {
                cell.wins = 149;
                cell.losses = 147;
            } else {
                cell.wins = 74;
                cell.losses = 74;
            }
        }
    }
    recompute_count_totals(wilson_failure);
    gate =
        joint::evaluate_final_direct_pool(wilson_failure);
    expect(
        gate.accounting_exact &&
            gate.aggregate_strict_win &&
            gate.every_challenger_deck_strict_win &&
            !gate.wilson_lower_above_half &&
            !gate.passed,
        "final small strict edge must fail pooled Wilson gate");

    auto deck_tie = summary;
    auto& tie_cell =
        deck_tie.challenger_deck_matchups[0][0];
    tie_cell.wins -= 28;
    tie_cell.losses += 28;
    recompute_count_totals(deck_tie);
    gate = joint::evaluate_final_direct_pool(deck_tie);
    expect(
        gate.accounting_exact &&
            gate.aggregate_strict_win &&
            gate.wilson_lower_above_half &&
            !gate.challenger_deck_strict_wins[0] &&
            !gate.every_challenger_deck_strict_win &&
            !gate.passed,
        "one 444-444 challenger deck must fail strictly");

    auto wrong_matrix = summary;
    auto& diagonal_zero =
        wrong_matrix.challenger_deck_matchups[0][0];
    auto& diagonal_one =
        wrong_matrix.challenger_deck_matchups[1][1];
    auto& off_zero_one =
        wrong_matrix.challenger_deck_matchups[0][1];
    auto& off_one_zero =
        wrong_matrix.challenger_deck_matchups[1][0];
    --diagonal_zero.games;
    --diagonal_zero.losses;
    --diagonal_one.games;
    --diagonal_one.losses;
    ++off_zero_one.games;
    ++off_zero_one.losses;
    ++off_one_zero.games;
    ++off_one_zero.losses;
    recompute_count_totals(wrong_matrix);
    gate = joint::evaluate_final_direct_pool(wrong_matrix);
    expect(
        wrong_matrix.total_games ==
                joint::kFinalDirectGames &&
            !gate.accounting_exact && !gate.passed,
        "295/149 matrix cells must fail exact final layout");

    summary.panel_count =
        joint::kFinalDirectPanelCount - 1;
    gate = joint::evaluate_final_direct_pool(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "eight merged panels must fail final accounting");

    summary = passing_final_pool();
    --summary.repetitions_per_deck_pairing;
    gate = joint::evaluate_final_direct_pool(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "73 final repetitions must fail accounting");

    summary = passing_final_pool();
    --summary.total_games;
    gate = joint::evaluate_final_direct_pool(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "4,439 reported final games must fail accounting");
}

void test_mixed_field_gate_covers_all_decks_and_policies() {
    const auto summary = passing_mixed_field_pool();
    const auto gate =
        joint::evaluate_mixed_field_pool(summary);
    expect(
        gate.accounting_exact &&
            gate.rates_finite &&
            gate.learned_lift_best_on_every_deck &&
            gate.passed,
        "passing mixed-field pool");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto& deck_gate = gate.by_deck[deck];
        expect(
            deck_gate.deck == static_cast<DeckId>(deck) &&
                deck_gate.rates_finite &&
                deck_gate.learned_lift_is_best &&
                deck_gate.best_other ==
                    BotKind::Handcrafted,
            "all five deck lift reports");
        expect_near(
            deck_gate.learned_lift_percentage_points,
            40.625, 1e-12,
            "Learned lift percentage points");
    }

    const auto learned =
        static_cast<std::size_t>(BotKind::Learned);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t bot = 0;
             bot < old_school::kBotKindCount; ++bot) {
            if (bot == learned) {
                continue;
            }
            auto challenger = passing_mixed_field_pool();
            auto& other = challenger.deck_bots[deck][bot];
            auto& donor = challenger.deck_bots
                [deck][bot == 0 ? 1 : 0];
            const std::size_t increase =
                challenger.deck_bots[deck][learned].wins +
                1 - other.wins;
            other.wins += increase;
            other.losses -= increase;
            donor.wins -= increase;
            donor.losses += increase;
            const auto challenged =
                joint::evaluate_mixed_field_pool(challenger);
            expect(
                challenged.accounting_exact &&
                    !challenged.by_deck[deck]
                         .learned_lift_is_best &&
                    !challenged.passed,
                "each other policy can defeat Learned lift");
        }
    }
}

void test_mixed_field_ties_counts_and_overflow_fail_closed() {
    auto summary = passing_mixed_field_pool();
    const auto random =
        static_cast<std::size_t>(BotKind::Random);
    const auto handcoded =
        static_cast<std::size_t>(BotKind::Handcrafted);
    const auto learned =
        static_cast<std::size_t>(BotKind::Learned);
    auto& random_stats = summary.deck_bots[0][random];
    auto& handcoded_stats = summary.deck_bots[0][handcoded];
    const std::size_t tie_increase =
        summary.deck_bots[0][learned].wins -
        handcoded_stats.wins;
    handcoded_stats.wins += tie_increase;
    handcoded_stats.losses -= tie_increase;
    random_stats.wins -= tie_increase;
    random_stats.losses += tie_increase;
    auto gate = joint::evaluate_mixed_field_pool(summary);
    expect(
        gate.accounting_exact &&
            gate.by_deck[0].learned_lift_is_best &&
            gate.passed,
        "exact lift equality must pass 1e-12 tolerance");

    summary = passing_mixed_field_pool();
    --summary.total_games;
    gate = joint::evaluate_mixed_field_pool(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "7,999 mixed games must fail");

    summary = passing_mixed_field_pool();
    auto& short_cell = summary.deck_bots[0][0];
    auto& long_cell = summary.deck_bots[0][1];
    --short_cell.games;
    --short_cell.losses;
    ++long_cell.games;
    ++long_cell.losses;
    gate = joint::evaluate_mixed_field_pool(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "639/641 cell redistribution must fail");

    summary = passing_mixed_field_pool();
    summary.draws = 1;
    gate = joint::evaluate_mixed_field_pool(summary);
    expect(
        !gate.accounting_exact && !gate.passed,
        "physical draw count must match seat draws");

    summary = passing_mixed_field_pool();
    auto& overflow = summary.deck_bots[0][0];
    overflow.games =
        std::numeric_limits<std::size_t>::max();
    overflow.wins =
        std::numeric_limits<std::size_t>::max();
    overflow.losses = 1;
    gate = joint::evaluate_mixed_field_pool(summary);
    expect(
        !gate.accounting_exact &&
            !gate.rates_finite && !gate.passed,
        "mixed-field overflow must fail closed");
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
        "direct challenger-deck semantics",
        test_direct_gameplay_gate_uses_challenger_own_deck_outcomes);
    tests.run(
        "direct strict/Wilson/count boundaries",
        test_direct_gameplay_strict_wilson_and_count_boundaries);
    tests.run(
        "fixed-seed exact non-losing panels",
        test_fixed_seed_panels_are_exact_and_non_losing);
    tests.run(
        "final pool exact deterministic merge",
        test_final_pool_merge_and_exact_accounting);
    tests.run(
        "final pool strength/matrix boundaries",
        test_final_pool_strength_and_matrix_boundaries);
    tests.run(
        "mixed-field all deck-policy lifts",
        test_mixed_field_gate_covers_all_decks_and_policies);
    tests.run(
        "mixed-field ties/counts/overflow",
        test_mixed_field_ties_counts_and_overflow_fail_closed);
    tests.run(
        "no-salvage stage suppression",
        test_stage_decision_suppresses_every_later_stage);
    return tests.finish();
}
