#pragma once

#include "old_school/game.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace old_school::fq0_sequence_projection {

// Diagnostic projection for rules-irrelevant public graveyard sequence.
// These functions preserve the frozen FQ0 serializers: they canonicalize
// only both players' graveyards in a copied state, delegate to the matching
// hidden-information-safe FQ0 API, and domain-separate its exact digest.
std::string graveyard_quotient_information_set_sha256(
    const GameState& state,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> ordered_actions);

std::string graveyard_quotient_leaf_consequence_sha256(
    const GameState& state, std::size_t observer,
    const LearnedDecisionContext& context,
    const std::optional<GameResult>& terminal_result =
        std::nullopt);

std::string graveyard_quotient_priority_consequence_sha256(
    const GameState& state, std::size_t observer,
    const LearnedDecisionContext& context,
    const PriorityAction& action);

} // namespace old_school::fq0_sequence_projection
