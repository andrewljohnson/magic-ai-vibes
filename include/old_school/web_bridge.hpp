#pragma once

#include "old_school/game.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::web {

inline constexpr std::size_t kFrozenWebC16TrainingGames = 800;
inline constexpr std::uint64_t kFrozenWebC16TrainingSeed = 424242;
inline constexpr std::size_t kFrozenWebC16Generations = 16;
inline constexpr std::size_t kFrozenWebC16SearchWorlds = 8;
inline constexpr std::string_view kFrozenWebC16Fingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

struct BridgeConfig {
    DeckId human_deck = DeckId::RUAggro;
    DeckId opponent_deck = DeckId::RUAggro;
    std::optional<std::vector<CardId>> human_deck_cards;
    std::optional<std::vector<CardId>> opponent_deck_cards;
    BotKind opponent_bot = BotKind::Learned;
    LearnedVariant learned_variant =
        LearnedVariant::ValueSearchChampion;
    std::uint64_t game_seed = 42;
    std::size_t monte_carlo_rollouts = 2;
    std::size_t deep_monte_carlo_rollouts = 8;
    std::size_t learned_rollouts = kFrozenWebC16SearchWorlds;
    std::size_t learned_generations = kFrozenWebC16Generations;
    std::size_t training_games = kFrozenWebC16TrainingGames;
    std::uint64_t training_seed = kDefaultLearnedTrainingSeed;
    std::string frozen_c16_artifact_path;
    std::string aq19_bilinear_artifact_path;
    bool reveal_opponent_hand = false;
    bool bluff_mode = false;
    bool value_adversarial_blocks = false;
    bool value_pass_dominance = false;
    bool value_actor_local_search = false;
    bool value_priority_bilinear = false;

    bool operator==(const BridgeConfig&) const = default;
};

enum class EvolutionPilot : std::uint8_t {
    Handcrafted,
    LearnedValueC16,
};

struct EvolutionJsonConfig {
    std::size_t generations = 3;
    std::size_t population = 8;
    std::size_t games_per_opponent = 1;
    std::uint64_t seed = 42;
    EvolutionPilot pilot = EvolutionPilot::Handcrafted;
    std::size_t learned_rollouts = 8;
    std::string frozen_c16_artifact_path;

    bool operator==(const EvolutionJsonConfig&) const = default;
};

DeckId parse_deck_id(std::string_view value);
BotKind parse_opponent_bot(std::string_view value,
                           LearnedVariant& learned_variant,
                           std::size_t& learned_generations,
                           bool& value_adversarial_blocks,
                           bool& value_pass_dominance,
                           bool& value_actor_local_search,
                           bool& value_priority_bilinear);
EvolutionPilot parse_evolution_pilot(std::string_view value);

// Parses the bridge's transport-only representation of an exact custom deck.
// The accepted form is exactly 40 comma-separated decimal CardId values.
std::vector<CardId> parse_exact_deck_cards(std::string_view value);

// Load-only boundary for the web's immutable research baseline. This always
// validates canonical T800/S424242/C16 artifact metadata and the exact frozen
// model fingerprint; it never trains, refreshes, or substitutes a model.
std::shared_ptr<const LearnedModel>
load_frozen_learned_value_c16(const std::string& path);

struct FrozenAq19Bilinear {
    std::shared_ptr<const LearnedPriorityBilinear> residual;
    std::uintmax_t artifact_bytes = 0;
    std::string artifact_file_sha256;
    std::string parameter_sha256;
    std::string parent_fingerprint;
};

FrozenAq19Bilinear
load_frozen_aq19_bilinear(const std::string& path);

// Pure translation from the web policy configuration into the engine policy.
// The web's frozen C16 research pilots differ only through the explicit
// default-off policy-treatment flags.
BotConfig make_opponent_bot_config(
    const BridgeConfig& config,
    std::shared_ptr<const LearnedModel> learned_model,
    std::shared_ptr<const LearnedPriorityBilinear>
        priority_bilinear = nullptr);

// Pure serialization seam used by focused tests. The manifest is sorted by
// numeric CardId and includes both the engine card name and exact count.
void write_evolution_json(std::ostream& output,
                          const DeckEvolutionSummary& summary,
                          const EvolutionJsonConfig& config);

// Runs the engine-owned evolution loop once and writes exactly one JSON value.
// Learned Value C16 is load-only through the frozen artifact boundary above.
int run_evolution_json(std::ostream& output,
                       const EvolutionJsonConfig& config);

// Runs one line-delimited JSON session. Every human decision is emitted with
// the exact legal options supplied by Game. The corresponding response is
// read from `input` before the engine resumes.
int run_bridge_session(std::istream& input, std::ostream& output,
                       const BridgeConfig& config);

} // namespace old_school::web
