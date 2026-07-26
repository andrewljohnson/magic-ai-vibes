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

double test_distribution_kl(
    const std::vector<double>& target,
    const std::vector<double>& distribution) {
    expect(
        target.size() == distribution.size(),
        "test KL vectors must have equal size");
    long double divergence = 0.0L;
    for (std::size_t index = 0; index < target.size();
         ++index) {
        if (target[index] != 0.0) {
            divergence +=
                static_cast<long double>(target[index]) *
                std::log(
                    static_cast<long double>(target[index]) /
                    static_cast<long double>(
                        distribution[index]));
        }
    }
    return static_cast<double>(divergence);
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

void test_evenly_spaced_retention_is_exact_and_validated() {
    expect(
        iteration::evenly_spaced_retained_indices(0, 4).empty(),
        "empty source must retain no indices");
    expect(
        iteration::evenly_spaced_retained_indices(1, 4) ==
            std::vector<std::size_t>{0},
        "cap above total must retain the only index");
    expect(
        iteration::evenly_spaced_retained_indices(4, 4) ==
            std::vector<std::size_t>{0, 1, 2, 3},
        "cap equal to total must retain every index");
    expect(
        iteration::evenly_spaced_retained_indices(3, 5) ==
            std::vector<std::size_t>{0, 1, 2},
        "cap above total must preserve chronological order");
    expect(
        iteration::evenly_spaced_retained_indices(10, 3) ==
            std::vector<std::size_t>{3, 6, 9},
        "ten roots capped at three must use declared spacing");
    expect(
        iteration::evenly_spaced_retained_indices(9, 4) ==
            std::vector<std::size_t>{2, 4, 6, 8},
        "uneven division must retain floor-boundary indices");
    expect(
        iteration::evenly_spaced_retained_indices(5, 1) ==
            std::vector<std::size_t>{4},
        "one retained root must be the final chronological root");

    for (std::size_t total = 0; total <= 100; ++total) {
        for (std::size_t cap = 1; cap <= 20; ++cap) {
            const auto retained =
                iteration::evenly_spaced_retained_indices(
                    total, cap);
            expect(
                retained.size() == std::min(total, cap),
                "retention must have exact bounded count");
            expect(
                std::is_sorted(
                    retained.begin(), retained.end()),
                "retained indices must remain chronological");
            expect(
                std::adjacent_find(
                    retained.begin(), retained.end()) ==
                    retained.end(),
                "retained indices must be unique");
            expect(
                std::all_of(
                    retained.begin(), retained.end(),
                    [&](std::size_t index) {
                        return index < total;
                    }),
                "retained index must be in range");

            std::vector<std::size_t> direct_formula;
            if (total <= cap) {
                for (std::size_t index = 0;
                     index < total; ++index) {
                    direct_formula.push_back(index);
                }
            } else {
                for (std::size_t index = 0;
                     index < total; ++index) {
                    if ((index + 1) * cap / total !=
                        index * cap / total) {
                        direct_formula.push_back(index);
                    }
                }
            }
            expect(
                retained == direct_formula,
                "retention must exactly implement declared formula");
            expect(
                retained ==
                    iteration::evenly_spaced_retained_indices(
                        total, cap),
                "retention must be deterministic");
        }
    }

    expect_invalid(
        [] {
            iteration::evenly_spaced_retained_indices(0, 0);
        },
        "zero retention cap must be rejected for empty input");
    expect_invalid(
        [] {
            iteration::evenly_spaced_retained_indices(5, 0);
        },
        "zero retention cap must be rejected");
}

void test_p16_exploration_distribution_is_stable_and_validated() {
    const std::vector<double> scores = {0.2, 0.4, 0.1};
    const auto distribution =
        iteration::p16_exploration_distribution(scores);
    const std::vector<double> expected = {
        0.13610901277946838,
        0.7927485943665389,
        0.07114239285399276,
    };
    expect(distribution.size() == expected.size(),
           "P16 exploration action count");
    double total = 0.0;
    for (std::size_t index = 0;
         index < distribution.size(); ++index) {
        expect_near(
            distribution[index], expected[index], 1.0e-15,
            "P16 exploration fixed example");
        expect(
            distribution[index] >=
                0.1 / static_cast<double>(
                          distribution.size()),
            "P16 uniform exploration floor");
        total += distribution[index];
    }
    expect_near(total, 1.0, 1.0e-15,
                "P16 exploration normalization");
    expect(
        distribution ==
            iteration::p16_exploration_distribution(scores),
        "P16 exploration must be deterministic");
    expect(
        distribution[1] > distribution[0] &&
            distribution[0] > distribution[2],
        "P16 exploration must preserve score ordering");

    const auto uniform =
        iteration::p16_exploration_distribution(
            std::vector<double>{7.0, 7.0, 7.0, 7.0});
    for (const double probability : uniform) {
        expect_near(probability, 0.25, 0.0,
                    "equal scores must remain exactly uniform");
    }

    const auto extreme =
        iteration::p16_exploration_distribution(
            std::vector<double>{
                1.0e300, -1.0e300, 0.0});
    expect_near(
        extreme[0], 0.93333333333333335, 1.0e-15,
        "stable softmax dominant action");
    expect_near(
        extreme[1], 0.033333333333333326, 1.0e-15,
        "stable softmax low action");
    expect_near(
        extreme[2], 0.033333333333333326, 1.0e-15,
        "stable softmax middle action");
    expect(
        std::all_of(
            extreme.begin(), extreme.end(),
            [](double probability) {
                return std::isfinite(probability);
            }),
        "stable softmax must remain finite");

    expect_invalid(
        [] {
            iteration::p16_exploration_distribution(
                std::vector<double>{});
        },
        "empty P16 score vector must be rejected");
    expect_invalid(
        [] {
            iteration::p16_exploration_distribution(
                std::vector<double>{
                    0.0,
                    std::numeric_limits<double>::quiet_NaN(),
                });
        },
        "NaN P16 score must be rejected");
    expect_invalid(
        [] {
            iteration::p16_exploration_distribution(
                std::vector<double>{
                    0.0,
                    std::numeric_limits<double>::infinity(),
                });
        },
        "infinite P16 score must be rejected");
}

void test_p16_outcome_signal_blends_td_and_terminal_and_clamps() {
    const std::vector<double> baselines = {0.2, 0.4, 0.6};
    const auto win =
        iteration::p16_outcome_signal(
            baselines, 1.0, 0.5);
    const std::vector<double> expected_win_returns = {
        0.8, 0.9, 1.0,
    };
    const std::vector<double> expected_win_advantages = {
        0.5, 0.5, 0.4,
    };
    expect(win.returns.size() == baselines.size() &&
               win.advantages.size() == baselines.size(),
           "P16 outcome signal must match trajectory length");
    for (std::size_t index = 0; index < baselines.size();
         ++index) {
        expect_near(
            win.returns[index],
            expected_win_returns[index], 1.0e-12,
            "P16 blended win return");
        expect_near(
            win.advantages[index],
            expected_win_advantages[index], 1.0e-12,
            "P16 clamped win advantage");
    }

    const auto loss =
        iteration::p16_outcome_signal(
            baselines, 0.0, 0.5);
    const std::vector<double> expected_loss_returns = {
        0.175, 0.15, 0.0,
    };
    const std::vector<double> expected_loss_advantages = {
        -0.025, -0.25, -0.5,
    };
    for (std::size_t index = 0; index < baselines.size();
         ++index) {
        expect_near(
            loss.returns[index],
            expected_loss_returns[index], 1.0e-12,
            "P16 blended loss return");
        expect_near(
            loss.advantages[index],
            expected_loss_advantages[index], 1.0e-12,
            "P16 clamped loss advantage");
        expect(
            loss.advantages[index] >=
                    -iteration::kP16AdvantageLimit &&
                loss.advantages[index] <=
                    iteration::kP16AdvantageLimit,
            "P16 advantage must stay within its clamp");
    }

    const auto default_lambda =
        iteration::p16_outcome_signal(
            baselines, 0.5);
    expect(
        default_lambda ==
            iteration::p16_outcome_signal(
                baselines, 0.5,
                iteration::kP16TdLambda),
        "P16 default lambda must be deterministic and explicit");
    const std::vector<double> empty;
    const auto empty_signal =
        iteration::p16_outcome_signal(empty, 0.5);
    expect(
        empty_signal.returns.empty() &&
            empty_signal.advantages.empty(),
        "empty P16 trajectory must remain empty");

    expect_invalid(
        [] {
            iteration::p16_outcome_signal(
                std::vector<double>{0.2, -0.1},
                0.5);
        },
        "invalid P16 baseline must be rejected");
    expect_invalid(
        [&] {
            iteration::p16_outcome_signal(
                baselines, 1.1);
        },
        "invalid P16 terminal value must be rejected");
    expect_invalid(
        [&] {
            iteration::p16_outcome_signal(
                baselines, 0.5,
                std::numeric_limits<double>::quiet_NaN());
        },
        "invalid P16 lambda must be rejected");
}

void test_p16_all_action_target_tilts_only_the_chosen_action() {
    const std::vector<double> behavior = {0.2, 0.3, 0.5};
    const auto unchanged =
        iteration::p16_all_action_target(
            behavior, 1, 0.0);
    expect(
        unchanged == behavior,
        "zero P16 advantage must preserve exact input bits");

    const auto positive =
        iteration::p16_all_action_target(
            behavior, 1, 0.25);
    const std::vector<double> expected_positive = {
        0.13197099250730032,
        0.5381015262244488,
        0.3299274812682508,
    };
    const auto negative =
        iteration::p16_all_action_target(
            behavior, 1, -0.25);
    const std::vector<double> expected_negative = {
        0.24680272245080337,
        0.13619047142218818,
        0.6170068061270084,
    };
    double positive_total = 0.0;
    double negative_total = 0.0;
    for (std::size_t index = 0; index < behavior.size();
         ++index) {
        expect_near(
            positive[index], expected_positive[index],
            1.0e-15, "positive P16 all-action target");
        expect_near(
            negative[index], expected_negative[index],
            1.0e-15, "negative P16 all-action target");
        positive_total += positive[index];
        negative_total += negative[index];
    }
    expect_near(positive_total, 1.0, 1.0e-15,
                "positive P16 target normalization");
    expect_near(negative_total, 1.0, 1.0e-15,
                "negative P16 target normalization");
    expect(
        positive[1] > behavior[1] &&
            positive[0] < behavior[0] &&
            positive[2] < behavior[2],
        "positive advantage must raise only the chosen action");
    expect(
        negative[1] < behavior[1] &&
            negative[0] > behavior[0] &&
            negative[2] > behavior[2],
        "negative advantage must suppress only the chosen action");
    expect(
        positive ==
            iteration::p16_all_action_target(
                behavior, 1, 0.25),
        "P16 all-action target must be deterministic");

    const auto maximum_positive =
        iteration::p16_all_action_target(
            behavior, 1,
            iteration::kP16AdvantageLimit);
    const auto maximum_negative =
        iteration::p16_all_action_target(
            behavior, 1,
            -iteration::kP16AdvantageLimit);
    expect(
        maximum_positive[1] > positive[1] &&
            maximum_negative[1] < negative[1],
        "P16 advantage bounds must produce monotone tilts");

    expect_invalid(
        [] {
            iteration::p16_all_action_target(
                std::vector<double>{}, 0, 0.0);
        },
        "empty P16 behavior distribution must be rejected");
    expect_invalid(
        [&] {
            iteration::p16_all_action_target(
                behavior, behavior.size(), 0.0);
        },
        "out-of-range P16 chosen action must be rejected");
    expect_invalid(
        [] {
            iteration::p16_all_action_target(
                std::vector<double>{0.2, 0.2},
                0, 0.0);
        },
        "unnormalized P16 behavior distribution must be rejected");
    expect_invalid(
        [] {
            iteration::p16_all_action_target(
                std::vector<double>{-0.1, 1.1},
                0, 0.0);
        },
        "out-of-range P16 behavior probability must be rejected");
    expect_invalid(
        [] {
            iteration::p16_all_action_target(
                std::vector<double>{
                    std::numeric_limits<double>::quiet_NaN(),
                    1.0,
                },
                0, 0.0);
        },
        "non-finite P16 behavior probability must be rejected");
    expect_invalid(
        [&] {
            iteration::p16_all_action_target(
                behavior, 1, 0.500001);
        },
        "P16 advantage above clamp must be rejected");
    expect_invalid(
        [&] {
            iteration::p16_all_action_target(
                behavior, 1,
                std::numeric_limits<double>::quiet_NaN());
        },
        "non-finite P16 advantage must be rejected");
}

void test_p16_mechanism_metrics_cover_ties_signs_and_saturation() {
    const std::vector<double> parent_one = {0.0, 0.0};
    const std::vector<double> candidate_one = {0.05, -0.05};
    const auto behavior_one =
        iteration::p16_exploration_distribution(parent_one);
    const auto target_one =
        iteration::p16_all_action_target(
            behavior_one, 0, 0.25);

    const std::vector<double> parent_two = {0.001, 0.0};
    const std::vector<double> candidate_two = {-0.1, 0.1};
    const auto behavior_two =
        iteration::p16_exploration_distribution(parent_two);
    const auto target_two =
        iteration::p16_all_action_target(
            behavior_two, 0, -0.5);

    const std::vector<double> parent_three = {0.0, 0.2, 0.2};
    const auto behavior_three =
        iteration::p16_exploration_distribution(parent_three);

    const std::vector<double> parent_four = {0.2, 0.0};
    const auto behavior_four =
        iteration::p16_exploration_distribution(parent_four);
    const auto target_four =
        iteration::p16_all_action_target(
            behavior_four, 1, 0.25);

    const std::vector<iteration::P16MechanismObservation>
        observations = {
            {
                .parent_combined_scores = parent_one,
                .candidate_combined_scores = candidate_one,
                .candidate_centered_policy_logits = {2.0, -2.0},
                .target_probabilities = target_one,
                .chosen = 0,
                .advantage = 0.25,
                .weight = 2.0,
            },
            {
                .parent_combined_scores = parent_two,
                .candidate_combined_scores = candidate_two,
                .candidate_centered_policy_logits = {0.1, -0.1},
                .target_probabilities = target_two,
                .chosen = 0,
                .advantage = -0.5,
                .weight = 1.0,
            },
            {
                .parent_combined_scores = parent_three,
                .candidate_combined_scores = parent_three,
                .candidate_centered_policy_logits = {
                    2.0, -1.0, -1.0,
                },
                .target_probabilities = behavior_three,
                .chosen = 0,
                .advantage = 0.0,
                .weight = 3.0,
            },
            {
                .parent_combined_scores = parent_four,
                .candidate_combined_scores = parent_four,
                .candidate_centered_policy_logits = {0.1, -0.1},
                .target_probabilities = target_four,
                .chosen = 1,
                .advantage = 0.25,
                .weight = 4.0,
            },
        };
    const auto metrics =
        iteration::evaluate_p16_mechanism_metrics(
            observations);

    expect(metrics.observation_count == 4,
           "mechanism observation count");
    expect_near(metrics.total_weight, 10.0, 0.0,
                "mechanism total weight");
    expect_near(metrics.positive_advantage_weight, 6.0, 0.0,
                "positive advantage weight");
    expect_near(metrics.negative_advantage_weight, 1.0, 0.0,
                "negative advantage weight");
    expect_near(metrics.zero_advantage_weight, 3.0, 0.0,
                "zero advantage weight");
    expect_near(
        metrics.conflict_weight, 5.0, 0.0,
        "negative exact-best and positive nonbest must conflict");

    // The first chosen action belongs to the full two-action parent argmax
    // tie, so its positive advantage is not a search/outcome conflict.
    expect_near(
        metrics.conflict_weight,
        observations[1].weight + observations[3].weight,
        0.0, "full parent argmax ties must be honored");

    expect_near(
        metrics.eligible_signed_movement_weight, 7.0, 0.0,
        "eligible signed movement weight");
    expect_near(
        metrics.correct_signed_movement_weight, 3.0, 0.0,
        "correct signed movement weight");
    expect_near(
        metrics.signed_movement_correct_rate, 3.0 / 7.0,
        1.0e-15, "pooled signed movement rate");
    expect_near(
        metrics.eligible_positive_movement_weight, 6.0, 0.0,
        "eligible positive movement weight");
    expect_near(
        metrics.correct_positive_movement_weight, 2.0, 0.0,
        "correct positive movement weight");
    expect_near(
        metrics.positive_movement_correct_rate, 1.0 / 3.0,
        1.0e-15, "positive movement rate");
    expect_near(
        metrics.eligible_negative_movement_weight, 1.0, 0.0,
        "eligible negative movement weight");
    expect_near(
        metrics.correct_negative_movement_weight, 1.0, 0.0,
        "correct negative movement weight");
    expect_near(
        metrics.negative_movement_correct_rate, 1.0, 0.0,
        "negative movement rate");

    expect_near(metrics.changed_argmax_weight, 3.0, 0.0,
                "exact argmax-set change weight");
    expect_near(
        metrics.changed_argmax_weight_fraction, 0.3,
        1.0e-15, "exact argmax-set change fraction");
    expect(metrics.residual_option_count == 9,
           "raw residual option count");
    expect(metrics.saturated_residual_count == 3,
           "raw saturated residual count");
    expect_near(
        metrics.residual_option_weight, 10.0, 0.0,
        "each root contributes its weight to option mass");
    expect_near(
        metrics.saturated_residual_weight, 3.0, 1.0e-15,
        "saturation must split each root weight over options");
    expect_near(
        metrics.residual_saturation_fraction, 0.3,
        1.0e-15, "balanced residual saturation fraction");

    const auto candidate_distribution_one =
        iteration::p16_exploration_distribution(candidate_one);
    const auto candidate_distribution_two =
        iteration::p16_exploration_distribution(candidate_two);
    const double expected_parent_kl =
        (2.0 * test_distribution_kl(
                   target_one, behavior_one) +
         test_distribution_kl(target_two, behavior_two) +
         3.0 * test_distribution_kl(
                   behavior_three, behavior_three) +
         4.0 * test_distribution_kl(
                   target_four, behavior_four)) /
        10.0;
    const double expected_candidate_kl =
        (2.0 * test_distribution_kl(
                   target_one, candidate_distribution_one) +
         test_distribution_kl(
             target_two, candidate_distribution_two) +
         3.0 * test_distribution_kl(
                   behavior_three, behavior_three) +
         4.0 * test_distribution_kl(
                   target_four, behavior_four)) /
        10.0;
    expect_near(
        metrics.parent_kl, expected_parent_kl, 1.0e-15,
        "weighted parent full-distribution KL");
    expect_near(
        metrics.candidate_kl, expected_candidate_kl, 1.0e-15,
        "weighted candidate full-distribution KL");
    expect(metrics.kl_reduction_defined,
           "positive parent KL must define reduction");
    expect_near(
        metrics.kl_reduction_fraction,
        (expected_parent_kl - expected_candidate_kl) /
            expected_parent_kl,
        1.0e-15, "weighted KL reduction");
}

void test_p16_mechanism_zero_kl_and_movement_threshold() {
    const std::vector<double> scores = {0.2, -0.1};
    const auto distribution =
        iteration::p16_exploration_distribution(scores);
    const std::vector<iteration::P16MechanismObservation>
        observations = {{
            .parent_combined_scores = scores,
            .candidate_combined_scores = scores,
            .candidate_centered_policy_logits = {0.0, 0.0},
            .target_probabilities = distribution,
            .chosen = 0,
            .advantage = 0.0,
            .weight = 2.0,
        }};
    const auto metrics =
        iteration::evaluate_p16_mechanism_metrics(
            observations);
    expect_near(metrics.parent_kl, 0.0, 0.0,
                "identical parent target KL");
    expect_near(metrics.candidate_kl, 0.0, 0.0,
                "identical candidate target KL");
    expect(!metrics.kl_reduction_defined,
           "zero parent KL must leave reduction undefined");
    expect_near(metrics.kl_reduction_fraction, 0.0, 0.0,
                "undefined KL reduction has neutral storage");
    expect_near(metrics.zero_advantage_weight, 2.0, 0.0,
                "exact zero advantage weight");
    expect_near(
        metrics.eligible_signed_movement_weight, 0.0, 0.0,
        "zero advantage is ineligible for signed movement");

    auto tiny = observations.front();
    tiny.advantage =
        0.5 * iteration::kP16MechanismMovementTolerance;
    const std::array tiny_observations = {tiny};
    const auto tiny_metrics =
        iteration::evaluate_p16_mechanism_metrics(
            tiny_observations);
    expect_near(
        tiny_metrics.positive_advantage_weight, 2.0, 0.0,
        "small positive advantage remains positive accounting");
    expect_near(
        tiny_metrics.eligible_signed_movement_weight, 0.0, 0.0,
        "sub-tolerance advantage is movement-ineligible");

    const std::vector<iteration::P16MechanismObservation> empty;
    expect(
        iteration::evaluate_p16_mechanism_metrics(empty) ==
            iteration::P16MechanismMetrics{},
        "empty mechanism input must return zero undefined metrics");
}

void test_p16_mechanism_metrics_reject_malformed_observations() {
    const std::vector<double> scores = {0.1, 0.0};
    const auto distribution =
        iteration::p16_exploration_distribution(scores);
    const iteration::P16MechanismObservation valid = {
        .parent_combined_scores = scores,
        .candidate_combined_scores = scores,
        .candidate_centered_policy_logits = {0.0, 0.0},
        .target_probabilities = distribution,
        .chosen = 0,
        .advantage = 0.0,
        .weight = 1.0,
    };
    const auto evaluate_one =
        [](const iteration::P16MechanismObservation& observation) {
            const std::array observations = {observation};
            static_cast<void>(
                iteration::evaluate_p16_mechanism_metrics(
                    observations));
        };

    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.parent_combined_scores.clear();
            invalid.candidate_combined_scores.clear();
            invalid.candidate_centered_policy_logits.clear();
            invalid.target_probabilities.clear();
            evaluate_one(invalid);
        },
        "empty mechanism vectors must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.candidate_combined_scores.pop_back();
            evaluate_one(invalid);
        },
        "mismatched candidate scores must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.candidate_centered_policy_logits.pop_back();
            evaluate_one(invalid);
        },
        "mismatched candidate logits must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.target_probabilities.pop_back();
            evaluate_one(invalid);
        },
        "mismatched targets must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.chosen = 2;
            evaluate_one(invalid);
        },
        "out-of-range mechanism choice must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.parent_combined_scores[0] =
                std::numeric_limits<double>::quiet_NaN();
            evaluate_one(invalid);
        },
        "non-finite parent score must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.candidate_centered_policy_logits[0] =
                std::numeric_limits<double>::infinity();
            evaluate_one(invalid);
        },
        "non-finite candidate logit must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.target_probabilities = {0.2, 0.2};
            evaluate_one(invalid);
        },
        "unnormalized mechanism target must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.target_probabilities = {
                std::numeric_limits<double>::quiet_NaN(), 1.0,
            };
            evaluate_one(invalid);
        },
        "non-finite mechanism target must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.weight = 0.0;
            evaluate_one(invalid);
        },
        "zero mechanism weight must be rejected");
    expect_invalid(
        [&] {
            auto invalid = valid;
            invalid.advantage =
                iteration::kP16AdvantageLimit + 0.01;
            evaluate_one(invalid);
        },
        "out-of-range mechanism advantage must be rejected");
}

