#include "old_school/target_factorial_audit.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace ct8 = old_school::target_factorial_audit;
namespace ta4 = old_school::turn_alignment_audit;
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
                  << " target-factorial audit tests passed\n";
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

constexpr std::size_t arm(ct8::TargetArm value) {
    return static_cast<std::size_t>(value);
}

constexpr std::size_t contrast(ct8::Contrast value) {
    return static_cast<std::size_t>(value);
}

ta4::AuditRecord common_record(
    std::size_t physical_game, DeckId deck,
    std::size_t root_index, std::size_t root_turn) {
    return {
        .physical_game = physical_game,
        .block = 0,
        .schedule_index = physical_game,
        .perspective = physical_game % 2,
        .deck = deck,
        .root_index = root_index,
        .root_turn = root_turn,
        .terminal_target = 0.5,
        .control_target = 0.70,
        .treatment_target = 0.60,
        .control_future_index = root_index + 4,
        .treatment_future_index = root_index + 6,
        .control_turn_distance = 2,
        .treatment_turn_distance =
            ta4::kTurnBootstrapAdvances,
        .record_offset8_target = 0.65,
        .calendar_turn8_target = 0.52,
        .record_offset8_future_index = root_index + 8,
        .calendar_turn8_future_index = root_index + 12,
        .record_offset8_turn_distance = 4,
        .calendar_turn8_turn_distance =
            ta4::kCalendarTurn8Advances,
    };
}

std::vector<ta4::AuditRecord> metric_fixture() {
    std::vector<ta4::AuditRecord> records;
    constexpr std::array<std::size_t, 3> turns = {3, 4, 8};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t game = 0; game < 2; ++game) {
            const std::size_t physical_game =
                2 * deck + game;
            for (std::size_t root = 0;
                 root < turns.size(); ++root) {
                records.push_back(common_record(
                    physical_game,
                    static_cast<DeckId>(deck),
                    root, turns[root]));
            }
            records.push_back({
                .physical_game = physical_game,
                .block = 0,
                .schedule_index = physical_game,
                .perspective = physical_game % 2,
                .deck = static_cast<DeckId>(deck),
                .root_index = turns.size(),
                .root_turn = 12,
                .terminal_target = 0.5,
                .control_target = 0.5,
                .treatment_target = 0.5,
                .record_offset8_target = 0.5,
                .calendar_turn8_target = 0.5,
            });
        }
    }
    return records;
}

old_school::terminal_weight_eval::ClusteredEstimate estimate(
    double mean, double lower, double upper) {
    return {
        .records = 4000,
        .clusters = 2000,
        .mean = mean,
        .standard_error = (upper - lower) / 3.92,
        .confidence_lower_95 = lower,
        .confidence_upper_95 = upper,
    };
}

void set_target(
    ct8::TargetMetrics& metrics, double bias,
    double brier = 0.20, double log_loss = 0.60) {
    metrics.brier =
        estimate(brier, brier - 0.002, brier + 0.002);
    metrics.soft_log_loss = estimate(
        log_loss, log_loss - 0.002, log_loss + 0.002);
    metrics.signed_bias =
        estimate(bias, bias - 0.002, bias + 0.002);
    metrics.target_mean = 0.5 + bias;
    metrics.target_variance = 0.02;
}

void set_delta(
    ct8::DeltaMetrics& metrics, double bias,
    double upper, double brier = -0.002,
    double loss = -0.002) {
    metrics.signed_bias_delta =
        estimate(bias, bias - 0.002, upper);
    metrics.brier_delta =
        estimate(brier, brier - 0.001, -0.0005);
    metrics.soft_log_loss_delta =
        estimate(loss, loss - 0.001, -0.0005);
}

void make_available(
    ct8::RowSetMetrics& rows, std::size_t records,
    std::size_t actor_games, std::size_t games) {
    rows.available = true;
    rows.records = records;
    rows.actor_games = actor_games;
    rows.physical_games = games;
}

