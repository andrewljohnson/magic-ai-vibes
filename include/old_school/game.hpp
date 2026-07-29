#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace old_school {

enum class CardId : std::uint8_t {
    Forest,
    Mountain,
    GrizzlyBears,
    LightningBolt,
    IronrootTreefolk,
    FireElemental,
    Island,
    Counterspell,
    WaterElemental,
    Tsunami,
    Plains,
    Millstone,
    Moat,
    FlyingMen,
    IronclawOrcs,
    GrayOgre,
    HillGiant,
    Disintegrate,
    GiantGrowth,
    MoxSapphire,
    SolRing,
    AncestralRecall,
    TimeWalk,
    Braingeyser,
    ForceSpike,
    AirElemental,
};

inline constexpr std::size_t kCardCount =
    static_cast<std::size_t>(CardId::AirElemental) + 1;

enum class CardType : std::uint8_t {
    Land,
    Creature,
    Instant,
    Sorcery,
    Artifact,
    Enchantment,
};

struct ManaCost {
    int generic = 0;
    int green = 0;
    int red = 0;
    int blue = 0;
    int white = 0;

    bool operator==(const ManaCost&) const = default;
};

struct CardDefinition {
    CardId id;
    std::string_view name;
    CardType type;
    ManaCost cost;
    int power = 0;
    int toughness = 0;
    int effect_damage = 0;
    bool flying = false;
    // Zero means the creature has no power-based blocking restriction.
    int cannot_block_power_at_least = 0;
};

const CardDefinition& card_definition(CardId card);

std::vector<CardId> green_deck();
std::vector<CardId> red_deck();
std::vector<CardId> blue_deck();
std::vector<CardId> white_control_deck();
std::vector<CardId> ru_aggro_deck();

using PermanentId = std::uint64_t;

struct LandPermanent {
    CardId card;
    bool tapped = false;

    bool operator==(const LandPermanent&) const = default;
};

struct CreaturePermanent {
    PermanentId id;
    CardId card;
    bool tapped = false;
    bool summoning_sick = true;
    int damage = 0;
    int temporary_power_bonus = 0;
    int temporary_toughness_bonus = 0;
    bool exile_on_death_this_turn = false;

    bool operator==(const CreaturePermanent&) const = default;
};

struct ArtifactPermanent {
    PermanentId id;
    CardId card;
    bool tapped = false;

    bool operator==(const ArtifactPermanent&) const = default;
};

struct PlayerState {
    int life = 20;
    std::vector<CardId> library;
    std::vector<CardId> hand;
    std::vector<CardId> graveyard;
    std::vector<CardId> exile;
    std::vector<LandPermanent> lands;
    std::vector<CreaturePermanent> creatures;
    std::vector<ArtifactPermanent> artifacts;
    std::vector<CardId> enchantments;
    // Mana abilities are implicit and do not use the stack. Unspent mana
    // remains available through the current phase, then is cleared.
    ManaCost mana_pool;
    bool land_played_this_turn = false;

    bool operator==(const PlayerState&) const = default;
};

struct Target {
    std::size_t player = 0;
    std::optional<PermanentId> creature;

    static Target player_target(std::size_t player_index);
    static Target creature_target(std::size_t controller,
                                  PermanentId creature_id);

    bool operator==(const Target&) const = default;
};

struct PlayerGameStats {
    std::size_t cards_drawn = 0;
    std::size_t lands_played = 0;
    std::size_t spells_cast = 0;
    std::size_t spells_countered = 0;
    std::size_t damage_to_opponent = 0;
    std::size_t cards_milled = 0;
    std::size_t decisions = 0;
    std::size_t monte_carlo_rollouts = 0;

    bool operator==(const PlayerGameStats&) const = default;
};

using StackObjectId = std::uint64_t;

enum class StackObjectKind : std::uint8_t {
    Spell,
    ActivatedAbility,
};

struct StackObject {
    StackObjectKind kind = StackObjectKind::Spell;
    StackObjectId id;
    CardId card;
    std::size_t controller;
    std::optional<Target> target;
    std::optional<StackObjectId> spell_target;
    int x_value = 0;

    bool operator==(const StackObject&) const = default;
};

struct GameState {
    std::array<PlayerState, 2> players;
    std::array<PlayerGameStats, 2> stats;
    std::vector<StackObject> stack;
    // Time Walk queues an extra turn for its controller. A queued turn is
    // consumed only after that player's current turn finishes.
    std::array<std::size_t, 2> extra_turns_pending = {0, 0};
    // Drawing from an empty library is recorded during spell resolution and
    // converted to a terminal result immediately after that object resolves.
    std::array<bool, 2> failed_draw = {false, false};
    std::size_t active_player = 0;
    std::size_t starting_player = 0;
    std::size_t turn_number = 0;
    PermanentId next_permanent_id = 1;
    StackObjectId next_stack_object_id = 1;

    bool operator==(const GameState&) const = default;
};

// Samples a complete state consistent with everything `observer` can know.
// The original decklists and all public physical zones determine each
// player's hidden card pool. The observer's hand is preserved, the opposing
// hand is sampled by its public size, and both libraries receive fresh random
// orders. Spell stack objects are physical cards; activated abilities are not.
GameState sample_determinization(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t observer, std::uint64_t seed);

// Exact domain-separated seeds used by Learned search. These public pure
// seams keep offline trajectory diagnostics on the same world and
// continuation coordinates as the production sampler.
std::uint64_t learned_search_world_seed(
    std::uint64_t root_seed, std::size_t world);
std::uint64_t learned_search_continuation_seed(
    std::uint64_t root_seed, std::size_t world,
    std::size_t rollout);

enum class PriorityActionKind : std::uint8_t {
    Pass,
    PlayLand,
    CastCreature,
    CastSorcery,
    CastArtifact,
    CastEnchantment,
    CastLightningBolt,
    CastDisintegrate,
    CastGiantGrowth,
    CastCounterspell,
    CastAncestralRecall,
    CastBraingeyser,
    CastForceSpike,
    ActivateMillstone,
};

struct PriorityAction {
    PriorityActionKind kind = PriorityActionKind::Pass;
    CardId card = CardId::Forest;
    std::optional<Target> target;
    std::optional<StackObjectId> spell_target;
    std::optional<PermanentId> source_permanent;
    int x_value = 0;

    static PriorityAction pass();
    static PriorityAction play_land(CardId land);
    static PriorityAction cast_creature(CardId creature);
    static PriorityAction cast_sorcery(CardId sorcery);
    static PriorityAction cast_artifact(CardId artifact);
    static PriorityAction cast_enchantment(CardId enchantment);
    static PriorityAction cast_lightning_bolt(Target bolt_target);
    static PriorityAction cast_disintegrate(int x_value,
                                            Target disintegrate_target);
    static PriorityAction cast_giant_growth(Target growth_target);
    static PriorityAction cast_counterspell(StackObjectId target_spell);
    static PriorityAction cast_ancestral_recall(Target draw_target);
    static PriorityAction cast_braingeyser(int x_value,
                                           Target draw_target);
    static PriorityAction cast_force_spike(StackObjectId target_spell);
    static PriorityAction activate_millstone(PermanentId millstone,
                                             Target mill_target);

    bool operator==(const PriorityAction&) const = default;
};

// Sorcery actions means the active player is in a main phase. Lands and
// creatures additionally require an empty stack; instants do not.
std::vector<PriorityAction>
legal_priority_actions(const GameState& state, std::size_t player,
                       bool sorcery_actions);
bool apply_priority_action(GameState& state, std::size_t player,
                           const PriorityAction& action,
                           bool sorcery_actions);
enum class ForceSpikePaymentChoice : std::uint8_t {
    PayIfAble,
    Decline,
};

bool resolve_top_of_stack(
    GameState& state,
    ForceSpikePaymentChoice force_spike_payment =
        ForceSpikePaymentChoice::PayIfAble);

// Advances only turn ownership, consuming a queued extra turn when present.
// The caller remains responsible for incrementing turn_number and beginning
// the turn.
std::size_t advance_turn_player(GameState& state);

struct PriorityState {
    std::size_t player = 0;
    int consecutive_passes = 0;

    bool operator==(const PriorityState&) const = default;
};

enum class TurnPhase : std::uint8_t {
    FirstMain,
    BeginCombat,
    DeclareAttackers,
    DeclareBlockers,
    DamageOrder,
    EndCombat,
    SecondMain,
};

// Ephemeral rules context for a Learned critic evaluation. This is kept
// separate from GameState because phase, priority ownership, and pass history
// belong to the engine's control flow rather than the physical game state.
// When `valid` is false every encoded context feature is zero, which provides
// an explicit compatibility mode for state-only critics and non-decision
// bootstrap boundaries.
struct LearnedDecisionContext {
    bool valid = false;
    TurnPhase phase = TurnPhase::FirstMain;
    // The player making the decision. In a priority window this is the
    // current priority holder.
    std::size_t decision_player = 0;
    int consecutive_passes = 0;
    bool sorcery_actions = false;

    bool operator==(const LearnedDecisionContext&) const = default;
};

// Fixed order:
//   valid,
//   seven phase one-hots,
//   decision player relative to perspective (self, opponent),
//   pass count (zero, one),
//   sorcery-actions bit.
inline constexpr std::size_t kLearnedDecisionContextFeatureCount = 13;
using LearnedDecisionContextFeatures =
    std::array<double, kLearnedDecisionContextFeatureCount>;

enum class LearnedCriticSchema : std::uint8_t {
    LegacyStateOnly,
    DecisionContextV1,
};

struct LearnedDecisionTracePoint {
    GameState state;
    LearnedDecisionContext context;
    // Present for Priority roots captured by the engine. Recording happens
    // after policy selection, so the snapshot and RNG stream are unchanged.
    std::optional<PriorityAction> selected_priority_action;

    bool operator==(const LearnedDecisionTracePoint&) const = default;
};

enum class LearnedDecisionTraceMode : std::uint8_t {
    Sparse,
    Dense,
};

inline constexpr std::size_t kLearnedDenseDecisionTraceLimit = 64;

LearnedDecisionContextFeatures learned_decision_context_features(
    const LearnedDecisionContext& context,
    std::size_t perspective);

// A perspective-safe snapshot for an interactive player. Public zones contain
// card identities; hidden zones expose counts only. The observer's own hand is
// the sole hidden zone whose card identities are present unless a human
// controller explicitly opts into the debug-only opponent-hand reveal below.
struct PublicPlayerState {
    int life = 20;
    std::size_t library_size = 0;
    std::size_t hand_size = 0;
    std::vector<CardId> graveyard;
    std::vector<CardId> exile;
    std::vector<LandPermanent> lands;
    std::vector<CreaturePermanent> creatures;
    std::vector<ArtifactPermanent> artifacts;
    std::vector<CardId> enchantments;
    ManaCost mana_pool;
    bool land_played_this_turn = false;

    bool operator==(const PublicPlayerState&) const = default;
};

struct PlayerObservation {
    std::size_t observer = 0;
    std::array<PublicPlayerState, 2> players;
    std::vector<CardId> hand;
    // Populated only for an opted-in human debug controller. Learned policies
    // never consume PlayerObservation and observe_game_state() leaves this
    // empty, preserving the normal hidden-information boundary.
    std::optional<std::vector<CardId>> revealed_opponent_hand;
    std::vector<StackObject> stack;
    std::array<std::size_t, 2> extra_turns_pending = {0, 0};
    std::size_t active_player = 0;
    std::size_t starting_player = 0;
    std::size_t turn_number = 0;

    bool operator==(const PlayerObservation&) const = default;
};

PlayerObservation observe_game_state(const GameState& state,
                                     std::size_t observer);

// One entry per untapped defending creature that can block at least one
// attacker. The creature may be left unassigned or assigned to exactly one of
// `legal_attackers`.
struct LegalBlockerChoice {
    PermanentId blocker = 0;
    std::vector<PermanentId> legal_attackers;
};

enum class GameEventKind : std::uint8_t {
    TurnStarted,
    PriorityActionSelected,
    StackObjectResolved,
    AttackersDeclared,
    BlockersDeclared,
    DamageOrderChosen,
    CombatResolved,
    CardsDiscarded,
};

// Event payloads contain public game information only. `blocks` uses
// (attacker, blocker) pairs and its per-attacker order is combat damage order
// for DamageOrderChosen and CombatResolved.
struct GameEvent {
    GameEventKind kind = GameEventKind::TurnStarted;
    std::size_t player = 0;
    TurnPhase phase = TurnPhase::FirstMain;
    std::optional<PriorityAction> priority_action;
    std::optional<StackObject> stack_object;
    std::vector<PermanentId> attackers;
    std::vector<std::pair<PermanentId, PermanentId>> blocks;
    std::vector<CardId> cards;

    bool operator==(const GameEvent&) const = default;
};

struct HumanController {
    // Return an index into the supplied legal action list.
    std::function<std::size_t(
        const PlayerObservation&, TurnPhase,
        const std::vector<PriorityAction>&)>
        choose_priority_action;
    // Return a unique subset of the supplied eligible attacker IDs.
    std::function<std::vector<PermanentId>(
        const PlayerObservation&,
        const std::vector<PermanentId>&)>
        choose_attackers;
    // Return (attacker, blocker) pairs. Each blocker may appear at most once
    // and may only choose from its LegalBlockerChoice entry.
    std::function<std::vector<std::pair<PermanentId, PermanentId>>(
        const PlayerObservation&,
        const std::vector<PermanentId>&,
        const std::vector<LegalBlockerChoice>&)>
        choose_blockers;
    // Return an exact permutation of `blockers`, first to receive damage
    // first. Called only when at least two creatures block one attacker.
    std::function<std::vector<PermanentId>(
        const PlayerObservation&, PermanentId,
        const std::vector<PermanentId>&)>
        choose_damage_order;
    // Return exactly `excess` unique zero-based positions from this
    // observation's hand. Cleanup publishes the discarded card identities.
    std::function<std::vector<std::size_t>(
        const PlayerObservation&, std::size_t excess)>
        choose_cleanup_discards;
    // Optional transcript hook. It receives this controller's observation
    // after public state changes (or at the declaration point for passes).
    std::function<void(const PlayerObservation&, const GameEvent&)>
        observe;
    // Interactive inspection aid. This affects only this human controller's
    // callbacks; bot observations, training features, and rollouts stay
    // hidden-information safe.
    bool reveal_opponent_hand = false;
    // A human bluffing interface may need to pause even when Pass is the
    // sole legal priority action. The default preserves automatic forced
    // passes for terminal play, simulations, and tests.
    bool bluff_mode = false;
};

enum class PriorityPassResult : std::uint8_t {
    Passed,
    StackObjectResolved,
    WindowEnded,
};

PriorityPassResult pass_priority(GameState& state,
                                 PriorityState& priority);

// Rules-only immediate-consequence transition used by evaluators that need
// to observe what a Priority action actually does, rather than the unresolved
// object it may first place on the stack. No opposing response is chosen:
// only Pass is taken until the object created by `action` (or the current top
// object when `action` is Pass) leaves the stack. Empty-stack actions stop
// after their immediate application.
struct ResolvedPriorityActionConsequence {
    GameState state;
    PriorityState priority;
    bool window_ended = false;
    bool terminal = false;
    int winner = -1;
    std::size_t priority_passes = 0;
    std::size_t stack_resolutions = 0;

    bool operator==(
        const ResolvedPriorityActionConsequence&) const = default;
};

std::optional<ResolvedPriorityActionConsequence>
resolve_priority_action_consequence(
    const GameState& state, std::size_t player, bool sorcery_actions,
    int consecutive_passes, const PriorityAction& action);

// A deterministic, rules-only proof that a legal Priority action is strictly
// worse than Pass. Both branches are force-passed until the current
// stack/window settles. Malformed or nonsettling comparisons fail closed by
// retaining the candidate.
struct ValuePassDominanceActionDiagnostic {
    PriorityAction action;
    bool comparison_settled = false;
    bool strictly_dominated_by_pass = false;

