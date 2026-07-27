#pragma once

#include "old_school/game.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <memory>
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
inline constexpr std::string_view kFieldRegressionsV1 =
    "old-school-field-regressions-v1";
inline constexpr std::string_view kAttackRegressionV1 =
    "old-school-attack-regression-v1";
inline constexpr std::string_view kProbePriorityCallbackCollector =
    "Game::HumanController::choose_priority_action";
inline constexpr std::string_view kProbeLandThenPassScript =
    "land-then-pass-v1";

enum class DecisionKind : std::uint8_t {
    Priority,
    Attack,
    Block,
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
    FieldRULife20FlyingMenChumpAir,
    FieldRULife4FlyingMenChumpAir,
    FieldGreenSecondMainSickBearGrowth,
    FieldGreenBeginCombatGrowthTappedAir,
    FieldGreenAttackAfterGrowthTappedAir,
    FieldGreenAttackAfterGrowthUntappedAirControl,
    DiagnosticRUAttackFlyingIntoLargerFlyingBlocker,
};

struct BinaryAttackDecision {
    PermanentId attacker = 0;
    bool include = false;

    bool operator==(const BinaryAttackDecision&) const = default;
};

struct BinaryBlockDecision {
    PermanentId attacker = 0;
    PermanentId blocker = 0;
    bool include = false;

    bool operator==(const BinaryBlockDecision&) const = default;
};

using CandidateAction =
    std::variant<PriorityAction, BinaryAttackDecision,
                 BinaryBlockDecision>;

struct Candidate {
    // A stable, human-readable description of the candidate. It is fixture
    // metadata only: the corpus intentionally contains no preferred-action
    // labels or scores.
    std::string descriptor;
    CandidateAction action;

    bool operator==(const Candidate&) const = default;
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

// A reject-only corpus made from concrete field reports. It is deliberately
// separate from probe-dev-v3 and every label-cache path: the paired states
// describe legal decisions and their rules consequences, but encode no
// preferred strategic action.
std::vector<DecisionProbe> make_field_regressions_v1();

// One post-C17, reject-only diagnostic for the reported attack-selection
// failure. The public board has exactly one legal 1/1 flying attacker and one
// untapped opposing 4/4 flying blocker, so the authored No Attack / Attack
// pair is the complete power set of legal attacker declarations.
std::vector<DecisionProbe> make_attack_regression_v1();

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

std::vector<std::string> validate_field_regressions_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed = kProbeValidationSeed);

std::vector<std::string> validate_attack_regression_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed = kProbeValidationSeed);

// Settles a binary block candidate captured after attackers were declared.
// resolve_combat() accepts a pre-declaration state, so this adapter restores
// only the referenced attacker's declaration tap before delegating to the
// engine-authoritative combat resolver.
bool settle_binary_block_decision(
    GameState& state, std::size_t attacking_player,
    const BinaryBlockDecision& decision);

// Repartitions/reorders only hidden zones, then verifies that
// sample_determinization returns the same sampled information set for a fixed
// seed.
bool hidden_clone_is_determinization_invariant(
    const DecisionProbe& probe, std::uint64_t seed);

// Evaluation-only immediate resource-dominance audit. These types and
// functions are intentionally owned by the probe module; neither training nor
// deployed policies import them.
enum class Dc1Dominance : std::uint8_t {
    Incomparable,
    FirstDominatesSecond,
    SecondDominatesFirst,
};

enum class Dc1ManaSourceKind : std::uint8_t {
    Land,
    Artifact,
};

struct Dc1ManaSource {
    Dc1ManaSourceKind kind = Dc1ManaSourceKind::Land;
    // Lands have no engine ID, so `key` is their stable root-zone position.
    // Artifacts use their PermanentId.
    std::uint64_t key = 0;
    CardId card = CardId::Forest;

    bool operator==(const Dc1ManaSource&) const = default;
    auto operator<=>(const Dc1ManaSource&) const = default;
};

struct Dc1PlayerResourceCost {
    std::array<std::size_t, kCardCount> hand_cards_consumed{};
    // Factual colored/generic mana capacity depleted while applying and
    // resolving the forced branch. Window-end pool clearing is excluded.
    ManaCost mana_depleted;
    std::vector<Dc1ManaSource> preexisting_sources_newly_tapped;
    bool land_play_entitlement_consumed = false;

