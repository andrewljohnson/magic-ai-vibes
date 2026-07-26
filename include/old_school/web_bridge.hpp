#pragma once

#include "old_school/game.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

namespace old_school::web {

inline constexpr std::size_t kFrozenWebC16TrainingGames = 800;
inline constexpr std::uint64_t kFrozenWebC16TrainingSeed = 424242;
inline constexpr std::size_t kFrozenWebC16Generations = 16;
inline constexpr std::string_view kFrozenWebC16Fingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

struct BridgeConfig {
    DeckId human_deck = DeckId::RUAggro;
    DeckId opponent_deck = DeckId::RUAggro;
    BotKind opponent_bot = BotKind::Learned;
    LearnedVariant learned_variant =
        LearnedVariant::ValueSearchChampion;
    std::uint64_t game_seed = 42;
    std::size_t monte_carlo_rollouts = 2;
    std::size_t deep_monte_carlo_rollouts = 8;
    std::size_t learned_rollouts = 8;
    std::size_t learned_generations = kFrozenWebC16Generations;
    std::size_t training_games = kFrozenWebC16TrainingGames;
    std::uint64_t training_seed = kDefaultLearnedTrainingSeed;
    std::string frozen_c16_artifact_path;
    bool reveal_opponent_hand = false;
    bool bluff_mode = false;

    bool operator==(const BridgeConfig&) const = default;
};

DeckId parse_deck_id(std::string_view value);
BotKind parse_opponent_bot(std::string_view value,
                           LearnedVariant& learned_variant,
                           std::size_t& learned_generations);

// Load-only boundary for the web's immutable research baseline. This always
// validates canonical T800/S424242/C16 artifact metadata and the exact frozen
// model fingerprint; it never trains, refreshes, or substitutes a model.
std::shared_ptr<const LearnedModel>
load_frozen_learned_value_c16(const std::string& path);

// Runs one line-delimited JSON session. Every human decision is emitted with
// the exact legal options supplied by Game. The corresponding response is
// read from `input` before the engine resumes.
int run_bridge_session(std::istream& input, std::ostream& output,
                       const BridgeConfig& config);

} // namespace old_school::web