    bool operator==(
        const ValuePassDominanceActionDiagnostic&) const = default;
};

struct ValuePassDominanceDiagnostic {
    std::vector<ValuePassDominanceActionDiagnostic> actions;
    std::size_t pass_index = 0;
    bool pass_settled = false;
    std::size_t failed_comparisons = 0;

    std::vector<PriorityAction> retained_actions() const;

    bool operator==(const ValuePassDominanceDiagnostic&) const = default;
};

ValuePassDominanceDiagnostic diagnose_value_pass_dominance(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes);

// Attackers and blockers are identified by permanent ID. A repeated attacker in
// blocks represents multiple creatures blocking it; block order is damage order.
bool resolve_combat(
    GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>& blocks);

void begin_turn(GameState& state, std::size_t player);
inline constexpr std::size_t kMaximumHandSize = 7;
std::vector<CardId> cleanup_turn(
    GameState& state, std::size_t active_player,
    const std::vector<std::size_t>& discard_indices);

enum class EndReason : std::uint8_t {
    LifeTotal,
    EmptyLibrary,
    TurnLimit,
};

enum class BotKind : std::uint8_t {
    Random,
    MonteCarlo,
    DeepMonteCarlo,
    Handcrafted,
    Learned,
};

enum class LearnedVariant : std::uint8_t {
    ValueSearchChampion,
    UnifiedActor,
};

enum class LearnedContinuationController : std::uint8_t {
    Legacy,
    PublicStackPassV1,
};

enum class LearnedContinuationSearchScope : std::uint8_t {
    PriorityOnly,
    AllDecisions,
};

inline constexpr std::size_t kBotKindCount = 5;
inline constexpr std::size_t kBotMatchupCount =
    kBotKindCount * (kBotKindCount - 1) / 2;
inline constexpr std::size_t
    kLearnedValueSearchHorizonTurns = 4;
inline constexpr std::size_t
    kLearnedValueSearchRolloutsPerWorld = 1;
inline constexpr bool
    kLearnedValueSearchBlendsShallowPrior = true;
inline constexpr std::size_t
    kLearnedValueActorLocalSearchWorlds = 8;
inline constexpr std::size_t
    kLearnedValueActorLocalSearchRolloutsPerWorld = 1;
inline constexpr std::size_t
    kLearnedValueActorLocalSearchHorizonTurns = 8;
inline constexpr std::size_t
    kLearnedValueActorLocalSearchEvaluationThreads = 4;
inline constexpr std::size_t
    kLearnedValueActorLocalSearchContinuationWorlds = 2;
inline constexpr std::string_view
    kLearnedValueActorLocalSearchRequiredFingerprint =
        "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
// AQ5-RPI0 reuses the exact AQ4-P1 Priority recipe. Combat roots use the
// latency-first K2/H4 recipe and permit one K1/H4 actor-local continuation
// layer before candidate continuations become depth zero.
inline constexpr std::size_t
    kLearnedValueRecursivePolicyImprovementCombatWorlds = 2;
inline constexpr std::size_t
    kLearnedValueRecursivePolicyImprovementCombatRolloutsPerWorld = 1;
inline constexpr std::size_t
    kLearnedValueRecursivePolicyImprovementCombatHorizonTurns = 4;
inline constexpr std::size_t
    kLearnedValueRecursivePolicyImprovementCombatEvaluationThreads = 1;
inline constexpr std::size_t
    kLearnedValueRecursivePolicyImprovementCombatContinuationWorlds = 1;
inline constexpr std::string_view
    kLearnedValueRecursivePolicyImprovementRequiredFingerprint =
        kLearnedValueActorLocalSearchRequiredFingerprint;

class LearnedModel;
class LearnedPolicyRecorder;
class LearnedPriorityBilinear;
struct LearnedNestedSearchTracker;

inline constexpr std::uint64_t kDefaultLearnedTrainingSeed = 424242;

struct BotConfig {
    BotKind kind = BotKind::Random;
    // `learned` is the frozen value-search champion. The unified actor remains
    // an explicit research challenger and is never selected implicitly.
    LearnedVariant learned_variant = LearnedVariant::ValueSearchChampion;
    // Complete random continuations sampled for every legal action.
    std::size_t rollouts_per_action = 2;
    double exploration_rate = 0.0;
    // Research-only epsilon-greedy exploration inside the bounded,
    // depth-zero Value-mirror continuation. The real root remains greedy.
    // Zero reproduces the historical Value policy bit-for-bit.
    double value_continuation_epsilon = 0.0;
    // Value-only bounded Priority correction. Zero preserves the historical
    // Value policy bit-for-bit. A nonzero weight adds
    //   weight * tanh(logit - mean legal logit)
    // after the unchanged production Value score has been computed.
    double value_priority_residual_weight = 0.0;
    // AQ19's immutable rank-2 state-by-action Priority residual. It is valid
    // only beside exact frozen C16 in the untreated K8/R1/H4 Learned Value
    // recipe. A null pointer preserves C16 bit-for-bit.
    std::shared_ptr<const LearnedPriorityBilinear>
        value_priority_bilinear;
    // Research-only exact Pass-dominance filter for Learned Value Priority
    // choices. It changes neither legal actions nor any other policy. False
    // preserves the historical selector and RNG stream bit-for-bit.
    bool value_pass_dominance = false;
    // Value-only blend between the historical unresolved root shallow prior
    // and the same critic observation after the selected action's immediate
    // rules consequence resolves. Zero preserves the historical observation
    // bit-for-bit; one is fully resolved. This never changes the real
    // continuation policy or filters a legal action.
    double value_resolved_shallow_prior_weight = 0.0;
    // Value-only attack-set aggregation. False averages over the existing
    // sampled legal block sets. True instead uses their minimum
    // attacker-perspective value, without changing which blocks are sampled.
    bool value_adversarial_blocks = false;
    // Default-off AQ4-P1 Priority treatment. It evaluates every real-root
    // action with the exact hidden-safe K8/H8 sampler and symmetric
    // actor-local K2/H4 Value continuations. Validation binds this switch to
    // the frozen C16 model and rejects every other research treatment.
    bool value_actor_local_search = false;
    // Default-off AQ5-RPI0 extension of actor-local search to Priority,
    // attacker, and blocker microdecisions. It is valid only with the exact
    // frozen C16 recipe; false preserves AQ4-P1 as Priority-only.
    bool value_recursive_policy_improvement = false;
    // Versioned, continuation-only controller for Learned Value. Legacy is
    // the exact historical behavior. PublicStackPassV1 is applied only in
    // depth-zero Value-mirror continuations, never at a real root.
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;
    std::size_t training_games = 800;
    // Optional per-seat frozen model. This permits a paired benchmark between
    // two Learned variants without sharing or silently retraining a model.
    std::shared_ptr<const LearnedModel> learned_model;
};

struct GameResult {
    // 0 or 1 for a winner; -1 for a draw.
    int winner = -1;
    EndReason reason = EndReason::TurnLimit;
    std::size_t turns = 0;
    std::size_t starting_player = 0;
    std::array<int, 2> ending_life = {20, 20};
    std::array<PlayerGameStats, 2> player_stats;
    std::array<BotKind, 2> bots = {
        BotKind::Random,
        BotKind::Random,
    };

    bool operator==(const GameResult&) const = default;
};

// Canonical soft terminal label used by the Value challenger recipes:
// draws are 0.5 and decisive results are discounted by game length.
double learned_discounted_terminal_target(
    const GameResult& result, std::size_t perspective);

struct GameConfig {
    std::size_t max_turns = 500;
    std::optional<std::size_t> starting_player;
    std::array<BotConfig, 2> bots = {BotConfig{}, BotConfig{}};
    // Independent from every game/evaluation seed.
    std::uint64_t learned_training_seed =
        kDefaultLearnedTrainingSeed;
    std::shared_ptr<const LearnedModel> learned_model;
    // Root value search is one ply. Continuations set this to zero explicitly
    // so mirror play remains bounded rather than recursively searching.
    std::size_t learned_search_depth = 1;
    // AQ5-RPI0 evaluation sentinel, independent of ordinary Value search
    // depth. Zero permits one actor-local Priority/Attack/Block search.
    // Candidate clones inherit one, which disables RPI for every later
    // decision and makes accidental recursive entry fail closed.
    std::size_t recursive_policy_improvement_evaluation_depth = 0;
    // Evaluation-only AQ6 propagation bit. A treated information-set
    // evaluator carries exact combat completion through every nested H4
    // Priority/Attack/Block decision. Ordinary games and AQ5 leave it false.
    bool learned_evaluation_exact_combat_subgame = false;
    // Training-only sink. The concrete recorder is intentionally opaque so
    // runtime callers cannot inspect or provide hidden game state.
    std::shared_ptr<LearnedPolicyRecorder> learned_policy_recorder;
    // A present controller overrides the corresponding BotConfig only for
    // real-game decisions. Internal bot rollouts remove these callbacks.
    std::array<std::optional<HumanController>, 2> human_controllers;
};

struct WhitePlanActionDiagnostic {
    PriorityAction action;
    double reference_score = 0.0;
    std::size_t two_world_first_place_count = 0;
};

struct WhitePlanTeacherDiagnostic {
    GameState state;
    std::vector<WhitePlanActionDiagnostic> actions;
    std::size_t reference_worlds = 0;
    std::size_t reference_best_action = 0;
    std::size_t two_world_trials = 0;
    std::size_t two_world_reference_agreements = 0;
    std::size_t two_world_plan_order_agreements = 0;
    std::size_t two_world_millstone_preferences = 0;
    std::size_t two_world_moat_preferences = 0;
    std::size_t two_world_plan_ties = 0;
    std::optional<std::size_t> opponent_millstone_action;
    std::optional<std::size_t> redundant_moat_action;

    double two_world_reference_agreement_rate() const;
    double two_world_plan_order_agreement_rate() const;
};

// Evaluation-only held-out state and root-search diagnostic. Neither is used
// by Learned training or runtime policy decisions.
GameState white_lock_plan_diagnostic_state();
WhitePlanTeacherDiagnostic diagnose_white_lock_plan_teacher(
    std::shared_ptr<const LearnedModel> model, std::uint64_t seed);

struct LearnedValuePriorityDiagnostic {
    std::vector<PriorityAction> legal_actions;
    std::vector<PriorityAction> actions;
    std::vector<PriorityAction> pass_dominated_actions;
    std::vector<double> base_scores;
    std::vector<double> policy_logits;
    std::vector<double> centered_policy_logits;
    std::vector<double> priority_residuals;
    std::vector<double> scores;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    double value_resolved_shallow_prior_weight = 0.0;
    std::size_t selected = 0;
    PriorityAction selected_action;

    bool operator==(const LearnedValuePriorityDiagnostic&) const =
        default;
};

struct LearnedValuePriorityResidualDiagnostic {
    std::vector<double> policy_logits;
    double mean_legal_logit = 0.0;
    std::vector<double> centered_policy_logits;
    std::vector<double> residuals;
};

struct LearnedPriorityBilinearDiagnostic {
    // Rows retain caller action order; canonical reduction order is exposed
    // so tests and the fit runner can verify the production mapping.
    std::vector<std::vector<double>> option_rows;
    std::vector<std::size_t> canonical_order;
    std::vector<double> residuals;

    bool operator==(
        const LearnedPriorityBilinearDiagnostic&) const =
        default;
};

struct LearnedPriorityBilinearContinuationDiagnostic {
    bool first_seat_has_root_object = false;
    bool second_seat_has_root_object = false;
    bool seats_share_object_identity = false;
    bool seats_are_semantically_equivalent = false;
    std::array<std::size_t, 2> rollout_counts{};
    std::array<LearnedVariant, 2> variants{};
    std::array<double, 2> exploration_rates{};

    bool operator==(
        const LearnedPriorityBilinearContinuationDiagnostic&) const =
        default;
};

// Structural, model-free audit of context omitted from the current Value
// observation. This is diagnostic data only; no field is consumed by training
// or a deployed policy.
struct ValueContextAliasDiagnostic {
    std::size_t root_player = 0;
    std::size_t perspective = 0;

    std::size_t stack_action_count = 0;
    bool stack_actions_identical = false;
    bool stack_critic_features_bit_identical = false;
    bool stack_policy_features_different = false;

    PriorityPassResult zero_pass_result =
        PriorityPassResult::Passed;
    std::size_t zero_pass_next_player = 0;
    int zero_pass_next_count = 0;
    std::size_t zero_pass_stack_size = 0;
    int zero_pass_life = 0;

    PriorityPassResult one_pass_result =
        PriorityPassResult::Passed;
    std::size_t one_pass_next_player = 0;
    int one_pass_next_count = 0;
    std::size_t one_pass_stack_size = 0;
    int one_pass_life = 0;

    std::size_t main_action_count = 0;
    bool main_actions_identical = false;
    bool main_critic_features_bit_identical = false;
    bool main_policy_features_different = false;
    bool hidden_information_bit_identical = false;

    bool demonstrated() const;
};

ValueContextAliasDiagnostic diagnose_value_context_aliases();

// Terminal utilities used by evaluation-only learned search. ExactOutcome
// preserves the historical win/draw/loss backup. C16DiscountedAbsoluteTurn
// uses the same absolute-turn discount as frozen C16 training labels.
enum class LearnedTerminalUtilityMode : std::uint8_t {
    ExactOutcome,
    C16DiscountedAbsoluteTurn,
};

// Configuration for evaluation-only information-set action scoring. Worlds
// are sampled once from the root player's observation, then every candidate
// is evaluated with the same world and continuation-seed matrix. Nested root
// search is disabled by default and, when explicitly enabled for Value
// continuations, is bounded to one actor-local level.
struct LearnedSearchConfig {
    std::uint64_t seed = 0;
    std::size_t worlds = 1;
    std::size_t rollouts_per_world = 1;
    // Zero finishes the current turn, begins/draws the next turn, and
    // bootstraps before priority. A positive value plays exactly that many
    // complete future turns. DecisionContextV1 then begins/draws the following
    // turn and bootstraps at its First Main decision; legacy models retain
    // their historical post-cleanup bootstrap exactly.
    std::size_t horizon_turns = 4;
    LearnedVariant continuation_variant = LearnedVariant::UnifiedActor;
    // Applies only to depth-zero Value-mirror continuation decisions. Root
    // candidates are still explicitly enumerated and scored greedily.
    double value_continuation_epsilon = 0.0;
    // Reproduces the deployed Value selector's one aggregate shallow-prior
    // observation blended with all continuation samples.
    bool blend_shallow_prior = false;
    // Blends only that one shallow observation with the rules-engine
    // consequence after the root stack object resolves. Zero is the exact
    // historical unresolved observation and one is fully resolved.
    // Continuation worlds, seeds, policies, and samples are unchanged.
    double value_resolved_shallow_prior_weight = 0.0;
    // Applied at the evaluated root and propagated symmetrically to both
    // Value-mirror continuation seats. It is invalid for Unified Actor
    // continuations.
    double value_priority_residual_weight = 0.0;
    // Applied only inside depth-zero Value-mirror continuations. The
    // evaluated root candidates remain the caller's explicit list.
    bool value_pass_dominance = false;
    // Propagated symmetrically to depth-zero Value-mirror continuation seats.
    // The evaluated root remains outside this controller.
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;
    // Evaluation-only Priority-sampler worker count. One preserves the
    // historical serial execution path exactly. Values above one may
    // evaluate independent, preindexed candidate/world/rollout cells
    // concurrently; sample order, seeds, scores, and accounting remain
    // unchanged. Binary-attack samples remain serial.
    std::size_t evaluation_threads = 1;
    // Evaluation-only AC1/DBC seam. Retain the first terminal-before-boundary
    // or prepared-next-turn First Main boundary already produced by each
    // Priority candidate/world/rollout trajectory. The rollout still runs to
    // its configured horizon; the default leaves historical execution
    // untouched.
    bool capture_priority_h0_boundaries = false;
    // Evaluation-only symmetric policy-improvement seam. Kept last so the
    // default-zero extension does not perturb existing aggregate
    // initialization. Zero preserves historical depth-zero continuations
    // bit-for-bit. A positive value gives each continuation actor a fresh
    // Value root search over this many worlds; inner continuations stay at
    // depth zero.
    std::size_t value_continuation_search_worlds = 0;
    // AQ4-P1 remains Priority-only. AQ5-RPI0 explicitly widens the same
    // bounded continuation search to attacker and blocker microdecisions.
    LearnedContinuationSearchScope value_continuation_search_scope =
        LearnedContinuationSearchScope::PriorityOnly;
    // Evaluation-only AQ6 seam. When enabled for Priority samples, retain
    // the unchanged Value critic after the candidate's rules-resolved
    // immediate consequence in the same candidate/world/rollout shape as
    // q_samples. It never participates in selection or continuation play.
    // Kept last so existing aggregate initialization retains its meaning.
    bool capture_settled_boundary_samples = false;
    // Evaluation-only AQ6 seam. Complete combat plans are enumerated through
    // the public rules transition, with the defender choosing blocks and the
    // attacker choosing damage order. The default preserves every historical
    // Attack and Block sampler path exactly.
    bool use_exact_combat_subgame = false;
    // Evaluation-only terminal backup semantics. Kept last so every existing
    // aggregate initializer and default search remains exact bit-for-bit.
    LearnedTerminalUtilityMode terminal_utility_mode =
        LearnedTerminalUtilityMode::ExactOutcome;