    bool operator==(const Dc1PlayerResourceCost&) const = default;
};

struct Dc1CanonicalSettlement {
    GameState settled_state;
    std::array<Dc1PlayerResourceCost, 2> resources;
    TurnPhase phase = TurnPhase::FirstMain;
    std::size_t final_priority_player = 0;
    int final_consecutive_passes = 0;
    bool terminal = false;
    bool window_ended = false;

    bool operator==(const Dc1CanonicalSettlement&) const = default;
};

struct Dc1PairComparison {
    std::string first_descriptor;
    std::string second_descriptor;
    std::vector<std::uint64_t> world_seeds;
    std::vector<Dc1Dominance> world_orientations;
    Dc1Dominance unanimous_orientation =
        Dc1Dominance::Incomparable;
    bool hidden_repartition_bit_identical = false;

    bool operator==(const Dc1PairComparison&) const = default;
};

struct Dc1PairLabelObservation {
    std::string exact_pair_key;
    std::string seat_game_key;
    bool positive = false;

    bool operator==(
        const Dc1PairLabelObservation&) const = default;
};

struct Dc1PairLabelDedupeResult {
    std::vector<Dc1PairLabelObservation> retained;
    std::size_t conflicting_pair_keys = 0;

    bool operator==(
        const Dc1PairLabelDedupeResult&) const = default;
};

// Groups only by exact information-set/action-pair identity. Repeated
// observations with one consistent label retain one deterministic
// representative; a key observed with both labels is dropped entirely.
Dc1PairLabelDedupeResult dedupe_dc1_pair_labels(
    std::vector<Dc1PairLabelObservation> observations);

inline constexpr std::size_t kDc1PriorityActionKindCount =
    static_cast<std::size_t>(
        PriorityActionKind::ActivateMillstone) +
    1;

struct Dc1LegalActionSetSummary {
    std::size_t legal_actions = 0;
    std::array<std::size_t, kDc1PriorityActionKindCount>
        action_kinds{};
    std::uint64_t sorted_descriptor_fnv1a64 = 0;
    bool descriptors_distinct = false;

    bool operator==(
        const Dc1LegalActionSetSummary&) const = default;
};

// Enumerates the complete rules-authoritative Priority action set and
// summarizes it without selecting, settling, or scoring an action. The
// supplied ceiling is a diagnostic memory/safety bound, never a truncation:
// exceeding it fails closed.
Dc1LegalActionSetSummary summarize_dc1_legal_actions(
    const GameState& state, std::size_t player,
    bool sorcery_actions, std::size_t diagnostic_ceiling = 512);

// Forces one legal Priority candidate, then takes no new non-Pass action:
// priority is passed until the current stack and window settle. No phase,
// combat, cleanup, draw, or controller callback is entered.
Dc1CanonicalSettlement settle_dc1_priority_candidate(
    const DecisionProbe& probe, const GameState& information_set_world,
    std::size_t candidate_index);

// True only when the two raw branches differ by their recorded branch-local
// resource ledgers and not by any normalized rules effect or stop context.
bool dc1_settlements_have_equal_normalized_effect(
    const DecisionProbe& probe,
    const Dc1CanonicalSettlement& first,
    const Dc1CanonicalSettlement& second);

// Compares a pair across common sampled information-set worlds. Strict
// dominance is reported only when every world has the same strict
// orientation; any tradeoff or disagreement is incomparable. The hidden-zone
// clone must reproduce the complete descriptor-keyed result exactly.
Dc1PairComparison compare_dc1_priority_pair(
    const DecisionProbe& probe, std::size_t first_candidate_index,
    std::size_t second_candidate_index, std::size_t worlds = 8,
    std::uint64_t seed = 0x4443315041495253ULL);

inline constexpr std::uint64_t kDc1TrainingMiningSeed = 577215;
inline constexpr std::uint64_t kDc1HeldoutMiningSeed = 271828;
inline constexpr std::size_t kDc1Worlds = 8;
inline constexpr std::size_t kDc1BlocksPerSplit = 2;
inline constexpr std::size_t kDc1MaxRootsPerSeatGame = 16;
inline constexpr std::size_t kDc1MaxPairsPerRoot = 8;
inline constexpr std::size_t kDc1MaxLegalActions = 90;

