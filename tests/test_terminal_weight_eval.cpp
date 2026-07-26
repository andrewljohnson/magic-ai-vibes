#include "old_school/terminal_weight_eval.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace tw = old_school::terminal_weight_eval;
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
                  << " terminal-weight evaluator tests passed\n";
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

std::vector<tw::HoldoutRecord> passing_holdout_records() {
    std::vector<tw::HoldoutRecord> records;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t game = 0; game < 2; ++game) {
            records.push_back({
                .physical_game = 2 * deck + game,
                .deck = static_cast<DeckId>(deck),
                .discounted_terminal_target = 0.4,
                .predictions = {0.7, 0.6, 0.45},
                .trace_turn_distance =
                    game == 0
                        ? std::optional<std::size_t>(2 + deck)
                        : std::nullopt,
            });
        }
    }
    return records;
}

void test_cr1_uses_record_weighted_game_residuals() {
    const std::vector<tw::ClusteredValue> values = {
        {0, 1.0},
        {0, 3.0},
        {1, 5.0},
        {1, 7.0},
    };
    const auto estimate =
        tw::cr1_clustered_estimate(values);
    expect(estimate.records == 4, "CR1 record count");
    expect(estimate.clusters == 2, "CR1 cluster count");
    expect_near(estimate.mean, 4.0, 1.0e-12, "CR1 mean");
    // U0=-4, U1=+4, so 2/(2-1)*(16+16)/4^2 = 4.
    expect_near(
        estimate.standard_error, 2.0, 1.0e-12,
        "CR1 standard error");
    expect_near(
        estimate.confidence_upper_95,
        4.0 + tw::kNormal95CriticalValue * 2.0,
        1.0e-12, "CR1 upper interval");

    expect_throws<std::invalid_argument>(
        [] {
            const std::vector<tw::ClusteredValue> one_cluster = {
                {0, 1.0}, {0, 2.0}};
            static_cast<void>(
                tw::cr1_clustered_estimate(one_cluster));
        },
        "CR1 must reject one cluster");
}

void test_holdout_schedule_is_exactly_balanced() {
    constexpr std::uint64_t seed = 918273645ULL;
    const auto first = tw::holdout_schedule(seed, 9, 2);
    const auto second = tw::holdout_schedule(seed, 9, 2);
    expect(first == second, "holdout schedule determinism");
    expect(
        first.size() ==
            2 *
                old_school::learned_iteration::
                    kBalancedScheduleGames,
        "holdout schedule game count");

    std::array<std::array<std::array<std::size_t, 2>, 2>,
               old_school::kDeckCount>
        quadrants{};
    for (std::size_t index = 0; index < first.size(); ++index) {
        expect(
            first[index].physical_game == index,
            "holdout physical game index");
        for (std::size_t seat = 0; seat < 2; ++seat) {
            const std::size_t deck =
                static_cast<std::size_t>(
                    first[index]
                        .scheduled.seat_decks[seat]);
            const std::size_t play =
                first[index].scheduled.starting_player ==
                        seat
                    ? 0
                    : 1;
            ++quadrants[deck][seat][play];
        }
    }
    for (const auto& deck : quadrants) {
        for (const auto& seat : deck) {
            expect(
                seat[0] == 8 && seat[1] == 8,
                "holdout deck/seat/play-draw balance");
        }
    }
}