    bool operator==(const LearnedSearchConfig&) const = default;
};

inline constexpr std::size_t
    kLearnedExactCombatMaximumAttackers = 8;
inline constexpr std::size_t
    kLearnedExactCombatMaximumBlockers = 8;
inline constexpr std::size_t
    kLearnedExactCombatMaximumBlockAssignments = 6'561;
inline constexpr std::size_t
    kLearnedExactCombatMaximumDamageOrdersPerAssignment = 40'320;
inline constexpr std::size_t
    kLearnedExactCombatMaximumCompletedPlans = 65'536;

// The only licensed AQ4-P1 search recipe. The inner H4/R1 behavior is the
// unchanged production Learned Value search used by each K2 continuation
// actor; this config controls the K8/R1/H8 outer search.
LearnedSearchConfig learned_value_actor_local_search_config(
    std::uint64_t seed);

// Production translation seam for AQ4/AQ15. The optional real-game
// defender-best-response attack treatment is validated but deliberately not
// propagated into the frozen actor-local Priority evaluator.
LearnedSearchConfig
learned_value_actor_local_priority_search_config(
    const BotConfig& bot, std::uint64_t seed);

// The two licensed AQ5-RPI0 real-root recipes. Priority retains AQ4-P1's
// K8/H8 outer search with K2/H4 all-decision continuations. Combat uses a
// latency-first K2/H4 outer search with K1/H4 all-decision continuations.
LearnedSearchConfig
learned_value_recursive_policy_improvement_priority_config(
    std::uint64_t seed);
LearnedSearchConfig
learned_value_recursive_policy_improvement_combat_config(
    std::uint64_t seed);

struct LearnedPriorityH0Boundary {
    GameState state;
    LearnedDecisionContext context;
    double continuation_score = 0.5;
    bool terminal = false;

    bool operator==(
        const LearnedPriorityH0Boundary&) const = default;
};

inline constexpr std::size_t
    kLearnedPriorityMacroActionBound = 4096;
inline constexpr std::size_t
    kLearnedPriorityMacroPhaseTransitionBound = 1024;
inline constexpr std::size_t
    kLearnedPriorityMacroTurnAdvanceBound = 64;

enum class LearnedPriorityMacroDisposition : std::uint8_t {
    PriorityBoundary,
    Terminal,
    Incomplete,
};

enum class LearnedPriorityMacroLimit : std::uint8_t {
    None,
    Action,
    PhaseTransition,
    TurnAdvance,
};

// Fixed, no-knob accounting used by the Priority macro-transition. Keeping
// the arithmetic in this small value type makes exact-bound behavior directly
// testable without weakening the production limits.
struct LearnedPriorityMacroBudget {
    // The caller-forced root Priority action has already been applied.
    std::size_t actions_applied = 1;
    std::size_t priority_actions_applied = 1;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;

    bool try_apply_actions(std::size_t count) noexcept;
    bool try_apply_forced_priority_action() noexcept;
    bool try_advance_phase() noexcept;
    bool try_advance_turn() noexcept;

    bool operator==(
        const LearnedPriorityMacroBudget&) const = default;
};

// Evaluation-only result of forcing one legal Priority action and then
// advancing through production rules. Forced singleton Priority actions are
// taken automatically. A nonterminal result stops before the first subsequent
// multi-action Priority policy call, preserving its exact state, context, and
// ordered legal actions. Limit exhaustion is incomplete evidence, never a
// terminal draw.
struct LearnedPriorityMacroTransition {
    LearnedPriorityMacroDisposition disposition =
        LearnedPriorityMacroDisposition::Incomplete;
    LearnedPriorityMacroLimit exhausted_limit =
        LearnedPriorityMacroLimit::None;
    GameState state;
    LearnedDecisionContext context;
    std::vector<PriorityAction> legal_actions;
    std::optional<GameResult> terminal_result;
    // Includes the caller-forced root action and every production
    // non-Priority policy choice.
    std::size_t actions_applied = 0;
    std::size_t priority_actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;

    bool operator==(
        const LearnedPriorityMacroTransition&) const = default;
};

LearnedPriorityMacroTransition
advance_learned_priority_macro_transition(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes, const PriorityAction& action,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed);

// Evaluation-only generative seam used by AQ7 information-set tree search.
// A search particle owns one complete sampled rules state, but a shared tree
// observes only decisions belonging to `root_observer`. Decisions belonging
// to the other player are selected on a fresh actor-local determinization and
// applied to the particle through the same authoritative rules transitions;
// they are never exposed as shared tree nodes.
enum class LearnedGenerativeDecisionKind : std::uint8_t {
    Priority,
    Attack,
    Block,
};

double learned_generative_terminal_utility(
    const GameResult& result,
    std::size_t perspective,
    LearnedTerminalUtilityMode mode);

struct LearnedGenerativePriorityDecision {
    LearnedDecisionContext context;

    bool operator==(
        const LearnedGenerativePriorityDecision&) const = default;
};

struct LearnedGenerativeAttackDecision {
    std::size_t attacking_player = 0;
    std::vector<PermanentId> selected_attackers;
    PermanentId subject = 0;
    std::vector<PermanentId> remaining_attackers;

    bool operator==(
        const LearnedGenerativeAttackDecision&) const = default;
};

struct LearnedGenerativeBlockDecision {
    std::size_t attacking_player = 0;
    std::vector<PermanentId> attackers;
    std::vector<std::pair<PermanentId, PermanentId>>
        selected_blocks;
    PermanentId subject_blocker = 0;
    std::vector<PermanentId> remaining_blockers;

    bool operator==(
        const LearnedGenerativeBlockDecision&) const = default;
};

using LearnedGenerativeDecision = std::variant<
    LearnedGenerativePriorityDecision,
    LearnedGenerativeAttackDecision,
    LearnedGenerativeBlockDecision>;

struct LearnedGenerativePosition {
    // This state is the simulation particle. Actor-local determinizations are
    // returned as temporary values and never overwrite it.
    GameState truth;
    std::array<std::vector<CardId>, 2> original_decks;
    std::size_t root_observer = 0;
    LearnedGenerativeDecision decision;

    bool operator==(
        const LearnedGenerativePosition&) const = default;
};

struct LearnedGenerativeAttackAction {
    PermanentId subject = 0;
    bool include = false;

    bool operator==(
        const LearnedGenerativeAttackAction&) const = default;
};

struct LearnedGenerativeBlockAction {
    PermanentId blocker = 0;
    std::optional<PermanentId> attacker;

    bool operator==(
        const LearnedGenerativeBlockAction&) const = default;
};

using LearnedGenerativeActionPayload = std::variant<
    PriorityAction,
    LearnedGenerativeAttackAction,
    LearnedGenerativeBlockAction>;

struct LearnedGenerativeAction {
    std::string stable_key;
    LearnedGenerativeActionPayload payload;

    bool operator==(
        const LearnedGenerativeAction&) const = default;
};

struct LearnedGenerativeActionPrior {
    LearnedGenerativeAction action;
    double successor_value = 0.5;
    double prior = 0.0;

    bool operator==(
        const LearnedGenerativeActionPrior&) const = default;
};

// Cumulative macro accounting captured immediately after an opponent policy
// choice is committed, before any continuation can reach a later root-private
// decision. This is evidence only; it does not affect transition control.
struct LearnedGenerativeDecisionAccounting {
    std::size_t actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;
    std::size_t opponent_decisions_applied = 0;

    bool operator==(
        const LearnedGenerativeDecisionAccounting&) const =
        default;
};

struct LearnedGenerativeObservation {
    std::size_t actor = 0;
    LearnedGenerativeDecisionKind kind =
        LearnedGenerativeDecisionKind::Priority;
    PlayerObservation observation;
    std::string information_set_key;
    std::string legal_signature;
    std::vector<LearnedGenerativeActionPrior> actions;
    double fpu_leaf_value = 0.5;

    bool operator==(
        const LearnedGenerativeObservation&) const = default;
};

struct LearnedGenerativeOpponentDecisionWitness {
    std::size_t actor = 0;
    LearnedGenerativeDecisionKind kind =
        LearnedGenerativeDecisionKind::Priority;
    std::string information_set_key;
    std::string legal_signature;
    std::vector<LearnedGenerativeActionPrior> actions;
    std::string selected_stable_key;
    std::uint64_t search_seed = 0;
    std::uint64_t tie_seed = 0;
    LearnedGenerativeDecisionAccounting
        accounting_through_decision;

    bool operator==(
        const LearnedGenerativeOpponentDecisionWitness&) const =
        default;
};

enum class LearnedGenerativeDisposition : std::uint8_t {
    DecisionBoundary,
    Terminal,
    Bound,
};

enum class LearnedGenerativeBound : std::uint8_t {
    None,
    MacroAction,
    MacroPhaseTransition,
    MacroTurnAdvance,
    ExactCombat,
};

struct LearnedGenerativeWitness {
    std::optional<PriorityAction> applied_priority_action;
    std::size_t priority_actions_applied = 0;
    std::size_t opponent_decisions_applied = 0;
    std::vector<LearnedGenerativeOpponentDecisionWitness>
        opponent_decisions;
    std::size_t stack_size_before = 0;
    std::size_t stack_size_after = 0;
    bool stack_settled = false;
    bool exact_combat_completed = false;
    std::size_t exact_combat_completed_plan_count = 0;
    bool exact_combat_contains_pure_chump = false;
    std::vector<std::pair<PermanentId, PermanentId>>
        completed_damage_ordered_blocks;

    bool operator==(
        const LearnedGenerativeWitness&) const = default;
};

struct LearnedGenerativeTransition {
    LearnedGenerativeDisposition disposition =
        LearnedGenerativeDisposition::Bound;
    LearnedGenerativeBound exhausted_bound =
        LearnedGenerativeBound::None;
    std::optional<LearnedGenerativePosition> position;
    std::optional<GameResult> terminal_result;
    LearnedGenerativeWitness witness;
    std::size_t actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;

    bool operator==(
        const LearnedGenerativeTransition&) const = default;
};

struct LearnedGenerativeLeafEvaluation {
    double value = 0.5;
    bool terminal = false;
    bool exact_combat_completed = false;
    std::size_t exact_combat_completed_plan_count = 0;
    bool exact_combat_contains_pure_chump = false;
    std::vector<std::pair<PermanentId, PermanentId>>
        completed_damage_ordered_blocks;

    bool operator==(
        const LearnedGenerativeLeafEvaluation&) const = default;
};

LearnedGenerativePosition
make_learned_generative_priority_position(
    const GameState& truth,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t root_observer,
    const LearnedDecisionContext& context);

LearnedGenerativePosition
make_learned_generative_attack_position(
    const GameState& truth,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t root_observer, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers);

LearnedGenerativePosition
make_learned_generative_block_position(
    const GameState& truth,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t root_observer, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>&
        selected_blocks,
    PermanentId subject_blocker,
    const std::vector<PermanentId>& remaining_blockers);

std::size_t learned_generative_actor(
    const LearnedGenerativePosition& position);
LearnedGenerativeDecisionKind learned_generative_decision_kind(
    const LearnedGenerativePosition& position);

// Returns a fresh world consistent with the acting player's information set.
// The input position is const so the simulation particle cannot be
// accidentally overwritten by a temporary determinization.
GameState learned_generative_actor_determinization(
    const LearnedGenerativePosition& position,
    std::uint64_t seed);

LearnedGenerativeObservation observe_learned_generative_position(
    const LearnedGenerativePosition& position,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed);

LearnedGenerativeObservation observe_learned_generative_position(
    const LearnedGenerativePosition& position,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed,
    LearnedTerminalUtilityMode terminal_utility_mode);

// Applies `stable_action_key` to the authoritative particle. By default,
// opponent decisions are hidden behind fresh actor-local frozen-C16 choices
// until the root observer acts again. Setting `advance_opponent` false is an
// evaluation-only one-edge seam used to form priors and focused tests.
LearnedGenerativeTransition advance_learned_generative_position(
    const LearnedGenerativePosition& position,
    std::string_view stable_action_key,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed,
    bool advance_opponent = true);

// A depth/node-bound leaf is always rules-complete. An Attack or Block prefix
// is completed exactly while preserving every already-fixed choice before
// the frozen critic is queried.
LearnedGenerativeLeafEvaluation
evaluate_learned_generative_leaf(
    const LearnedGenerativePosition& position,
    std::size_t perspective,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed);

LearnedGenerativeLeafEvaluation
evaluate_learned_generative_leaf(
    const LearnedGenerativePosition& position,
    std::size_t perspective,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed,
    LearnedTerminalUtilityMode terminal_utility_mode);

struct LearnedActionSamples {
    // Outer order matches the caller's candidate order. Inner samples are
    // flattened world-major, then rollout-major, and are paired across every
    // candidate.
    std::vector<std::vector<double>> q_samples;
    // Same shape and order as q_samples. One marks an exact terminal
    // outcome; zero marks a learned bootstrap. Bytes, rather than
    // vector<bool>, keep parallel writes to distinct cells independent.
    std::vector<std::vector<std::uint8_t>>
        terminal_evaluation_flags;
    // Priority-only components of q_samples in the exact same outer and
    // inner order. Together they expose the deployed blend inputs without
    // changing its arithmetic; an optional action-wide Priority residual is
    // still reflected in q_samples separately. Binary Attack and Block
    // samplers leave both empty.
    std::vector<std::vector<double>>
        priority_shallow_prior_samples;
    std::vector<std::vector<double>>
        priority_continuation_samples;
    // Present only when capture_settled_boundary_samples is enabled.
    // Candidate/world/rollout order exactly matches q_samples. Priority
    // observes the rules-resolved immediate consequence; exact Attack and
    // Block observe the actor's post-combat End Combat value in that actor's
    // sampled information-set world. These values are evaluation-only: they
    // never alter q_samples or aggregate scores.
    std::vector<std::vector<double>>
        settled_boundary_samples;
    // Evaluation-only exact-combat witnesses, in q_samples shape. A pure-
    // chump byte marks a selected completed plan in which at least one
    // blocker dies while its attacker survives. A bound-fallback byte marks
    // that exact enumeration exhausted only a declared cardinality bound and
    // the cell therefore retained the unchanged legacy C16 completion.
    // Both matrices are empty for Priority, legacy combat, and defaults.
    std::vector<std::vector<std::uint8_t>>
        exact_combat_pure_chump_flags;
    std::vector<std::vector<std::uint8_t>>
        exact_combat_bound_fallback_flags;
    // Priority-only aggregate scores, in caller candidate order. These
    // reproduce the deployed Learned Value arithmetic exactly: aggregate
    // the shallow observations first, then the continuation observations,
    // with the same division order as the live selector. Raw paired
    // q_samples remain available for statistical comparisons. Binary Attack
    // and Block samplers leave this empty.
    std::vector<double> exact_priority_aggregate_scores;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    // Each rollout is classified exactly once by the score source.
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    // Priority-only nested actor-search accounting. The matrix is absent when
    // value_continuation_search_worlds is zero; otherwise it has the exact
    // q_samples shape and cross-sums to inner_rollout_evaluations.
    std::vector<std::vector<std::size_t>>
        priority_inner_rollout_evaluations;
    std::vector<std::vector<std::size_t>>
        priority_inner_search_invocations;
    std::vector<std::vector<std::size_t>>
        priority_inner_search_max_depth;
    // Attack- and Block-sampler nested actor-search accounting. These
    // matrices are absent for depth-zero combat continuations; otherwise
    // they have the exact q_samples shape and cross-sum to the generic
    // inner-search totals below.
    std::vector<std::vector<std::size_t>>
        combat_inner_rollout_evaluations;
    std::vector<std::vector<std::size_t>>
        combat_inner_search_invocations;
    std::vector<std::vector<std::size_t>>
        combat_inner_search_max_depth;
    std::size_t inner_rollout_evaluations = 0;
    std::size_t inner_search_invocations = 0;
    std::size_t inner_search_max_depth = 0;
    // Present only when capture_priority_h0_boundaries is enabled. Outer and
    // inner order exactly match q_samples.
    std::vector<std::vector<LearnedPriorityH0Boundary>>
        priority_h0_boundaries;