struct Dc1MiningConfig {
    std::uint64_t training_seed = kDc1TrainingMiningSeed;
    std::uint64_t heldout_seed = kDc1HeldoutMiningSeed;
    std::size_t blocks_per_split = kDc1BlocksPerSplit;
    std::size_t worlds = kDc1Worlds;
    std::size_t max_roots_per_seat_game =
        kDc1MaxRootsPerSeatGame;
    std::size_t max_pairs_per_root = kDc1MaxPairsPerRoot;
    std::size_t max_legal_actions = kDc1MaxLegalActions;
    std::size_t max_game_turns = 128;
    double training_exploration_rate = 0.10;
    std::size_t training_minimum_examples_per_deck = 32;
    std::size_t training_minimum_seat_games_per_deck = 8;
    std::size_t heldout_minimum_examples_per_deck = 16;
    std::size_t heldout_minimum_seat_games_per_deck = 4;
    std::string required_model_fingerprint;

    bool operator==(const Dc1MiningConfig&) const = default;
};

struct Dc1DeckMiningSummary {
    std::size_t seat_games = 0;
    std::size_t raw_priority_roots = 0;
    std::size_t raw_multi_action_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t pair_groups = 0;
    std::size_t paired_world_cells = 0;
    std::size_t settlement_operations = 0;
    std::size_t unique_positive_pairs = 0;
    std::size_t unique_incomparable_pairs = 0;
    std::size_t unique_matched_incomparable_controls = 0;
    std::size_t conflicting_pair_keys = 0;
    std::size_t positive_seat_games = 0;
    std::size_t incomparable_control_seat_games = 0;
    bool density_passed = false;

    bool operator==(const Dc1DeckMiningSummary&) const = default;
};

struct Dc1MiningSplitReport {
    std::uint64_t seed = 0;
    std::size_t games = 0;
    std::size_t seat_games = 0;
    std::size_t raw_priority_roots = 0;
    std::size_t raw_multi_action_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t pair_groups = 0;
    std::size_t paired_world_cells = 0;
    std::size_t settlement_operations = 0;
    std::array<Dc1DeckMiningSummary, kDeckCount> decks{};
    bool hidden_repartition_passed = false;
    bool accounting_passed = false;
    bool density_passed = false;

    bool operator==(const Dc1MiningSplitReport&) const = default;
};

struct Dc1DominanceAuditReport {
    std::string model_fingerprint;
    Dc1MiningConfig config;
    bool fixture_gate_passed = false;
    Dc1MiningSplitReport training;
    Dc1MiningSplitReport heldout;
    bool accounting_passed = false;
    bool gate_passed = false;

    bool operator==(const Dc1DominanceAuditReport&) const = default;
};

// Runs the bounded, evaluation-only all-five density audit. It never fits,
// mutates, saves, or deploys a model.
Dc1DominanceAuditReport audit_dc1_dominance_mining(
    std::shared_ptr<const LearnedModel> frozen_parent,
    Dc1MiningConfig config = {});

inline constexpr std::size_t kDc1ActionCensusThreshold = 64;
inline constexpr std::size_t kDc1ActionCensusCeiling = 512;

struct Dc1ActionCensusContext {
    bool training_split = false;
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    std::size_t seat = 0;
    DeckId root_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    std::size_t trace_ordinal = 0;
    std::size_t turn_number = 0;
    TurnPhase phase = TurnPhase::FirstMain;
    int consecutive_passes = 0;
    std::size_t stack_size = 0;
    Dc1LegalActionSetSummary actions;

    bool operator==(
        const Dc1ActionCensusContext&) const = default;
};

struct Dc1ActionCensusDeckSummary {
    std::size_t seat_games = 0;
    std::size_t priority_roots = 0;
    std::size_t over_threshold_roots = 0;
    std::size_t maximum_legal_actions = 0;
    std::vector<std::size_t> legal_action_histogram;

    bool operator==(
        const Dc1ActionCensusDeckSummary&) const = default;
};

