#include "old_school/turn_alignment_audit.hpp"

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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

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
                  << " turn-alignment audit tests passed\n";
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

void expect_bit_identical(
    double actual, double expected,
    std::string_view message) {
    expect(
        std::bit_cast<std::uint64_t>(actual) ==
            std::bit_cast<std::uint64_t>(expected),
        message);
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

std::vector<ta4::AuditRecord> metric_fixture() {
    std::vector<ta4::AuditRecord> records;
    constexpr std::array<std::size_t, 3> root_turns = {
        3, 4, 8};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t game_in_deck = 0;
             game_in_deck < 2; ++game_in_deck) {
            const std::size_t physical_game =
                3 * deck + game_in_deck;
            const std::size_t perspective =
                game_in_deck % 2;
            for (std::size_t root = 0;
                 root < root_turns.size(); ++root) {
                const bool high_control =
                    game_in_deck == 0;
                records.push_back({
                    .physical_game = physical_game,
                    .block = 0,
                    .schedule_index = physical_game,
                    .perspective = perspective,
                    .deck = static_cast<DeckId>(deck),
                    .root_index = root,
                    .root_turn = root_turns[root],
                    .terminal_target = 0.5,
                    .control_target =
                        high_control ? 0.7 : 0.3,
                    .treatment_target =
                        high_control ? 0.6 : 0.4,
                    .control_future_index = root + 4,
                    .treatment_future_index = root + 7,
                    .control_turn_distance =
                        2 * root,
                    .treatment_turn_distance =
                        ta4::kTurnBootstrapAdvances,
                });
            }
            records.push_back({
                .physical_game = physical_game,
                .block = 0,
                .schedule_index = physical_game,
                .perspective = perspective,
                .deck = static_cast<DeckId>(deck),
                .root_index = root_turns.size(),
                .root_turn = 12,
                .terminal_target = 0.5,
                .control_target = 0.5,
                .treatment_target = 0.5,
            });
        }
        records.push_back({
            .physical_game = 3 * deck + 2,
            .block = 0,
            .schedule_index = 3 * deck + 2,
            .perspective = 0,
            .deck = static_cast<DeckId>(deck),
            .root_index = 0,
            .root_turn = 12,
            .terminal_target = 0.5,
            .control_target = 0.5,
            .treatment_target = 0.5,
        });
    }
    return records;
}

ta4::ScientificReport passing_gate_fixture() {
    std::vector<ta4::AuditRecord> records;
    records.reserve(ta4::kAuditPerspectives);
    for (std::size_t physical_game = 0;
         physical_game < ta4::kAuditPhysicalGames;
         ++physical_game) {
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            const std::size_t actor =
                2 * physical_game + perspective;
            const DeckId deck = static_cast<DeckId>(
                actor % old_school::kDeckCount);
            double control = 0.4;
            double treatment = 0.4;
            if (deck == DeckId::Green) {
                control = 0.60;
                treatment = 0.58;
            } else if (deck == DeckId::Blue) {
                control = 0.20;
                treatment = 0.22;
            }
            records.push_back({
                .physical_game = physical_game,
                .block =
                    physical_game /
                    old_school::learned_iteration::
                        kBalancedScheduleGames,
                .schedule_index =
                    physical_game %
                    old_school::learned_iteration::
                        kBalancedScheduleGames,
                .perspective = perspective,
                .deck = deck,
                .root_index = 0,
                .root_turn = 2,
                .terminal_target = 0.4,
                .control_target = control,
                .treatment_target = treatment,
                .control_future_index = 4,
                .treatment_future_index = 8,
                .control_turn_distance = 2,
                .treatment_turn_distance =
                    ta4::kTurnBootstrapAdvances,
            });
        }
    }

    ta4::ScientificReport report;
    report.seed = 0x54A40001ULL;
    report.generation = 7;
    report.balanced_blocks = ta4::kAuditBalancedBlocks;
    report.physical_games = ta4::kAuditPhysicalGames;
    report.perspectives = ta4::kAuditPerspectives;
    report.metrics = ta4::score_records(records);
    report.schedule_balanced = true;
    report.common_coverage_passed = true;
    report.trace_invariants_passed = true;
    report.control_identity_passed = true;
    report.tail_identity_passed = true;
    report.treatment_distance_passed = true;
    report.treatment_earliest_passed = true;
    report.hidden_repartition_passed = true;
    return report;
}