std::vector<double> scores_with_centered_tanh_residual(
    const std::vector<double>& base_scores,
    const std::vector<double>& centered_logits) {
    expect(
        base_scores.size() == centered_logits.size(),
        "test residual vectors must have equal size");
    std::vector<double> scores = base_scores;
    for (std::size_t option = 0; option < scores.size();
         ++option) {
        scores[option] +=
            0.10 * std::tanh(centered_logits[option]);
    }
    return scores;
}

void test_centered_tanh_oracle_recovers_feasible_root() {
    const std::vector<double> base = {0.02, -0.01, 0.0};
    const std::vector<double> centered = {0.7, -0.2, -0.5};
    const auto target =
        iteration::p16_exploration_distribution(
            scores_with_centered_tanh_residual(
                base, centered));
    const std::array observations = {
        iteration::CenteredTanhOracleObservation{
            .base_scores = base,
            .target_probabilities = target,
            .weight = 2.0,
        },
    };
    const auto first =
        iteration::evaluate_centered_tanh_rootwise_oracle(
            observations);
    const auto repeated =
        iteration::evaluate_centered_tanh_rootwise_oracle(
            observations);

    expect(first == repeated,
           "rootwise oracle must be bit-deterministic");
    expect(first.observation_count == 1,
           "rootwise oracle observation count");
    expect_near(first.total_weight, 2.0, 0.0,
                "rootwise oracle total weight");
    expect(first.reduction_defined,
           "nonstationary oracle target defines reduction");
    expect(first.parent_kl > 0.0,
           "feasible oracle target differs from parent");
    expect(first.full_range.numerical_best_kl < 1.0e-12,
           "full-range oracle must recover feasible target");
    expect(
        first.zero_saturation.numerical_best_kl < 1.0e-12,
        "zero-saturation oracle must recover unsaturated target");
    expect(
        first.full_range.certified_kl_lower_bound <=
            first.full_range.numerical_best_kl + 1.0e-12,
        "certified full-range KL must lower-bound feasible KL");
    expect(
        first.zero_saturation.certified_kl_lower_bound <=
            first.zero_saturation.numerical_best_kl + 1.0e-12,
        "certified zero-saturation KL must lower-bound feasible KL");
    expect(
        first.zero_saturation
                .maximum_abs_squashed_residual <
            iteration::kP16ResidualSaturationThreshold,
        "zero-saturation solution must remain strictly unsaturated");
    expect(
        first.full_range.achievable_reduction_fraction >
            0.999999,
        "feasible full-range reduction must approach one");
    expect(
        first.zero_saturation.achievable_reduction_fraction >
            0.999999,
        "feasible zero-saturation reduction must approach one");
}

