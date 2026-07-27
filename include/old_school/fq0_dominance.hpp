#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace old_school::probes {
struct DecisionProbe;
}

namespace old_school::fq0_dominance {
class Settlement;
struct CanonicalSettlement;
}

namespace old_school::fq0_dominance_transition {
fq0_dominance::Settlement advance_to_next_first_main(
    const probes::DecisionProbe& probe,
    const GameState& information_set_world,
    std::size_t candidate_index,
    std::string root_information_fingerprint);
}

namespace old_school::fq0_dominance {

enum class SourceKind : std::uint8_t {
    Land,
    Artifact,
};

// `key` identifies a source in the settlement's root state. Lands use their
// root-zone position because the engine does not assign them permanent IDs;
// artifacts use PermanentId. Comparison later collapses permutation-
// equivalent sources to (kind, card) multiplicities.
struct ResourceSource {
    SourceKind kind = SourceKind::Land;
    std::uint64_t key = 0;
    CardId card = CardId::Forest;

    bool operator==(const ResourceSource&) const = default;
    auto operator<=>(const ResourceSource&) const = default;
};

struct PlayerResourceCost {
    std::array<std::size_t, kCardCount> hand_cards_consumed{};
    ManaCost mana_depleted;
    std::vector<ResourceSource> preexisting_sources_newly_tapped;
    bool land_play_entitlement_consumed = false;

    bool operator==(const PlayerResourceCost&) const = default;
};

// Exact public-rules state transition used to verify the resource ledger.
// `include_hand_and_land_costs` is true for the forced root action and false
// for later priority passes/resolutions, matching the frozen PD0 accumulation
// semantics.
struct ResourceOperation {
    GameState before;
    GameState after;
    bool include_hand_and_land_costs = false;

    bool operator==(const ResourceOperation&) const = default;
};

// Pure input to the dominance comparator. The engine-owned neutral transition
// driver is responsible for producing this record and its resource ledger.
// `root_information_fingerprint` binds comparisons to one owner information
// set without exposing the hidden state itself.
class Settlement {
  public:
    Settlement(const Settlement&) = default;
    Settlement(Settlement&&) noexcept = default;

    const std::string& root_information_fingerprint() const {
        return root_information_fingerprint_;
    }
    const GameState& root_state() const {
        return root_state_;
    }
    const GameState& resource_boundary_state() const {
        return resource_boundary_state_;
    }
    const std::vector<ResourceOperation>& resource_operations() const {
        return resource_operations_;
    }
    const GameState& boundary_state() const {
        return boundary_state_;
    }
    const LearnedDecisionContext& boundary_context() const {
        return boundary_context_;
    }
    const std::array<PlayerResourceCost, 2>& costs() const {
        return costs_;
    }
    bool terminal() const {
        return terminal_;
    }
    // -2 for a nonterminal boundary, -1 for a terminal draw, or 0/1.
    int terminal_winner() const {
        return terminal_winner_;
    }
    bool complete() const {
        return complete_;
    }
    bool unresolved_transient_choice_effect() const {
        return unresolved_transient_choice_effect_;
    }

    bool operator==(const Settlement&) const = default;

  private:
    Settlement(
        std::string root_information_fingerprint,
        GameState root_state,
        GameState resource_boundary_state,
        std::vector<ResourceOperation> resource_operations,
        GameState boundary_state,
        LearnedDecisionContext boundary_context,
        std::array<PlayerResourceCost, 2> costs,
        bool terminal, int terminal_winner, bool complete,
        bool unresolved_transient_choice_effect)
        : root_information_fingerprint_(
              std::move(root_information_fingerprint)),
          root_state_(std::move(root_state)),
          resource_boundary_state_(
              std::move(resource_boundary_state)),
          resource_operations_(
              std::move(resource_operations)),
          boundary_state_(std::move(boundary_state)),
          boundary_context_(boundary_context),
          costs_(std::move(costs)),
          terminal_(terminal),
          terminal_winner_(terminal_winner),
          complete_(complete),
          unresolved_transient_choice_effect_(
              unresolved_transient_choice_effect) {}

    std::string root_information_fingerprint_;
    GameState root_state_;
    // State immediately after the forced root Priority window settles. The
    // operation chain must begin at root_state and end here.
    GameState resource_boundary_state_;
    std::vector<ResourceOperation> resource_operations_;
    GameState boundary_state_;
    LearnedDecisionContext boundary_context_;
    std::array<PlayerResourceCost, 2> costs_;
    bool terminal_ = false;
    int terminal_winner_ = -2;
    bool complete_ = false;
    bool unresolved_transient_choice_effect_ = false;

    friend Settlement
    fq0_dominance_transition::advance_to_next_first_main(
        const probes::DecisionProbe& probe,
        const GameState& information_set_world,
        std::size_t candidate_index,
        std::string root_information_fingerprint);

    friend CanonicalSettlement canonicalize_settlement(
        const Settlement& settlement, std::size_t observer);
};

// Source identities are deliberately absent here. The canonical cost still
// distinguishes source kind and card identity, but treats physically
// interchangeable copies as a multiset.
struct CanonicalPlayerResourceCost {
    std::array<std::size_t, kCardCount> hand_cards_consumed{};
    ManaCost mana_depleted;
    std::array<std::size_t, kCardCount> lands_newly_tapped{};
    std::array<std::size_t, kCardCount> artifacts_newly_tapped{};
    bool land_play_entitlement_consumed = false;

    bool operator==(
        const CanonicalPlayerResourceCost&) const = default;
};

struct CanonicalSettlement {
    bool valid = false;
    // Binary, schema-tagged owner-observable projection. It intentionally
    // omits opponent-hand identities, both library identities/orders,
    // reporting statistics, physical permanent IDs, and allocator counters.
    std::string owner_observable_consequence;
    std::array<CanonicalPlayerResourceCost, 2> costs;

    bool operator==(const CanonicalSettlement&) const = default;
};

enum class Orientation : std::uint8_t {
    Incomparable,
    FirstDominatesSecond,
    SecondDominatesFirst,
};

struct Comparison {
    Orientation orientation = Orientation::Incomparable;
    bool root_information_equal = false;
    bool first_normalized = false;
    bool second_normalized = false;
    bool consequences_equal = false;
    CanonicalSettlement first;
    CanonicalSettlement second;

    bool operator==(const Comparison&) const = default;
};

// Restores only exactly matched, ledger-recorded costs and constructs the
// canonical owner-observable consequence. Any ambiguity or malformed source
// fails closed with valid=false.
CanonicalSettlement canonicalize_settlement(
    const Settlement& settlement, std::size_t observer);

// A dominates B only when normalized consequences are byte-identical, A's
// actor cost is componentwise no greater, A's opponent cost is componentwise
// no smaller, and at least one resource relation is strict.
Comparison compare(const Settlement& first,
                   const Settlement& second,
                   std::size_t actor);

} // namespace old_school::fq0_dominance
