#pragma once

#include "old_school/action_q_bellman_teacher.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace old_school::action_q_multiscale_teacher {

// AQ2-MS0 has one fixed blend. These are experiment constants rather than
// runtime knobs.
inline constexpr double kBellmanWeight = 0.75;
inline constexpr double kResolvedWeight = 0.25;

// Scalar-only accounting for the four immediate-consequence samples of one
// root action. Terminal and critic evaluations form an exact partition.
struct ResolvedActionAccounting {
    std::size_t evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t critic_evaluations = 0;
    std::size_t window_ended_evaluations = 0;
    std::size_t priority_passes = 0;
    std::size_t stack_resolutions = 0;

    bool operator==(
        const ResolvedActionAccounting&) const = default;
};

// The safe coordinate and scalar outcome of one deterministic settlement.
// `source_world` is copied from AQ1 and therefore binds the exact reused
// determinization seed; AQ2 does not consume its macro seed.
struct ResolvedSample {
    action_q_bellman_teacher::WorldSeeds source_world;
    double value = 0.0;
    bool terminal = false;
    bool critic_leaf = false;
    bool window_ended = false;
    std::size_t priority_passes = 0;
    std::size_t stack_resolutions = 0;

    bool operator==(const ResolvedSample&) const = default;
};

// One descriptor-canonical AQ2 target. `resolved_samples` is indexed by the
// same four root worlds retained in `RootTargets::bellman.root_worlds`.
// Neither this row nor any other AQ2 result retains a sampled GameState.
struct ActionTarget {
    std::string descriptor;
    PriorityAction action;
    double bellman_value = 0.0;
    std::vector<ResolvedSample> resolved_samples;
    double resolved_value = 0.0;
    double value = 0.0;
    ResolvedActionAccounting resolved_accounting;

    bool operator==(const ActionTarget&) const = default;
};

struct ResolvedAccounting {
    std::size_t root_actions = 0;
    // These are shared across actions, not independently resampled.
    std::size_t root_determinizations = 0;
    std::size_t evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t critic_evaluations = 0;
    std::size_t window_ended_evaluations = 0;
    std::size_t priority_passes = 0;
    std::size_t stack_resolutions = 0;

    bool operator==(const ResolvedAccounting&) const = default;
};

// AQ1's retained result is itself scalar/coordinate-only and contains the
// complete proof that the temporal target used the fixed K4/K4 recipe. AQ2
// adds only resolved scalar samples and their fixed convex combination.
struct RootTargets {
    action_q_bellman_teacher::RootTargets bellman;
    std::vector<ActionTarget> actions;
    ResolvedAccounting resolved_accounting;

    bool operator==(const RootTargets&) const = default;
};

// Scores a complete legal Priority root with unchanged AQ1 K4/K4 Bellman
// values. Immediate resolved-consequence values reuse, exactly, the four
// determinization seeds in the returned AQ1 root result.
RootTargets score_priority_root(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> candidates,
    std::shared_ptr<const LearnedModel> legacy_c16,
    std::uint64_t root_seed);

// Rechecks the complete AQ1 result, canonical action alignment, fixed blend,
// scalar probabilities, world indexing, and all resolved accounting sums.
// It requires no sampled state or hidden-zone payload.
void validate_root_targets(const RootTargets& targets);

} // namespace old_school::action_q_multiscale_teacher