void test_centered_tanh_oracle_brackets_unreachable_tilt() {
    const std::vector<double> base = {0.0, 0.0};
    const auto parent =
        iteration::p16_exploration_distribution(base);
    const auto target =
        iteration::p16_all_action_target(
            parent, 0, iteration::kP16AdvantageLimit);
    const iteration::CenteredTanhOracleObservation tilted = {
        .base_scores = base,
        .target_probabilities = target,
        .weight = 3.0,
    };
    const iteration::CenteredTanhOracleObservation stationary = {
        .base_scores = {0.1, -0.1},
        .target_probabilities =
            iteration::p16_exploration_distribution(
                std::vector<double>{0.1, -0.1}),
        .weight = 1.0,
    };
    const std::array observations = {tilted, stationary};
    const auto metrics =
        iteration::evaluate_centered_tanh_rootwise_oracle(
            observations);

    expect(metrics.observation_count == 2,
           "aggregate oracle observation count");
    expect_near(metrics.total_weight, 4.0, 0.0,
                "aggregate oracle weight");
    expect(metrics.parent_kl > 0.0,
           "maximum tilt must differ from parent");
    expect(
        metrics.full_range.numerical_best_kl > 1.0e-5,
        "bounded residual plus exploration floor cannot fit max tilt");
    expect(
        metrics.full_range.numerical_best_kl <
            metrics.parent_kl,
        "full-range oracle must improve the max tilt");
    expect(
        metrics.zero_saturation.numerical_best_kl >
            metrics.full_range.numerical_best_kl,
        "zero-saturation constraint must tighten the max-tilt fit");
    const auto full_boundary =
        iteration::p16_exploration_distribution(
            std::vector<double>{
                0.10 * std::tanh(12.0),
                -0.10 * std::tanh(12.0),
            });
    const auto zero_saturation_boundary =
        iteration::p16_exploration_distribution(
            std::vector<double>{0.095, -0.095});
    expect_near(
        metrics.full_range.numerical_best_kl,
        0.75 * test_distribution_kl(
                   target, full_boundary),
        1.0e-12,
        "binary full-range oracle reaches its exact box boundary");
    expect_near(
        metrics.zero_saturation.numerical_best_kl,
        0.75 * test_distribution_kl(
                   target, zero_saturation_boundary),
        1.0e-12,
        "binary zero-saturation oracle reaches its strict boundary");
    expect(
        metrics.full_range.certified_kl_lower_bound <=
            metrics.full_range.numerical_best_kl + 1.0e-10,
        "full-range certificate must bracket numerical fit");
    expect(
        metrics.zero_saturation.certified_kl_lower_bound <=
            metrics.zero_saturation.numerical_best_kl +
                1.0e-10,
        "zero-saturation certificate must bracket numerical fit");
    expect(
        metrics.full_range.achievable_reduction_fraction <=
            metrics.full_range
                .certified_reduction_upper_bound +
                1.0e-10,
        "full-range reduction bracket must be ordered");
    expect(
        metrics.zero_saturation
                .achievable_reduction_fraction <=
            metrics.zero_saturation
                    .certified_reduction_upper_bound +
                1.0e-10,
        "zero-saturation reduction bracket must be ordered");
    expect(
        metrics.zero_saturation
                .maximum_abs_squashed_residual <
            iteration::kP16ResidualSaturationThreshold,
        "constrained max-tilt solution must not saturate");
}

