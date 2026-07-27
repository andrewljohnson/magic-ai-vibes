#pragma once

#include "old_school/game.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace old_school::fq0_information_set {

// Exact owner-observable identity of a multi-action Priority boundary.
// `observation.revealed_opponent_hand` is always cleared by the constructor:
// debug-only opponent-hand disclosure is not part of an information set.
// The legal actions intentionally retain their engine-provided order.
class InformationSetKey {
  public:
    InformationSetKey(const InformationSetKey&) = default;
    InformationSetKey(InformationSetKey&&) noexcept = default;

    const PlayerObservation& observation() const {
        return observation_;
    }
    const LearnedDecisionContext& context() const {
        return context_;
    }
    const std::vector<PriorityAction>& ordered_actions() const {
        return ordered_actions_;
    }

    bool operator==(const InformationSetKey&) const = default;

  private:
    InformationSetKey(
        PlayerObservation observation,
        LearnedDecisionContext context,
        std::vector<PriorityAction> ordered_actions)
        : observation_(std::move(observation)),
          context_(context),
          ordered_actions_(std::move(ordered_actions)) {}

    PlayerObservation observation_;
    LearnedDecisionContext context_;
    std::vector<PriorityAction> ordered_actions_;

    friend InformationSetKey make_information_set_key(
        const GameState& state,
        const LearnedDecisionContext& context,
        std::span<const PriorityAction> ordered_actions);
    friend std::string information_set_sha256(
        const InformationSetKey& key);
};

InformationSetKey make_information_set_key(
    const GameState& state,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> ordered_actions);

// SHA-256 over a versioned, length-delimited serialization of every field in
// InformationSetKey. Opponent hand identities and both libraries' identities
// and order cannot enter because PlayerObservation contains only their sizes.
std::string information_set_sha256(const InformationSetKey& key);

struct CanonicalActionRow {
    std::string descriptor;
    PriorityAction action;

    bool operator==(const CanonicalActionRow&) const = default;
};

// Produces descriptor-sorted typed actions using the repository's stable
// rules descriptor. Empty inputs and duplicate descriptors fail closed.
std::vector<CanonicalActionRow>
descriptor_canonical_action_rows(
    const InformationSetKey& key);

// Redacted evidence hash for a critic or terminal leaf. It binds the complete
// owner observation, decision context, and (when present) terminal outcome,
// but never any hand/library identity hidden from `observer`.
std::string redacted_leaf_consequence_sha256(
    const GameState& state, std::size_t observer,
    const LearnedDecisionContext& context,
    const std::optional<GameResult>& terminal_result =
        std::nullopt);

enum class SeedDomain : std::uint8_t {
    RootDeterminization,
    RootMacroTransition,
    SuccessorSelectionDeterminization,
    SuccessorSelectionMacroTransition,
    SuccessorEvaluationDeterminization,
    SuccessorEvaluationMacroTransition,
    InvarianceCheck,
};

enum class SeedBank : std::uint8_t {
    Root,
    A,
    B,
};

// Candidate identity is deliberately absent. Thus every candidate sharing
// these coordinates receives common worlds and common initial randomness.
struct IndexedSeedCoordinates {
    SeedDomain domain = SeedDomain::RootDeterminization;
    std::string scope;
    std::string group;
    SeedBank bank = SeedBank::Root;
    std::size_t block = 0;
    std::size_t world = 0;

    bool operator==(const IndexedSeedCoordinates&) const = default;
};

std::uint64_t derive_indexed_seed(
    std::uint64_t base_seed,
    const IndexedSeedCoordinates& coordinates);

// Exact undiscounted terminal value from the root owner's perspective.
double terminal_root_owner_value(
    const GameResult& result, std::size_t root_owner);

struct LegacyLeafCriticEvaluation {
    double value = 0.0;
    std::uint64_t contextual_bits = 0;
    std::uint64_t legacy_bits = 0;
    bool legacy_bit_identity = false;

    bool operator==(
        const LegacyLeafCriticEvaluation&) const = default;
};

// Calls the contextual critic first, then independently calls the legacy
// state-only critic. The model must be LegacyStateOnly and any IEEE-754 bit
// disagreement is an infrastructure error.
LegacyLeafCriticEvaluation evaluate_legacy_leaf_critic(
    const GameState& state, std::size_t perspective,
    const LearnedDecisionContext& context,
    std::shared_ptr<const LearnedModel> model);

} // namespace old_school::fq0_information_set
