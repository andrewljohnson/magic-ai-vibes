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

struct BridgeConfig {
    DeckId human_deck = DeckId::UWR;
    DeckId opponent_deck = DeckId::UWR;
    std::optional<std::vector<CardId>> human_deck_cards;
    std::optional<std::vector<CardId>> opponent_deck_cards;
    BotKind opponent_bot = BotKind::Handcrafted;
    std::uint64_t game_seed = 42;
    std::size_t monte_carlo_rollouts = 2;
    std::size_t deep_monte_carlo_rollouts = 8;
    bool reveal_opponent_hand = false;
    bool bluff_mode = false;
    // Self-Play Zero opponent: a controller-driven bot loaded from a text
    // artifact. When set, `opponent_bot` is ignored for decisions.
    bool opponent_spz = false;
    std::string spz_artifact_path;
    std::size_t spz_worlds = 4;
    bool spz_rollout = true;

    bool operator==(const BridgeConfig&) const = default;
};

enum class EvolutionPilot : std::uint8_t {
    Random,
    MonteCarlo,
    DeepMonteCarlo,
    Handcrafted,
    SelfPlayZero,
};

struct EvolutionJsonConfig {
    std::size_t generations = 3;
    std::size_t population = 9;
    std::size_t games_per_opponent = 1;
    std::uint64_t seed = 42;
    EvolutionPilot pilot = EvolutionPilot::Handcrafted;
    std::size_t monte_carlo_rollouts = 2;
    std::size_t deep_monte_carlo_rollouts = 8;
    // Self-Play Zero pilot: net artifact and a fast myopic policy (no
    // rollout lookahead) so evolution fitness stays cheap.
    std::string spz_artifact_path;

    bool operator==(const EvolutionJsonConfig&) const = default;
};

DeckId parse_deck_id(std::string_view value);
BotKind parse_opponent_bot(std::string_view value);
EvolutionPilot parse_evolution_pilot(std::string_view value);

// Parses the bridge's transport-only representation of an exact custom deck.
// The accepted form is exactly 40 comma-separated decimal CardId values.
std::vector<CardId> parse_exact_deck_cards(std::string_view value);

// Pure translation from the web policy configuration into the engine policy.
BotConfig make_opponent_bot_config(const BridgeConfig& config);

// Pure serialization seam used by focused tests. The manifest is sorted by
// numeric CardId and includes both the engine card name and exact count.
void write_evolution_json(std::ostream& output,
                          const DeckEvolutionSummary& summary,
                          const EvolutionJsonConfig& config);

// Runs the engine-owned evolution loop once and writes exactly one JSON value.
int run_evolution_json(std::ostream& output,
                       const EvolutionJsonConfig& config);

// Runs one line-delimited JSON session. Every human decision is emitted with
// the exact legal options supplied by Game. The corresponding response is
// read from `input` before the engine resumes.
int run_bridge_session(std::istream& input, std::ostream& output,
                       const BridgeConfig& config);

} // namespace old_school::web
