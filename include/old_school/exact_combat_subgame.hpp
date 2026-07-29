#pragma once

#include "old_school/game.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace old_school::exact_combat_subgame {

// Every limit is supplied by the caller. Cardinality limits may be zero when
// that cardinality is intentionally forbidden; enumeration limits must be
// positive because even an empty attack has one complete outcome.
struct Bounds {
    std::size_t maximum_attackers = 0;
    std::size_t maximum_blockers = 0;
    std::size_t maximum_block_assignments = 0;
    std::size_t maximum_damage_orders_per_assignment = 0;
    std::size_t maximum_completed_plans = 0;

    bool operator==(const Bounds&) const = default;
};

struct BlockerOptions {
    PermanentId blocker = 0;
    // Preserves the caller's fixed attacker order.
    std::vector<PermanentId> legal_attackers;

    bool operator==(const BlockerOptions&) const = default;
};

struct CompletedPlan {
    // Stable, contiguous indices in deterministic enumeration order.
    std::size_t block_assignment_index = 0;
    std::size_t damage_order_index = 0;

    // Declaration order follows the defender's battlefield order. The second
    // vector is grouped by fixed attacker order; within each group it is the
    // exact attacker-controlled damage-assignment order passed to
    // resolve_combat().
    std::vector<std::pair<PermanentId, PermanentId>>
        declared_blocks;
    std::vector<std::pair<PermanentId, PermanentId>>
        damage_ordered_blocks;

    GameState resulting_state;

    // A blocker is a pure chump in this completed line when it dies while the
    // attacker it blocked survives. A double block that kills the attacker is
    // therefore not mislabeled as a pure chump.
    std::vector<PermanentId> pure_chump_blockers;

    bool contains_pure_chump() const noexcept {
        return !pure_chump_blockers.empty();
    }

    bool operator==(const CompletedPlan&) const = default;
};

struct Enumeration {
    std::size_t attacking_player = 0;
    std::vector<PermanentId> attackers;
    std::vector<BlockerOptions> blocker_options;
    // This is the exact product of (No Block + legal attackers) for every
    // available blocker, not a post-filtered count.
    std::size_t legal_block_assignments = 0;
    std::vector<CompletedPlan> plans;

    bool operator==(const Enumeration&) const = default;
};

using StateScorer =
    std::function<double(const GameState&, std::size_t perspective)>;

struct AttackerBestResponse {
    std::size_t block_assignment_index = 0;
    std::size_t completed_plan_index = 0;
    double attacker_score = 0.0;
    double defender_score = 0.0;

    bool operator==(const AttackerBestResponse&) const = default;
};

struct Selection {
    // One attacker-max response for every legal block assignment.
    std::vector<AttackerBestResponse> attacker_best_responses;
    // Index into both attacker_best_responses and the block-assignment
    // coordinate. The chosen completed plan is available through that row.
    std::size_t defender_selected_assignment = 0;

    const AttackerBestResponse& selected_response() const;

    bool operator==(const Selection&) const = default;
};

// Enumerates the exact public combat subgame for a fixed attack. A caller may
// pass either the pre-attack untapped state or a Declare Blockers snapshot in
// which the fixed attackers are already tapped; only those declared attackers
// are normalized to untapped immediately before the authoritative rules
// transition. All untapped defending creatures are considered, including
// creatures whose only legal choice is No Block.
//
// Malformed roots, bound exhaustion, arithmetic overflow, and any internal
// disagreement with resolve_combat() throw instead of returning partial
// evidence.
Enumeration enumerate(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const Bounds& bounds);

// The defender first chooses a complete block assignment. For each assignment
// the attacker chooses its damage order by maximizing its own supplied score;
// the defender then maximizes its score over those attacker best responses.
// Exact own-score ties retain canonical enumeration order. In particular, a
// tie never consults the other player's critic because that critic may encode
// private information unavailable to the actor making the choice.
Selection select_defender_max_after_attacker_max(
    const Enumeration& enumeration,
    const StateScorer& scorer);

// Convenience adapter for the public Learned critic. DecisionContextV1 models
// are evaluated at the public End Combat priority boundary; state-only models
// use the unchanged public state critic.
StateScorer make_learned_end_combat_scorer(
    std::shared_ptr<const LearnedModel> model);

} // namespace old_school::exact_combat_subgame