void test_holdout_metrics_and_gate_follow_preregistration() {
    const auto records = passing_holdout_records();
    const auto report =
        tw::score_holdout_records(records);
    expect(report.pooled.records == 10, "pooled record count");
    expect(
        report.pooled.physical_games == 10,
        "pooled physical games");
    expect(
        report.pooled.perspectives == 10,
        "pooled perspectives");
    expect(
        report.pooled.bootstrapped_records == 5 &&
            report.pooled.terminal_tail_records == 5,
        "trace bootstrap/tail accounting");
    expect_near(
        report.pooled.target_variance, 0.0, 1.0e-12,
        "target variance");
    expect(
        report.pooled.models[2].brier.mean <
            report.pooled.models[1].brier.mean &&
            report.pooled.models[1].brier.mean <
                report.pooled.models[0].brier.mean,
        "Brier ordering");
    expect(
        report.pooled.treatment_comparisons[0]
                .brier_delta.confidence_upper_95 <
            0.0,
        "paired Brier CR1 gate");

    const auto gate = tw::evaluate_offline_gate(
        report,
        {
            .expected_physical_games = 10,
            .expected_perspectives_per_deck = 2,
        });
    expect(gate.accounting_exact, "offline accounting");
    expect(gate.pooled_losses_improved, "pooled loss gate");
    expect(gate.per_deck_loss_guard, "deck loss guard");
    expect(gate.green_bias_shrank, "Green bias shrink");
    expect(gate.blue_bias_shrank, "Blue bias shrink");
    expect(
        gate.no_new_material_bias,
        "new material bias guard");
    expect(gate.passed, "offline gate should pass");

    auto rejected = report;
    rejected.pooled.treatment_comparisons[0]
        .soft_log_loss_delta.confidence_upper_95 = 0.0;
    const auto rejected_gate = tw::evaluate_offline_gate(
        rejected,
        {
            .expected_physical_games = 10,
            .expected_perspectives_per_deck = 2,
        });
    expect(
        !rejected_gate.passed &&
            !rejected_gate.pooled_losses_improved,
        "zero upper bound must reject");
}

void test_offline_gate_boundaries_and_bias_inheritance() {
    const auto base = tw::score_holdout_records(
        passing_holdout_records());

    auto equal_green = base;
    equal_green.by_deck[
        static_cast<std::size_t>(DeckId::Green)]
        .models[2]
        .signed_bias.mean =
        equal_green.by_deck[
            static_cast<std::size_t>(DeckId::Green)]
            .models[1]
            .signed_bias.mean;
    const auto equal_green_gate =
        tw::evaluate_offline_gate(
            equal_green,
            {
                .expected_physical_games = 10,
                .expected_perspectives_per_deck = 2,
            });
    expect(
        !equal_green_gate.green_bias_shrank &&
            !equal_green_gate.passed,
        "Green equal absolute bias must reject strict shrink");

    auto equal_blue = base;
    equal_blue.by_deck[
        static_cast<std::size_t>(DeckId::Blue)]
        .models[2]
        .signed_bias.mean =
        equal_blue.by_deck[
            static_cast<std::size_t>(DeckId::Blue)]
            .models[1]
            .signed_bias.mean;
    const auto equal_blue_gate =
        tw::evaluate_offline_gate(
            equal_blue,
            {
                .expected_physical_games = 10,
                .expected_perspectives_per_deck = 2,
            });
    expect(
        !equal_blue_gate.blue_bias_shrank &&
            !equal_blue_gate.passed,
        "Blue equal absolute bias must reject strict shrink");

    auto boundary = base;
    for (auto& deck : boundary.by_deck) {
        for (auto& comparison :
             deck.treatment_comparisons) {
            comparison.brier_delta.mean = 0.01;
            comparison.soft_log_loss_delta.mean = 0.01;
        }
    }
    expect(
        tw::evaluate_offline_gate(
            boundary,
            {
                .expected_physical_games = 10,
                .expected_perspectives_per_deck = 2,
            })
            .per_deck_loss_guard,
        "exact +0.01 deck loss boundary must pass");
    boundary.by_deck[0]
        .treatment_comparisons[0]
        .brier_delta.mean =
        std::nextafter(
            0.01, std::numeric_limits<double>::infinity());
    expect(
        !tw::evaluate_offline_gate(
             boundary,
             {
                 .expected_physical_games = 10,
                 .expected_perspectives_per_deck = 2,
             })
             .per_deck_loss_guard,
        "deck loss above +0.01 must reject");

    auto material = base;
    auto& red_models =
        material.by_deck[
            static_cast<std::size_t>(DeckId::Red)]
            .models;
    for (std::size_t control = 0; control < 2; ++control) {
        red_models[control].signed_bias.mean = 0.0;
        red_models[control]
            .signed_bias.confidence_lower_95 = -0.01;
        red_models[control]
            .signed_bias.confidence_upper_95 = 0.01;
    }
    red_models[2].signed_bias.mean = 0.06;
    red_models[2].signed_bias.confidence_lower_95 = 0.05;
    red_models[2].signed_bias.confidence_upper_95 = 0.07;
    expect(
        !tw::evaluate_offline_gate(
             material,
             {
                 .expected_physical_games = 10,
                 .expected_perspectives_per_deck = 2,
             })
             .no_new_material_bias,
        "new material bias without a control must reject");
    red_models[1].signed_bias.mean = 0.08;
    red_models[1].signed_bias.confidence_lower_95 = 0.07;
    red_models[1].signed_bias.confidence_upper_95 = 0.09;
    expect(
        tw::evaluate_offline_gate(
            material,
            {
                .expected_physical_games = 10,
                .expected_perspectives_per_deck = 2,
            })
            .no_new_material_bias,
        "inherited same-sign material bias must not be newly rejected");
}