struct Dc1ActionCensusSplitReport {
    std::uint64_t seed = 0;
    std::size_t games = 0;
    std::size_t seat_games = 0;
    std::size_t priority_roots = 0;
    std::size_t over_threshold_roots = 0;
    std::size_t maximum_legal_actions = 0;
    std::vector<std::size_t> legal_action_histogram;
    std::array<Dc1ActionCensusDeckSummary, kDeckCount> decks{};
    std::vector<Dc1ActionCensusContext> over_threshold_contexts;
    bool descriptors_distinct = false;
    bool accounting_passed = false;

    bool operator==(
        const Dc1ActionCensusSplitReport&) const = default;
};

struct Dc1ActionCensusConfig {
    std::uint64_t training_seed = kDc1TrainingMiningSeed;
    std::uint64_t heldout_seed = kDc1HeldoutMiningSeed;
    std::size_t blocks_per_split = kDc1BlocksPerSplit;
    std::size_t worlds = kDc1Worlds;
    std::size_t max_game_turns = 128;
    double training_exploration_rate = 0.10;
    std::size_t threshold = kDc1ActionCensusThreshold;
    std::size_t diagnostic_ceiling = kDc1ActionCensusCeiling;
    std::string required_model_fingerprint;

    bool operator==(const Dc1ActionCensusConfig&) const = default;
};

struct Dc1ActionCensusReport {
    std::string model_fingerprint;
    Dc1ActionCensusConfig config;
    Dc1ActionCensusSplitReport training;
    Dc1ActionCensusSplitReport heldout;
    std::size_t pair_comparisons = 0;
    std::size_t density_examples = 0;
    bool accounting_passed = false;
    bool reproduced_over_threshold_root = false;
    bool ceiling_passed = false;
    bool gate_passed = false;

    bool operator==(
        const Dc1ActionCensusReport&) const = default;
};

// Replays the fixed DC1 trajectories and enumerates every complete Priority
// legal-action set. It performs no pair comparison, settlement, density
// counting, fit, model write, filter, or deployment.
Dc1ActionCensusReport audit_dc1_action_census(
    std::shared_ptr<const LearnedModel> frozen_parent,
    Dc1ActionCensusConfig config = {});

// Evaluation-only Environment-v3 audit of Blue's actual decisions while an
// opponent-controlled spell is on top of the stack. Handcrafted is permitted
// only as the opposing source-game policy; every score and continuation below
// is produced by the frozen Learned model from Blue's information set.
inline constexpr std::uint64_t kBsrSourceSeed = 1618033;
inline constexpr std::uint64_t kBsrReferenceSeed = 1414213562;
inline constexpr std::size_t kBsrSourceBlocks = 10;
inline constexpr std::size_t kBsrSourceGamesPerBlock =
    kDeckCount * 2 * 2;
inline constexpr std::size_t kBsrSourceGames =
    kBsrSourceBlocks * kBsrSourceGamesPerBlock;
inline constexpr std::size_t kBsrRootsPerLoss = 2;
inline constexpr std::size_t kBsrRootsPerOpponent = 8;
inline constexpr std::size_t kBsrRetainedRoots =
    kDeckCount * kBsrRootsPerOpponent;
inline constexpr std::size_t kBsrMinimumLossesPerOpponent = 4;
inline constexpr std::size_t kBsrMaximumLegalActions = 512;
inline constexpr std::size_t kBsrScoutWorlds = 64;
inline constexpr std::size_t kBsrConfirmationWorlds = 64;
inline constexpr std::size_t kBsrReferenceHorizon = 8;
inline constexpr std::size_t kBsrReferenceEvaluationThreads = 4;
inline constexpr double kBsrDiagnosticRegretThreshold = 0.05;
inline constexpr double kBsrPracticalRegretThreshold = 0.20;
inline constexpr double kBsrPracticalLower95Threshold = 0.10;
inline constexpr std::size_t kBsrMaximumReferenceEvaluations =
    kBsrRetainedRoots * kBsrMaximumLegalActions *
    (kBsrScoutWorlds + kBsrConfirmationWorlds) * 2;
inline constexpr std::string_view kBsrEnvironmentRevision =
    "old-school-environment-v3-cleanup-discard";

struct BsrSourceGame {
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    DeckId opponent_deck = DeckId::Green;
    std::size_t tracked_seat = 0;
    bool tracked_starts = false;
    std::size_t starting_player = 0;
    std::uint64_t seed = 0;

