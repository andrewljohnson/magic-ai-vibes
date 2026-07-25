#pragma once

#include "alpha/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace alpha::learned_iteration {

// Every stochastic part of an iteration receives a seed derived from its
// semantic domain and stable indices. Adding work in one domain therefore
// cannot shift the random stream used by another domain.
enum class SeedDomain : std::uint64_t {
    SelfPlayGame = 0x53454c46504c4159ULL,
    PrioritySearch = 0x5052494f52495459ULL,
    AttackSearch = 0x41545441434b5352ULL,
    PriorityChoice = 0x5052494f43484345ULL,
    AttackChoice = 0x41544b43484f4943ULL,
    CriticFit = 0x4352495449434649ULL,
    PolicyFit = 0x504f4c4943594649ULL,
    ReplayShuffle = 0x5245504c41595348ULL,
};

std::uint64_t derive_seed(
    std::uint64_t root_seed, SeedDomain domain,
    std::uint64_t generation, std::uint64_t index,
    std::uint64_t subindex = 0) noexcept;

inline constexpr std::size_t kBalancedPairings = 6;
inline constexpr std::size_t kBalancedGamesPerPairing = 4;
inline constexpr std::size_t kBalancedScheduleGames =
    kBalancedPairings * kBalancedGamesPerPairing;

struct ScheduledGame {
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::array<DeckId, 2> seat_decks = {
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t starting_player = 0;
    std::uint64_t seed = 0;

    bool operator==(const ScheduledGame&) const = default;
};

// One block contains each unordered deck pair in both seat orientations and
// with each seat starting once: 6 * 2 * 2 = 24 games.
std::array<ScheduledGame, kBalancedScheduleGames>
balanced_schedule(std::uint64_t root_seed, std::uint64_t generation,
                  std::uint64_t block_index = 0);

// Learned critics are win probabilities: win=1, loss=0, draw=0.5.
double terminal_value_for_perspective(
    int winner, std::size_t perspective);

// `chronological_values[t]` is V(s_t) for one player's chronologically
// ordered decision states. With no intermediate reward, the target for the
// final state is terminal_z and earlier targets use
//   G_t = (1-lambda) * V(s_{t+1}) + lambda * G_{t+1}.
// V(s_0) is intentionally not part of its own target.
std::vector<double> td_lambda_targets(
    std::span<const double> chronological_values,
    double terminal_z, double lambda);

inline constexpr std::size_t kReplayWindowGenerations = 3;

// Generations are immutable after insertion. Snapshots share const storage,
// so repeated ensemble/member training cannot move examples out of the
// replay window, and an evicted generation remains valid in old snapshots.
template <typename Example>
class ReplayWindow {
  public:
    using ImmutableExamples =
        std::shared_ptr<const std::vector<Example>>;

    struct Generation {
        std::uint64_t index = 0;
        ImmutableExamples examples;
    };

    void append_generation(
        std::uint64_t generation,
        const std::vector<Example>& examples) {
        append_storage(
            generation,
            std::make_shared<const std::vector<Example>>(
                examples));
    }

    void append_generation(
        std::uint64_t generation,
        std::vector<Example>&& examples) {
        append_storage(
            generation,
            std::make_shared<const std::vector<Example>>(
                std::move(examples)));
    }

    [[nodiscard]] std::size_t generation_count() const noexcept {
        return generations_.size();
    }

    [[nodiscard]] std::size_t example_count() const noexcept {
        std::size_t count = 0;
        for (const auto& generation : generations_) {
            count += generation.examples->size();
        }
        return count;
    }

    [[nodiscard]] std::vector<Generation> snapshot() const {
        return {generations_.begin(), generations_.end()};
    }

    template <typename Visitor>
    void for_each(Visitor&& visitor) const {
        for (const auto& generation : generations_) {
            for (const auto& example : *generation.examples) {
                std::invoke(
                    visitor, generation.index, example);
            }
        }
    }

  private:
    void append_storage(
        std::uint64_t generation,
        ImmutableExamples examples) {
        if (!generations_.empty() &&
            generation <= generations_.back().index) {
            throw std::invalid_argument(
                "replay generations must be strictly increasing");
        }
        if (generations_.size() ==
            kReplayWindowGenerations) {
            generations_.pop_front();
        }
        generations_.push_back({
            .index = generation,
            .examples = std::move(examples),
        });
    }

    std::deque<Generation> generations_;
};

} // namespace alpha::learned_iteration
