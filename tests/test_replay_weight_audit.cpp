#include "old_school/replay_weight_audit.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace rb0 = old_school::replay_weight_audit;
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
                  << " replay-weight audit tests passed\n";
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

template <typename Exception, typename Function>
void expect_throws(
    Function&& function, std::string_view message) {
    bool threw = false;
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        threw = true;
    }
    expect(threw, message);
}

rb0::AuditRecord record(
    std::size_t game, std::size_t turn, double error,
    double weight = 1.0, DeckId deck = DeckId::Green,
    bool eligible = true) {
    const std::size_t root = turn - 1;
    return {
        .physical_game = game,
        .block = 0,
        .schedule_index =
            game %
            old_school::learned_iteration::
                kBalancedScheduleGames,
        .perspective = game % 2,
        .deck = deck,
        .root_index = root,
        .root_turn = turn,
        .terminal_target = 0.5,
        .ro4_target = eligible ? 0.5 + error : 0.5,
        .ro4_future_index =
            eligible
                ? std::optional<std::size_t>(
                      root + rb0::kRecordBootstrapDistance)
                : std::nullopt,
        .treatment_weight = weight,
    };
}

old_school::terminal_weight_eval::ClusteredEstimate estimate(
    double mean, double lower, double upper) {
    return {
        .records = 10000,
        .clusters = 2400,
        .mean = mean,
        .standard_error = (upper - lower) / 3.92,
        .confidence_lower_95 = lower,
        .confidence_upper_95 = upper,
    };
}

void set_comparison(
    rb0::MetricComparison& comparison,
    double control, double treatment, double delta,
    double delta_upper) {
    comparison.control =
        estimate(control, control - 0.002, control + 0.002);
    comparison.treatment =
        estimate(
            treatment, treatment - 0.002,
            treatment + 0.002);
    comparison.treatment_minus_control =
        estimate(
            delta, delta - 0.002, delta_upper);
}

void set_passing_rows(rb0::RowSetMetrics& rows) {
    rows.available = true;
    rows.records = 10000;
    rows.actor_games = 4800;
    rows.physical_games = 2400;
    rows.bootstrapped_records = 9000;
    rows.terminal_tail_records = 1000;
    rows.weights.kish_effective_sample_size = 8000.0;
    set_comparison(
        rows.metrics.signed_bias,
        0.020, 0.015, -0.005, -0.001);
    set_comparison(
        rows.metrics.brier,
        0.100, 0.099, -0.001, 0.0005);
    set_comparison(
        rows.metrics.soft_log_loss,
        0.300, 0.299, -0.001, 0.0005);
}

rb0::ScientificReport passing_report() {
    rb0::ScientificReport report;
    report.seed = 123;
    report.generation = rb0::kAuditGeneration;
    report.balanced_blocks = rb0::kAuditBalancedBlocks;
    report.physical_games = rb0::kAuditPhysicalGames;
    report.actor_games = rb0::kAuditActorGames;
    report.schedule_balanced = true;
    report.eligible_coverage_passed = true;
    report.early_green_control_qualified = true;
    report.kish_qualification_passed = true;
    report.trace_invariants_passed = true;
    report.ro4_identity_passed = true;
    report.terminal_tail_identity_passed = true;
    report.hidden_repartition_passed = true;
    report.hidden_grouping_identity_passed = true;
    report.hidden_target_hash_identity_passed = true;
    report.hidden_weight_identity_passed = true;
    report.hidden_scoring_hash_identity_passed = true;
    report.hidden_repartition_states = 1;
    report.weight_identity_passed = true;
    set_passing_rows(report.metrics.pooled.all_records);
    set_passing_rows(report.metrics.pooled.ro4_eligible);
    for (auto& deck : report.metrics.by_deck) {
        set_passing_rows(deck.all_records);
        set_passing_rows(deck.ro4_eligible);
    }
    auto& green =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)];
    auto& early =
        green.eligible_by_root_turn[
            static_cast<std::size_t>(
                rb0::RootTurnStratum::Early)];
    set_passing_rows(early);
    set_comparison(
        early.metrics.signed_bias,
        0.030, 0.020, -0.010, -0.005);
    return report;
}