ct8::ScientificReport passing_gate_fixture() {
    ct8::ScientificReport report;
    report.seed = 123;
    report.generation = ct8::kAuditGeneration;
    report.balanced_blocks = ct8::kAuditBalancedBlocks;
    report.physical_games = ct8::kAuditPhysicalGames;
    report.perspectives = ct8::kAuditPerspectives;
    report.schedule_balanced = true;
    report.common_coverage_passed = true;
    report.early_green_control_qualified = true;
    report.trace_invariants_passed = true;
    report.record_offset4_identity_passed = true;
    report.calendar_turn4_distance_passed = true;
    report.calendar_turn4_earliest_passed = true;
    report.record_offset8_identity_passed = true;
    report.calendar_turn8_distance_passed = true;
    report.calendar_turn8_earliest_passed = true;
    report.tail_identity_passed = true;
    report.hidden_repartition_passed = true;
    report.hidden_repartition_states = 1;

    auto initialize_rows =
        [](ct8::RowSetMetrics& rows,
           std::size_t records, std::size_t actors,
           std::size_t games) {
            make_available(rows, records, actors, games);
            for (ct8::WeightingMetrics* weighting : {
                     &rows.record_weighted,
                     &rows.equal_actor_game}) {
                set_target(
                    weighting->arms[arm(
                        ct8::TargetArm::RecordOffset4)],
                    0.01, 0.20);
                set_target(
                    weighting->arms[arm(
                        ct8::TargetArm::RecordOffset8)],
                    0.008, 0.198);
                set_target(
                    weighting->arms[arm(
                        ct8::TargetArm::CalendarTurn4)],
                    0.007, 0.197);
                set_target(
                    weighting->arms[arm(
                        ct8::TargetArm::CalendarTurn8)],
                    0.005, 0.190);
                for (auto& delta : weighting->contrasts) {
                    set_delta(delta, -0.002, -0.0005);
                }
                set_delta(
                    weighting->interaction,
                    -0.003, -0.001);
            }
        };

    initialize_rows(
        report.metrics.pooled.all_records,
        10000, 4000, 2000);
    initialize_rows(
        report.metrics.pooled.four_arm_common,
        6000, 3200, 1700);
    auto& pooled_common =
        report.metrics.pooled.four_arm_common;
    set_delta(
        pooled_common.record_weighted.contrasts[
            contrast(
                ct8::Contrast::
                    CalendarTurn8MinusRecordOffset4)],
        -0.02, -0.015, -0.003, -0.004);
    set_delta(
        pooled_common.record_weighted.contrasts[
            contrast(
                ct8::Contrast::
                    CalendarTurn8MinusCalendarTurn4)],
        -0.01, -0.005, -0.001, -0.001);
    set_delta(
        pooled_common.record_weighted.contrasts[
            contrast(
                ct8::Contrast::
                    CalendarTurn8MinusRecordOffset8)],
        -0.01, -0.005, -0.001, -0.001);

    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& scope = report.metrics.by_deck[deck];
        initialize_rows(
            scope.all_records, 2000, 800, 400);
        initialize_rows(
            scope.four_arm_common, 1200, 640, 340);
    }

    auto& green =
        report.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)];
    auto& early =
        green.four_arm_common_by_root_turn[
            static_cast<std::size_t>(
                ct8::RootTurnStratum::Early)];
    initialize_rows(early, 1200, 520, 300);
    for (ct8::WeightingMetrics* weighting : {
             &early.record_weighted,
             &early.equal_actor_game}) {
        set_target(
            weighting->arms[arm(
                ct8::TargetArm::RecordOffset4)],
            0.040);
        set_target(
            weighting->arms[arm(
                ct8::TargetArm::RecordOffset8)],
            0.032);
        set_target(
            weighting->arms[arm(
                ct8::TargetArm::CalendarTurn4)],
            0.031);
        set_target(
            weighting->arms[arm(
                ct8::TargetArm::CalendarTurn8)],
            0.020);
        set_delta(
            weighting->contrasts[
                contrast(
                    ct8::Contrast::
                        CalendarTurn8MinusRecordOffset4)],
            -0.020, -0.015);
        set_delta(
            weighting->interaction, -0.003, -0.001);
    }
    for (ct8::WeightingMetrics* weighting : {
             &green.four_arm_common.record_weighted,
             &green.four_arm_common.equal_actor_game}) {
        set_target(
            weighting->arms[arm(
                ct8::TargetArm::RecordOffset4)],
            0.030);
        set_target(
            weighting->arms[arm(
                ct8::TargetArm::CalendarTurn8)],
            0.020);
    }

    auto set_direction =
        [&](DeckId deck, double control_bias,
            double treatment_bias, double delta) {
            auto& common =
                report.metrics.by_deck[
                    static_cast<std::size_t>(deck)]
                    .four_arm_common;
            for (ct8::WeightingMetrics* weighting : {
                     &common.record_weighted,
                     &common.equal_actor_game}) {
                set_target(
                    weighting->arms[arm(
                        ct8::TargetArm::RecordOffset4)],
                    control_bias);
                set_target(
                    weighting->arms[arm(
                        ct8::TargetArm::CalendarTurn8)],
                    treatment_bias);
                set_delta(
                    weighting->contrasts[
                        contrast(
                            ct8::Contrast::
                                CalendarTurn8MinusRecordOffset4)],
                    delta,
                    delta > 0.0 ? delta + 0.002
                                : delta + 0.002);
            }
        };
    set_direction(DeckId::Blue, -0.030, -0.020, 0.010);
    set_direction(DeckId::RUAggro, 0.030, 0.020, -0.010);
    return report;
}

