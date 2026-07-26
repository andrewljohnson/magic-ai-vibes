#pragma once

#include "old_school/game.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace old_school::web {

struct BridgeConfig {
    DeckId human_deck = DeckId::RUAggro;
    DeckId opponent_deck = DeckId::RUAggro;
    BotKind opponent_bot = BotKind::Learned;
    LearnedVariant learned_variant =
        LearnedVariant::ValueSearchChampion;
    std::uint64_t game_seed = 42;
    std::size_t monte_carlo_rollouts = 2;
    std::size_t deep_monte_carlo_rollouts = 8;
    std::size_t learned_rollouts = 2;
    std::size_t training_games = 100;
    std::uint64_t training_seed = kDefaultLearnedTrainingSeed;
    bool reveal_opponent_hand = false;
    bool bluff_mode = false;

    bool operator==(const BridgeConfig&) const = default;
};

DeckId parse_deck_id(std::string_view value);
BotKind parse_opponent_bot(std::string_view value,
                           LearnedVariant& learned_variant);

// Runs one line-delimited JSON session. Every human decision is emitted with
// the exact legal options supplied by Game. The corresponding response is
// read from `input` before the engine resumes.
int run_bridge_session(std::istream& input, std::ostream& output,
                       const BridgeConfig& config);

} // namespace old_school::web