void test_balanced_schedule() {
    const auto schedule =
        rb0::audit_schedule(987654321ULL, 3, 2);
    expect(
        schedule.size() ==
            2 *
                old_school::learned_iteration::
                    kBalancedScheduleGames,
        "RB0 schedule size");
    for (std::size_t index = 0;
         index < schedule.size(); ++index) {
        expect(
            schedule[index].physical_game == index,
            "RB0 schedule physical-game identity");
        expect(
            schedule[index].block ==
                index /
                    old_school::learned_iteration::
                        kBalancedScheduleGames,
            "RB0 schedule block identity");
    }
    std::array<
        std::array<std::size_t, old_school::kDeckCount>,
        old_school::kDeckCount>
        ordered{};
    for (const rb0::AuditTask& task : schedule) {
        ++ordered[
            static_cast<std::size_t>(
                task.scheduled.seat_decks[0])][
            static_cast<std::size_t>(
                task.scheduled.seat_decks[1])];
    }
    for (std::size_t first = 0;
         first < old_school::kDeckCount; ++first) {
        for (std::size_t second = 0;
             second < old_school::kDeckCount; ++second) {
            expect(
                ordered[first][second] ==
                    (first == second ? 0U : 4U),
                "RB0 ordered distinct-deck pairing balance");
        }
    }
}

void test_uniform_hierarchy_is_bit_exact_unit_weight() {
    std::vector<rb0::ReplayCoordinate> coordinates;
    for (std::size_t game = 0; game < 3; ++game) {
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            for (std::size_t turn = 1; turn <= 4; ++turn) {
                coordinates.push_back({
                    .physical_game = game,
                    .perspective = perspective,
                    .calendar_turn = turn,
                });
            }
        }
    }
    const auto weights =
        rb0::hierarchical_weights(coordinates);
    expect(weights.size() == coordinates.size(), "unit weight count");
    for (const double weight : weights) {
        expect(
            std::bit_cast<std::uint64_t>(weight) ==
                std::bit_cast<std::uint64_t>(1.0),
            "uniform hierarchy must be bit-exact 1.0");
    }
}

void test_uneven_hierarchy_equalizes_actor_and_turn_mass() {
    const std::vector<rb0::ReplayCoordinate> coordinates = {
        {0, 0, 1},
        {0, 0, 1},
        {0, 0, 2},
        {1, 0, 1},
    };
    const auto weights =
        rb0::hierarchical_weights(coordinates);
    expect(weights.size() == 4, "uneven weight count");
    expect_near(weights[0], 0.5, 0.0, "first repeated row");
    expect_near(weights[1], 0.5, 0.0, "second repeated row");
    expect_near(weights[2], 1.0, 0.0, "second turn");
    expect_near(weights[3], 2.0, 0.0, "short actor");
    expect_near(
        weights[0] + weights[1] + weights[2],
        2.0, 0.0, "first actor mass");
    expect_near(weights[3], 2.0, 0.0, "second actor mass");
    expect_near(
        weights[0] + weights[1],
        weights[2], 0.0, "equal within-actor turn mass");
}

void test_hierarchy_rejects_malformed_coordinates() {
    expect_throws<std::invalid_argument>(
        [] {
            static_cast<void>(
                rb0::hierarchical_weights({}));
        },
        "empty hierarchy");
    const std::vector<rb0::ReplayCoordinate> bad_player = {
        {0, 2, 1},
    };
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                rb0::hierarchical_weights(bad_player));
        },
        "invalid perspective");
    const std::vector<rb0::ReplayCoordinate> bad_turn = {
        {0, 0, 0},
    };
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                rb0::hierarchical_weights(bad_turn));
        },
        "zero calendar turn");
}

