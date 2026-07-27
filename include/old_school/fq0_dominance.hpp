#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

// Pure input to the dominance comparator. The engine-owned neutral transition
// driver is responsible for producing this record and its resource ledger.
// `root_information_fingerprint` binds comparisons to one owner information
// set without exposing the hidden state itself.
struct Settlement {
    std::string root_information_fingerprint;
    GameState root_state;
    GameState boundary_state;
    LearnedDecisionContext boundary_context;
    std::array<PlayerResourceCost, 2> costs;
    bool terminal = false;
    // -2 for a nonterminal boundary, -1 for a terminal draw, or 0/1.
    int terminal_winner = -2;
    bool complete = false;
    bool unresolved_transient_choice_effect = false;

    bool operator==(const Settlement&) const = default;
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
