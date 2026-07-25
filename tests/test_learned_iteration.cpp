#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace iteration = old_school::learned_iteration;
using old_school::DeckId;

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
                  << " learned iteration tests passed\n";
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

void expect_near(double actual, double expected,
                 double tolerance, std::string_view message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) + ": expected " +
            std::to_string(expected) + ", got " +
            std::to_string(actual));
    }
}

template <typename Function>
void expect_invalid(Function&& function,
                    std::string_view message) {
    bool threw = false;
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, message);
}

void test_schedule_is_exactly_balanced() {
    const auto games =
        iteration::balanced_schedule(424242, 3, 7);
    expect(
        games.size() == iteration::kBalancedScheduleGames,
        "schedule must contain exactly 40 games");

    std::array<std::size_t, iteration::kBalancedDeckCount>
        appearances{};
    std::array<std::size_t, iteration::kBalancedDeckCount>
        seat_zero{};
    std::array<std::size_t, iteration::kBalancedDeckCount>
        seat_one{};
    std::array<std::size_t, iteration::kBalancedDeckCount>
        starts{};
    std::array<std::size_t, iteration::kBalancedDeckCount>
        draws{};
    std::array<std::size_t,
               iteration::kBalancedPairings>
        pairing_games{};
    constexpr std::array<std::pair<DeckId, DeckId>,
                         iteration::kBalancedPairings>
        expected_pairings = {{
            {DeckId::Green, DeckId::Red},
            {DeckId::Green, DeckId::Blue},
            {DeckId::Green, DeckId::White},
            {DeckId::Green, DeckId::RUAggro},
            {DeckId::Red, DeckId::Blue},
            {DeckId::Red, DeckId::White},
            {DeckId::Red, DeckId::RUAggro},
            {DeckId::Blue, DeckId::White},
            {DeckId::Blue, DeckId::RUAggro},
            {DeckId::White, DeckId::RUAggro},
        }};
    std::set<std::pair<DeckId, DeckId>>
        unordered_pairings;
    std::set<std::tuple<std::size_t, DeckId, DeckId,
                        std::size_t>>
        exact_games;

    for (std::size_t index = 0; index < games.size();
         ++index) {
        const auto& game = games[index];
        expect(game.schedule_index == index,
               "schedule indices must be contiguous");
        expect(game.pairing_index <
                   iteration::kBalancedPairings,
               "pairing index out of range");
        expect(game.starting_player < 2,
               "starting player out of range");
        expect(game.seat_decks[0] != game.seat_decks[1],
               "a deck cannot play itself in the block");
        ++pairing_games[game.pairing_index];

        const auto canonical_pair =
            game.seat_decks[0] < game.seat_decks[1]
                ? std::pair{
                      game.seat_decks[0],
                      game.seat_decks[1],
                  }
                : std::pair{
                      game.seat_decks[1],
                      game.seat_decks[0],
                  };
        expect(
            canonical_pair ==
                expected_pairings[game.pairing_index],
            "pairing index must identify its canonical deck pair");
        unordered_pairings.insert(canonical_pair);

        for (std::size_t seat = 0; seat < 2; ++seat) {
            const std::size_t deck =
                static_cast<std::size_t>(
                    game.seat_decks[seat]);
            expect(
                deck < iteration::kBalancedDeckCount,
                "scheduled deck must belong to the five-deck environment");
            ++appearances[deck];
            if (seat == 0) {
                ++seat_zero[deck];
            } else {
                ++seat_one[deck];
            }
            if (seat == game.starting_player) {
                ++starts[deck];
            } else {
                ++draws[deck];
            }
        }
        expect(
            exact_games
                .insert({
                    game.pairing_index,
                    game.seat_decks[0],
                    game.seat_decks[1],
                    game.starting_player,
                })
                .second,
            "each pairing/orientation/starter must be unique");
    }

    for (const std::size_t count : pairing_games) {
        expect(count == 4,
               "each unordered pairing needs four games");
    }
    expect(
        unordered_pairings.size() ==
            iteration::kBalancedPairings,
        "schedule must contain all ten unordered deck pairs");
    for (std::size_t deck = 0;
         deck < iteration::kBalancedDeckCount; ++deck) {
        expect(appearances[deck] == 16,
               "each deck needs sixteen appearances");
        expect(seat_zero[deck] == 8 &&
                   seat_one[deck] == 8,
               "each deck must be seat-balanced");
        expect(starts[deck] == 8 && draws[deck] == 8,
               "each deck must be play/draw balanced");
    }
}