void test_unit_weight_reducer_is_bit_exact_control() {
    std::vector<rb0::AuditRecord> records;
    for (std::size_t game = 0; game < 2; ++game) {
        records.push_back(record(game, 1, 0.10));
        records.push_back(record(game, 4, -0.05));
        records.push_back(record(game, 8, 0.02));
    }
    const auto report = rb0::score_records(records);
    const auto& metrics =
        report.pooled.all_records.metrics;
    for (const rb0::MetricComparison* comparison : {
             &metrics.signed_bias,
             &metrics.brier,
             &metrics.soft_log_loss}) {
        expect(
            comparison->control == comparison->treatment,
            "unit weighted estimate must equal legacy control "
            "bit-for-bit");
        expect(
            comparison->treatment_minus_control ==
                old_school::terminal_weight_eval::
                    ClusteredEstimate{
                        .records = records.size(),
                        .clusters = 2,
                        .mean = 0.0,
                        .standard_error = 0.0,
                        .confidence_lower_95 = 0.0,
                        .confidence_upper_95 = 0.0,
                    },
            "unit weighted contrast must be exact zero");
    }
}

void test_two_ratio_cluster_score_has_no_extra_division() {
    std::vector<rb0::AuditRecord> records = {
        record(0, 1, 0.10, 1.0),
        record(1, 1, 0.30, 3.0),
        record(2, 4, 0.10, 1.0),
        record(3, 4, 0.10, 1.0),
        record(4, 8, 0.10, 1.0),
        record(5, 8, 0.10, 1.0),
    };
    const auto report = rb0::score_records(records);
    const auto& early =
        report.by_deck[
                static_cast<std::size_t>(DeckId::Green)]
            .eligible_by_root_turn[
                static_cast<std::size_t>(
                    rb0::RootTurnStratum::Early)]
            .metrics.signed_bias;
    expect_near(
        early.control.mean, 0.20, 1.0e-15,
        "two-ratio control mean");
    expect_near(
        early.treatment.mean, 0.25, 1.0e-15,
        "two-ratio treatment mean");
    expect_near(
        early.treatment_minus_control.mean,
        0.05, 1.0e-15, "two-ratio contrast mean");
    // U_0=+0.0125 and U_1=-0.0125. CR1 is
    // sqrt(2/(2-1) * sum_g U_g^2) = 0.025, with no /G.
    expect_near(
        early.treatment_minus_control.standard_error,
        0.025, 1.0e-14,
        "two-ratio CR1 must not divide normalized scores again");
}

void test_scope_keeps_global_weights_and_renormalizes_mean_only() {
    std::vector<rb0::AuditRecord> records = {
        record(0, 1, 0.10, 1.0),
        record(1, 1, 0.30, 3.0),
        record(2, 4, -0.10, 100.0),
        record(3, 4, -0.10, 100.0),
        record(4, 8, 0.00, 1.0),
        record(5, 8, 0.00, 1.0),
    };
    const auto report = rb0::score_records(records);
    const auto& early =
        report.pooled.eligible_by_root_turn[
            static_cast<std::size_t>(
                rb0::RootTurnStratum::Early)];
    expect_near(
        early.weights.total_weight, 4.0, 0.0,
        "scope preserves raw global weights");
    expect_near(
        early.metrics.signed_bias.treatment.mean,
        0.25, 1.0e-15,
        "scope renormalizes only its weighted mean");
}

void test_scoring_rejects_bad_tail_identity() {
    std::vector<rb0::AuditRecord> records = {
        record(0, 1, 0.0, 1.0, DeckId::Green, false),
    };
    records[0].ro4_target = 0.6;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                rb0::score_records(records));
        },
        "bad terminal-tail identity");
}