    bool operator==(const BsrSourceGame&) const = default;
};

std::vector<BsrSourceGame> bsr_source_schedule(
    std::uint64_t seed = kBsrSourceSeed,
    std::size_t blocks = kBsrSourceBlocks);

enum class BsrRootEligibility : std::uint8_t {
    Eligible,
    InvalidContext,
    WrongDecisionOwner,
    EmptyStack,
    TrackedSpellOnTop,
    ActionCountOutsideBounds,
    MissingSelectedAction,
    SelectedActionNotLegal,
    SelectedActionAmbiguous,
    DuplicateActionDescriptor,
};

struct BsrRootClassification {
    BsrRootEligibility eligibility =
        BsrRootEligibility::InvalidContext;
    std::vector<PriorityAction> legal_actions;
    std::vector<std::string> descriptors;
    std::size_t selected_action_matches = 0;
    std::optional<std::size_t> selected_action_index;

    bool eligible() const {
        return eligibility == BsrRootEligibility::Eligible;
    }

    bool operator==(const BsrRootClassification&) const = default;
};

// Card-agnostic, lossless descriptor used by harvested Priority roots. It is
// derived only from the rules action fields, including targets and X.
std::string stable_priority_action_descriptor(
    const PriorityAction& action);

BsrRootClassification classify_bsr_trace_root(
    const LearnedDecisionTracePoint& point,
    std::size_t tracked_player,
    std::size_t maximum_legal_actions =
        kBsrMaximumLegalActions);

// Pure deterministic retention seam used by the audit and focused tests.
// Candidate positions are returned in opponent/provenance order after first
// keeping the lexicographically smallest stable information/action-set keys
// within each source loss.
struct BsrRetentionCandidate {
    DeckId opponent_deck = DeckId::Green;
    std::string source_loss_key;
    std::string provenance_key;
    std::string stable_selection_key;

    bool operator==(const BsrRetentionCandidate&) const = default;
};

std::vector<std::size_t> select_bsr_retained_candidate_indices(
    const std::vector<BsrRetentionCandidate>& candidates,
    std::size_t roots_per_loss = kBsrRootsPerLoss,
    std::size_t roots_per_opponent = kBsrRootsPerOpponent);

bool bsr_retention_requirements_met(
    const std::vector<BsrRetentionCandidate>& retained,
    std::size_t roots_per_loss = kBsrRootsPerLoss,
    std::size_t roots_per_opponent = kBsrRootsPerOpponent,
    std::size_t minimum_losses_per_opponent =
        kBsrMinimumLossesPerOpponent);

struct BsrRootKeyContext {
    std::uint64_t game_seed = 0;
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    std::size_t tracked_seat = 0;
    bool tracked_starts = false;
    std::size_t trace_ordinal = 0;

    bool operator==(const BsrRootKeyContext&) const = default;
};

// Hidden-information-safe identity for one actual source decision. It binds
// the Environment-v3 revision, frozen model, exact source provenance and
// owner, public information/action set, and actual selected action.
std::string bsr_stable_root_fingerprint(
    const DecisionProbe& probe,
    std::string_view actual_action_descriptor,
    std::string_view model_fingerprint,
    const BsrRootKeyContext& provenance);

struct BsrReferenceConfig {
    std::uint64_t seed = kBsrReferenceSeed;
    std::size_t scout_worlds = kBsrScoutWorlds;
    std::size_t confirmation_worlds =
        kBsrConfirmationWorlds;
    std::size_t horizon_turns = kBsrReferenceHorizon;
    std::size_t rollouts_per_world = 1;
    std::size_t evaluation_threads =
        kBsrReferenceEvaluationThreads;

    bool operator==(const BsrReferenceConfig&) const = default;
};

struct BsrPairedRegretEstimate {
    double regret = 0.0;
    double standard_error = 0.0;
    double lower_95 = 0.0;

    bool operator==(
        const BsrPairedRegretEstimate&) const = default;
};

BsrPairedRegretEstimate bsr_paired_regret_estimate(
    const std::vector<double>& best_samples,
    const std::vector<double>& actual_samples);

bool bsr_diagnostic_stable_mistake(
    bool scout_confirmation_best_set_stable,
    bool actual_outside_best_sets,
    const BsrPairedRegretEstimate& estimate);

