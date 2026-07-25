#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

namespace alpha {

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
};

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
};

const CardDefinition& card_definition(CardId card);

std::vector<CardId> green_alpha_deck();
std::vector<CardId> red_alpha_deck();
std::vector<CardId> blue_alpha_deck();
std::vector<CardId> white_control_deck();

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

enum class PriorityActionKind : std::uint8_t {
    Pass,
    PlayLand,
    CastCreature,
    CastSorcery,
    CastArtifact,
    CastEnchantment,
    CastLightningBolt,
    CastCounterspell,
    ActivateMillstone,
};

struct PriorityAction {
    PriorityActionKind kind = PriorityActionKind::Pass;
    CardId card = CardId::Forest;
    std::optional<Target> target;
    std::optional<StackObjectId> spell_target;
    std::optional<PermanentId> source_permanent;

    static PriorityAction pass();
    static PriorityAction play_land(CardId land);
    static PriorityAction cast_creature(CardId creature);
    static PriorityAction cast_sorcery(CardId sorcery);
    static PriorityAction cast_artifact(CardId artifact);
    static PriorityAction cast_enchantment(CardId enchantment);
    static PriorityAction cast_lightning_bolt(Target bolt_target);
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

inline constexpr std::size_t kBotKindCount = 5;
inline constexpr std::size_t kBotMatchupCount =
    kBotKindCount * (kBotKindCount - 1) / 2;

class LearnedModel;

struct BotConfig {
    BotKind kind = BotKind::Random;
    // Complete random continuations sampled for every legal action.
    std::size_t rollouts_per_action = 2;
    double exploration_rate = 0.0;
    std::size_t training_games = 800;
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
    std::shared_ptr<const LearnedModel> learned_model;
};

class Game {
  public:
    Game(std::vector<CardId> player_zero_deck,
         std::vector<CardId> player_one_deck, std::uint64_t seed,
         GameConfig config = {});

    GameResult run();
    GameResult run_with_trace(std::vector<GameState>& trace);
    const GameState& state() const;

  private:
    void initialize();
    bool draw_card(std::size_t player);
    std::optional<GameResult>
    play_priority_window(bool sorcery_actions);
    std::optional<GameResult>
    continue_priority_window(bool sorcery_actions,
                             PriorityState priority);
    std::optional<GameResult> play_combat();
    PriorityAction
    choose_priority_action(const std::vector<PriorityAction>& actions,
                           std::size_t player, bool sorcery_actions);
    PriorityAction
    choose_handcrafted_action(const std::vector<PriorityAction>& actions,
                              std::size_t player);
    PriorityAction
    choose_learned_action(const std::vector<PriorityAction>& actions,
                          std::size_t player, bool sorcery_actions);
    double learned_rollout_action(const PriorityAction& action,
                                  std::size_t player,
                                  bool sorcery_actions,
                                  std::uint64_t seed) const;
    double handcrafted_action_score(const PriorityAction& action,
                                    std::size_t player) const;
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
};

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
    std::array<DeckSimulationStats, 4> decks;
    std::array<std::array<DeckSimulationStats, kBotKindCount>, 4>
        deck_bots;
    std::array<BotSimulationStats, kBotKindCount> bots;
    std::array<BotMatchupStats, kBotMatchupCount> bot_matchups;
    std::array<MatchupSummary, 6> matchups;
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
    std::array<DeckLiftComparison, 4> decks;

    bool complete() const;
    bool learned_is_best_on_every_deck() const;
};

std::string_view deck_name(DeckId deck);
std::string_view deck_list(DeckId deck);
std::string_view bot_name(BotKind bot);
LearnedDeckLiftSummary
compare_learned_deck_lifts(const TournamentSummary& summary);
TournamentSummary run_tournament(std::size_t games_per_matchup,
                                 std::uint64_t seed,
                                 GameConfig game_config = {},
                                 TournamentConfig tournament_config = {});

struct BotBenchmarkSummary {
    BotConfig challenger;
    BotConfig baseline;
    std::size_t repetitions_per_deck_pairing = 0;
    std::size_t total_games = 0;
    BotSimulationStats challenger_stats;
    BotSimulationStats baseline_stats;
    std::array<DeckSimulationStats, 4> challenger_decks;
    std::array<DeckSimulationStats, 4> baseline_decks;

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
    std::array<DeckSimulationStats, 4> by_opponent;
};

struct DeckEvolutionSummary {
    EvolvedDeck best;
    std::vector<double> generation_best_win_rates;
};

DeckEvolutionSummary
evolve_deck(DeckEvolutionConfig config, std::uint64_t seed,
            GameConfig game_config = {});

} // namespace alpha
