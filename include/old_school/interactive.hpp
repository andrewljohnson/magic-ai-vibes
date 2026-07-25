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

// Runs one Human RU Aggro versus Learned Value RU Aggro game. All terminal
// rendering is produced from PlayerObservation, so the opponent's hand and
// both library orders are unavailable to the interactive adapter.
InteractiveMatchResult run_interactive_match(
    std::istream& input, std::ostream& output, std::uint64_t seed,
    std::shared_ptr<const LearnedModel> learned_model);

} // namespace old_school