bool bsr_practical_high_cost_mistake(
    bool scout_confirmation_best_set_stable,
    bool actual_outside_best_sets,
    const BsrPairedRegretEstimate& estimate);

bool bsr_practical_audit_gate(
    bool audit_valid,
    std::size_t practical_high_cost_mistakes);

struct BsrRootScore {
    struct ActionMean {
        std::string descriptor;
        double scout_mean = 0.0;
        double confirmation_mean = 0.0;

        bool operator==(const ActionMean&) const = default;
    };

    std::string stable_id;
    std::string information_action_fingerprint;
    std::size_t action_count = 0;
    std::size_t actual_action_index = 0;
    std::string actual_action_descriptor;
    std::string reference_model_fingerprint;
    std::uint64_t reference_seed_base = 0;
    std::uint64_t scout_seed = 0;
    std::uint64_t confirmation_seed = 0;
    std::size_t scout_worlds = 0;
    std::size_t confirmation_worlds = 0;
    std::size_t horizon_turns = 0;
    std::size_t rollouts_per_world = 0;
    std::size_t evaluation_threads = 0;
    std::vector<std::string> scout_best_actions;
    std::vector<std::string> confirmation_best_actions;
    // Descriptor-keyed means retain the complete reference result needed to
    // preserve a divergence root. Rows are canonical descriptor order and
    // never contain a sampled opponent hidden-zone identity.
    std::vector<ActionMean> action_means;
    double scout_actual_mean = 0.0;
    double scout_best_mean = 0.0;
    double confirmation_actual_mean = 0.0;
    double confirmation_best_mean = 0.0;
    double confirmation_regret = 0.0;
    double paired_standard_error = 0.0;
    double paired_lower_95 = 0.0;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    bool scout_confirmation_best_set_stable = false;
    bool actual_outside_best_sets = false;
    bool diagnostic_stable_mistake = false;
    bool practical_high_cost_mistake = false;
    bool descriptor_order_invariant = false;
    bool hidden_repartition_eligible = false;
    bool hidden_repartition_bit_identical = false;
    bool accounting_passed = false;

    bool operator==(const BsrRootScore&) const = default;
};

// Canonicalizes candidates by their stable descriptor before scoring. This
// makes input order irrelevant while retaining same-world pairing across all
// actions. The hidden clone is scored with the same scout/confirmation seeds
// and must be bit-identical.
BsrRootScore score_bsr_priority_probe(
    const DecisionProbe& probe,
    std::string_view actual_action_descriptor,
    std::shared_ptr<const LearnedModel> frozen_model,
    BsrReferenceConfig config = {});

// Hidden-information-safe digest of the owner observation plus the complete
// stable Priority action descriptor set.
std::string bsr_information_action_fingerprint(
    const DecisionProbe& probe);

struct BsrSourceCellSummary {
    DeckId opponent_deck = DeckId::Green;
    std::size_t tracked_seat = 0;
    bool tracked_starts = false;
    std::size_t games = 0;
    std::size_t tracked_losses = 0;
    std::size_t draws = 0;
    std::size_t turn_limit_draws = 0;
    std::size_t trace_roots = 0;
    std::size_t tracked_held_opponent_stack_roots = 0;
    std::size_t opponent_held_opponent_stack_roots = 0;
    std::size_t eligible_loss_roots = 0;
    std::size_t loss_games_with_eligible_roots = 0;
    std::size_t retained_roots = 0;

    bool operator==(const BsrSourceCellSummary&) const = default;
};

struct BsrDeckSummary {
    DeckId opponent_deck = DeckId::Green;
    std::size_t games = 0;
    std::size_t tracked_losses = 0;
    std::size_t draws = 0;
    std::size_t turn_limit_draws = 0;
    std::size_t trace_roots = 0;
    std::size_t tracked_held_opponent_stack_roots = 0;
    std::size_t opponent_held_opponent_stack_roots = 0;
    std::size_t eligible_loss_roots = 0;
    std::size_t loss_games_with_eligible_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t retained_distinct_losses = 0;
    std::size_t diagnostic_stable_mistakes = 0;
    std::size_t practical_high_cost_mistakes = 0;
    std::size_t reference_rollout_evaluations = 0;
    std::size_t reference_terminal_evaluations = 0;
    std::size_t reference_bootstrapped_evaluations = 0;