void test_gate_and_soft_log_guard() {
    const auto passing = passing_report();
    const auto gate = rb0::evaluate_gate(passing);
    expect(gate.passed, "passing RB0 gate fixture");
    expect(
        gate.per_deck_soft_log_guard,
        "passing soft-log deck guard");

    auto bad = passing;
    auto& comparison =
        bad.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Blue)]
            .ro4_eligible.metrics.soft_log_loss
            .treatment_minus_control;
    comparison.mean = 0.0021;
    comparison.confidence_upper_95 = 0.0029;
    const auto rejected = rb0::evaluate_gate(bad);
    expect(
        !rejected.per_deck_soft_log_guard &&
            !rejected.passed,
        "per-deck soft-log point guard");
}

void test_qualification_failure_is_scientific_exit_one() {
    auto scientific = passing_report();
    scientific.eligible_coverage_passed = false;
    const auto gate = rb0::evaluate_gate(scientific);
    expect(
        !gate.evidence_complete &&
            gate.mechanical_invariants,
        "coverage miss is scientific, not mechanical");
    rb0::AuditReport report;
    report.parent_fingerprint =
        std::string(rb0::kParentFingerprint);
    report.scientific = scientific;
    report.gate = gate;
    report.repeated_report_bit_identical = true;
    report.reversed_input_bit_identical = true;
    report.worker_reduction_bit_identical = true;
    report.artifact_unchanged = true;
    expect(
        rb0::infrastructure_complete(report),
        "qualification miss must leave infrastructure complete");
    expect(
        rb0::audit_exit_code(
            rb0::infrastructure_complete(report),
            gate.passed) == 1,
        "qualification miss must exit 1");
}

void test_test_scale_collection_is_order_and_worker_deterministic() {
    constexpr std::uint64_t model_seed = 81726354ULL;
    constexpr std::uint64_t corpus_seed = 19283746ULL;
    const auto parent =
        old_school::train_learned_value_challenger(
            1, model_seed, 2);
    const auto full =
        rb0::audit_schedule(corpus_seed, 2, 1);
    std::vector<rb0::AuditTask> tasks(
        full.begin(), full.begin() + 2);
    const rb0::CaptureConfig parallel_config = {
        .max_game_turns = 80,
        .worker_count = 2,
        .verify_hidden_repartition = true,
        .schedule_seed = corpus_seed,
        .schedule_generation = 2,
    };
    const auto first =
        rb0::collect(
            tasks, parent, parallel_config);
    const auto repeated =
        rb0::collect(
            tasks, parent, parallel_config);
    std::vector<rb0::AuditTask> reversed = tasks;
    std::reverse(reversed.begin(), reversed.end());
    const auto reversed_capture =
        rb0::collect(
            reversed, parent, parallel_config);
    const auto serial =
        rb0::collect(
            tasks, parent,
            {
                .max_game_turns = 80,
                .worker_count = 1,
                .verify_hidden_repartition = true,
                .schedule_seed = corpus_seed,
                .schedule_generation = 2,
            });
    expect(
        first == repeated,
        "RB0 repeated fresh construction determinism");
    expect(
        first == reversed_capture,
        "RB0 reverse-input determinism");
    expect(
        first == serial,
        "RB0 fixed one-versus-N worker determinism");
    expect(
        first.trace_invariants_passed &&
            first.ro4_identity_passed &&
            first.terminal_tail_identity_passed &&
            first.hidden_repartition_passed &&
            first.hidden_grouping_identity_passed &&
            first.hidden_target_hash_identity_passed &&
            first.hidden_weight_identity_passed &&
            first.hidden_scoring_hash_identity_passed &&
            first.hidden_repartition_states > 0 &&
            first.weight_identity_passed,
        "RB0 test-scale collection safeguards");
    expect(
        !first.records.empty(),
        "RB0 test-scale collection rows");
    for (const rb0::AuditRecord& row : first.records) {
        if (row.ro4_future_index.has_value()) {
            expect(
                *row.ro4_future_index ==
                    row.root_index +
                        rb0::kRecordBootstrapDistance,
                "RB0 collected future offset");
        } else {
            expect(
                std::bit_cast<std::uint64_t>(
                    row.ro4_target) ==
                    std::bit_cast<std::uint64_t>(
                        row.terminal_target),
                "RB0 collected terminal tail identity");
        }
    }
}