void test_schedule_is_deterministic_and_exactly_balanced() {
    constexpr std::uint64_t seed = 0x54A40002ULL;
    const auto first = ta4::audit_schedule(seed, 7, 2);
    const auto second = ta4::audit_schedule(seed, 7, 2);
    expect(first == second, "TA4 schedule determinism");
    expect(
        first.size() ==
            2 *
                old_school::learned_iteration::
                    kBalancedScheduleGames,
        "TA4 two-block schedule size");

    std::array<std::array<std::array<std::size_t, 2>, 2>,
               old_school::kDeckCount>
        quadrants{};
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        const auto& task = first[index];
        expect(
            task.physical_game == index,
            "TA4 physical game IDs are global and contiguous");
        expect(
            task.block ==
                index /
                    old_school::learned_iteration::
                        kBalancedScheduleGames,
            "TA4 task block coordinate");
        expect(
            task.scheduled.schedule_index ==
                index %
                    old_school::learned_iteration::
                        kBalancedScheduleGames,
            "TA4 schedule index is block-local");
        for (std::size_t seat = 0; seat < 2; ++seat) {
            const std::size_t deck =
                static_cast<std::size_t>(
                    task.scheduled.seat_decks[seat]);
            const std::size_t started =
                task.scheduled.starting_player == seat ? 0 : 1;
            ++quadrants[deck][seat][started];
        }
    }
    for (const auto& deck : quadrants) {
        for (const auto& seat : deck) {
            expect(
                seat[0] == 8 && seat[1] == 8,
                "TA4 deck/seat/play-draw quadrants");
        }
    }
    expect(
        first != ta4::audit_schedule(seed + 1, 7, 2),
        "TA4 schedule seed must affect stochastic fields");
    expect_throws<std::invalid_argument>(
        [] {
            static_cast<void>(
                ta4::audit_schedule(7, 3, 0));
        },
        "TA4 schedule must reject zero blocks");
}