void test_centered_tanh_oracle_validates_inputs() {
    const iteration::CenteredTanhOracleObservation valid = {
        .base_scores = {0.0, 0.0},
        .target_probabilities = {0.5, 0.5},
        .weight = 1.0,
    };
    const std::array valid_observations = {valid};
    const std::vector<
        iteration::CenteredTanhOracleObservation>
        empty;
    expect(
        iteration::evaluate_centered_tanh_rootwise_oracle(
            empty) ==
            iteration::CenteredTanhRootwiseOracleMetrics{},
        "empty oracle corpus must return zero metrics");

    expect_invalid(
        [&] {
            auto malformed = valid;
            malformed.base_scores.pop_back();
            const std::array observations = {malformed};
            static_cast<void>(
                iteration::
                    evaluate_centered_tanh_rootwise_oracle(
                        observations));
        },
        "oracle must reject mismatched vectors");
    expect_invalid(
        [&] {
            auto malformed = valid;
            malformed.target_probabilities = {0.4, 0.4};
            const std::array observations = {malformed};
            static_cast<void>(
                iteration::
                    evaluate_centered_tanh_rootwise_oracle(
                        observations));
        },
        "oracle must reject unnormalized targets");
    expect_invalid(
        [&] {
            auto malformed = valid;
            malformed.weight = 0.0;
            const std::array observations = {malformed};
            static_cast<void>(
                iteration::
                    evaluate_centered_tanh_rootwise_oracle(
                        observations));
        },
        "oracle must reject zero weight");
    expect_invalid(
        [&] {
            auto config =
                iteration::CenteredTanhOracleConfig{};
            config.policy_mixture_weight = 1.0;
            static_cast<void>(
                iteration::
                    evaluate_centered_tanh_rootwise_oracle(
                        valid_observations, config));
        },
        "oracle must reject a mixture without exploration floor");
    expect_invalid(
        [&] {
            auto config =
                iteration::CenteredTanhOracleConfig{};
            config.saturation_threshold =
                std::numeric_limits<double>::quiet_NaN();
            static_cast<void>(
                iteration::
                    evaluate_centered_tanh_rootwise_oracle(
                        valid_observations, config));
        },
        "oracle must reject non-finite saturation threshold");
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
    const auto generic_targets =
        iteration::n_state_bootstrap_targets(
            parent_values, 0.25, 4);
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
    expect(
        generic_targets == targets,
        "generic distance four must preserve the canonical targets");

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
}