    bool operator==(const BsrDeckSummary&) const = default;
};

struct BsrRetainedRoot {
    std::string stable_id;
    std::string stable_root_fingerprint;
    std::string information_action_fingerprint;
    DeckId opponent_deck = DeckId::Green;
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    std::size_t tracked_seat = 0;
    bool tracked_starts = false;
    std::uint64_t game_seed = 0;
    std::size_t trace_ordinal = 0;
    std::size_t turn_number = 0;
    TurnPhase phase = TurnPhase::FirstMain;
    std::size_t action_count = 0;
    std::string actual_action_descriptor;
    BsrRootScore score;

    bool operator==(const BsrRetainedRoot&) const = default;
};

struct BsrAuditConfig {
    std::uint64_t source_seed = kBsrSourceSeed;
    std::uint64_t reference_seed = kBsrReferenceSeed;
    std::size_t source_blocks = kBsrSourceBlocks;
    std::size_t source_max_turns = 128;
    std::size_t production_worlds = 8;
    std::size_t roots_per_loss = kBsrRootsPerLoss;
    std::size_t roots_per_opponent =
        kBsrRootsPerOpponent;
    std::size_t minimum_losses_per_opponent =
        kBsrMinimumLossesPerOpponent;
    std::size_t maximum_legal_actions =
        kBsrMaximumLegalActions;
    BsrReferenceConfig reference;
    std::string required_model_fingerprint;

    bool operator==(const BsrAuditConfig&) const = default;
};

struct BsrAuditReport {
    std::string model_fingerprint;
    BsrAuditConfig config;
    std::vector<BsrSourceGame> schedule;
    std::array<BsrSourceCellSummary,
               kBsrSourceGamesPerBlock>
        source_cells{};
    std::array<BsrDeckSummary, kDeckCount> decks{};
    std::vector<BsrRetainedRoot> roots;
    std::size_t source_games = 0;
    std::size_t tracked_losses = 0;
    std::size_t draws = 0;
    std::size_t turn_limit_draws = 0;
    std::size_t trace_roots = 0;
    std::size_t tracked_held_opponent_stack_roots = 0;
    std::size_t opponent_held_opponent_stack_roots = 0;
    std::size_t eligible_loss_roots = 0;
    std::size_t loss_games_with_eligible_roots = 0;
    std::size_t retained_distinct_losses = 0;
    std::size_t diagnostic_stable_mistakes = 0;
    std::size_t practical_high_cost_mistakes = 0;
    std::size_t mistake_opponent_strata = 0;
    std::size_t reference_rollout_evaluations = 0;
    std::size_t reference_terminal_evaluations = 0;
    std::size_t reference_bootstrapped_evaluations = 0;
    bool source_balance_passed = false;
    bool retention_passed = false;
    bool traced_actions_valid = false;
    bool descriptor_order_invariant = false;
    bool hidden_repartition_passed = false;
    bool scout_confirmation_seeds_disjoint = false;
    bool accounting_passed = false;
    bool bounds_passed = false;
    bool audit_valid = false;
    bool diagnostic_replication_found = false;
    bool gate_passed = false;

    bool operator==(const BsrAuditReport&) const = default;
};

// Runs only source matches plus the bounded Learned-mirror reference. It does
// not fit, mutate, save, or deploy a model.
BsrAuditReport audit_bsr_blue_stack_regret(
    std::shared_ptr<const LearnedModel> frozen_model,
    BsrAuditConfig config = {});

// DVR1 is an instrumentation-only, owner-visible serialization boundary for
// exact production/reference Priority disagreements. It deliberately stores
// original deck compositions (the information-set prior) while omitting the
// opponent's actual hidden hand and library partition.
inline constexpr std::string_view kDvr1CaptureSchema =
    "old-school-owner-visible-divergence-root-v1";