void test_calendar_alignment_counts_extra_turns_and_priority_roots() {
    old_school::GameState state;
    state.turn_number = 7;
    state.active_player = 0;
    state.players[0].hand = {
        old_school::CardId::TimeWalk,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    expect(
        old_school::apply_priority_action(
            state, 0,
            old_school::PriorityAction::cast_sorcery(
                old_school::CardId::TimeWalk),
            true),
        "TA4 Time Walk fixture cast");
    expect(
        old_school::resolve_top_of_stack(state),
        "TA4 Time Walk fixture resolution");

    std::vector<std::size_t> turns = {
        state.turn_number,
        state.turn_number,
    };
    std::vector<std::size_t> active_players = {
        state.active_player,
        state.active_player,
    };
    for (std::size_t advance = 0; advance < 4; ++advance) {
        ++state.turn_number;
        active_players.push_back(
            old_school::advance_turn_player(state));
        turns.push_back(state.turn_number);
    }
    expect(
        active_players ==
            std::vector<std::size_t>({0, 0, 0, 1, 0, 1}),
        "TA4 Time Walk remains one numbered physical turn");

    const auto indices =
        old_school::learned_iteration::
            turn_aligned_bootstrap_indices(
                turns, ta4::kTurnBootstrapAdvances);
    expect(
        indices[0] == std::optional<std::size_t>(5) &&
            indices[1] == std::optional<std::size_t>(5),
        "TA4 same-turn priority roots share the earliest "
        "four-turn future");
    for (std::size_t root = 2;
         root < indices.size(); ++root) {
        expect(
            !indices[root].has_value(),
            "TA4 roots without four future turns remain tails");
    }

    const std::vector<double> parent_values = {
        0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
    const auto targets =
        old_school::learned_iteration::
            weighted_turn_aligned_bootstrap_targets(
                parent_values, turns, 0.8,
                ta4::kTurnBootstrapAdvances,
                ta4::kTerminalWeight);
    expect(
        targets ==
            std::vector<double>({
                0.7, 0.7, 0.8, 0.8, 0.8, 0.8}),
        "TA4 aligned roots bootstrap and terminal tails "
        "remain exact");
}

void test_eight_horizon_targets_use_exact_futures_and_tails() {
    const std::vector<std::size_t> turns = {
        1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 9, 10};
    const auto indices =
        old_school::learned_iteration::
            turn_aligned_bootstrap_indices(
                turns, ta4::kCalendarTurn8Advances);
    expect(
        indices[0] == std::optional<std::size_t>(10) &&
            indices[1] == std::optional<std::size_t>(10),
        "CT8 same-turn priority roots share the earliest "
        "eight-turn future");
    expect(
        indices[2] == std::optional<std::size_t>(12) &&
            indices[3] == std::optional<std::size_t>(12),
        "CT8 ordinary roots land exactly eight turns ahead");
    for (std::size_t root = 4; root < indices.size();
         ++root) {
        expect(
            !indices[root].has_value(),
            "CT8 roots without eight future turns are tails");
    }

    std::vector<double> parent_values;
    parent_values.reserve(turns.size());
    for (std::size_t index = 0; index < turns.size();
         ++index) {
        parent_values.push_back(
            0.02 * static_cast<double>(index + 1));
    }
    constexpr double terminal = 0.8;
    const auto calendar_targets =
        old_school::learned_iteration::
            weighted_turn_aligned_bootstrap_targets(
                parent_values, turns, terminal,
                ta4::kCalendarTurn8Advances,
                ta4::kTerminalWeight);
    expect_near(
        calendar_targets[0],
        0.5 * terminal + 0.5 * parent_values[10],
        1.0e-15, "CT8 aligned target uses its exact future");
    for (std::size_t root = 4;
         root < calendar_targets.size(); ++root) {
        expect_bit_identical(
            calendar_targets[root], terminal,
            "CT8 aligned terminal tails retain z exactly");
    }

    const auto record_targets =
        old_school::learned_iteration::
            n_state_bootstrap_targets(
                parent_values, terminal,
                ta4::kRecordOffset8Distance);
    expect_near(
        record_targets[0],
        0.5 * terminal + 0.5 * parent_values[8],
        1.0e-15,
        "RO8 target uses exactly eight trace records");
    for (std::size_t root = parent_values.size() -
             ta4::kRecordOffset8Distance;
         root < record_targets.size(); ++root) {
        expect_bit_identical(
            record_targets[root], terminal,
            "RO8 terminal tails retain z exactly");
    }
}

void test_eight_turn_alignment_counts_time_walk_turns() {
    old_school::GameState state;
    state.turn_number = 7;
    state.active_player = 0;
    state.players[0].hand = {
        old_school::CardId::TimeWalk,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    expect(
        old_school::apply_priority_action(
            state, 0,
            old_school::PriorityAction::cast_sorcery(
                old_school::CardId::TimeWalk),
            true),
        "CT8 Time Walk fixture cast");
    expect(
        old_school::resolve_top_of_stack(state),
        "CT8 Time Walk fixture resolution");

    std::vector<std::size_t> turns = {
        state.turn_number,
        state.turn_number,
    };
    std::vector<std::size_t> active_players = {
        state.active_player,
        state.active_player,
    };
    for (std::size_t advance = 0; advance < 8; ++advance) {
        ++state.turn_number;
        active_players.push_back(
            old_school::advance_turn_player(state));
        turns.push_back(state.turn_number);
    }
    expect(
        active_players ==
            std::vector<std::size_t>(
                {0, 0, 0, 1, 0, 1, 0, 1, 0, 1}),
        "CT8 Time Walk repeats the active player while calendar "
        "turn numbers advance");
    const auto indices =
        old_school::learned_iteration::
            turn_aligned_bootstrap_indices(
                turns, ta4::kCalendarTurn8Advances);
    expect(
        indices[0] == std::optional<std::size_t>(9) &&
            indices[1] == std::optional<std::size_t>(9),
        "CT8 Time Walk priority roots land at turn plus eight");
    for (std::size_t root = 2; root < indices.size();
         ++root) {
        expect(
            !indices[root].has_value(),
            "CT8 Time Walk late roots remain terminal tails");
    }
}

void test_scoring_tracks_paired_targets_and_tail_accounting() {
    const auto records = metric_fixture();
    const auto report = ta4::score_records(records);
    expect(
        report.pooled.all_records.records == 45,
        "TA4 pooled all-record count");
    expect(
        report.pooled.common_records.records == 30,
        "TA4 pooled common-record count");
    expect(
        report.pooled.all_records.actor_games == 15 &&
            report.pooled.all_records.physical_games == 15,
        "TA4 pooled actor/game counts");
    expect(
        report.pooled.control_bootstrapped_records == 30 &&
            report.pooled.control_terminal_tail_records == 15,
        "TA4 control bootstrap/tail accounting");
    expect(
        report.pooled.treatment_bootstrapped_records == 30 &&
            report.pooled.treatment_terminal_tail_records == 15,
        "TA4 treatment bootstrap/tail accounting");
    expect(
        report.pooled.control_turn_distance_histogram[0] == 10 &&
            report.pooled.control_turn_distance_histogram[2] == 10 &&
            report.pooled.control_turn_distance_histogram[4] == 10,
        "TA4 record-offset physical-turn histogram");
    for (const auto& stratum :
         report.pooled.common_by_root_turn) {
        expect(
            stratum.available && stratum.records == 10,
            "TA4 root-turn stratum accounting");
    }

    const auto& paired =
        report.pooled.common_records.record_weighted
            .treatment_minus_control;
    expect_near(
        paired.brier_delta.mean, -0.03, 1.0e-12,
        "TA4 paired Brier delta");
    expect_near(
        paired.signed_bias_delta.mean, 0.0, 1.0e-12,
        "TA4 paired signed-bias delta");
    expect(
        paired.brier_delta.records == 30 &&
            paired.brier_delta.clusters == 10,
        "TA4 paired CR1 record/cluster counts");
    expect(
        report.pooled.common_records.actor_games == 10 &&
            report.pooled.common_records.excluded_actor_games == 5,
        "TA4 zero-common actor-games are excluded and reported");
    expect_near(
        report.pooled.common_records.record_weighted
            .treatment.target_mean,
        0.5, 1.0e-12, "TA4 treatment target mean");
    expect(
        report.pooled.common_records.equal_actor_game
                .treatment_minus_control.brier_delta.mean <
            0.0,
        "TA4 equal-actor-game paired direction");
    for (const auto& deck : report.by_deck) {
        expect(
            deck.all_records.records == 9 &&
                deck.common_records.records == 6,
            "TA4 per-deck record counts");
        expect(
            deck.common_records.physical_games == 2 &&
                deck.common_records.actor_games == 2 &&
                deck.common_records.excluded_actor_games == 1,
            "TA4 per-deck common-row coverage accounting");
    }
}

void test_scoring_rejects_invalid_pairing_inputs() {
    auto records = metric_fixture();
    records.front().control_future_index = std::nullopt;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(ta4::score_records(records));
        },
        "TA4 treatment future without control");

    records = metric_fixture();
    records.front().control_turn_distance =
        ta4::kRecordBootstrapDistance + 1;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(ta4::score_records(records));
        },
        "TA4 record-offset distance above four");

    records = metric_fixture();
    records.push_back(records.front());
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(ta4::score_records(records));
        },
        "TA4 duplicate row identity");

    records = metric_fixture();
    records.front().treatment_target = 1.1;
    expect_throws<std::invalid_argument>(
        [&] {
            static_cast<void>(ta4::score_records(records));
        },
        "TA4 invalid target probability");
}

