#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace old_school::probes {

inline constexpr std::string_view kProbeDevV1 =
    "old-school-probe-dev-v1";
inline constexpr std::string_view kProbeDevV2 =
    "old-school-probe-dev-v2";
inline constexpr std::uint64_t kProbeValidationSeed = 0x50524F42455631ULL;

enum class DecisionKind : std::uint8_t {
    Priority,
    Attack,
};

enum class Category : std::uint8_t {
    GreenDevelop,
    GreenTsunamiTiming,
    GreenFavorableAttack,
    GreenUnfavorableAttack,
    RedFaceLethal,
    RedClearBlocker,
    RedFinishDamagedThreat,
    RedStackRace,
    BlueCounterExpensiveSpell,
    BlueConserveCounter,
    BlueCounterLethal,
    BlueCounterWar,
    WhiteEmergencyMoat,
    WhiteEstablishMillstone,
    WhiteMillBeforeDraw,
    WhiteAvoidRedundantMoat,
};

struct BinaryAttackDecision {
    PermanentId attacker = 0;
    bool include = false;

    bool operator==(const BinaryAttackDecision&) const = default;
};

using CandidateAction =
    std::variant<PriorityAction, BinaryAttackDecision>;

struct Candidate {
    // A stable, human-readable description of the candidate. It is fixture
    // metadata only: the corpus intentionally contains no preferred-action
    // labels or scores.
    std::string descriptor;
    CandidateAction action;
};

struct DecisionProbe {
    std::string stable_id;
    Category category = Category::GreenDevelop;
    DecisionKind decision_kind = DecisionKind::Priority;
    DeckId root_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    std::size_t root_player = 0;
    TurnPhase phase = TurnPhase::FirstMain;
    int consecutive_passes = 0;
    GameState state;
    std::array<std::vector<CardId>, 2> original_decks;
    std::vector<Candidate> candidates;
};

struct Validation {
    bool exact_card_conservation = false;
    bool candidates_legal_and_complete = false;
    bool reachable_state = false;
    bool hidden_clone_invariant = false;
    std::vector<std::string> errors;

    bool ok() const;
};

// Constructs the fixed, eval-only 16-position Green/Red/Blue/White
// development corpus. RU Aggro is rejected until dedicated probes are
// authored. Nothing in the runtime policy or training path imports this
// module.
std::vector<DecisionProbe> make_probe_dev_v1();

// Revision of the development corpus whose plan probes make the root choice
// irreversible within the current turn. V1 remains only as the source fixture
// definition used to construct V2; Old School caches use a new hard-cut
// identity.
std::vector<DecisionProbe> make_probe_dev_v2();

Validation validate_probe(
    const DecisionProbe& probe,
    std::uint64_t hidden_seed = kProbeValidationSeed);

// Includes per-probe validation plus corpus-level checks such as stable-ID and
// category uniqueness and four probes per root deck.
std::vector<std::string> validate_probe_dev_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed = kProbeValidationSeed);

std::vector<std::string> validate_probe_dev_v2(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed = kProbeValidationSeed);

// Repartitions/reorders only hidden zones, then verifies that
// sample_determinization returns the same sampled information set for a fixed
// seed.
bool hidden_clone_is_determinization_invariant(
    const DecisionProbe& probe, std::uint64_t seed);

} // namespace old_school::probes