void test_gameplay_schedule_forms_common_seed_quartets() {
    constexpr std::uint64_t seed = 123456789ULL;
    const auto first =
        tw::same_deck_gameplay_schedule(seed, 3);
    const auto second =
        tw::same_deck_gameplay_schedule(seed, 3);
    expect(first == second, "gameplay schedule determinism");
    expect(
        first.size() == old_school::kDeckCount * 3 * 4,
        "gameplay schedule count");

    std::array<std::array<std::array<std::size_t, 2>, 2>,
               old_school::kDeckCount>
        quadrants{};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t quartet = 0; quartet < 3;
             ++quartet) {
            const std::size_t base = (deck * 3 + quartet) * 4;
            const std::uint64_t common_seed =
                first[base].seed;
            for (std::size_t offset = 0; offset < 4;
                 ++offset) {
                const auto& task = first[base + offset];
                expect(
                    task.seed == common_seed,
                    "quartet must share one game seed");
                expect(
                    static_cast<std::size_t>(task.deck) ==
                            deck &&
                        task.quartet == quartet,
                    "quartet identity");
                ++quadrants[deck][task.challenger_player]
                             [task.starting_player];
            }
        }
    }
    for (const auto& deck : quadrants) {
        for (const auto& seat : deck) {
            expect(
                seat[0] == 3 && seat[1] == 3,
                "gameplay seat/start quadrant");
        }
    }
}

void test_gameplay_gate_and_panel_suppression() {
    std::vector<tw::GameplayOutcome> outcomes;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t quartet = 0; quartet < 3;
             ++quartet) {
            for (std::size_t game = 0; game < 4; ++game) {
                outcomes.push_back({
                    .deck = static_cast<DeckId>(deck),
                    .quartet = quartet,
                    .challenger_won = game < 3,
                    .baseline_won = game == 3,
                });
            }
        }
    }
    const auto panel =
        tw::score_gameplay_outcomes(outcomes);
    expect(
        panel.games == 60 && panel.wins == 45 &&
            panel.losses == 15 && panel.draws == 0,
        "gameplay pooled accounting");
    for (const auto& deck : panel.by_deck) {
        expect(
            deck.games == 12 && deck.wins == 9 &&
                deck.losses == 3,
            "gameplay deck accounting");
        expect_near(
            deck.score.mean, 0.75, 1.0e-12,
            "quartet-clustered score");
    }
    const auto gate = tw::evaluate_gameplay_gate(
        panel,
        {
            .expected_total_games = 60,
            .expected_games_per_deck = 12,
            .expected_quartets_per_deck = 3,
            .minimum_aggregate_wins = 31,
        });
    expect(gate.passed, "gameplay gate should pass");

    expect(
        tw::evaluation_stage_decision(false, std::nullopt) ==
            tw::StageDecision{false, false},
        "offline rejection suppresses both panels");
    expect(
        tw::evaluation_stage_decision(true, std::nullopt) ==
            tw::StageDecision{true, false},
        "offline pass permits only panel one initially");
    expect(
        tw::evaluation_stage_decision(true, false) ==
            tw::StageDecision{true, false},
        "panel-one rejection suppresses panel two");
    expect(
        tw::evaluation_stage_decision(true, true) ==
            tw::StageDecision{true, true},
        "panel-one pass permits panel two");
}

