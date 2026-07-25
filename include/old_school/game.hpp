#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
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
};

inline constexpr std::size_t kCardCount =
    static_cast<std::size_t>(CardId::GiantGrowth) + 1;

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
};

struct ArtifactPermanent {
    PermanentId id;
    CardId card;
    bool tapped = false;
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
    bool land_played_this_turn = false;
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
};

struct GameState {
    std::array<PlayerState, 2> players;
    std::array<PlayerGameStats, 2> stats;
    std::vector<StackObject> stack;
    std::size_t active_player = 0;
    std::size_t starting_player = 0;
    std::size_t turn_number = 0;
    PermanentId next_permanent_id = 1;
    StackObjectId next_stack_object_id = 1;
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
bool resolve_top_of_stack(GameState& state);

struct PriorityState {
    std::size_t player = 0;
    int consecutive_passes = 0;
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

enum class PriorityPassResult : std::uint8_t {
    Passed,
    StackObjectResolved,
    WindowEnded,
};

PriorityPassResult pass_priority(GameState& state,
                                 PriorityState& priority);

// Attackers and blockers are identified by permanent ID. A repeated attacker in
// blocks represents multiple creatures blocking it; block order is damage order.
bool resolve_combat(
    GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>& blocks);

void begin_turn(GameState& state, std::size_t player);
void cleanup_turn(GameState& state);

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

inline constexpr std::size_t kBotKindCount = 5;
inline constexpr std::size_t kBotMatchupCount =
    kBotKindCount * (kBotKindCount - 1) / 2;

class LearnedModel;
class LearnedPolicyRecorder;

inline constexpr std::uint64_t kDefaultLearnedTrainingSeed = 424242;

struct BotConfig {
    BotKind kind = BotKind::Random;
    // `learned` is the frozen value-search champion. The unified actor remains
    // an explicit research challenger and is never selected implicitly.
    LearnedVariant learned_variant = LearnedVariant::ValueSearchChampion;
    // Complete random continuations sampled for every legal action.
    std::size_t rollouts_per_action = 2;
    double exploration_rate = 0.0;
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
};

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
    // Training-only sink. The concrete recorder is intentionally opaque so
    // runtime callers cannot inspect or provide hidden game state.
    std::shared_ptr<LearnedPolicyRecorder> learned_policy_recorder;
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
    std::vector<PriorityAction> actions;
    std::vector<double> scores;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
};

// Configuration for evaluation-only information-set action scoring. Worlds
// are sampled once from the root player's observation, then every candidate
// is evaluated with the same world and continuation-seed matrix. Nested root
// search is always disabled inside continuations.
struct LearnedSearchConfig {
    std::uint64_t seed = 0;
    std::size_t worlds = 1;
    std::size_t rollouts_per_world = 1;
    // Zero finishes the current turn, begins/draws the next turn, and
    // bootstraps before priority. A positive value plays exactly that many
    // complete future turns and bootstraps after the final cleanup.
    std::size_t horizon_turns = 4;
    LearnedVariant continuation_variant = LearnedVariant::UnifiedActor;
    // Reproduces the deployed Value selector's one aggregate shallow-prior
    // observation blended with all continuation samples.
    bool blend_shallow_prior = false;
};

struct LearnedActionSamples {
    // Outer order matches the caller's candidate order. Inner samples are
    // flattened world-major, then rollout-major, and are paired across every
    // candidate.
    std::vector<std::vector<double>> q_samples;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
};

struct LearnedValueAttackSetScores {
    std::vector<double> scores;
    // The deployed selector retains the first candidate on an exact tie.
    std::size_t selected_candidate = 0;
};

// Evaluation-only view of the deployed Value attack-set selector. `seed`
// initializes the RNG exactly at the block-candidate enumeration boundary;
// candidates are scored in caller order with the unchanged deployed
// block-sampling distribution.
LearnedValueAttackSetScores learned_value_attack_set_scores(
    const GameState& state, std::size_t attacking_player,
    const std::vector<std::vector<PermanentId>>& candidates,
    std::shared_ptr<const LearnedModel> model, std::uint64_t seed);

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
double learned_critic_value(
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
    std::size_t rollouts_per_action, std::uint64_t seed);

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
        std::size_t rollouts_per_action, std::uint64_t seed);
    friend LearnedActionSamples learned_priority_action_samples(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t player, bool sorcery_actions, TurnPhase phase,
        int consecutive_passes,
        const std::vector<PriorityAction>& candidates,
        std::shared_ptr<const LearnedModel> model,
        LearnedSearchConfig config);
    friend LearnedActionSamples learned_binary_attack_samples(
        const GameState& state,
        const std::array<std::vector<CardId>, 2>& original_decks,
        std::size_t attacking_player,
        const std::vector<PermanentId>& selected_attackers,
        PermanentId subject,
        const std::vector<PermanentId>& remaining_attackers,
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
    double finish_learned_evaluation_horizon(
        std::size_t perspective, std::size_t horizon_turns);
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
        bool sorcery_actions, int consecutive_passes,
        const GameState& sampled_state) const;
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
    GameResult run_from_turn(std::size_t first_turn);
    std::optional<GameResult> life_total_result() const;
    GameResult make_result(int winner, EndReason reason) const;

    std::array<std::vector<CardId>, 2> decks_;
    std::mt19937_64 random_;
    GameConfig config_;
    GameState state_;
    std::optional<GameResult> setup_result_;
    std::vector<GameState>* trace_ = nullptr;
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
    std::size_t learned_training_games = 800;
};

struct MatchupSummary {
    DeckId first_deck;
    DeckId second_deck;
    SimulationSummary result;
};

struct TournamentSummary {
    std::size_t games_per_matchup = 0;
    std::size_t total_games = 0;
    std::uint64_t learned_training_seed =
        kDefaultLearnedTrainingSeed;
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

struct BotBenchmarkSummary {
    BotConfig challenger;
    BotConfig baseline;
    std::uint64_t learned_training_seed =
        kDefaultLearnedTrainingSeed;
    std::size_t repetitions_per_deck_pairing = 0;
    std::size_t total_games = 0;
    BotSimulationStats challenger_stats;
    BotSimulationStats baseline_stats;
    std::array<DeckSimulationStats, kDeckCount> challenger_decks;
    std::array<DeckSimulationStats, kDeckCount> baseline_decks;

    double challenger_win_rate() const;
    double confidence_low_95() const;
    double confidence_high_95() const;
    bool challenger_is_better_95() const;
};

BotBenchmarkSummary
run_bot_benchmark(std::size_t repetitions_per_deck_pairing,
                  std::uint64_t seed, BotConfig challenger,
                  BotConfig baseline, GameConfig game_config = {});

std::shared_ptr<const LearnedModel>
train_learned_model(std::size_t training_games, std::uint64_t seed);
std::shared_ptr<const LearnedModel>
train_learned_value_champion(std::size_t training_games,
                            std::uint64_t seed);
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
// Observation presented to Learned: own private zones plus public
// information, never the opponent's hidden card identities.
std::vector<double>
learned_observation(const GameState& state, std::size_t perspective);
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