    bool operator==(const LearnedActionSamples&) const = default;
};

struct LearnedValueAttackSetScores {
    std::vector<double> scores;
    // The deployed selector retains the first candidate on an exact tie.
    std::size_t selected_candidate = 0;
};

struct LearnedValueBinaryBlockScores {
    // Canonical order: No Block, Block.
    std::array<double, 2> scores = {0.0, 0.0};
    // The deployed selector retains row zero on an exact tie.
    std::size_t selected_candidate = 0;
};

struct LearnedValueBlockChoiceScores {
    // Canonical row zero is No Block; row i + 1 assigns the subject blocker
    // to legal_attackers[i].
    std::vector<PermanentId> legal_attackers;
    std::vector<double> scores;
    // Matches the deployed joint selector: the earliest canonical branch is
    // retained on an exact tie.
    std::size_t selected_candidate = 0;
};

// Pure aggregation seam used by the deployed attack-set scorer. Outer rows
// are attack candidates and inner rows are the already-generated
// attacker-perspective legal-block scores. False preserves the historical
// mean; true uses the defender's best response (the minimum).
LearnedValueAttackSetScores
aggregate_learned_value_attack_block_scores(
    const std::vector<std::vector<double>>& block_scores,
    bool adversarial_blocks);

// Evaluation-only view of the exact fixed-seed combat samples consumed by
// both aggregation modes. The deployed selector generates this matrix before
// choosing mean or defender-min and draws no further randomness afterward.
std::vector<std::vector<double>>
learned_value_attack_block_samples(
    const GameState& state, std::size_t attacking_player,
    const std::vector<std::vector<PermanentId>>& candidates,
    std::shared_ptr<const LearnedModel> model, std::uint64_t seed);

// Evaluation-only view of the deployed Value attack-set selector. `seed`
// initializes the RNG exactly at the block-candidate enumeration boundary;
// candidates are scored in caller order with the unchanged deployed
// block-sampling distribution. The optional treatment changes only the
// aggregation of the already-generated per-block scores.
LearnedValueAttackSetScores learned_value_attack_set_scores(
    const GameState& state, std::size_t attacking_player,
    const std::vector<std::vector<PermanentId>>& candidates,
    std::shared_ptr<const LearnedModel> model, std::uint64_t seed,
    bool adversarial_blocks = false);

// Evaluation-only exact immediate view of the deployed Value block selector
// for one attacker and blocker. It scores the two post-combat states without
// any continuation, card heuristic, or combat heuristic.
LearnedValueBinaryBlockScores learned_value_binary_block_scores(
    const GameState& state, std::size_t defending_player,
    PermanentId attacker, PermanentId blocker,
    std::shared_ptr<const LearnedModel> model);

// Evaluation-only immediate view of the deployed Value joint blocker
// selector, conditioned on an already-fixed blocker prefix and the current
// subject. Each current choice is scored after the unchanged C16 selector
// completes the remaining blocker suffix.
LearnedValueBlockChoiceScores learned_value_block_choice_scores(
    const GameState& state, std::size_t defending_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>&
        selected_blocks,
    PermanentId subject_blocker,
    const std::vector<PermanentId>& remaining_blockers,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed);

LearnedActionSamples learned_priority_action_samples(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes,
    const std::vector<PriorityAction>& candidates,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig config);

// Evaluates the current binary attacker decision. `selected_attackers` is the
// already-fixed prefix and `remaining_attackers` contains the legal attackers
// after `subject`, in their deployed decision order. The two rows are Skip
// and Include, respectively.
LearnedActionSamples learned_binary_attack_samples(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig config);

// Evaluates one already-declared attacker against one available blocker from
// the defender's information set. The two rows are No Block and Block,
// respectively. Combat legality and consequences are delegated to the rules
// engine.
LearnedActionSamples learned_binary_block_samples(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t defending_player, PermanentId attacker,
    PermanentId blocker,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig config);

struct LearnedBlockChoiceSamples {
    // q_samples row zero is No Block. Row i + 1 assigns the subject blocker
    // to legal_attackers[i], preserving this canonical engine order.
    std::vector<PermanentId> legal_attackers;
    LearnedActionSamples samples;
};

// Evaluates one full-context blocker microdecision. `attackers` is the
// complete declared attack, `selected_blocks` preserves the already-fixed
// blocker prefix as (attacker, blocker), and `remaining_blockers` is the
// canonical suffix after `subject_blocker`. Every candidate completes and
// resolves the full combat before it is scored.
LearnedBlockChoiceSamples learned_block_choice_samples(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t defending_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>&
        selected_blocks,
    PermanentId subject_blocker,
    const std::vector<PermanentId>& remaining_blockers,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig config);

std::vector<double> learned_actor_priority_logits(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const std::vector<PriorityAction>& candidates,
    std::shared_ptr<const LearnedModel> model);
std::array<double, 2> learned_actor_binary_attack_logits(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers,
    std::shared_ptr<const LearnedModel> model);
inline constexpr std::size_t
    kLearnedCriticObservationFeatureCount =
        50 + 24 * kCardCount;
inline constexpr std::size_t
    kLearnedCriticLeafCount = 2;
inline constexpr std::size_t
    kLearnedCriticHiddenCount = 16;
struct LearnedCriticDirectPathParameters {
    std::array<
        std::array<
            double,
            kLearnedCriticObservationFeatureCount>,
        kLearnedCriticLeafCount>
        leaves{};

    bool operator==(
        const LearnedCriticDirectPathParameters&) const =
        default;
};
using LearnedCriticContextDirectPathParameters =
    std::array<
        std::array<
            double,
            kLearnedDecisionContextFeatureCount>,
        kLearnedCriticLeafCount>;
using LearnedCriticHiddenActivations =
    std::array<
        std::array<
            double,
            kLearnedCriticHiddenCount>,
        kLearnedCriticLeafCount>;
using LearnedCriticHiddenOutputParameters =
    LearnedCriticHiddenActivations;
using LearnedCriticOutputBiasParameters =
    std::array<double, kLearnedCriticLeafCount>;
double learned_critic_value(
    const GameState& state, std::size_t perspective,
    std::shared_ptr<const LearnedModel> model);
// Scores an already-encoded actor-local state observation. This is the
// narrow persistence-safe critic seam: callers need not retain or reconstruct
// a GameState after the observation has passed its live collection checks.
double learned_critic_observation_value(
    std::span<const double> observation,
    std::shared_ptr<const LearnedModel> model);
std::array<double, kLearnedCriticLeafCount>
learned_critic_observation_leaf_values(
    std::span<const double> observation,
    std::shared_ptr<const LearnedModel> model);
// Exact frozen tanh activations for the two critic leaves. This deliberately
// exposes no trunk parameters and accepts only the exact two-leaf
// LegacyStateOnly Value topology.
LearnedCriticHiddenActivations
learned_critic_observation_hidden_activations(
    std::span<const double> observation,
    std::shared_ptr<const LearnedModel> model);
// Exact two-leaf LegacyStateOnly Value topology only.
LearnedCriticDirectPathParameters
learned_critic_direct_path_parameters(
    std::shared_ptr<const LearnedModel> model);
LearnedCriticContextDirectPathParameters
learned_critic_context_direct_path_parameters(
    std::shared_ptr<const LearnedModel> model);
LearnedCriticHiddenOutputParameters
learned_critic_hidden_output_parameters(
    std::shared_ptr<const LearnedModel> model);
LearnedCriticOutputBiasParameters
learned_critic_output_bias_parameters(
    std::shared_ptr<const LearnedModel> model);
// Returns an immutable clone after adding one shared actor-local feature
// delta to both critic leaves. A numerically all-zero delta returns the exact
// parent object, preserving bit identity.
std::shared_ptr<const LearnedModel>
with_learned_shared_critic_direct_delta(
    std::shared_ptr<const LearnedModel> parent,
    std::span<const double> delta);
// Returns an immutable clone after adding an independent hidden-to-output
// delta to each exact critic leaf. A numerically all-zero delta returns the
// exact parent object, preserving bit identity.
std::shared_ptr<const LearnedModel>
with_learned_critic_hidden_output_delta(
    std::shared_ptr<const LearnedModel> parent,
    const LearnedCriticHiddenOutputParameters& delta);
// Evaluation-only access to the two predictions underlying an exact
// two-leaf LegacyStateOnly Value ensemble.
std::array<double, 2> learned_critic_leaf_values(
    const GameState& state, std::size_t perspective,
    std::shared_ptr<const LearnedModel> model);

// Evaluation-only diagnostics for agreement with the deployed Handcrafted
// policy. These scores must never be used as Learned labels or training data.
std::vector<double> handcrafted_priority_scores(
    const GameState& state, std::size_t player,
    const std::vector<PriorityAction>& candidates,
    TurnPhase phase = TurnPhase::FirstMain);
std::array<double, 2> handcrafted_binary_attack_scores(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& selected_attackers,
    PermanentId subject,
    const std::vector<PermanentId>& remaining_attackers);

// Evaluation-only seam used to verify information-set invariance and bounded
// root-search accounting. It uses exactly the champion's common-world scorer.
LearnedValuePriorityDiagnostic diagnose_learned_value_priority(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes, std::shared_ptr<const LearnedModel> model,
    std::size_t rollouts_per_action, std::uint64_t seed,
    double value_continuation_epsilon = 0.0,
    double value_priority_residual_weight = 0.0,
    bool value_pass_dominance = false,
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy,
    double value_resolved_shallow_prior_weight = 0.0);

// Evaluation-only seam for the continuation controller. `scores` follows
// `legal_actions` order and is otherwise opaque to the controller.
struct LearnedContinuationControllerDiagnostic {
    std::vector<PriorityAction> legal_actions;
    std::vector<PriorityAction> pass_dominated_actions;
    std::vector<PriorityAction> controller_pruned_actions;
    std::vector<PriorityAction> retained_actions;
    std::vector<double> scores;
    bool explored = false;
    bool tie_break_path_used = false;
    std::size_t selected_legal_index = 0;
    PriorityAction selected_action;

    bool operator==(
        const LearnedContinuationControllerDiagnostic&) const =
        default;
};

LearnedContinuationControllerDiagnostic
diagnose_learned_value_continuation_controller(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const std::vector<double>& scores,
    LearnedContinuationController controller,
    double exploration_rate, std::uint64_t seed,
    bool value_pass_dominance = false);

// Evaluation-only decomposition of the bounded Value Priority residual.
// Inputs are the same hidden-information-safe observation/action features
// used by Learned. No opponent hidden identities are exposed.
LearnedValuePriorityResidualDiagnostic
diagnose_learned_value_priority_residual(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const std::vector<PriorityAction>& candidates,
    std::shared_ptr<const LearnedModel> model,
    double value_priority_residual_weight);

// Evaluation-only view of AQ19's exact production scorer. The candidate list
// may be permuted, but every entry must be a unique legal action.
// Reductions always follow the engine's canonical typed-action order.
LearnedPriorityBilinearDiagnostic
diagnose_learned_priority_bilinear_residual(
    const GameState& state, std::size_t player,
    bool sorcery_actions, TurnPhase phase, int consecutive_passes,
    const std::vector<PriorityAction>& candidates,
    std::shared_ptr<const LearnedPriorityBilinear> residual);

// Evaluation-only view of the exact builder used by Value search for its two
// symmetric depth-zero mirror seats.
LearnedPriorityBilinearContinuationDiagnostic
diagnose_learned_priority_bilinear_continuation(
    const BotConfig& root);

// Focused evaluation-only seams for proving that generation-mode searched
// choices are the actions actually applied by the engine.
struct LearnedActorGenerationPriorityDiagnostic {
    std::size_t searched_roots = 0;
    std::size_t rollout_evaluations = 0;
    PriorityAction selected_action;
    bool transition_applied = false;
    std::optional<PriorityPassResult> pass_result;
    std::optional<GameResult> terminal_result;
    GameState final_state;
};

LearnedActorGenerationPriorityDiagnostic
diagnose_learned_actor_generation_priority(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes, std::shared_ptr<const LearnedModel> parent,
    LearnedSearchConfig search);

struct LearnedValuePolicyPriorityDiagnostic {
    std::vector<PriorityAction> actions;
    std::vector<double> base_scores;
    std::vector<double> policy_logits;
    std::vector<double> centered_policy_logits;
    std::vector<double> residuals;
    std::vector<double> combined_scores;
    std::vector<double> behavior_probabilities;
    std::uint64_t search_seed = 0;
    std::uint64_t choice_seed = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t chosen = 0;
    PriorityAction selected_action;
    bool transition_applied = false;
    std::optional<PriorityPassResult> pass_result;
    std::optional<GameResult> terminal_result;
    GameState final_state;
};

// Focused test seam for P-family collection. It runs one real multi-action
// Priority root through the indexed production Value scorer and 90/10
// categorical behavior, then applies that exact sampled action.
LearnedValuePolicyPriorityDiagnostic
diagnose_learned_value_policy_priority(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes, std::shared_ptr<const LearnedModel> parent,
    std::uint64_t root_seed, std::uint64_t generation,
    std::size_t schedule_index, std::size_t worlds,
    double residual_weight = 0.10);

struct LearnedActorGenerationAttackDiagnostic {
    std::size_t searched_roots = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t included_attackers = 0;
    std::optional<GameResult> terminal_result;
    GameState final_state;
};

LearnedActorGenerationAttackDiagnostic
diagnose_learned_actor_generation_attack(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::shared_ptr<const LearnedModel> parent,
    LearnedSearchConfig search);

class Game {
  public:
    Game(std::vector<CardId> player_zero_deck,
         std::vector<CardId> player_one_deck, std::uint64_t seed,
         GameConfig config = {});

    GameResult run();
    GameResult run_with_trace(std::vector<GameState>& trace);
    GameResult run_with_learned_decision_trace(
        std::vector<LearnedDecisionTracePoint>& trace,
        LearnedDecisionTraceMode mode);
    // Evaluation-only full Priority-root capture. Unlike Dense training
    // traces this does not apply the 64-root training cap; callers must bound
    // the game and perform their own deterministic retention.
    GameResult run_with_priority_root_trace(
        std::vector<LearnedDecisionTracePoint>& trace);
    const GameState& state() const;

