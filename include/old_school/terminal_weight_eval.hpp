#pragma once

#include "old_school/game.hpp"
#include "old_school/learned_iteration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace old_school::terminal_weight_eval {

inline constexpr double kNormal95CriticalValue =
    1.959963984540054;
inline constexpr std::size_t kHoldoutGeneration = 17;
inline constexpr std::size_t kHoldoutBalancedBlocks = 5;
inline constexpr std::size_t kHoldoutPhysicalGames =
    kHoldoutBalancedBlocks *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kHoldoutPerspectivesPerDeck = 80;
inline constexpr std::size_t kGameplayQuartetsPerDeck = 50;
inline constexpr std::size_t kGameplayGamesPerDeck =
    4 * kGameplayQuartetsPerDeck;
inline constexpr std::size_t kGameplayTotalGames =
    kDeckCount * kGameplayGamesPerDeck;
inline constexpr std::size_t kGameplayMinimumWins = 501;

enum class CriticModel : std::uint8_t {
    ParentC16,
    TW50,
    TW75,
};

inline constexpr std::size_t kCriticModelCount = 3;

struct ArtifactSnapshot {
    std::string path;
    std::uintmax_t size = 0;
    std::int64_t modification_time_ticks = 0;
    std::string content_hash;

    bool operator==(const ArtifactSnapshot&) const = default;
};

// Reads the entire artifact and binds its path, byte count, file-clock
// timestamp, and deterministic content digest. Missing or changing files fail
// closed.
ArtifactSnapshot snapshot_artifact(const std::string& path);

struct ClusteredValue {
    std::size_t cluster = 0;
    double value = 0.0;
};

struct ClusteredEstimate {
    std::size_t records = 0;
    std::size_t clusters = 0;
    double mean = 0.0;
    double standard_error = 0.0;
    double confidence_lower_95 = 0.0;
    double confidence_upper_95 = 0.0;

    bool operator==(const ClusteredEstimate&) const = default;
};

// Record-weighted mean with physical-game CR1 uncertainty:
//   G/(G-1) * sum_g(sum_i(x_i - x_bar))^2 / N^2.
ClusteredEstimate cr1_clustered_estimate(
    std::span<const ClusteredValue> values);

struct HoldoutTask {
    std::size_t physical_game = 0;
    learned_iteration::ScheduledGame scheduled;

    bool operator==(const HoldoutTask&) const = default;
};

std::vector<HoldoutTask> holdout_schedule(
    std::uint64_t seed, std::size_t generation,
    std::size_t balanced_blocks);

struct HoldoutRecord {
    std::size_t physical_game = 0;
    DeckId deck = DeckId::Green;
    double discounted_terminal_target = 0.5;
    std::array<double, kCriticModelCount> predictions{};
    // Present only when trace record i has an i+4 bootstrap record.
    std::optional<std::size_t> trace_turn_distance;

    bool operator==(const HoldoutRecord&) const = default;
};

struct HoldoutCollectionConfig {
    std::uint64_t seed = 0;
    std::size_t generation = 0;
    std::size_t balanced_blocks = 0;
    std::size_t max_game_turns = 500;
    std::size_t pilot_training_games = 800;

    bool operator==(
        const HoldoutCollectionConfig&) const = default;
};

// Runs a frozen-parent Learned mirror in both seats (K=1/H=4,
// exploration=0.05, Legacy continuation, PD0 off), then scores every traced
// state from both perspectives with parent/control/treatment critics. The
// schedule and output order are deterministic for a fixed configuration.
std::vector<HoldoutRecord> collect_holdout_records(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> control,
    std::shared_ptr<const LearnedModel> treatment,
    HoldoutCollectionConfig config,
    std::ostream& progress);

struct CriticMetrics {
    ClusteredEstimate brier;
    ClusteredEstimate soft_log_loss;
    ClusteredEstimate signed_bias;
    double prediction_mean = 0.0;
    std::size_t saturated_predictions = 0;
    double saturation_fraction = 0.0;

    bool operator==(const CriticMetrics&) const = default;
};

struct CriticComparison {
    ClusteredEstimate brier_delta;
    ClusteredEstimate soft_log_loss_delta;

    bool operator==(const CriticComparison&) const = default;
};

struct HoldoutScopeMetrics {
    std::size_t records = 0;
    std::size_t perspectives = 0;
    std::size_t physical_games = 0;
    double target_mean = 0.0;
    double target_variance = 0.0;
    std::size_t bootstrapped_records = 0;
    std::size_t terminal_tail_records = 0;
    double mean_trace_turn_distance = 0.0;
    std::size_t minimum_trace_turn_distance = 0;
    std::size_t maximum_trace_turn_distance = 0;
    std::array<CriticMetrics, kCriticModelCount> models{};
    // TW75 minus TW50, then TW75 minus frozen C16.
    std::array<CriticComparison, 2> treatment_comparisons{};