void test_collection_rejects_noncanonical_task_binding() {
    constexpr std::uint64_t model_seed = 44556677ULL;
    constexpr std::uint64_t corpus_seed = 77665544ULL;
    const auto parent =
        old_school::train_learned_value_challenger(
            1, model_seed, 2);
    const auto schedule =
        rb0::audit_schedule(corpus_seed, 3, 1);
    const rb0::CaptureConfig config = {
        .max_game_turns = 20,
        .worker_count = 1,
        .verify_hidden_repartition = false,
        .schedule_seed = corpus_seed,
        .schedule_generation = 3,
    };
    std::vector<rb0::AuditTask> wrong_seed = {
        schedule.front(),
    };
    ++wrong_seed[0].scheduled.seed;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                rb0::collect(
                    wrong_seed, parent, config));
        },
        "RB0 altered scheduled seed");
    std::vector<rb0::AuditTask> wrong_identity = {
        schedule.front(),
    };
    ++wrong_identity[0].physical_game;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                rb0::collect(
                    wrong_identity, parent, config));
        },
        "RB0 altered physical-game identity");
}

void test_reports_are_stable_and_explicit_about_post_run_effect() {
    rb0::AuditReport report;
    report.parent_fingerprint =
        std::string(rb0::kParentFingerprint);
    report.scientific = passing_report();
    report.gate = rb0::evaluate_gate(report.scientific);
    report.repeated_report_bit_identical = true;
    report.reversed_input_bit_identical = true;
    report.worker_reduction_bit_identical = true;
    report.artifact_unchanged = true;
    report.passed = report.gate.passed;
    std::ostringstream human;
    std::ostringstream tsv;
    rb0::write_human_report(report, human);
    rb0::write_tsv_report(report, tsv);
    expect(
        human.str().find(
            "post-run approximate detectable effect "
            "(2.802*SE, normal)") != std::string::npos,
        "human report post-run effect label");
    expect(
        tsv.str().starts_with("RB0_TSV_BEGIN\n") &&
            tsv.str().find("RB0_TSV_END\n") !=
                std::string::npos,
        "stable TSV framing");
    expect(
        tsv.str().find("per_deck_soft_log_guard") !=
            std::string::npos,
        "TSV soft-log gate");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "balanced schedule", test_balanced_schedule);
    runner.run(
        "uniform hierarchy is bit-exact unit weight",
        test_uniform_hierarchy_is_bit_exact_unit_weight);
    runner.run(
        "uneven hierarchy equalizes actor and turn mass",
        test_uneven_hierarchy_equalizes_actor_and_turn_mass);
    runner.run(
        "hierarchy rejects malformed coordinates",
        test_hierarchy_rejects_malformed_coordinates);
    runner.run(
        "unit-weight reducer is bit-exact control",
        test_unit_weight_reducer_is_bit_exact_control);
    runner.run(
        "two-ratio cluster score has no extra division",
        test_two_ratio_cluster_score_has_no_extra_division);
    runner.run(
        "scope keeps global weights",
        test_scope_keeps_global_weights_and_renormalizes_mean_only);
    runner.run(
        "scoring rejects bad tail identity",
        test_scoring_rejects_bad_tail_identity);
    runner.run(
        "gate and soft-log guard",
        test_gate_and_soft_log_guard);
    runner.run(
        "qualification failure is scientific exit one",
        test_qualification_failure_is_scientific_exit_one);
    runner.run(
        "test-scale collection is deterministic",
        test_test_scale_collection_is_order_and_worker_deterministic);
    runner.run(
        "collection rejects noncanonical task binding",
        test_collection_rejects_noncanonical_task_binding);
    runner.run(
        "reports have stable explicit labels",
        test_reports_are_stable_and_explicit_about_post_run_effect);
    return runner.finish();
}