  private:
    friend WhitePlanTeacherDiagnostic diagnose_white_lock_plan_teacher(
        std::shared_ptr<const LearnedModel> model,
        std::uint64_t seed);
    friend LearnedValuePriorityDiagnostic
    diagnose_learned_value_priority(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t player, bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        std::shared_ptr<const LearnedModel> model,
        std::size_t rollouts_per_action, std::uint64_t seed,
        double value_continuation_epsilon,
        double value_priority_residual_weight,
        bool value_pass_dominance,
        LearnedContinuationController
            value_continuation_controller,
        double value_resolved_shallow_prior_weight);
    friend LearnedActionSamples learned_priority_action_samples(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t player, bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        const std::vector<PriorityAction>& candidates,
        std::shared_ptr<const LearnedModel> model,
        LearnedSearchConfig config);
    friend LearnedPriorityMacroTransition
    advance_learned_priority_macro_transition(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t player, bool sorcery_actions, TurnPhase phase,
        int consecutive_passes, const PriorityAction& action,
        std::shared_ptr<const LearnedModel> model,
        std::uint64_t seed);
    friend LearnedGenerativeTransition
    advance_learned_generative_position(
        const LearnedGenerativePosition& position,
        std::string_view stable_action_key,
        std::shared_ptr<const LearnedModel> model,
        std::uint64_t seed,
        bool advance_opponent);
    friend LearnedActionSamples learned_binary_attack_samples(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t attacking_player,
        const std::vector<PermanentId>& selected_attackers,
        PermanentId subject,
        const std::vector<PermanentId>& remaining_attackers,
        std::shared_ptr<const LearnedModel> model,
        LearnedSearchConfig config);
    friend LearnedActionSamples learned_binary_block_samples(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t defending_player, PermanentId attacker,
        PermanentId blocker,
        std::shared_ptr<const LearnedModel> model,
        LearnedSearchConfig config);
    friend LearnedValueBlockChoiceScores
    learned_value_block_choice_scores(
        const GameState& state, std::size_t defending_player,
        const std::vector<PermanentId>& attackers,
        const std::vector<std::pair<PermanentId, PermanentId>>&
            selected_blocks,
        PermanentId subject_blocker,
        const std::vector<PermanentId>& remaining_blockers,
        std::shared_ptr<const LearnedModel> model,
        std::uint64_t seed);
    friend LearnedBlockChoiceSamples learned_block_choice_samples(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t defending_player,
        const std::vector<PermanentId>& attackers,
        const std::vector<std::pair<PermanentId, PermanentId>>&
            selected_blocks,
        PermanentId subject_blocker,
        const std::vector<PermanentId>& remaining_blockers,
        std::shared_ptr<const LearnedModel> model,
        LearnedSearchConfig config);
    friend std::vector<double> handcrafted_priority_scores(
        const GameState& state, std::size_t player,
        const std::vector<PriorityAction>& candidates,
        TurnPhase phase);
    friend LearnedActorGenerationAttackDiagnostic
    diagnose_learned_actor_generation_attack(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::shared_ptr<const LearnedModel> parent,
        LearnedSearchConfig search);
    friend LearnedActorGenerationPriorityDiagnostic
    diagnose_learned_actor_generation_priority(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t player, bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        std::shared_ptr<const LearnedModel> parent,
        LearnedSearchConfig search);
    friend LearnedValuePolicyPriorityDiagnostic
    diagnose_learned_value_policy_priority(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t player, bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        std::shared_ptr<const LearnedModel> parent,
        std::uint64_t root_seed, std::uint64_t generation,
        std::size_t schedule_index, std::size_t worlds,
        double residual_weight);

    void initialize();
    bool draw_card(std::size_t player);
    std::optional<GameResult>
    play_priority_window(bool sorcery_actions, TurnPhase phase);
    std::optional<GameResult>
    continue_priority_window(bool sorcery_actions,
                             TurnPhase phase, PriorityState priority);
    std::optional<GameResult> play_combat();
    std::optional<GameResult> play_combat_after_beginning();
    std::optional<GameResult> play_combat_with_attackers(
        std::vector<PermanentId> attackers);
    struct LearnedHorizonEvaluation {
        double score = 0.0;
        bool terminal = false;
    };
    LearnedHorizonEvaluation finish_learned_evaluation_horizon(
        std::size_t perspective, std::size_t horizon_turns,
        LearnedTerminalUtilityMode terminal_utility_mode,
        std::optional<LearnedPriorityH0Boundary>*
            first_prepared_boundary = nullptr);
    PriorityAction
    choose_priority_action(const std::vector<PriorityAction>& actions,
                           std::size_t player, bool sorcery_actions,
                           TurnPhase phase, int consecutive_passes);
    double learned_information_set_action_score(
        const PriorityAction& action, std::size_t player,
        bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        const GameState& sampled_state, std::uint64_t seed) const;
    double learned_value_search_action_score(
        const PriorityAction& action, std::size_t player,
        bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        const GameState& sampled_state, std::uint64_t seed) const;
    double learned_value_shallow_action_score(
        const PriorityAction& action, std::size_t player,
        bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        const GameState& sampled_state,
        double resolved_consequence_weight = 0.0) const;
    std::optional<GameResult>
    finish_turn_after_priority_phase(TurnPhase phase);
    std::shared_ptr<const LearnedModel>
    learned_model_for(std::size_t player) const;
    PriorityAction
    choose_handcrafted_action(const std::vector<PriorityAction>& actions,
                              std::size_t player, TurnPhase phase);
    double handcrafted_action_score(const PriorityAction& action,
                                    std::size_t player,
                                    TurnPhase phase) const;
    double rollout_action(const PriorityAction& action,
                          std::size_t player, bool sorcery_actions,
                          std::uint64_t seed) const;
    std::vector<std::size_t>
    choose_cleanup_discards(std::size_t player,
                            std::size_t excess);
    void perform_cleanup();
    GameResult run_from_turn(std::size_t first_turn);
    std::optional<GameResult> life_total_result() const;
    GameResult make_result(int winner, EndReason reason) const;
    const HumanController*
    human_controller(std::size_t player) const;
    PlayerObservation
    human_observation(std::size_t player) const;
    bool has_human_observer() const;
    void notify_human_observers(const GameEvent& event) const;

    struct LearnedPriorityMacroControl;
    void note_learned_priority_macro_phase(TurnPhase phase);
    void note_learned_priority_macro_cleanup();
    void note_learned_priority_macro_turn_advance();
    void note_learned_priority_macro_nonpriority_actions(
        std::size_t count = 1);

    enum class LearnedDecisionRole : std::uint8_t {
        RealRoot,
        ValueContinuation,
    };

    std::array<std::vector<CardId>, 2> decks_;
    std::mt19937_64 random_;
    GameConfig config_;
    GameState state_;
    std::optional<GameResult> setup_result_;
    std::vector<GameState>* trace_ = nullptr;
    std::vector<LearnedDecisionTracePoint>*
        learned_decision_trace_ = nullptr;
    LearnedDecisionTraceMode learned_decision_trace_mode_ =
        LearnedDecisionTraceMode::Sparse;
    LearnedDecisionRole learned_decision_role_ =
        LearnedDecisionRole::RealRoot;
    // Non-null only inside the evaluation-only macro-transition driver.
    LearnedPriorityMacroControl*
        learned_priority_macro_control_ = nullptr;
    // Evaluation-only shared witness. Copies created by inner action
    // continuations retain this pointer, making accidental recursive root
    // search observable even when the child Game itself is discarded.
    std::shared_ptr<LearnedNestedSearchTracker>
        learned_nested_search_tracker_;
};

struct DeckSimulationStats {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
    std::size_t on_play_games = 0;
    std::size_t on_play_wins = 0;
    std::size_t on_draw_games = 0;
    std::size_t on_draw_wins = 0;
    std::int64_t total_ending_life = 0;
    std::size_t total_cards_drawn = 0;
    std::size_t total_lands_played = 0;
    std::size_t total_spells_cast = 0;
    std::size_t total_spells_countered = 0;
    std::size_t total_damage_to_opponent = 0;
    std::size_t total_cards_milled = 0;

    double win_rate() const;
    double on_play_win_rate() const;
    double on_draw_win_rate() const;
    double average_ending_life() const;
    double average_cards_drawn() const;
    double average_lands_played() const;
    double average_spells_cast() const;
    double average_spells_countered() const;
    double average_damage_to_opponent() const;
    double average_cards_milled() const;
};

struct BotSimulationStats {
    // One entry per player seat, so two seat-games are recorded per game.
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
    std::size_t total_decisions = 0;
    std::size_t total_rollouts = 0;

    double win_rate() const;
    double average_decisions() const;
    double average_rollouts() const;
    double average_rollouts_per_decision() const;
};

struct BotMatchupStats {
    BotKind first_bot = BotKind::Random;
    BotKind second_bot = BotKind::MonteCarlo;
    std::size_t games = 0;
    std::size_t first_wins = 0;
    std::size_t second_wins = 0;
    std::size_t draws = 0;

    double first_win_rate() const;
    double second_win_rate() const;
};

struct SimulationSummary {
    std::size_t games = 0;
    std::array<DeckSimulationStats, 2> decks;
    std::array<std::array<DeckSimulationStats, kBotKindCount>, 2>
        deck_bots;
    std::array<BotSimulationStats, kBotKindCount> bots;
    std::array<BotMatchupStats, kBotMatchupCount> bot_matchups;
    std::size_t draws = 0;
    std::size_t life_total_finishes = 0;
    std::size_t empty_library_finishes = 0;
    std::size_t turn_limit_draws = 0;
    std::size_t total_turns = 0;

    double average_turns() const;
};

SimulationSummary run_simulation(std::size_t games, std::uint64_t seed,
                                 GameConfig game_config = {});

enum class DeckId : std::uint8_t {
    Green,
    Red,
    Blue,
    White,
    RUAggro,
};

inline constexpr std::size_t kDeckCount = 5;
inline constexpr std::size_t kDistinctDeckPairingCount =
    kDeckCount * (kDeckCount - 1) / 2;

enum class BotField : std::uint8_t {
    Random,
    MonteCarlo,
    DeepMonteCarlo,
    Handcrafted,
    Learned,
    Mixed,
};

struct TournamentConfig {
    BotField bot_field = BotField::Random;
    LearnedVariant learned_variant =
        LearnedVariant::ValueSearchChampion;
    std::size_t monte_carlo_rollouts = 2;
    std::size_t deep_monte_carlo_rollouts = 8;
    std::size_t learned_rollouts = 2;
    double value_continuation_epsilon = 0.0;
    std::size_t learned_training_games = 800;
    // Optional exact, already-trained Learned policy. When present this is
    // the complete Learned seat configuration for Learned and Mixed fields;
    // no tournament training is performed.
    std::optional<BotConfig> frozen_learned_bot;
};

struct MatchupSummary {
    DeckId first_deck;
    DeckId second_deck;
    SimulationSummary result;
};

struct TournamentSummary {
    std::size_t games_per_matchup = 0;
    std::size_t total_games = 0;
    std::uint64_t evaluation_seed = 0;
    std::uint64_t learned_training_seed =
        kDefaultLearnedTrainingSeed;
    // Exact Learned policy actually scheduled, including its frozen model.
    // Empty when the field contains no Learned bot.
    std::optional<BotConfig> effective_learned_bot;
    std::string effective_learned_model_fingerprint;
    std::array<DeckSimulationStats, kDeckCount> decks;
    std::array<std::array<DeckSimulationStats, kBotKindCount>,
               kDeckCount>
        deck_bots;
    std::array<BotSimulationStats, kBotKindCount> bots;
    std::array<BotMatchupStats, kBotMatchupCount> bot_matchups;
    std::array<MatchupSummary, kDistinctDeckPairingCount> matchups;
    std::size_t draws = 0;
    std::size_t life_total_finishes = 0;
    std::size_t empty_library_finishes = 0;
    std::size_t turn_limit_draws = 0;
    std::size_t total_turns = 0;

    double average_turns() const;
};

struct DeckLiftComparison {
    DeckId deck = DeckId::Green;
    bool available = false;
    double learned_lift = 0.0;
    double best_other_lift = 0.0;
    BotKind best_other = BotKind::Random;
    bool learned_is_best = false;
};

struct LearnedDeckLiftSummary {
    std::array<DeckLiftComparison, kDeckCount> decks;

    bool complete() const;
    bool learned_is_best_on_every_deck() const;
};

std::string_view deck_name(DeckId deck);
std::string_view deck_list(DeckId deck);
std::string_view bot_name(BotKind bot);
std::string_view learned_variant_name(LearnedVariant variant);
std::string bot_config_name(const BotConfig& bot);
LearnedDeckLiftSummary
compare_learned_deck_lifts(const TournamentSummary& summary);
TournamentSummary run_tournament(std::size_t games_per_matchup,
                                 std::uint64_t seed,
                                 GameConfig game_config = {},
                                 TournamentConfig tournament_config = {});

struct BotBenchmarkOutcomeCounts {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;

    bool operator==(const BotBenchmarkOutcomeCounts&) const = default;
};

// [deck][physical policy seat][play/draw], where play/draw index zero is
// on-play and index one is on-draw.
using BotBenchmarkOutcomeQuadrants =
    std::array<std::array<std::array<BotBenchmarkOutcomeCounts, 2>, 2>,
               kDeckCount>;

struct BotBenchmarkClusteredScoreEstimate {
    std::size_t clusters = 0;
    std::size_t records = 0;
    double mean = 0.0;
    double standard_error = 0.0;
    double confidence_low_95 = 0.0;
    double confidence_high_95 = 0.0;

    bool operator==(
        const BotBenchmarkClusteredScoreEstimate&) const = default;
};

struct BotBenchmarkSummary {
    BotConfig challenger;
    BotConfig baseline;
    std::uint64_t evaluation_seed = 0;
    std::uint64_t learned_training_seed =
        kDefaultLearnedTrainingSeed;
    std::string challenger_model_fingerprint;
    std::string baseline_model_fingerprint;
    std::size_t repetitions_per_deck_pairing = 0;
    std::size_t total_games = 0;
    BotSimulationStats challenger_stats;
    BotSimulationStats baseline_stats;
    std::array<DeckSimulationStats, kDeckCount> challenger_decks;
    std::array<DeckSimulationStats, kDeckCount> baseline_decks;
    BotBenchmarkOutcomeQuadrants challenger_outcome_quadrants;
    BotBenchmarkOutcomeQuadrants baseline_outcome_quadrants;
    BotBenchmarkClusteredScoreEstimate challenger_quartet_cr1;
    // Challenger-perspective outcomes for each exact ordered deck matchup.
    // Rows are challenger decks and columns are baseline decks.
    std::array<std::array<DeckSimulationStats, kDeckCount>, kDeckCount>
        challenger_deck_matchups;
    std::size_t total_turns = 0;
    std::size_t life_total_finishes = 0;
    std::size_t empty_library_finishes = 0;
    std::size_t turn_limit_draws = 0;

    double challenger_win_rate() const;
    double confidence_low_95() const;
    double confidence_high_95() const;
    bool challenger_is_better_95() const;
};

BotBenchmarkSummary
run_bot_benchmark(std::size_t repetitions_per_deck_pairing,
                  std::uint64_t seed, BotConfig challenger,
                  BotConfig baseline, GameConfig game_config = {},
                  bool allow_identical_policy_control = false);

std::shared_ptr<const LearnedModel>
train_learned_model(std::size_t training_games, std::uint64_t seed);
std::shared_ptr<const LearnedModel>
train_learned_value_champion(std::size_t training_games,
                            std::uint64_t seed);
std::shared_ptr<const LearnedModel>
train_learned_value_challenger(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations);
class LearnedValueChallengerArtifact {
  public:
    std::shared_ptr<const LearnedModel> model() const;
    std::size_t training_games() const;
    std::uint64_t seed() const;
    std::size_t self_play_generations() const;

