#pragma once

// Self-Play Zero (SPZ): a general, self-taught bot.
//
// SPZ learns a value network purely from mirror self-play outcomes and acts
// greedily over one-step afterstates computed with the engine's public,
// rules-only transitions. It consumes only perspective-safe observations
// (PlayerObservation plus both original decklists), never hidden opponent
// state, and never uses hand-authored card values or card-specific policy
// switches. It attaches to a game seat through HumanController callbacks.

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace old_school::selfplay_zero {

inline constexpr std::size_t kSpzDeckCount = 5;

// The five-deck metagame environment, in canonical order.
const std::array<std::vector<CardId>, kSpzDeckCount>& spz_decks();
std::string_view spz_deck_name(std::size_t deck_index);

// ---------------------------------------------------------------------------
// Features

inline constexpr std::size_t kSpzPhaseCount = 7;

std::size_t spz_feature_count();

// Perspective-safe feature vector for `observation.observer`. `phase` is the
// decision/evaluation phase context, which the observation itself does not
// carry. `original_decks` provides both public decklists so the extractor can
// derive the observer's remaining library pool and the opponent's unseen
// (hand + library) pool; identities inside those hidden zones are never read.
std::vector<float> spz_features(
    const PlayerObservation& observation,
    const std::array<std::vector<CardId>, 2>& original_decks,
    TurnPhase phase);

// Rebuilds a full GameState from a perspective-safe observation. Hidden zones
// (the opponent's hand and both libraries) are filled with placeholder cards
// of the correct sizes; callers must pass the result through
// sample_determinization before any transition that reads hidden identities.
GameState reconstruct_observed_state(const PlayerObservation& observation);

// ---------------------------------------------------------------------------
// Value network

class SpzNet {
  public:
    SpzNet(std::size_t inputs, std::size_t hidden, std::uint64_t seed);

    std::size_t input_count() const { return inputs_; }
    std::size_t hidden_count() const { return hidden_; }

    // Win probability in [0, 1] for the perspective the features encode.
    double value(const std::vector<float>& features) const;

    // One SGD-with-momentum minibatch step on binary cross-entropy.
    // Returns the mean pre-update loss of the batch.
    double train_batch(const std::vector<const std::vector<float>*>& features,
                       const std::vector<float>& targets,
                       double learning_rate);

    void save(std::ostream& out) const;
    static SpzNet load(std::istream& in);

  private:
    SpzNet() = default;

    std::size_t inputs_ = 0;
    std::size_t hidden_ = 0;
    // Row-major hidden x inputs, then hidden biases, then output weights and
    // a single output bias. Momentum buffers mirror the parameter layout.
    std::vector<double> hidden_weights_;
    std::vector<double> hidden_bias_;
    std::vector<double> output_weights_;
    double output_bias_ = 0.0;
    std::vector<double> momentum_;
};

void save_spz_net(const SpzNet& net, const std::string& path);
SpzNet load_spz_net(const std::string& path);

// ---------------------------------------------------------------------------
// Policy / controller

struct SpzPolicyConfig {
    // Determinized worlds averaged per priority-action evaluation.
    std::size_t worlds = 2;
    // Worlds averaged when predicting opponent blocks during attack search.
    std::size_t block_prediction_worlds = 2;
    // Epsilon-greedy exploration over priority actions and combat choices.
    // Zero is the deterministic greedy policy used for evaluation.
    double epsilon = 0.0;
    std::uint64_t seed = 1;
    // Decision-time lookahead: score root candidates by playing each
    // determinized world forward with a cheap greedy mirror policy until the
    // start of the deciding seat's next turn, evaluating the network there.
    bool rollout = false;
    // Priority candidates advanced to rollout scoring (myopic preranking).
    std::size_t rollout_top_k = 5;
    // How many of the deciding seat's future turn starts the rollout plays
    // through before evaluating. 1 stops at the seat's next turn start; 2
    // additionally plays that turn and the following opponent turn.
    std::size_t rollout_turn_cycles = 1;
    // Priority-decision search mode. GreedyRollout is the deployed champion
    // behavior. Ismcts searches a per-world tree over both players' priority
    // decisions (combat stays on the greedy machinery) with value-net
    // leaves, myopic priors, and PUCT selection.
    enum class Search : std::uint8_t { GreedyRollout, Ismcts };
    Search search = Search::GreedyRollout;
    // Total tree simulations per decision, split across the sampled worlds.
    std::size_t ismcts_iterations = 160;
    // Rules-only prune of real-root priority actions that are strictly
    // dominated by Pass (identical settled state, strictly more of the
    // actor's resources consumed) — e.g. an X=0 Braingeyser. No card
    // knowledge; comparisons that fail to settle retain the action.
    bool pass_dominance_prune = true;
};

// Outcome-labeled training example. Targets are filled in after the game.
struct SpzSample {
    std::vector<float> features;
    float target = 0.5f;
};

// Per-seat recording sink for self-play training.
struct SpzRecorder {
    std::vector<std::vector<float>> feature_rows;
};

// Builds the full five-callback controller for `seat`. `recorder` may be null
// outside training. The controller owns an independent RNG stream seeded from
// `config.seed`; a fixed (net, decks, seat, config) tuple replays the same
// decisions for the same game.
HumanController make_spz_controller(
    std::shared_ptr<const SpzNet> net,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t seat, const SpzPolicyConfig& config,
    SpzRecorder* recorder = nullptr);

// ---------------------------------------------------------------------------
// Training

// Deterministic, card-agnostic self-play schedule. Consecutive coordinates
// cycle through every ordered deck pairing, and consecutive repetitions of a
// pairing alternate play/draw. A game count divisible by 50 is therefore
// exactly balanced by ordered deck pairing, physical game seat, and
// play/draw in every iteration. Mirror training has no challenger/baseline
// policy seat; randomized league activation and snapshot-seat assignment are
// seeded stochastic variation, not an exact-balance claim.
struct SpzTrainingCoordinate {
    std::size_t deck_zero = 0;
    std::size_t deck_one = 0;
    std::size_t pairing_repetition = 0;
    std::size_t starting_player = 0;

    bool operator==(const SpzTrainingCoordinate&) const = default;
};

SpzTrainingCoordinate spz_training_coordinate(
    std::size_t iteration, std::size_t games_per_iteration,
    std::size_t game_index);

struct SpzTrainConfig {
    std::size_t iterations = 60;
    std::size_t games_per_iteration = 128;
    std::size_t hidden = 64;
    std::uint64_t seed = 20260729;
    std::size_t max_turns = 120;
    std::size_t training_worlds = 2;
    // Generate self-play games with rollout lookahead (slower, stronger
    // play; used to fine-tune a net toward search-improved targets).
    bool rollout = false;
    // Self-play with the ISMCTS contender search instead of greedy rollout
    // scoring, so the value net learns the trajectories that search
    // produces (search/net co-training).
    bool ismcts = false;
    std::size_t ismcts_iterations = 96;
    double epsilon_start = 0.25;
    double epsilon_final = 0.03;
    double learning_rate = 0.01;
    std::size_t batch_size = 64;
    // Gradient steps per iteration = replay_passes * new_samples / batch.
    double replay_passes = 2.0;
    std::size_t replay_capacity = 150000;
    std::size_t threads = 1;
    // Optional warm start; when set, `hidden` is ignored and training
    // continues from this network's parameters.
    std::shared_ptr<const SpzNet> initial_net;
    // League play: with this probability a game's second seat is driven by
    // a uniformly chosen past snapshot instead of the current network,
    // countering mirror-only self-play drift. Zero restores pure mirrors.
    double league_probability = 0.5;
    std::size_t league_snapshot_interval = 10;
    std::size_t league_pool_size = 8;
    // Length-discounted outcome targets (the engine's canonical soft label)
    // instead of raw 0/0.5/1 outcomes.
    bool discounted_targets = true;
    // When nonempty, saves "<prefix><iteration>.txt" checkpoints.
    std::string checkpoint_prefix;
    std::size_t checkpoint_interval = 25;
    // Optional progress line sink (already newline-free).
    std::function<void(const std::string&)> log;
};

std::shared_ptr<SpzNet> train_spz(const SpzTrainConfig& config);

// ---------------------------------------------------------------------------
// Benchmark

struct SpzDeckStats {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;

    double win_rate() const;
};

struct SpzBenchmarkResult {
    // Indexed by the SPZ seat's deck in spz_decks() order.
    std::array<SpzDeckStats, kSpzDeckCount> per_deck;
    // [spz deck][baseline deck] slice of the same games.
    std::array<std::array<SpzDeckStats, kSpzDeckCount>, kSpzDeckCount>
        matchups;
    SpzDeckStats aggregate;
    double wilson_lower_bound_95 = 0.0;

    // Win rate of the baseline bot when *it* holds `deck` against the SPZ
    // field — the pilot-skill comparison partner for per_deck[deck].
    double baseline_deck_win_rate(std::size_t deck) const;
};

// Deck-, seat-, and play/draw-balanced paired benchmark of SPZ against a
// baseline engine bot. Every (spz deck, opponent deck, repetition) triple
// plays two games with identical seed and identical SPZ play/draw role,
// swapping only the seats. Draws count half a win in the Wilson bound.
// When `baseline_spz_policy` is set the baseline seat is driven by a second
// SPZ controller (same net, that policy) instead of an engine bot — the
// contender-versus-champion gate.
SpzBenchmarkResult run_spz_benchmark(
    std::shared_ptr<const SpzNet> net, BotKind baseline,
    std::size_t repetitions_per_pairing, std::uint64_t seed,
    const SpzPolicyConfig& policy, std::size_t max_turns = 200,
    std::size_t threads = 1,
    const std::function<void(const std::string&)>& log = {},
    const SpzPolicyConfig* baseline_spz_policy = nullptr,
    std::shared_ptr<const SpzNet> baseline_net = nullptr);

}  // namespace old_school::selfplay_zero