void test_eight_state_bootstrap_indexing_and_tail_are_exact() {
    const std::vector<double> parent_values = {
        0.01, 0.02, 0.03, 0.04, 0.05,
        0.06, 0.07, 0.08, 0.09, 0.10,
        0.11, 0.12,
    };
    const auto targets =
        iteration::n_state_bootstrap_targets(
            parent_values, 0.40, 8);
    const std::vector<double> expected = {
        0.245, 0.250, 0.255, 0.260,
        0.400, 0.400, 0.400, 0.400,
        0.400, 0.400, 0.400, 0.400,
    };
    expect(targets.size() == expected.size(),
           "eight-state target count must match state count");
    for (std::size_t index = 0; index < targets.size();
         ++index) {
        expect_near(
            targets[index], expected[index], 1.0e-12,
            "eight-state bootstrap target");
    }
}

void test_n_state_bootstrap_validates_inputs() {
    const std::vector<double> parent_values = {
        0.10, 0.20, 0.30,
    };
    expect_invalid(
        [&] {
            iteration::n_state_bootstrap_targets(
                parent_values, 0.5, 0);
        },
        "zero bootstrap distance must be rejected");
    expect_invalid(
        [&] {
            iteration::n_state_bootstrap_targets(
                parent_values, 1.01, 4);
        },
        "invalid bootstrap terminal value must be rejected");
    expect_invalid(
        [] {
            const std::vector<double> invalid = {
                0.2,
                std::numeric_limits<double>::infinity(),
            };
            iteration::n_state_bootstrap_targets(
                invalid, 0.5, 4);
        },
        "invalid parent bootstrap value must be rejected");
}