void test_seed_derivation_is_indexed_and_domain_separated() {
    const auto first =
        iteration::balanced_schedule(424242, 3, 7);
    const auto repeated =
        iteration::balanced_schedule(424242, 3, 7);
    expect(first == repeated,
           "fixed schedule seed must be deterministic");

    const auto next_generation =
        iteration::balanced_schedule(424242, 4, 7);
    const auto next_block =
        iteration::balanced_schedule(424242, 3, 8);
    std::set<std::uint64_t> schedule_seeds;
    for (std::size_t index = 0; index < first.size();
         ++index) {
        expect(first[index].seed !=
                   next_generation[index].seed,
               "generation must affect game seed");
        expect(first[index].seed != next_block[index].seed,
               "block must affect game seed");
        expect(schedule_seeds.insert(first[index].seed).second,
               "schedule game seeds must be distinct");
    }

    constexpr std::array<iteration::SeedDomain, 8>
        domains = {
            iteration::SeedDomain::SelfPlayGame,
            iteration::SeedDomain::PrioritySearch,
            iteration::SeedDomain::AttackSearch,
            iteration::SeedDomain::PriorityChoice,
            iteration::SeedDomain::AttackChoice,
            iteration::SeedDomain::CriticFit,
            iteration::SeedDomain::PolicyFit,
            iteration::SeedDomain::ReplayShuffle,
        };
    std::set<std::uint64_t> domain_seeds;
    for (const auto domain : domains) {
        domain_seeds.insert(iteration::derive_seed(
            424242, domain, 3, 7, 11));
    }
    expect(domain_seeds.size() == domains.size(),
           "seed domains must be separated");
    expect(
        iteration::derive_seed(
            424242, iteration::SeedDomain::PriorityChoice,
            3, 7, 11) !=
            iteration::derive_seed(
                424242,
                iteration::SeedDomain::AttackChoice,
                3, 7, 11),
        "priority and attack choices need independent seeds");

    const std::uint64_t seed = iteration::derive_seed(
        424242, iteration::SeedDomain::PrioritySearch,
        3, 7, 11);
    expect(
        seed == iteration::derive_seed(
                    424242,
                    iteration::SeedDomain::PrioritySearch,
                    3, 7, 11),
        "indexed seed derivation must be deterministic");
    expect(
        seed != iteration::derive_seed(
                    424242,
                    iteration::SeedDomain::PrioritySearch,
                    3, 8, 11),
        "item index must affect seed");
    expect(
        seed != iteration::derive_seed(
                    424242,
                    iteration::SeedDomain::PrioritySearch,
                    3, 7, 12),
        "subindex must affect seed");
}