    bool operator==(const HoldoutScopeMetrics&) const = default;
};

struct HoldoutReport {
    HoldoutScopeMetrics pooled;
    std::array<HoldoutScopeMetrics, kDeckCount> by_deck{};

    bool operator==(const HoldoutReport&) const = default;
};

HoldoutReport score_holdout_records(
    std::span<const HoldoutRecord> records);

struct OfflineGateConfig {
    std::size_t expected_physical_games =
        kHoldoutPhysicalGames;
    std::size_t expected_perspectives_per_deck =
        kHoldoutPerspectivesPerDeck;
};

struct OfflineGateReport {
    bool accounting_exact = false;
    bool pooled_losses_improved = false;
    bool per_deck_loss_guard = false;
    bool green_bias_shrank = false;
    bool blue_bias_shrank = false;
    bool no_new_material_bias = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const OfflineGateReport&) const = default;
};

OfflineGateReport evaluate_offline_gate(
    const HoldoutReport& report,
    OfflineGateConfig config = {});

struct GameplayTask {
    DeckId deck = DeckId::Green;
    std::size_t quartet = 0;
    std::size_t challenger_player = 0;
    std::size_t starting_player = 0;
    std::uint64_t seed = 0;

    bool operator==(const GameplayTask&) const = default;
};

std::vector<GameplayTask> same_deck_gameplay_schedule(
    std::uint64_t seed, std::size_t quartets_per_deck);

struct GameplayOutcome {
    DeckId deck = DeckId::Green;
    std::size_t quartet = 0;
    bool challenger_won = false;
    bool baseline_won = false;

    bool operator==(const GameplayOutcome&) const = default;
};

struct GameplayDeckReport {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
    ClusteredEstimate score;

    bool operator==(const GameplayDeckReport&) const = default;
};

struct GameplayPanelReport {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
    ClusteredEstimate score;
    std::array<GameplayDeckReport, kDeckCount> by_deck{};

    bool operator==(const GameplayPanelReport&) const = default;
};

GameplayPanelReport score_gameplay_outcomes(
    std::span<const GameplayOutcome> outcomes);

struct GameplayGateConfig {
    std::size_t expected_total_games = kGameplayTotalGames;
    std::size_t expected_games_per_deck =
        kGameplayGamesPerDeck;
    std::size_t expected_quartets_per_deck =
        kGameplayQuartetsPerDeck;
    std::size_t minimum_aggregate_wins =
        kGameplayMinimumWins;
};

struct GameplayGateReport {
    bool accounting_exact = false;
    bool aggregate_wins_passed = false;
    bool every_deck_non_losing = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const GameplayGateReport&) const = default;
};

GameplayGateReport evaluate_gameplay_gate(
    const GameplayPanelReport& report,
    GameplayGateConfig config = {});

struct StageDecision {
    bool run_panel_one = false;
    bool run_panel_two = false;

    bool operator==(const StageDecision&) const = default;
};

StageDecision evaluation_stage_decision(
    bool offline_passed,
    std::optional<bool> panel_one_passed);

struct SealedEvaluationReport {
    std::array<ArtifactSnapshot, 2> artifacts_before;
    std::array<ArtifactSnapshot, 2> artifacts_after;
    std::string parent_fingerprint;
    std::string control_fingerprint;
    std::string treatment_fingerprint;
    HoldoutReport holdout;
    OfflineGateReport offline_gate;
    std::optional<GameplayPanelReport> treatment_vs_control;
    std::optional<GameplayGateReport>
        treatment_vs_control_gate;
    std::optional<GameplayPanelReport> treatment_vs_parent;
    std::optional<GameplayGateReport>
        treatment_vs_parent_gate;
    bool action_probe_reference_available = false;
    bool hidden_repartition_passed = false;
    std::size_t hidden_repartition_policies = 0;
    std::size_t hidden_repartition_probes = 0;
    bool passed = false;
};

// Canonical-only, load-only experiment route. It opens each reserved
// evaluation seed exactly as preregistered and never calls a trainer.
SealedEvaluationReport run_sealed_terminal_weight_c17_evaluation(
    std::ostream& progress);

void write_human_report(
    const SealedEvaluationReport& report, std::ostream& output);
void write_tsv_report(
    const SealedEvaluationReport& report, std::ostream& output);

} // namespace old_school::terminal_weight_eval