struct Dvr1OwnerVisibleRecord {
    std::string schema = std::string(kDvr1CaptureSchema);
    std::string environment_revision =
        std::string(kBsrEnvironmentRevision);
    std::string stable_id;
    std::string production_model_fingerprint;
    std::string information_action_fingerprint;
    BsrRootKeyContext provenance;
    DeckId owner_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    std::size_t decision_owner = 0;
    std::size_t active_player = 0;
    std::size_t starting_player = 0;
    bool owner_on_play = false;
    std::size_t turn_number = 0;
    TurnPhase phase = TurnPhase::FirstMain;
    int consecutive_passes = 0;
    std::array<PublicPlayerState, 2> players;
    std::vector<CardId> owner_hand;
    std::vector<StackObject> stack;
    std::array<std::size_t, 2> extra_turns_pending = {0, 0};
    PermanentId next_permanent_id = 1;
    StackObjectId next_stack_object_id = 1;
    std::array<bool, 2> failed_draw = {false, false};
    std::array<std::array<std::size_t, kCardCount>, 2>
        original_deck_composition{};
    std::vector<std::string> legal_action_descriptors;
    std::vector<PriorityAction> legal_actions;
    std::string production_action_descriptor;
    std::vector<std::string> reference_best_actions;
    std::vector<BsrRootScore::ActionMean> reference_action_means;
    std::string reference_model_fingerprint;
    std::uint64_t reference_seed_base = 0;
    std::uint64_t reference_scout_seed = 0;
    std::uint64_t reference_confirmation_seed = 0;
    std::size_t reference_scout_worlds = 0;
    std::size_t reference_confirmation_worlds = 0;
    std::size_t reference_horizon_turns = 0;
    std::size_t reference_rollouts_per_world = 0;
    std::size_t reference_evaluation_threads = 0;
    std::size_t reference_sampled_worlds = 0;
    std::size_t reference_rollout_evaluations = 0;
    std::size_t reference_terminal_evaluations = 0;
    std::size_t reference_bootstrapped_evaluations = 0;
    double reference_regret = 0.0;
    double paired_standard_error = 0.0;
    double paired_lower_95 = 0.0;

    bool operator==(const Dvr1OwnerVisibleRecord&) const = default;
};

enum class Dvr1CaptureDisposition : std::uint8_t {
    Captured,
    InvalidInput,
    IncompleteActionSet,
    ProductionDescriptorMismatch,
    InconsistentReferenceEvidence,
    UnstableReferenceBestSet,
    ReferenceAgreement,
};

struct Dvr1CaptureResult {
    Dvr1CaptureDisposition disposition =
        Dvr1CaptureDisposition::InvalidInput;
    std::optional<Dvr1OwnerVisibleRecord> record;
    std::string serialized_record;
    std::string record_fingerprint;

    bool captured() const {
        return disposition == Dvr1CaptureDisposition::Captured &&
               record.has_value() &&
               !serialized_record.empty() &&
               !record_fingerprint.empty();
    }

    bool operator==(const Dvr1CaptureResult&) const = default;
};

// Fails closed unless the probe contains the complete legal Priority action
// set, the production descriptor maps exactly once, and independent
// scout/confirmation references agree on a best set that excludes production.
Dvr1CaptureResult capture_dvr1_owner_visible_divergence(
    const DecisionProbe& probe,
    std::string_view production_action_descriptor,
    std::string_view production_model_fingerprint,
    const BsrRootKeyContext& provenance,
    const BsrRootScore& reference);

// Canonical, locale-independent record bytes and their stable FNV-1a
// fingerprint. Strict decoding and DecisionProbe rehydration live in the
// dedicated dvr1_replay module.
std::string serialize_dvr1_owner_visible_record(
    const Dvr1OwnerVisibleRecord& record);

std::string dvr1_owner_visible_record_fingerprint(
    const Dvr1OwnerVisibleRecord& record);

struct Dvr1SelectorReport {
    std::vector<std::size_t> selected_indices;
    std::size_t blue_opponent_stack_candidates = 0;
    std::size_t blue_opponent_stack_selected = 0;
    std::size_t other_candidates = 0;
    std::size_t other_selected = 0;

    std::size_t blue_opponent_stack_skipped() const {
        return blue_opponent_stack_candidates -
               blue_opponent_stack_selected;
    }

    std::size_t other_skipped() const {
        return other_candidates - other_selected;
    }

    bool operator==(const Dvr1SelectorReport&) const = default;
};

Dvr1SelectorReport select_dvr1_capture_roots(
    const std::vector<DecisionProbe>& roots,
    std::size_t maximum_roots);

} // namespace old_school::probes