void test_td_lambda_three_state_math_and_terminal_values() {
    const std::vector<double> values = {0.2, 0.4, 0.6};

    const auto win =
        iteration::td_lambda_targets(values, 1.0, 0.5);
    expect(win.size() == 3,
           "TD targets must match trajectory length");
    expect_near(win[0], 0.6, 1.0e-12,
                "three-state win target at t0");
    expect_near(win[1], 0.8, 1.0e-12,
                "three-state win target at t1");
    expect_near(win[2], 1.0, 1.0e-12,
                "terminal win target");

    const auto loss =
        iteration::td_lambda_targets(values, 0.0, 0.5);
    expect_near(loss[0], 0.35, 1.0e-12,
                "three-state loss target at t0");
    expect_near(loss[1], 0.3, 1.0e-12,
                "three-state loss target at t1");
    expect_near(loss[2], 0.0, 1.0e-12,
                "terminal loss target");

    const auto draw =
        iteration::td_lambda_targets(values, 0.5, 0.5);
    expect_near(draw[0], 0.475, 1.0e-12,
                "three-state draw target at t0");
    expect_near(draw[1], 0.55, 1.0e-12,
                "three-state draw target at t1");
    expect_near(draw[2], 0.5, 1.0e-12,
                "terminal draw target");

    const auto bootstrap_only =
        iteration::td_lambda_targets(values, 1.0, 0.0);
    expect_near(bootstrap_only[0], 0.4, 1.0e-12,
                "lambda zero t0 target");
    expect_near(bootstrap_only[1], 0.6, 1.0e-12,
                "lambda zero t1 target");
    expect_near(bootstrap_only[2], 1.0, 1.0e-12,
                "lambda zero terminal target");

    const auto terminal_only =
        iteration::td_lambda_targets(values, 0.5, 1.0);
    for (const double target : terminal_only) {
        expect_near(target, 0.5, 1.0e-12,
                    "lambda one must propagate terminal value");
    }

    const std::vector<double> one_state = {0.2};
    const auto one_state_targets =
        iteration::td_lambda_targets(
            one_state, 1.0, 0.9);
    expect(
        one_state_targets.size() == 1,
        "one-state trajectory needs one target");
    expect_near(one_state_targets[0], 1.0, 0.0,
                "one-state target must be terminal");

    const std::vector<double> empty;
    expect(
        iteration::td_lambda_targets(
            empty, 0.5, 0.9).empty(),
        "empty trajectory must have no targets");

    expect_near(
        iteration::terminal_value_for_perspective(0, 0),
        1.0, 0.0, "winner perspective value");
    expect_near(
        iteration::terminal_value_for_perspective(0, 1),
        0.0, 0.0, "loser perspective value");
    expect_near(
        iteration::terminal_value_for_perspective(-1, 0),
        0.5, 0.0, "draw perspective zero value");
    expect_near(
        iteration::terminal_value_for_perspective(-1, 1),
        0.5, 0.0, "draw perspective one value");

    expect_invalid(
        [&] {
            iteration::td_lambda_targets(
                values, 1.0, 1.01);
        },
        "lambda above one must be rejected");
    expect_invalid(
        [&] {
            iteration::td_lambda_targets(
                values, 1.0, -0.01);
        },
        "lambda below zero must be rejected");
    expect_invalid(
        [&] {
            iteration::td_lambda_targets(
                values, 1.0,
                std::numeric_limits<double>::quiet_NaN());
        },
        "NaN lambda must be rejected");
    expect_invalid(
        [&] {
            iteration::td_lambda_targets(
                values, -0.01, 0.9);
        },
        "invalid terminal value must be rejected");
    expect_invalid(
        [&] {
            const std::vector<double> invalid = {
                0.2,
                std::numeric_limits<double>::quiet_NaN(),
            };
            iteration::td_lambda_targets(
                invalid, 1.0, 0.9);
        },
        "invalid critic value must be rejected");
    expect_invalid(
        [] {
            iteration::terminal_value_for_perspective(
                2, 0);
        },
        "invalid winner must be rejected");
    expect_invalid(
        [] {
            iteration::terminal_value_for_perspective(
                0, 2);
        },
        "invalid perspective must be rejected");
}

void test_four_state_bootstrap_is_exact_and_terminal_at_tail() {
    const std::vector<double> parent_values = {
        0.10, 0.20, 0.30, 0.40,
        0.50, 0.60, 0.70, 0.80,
        0.90,
    };
    const auto targets =
        iteration::four_state_bootstrap_targets(
            parent_values, 0.25);
    const std::vector<double> expected = {
        0.375, 0.425, 0.475, 0.525, 0.575,
        0.25, 0.25, 0.25, 0.25,
    };
    expect(targets.size() == expected.size(),
           "bootstrap target count must match state count");
    for (std::size_t index = 0; index < targets.size();
         ++index) {
        expect_near(
            targets[index], expected[index], 1.0e-12,
            "four-state bootstrap target");
    }

    const std::vector<double> four_values = {
        0.1, 0.2, 0.3, 0.4,
    };
    const auto four_targets =
        iteration::four_state_bootstrap_targets(
            four_values, 1.0);
    expect(
        std::all_of(
            four_targets.begin(), four_targets.end(),
            [](double target) { return target == 1.0; }),
        "a four-state trace has no state four positions later");
    const std::vector<double> empty;
    expect(
        iteration::four_state_bootstrap_targets(
            empty, 0.5).empty(),
        "empty bootstrap trajectory must stay empty");

    expect_invalid(
        [&] {
            iteration::four_state_bootstrap_targets(
                parent_values, 1.01);
        },
        "invalid bootstrap terminal value must be rejected");
    expect_invalid(
        [] {
            const std::vector<double> invalid = {
                0.2,
                std::numeric_limits<double>::infinity(),
            };
            iteration::four_state_bootstrap_targets(
                invalid, 0.5);
        },
        "invalid parent bootstrap value must be rejected");
}

