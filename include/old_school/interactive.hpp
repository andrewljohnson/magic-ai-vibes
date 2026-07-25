#pragma once

#include "old_school/game.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>

namespace old_school {

struct InteractiveMatchResult {
    bool abandoned = false;
    std::optional<GameResult> game;
};

struct InteractiveMatchup {
    DeckId human_deck = DeckId::RUAggro;
    DeckId learned_deck = DeckId::RUAggro;

    bool operator==(const InteractiveMatchup&) const = default;
};

// Selects a reproducible ordered non-mirror matchup uniformly from the
// five-deck field. The selection uses a stream independent from the game's
// shuffle stream.
InteractiveMatchup choose_interactive_matchup(std::uint64_t seed);

// Runs one Human versus Learned Value game with the selected decks. The
// terminal opts into the human-only opponent-hand reveal for behavior
// inspection. The Learned policy still receives no opponent hand identities,
// and neither library order is exposed.
InteractiveMatchResult run_interactive_match(
    std::istream& input, std::ostream& output, std::uint64_t seed,
    std::shared_ptr<const LearnedModel> learned_model,
    InteractiveMatchup matchup);

} // namespace old_school