  private:
    LearnedValueChallengerArtifact(
        std::shared_ptr<const LearnedModel> model,
        std::size_t training_games, std::uint64_t seed,
        std::size_t self_play_generations);

    std::shared_ptr<const LearnedModel> model_;
    std::size_t training_games_ = 0;
    std::uint64_t seed_ = 0;
    std::size_t self_play_generations_ = 0;

    friend LearnedValueChallengerArtifact
    train_learned_value_challenger_artifact(
        std::size_t training_games, std::uint64_t seed,
        std::size_t self_play_generations);
    friend void write_learned_value_challenger_artifact_atomic(
        const std::string& path,
        const LearnedValueChallengerArtifact& artifact);
    friend LearnedValueChallengerArtifact
    load_learned_value_challenger_artifact(
        const std::string& path,
        std::size_t expected_training_games,
        std::uint64_t expected_seed,
        std::size_t expected_self_play_generations);
};

LearnedValueChallengerArtifact
train_learned_value_challenger_artifact(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations);
// Challenger artifacts are deliberately separate from legacy G0 and the
// immutable G8 bundles. Their cache identity binds the exact challenger
// recipe, engine/model schema, training size, seed, and generation count.
std::string learned_value_challenger_cache_path(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations);
void write_learned_value_challenger_artifact_atomic(
    const std::string& path,
    const LearnedValueChallengerArtifact& artifact);
LearnedValueChallengerArtifact
load_learned_value_challenger_artifact(
    const std::string& path, std::size_t expected_training_games,
    std::uint64_t expected_seed,
    std::size_t expected_self_play_generations);

// Coverage accounting for the live decision roots used by the contextual
// S1 trainer. Deck counts are attributed to the deck controlled by the
// decision player. Stack status uses index 0 for empty and 1 for non-empty.
struct LearnedValueContextRootCoverage {
    std::size_t anchor_roots = 0;
    std::size_t self_play_roots = 0;
    std::array<std::size_t, kDeckCount> decision_player_decks{};
    std::array<std::size_t, 7> phases{};
    std::array<std::size_t, 2> pass_counts{};
    std::array<std::size_t, 2> stack_status{};

    std::size_t total_roots() const;
    bool operator==(
        const LearnedValueContextRootCoverage&) const = default;
};

class LearnedValueContextChallengerArtifact {
  public:
    std::shared_ptr<const LearnedModel> model() const;
    std::size_t training_games() const;
    std::uint64_t seed() const;
    std::size_t self_play_generations() const;
    LearnedCriticSchema critic_schema() const;
    LearnedDecisionTraceMode trace_mode() const;
    std::size_t trace_limit() const;
    const LearnedValueContextRootCoverage& root_coverage() const;

  private:
    LearnedValueContextChallengerArtifact(
        std::shared_ptr<const LearnedModel> model,
        std::size_t training_games, std::uint64_t seed,
        std::size_t self_play_generations,
        LearnedValueContextRootCoverage root_coverage);

    std::shared_ptr<const LearnedModel> model_;
    std::size_t training_games_ = 0;
    std::uint64_t seed_ = 0;
    std::size_t self_play_generations_ = 0;
    LearnedValueContextRootCoverage root_coverage_;

    friend LearnedValueContextChallengerArtifact
    train_learned_value_context_challenger_artifact(
        std::size_t training_games, std::uint64_t seed,
        std::size_t self_play_generations);
    friend void
    write_learned_value_context_challenger_artifact_atomic(
        const std::string& path,
        const LearnedValueContextChallengerArtifact& artifact);
    friend LearnedValueContextChallengerArtifact
    load_learned_value_context_challenger_artifact(
        const std::string& path,
        std::size_t expected_training_games,
        std::uint64_t expected_seed,
        std::size_t expected_self_play_generations);
};

std::shared_ptr<const LearnedModel>
train_learned_value_context_challenger(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations);
LearnedValueContextChallengerArtifact
train_learned_value_context_challenger_artifact(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations);
std::string learned_value_context_challenger_cache_path(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations);
void write_learned_value_context_challenger_artifact_atomic(
    const std::string& path,
    const LearnedValueContextChallengerArtifact& artifact);
LearnedValueContextChallengerArtifact
load_learned_value_context_challenger_artifact(
    const std::string& path, std::size_t expected_training_games,
    std::uint64_t expected_seed,
    std::size_t expected_self_play_generations);

// The preregistered dense-trace ablation keeps one implementation while
// publishing D0 (context columns masked to zero) and D1 (live context) as
// distinct, fail-closed artifact families.
enum class LearnedValueDenseContextTreatment : std::uint8_t {
    ContextMasked,
    ContextLive,
};

class LearnedValueDenseContextChallengerArtifact {
  public:
    std::shared_ptr<const LearnedModel> model() const;
    std::size_t training_games() const;
    std::uint64_t seed() const;
    std::size_t self_play_generations() const;
    LearnedValueDenseContextTreatment treatment() const;
    LearnedCriticSchema critic_schema() const;
    bool context_masked() const;
    LearnedDecisionTraceMode trace_mode() const;
    std::size_t trace_limit() const;
    const LearnedValueContextRootCoverage& root_coverage() const;

  private:
    LearnedValueDenseContextChallengerArtifact(
        std::shared_ptr<const LearnedModel> model,
        std::size_t training_games, std::uint64_t seed,
        std::size_t self_play_generations,
        LearnedValueDenseContextTreatment treatment,
        LearnedValueContextRootCoverage root_coverage);

    std::shared_ptr<const LearnedModel> model_;
    std::size_t training_games_ = 0;
    std::uint64_t seed_ = 0;
    std::size_t self_play_generations_ = 0;
    LearnedValueDenseContextTreatment treatment_ =
        LearnedValueDenseContextTreatment::ContextMasked;
    LearnedValueContextRootCoverage root_coverage_;

    friend LearnedValueDenseContextChallengerArtifact
    train_learned_value_dense_context_challenger_artifact(
        std::size_t training_games, std::uint64_t seed,
        std::size_t self_play_generations,
        LearnedValueDenseContextTreatment treatment);
    friend void
    write_learned_value_dense_context_challenger_artifact_atomic(
        const std::string& path,
        const LearnedValueDenseContextChallengerArtifact& artifact);
    friend LearnedValueDenseContextChallengerArtifact
    load_learned_value_dense_context_challenger_artifact(
        const std::string& path,
        std::size_t expected_training_games,
        std::uint64_t expected_seed,
        std::size_t expected_self_play_generations,
        LearnedValueDenseContextTreatment expected_treatment);
};

std::shared_ptr<const LearnedModel>
train_learned_value_dense_context_challenger(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations,
    LearnedValueDenseContextTreatment treatment);
LearnedValueDenseContextChallengerArtifact
train_learned_value_dense_context_challenger_artifact(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations,
    LearnedValueDenseContextTreatment treatment);
std::string learned_value_dense_context_challenger_cache_path(
    std::size_t training_games, std::uint64_t seed,
    std::size_t self_play_generations,
    LearnedValueDenseContextTreatment treatment);
void write_learned_value_dense_context_challenger_artifact_atomic(
    const std::string& path,
    const LearnedValueDenseContextChallengerArtifact& artifact);
LearnedValueDenseContextChallengerArtifact
load_learned_value_dense_context_challenger_artifact(
    const std::string& path, std::size_t expected_training_games,
    std::uint64_t expected_seed,
    std::size_t expected_self_play_generations,
    LearnedValueDenseContextTreatment expected_treatment);

std::shared_ptr<const LearnedModel>
train_learned_actor_model(std::size_t training_games,
                          std::uint64_t seed);

// Card-agnostic update seams for iterated Learned training. Callers provide
// already-encoded observations/action features and targets; this layer never
// inspects cards, game state, or another policy.
struct LearnedCriticTrainingExample {
    std::vector<double> features;
    double target = 0.5;
};

struct LearnedContextualCriticTrainingExample {
    std::vector<double> features;
    LearnedDecisionContextFeatures context_features{};
    double target = 0.5;
};

struct LearnedValueUpdateConfig {
    std::size_t epochs = 3;
    double learning_rate = 0.006;
    // Every independently cloned critic leaf uses
    //   root_seed ^ (member_training_tag + member_index).
    // Keeping both components explicit reproduces the legacy Value trainer's
    // member seed flow without coupling callers to ensemble size.
    std::uint64_t root_seed = 0;
    std::uint64_t member_training_tag = 0;
};

// Recursively deep-clones a frozen Value model, updates every independently
// cloned critic leaf, then republishes the composite as immutable. An empty
// example set performs a pure recursive clone.
std::shared_ptr<const LearnedModel> update_learned_value_model(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedCriticTrainingExample>& examples,
    LearnedValueUpdateConfig config = {});

// A deterministic, full-batch calibration seam for frozen Value critics.
// Each example contributes `weight` times its binary cross-entropy. Only
// each critic leaf's hidden-to-output weights and output bias may change;
// the critic trunk, direct path, policy tensors, and parent stay frozen.
struct LearnedWeightedCriticTrainingExample {
    std::vector<double> features;
    double target = 0.5;
    double weight = 1.0;

    bool operator==(
        const LearnedWeightedCriticTrainingExample&) const =
        default;
};

struct LearnedOutputCalibrationConfig {
    // OC1 permits a smaller cap for fail-closed tests, but never more than
    // the preregistered 32 iterations. The tether and tolerance are fixed.
    std::size_t max_iterations = 32;
    double l2_tether = 0.01;
    double gradient_tolerance = 1e-10;
};

struct LearnedOutputCalibrationDiagnostics {
    std::size_t example_count = 0;
    std::size_t leaf_count = 0;
    // Maximum number of accepted full-batch updates among critic leaves.
    std::size_t iterations = 0;
    bool converged = false;
    double total_weight = 0.0;
    // For an ensemble these are the arithmetic means of the leaf losses.
    double before_weighted_bce = 0.0;
    double after_weighted_bce = 0.0;
    double max_parameter_delta = 0.0;

    bool operator==(
        const LearnedOutputCalibrationDiagnostics&) const = default;
};

struct LearnedOutputCalibrationResult {
    std::shared_ptr<const LearnedModel> model;
    LearnedOutputCalibrationDiagnostics diagnostics;
};

inline constexpr std::size_t
    kLearnedOutputCalibrationLeafCount = 2;
inline constexpr std::size_t
    kLearnedOutputCalibrationParameterCount = 17;

struct LearnedOutputCalibrationParameters {
    std::array<
        std::array<
            double,
            kLearnedOutputCalibrationParameterCount>,
        kLearnedOutputCalibrationLeafCount>
        leaves{};

    bool operator==(
        const LearnedOutputCalibrationParameters&) const =
        default;
};

LearnedOutputCalibrationResult
calibrate_learned_value_output_layer(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedWeightedCriticTrainingExample>& examples,
    LearnedOutputCalibrationConfig config = {});

// Exact OC1 persistence seam. Both operations require the frozen
// two-leaf LegacyStateOnly Value topology. Applying parameters deep-clones
// the parent and changes only the 16 hidden-to-output weights and scalar
// output bias in each leaf.
LearnedOutputCalibrationParameters
learned_output_calibration_parameters(
    std::shared_ptr<const LearnedModel> model);
std::shared_ptr<const LearnedModel>
with_learned_output_calibration_parameters(
    std::shared_ptr<const LearnedModel> parent,
    const LearnedOutputCalibrationParameters& parameters);

// Returns an immutable deep clone with a separate, zero-initialized
// DecisionContextV1 input path. The legacy state weights, policy tensors,
// predictions, and source model are unchanged.
std::shared_ptr<const LearnedModel> with_learned_decision_context(
    std::shared_ptr<const LearnedModel> parent);

// Fits only a DecisionContextV1 Value model. State and context inputs remain
// physically separate so legacy model dimensions and artifacts stay stable.
std::shared_ptr<const LearnedModel>
update_learned_contextual_value_model(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedContextualCriticTrainingExample>& examples,
    LearnedValueUpdateConfig config = {});

enum class LearnedPolicyDecisionKind : std::uint8_t {
    Priority,
    Attack,
    Block,
    DamageOrder,
};

struct LearnedPolicyTrainingExample {
    std::vector<std::vector<double>> options;
    std::vector<double> target_probabilities;
    LearnedPolicyDecisionKind decision_kind =
        LearnedPolicyDecisionKind::Priority;
    double weight = 1.0;
};

struct LearnedActorUpdateConfig {
    std::size_t critic_epochs = 1;
    double critic_learning_rate = 0.001;
    std::uint64_t critic_seed = 0;
    std::size_t policy_epochs = 1;
    double policy_learning_rate = 0.001;
    std::uint64_t policy_seed = 0;
};

struct LearnedValuePriorityHeadUpdateConfig {
    std::size_t batch_size = 64;
    std::size_t epochs = 8;
    double learning_rate = 0.001;
    double beta1 = 0.9;
    double beta2 = 0.999;
    double epsilon = 1.0e-8;
    double global_gradient_norm_clip = 5.0;
    std::uint64_t seed = 0;
    double residual_weight = 0.10;
    double policy_temperature = 0.10;

    bool operator==(
        const LearnedValuePriorityHeadUpdateConfig&) const = default;
};

struct LearnedValuePriorityTrainingExample {
    std::vector<std::vector<double>> options;
    // Immutable production Value scores from the frozen parent.
    std::vector<double> base_scores;
    // Soft target over the deployed 90% score-softmax / 10% uniform
    // behavior mixture, not over the bare policy-head softmax.
    std::vector<double> target_probabilities;
    double weight = 1.0;
};

// Exact binary64 parameters for the outer (non-recursive) Priority policy
// head. The input-hidden matrix is stored as hidden-unit rows. This seam is
// intentionally tensor-only: it neither serializes nor changes the critic,
// the other policy heads, or any ensemble members.
struct LearnedPriorityHeadParameters {
    std::vector<std::vector<double>> input_hidden;
    std::vector<double> hidden_bias;
    std::vector<double> hidden_output;
    std::vector<double> direct;
    double output_bias = 0.0;

    bool operator==(
        const LearnedPriorityHeadParameters&) const = default;
};

LearnedPriorityHeadParameters learned_priority_head_parameters(
    std::shared_ptr<const LearnedModel> model);

// Copies `parent`, replaces only its outer Priority head, and publishes the
// copy as immutable. Every tensor must have the exact production dimensions
// and contain only finite values.
std::shared_ptr<const LearnedModel>
with_learned_priority_head_parameters(
    std::shared_ptr<const LearnedModel> parent,
    const LearnedPriorityHeadParameters& parameters);

// Deep-clones a frozen Value model and fits only its outer Priority policy
// head with soft all-action targets. Critic tensors and the Attack, Block,
// and DamageOrder heads are copied bit-for-bit and never updated.
std::shared_ptr<const LearnedModel>
update_learned_value_priority_head(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedValuePriorityTrainingExample>&
        priority_examples,
    LearnedValuePriorityHeadUpdateConfig config = {});

// Evaluation-only tensor-isolation seam. Options are already-neutral policy
// feature vectors, so this exposes no additional game or hidden information.
std::vector<double> learned_policy_head_logits(
    const std::vector<std::vector<double>>& options,
    LearnedPolicyDecisionKind decision_kind,
    std::shared_ptr<const LearnedModel> model);

// Recursively deep-clones `parent`, updates every independently cloned critic
// leaf and the cloned outer policy heads, then republishes the result as
// immutable. Empty example sets perform a pure deep clone.
std::shared_ptr<const LearnedModel> update_learned_actor_model(
    std::shared_ptr<const LearnedModel> parent,
    const std::vector<LearnedCriticTrainingExample>& critic_examples,
    const std::vector<LearnedPolicyTrainingExample>& policy_examples,
    LearnedActorUpdateConfig config);