void test_n_state_bootstrap_large_distance_cannot_overflow() {
    const std::vector<double> parent_values = {
        0.10, 0.20, 0.30,
    };
    const auto targets =
        iteration::n_state_bootstrap_targets(
            parent_values, 0.75,
            std::numeric_limits<std::size_t>::max());
    expect(
        targets ==
            std::vector<double>({0.75, 0.75, 0.75}),
        "a distance larger than the trace must leave terminal targets");
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
        "exact evenly spaced retained roots",
        test_evenly_spaced_retention_is_exact_and_validated);
    runner.run(
        "P16 stable exploration distribution",
        test_p16_exploration_distribution_is_stable_and_validated);
    runner.run(
        "P16 blended outcome signal and clamped advantage",
        test_p16_outcome_signal_blends_td_and_terminal_and_clamps);
    runner.run(
        "P16 advantage-tilted all-action target",
        test_p16_all_action_target_tilts_only_the_chosen_action);
    runner.run(
        "P16 generic mechanism metrics",
        test_p16_mechanism_metrics_cover_ties_signs_and_saturation);
    runner.run(
        "P16 zero-KL and movement threshold",
        test_p16_mechanism_zero_kl_and_movement_threshold);
    runner.run(
        "P16 malformed mechanism observations",
        test_p16_mechanism_metrics_reject_malformed_observations);
    runner.run(
        "centered-tanh oracle feasible-root recovery",
        test_centered_tanh_oracle_recovers_feasible_root);
    runner.run(
        "centered-tanh oracle unreachable-target bracket",
        test_centered_tanh_oracle_brackets_unreachable_tilt);
    runner.run(
        "centered-tanh oracle input validation",
        test_centered_tanh_oracle_validates_inputs);
    runner.run(
        "exact four-state Value bootstrap",
        test_four_state_bootstrap_is_exact_and_terminal_at_tail);
    runner.run(
        "exact eight-state Value bootstrap",
        test_eight_state_bootstrap_indexing_and_tail_are_exact);
    runner.run(
        "n-state Value bootstrap input validation",
        test_n_state_bootstrap_validates_inputs);
    runner.run(
        "n-state Value bootstrap overflow safety",
        test_n_state_bootstrap_large_distance_cannot_overflow);
    runner.run(
        "exact Value G8 Late-Mix50 assignment",
        test_value_g8_mix50_assignment_is_exact_and_rng_free);
    runner.run(
        "immutable three-generation replay window",
        test_replay_window_evicts_without_consuming);
    return runner.finish();
}