void test_gameplay_raw_win_draw_and_cluster_boundaries() {
    constexpr std::array<std::size_t, old_school::kDeckCount>
        wins_by_deck = {0, 126, 125, 125, 125};
    std::vector<tw::GameplayOutcome> outcomes;
    outcomes.reserve(tw::kGameplayTotalGames);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t game = 0;
             game < tw::kGameplayGamesPerDeck; ++game) {
            const bool win = game < wins_by_deck[deck];
            const bool draw = deck == 0;
            outcomes.push_back({
                .deck = static_cast<DeckId>(deck),
                .quartet = game / 4,
                .challenger_won = win,
                .baseline_won = !win && !draw,
            });
        }
    }
    const auto panel =
        tw::score_gameplay_outcomes(outcomes);
    const auto gate = tw::evaluate_gameplay_gate(panel);
    expect(
        panel.wins == 501 && panel.draws == 200 &&
            panel.by_deck[0].wins == 0 &&
            panel.by_deck[0].losses == 0 &&
            panel.by_deck[0].draws == 200,
        "raw-501/draw fixture accounting");
    expect(
        gate.passed && gate.every_deck_non_losing,
        "501 raw wins and a tied all-draw deck must pass");

    auto missing_cluster = panel;
    --missing_cluster.score.clusters;
    expect(
        !tw::evaluate_gameplay_gate(missing_cluster)
             .accounting_exact,
        "missing quartet cluster must fail accounting");

    auto one_fewer_win = outcomes;
    const auto found = std::find_if(
        one_fewer_win.begin(), one_fewer_win.end(),
        [](const tw::GameplayOutcome& outcome) {
            return outcome.challenger_won;
        });
    expect(
        found != one_fewer_win.end(),
        "raw-win fixture must contain a win");
    found->challenger_won = false;
    const auto five_hundred_panel =
        tw::score_gameplay_outcomes(one_fewer_win);
    const auto five_hundred_gate =
        tw::evaluate_gameplay_gate(five_hundred_panel);
    expect(
        five_hundred_panel.wins == 500 &&
            five_hundred_gate.every_deck_non_losing &&
            !five_hundred_gate.aggregate_wins_passed &&
            !five_hundred_gate.passed,
        "500 raw wins must reject despite non-losing decks");
}

void test_artifact_snapshot_binds_content_size_and_time() {
    const auto path =
        std::filesystem::temp_directory_path() /
        "old-school-terminal-weight-eval-snapshot-test.bin";
    {
        std::ofstream output(path, std::ios::binary);
        output << "alpha";
    }
    const auto first =
        tw::snapshot_artifact(path.string());
    const auto second =
        tw::snapshot_artifact(path.string());
    expect(first == second, "unchanged snapshot equality");
    expect(
        first.size == 5 &&
            first.content_hash.size() == 64,
        "snapshot size and digest");
    {
        std::ofstream output(
            path, std::ios::binary | std::ios::app);
        output << "beta";
    }
    const auto changed =
        tw::snapshot_artifact(path.string());
    expect(
        changed != first &&
            changed.content_hash != first.content_hash &&
            changed.size != first.size,
        "snapshot must detect mutation");
    std::error_code error;
    std::filesystem::remove(path, error);
    expect(!error, "snapshot fixture cleanup");

    expect_throws<std::runtime_error>(
        [&] {
            static_cast<void>(
                tw::snapshot_artifact(path.string()));
        },
        "missing artifact must fail closed");
}