void test_fixed_schedule_is_balanced_and_not_reserved() {
    constexpr std::uint64_t test_seed = 0x4354380001ULL;
    const auto first = ta4::audit_schedule(test_seed, 3, 2);
    const auto second = ta4::audit_schedule(test_seed, 3, 2);
    expect(first == second, "CT8 schedule determinism");
    expect(
        first.size() ==
            2 *
                old_school::learned_iteration::
                    kBalancedScheduleGames,
        "CT8 two-block schedule size");
    std::array<std::array<std::array<std::size_t, 2>, 2>,
               old_school::kDeckCount>
        counts{};
    for (const auto& task : first) {
        for (std::size_t seat = 0; seat < 2; ++seat) {
            const std::size_t deck =
                static_cast<std::size_t>(
                    task.scheduled.seat_decks[seat]);
            const std::size_t started =
                task.scheduled.starting_player == seat ? 1 : 0;
            ++counts[deck][seat][started];
        }
    }
    for (const auto& deck : counts) {
        for (const auto& seat : deck) {
            expect(
                seat[0] == 8 && seat[1] == 8,
                "CT8 deck/seat/play-draw cell");
        }
    }
    expect(
        ct8::kAuditSeed == 202607260621ULL &&
            ct8::kAuditGeneration == 19 &&
            ct8::kAuditBalancedBlocks == 50,
        "CT8 canonical coordinates are frozen");
}