void test_value_g8_mix50_assignment_is_exact_and_rng_free() {
    for (std::size_t generation = 1; generation <= 4;
         ++generation) {
        for (std::size_t game = 0; game < 200; ++game) {
            expect(
                !iteration::value_g8_mix50_game_uses_search(
                    generation, game),
                "G1-G4 must remain entirely raw");
        }
    }
    for (std::size_t generation = 5; generation <= 8;
         ++generation) {
        std::size_t raw_games = 0;
        std::size_t search_games = 0;
        for (std::size_t game = 0; game < 200; ++game) {
            const bool searched =
                iteration::value_g8_mix50_game_uses_search(
                    generation, game);
            expect(
                searched == (game % 2 == 1),
                "each late pair must be raw then search");
            raw_games += searched ? 0 : 1;
            search_games += searched ? 1 : 0;
        }
        expect(raw_games == 100 && search_games == 100,
               "canonical late generations must split 100/100");
    }
    expect(
        !iteration::value_g8_mix50_game_uses_search(9, 1),
        "assignment must be bounded to G5-G8");
}

void test_replay_window_evicts_without_consuming() {
    iteration::ReplayWindow<std::string> replay;
    std::vector<std::string> first = {"g0-a", "g0-b"};
    replay.append_generation(0, first);
    first[0] = "caller-mutated";
    replay.append_generation(
        1, std::vector<std::string>{"g1"});

    const auto old_snapshot = replay.snapshot();
    expect(old_snapshot.size() == 2,
           "snapshot must include both generations");
    expect((*old_snapshot[0].examples)[0] == "g0-a",
           "lvalue insertion must copy into immutable storage");

    auto visit = [&](std::vector<std::string>& output) {
        replay.for_each(
            [&](std::uint64_t generation,
                const std::string& example) {
                output.push_back(
                    std::to_string(generation) + ":" +
                    example);
            });
    };
    std::vector<std::string> first_visit;
    std::vector<std::string> second_visit;
    visit(first_visit);
    visit(second_visit);
    const std::vector<std::string> expected_first_visit = {
        "0:g0-a",
        "0:g0-b",
        "1:g1",
    };
    expect(first_visit == expected_first_visit,
           "replay traversal must preserve generation and example order");
    expect(first_visit == second_visit,
           "replay iteration must not consume examples");
    expect(replay.example_count() == 3,
           "initial replay example count");

    replay.append_generation(
        2, std::vector<std::string>{"g2-a", "g2-b"});
    replay.append_generation(
        3, std::vector<std::string>{"g3"});
    const auto current = replay.snapshot();
    expect(
        replay.generation_count() ==
            iteration::kReplayWindowGenerations,
        "replay must retain exactly three generations");
    expect(current[0].index == 1 &&
               current[1].index == 2 &&
               current[2].index == 3,
           "oldest replay generation must be evicted");
    expect(replay.example_count() == 4,
           "evicted examples must leave current count");

    std::vector<std::string> current_visit;
    visit(current_visit);
    const std::vector<std::string> expected_current_visit = {
        "1:g1",
        "2:g2-a",
        "2:g2-b",
        "3:g3",
    };
    expect(
        current_visit == expected_current_visit,
        "evicted replay traversal must retain exact order");

    expect(old_snapshot[0].index == 0,
           "old snapshot generation index changed");
    expect((*old_snapshot[0].examples)[0] == "g0-a" &&
               (*old_snapshot[0].examples)[1] == "g0-b",
           "evicted immutable snapshot must remain valid");

    expect_invalid(
        [&] {
            replay.append_generation(
                3, std::vector<std::string>{"duplicate"});
        },
        "duplicate replay generation must be rejected");
    expect_invalid(
        [&] {
            replay.append_generation(
                2, std::vector<std::string>{"decreasing"});
        },
        "decreasing replay generation must be rejected");
    expect(replay.example_count() == 4,
           "rejected append must not mutate replay");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "balanced 40-game five-deck schedule",
        test_schedule_is_exactly_balanced);
    runner.run(
        "indexed independent seed domains",
        test_seed_derivation_is_indexed_and_domain_separated);
    runner.run(
        "TD lambda three-state and terminal math",
        test_td_lambda_three_state_math_and_terminal_values);
    runner.run(
        "exact four-state Value bootstrap",
        test_four_state_bootstrap_is_exact_and_terminal_at_tail);
    runner.run(
        "exact Value G8 Late-Mix50 assignment",
        test_value_g8_mix50_assignment_is_exact_and_rng_free);
    runner.run(
        "immutable three-generation replay window",
        test_replay_window_evicts_without_consuming);
    return runner.finish();
}