std::size_t tab_fields(std::string_view line) {
    return 1 + static_cast<std::size_t>(
                   std::count(
                       line.begin(), line.end(), '\t'));
}

void test_reports_have_strict_tsv_and_suppression_rows() {
    tw::SealedEvaluationReport report;
    report.artifacts_before[0] = {
        "parent.bin", 10, 20, std::string(64, 'a')};
    report.artifacts_before[1] = {
        "tw.bin", 30, 40, std::string(64, 'b')};
    report.artifacts_after = report.artifacts_before;
    report.parent_fingerprint = std::string(64, 'c');
    report.control_fingerprint = std::string(64, 'd');
    report.treatment_fingerprint = std::string(64, 'e');
    report.holdout =
        tw::score_holdout_records(
            passing_holdout_records());
    report.offline_gate = tw::evaluate_offline_gate(
        report.holdout,
        {
            .expected_physical_games = 10,
            .expected_perspectives_per_deck = 2,
        });

    std::ostringstream output;
    tw::write_tsv_report(report, output);
    const std::string text = output.str();
    expect(
        text.find("TW_C17_TSV_BEGIN") !=
                std::string::npos &&
            text.find("TW_C17_TSV_END") !=
                std::string::npos,
        "TSV markers");
    expect(
        text.find("\tSUPPRESSED\t") != std::string::npos,
        "suppressed panel row");
    std::istringstream lines(text);
    std::string line;
    bool in_tsv = false;
    while (std::getline(lines, line)) {
        if (line == "TW_C17_TSV_BEGIN") {
            in_tsv = true;
            continue;
        }
        if (line == "TW_C17_TSV_END") {
            break;
        }
        if (in_tsv) {
            expect(
                tab_fields(line) == 13,
                "every TSV row must have 13 fields");
        }
    }
}

void test_canonical_route_is_load_only_in_source() {
    std::ifstream input("src/terminal_weight_eval.cpp");
    expect(
        static_cast<bool>(input),
        "evaluator source must be readable");
    const std::string source{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    expect(
        source.find(
            "load_learned_value_challenger_artifact") !=
            std::string::npos &&
            source.find(
                "load_learned_terminal_weight_c17_artifact") !=
                std::string::npos,
        "sealed route must load both artifacts");
    expect(
        source.find("train_learned") == std::string::npos,
        "sealed route must not link or call a trainer");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "CR1 clustered estimator",
        test_cr1_uses_record_weighted_game_residuals);
    tests.run(
        "HOLD1 balanced schedule",
        test_holdout_schedule_is_exactly_balanced);
    tests.run(
        "HOLD1 metrics and gate",
        test_holdout_metrics_and_gate_follow_preregistration);
    tests.run(
        "offline gate boundaries",
        test_offline_gate_boundaries_and_bias_inheritance);
    tests.run(
        "same-deck common-seed quartets",
        test_gameplay_schedule_forms_common_seed_quartets);
    tests.run(
        "gameplay gate and suppression",
        test_gameplay_gate_and_panel_suppression);
    tests.run(
        "gameplay raw-win/draw boundaries",
        test_gameplay_raw_win_draw_and_cluster_boundaries);
    tests.run(
        "artifact snapshot",
        test_artifact_snapshot_binds_content_size_and_time);
    tests.run(
        "strict TSV report",
        test_reports_have_strict_tsv_and_suppression_rows);
    tests.run(
        "load-only canonical route",
        test_canonical_route_is_load_only_in_source);
    return tests.finish();
}
