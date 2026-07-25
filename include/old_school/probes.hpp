#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace old_school::probes {

inline constexpr std::string_view kProbeDevV3 =
    "old-school-probe-dev-v3";
inline constexpr std::uint64_t kProbeValidationSeed = 0x50524F42455631ULL;
inline constexpr std::string_view kProbeValidationV1 =
    "old-school-probe-validation-v1";
inline constexpr std::uint64_t kProbeValidationV1GameSeed =
    0x52555830484F4C44ULL;
inline constexpr std::string_view kForceSpikePolicyControlsV1 =
    "old-school-force-spike-policy-controls-v1";
inline constexpr std::string_view kProbePriorityCallbackCollector =
    "Game::HumanController::choose_priority_action";
inline constexpr std::string_view kProbeLandThenPassScript =
    "land-then-pass-v1";

enum class DecisionKind : std::uint8_t {
    Priority,
    Attack,
};

enum class Category : std::uint8_t {
    GreenDevelop,
    // Retained only so the private legacy fixture builders in probes.cpp can
    // supply the unchanged Red/Blue/White v3 positions without duplicating
    // their setup code. These three categories are not part of probe-dev-v3.
    GreenTsunamiTiming,
    GreenFavorableAttack,
    GreenUnfavorableAttack,
    RedFaceLethal,
    RedClearBlocker,
    RedFinishDamagedThreat,
    RedStackRace,
    BlueCounterExpensiveSpell,
    BlueForceSpike,
    BlueCounterLethal,
    BlueCounterWar,
    WhiteEmergencyMoat,
    WhiteEstablishMillstone,
    WhiteMillBeforeDraw,
    WhiteAvoidRedundantMoat,
    GreenGrowthSaveBolt,
    GreenGrowthPushCombat,
    GreenGrowthHold,
    RULandColor,
    RUBlockerDevelopment,
    RUFlyingMoatAttack,
    RUDisintegrateLethal,
    RUDisintegrateHoldValidation,
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

struct HarvestProvenance {
    std::string collector;
    std::string trajectory_script;
    std::uint64_t game_seed = 0;
    std::size_t starting_player = 0;
    // Zero-based across all nontrivial priority callbacks in the game.
    std::size_t priority_decision_ordinal = 0;
    std::size_t turn_number = 0;
    TurnPhase phase = TurnPhase::FirstMain;

    bool operator==(const HarvestProvenance&) const = default;
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
    // Present only when the state was captured from a real seeded engine
    // trajectory rather than authored directly.
    std::optional<HarvestProvenance> harvest;
};

struct Validation {
    bool exact_card_conservation = false;
    bool candidates_legal_and_complete = false;
    bool reachable_state = false;
    bool hidden_clone_invariant = false;
    std::vector<std::string> errors;

    bool ok() const;
};

// Constructs the fixed, eval-only 20-position development corpus: four
// decisions for each of Green, Red, Blue, White, and RU Aggro. Candidate
// metadata contains factual actions only, never preferred-action labels.
// Nothing in the runtime policy or training path imports this module.
std::vector<DecisionProbe> make_probe_dev_v3();

// A focused, independently named validation corpus harvested from real seeded
// priority callbacks. V1 intentionally contains one nonlethal RU
// hold-versus-X=0 state. It is a behavioral regression instrument, not a
// deck-balanced corpus and therefore cannot support bot promotion or
// all-five-deck strength claims.
std::vector<DecisionProbe> make_probe_validation_v1();

// Two eval-only states that differ only in whether the opposing spell's
// controller has one public mana available for Force Spike's tax. They are
// supplemental policy controls, not part of the balanced dev-v3 metrics or
// any promotion-eligible corpus.
std::vector<DecisionProbe> make_force_spike_policy_controls_v1();

Validation validate_probe(
    const DecisionProbe& probe,
    std::uint64_t hidden_seed = kProbeValidationSeed);

// Includes per-probe validation plus corpus-level checks such as stable-ID
// and category uniqueness and exactly four probes per metagame deck.
std::vector<std::string> validate_probe_dev_v3(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed = kProbeValidationSeed);

std::vector<std::string> validate_probe_validation_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed = kProbeValidationSeed);

std::vector<std::string> validate_force_spike_policy_controls_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed = kProbeValidationSeed);

// Repartitions/reorders only hidden zones, then verifies that
// sample_determinization returns the same sampled information set for a fixed
// seed.
bool hidden_clone_is_determinization_invariant(
    const DecisionProbe& probe, std::uint64_t seed);

} // namespace old_school::probes
