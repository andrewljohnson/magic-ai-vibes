#include "alpha/learned_iteration.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace alpha::learned_iteration {
namespace {

std::uint64_t mix_seed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value =
        (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value =
        (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

bool valid_probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

} // namespace

std::uint64_t derive_seed(
    std::uint64_t root_seed, SeedDomain domain,
    std::uint64_t generation, std::uint64_t index,
    std::uint64_t subindex) noexcept {
    std::uint64_t seed = mix_seed(
        root_seed ^
        mix_seed(static_cast<std::uint64_t>(domain)));
    seed = mix_seed(
        seed ^ mix_seed(
                   generation ^ 0xd1b54a32d192ed03ULL));
    seed = mix_seed(
        seed ^ mix_seed(index ^ 0x94d049bb133111ebULL));
    return mix_seed(
        seed ^ mix_seed(
                   subindex ^ 0xbf58476d1ce4e5b9ULL));
}

std::array<ScheduledGame, kBalancedScheduleGames>
balanced_schedule(std::uint64_t root_seed,
                  std::uint64_t generation,
                  std::uint64_t block_index) {
    constexpr std::array<std::pair<DeckId, DeckId>,
                         kBalancedPairings>
        pairings = {{
            {DeckId::Green, DeckId::Red},
            {DeckId::Green, DeckId::Blue},
            {DeckId::Green, DeckId::White},
            {DeckId::Red, DeckId::Blue},
            {DeckId::Red, DeckId::White},
            {DeckId::Blue, DeckId::White},
        }};

    std::array<ScheduledGame, kBalancedScheduleGames> games;
    std::size_t schedule_index = 0;
    for (std::size_t pairing_index = 0;
         pairing_index < pairings.size(); ++pairing_index) {
        const auto [first, second] = pairings[pairing_index];
        for (std::size_t orientation = 0;
             orientation < 2; ++orientation) {
            const std::array<DeckId, 2> seats =
                orientation == 0
                    ? std::array<DeckId, 2>{first, second}
                    : std::array<DeckId, 2>{second, first};
            for (std::size_t starting_player = 0;
                 starting_player < 2; ++starting_player) {
                games[schedule_index] = {
                    .schedule_index = schedule_index,
                    .pairing_index = pairing_index,
                    .seat_decks = seats,
                    .starting_player = starting_player,
                    .seed = derive_seed(
                        root_seed, SeedDomain::SelfPlayGame,
                        generation, block_index,
                        schedule_index),
                };
                ++schedule_index;
            }
        }
    }
    return games;
}

double terminal_value_for_perspective(
    int winner, std::size_t perspective) {
    if (perspective >= 2) {
        throw std::invalid_argument(
            "terminal perspective must be 0 or 1");
    }
    if (winner == -1) {
        return 0.5;
    }
    if (winner != 0 && winner != 1) {
        throw std::invalid_argument(
            "terminal winner must be -1, 0, or 1");
    }
    return winner == static_cast<int>(perspective)
               ? 1.0
               : 0.0;
}

std::vector<double> td_lambda_targets(
    std::span<const double> chronological_values,
    double terminal_z, double lambda) {
    if (!valid_probability(terminal_z)) {
        throw std::invalid_argument(
            "terminal value must be a probability");
    }
    if (!std::isfinite(lambda) ||
        lambda < 0.0 || lambda > 1.0) {
        throw std::invalid_argument(
            "TD lambda must be in [0, 1]");
    }
    for (const double value : chronological_values) {
        if (!valid_probability(value)) {
            throw std::invalid_argument(
                "critic values must be probabilities");
        }
    }

    std::vector<double> targets(
        chronological_values.size());
    if (targets.empty()) {
        return targets;
    }
    targets.back() = terminal_z;
    for (std::size_t index = targets.size() - 1;
         index > 0; --index) {
        targets[index - 1] =
            (1.0 - lambda) * chronological_values[index] +
            lambda * targets[index];
    }
    return targets;
}

} // namespace alpha::learned_iteration