struct LearnedActorGenerationConfig {
    std::size_t search_worlds = 8;
    std::size_t rollouts_per_world = 1;
    std::size_t horizon_turns = 0;
    std::size_t max_roots_per_seat_kind = 24;
    double td_lambda = 0.90;
    std::size_t critic_epochs = 2;
    double critic_learning_rate = 0.002;
    std::size_t policy_epochs = 2;
    double policy_learning_rate = 0.001;
    std::uint64_t generation = 1;
};

struct LearnedActorGenerationGameReport {
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::array<DeckId, 2> seat_decks = {
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t starting_player = 0;
    std::uint64_t game_seed = 0;
    int winner = -1;
    std::array<std::size_t, 2> priority_roots_by_seat{};
    std::array<std::size_t, 2> attack_roots_by_seat{};
    std::array<std::size_t, 2> attack_includes_by_seat{};
    std::array<double, 2> priority_policy_weight_sums{};
    std::array<double, 2> attack_policy_weight_sums{};
    std::size_t priority_rollout_evaluations = 0;
    std::size_t attack_rollout_evaluations = 0;

    bool operator==(
        const LearnedActorGenerationGameReport&) const = default;
};

struct LearnedPolicyFitDiagnostics {
    std::size_t example_count = 0;
    double total_weight = 0.0;
    // Runtime chooses uniformly among exact model-logit ties. The teacher
    // accepts every exact target maximum, so per-example expected agreement
    // is |model-best intersection teacher-best| / |model-best|.
    double parent_expected_top_one_agreement = 0.0;
    double candidate_expected_top_one_agreement = 0.0;
    double weighted_teacher_entropy = 0.0;
    double parent_weighted_cross_entropy = 0.0;
    double candidate_weighted_cross_entropy = 0.0;
    double parent_excess_cross_entropy = 0.0;
    double candidate_excess_cross_entropy = 0.0;
    std::size_t changed_argmax_examples = 0;
    double changed_argmax_weight = 0.0;
    double changed_argmax_weight_fraction = 0.0;

    bool operator==(
        const LearnedPolicyFitDiagnostics&) const = default;
};

struct LearnedCriticFitDiagnostics {
    std::size_t example_count = 0;
    double target_mean = 0.0;
    double target_variance = 0.0;
    double parent_mean_squared_error = 0.0;
    double candidate_mean_squared_error = 0.0;
    double parent_binary_cross_entropy = 0.0;
    double candidate_binary_cross_entropy = 0.0;

    bool operator==(
        const LearnedCriticFitDiagnostics&) const = default;
};

struct LearnedActorFitDiagnostics {
    LearnedPolicyFitDiagnostics priority;
    LearnedPolicyFitDiagnostics attack;
    LearnedCriticFitDiagnostics critic;

    bool operator==(
        const LearnedActorFitDiagnostics&) const = default;
};

struct LearnedActorGenerationReport {
    std::uint64_t root_seed = 0;
    std::uint64_t generation = 1;
    std::vector<LearnedActorGenerationGameReport> games;
    std::size_t priority_roots = 0;
    std::size_t attack_roots = 0;
    std::size_t priority_rollout_evaluations = 0;
    std::size_t attack_rollout_evaluations = 0;
    std::size_t critic_examples = 0;
    std::size_t priority_policy_examples = 0;
    std::size_t attack_policy_examples = 0;
    std::size_t deduplicated_critic_observations = 0;
    std::size_t replay_generations = 0;
    double minimum_policy_target_sum = 0.0;
    double maximum_policy_target_sum = 0.0;
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    LearnedActorFitDiagnostics fit;

    bool operator==(
        const LearnedActorGenerationReport&) const = default;
};

struct LearnedActorGenerationResult {
    std::shared_ptr<const LearnedModel> model;
    LearnedActorGenerationReport report;
};

// Fits exactly one new immutable generation from one exact 24-game balanced
// frozen-parent mirror block.
LearnedActorGenerationResult train_learned_actor_generation(
    std::shared_ptr<const LearnedModel> parent,
    std::uint64_t root_seed,
    LearnedActorGenerationConfig config = {});

// Evaluates fixed, already-encoded training examples without fitting either
// model. This is also the focused test seam for the generation fit report.
LearnedActorFitDiagnostics diagnose_learned_actor_fit(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const std::vector<LearnedCriticTrainingExample>& critic_examples,
    const std::vector<LearnedPolicyTrainingExample>& policy_examples);

inline constexpr std::size_t kLearnedValueG8Generations = 8;

enum class LearnedValueG8Recipe : std::uint8_t {
    CanonicalAllSearchLate,
    LateMix50,
};

struct LearnedValueGenerationReport {
    // One-based publication index: G1 through G8.
    std::size_t generation = 0;
    std::size_t self_play_games = 0;
    std::size_t generation_examples = 0;
    std::size_t anchor_examples = 0;
    std::size_t replay_generations = 0;
    std::size_t replay_examples = 0;
    // Populated only by the distinct Late-Mix50 recipe. Canonical reports
    // retain zeroes here so their v1 serialized bytes remain unchanged.
    std::size_t raw_collection_games = 0;
    std::size_t search_collection_games = 0;
    std::size_t raw_collection_examples = 0;
    std::size_t search_collection_examples = 0;
    bool search_enabled = false;
    std::size_t search_worlds = 0;
    std::size_t search_horizon_turns = 0;
    std::size_t rollout_evaluations = 0;
    double exploration_rate = 0.0;
    std::string parent_fingerprint;
    std::string candidate_fingerprint;

    bool operator==(
        const LearnedValueGenerationReport&) const = default;
};

struct LearnedValueG8Report {
    // The canonical recipe is the default so legacy aggregate initialization
    // and the canonical v1 loader preserve their exact semantics.
    LearnedValueG8Recipe recipe =
        LearnedValueG8Recipe::CanonicalAllSearchLate;
    std::size_t training_games = 0;
    std::uint64_t root_seed = 0;
    std::size_t base_examples = 0;
    std::string base_fingerprint;
    std::string final_fingerprint;
    std::vector<LearnedValueGenerationReport> generations;

    bool operator==(const LearnedValueG8Report&) const = default;
};

struct LearnedValueG8Result {
    // Convenience alias for checkpoints.back().
    std::shared_ptr<const LearnedModel> model;
    // G0 is the base random-play fit; entries 1..8 are immutable G1..G8.
    std::vector<std::shared_ptr<const LearnedModel>> checkpoints;
    LearnedValueG8Report report;
};

// Canonical immutable reproduction of the bootstrapped Value lead. At the
// default run size it uses an 800-game random anchor and eight 200-game
// frozen-parent mirror generations.
LearnedValueG8Result train_learned_value_g8(
    std::size_t training_games, std::uint64_t seed);

// Single-axis collection repair: base/G1-G4 are canonical, while G5-G8
// alternate exact raw/search game pairs without consuming assignment RNG.
// The derived generation game count must be positive and even.
LearnedValueG8Result train_learned_value_g8_mix50(
    std::size_t training_games, std::uint64_t seed);

// Canonical, versioned frozen-artifact cache for the exact immutable G8
// recipe above. The writer validates the complete report/checkpoint graph
// before atomically replacing `path`; the loader is fail-closed on stale
// metadata, corruption, trailing bytes, or fingerprint mismatches.
std::string learned_value_g8_cache_path(
    std::size_t training_games, std::uint64_t seed);
void write_learned_value_g8_bundle_atomic(
    const std::string& path, const LearnedValueG8Result& result);
LearnedValueG8Result load_learned_value_g8_bundle(
    const std::string& path, std::size_t expected_training_games,
    std::uint64_t expected_seed);

// Separate fail-closed artifact recipe for Late-Mix50. These functions never
// accept or overwrite a canonical Value G8 bundle.
std::string learned_value_g8_mix50_cache_path(
    std::size_t training_games, std::uint64_t seed);
void write_learned_value_g8_mix50_bundle_atomic(
    const std::string& path, const LearnedValueG8Result& result);
LearnedValueG8Result load_learned_value_g8_mix50_bundle(
    const std::string& path, std::size_t expected_training_games,
    std::uint64_t expected_seed);
// Selects immutable published G1..G8 from a canonical bundle. Generation zero
// is deliberately excluded because benchmark G0 is the separate legacy Value
// model, not the bundle's random-anchor base.
std::shared_ptr<const LearnedModel>
learned_value_g8_generation_checkpoint(
    const LearnedValueG8Result& result, std::size_t generation);

// Stable content fingerprint over the model variant, all critic/policy weight
// bit patterns, and recursively serialized ensemble members.
std::string learned_model_fingerprint(
    std::shared_ptr<const LearnedModel> model);
// Stable, bit-exact fingerprints for isolation checks. Each digest binds
// only the named component plus recursive model topology, so an update to
// one policy head cannot obscure a mutation in another head or the critic.
struct LearnedModelComponentFingerprints {
    std::string critic;
    std::string priority;
    std::string attack;
    std::string block;
    std::string damage_order;

    bool operator==(
        const LearnedModelComponentFingerprints&) const = default;
};
LearnedModelComponentFingerprints
learned_model_component_fingerprints(
    std::shared_ptr<const LearnedModel> model);

// Critic-only tensor-group fingerprints for narrow-update isolation checks.
// Each digest also binds recursive ensemble topology and critic schema.
struct LearnedCriticTensorFingerprints {
    std::string input_hidden;
    std::string output_layer;
    std::string direct_paths;

    bool operator==(
        const LearnedCriticTensorFingerprints&) const = default;
};
LearnedCriticTensorFingerprints
learned_critic_tensor_fingerprints(
    std::shared_ptr<const LearnedModel> model);

inline constexpr std::uint64_t
    kTerminalWeightC17ShardSeed = 202607260311ULL;
inline constexpr std::uint64_t
    kTerminalWeightC17HoldoutSeed = 202607260312ULL;
inline constexpr std::uint64_t
    kTerminalWeightC17GameplaySeed = 202607260313ULL;
inline constexpr std::string_view
    kTerminalWeightC17ParentFingerprint =
        "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

struct LearnedTerminalWeightC17DeckReport {
    std::size_t games = 0;
    std::size_t examples = 0;
    std::size_t bootstrapped_examples = 0;
    std::size_t terminal_tail_examples = 0;

    bool operator==(
        const LearnedTerminalWeightC17DeckReport&) const = default;
};

struct LearnedTerminalWeightC17Report {
    std::size_t training_games = 0;
    std::uint64_t parent_training_seed = 0;
    std::size_t parent_generations = 0;
    std::uint64_t shard_seed = 0;
    std::size_t balanced_blocks = 0;
    std::size_t scheduled_games = 0;
    std::size_t bootstrap_distance = 0;
    std::size_t collection_search_worlds = 0;
    std::size_t collection_horizon_turns = 0;
    std::size_t collection_max_game_turns = 0;
    double collection_exploration_rate = 0.0;
    double control_terminal_weight = 0.0;
    double treatment_terminal_weight = 0.0;
    std::size_t fit_epochs = 0;
    double fit_learning_rate = 0.0;
    std::size_t anchor_examples = 0;
    std::size_t penultimate_generation_examples = 0;
    std::size_t last_generation_examples = 0;
    std::size_t historical_replay_examples = 0;
    std::size_t fit_examples = 0;
    std::size_t shard_examples = 0;
    std::size_t bootstrapped_examples = 0;
    std::size_t terminal_tail_examples = 0;
    double maximum_target_delta_error = 0.0;
    std::array<LearnedTerminalWeightC17DeckReport, kDeckCount>
        decks{};
    std::string parent_fingerprint;
    std::string control_fingerprint;
    std::string treatment_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints control_components;
    LearnedModelComponentFingerprints treatment_components;
    std::string anchor_hash;
    std::string penultimate_generation_hash;
    std::string last_generation_hash;
    std::string historical_replay_hash;
    std::string fit_feature_order_hash;
    std::string raw_shard_hash;
    std::string schedule_hash;
    std::string feature_hash;
    std::string outcome_hash;
    std::string control_target_hash;
    std::string treatment_target_hash;

    bool operator==(
        const LearnedTerminalWeightC17Report&) const = default;
};

// Test-scale configurations may reduce the parent generation count, block
// count, and turn bound. The published CLI route fixes the canonical values
// below and requires the exact frozen C16 fingerprint.
struct LearnedTerminalWeightC17Config {
    std::size_t training_games = 800;
    std::uint64_t parent_training_seed =
        kDefaultLearnedTrainingSeed;
    std::size_t parent_generations = 16;
    std::uint64_t shard_seed = kTerminalWeightC17ShardSeed;
    std::size_t balanced_blocks = 5;
    std::size_t max_game_turns = 500;
    std::string required_parent_fingerprint =
        std::string(kTerminalWeightC17ParentFingerprint);
};

class LearnedTerminalWeightC17Artifact {
  public:
    std::shared_ptr<const LearnedModel> control_model() const;
    std::shared_ptr<const LearnedModel> treatment_model() const;
    const LearnedTerminalWeightC17Report& report() const;

  private:
    LearnedTerminalWeightC17Artifact(
        std::shared_ptr<const LearnedModel> control_model,
        std::shared_ptr<const LearnedModel> treatment_model,
        LearnedTerminalWeightC17Report report);

    std::shared_ptr<const LearnedModel> control_model_;
    std::shared_ptr<const LearnedModel> treatment_model_;
    LearnedTerminalWeightC17Report report_;

    friend LearnedTerminalWeightC17Artifact
    train_learned_terminal_weight_c17_family(
        LearnedTerminalWeightC17Config config);
    friend void
    write_learned_terminal_weight_c17_artifact_atomic(
        const std::string& path,
        const LearnedTerminalWeightC17Artifact& artifact);
    friend LearnedTerminalWeightC17Artifact
    load_learned_terminal_weight_c17_artifact(
        const std::string& path,
        std::size_t expected_training_games,
        std::uint64_t expected_parent_training_seed,
        std::uint64_t expected_shard_seed);
};

LearnedTerminalWeightC17Artifact
train_learned_terminal_weight_c17_family(
    LearnedTerminalWeightC17Config config = {});
std::string learned_terminal_weight_c17_cache_path(
    std::size_t training_games,
    std::uint64_t parent_training_seed,
    std::uint64_t shard_seed = kTerminalWeightC17ShardSeed);
void write_learned_terminal_weight_c17_artifact_atomic(
    const std::string& path,
    const LearnedTerminalWeightC17Artifact& artifact);
LearnedTerminalWeightC17Artifact
load_learned_terminal_weight_c17_artifact(
    const std::string& path,
    std::size_t expected_training_games,
    std::uint64_t expected_parent_training_seed,
    std::uint64_t expected_shard_seed =
        kTerminalWeightC17ShardSeed);

inline constexpr std::uint64_t
    kLearnedJointC17ShardSeed = 202607261145ULL;
inline constexpr std::uint64_t
    kLearnedJointC17HoldoutSeed = 202607261146ULL;
inline constexpr std::uint64_t
    kLearnedJointC17MatchedControlGameplaySeed =
        202607261147ULL;
inline constexpr std::uint64_t
    kLearnedJointC17FrozenC16GameplaySeed =
        202607261148ULL;
inline constexpr std::uint64_t
    kLearnedJointC17HandcodedGameplaySeed =
        202607261149ULL;
inline constexpr std::string_view
    kLearnedJointC17ParentFingerprint =
        "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::string_view
    kLearnedJointC17ControlPolicyToken =
        "learned-value-j1-control-c17";
inline constexpr std::string_view
    kLearnedJointC17TreatmentPolicyToken =
        "learned-value-j1-treatment-c17";

enum class LearnedJointC17Arm : std::uint8_t {
    Control,
    Treatment,
};

struct LearnedJointC17PolicyMetadata {
    std::string policy_token;
    std::size_t rollouts_per_action = 0;
    std::size_t horizon_turns = 0;
    double value_continuation_epsilon = 0.0;
    double value_priority_residual_weight = 0.0;
    bool blend_shallow_prior = false;
    bool value_pass_dominance = false;
    LearnedContinuationController continuation_controller =
        LearnedContinuationController::Legacy;