void test_four_arm_scoring_and_interaction_are_paired() {
    const auto report =
        ct8::score_four_arm_records(metric_fixture());
    const auto& pooled = report.pooled;
    expect(
        pooled.all_records.records == 40 &&
            pooled.four_arm_common.records == 30,
        "CT8 all/four-common row accounting");
    expect(
        pooled.four_arm_common.actor_games == 10 &&
            pooled.four_arm_common.physical_games == 10,
        "CT8 common actor/game accounting");
    for (const auto& pair : pooled.pair_common) {
        expect(
            pair.records == 30 &&
                pair.actor_games == 10,
            "CT8 pair-common accounting");
    }
    expect(
        pooled.bootstrapped_records ==
                std::array<std::size_t, 4>{
                    30, 30, 30, 30} &&
            pooled.terminal_tail_records ==
                std::array<std::size_t, 4>{
                    10, 10, 10, 10},
        "CT8 four-arm bootstrap/tail accounting");
    expect(
        pooled
                .record_offset4_turn_distance_histogram[2] ==
            30 &&
            pooled
                    .record_offset8_turn_distance_histogram[4] ==
                30,
        "CT8 record-offset turn-distance histograms");

    const auto& record =
        pooled.four_arm_common.record_weighted;
    expect_near(
        record.contrasts[
                   contrast(
                       ct8::Contrast::
                           CalendarTurn8MinusRecordOffset4)]
            .signed_bias_delta.mean,
        -0.18, 1.0e-12, "CT8-RO4 paired bias delta");
    expect_near(
        record.interaction.signed_bias_delta.mean,
        -0.03, 1.0e-12,
        "CT8 factorial bias interaction");
    expect(
        record.interaction.signed_bias_delta.records == 30 &&
            record.interaction.signed_bias_delta.clusters == 10,
        "CT8 interaction CR1 rows/clusters");
    expect_near(
        report.ro4_ct4_reference.pooled.common_records
            .record_weighted.treatment_minus_control
            .signed_bias_delta.mean,
        -0.10, 1.0e-12,
        "CT8 retains established RO4/CT4 scorer");
    for (const auto& stratum :
         pooled.four_arm_common_by_root_turn) {
        expect(
            stratum.available && stratum.records == 10,
            "CT8 four-common root-turn strata");
    }
}

void test_scoring_rejects_invalid_eight_arm_inputs() {
    auto records = metric_fixture();
    records.front().record_offset8_future_index =
        records.front().root_index + 7;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                ct8::score_four_arm_records(records));
        },
        "CT8 rejects non-eight RO8 index");

    records = metric_fixture();
    records.front().calendar_turn8_turn_distance = 7;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                ct8::score_four_arm_records(records));
        },
        "CT8 rejects non-eight calendar distance");

    records = metric_fixture();
    records.back().calendar_turn8_target = 0.6;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(
                ct8::score_four_arm_records(records));
        },
        "CT8 rejects a nonterminal CT8 tail");
}