void test_gate_accepts_only_complete_registered_mechanism() {
    const auto report = passing_gate_fixture();
    const auto gate = ta4::evaluate_gate(report);
    expect(gate.green_bias_moved, "TA4 Green MDE gate");
    expect(gate.green_bias_precise, "TA4 Green precision gate");
    expect(
        gate.green_equal_actor_direction,
        "TA4 Green equal-actor direction");
    expect(
        gate.green_absolute_bias_shrank,
        "TA4 Green absolute bias shrink");
    expect(
        gate.pooled_common_brier_noninferior &&
            gate.pooled_all_brier_nonpositive,
        "TA4 pooled Brier guards");
    expect(
        gate.blue_direction_and_shrink &&
            gate.additional_deck_moved,
        "TA4 Blue corroboration");
    expect(
        gate.per_deck_all_brier_guard &&
            gate.red_white_no_new_material_bias,
        "TA4 per-deck guards");
    expect(
        gate.evidence_complete &&
            gate.mechanical_invariants,
        "TA4 evidence and mechanics");
    expect(
        gate.passed && gate.failures.empty(),
        "TA4 complete synthetic mechanism passes");

    auto mechanically_invalid = report;
    mechanically_invalid.hidden_repartition_passed = false;
    const auto mechanical_gate =
        ta4::evaluate_gate(mechanically_invalid);
    expect(
        !mechanical_gate.mechanical_invariants &&
            !mechanical_gate.passed,
        "TA4 must fail a mechanical invariant");
    expect(
        std::find(
            mechanical_gate.failures.begin(),
            mechanical_gate.failures.end(),
            "TA4 mechanical invariants did not all pass") !=
            mechanical_gate.failures.end(),
        "TA4 mechanical failure is reported");

    auto new_red_bias = report;
    auto& red =
        new_red_bias.metrics.by_deck[
            static_cast<std::size_t>(DeckId::Red)]
            .all_records.record_weighted;
    red.treatment.signed_bias.mean = 0.10;
    red.treatment.signed_bias.confidence_lower_95 = 0.08;
    red.treatment.signed_bias.confidence_upper_95 = 0.12;
    const auto red_gate = ta4::evaluate_gate(new_red_bias);
    expect(
        !red_gate.red_white_no_new_material_bias &&
            !red_gate.passed,
        "TA4 must reject a new material Red bias");
}