    bool operator==(
        const LearnedJointC17PolicyMetadata&) const = default;
};

struct LearnedJointC17DeckReport {
    std::size_t perspectives = 0;
    std::size_t examples = 0;
    std::size_t control_bootstrapped_examples = 0;
    std::size_t control_terminal_tail_examples = 0;
    std::size_t treatment_bootstrapped_examples = 0;
    std::size_t treatment_terminal_tail_examples = 0;

    bool operator==(
        const LearnedJointC17DeckReport&) const = default;
};

struct LearnedJointC17Report {
    std::size_t training_games = 0;
    std::uint64_t parent_training_seed = 0;
    std::size_t parent_generations = 0;
    std::uint64_t shard_seed = 0;
    std::size_t shard_generation = 0;
    std::size_t balanced_blocks = 0;
    std::size_t scheduled_games = 0;
    std::size_t actor_perspectives = 0;
    std::size_t control_record_bootstrap_distance = 0;
    std::size_t treatment_turn_bootstrap_advances = 0;
    std::size_t collection_search_worlds = 0;
    std::size_t collection_horizon_turns = 0;
    std::size_t collection_max_game_turns = 0;
    double collection_exploration_rate = 0.0;
    double collection_value_continuation_epsilon = 0.0;
    double collection_value_priority_residual_weight = 0.0;
    bool collection_blend_shallow_prior = false;
    bool collection_value_pass_dominance = false;
    LearnedContinuationController
        collection_continuation_controller =
            LearnedContinuationController::Legacy;
    double control_terminal_weight = 0.0;
    double treatment_terminal_weight = 0.0;
    std::size_t fit_epochs = 0;
    double fit_learning_rate = 0.0;
    double fit_example_weight = 0.0;
    std::uint64_t fit_root_seed = 0;
    std::uint64_t fit_member_training_tag = 0;
    std::size_t anchor_examples = 0;
    std::size_t penultimate_generation_examples = 0;
    std::size_t last_generation_examples = 0;
    std::size_t historical_replay_examples = 0;
    std::size_t fit_examples = 0;
    std::size_t shard_examples = 0;
    std::size_t control_bootstrapped_examples = 0;
    std::size_t control_terminal_tail_examples = 0;
    std::size_t treatment_bootstrapped_examples = 0;
    std::size_t treatment_terminal_tail_examples = 0;
    double maximum_control_target_error = 0.0;
    double maximum_treatment_target_error = 0.0;
    std::array<LearnedJointC17DeckReport, kDeckCount> decks{};
    LearnedJointC17PolicyMetadata control_policy;
    LearnedJointC17PolicyMetadata treatment_policy;
    std::string parent_fingerprint;
    std::string control_fingerprint;
    std::string treatment_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints control_components;
    LearnedModelComponentFingerprints treatment_components;
    std::string anchor_hash;
    std::string penultimate_generation_hash;
    std::string last_generation_hash;
    std::string control_historical_replay_hash;
    std::string treatment_historical_replay_hash;
    std::string control_fit_feature_order_hash;
    std::string treatment_fit_feature_order_hash;
    std::string control_fit_order_hash;
    std::string treatment_fit_order_hash;
    std::string control_raw_shard_hash;
    std::string treatment_raw_shard_hash;
    std::string schedule_hash;
    std::string feature_hash;
    std::string outcome_hash;
    std::string turn_number_hash;
    std::string control_future_index_hash;
    std::string treatment_future_index_hash;
    std::string control_target_hash;
    std::string treatment_target_hash;
    std::string control_fit_target_hash;
    std::string treatment_fit_target_hash;

    bool operator==(
        const LearnedJointC17Report&) const = default;
};

struct LearnedJointC17Deployment {
    LearnedJointC17Arm arm = LearnedJointC17Arm::Control;
    std::string policy_token;
    std::shared_ptr<const LearnedModel> model;
    BotConfig bot;
    LearnedSearchConfig search;
};

struct LearnedJointC17Config {
    std::size_t training_games = 800;
    std::uint64_t parent_training_seed =
        kDefaultLearnedTrainingSeed;
    std::size_t parent_generations = 16;
    std::uint64_t shard_seed = kLearnedJointC17ShardSeed;
    std::size_t balanced_blocks = 5;
    std::size_t max_game_turns = 500;
    std::string required_parent_fingerprint =
        std::string(kLearnedJointC17ParentFingerprint);
};

class LearnedJointC17Artifact {
  public:
    std::shared_ptr<const LearnedModel> control_model() const;
    std::shared_ptr<const LearnedModel> treatment_model() const;
    const LearnedJointC17Report& report() const;
    LearnedJointC17Deployment deployment(
        LearnedJointC17Arm arm,
        std::uint64_t search_seed = 0) const;
    LearnedJointC17Deployment control_deployment(
        std::uint64_t search_seed = 0) const;
    LearnedJointC17Deployment treatment_deployment(
        std::uint64_t search_seed = 0) const;

  private:
    LearnedJointC17Artifact(
        std::shared_ptr<const LearnedModel> control_model,
        std::shared_ptr<const LearnedModel> treatment_model,
        LearnedJointC17Report report);

    std::shared_ptr<const LearnedModel> control_model_;
    std::shared_ptr<const LearnedModel> treatment_model_;
    LearnedJointC17Report report_;

    friend LearnedJointC17Artifact
    train_learned_joint_c17_family(
        LearnedJointC17Config config);
    friend void
    write_learned_joint_c17_artifact_atomic(
        const std::string& path,
        const LearnedJointC17Artifact& artifact);
    friend LearnedJointC17Artifact
    load_learned_joint_c17_artifact(
        const std::string& path,
        std::size_t expected_training_games,
        std::uint64_t expected_parent_training_seed,
        std::uint64_t expected_shard_seed);
};

LearnedJointC17Artifact
train_learned_joint_c17_family(
    LearnedJointC17Config config = {});
std::string learned_joint_c17_cache_path(
    std::size_t training_games,
    std::uint64_t parent_training_seed,
    std::uint64_t shard_seed = kLearnedJointC17ShardSeed);
void write_learned_joint_c17_artifact_atomic(
    const std::string& path,
    const LearnedJointC17Artifact& artifact);
LearnedJointC17Artifact
load_learned_joint_c17_artifact(
    const std::string& path,
    std::size_t expected_training_games,
    std::uint64_t expected_parent_training_seed,
    std::uint64_t expected_shard_seed =
        kLearnedJointC17ShardSeed);

struct LearnedValuePolicyFamilyConfig {
    std::size_t generations = 1;
    std::size_t search_worlds = 8;
    std::size_t max_roots_per_actor_game = 32;
    std::size_t max_game_turns = 500;
    // Collection completion order is non-semantic; reduction remains in the
    // fixed 40-game schedule order for every supported worker count.
    std::size_t collection_threads = 4;
    double residual_weight = 0.10;
    double td_lambda = 0.90;
    LearnedValuePriorityHeadUpdateConfig optimizer{};
    // Optional measurement-only fits over each generation's exact immutable
    // replay examples. Every diagnostic starts from the same frozen parent as
    // the production candidate and cannot affect the published checkpoint.
    // Seeds must remain zero here; the generation's indexed PolicyFit seed is
    // substituted identically for every cell.
    std::vector<LearnedValuePriorityHeadUpdateConfig>
        capacity_diagnostic_optimizers;
    // Measurement-only independent-root capacity bracket. This never changes
    // a checkpoint; the canonical diagnostic route enables it only for P1,
    // whose uniform residual makes the oracle's zero-residual parent exact.
    bool compute_rootwise_oracle = false;
    // When nonempty, family construction fails unless P0 has this exact
    // fingerprint. Canonical experiment routes use this to bind the recipe
    // to their preregistered frozen parent.
    std::string required_p0_fingerprint;
};

struct LearnedValuePolicyGameReport {
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::array<DeckId, 2> seat_decks = {
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t starting_player = 0;
    std::uint64_t game_seed = 0;
    int winner = -1;
    std::array<std::size_t, 2> raw_priority_roots{};
    std::array<std::size_t, 2> raw_legal_options{};
    std::array<std::size_t, 2> retained_priority_roots{};
    std::array<std::vector<std::size_t>, 2> retained_ordinals;
    std::array<std::size_t, 2> rollout_evaluations{};
    std::array<double, 2> policy_weight_sums{};

    bool operator==(
        const LearnedValuePolicyGameReport&) const = default;
};

struct LearnedValuePolicyDeckReport {
    std::size_t seat_games = 0;
    std::size_t seat_zero_games = 0;
    std::size_t starting_games = 0;
    std::size_t rootless_actor_games = 0;
    std::size_t raw_priority_roots = 0;
    std::size_t raw_legal_options = 0;
    std::size_t retained_priority_roots = 0;
    std::size_t rollout_evaluations = 0;
    double policy_weight = 0.0;
    double positive_advantage_weight = 0.0;
    double negative_advantage_weight = 0.0;
    double zero_advantage_weight = 0.0;
    double conflict_weight = 0.0;

    bool operator==(
        const LearnedValuePolicyDeckReport&) const = default;
};

struct LearnedValuePolicyMechanismReport {
    std::size_t observation_count = 0;
    std::size_t residual_option_count = 0;
    std::size_t saturated_residual_count = 0;
    double total_weight = 0.0;
    double parent_kl = 0.0;
    double candidate_kl = 0.0;
    bool kl_reduction_defined = false;
    double kl_reduction_fraction = 0.0;
    double positive_advantage_weight = 0.0;
    double negative_advantage_weight = 0.0;
    double zero_advantage_weight = 0.0;
    double conflict_weight = 0.0;
    double eligible_signed_movement_weight = 0.0;
    double correct_signed_movement_weight = 0.0;
    double signed_movement_correct_rate = 0.0;
    double eligible_positive_movement_weight = 0.0;
    double correct_positive_movement_weight = 0.0;
    double positive_movement_correct_rate = 0.0;
    double eligible_negative_movement_weight = 0.0;
    double correct_negative_movement_weight = 0.0;
    double negative_movement_correct_rate = 0.0;
    double changed_argmax_weight = 0.0;
    double changed_argmax_weight_fraction = 0.0;
    double residual_option_weight = 0.0;
    double saturated_residual_weight = 0.0;
    double residual_saturation_fraction = 0.0;

    bool operator==(
        const LearnedValuePolicyMechanismReport&) const = default;
};

struct LearnedValuePolicyCapacityDiagnosticReport {
    LearnedValuePriorityHeadUpdateConfig optimizer;
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    LearnedValuePolicyMechanismReport mechanism;
    std::array<LearnedValuePolicyMechanismReport, kDeckCount>
        mechanisms_by_deck;

    bool operator==(
        const LearnedValuePolicyCapacityDiagnosticReport&) const =
        default;
};

struct LearnedValuePolicyRootwiseOracleBoundReport {
    double numerical_best_kl = 0.0;
    double achievable_reduction_fraction = 0.0;
    double certified_kl_lower_bound = 0.0;
    double certified_reduction_upper_bound = 0.0;
    double maximum_abs_squashed_residual = 0.0;
    std::size_t roots_reaching_iteration_limit = 0;

    bool operator==(
        const LearnedValuePolicyRootwiseOracleBoundReport&) const =
        default;
};

struct LearnedValuePolicyRootwiseOracleReport {
    std::size_t observation_count = 0;
    double total_weight = 0.0;
    double parent_kl = 0.0;
    bool reduction_defined = false;
    LearnedValuePolicyRootwiseOracleBoundReport full_range;
    LearnedValuePolicyRootwiseOracleBoundReport zero_saturation;

    bool operator==(
        const LearnedValuePolicyRootwiseOracleReport&) const =
        default;
};

struct LearnedValuePolicyGenerationReport {
    std::uint64_t root_seed = 0;
    std::size_t generation = 0;
    bool canonical_recipe = false;
    std::size_t search_worlds = 0;
    std::size_t rollouts_per_world = 1;
    std::size_t search_horizon_turns = 4;
    std::size_t max_roots_per_actor_game = 0;
    std::size_t max_game_turns = 0;
    std::size_t collection_threads = 0;
    double residual_weight = 0.0;
    double td_lambda = 0.0;
    LearnedValuePriorityHeadUpdateConfig optimizer;
    std::vector<LearnedValuePolicyGameReport> games;
    std::array<LearnedValuePolicyDeckReport, kDeckCount> decks;
    std::size_t raw_priority_roots = 0;
    std::size_t raw_legal_options = 0;
    std::size_t retained_priority_roots = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t rootless_actor_games = 0;
    double policy_weight = 0.0;
    std::size_t replay_generations = 0;
    std::size_t replay_examples = 0;
    double minimum_target_sum = 0.0;
    double maximum_target_sum = 0.0;
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    LearnedValuePolicyMechanismReport mechanism;
    // Empty for normal P-family training. Populated only when explicitly
    // requested to measure in-sample head capacity on the already-collected
    // shard; these models are never returned as family checkpoints.
    std::vector<LearnedValuePolicyCapacityDiagnosticReport>
        capacity_diagnostics;
    std::optional<LearnedValuePolicyRootwiseOracleReport>
        rootwise_oracle;

    bool operator==(
        const LearnedValuePolicyGenerationReport&) const = default;
};

struct LearnedValuePolicyFamilyResult {
    // P0 is checkpoints.front(); subsequent entries are P1 through Pn.
    std::vector<std::shared_ptr<const LearnedModel>> checkpoints;
    std::vector<LearnedValuePolicyGenerationReport> reports;
};

// Trains immutable P-family generations from a frozen Value parent. Each
// generation collects the complete 40-game, five-deck mirror schedule with
// indexed information-set search and fits only the bounded Priority residual.
LearnedValuePolicyFamilyResult train_learned_value_policy_family(
    std::shared_ptr<const LearnedModel> p0,
    std::uint64_t root_seed,
    LearnedValuePolicyFamilyConfig config = {});
// Observation presented to Learned: own private zones plus public
// information, never the opponent's hidden card identities.
std::vector<double>
learned_observation(const GameState& state, std::size_t perspective);
// Diagnostic-only candidate planes for public graveyard order. The deployed
// C16 observation remains unchanged until a separately gated model-schema
// experiment adopts them.
inline constexpr std::size_t kLearnedGraveyardOrderFeatureCount =
    2 * kCardCount;
using LearnedGraveyardOrderFeatures =
    std::array<double, kLearnedGraveyardOrderFeatureCount>;
LearnedGraveyardOrderFeatures learned_graveyard_order_features(
    const GameState& state, std::size_t perspective);
// Evaluation/debug seam containing the unchanged state observation followed
// by the fixed neutral context vector. It is not consumed by legacy policies.
std::vector<double> learned_contextual_observation(
    const GameState& state, std::size_t perspective,
    const LearnedDecisionContext& context);
LearnedCriticSchema learned_critic_schema(
    std::shared_ptr<const LearnedModel> model);
double learned_contextual_critic_value(
    const GameState& state, std::size_t perspective,
    const LearnedDecisionContext& context,
    std::shared_ptr<const LearnedModel> model);
std::vector<double> learned_priority_policy_features(
    const GameState& state, std::size_t perspective,
    const PriorityAction& action, bool sorcery_actions,
    TurnPhase phase, int consecutive_passes);
std::vector<double>
learned_soft_priority_target(const std::vector<double>& scores);

struct DeckEvolutionConfig {
    std::size_t generations = 10;
    std::size_t population = 16;
    std::size_t repetitions_per_opponent = 4;
    BotConfig pilot = {
        .kind = BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
};

struct EvolvedDeck {
    std::vector<CardId> cards;
    DeckSimulationStats total;
    std::array<DeckSimulationStats, kDeckCount> by_opponent;
};

struct DeckEvolutionSummary {
    EvolvedDeck best;
    std::vector<double> generation_best_win_rates;
};

DeckEvolutionSummary
evolve_deck(DeckEvolutionConfig config, std::uint64_t seed,
            GameConfig game_config = {});

} // namespace old_school