void test_gate_pass_reject_and_incomplete_taxonomy() {
    const auto report = passing_gate_fixture();
    const auto gate = ct8::evaluate_gate(report);
    expect(
        gate.early_green_primary &&
            gate.early_green_constituent_advantage &&
            gate.early_green_interaction,
        "CT8 early-Green primary and factorial gates");
    expect(
        gate.pooled_loss_vs_control &&
            gate.pooled_loss_vs_constituents &&
            gate.pooled_all_brier_best,
        "CT8 pooled loss gates");
    expect(
        gate.blue_direction_and_shrink &&
            gate.ru_direction_and_shrink,
        "CT8 registered deck directions");
    expect(
        gate.evidence_complete &&
            gate.mechanical_invariants &&
            gate.passed &&
            gate.failures.empty(),
        "CT8 synthetic complete mechanism passes");
    expect(
        ct8::audit_exit_code(true, gate.passed) == 0,
        "CT8 pass exit code");
    ct8::AuditReport complete_audit;
    complete_audit.parent_fingerprint =
        std::string(ct8::kParentFingerprint);
    complete_audit.scientific = report;
    complete_audit.gate = gate;
    complete_audit.repeated_report_bit_identical = true;
    complete_audit.artifact_unchanged = true;
    expect(
        ct8::infrastructure_complete(complete_audit),
        "CT8 complete audit infrastructure");

    auto rejected = report;
    auto& early =
        rejected.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Green)]
            .four_arm_common_by_root_turn[
                static_cast<std::size_t>(
                    ct8::RootTurnStratum::Early)];
    early.record_weighted.contrasts[
              contrast(
                  ct8::Contrast::
                      CalendarTurn8MinusRecordOffset4)]
        .signed_bias_delta.mean = -0.005;
    const auto rejected_gate =
        ct8::evaluate_gate(rejected);
    expect(
        !rejected_gate.early_green_primary &&
            !rejected_gate.passed &&
            rejected_gate.evidence_complete,
        "CT8 complete scientific rejection");
    expect(
        ct8::audit_exit_code(
            true, rejected_gate.passed) == 1,
        "CT8 rejection exit code");

    auto incomplete = report;
    incomplete.early_green_control_qualified = false;
    const auto incomplete_gate =
        ct8::evaluate_gate(incomplete);
    expect(
        !incomplete_gate.evidence_complete &&
            !incomplete_gate.passed,
        "CT8 unqualified control is incomplete");
    expect(
        ct8::audit_exit_code(
            incomplete_gate.evidence_complete,
            incomplete_gate.passed) == 2,
        "CT8 incomplete exit code");
    complete_audit.scientific = incomplete;
    complete_audit.gate = incomplete_gate;
    expect(
        !ct8::infrastructure_complete(complete_audit),
        "CT8 unqualified corpus fails infrastructure");

    auto mechanical = report;
    mechanical.hidden_repartition_passed = false;
    const auto mechanical_gate =
        ct8::evaluate_gate(mechanical);
    expect(
        !mechanical_gate.mechanical_invariants &&
            !mechanical_gate.passed,
        "CT8 hidden-information failure is mechanical");
}

void test_small_collection_covers_new_mechanics_and_hidden_clone() {
    constexpr std::uint64_t model_seed = 0x4354380002ULL;
    constexpr std::uint64_t audit_seed = 0x4354380003ULL;
    auto model =
        old_school::train_learned_value_champion(
            1, model_seed);
    auto tasks = ta4::audit_schedule(audit_seed, 3, 1);
    tasks.resize(2);
    const ta4::Capture serial = ta4::collect(
        tasks, model,
        {
            .max_game_turns = 40,
            .worker_count = 1,
            .verify_hidden_repartition = true,
        });
    const ta4::Capture parallel = ta4::collect(
        tasks, model,
        {
            .max_game_turns = 40,
            .worker_count = 2,
            .verify_hidden_repartition = true,
        });
    expect(
        serial == parallel,
        "CT8 capture is worker-count deterministic");
    expect(
        serial.record_offset8_identity_passed &&
            serial.calendar_turn8_distance_passed &&
            serial.calendar_turn8_earliest_passed &&
            serial.eight_tail_identity_passed &&
            serial.hidden_repartition_passed,
        "CT8 capture mechanics and hidden clone");
    expect(
        serial.record_offset8_target_hash.size() == 64 &&
            serial.calendar_turn8_target_hash.size() == 64,
        "CT8 dedicated target hashes");
    for (const auto& record : serial.records) {
        if (record.record_offset8_future_index.has_value()) {
            expect(
                *record.record_offset8_future_index ==
                    record.root_index +
                        ta4::kRecordOffset8Distance,
                "CT8 exact RO8 landing");
        }
        if (record.calendar_turn8_future_index.has_value()) {
            expect(
                record.calendar_turn8_turn_distance ==
                    std::optional<std::size_t>(
                        ta4::kCalendarTurn8Advances),
                "CT8 exact calendar-eight landing");
        }
        if (!record.record_offset8_future_index.has_value()) {
            expect(
                std::bit_cast<std::uint64_t>(
                    record.record_offset8_target) ==
                    std::bit_cast<std::uint64_t>(
                        record.terminal_target),
                "CT8 RO8 terminal tail identity");
        }
    }
}

