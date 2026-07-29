#pragma once

#include "old_school/fq0_bellman.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace old_school::action_q_bellman_teacher {

// AQ1-BL0 has one fixed recipe. These counts are intentionally not exposed
// as runtime knobs: changing either count creates a different experiment.
inline constexpr std::size_t kRootWorlds = 4;
inline constexpr std::size_t kSuccessorWorlds = 4;

// Complete accounting for one collection of Priority macro-transitions.
// Every transition is either terminal or a subsequent nontrivial Priority
// boundary. Critic leaves are a subset of boundary transitions and are zero
// for the first (root-action) boundary.
struct MacroAccounting {
    std::size_t transitions = 0;
    std::size_t terminal_transitions = 0;
    std::size_t boundary_transitions = 0;
    std::size_t critic_leaves = 0;
    std::size_t actions_applied = 0;
    std::size_t priority_actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;

    bool operator==(const MacroAccounting&) const = default;
};

// The coordinates are safe to retain: they identify a sampled world but do
// not contain the sampled GameState or any hidden-zone card identity.
struct WorldSeeds {
    std::size_t world_index = 0;
    std::uint64_t determinization_seed = 0;
    std::uint64_t macro_seed = 0;

    bool operator==(const WorldSeeds&) const = default;
};

struct SuccessorBank {
    std::string name;
    std::string stream_key;
    std::vector<WorldSeeds> worlds;
    // Descriptor-canonical scalar samples only. No state or hidden payload is
    // retained in an ActionSamples row.
    std::vector<fq0_bellman::ActionSamples> actions;
    MacroAccounting accounting;

    bool operator==(const SuccessorBank&) const = default;
};

// One unique successor-owner information set. Multiple root actions or root
// particles may refer to this evaluation without evaluating it again.
struct SuccessorEvaluation {
    std::string information_set_fingerprint;
    std::size_t successor_owner = 0;
    std::vector<PriorityAction> actions;
    SuccessorBank bank_a;
    SuccessorBank bank_b;
    fq0_bellman::CrossFitValue cross_fit;

    bool operator==(const SuccessorEvaluation&) const = default;
};

// Membership is retained explicitly so the four root particles can be
// proven to form a disjoint, complete partition before backup.
struct SuccessorParticleGroup {
    std::string information_set_fingerprint;
    std::size_t successor_owner = 0;
    fq0_bellman::OwnerRelation relation =
        fq0_bellman::OwnerRelation::SameOwner;
    std::vector<std::size_t> root_world_indices;
    double successor_owner_value = 0.0;

    bool operator==(const SuccessorParticleGroup&) const = default;
};

struct RootActionTarget {
    std::string descriptor;
    PriorityAction action;
    double value = 0.0;
    std::vector<fq0_bellman::TerminalParticle> terminal_particles;
    std::vector<SuccessorParticleGroup> successor_groups;
    MacroAccounting root_accounting;

    bool operator==(const RootActionTarget&) const = default;
};

struct TeacherAccounting {
    std::size_t root_actions = 0;
    std::size_t root_determinizations = 0;
    std::size_t root_terminal_particles = 0;
    std::size_t root_boundary_particles = 0;
    std::size_t successor_group_occurrences = 0;
    std::size_t same_owner_group_occurrences = 0;
    std::size_t opponent_owner_group_occurrences = 0;
    std::size_t same_owner_root_particles = 0;
    std::size_t opponent_owner_root_particles = 0;
    std::size_t unique_successor_information_sets = 0;
    std::size_t successor_actions = 0;
    std::size_t successor_determinizations = 0;
    MacroAccounting root_macros;
    MacroAccounting successor_macros;

    bool operator==(const TeacherAccounting&) const = default;
};

// The complete public result of a Bellman-lite root. It deliberately contains
// no GameState, PlayerState, observation hand payload, or sampled hidden zone.
struct RootTargets {
    std::uint64_t root_seed = 0;
    std::string root_information_set_fingerprint;
    std::size_t root_owner = 0;
    std::vector<WorldSeeds> root_worlds;
    std::vector<RootActionTarget> actions;
    std::vector<SuccessorEvaluation> successor_evaluations;
    TeacherAccounting accounting;

    bool operator==(const RootTargets&) const = default;
};

// Scores a complete legal Priority root with the fixed AQ1-BL0 recipe.
// `candidates` may be in any order but must be an exact permutation of the
// engine-authoritative legal set. Candidate and action identity never enter a
// seed; actions share root worlds and successor actions share each bank.
RootTargets score_priority_root(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> candidates,
    std::shared_ptr<const LearnedModel> legacy_c16,
    std::uint64_t root_seed);

// Rechecks every dimension, sample bank, particle partition, perspective
// relation, cross-fit value, and accounting cross-sum in a retained result.
// This validator needs no hidden state.
void validate_root_targets(const RootTargets& targets);

} // namespace old_school::action_q_bellman_teacher