void test_canonical_source_is_load_only_and_snapshotted() {
    std::ifstream input("src/turn_alignment_audit.cpp");
    expect(
        static_cast<bool>(input),
        "TA4 source must be readable");
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    expect(
        source.find(
            "load_learned_value_challenger_artifact") !=
            std::string::npos,
        "TA4 canonical route loads the frozen parent");
    expect(
        source.find("snapshot_artifact") != std::string::npos,
        "TA4 canonical route snapshots its artifact");
    expect(
        source.find("train_learned") == std::string::npos,
        "TA4 source must not link or call a trainer");
}

void test_cli_exit_taxonomy_is_exact() {
    expect(
        ta4::audit_exit_code(true, true) == 0,
        "TA4 valid scientific pass exits zero");
    expect(
        ta4::audit_exit_code(true, false) == 1,
        "TA4 valid scientific rejection exits one");
    expect(
        ta4::audit_exit_code(false, false) == 2 &&
            ta4::audit_exit_code(false, true) == 2,
        "TA4 incomplete evidence always exits two");
}

void test_small_noncanonical_collection_is_reproducible() {
    constexpr std::uint64_t model_seed = 0x54A40003ULL;
    constexpr std::uint64_t audit_seed = 0x54A40004ULL;
    auto model =
        old_school::train_learned_value_champion(
            1, model_seed);
    auto tasks = ta4::audit_schedule(audit_seed, 7, 1);
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
        "TA4 collection is worker-count deterministic");
    expect(
        serial.physical_games == 2 &&
            serial.perspectives == 4,
        "TA4 small collection accounting");
    expect(
        !serial.records.empty(),
        "TA4 small collection emits trace records");
    expect(
        serial.trace_invariants_passed &&
            serial.control_identity_passed &&
            serial.tail_identity_passed &&
            serial.treatment_distance_passed &&
            serial.treatment_earliest_passed &&
            serial.hidden_repartition_passed &&
            serial.record_offset8_identity_passed &&
            serial.calendar_turn8_distance_passed &&
            serial.calendar_turn8_earliest_passed &&
            serial.eight_tail_identity_passed,
        "TA4/CT8 small collection invariants");
    expect(
        !serial.schedule_hash.empty() &&
            !serial.trace_hash.empty() &&
            !serial.control_target_hash.empty() &&
            !serial.treatment_target_hash.empty() &&
            !serial.record_offset8_target_hash.empty() &&
            !serial.calendar_turn8_target_hash.empty(),
        "TA4/CT8 small collection hashes");
    expect(
        serial.hidden_repartition_states > 0,
        "CT8 collection exercised a changed hidden clone");
    for (const auto& record : serial.records) {
        if (record.treatment_future_index.has_value()) {
            expect(
                record.control_future_index.has_value(),
                "TA4 common row has both futures");
            expect(
                record.treatment_turn_distance ==
                    std::optional<std::size_t>(
                        ta4::kTurnBootstrapAdvances),
                "TA4 aligned future is exactly four turns");
        }
        expect(
            record.record_offset8_future_index.has_value() ==
                record.record_offset8_turn_distance.has_value(),
            "RO8 future and turn-distance presence match");
        expect(
            record.calendar_turn8_future_index.has_value() ==
                record.calendar_turn8_turn_distance.has_value(),
            "CT8 future and turn-distance presence match");
        if (record.record_offset8_future_index.has_value()) {
            expect(
                *record.record_offset8_future_index ==
                    record.root_index +
                        ta4::kRecordOffset8Distance,
                "RO8 future is exactly eight records ahead");
            expect(
                *record.record_offset8_turn_distance <=
                    ta4::kRecordOffset8Distance,
                "RO8 future advances at most eight turns");
        } else {
            expect_bit_identical(
                record.record_offset8_target,
                record.terminal_target,
                "RO8 collected tail is bit-identical to z");
        }
        if (record.calendar_turn8_future_index.has_value()) {
            expect(
                record.record_offset8_future_index.has_value() &&
                    record.treatment_future_index.has_value(),
                "CT8 common row has all shorter futures");
            expect(
                record.calendar_turn8_turn_distance ==
                    std::optional<std::size_t>(
                        ta4::kCalendarTurn8Advances),
                "CT8 aligned future is exactly eight turns");
            const auto future = std::find_if(
                serial.records.begin(), serial.records.end(),
                [&](const ta4::AuditRecord& candidate) {
                    return candidate.physical_game ==
                               record.physical_game &&
                           candidate.perspective ==
                               record.perspective &&
                           candidate.root_index ==
                               *record
                                    .calendar_turn8_future_index;
                });
            expect(
                future != serial.records.end() &&
                    future->root_turn ==
                        record.root_turn +
                            ta4::kCalendarTurn8Advances,
                "CT8 collected future lands on exact turn");
            const bool has_earlier_landing = std::any_of(
                serial.records.begin(), serial.records.end(),
                [&](const ta4::AuditRecord& candidate) {
                    return candidate.physical_game ==
                               record.physical_game &&
                           candidate.perspective ==
                               record.perspective &&
                           candidate.root_index >
                               record.root_index &&
                           candidate.root_index <
                               *record
                                    .calendar_turn8_future_index &&
                           candidate.root_turn >=
                               future->root_turn;
                });
            expect(
                !has_earlier_landing,
                "CT8 collected future is the earliest exact "
                "turn landing");
        } else {
            expect_bit_identical(
                record.calendar_turn8_target,
                record.terminal_target,
                "CT8 collected tail is bit-identical to z");
        }
    }
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "schedule is deterministic and exactly balanced",
        test_schedule_is_deterministic_and_exactly_balanced);
    runner.run(
        "calendar alignment counts extra turns and priority roots",
        test_calendar_alignment_counts_extra_turns_and_priority_roots);
    runner.run(
        "eight-horizon targets use exact futures and tails",
        test_eight_horizon_targets_use_exact_futures_and_tails);
    runner.run(
        "eight-turn alignment counts Time Walk turns",
        test_eight_turn_alignment_counts_time_walk_turns);
    runner.run(
        "scoring tracks paired targets and tail accounting",
        test_scoring_tracks_paired_targets_and_tail_accounting);
    runner.run(
        "scoring rejects invalid pairing inputs",
        test_scoring_rejects_invalid_pairing_inputs);
    runner.run(
        "gate accepts only complete registered mechanism",
        test_gate_accepts_only_complete_registered_mechanism);
    runner.run(
        "canonical source is load only and snapshotted",
        test_canonical_source_is_load_only_and_snapshotted);
    runner.run(
        "CLI exit taxonomy is exact",
        test_cli_exit_taxonomy_is_exact);
    runner.run(
        "small noncanonical collection is reproducible",
        test_small_noncanonical_collection_is_reproducible);
    return runner.finish();
}