void test_missing_artifact_and_source_fail_closed() {
    expect_throws<std::runtime_error>(
        [] {
            static_cast<void>(
                ct8::require_artifact_snapshot(
                    "/tmp/old-school-ct8-artifact-"
                    "must-not-exist.bin"));
        },
        "CT8 missing frozen artifact fails closed");

    std::ifstream input("src/target_factorial_audit.cpp");
    expect(
        static_cast<bool>(input),
        "CT8 evaluator source is readable");
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    expect(
        source.find(
            "load_learned_value_challenger_artifact") !=
            std::string::npos,
        "CT8 canonical route loads the frozen parent");
    expect(
        source.find("require_artifact_snapshot") !=
            std::string::npos,
        "CT8 canonical route snapshots its artifact");
    expect(
        source.find("train_learned") ==
            std::string::npos,
        "CT8 evaluator source does not call a trainer");
}

void test_reports_are_machine_and_human_readable() {
    ct8::AuditReport report;
    report.parent_fingerprint =
        std::string(ct8::kParentFingerprint);
    report.scientific = passing_gate_fixture();
    report.gate = ct8::evaluate_gate(report.scientific);
    report.repeated_report_bit_identical = true;
    report.artifact_unchanged = true;
    report.passed = report.gate.passed;
    std::ostringstream human;
    std::ostringstream tsv;
    ct8::write_human_report(report, human);
    ct8::write_tsv_report(report, tsv);
    const std::string human_text = human.str();
    const std::string tsv_text = tsv.str();
    expect(
        human_text.find(
            "record/calendar x four/eight") !=
                std::string::npos &&
            human_text.find("pair-common CT8-RO4") !=
                std::string::npos &&
            human_text.find(
                "RO4 physical-turn distances:") !=
                std::string::npos &&
            human_text.find(
                "RO8 physical-turn distances:") !=
                std::string::npos,
        "CT8 human report identifies factorial and pairs");
    expect(
        tsv_text.find("CT8_TSV_BEGIN") !=
                std::string::npos &&
            tsv_text.find("ct8-audit-v1") !=
                std::string::npos &&
            tsv_text.find("interaction") !=
                std::string::npos &&
            tsv_text.find(
                "physical_turn_distance_count") !=
                std::string::npos &&
            tsv_text.find("CT8_TSV_END") !=
                std::string::npos,
        "CT8 TSV report has stable framing and interaction");
    const std::string distribution_marker =
        "diagnostic\tPooled\t"
        "all/equal_actor_game/RO4\t"
        "target_distribution";
    const std::size_t distribution =
        tsv_text.find(distribution_marker);
    expect(
        distribution != std::string::npos,
        "CT8 TSV includes equal-actor target distribution");
    const std::size_t line_end =
        tsv_text.find('\n', distribution);
    const std::string distribution_line =
        tsv_text.substr(
            distribution, line_end - distribution);
    expect(
        distribution_line.find("\t4000\t2000\t") !=
            std::string::npos,
        "CT8 equal-actor distribution reports effective "
        "actor-game samples and physical-game clusters");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "fixed schedule is balanced and coordinates frozen",
        test_fixed_schedule_is_balanced_and_not_reserved);
    runner.run(
        "four-arm scoring and interaction are paired",
        test_four_arm_scoring_and_interaction_are_paired);
    runner.run(
        "scoring rejects invalid eight-arm inputs",
        test_scoring_rejects_invalid_eight_arm_inputs);
    runner.run(
        "gate pass reject and incomplete taxonomy",
        test_gate_pass_reject_and_incomplete_taxonomy);
    runner.run(
        "small collection covers mechanics and hidden clone",
        test_small_collection_covers_new_mechanics_and_hidden_clone);
    runner.run(
        "missing artifact and source fail closed",
        test_missing_artifact_and_source_fail_closed);
    runner.run(
        "reports are machine and human readable",
        test_reports_are_machine_and_human_readable);
    return runner.finish();
}
