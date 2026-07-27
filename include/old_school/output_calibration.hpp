#pragma once

#include "old_school/game.hpp"
#include "old_school/learned_iteration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace old_school::output_calibration {

inline constexpr std::uint64_t kFitSeed = 202607261927ULL;
inline constexpr std::uint64_t kHoldoutSeed = 202607261928ULL;
inline constexpr std::size_t kFitGeneration = 17;
inline constexpr std::size_t kHoldoutGeneration = 18;
inline constexpr std::size_t kBalancedBlocks = 8;
inline constexpr std::size_t kPhysicalGames =
    kBalancedBlocks *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kActorPerspectives =
    2 * kPhysicalGames;
inline constexpr std::size_t kPerspectivesPerDeck =
    kActorPerspectives / kDeckCount;
inline constexpr std::size_t kMaximumGameTurns = 500;
inline constexpr std::size_t kPilotTrainingGames = 800;
inline constexpr double kExplorationRate = 0.05;
inline constexpr double kNormal95CriticalValue =
    1.959963984540054;
inline constexpr double kDeckLossGuard = 0.002;
inline constexpr double kOtherDeckBiasGuard = 0.010;
inline constexpr double kMaterialBiasThreshold = 0.05;

struct CollectionConfig {
    std::uint64_t seed = 0;
    std::size_t generation = 0;
    std::size_t balanced_blocks = 0;
    std::size_t max_game_turns = kMaximumGameTurns;
    std::size_t pilot_training_games = kPilotTrainingGames;
    // Zero selects the available hardware concurrency. Collection is reduced
    // in schedule order, so this field is non-semantic.
    std::size_t worker_count = 0;

    bool operator==(const CollectionConfig&) const = default;
};

CollectionConfig canonical_fit_config();
CollectionConfig canonical_holdout_config();

struct CollectionTask {
    std::size_t physical_game = 0;
    std::size_t block = 0;
    learned_iteration::ScheduledGame scheduled;

    bool operator==(const CollectionTask&) const = default;
};

std::vector<CollectionTask> collection_schedule(
    const CollectionConfig& config);

struct ScheduleAccounting {
    std::size_t physical_games = 0;
    std::array<std::size_t, kDeckCount> perspectives_by_deck{};
    // deck / physical seat / whether that deck starts.
    std::array<
        std::array<std::array<std::size_t, 2>, 2>,
        kDeckCount>
        deck_seat_start{};
    bool tasks_well_formed = false;
    bool exact_balanced_blocks = false;

    bool operator==(const ScheduleAccounting&) const = default;
};

ScheduleAccounting inspect_collection_schedule(
    std::span<const CollectionTask> tasks,
    std::size_t expected_balanced_blocks);

struct HiddenExchange {
    GameState state;
    bool changed = false;
};

// Exchanges the first opponent hand card for which the opponent library has
// a different identity. The observer's complete private information and all
// public zones remain untouched. A library reorder alone is not a change.
HiddenExchange exchange_opponent_hidden_identity(
    const GameState& source, std::size_t perspective);

struct HiddenDeckCounts {
    std::size_t attempted = 0;
    std::size_t changed = 0;
    std::size_t unchanged = 0;

    bool operator==(const HiddenDeckCounts&) const = default;
};

struct HiddenRepartitionReport {
    HiddenDeckCounts pooled;
    std::array<HiddenDeckCounts, kDeckCount> by_deck{};
    bool owner_visible_rows_bit_identical = true;
    bool encoded_features_bit_identical = true;
    bool parent_leaf_predictions_bit_identical = true;
    bool parent_predictions_bit_identical = true;
    bool candidate_leaf_predictions_bit_identical = true;
    bool candidate_predictions_bit_identical = true;
    std::string original_owner_visible_rows_hash;
    std::string repartitioned_owner_visible_rows_hash;
    std::string original_encoded_rows_hash;
    std::string repartitioned_encoded_rows_hash;
    std::string original_parent_leaf_hash;
    std::string repartitioned_parent_leaf_hash;
    std::string original_parent_prediction_hash;
    std::string repartitioned_parent_prediction_hash;
    std::string original_candidate_leaf_hash;
    std::string repartitioned_candidate_leaf_hash;
    std::string original_candidate_prediction_hash;
    std::string repartitioned_candidate_prediction_hash;

    bool bit_identical() const;
    bool nonvacuous_all_decks() const;
    bool operator==(
        const HiddenRepartitionReport&) const = default;
};

struct CorpusHashes {
    std::string schedule;
    std::string outcomes;
    std::string record_counts;
    std::string features;
    std::string targets;
    std::string weights;
    std::string optimizer_input;
    std::string parent_leaf_predictions;
    std::string parent_predictions;
    std::string candidate_leaf_predictions;
    std::string candidate_predictions;

    bool operator==(const CorpusHashes&) const = default;
};

struct DeckAccounting {
    std::size_t perspectives = 0;
    std::size_t records = 0;
    double total_weight = 0.0;

    bool operator==(const DeckAccounting&) const = default;
};

struct CollectionAccounting {
    CollectionConfig config;
    ScheduleAccounting schedule;
    std::size_t actor_perspectives = 0;
    std::size_t records = 0;
    double total_weight = 0.0;
    std::array<DeckAccounting, kDeckCount> by_deck{};
    HiddenRepartitionReport hidden;

    bool operator==(const CollectionAccounting&) const = default;
};

struct TrainingRecord {
    std::size_t physical_game = 0;
    std::size_t perspective = 0;
    DeckId deck = DeckId::Green;
    std::size_t trace_index = 0;
    std::size_t trace_size = 0;
    std::vector<double> features;
    double target = 0.5;
    double weight = 1.0;
    std::array<double, 2> parent_leaf_predictions{};
    double parent_prediction = 0.5;

    bool operator==(const TrainingRecord&) const = default;
};

struct TrainingCorpus {
    std::vector<CollectionTask> tasks;
    std::vector<GameResult> outcomes;
    std::vector<TrainingRecord> records;
    CollectionAccounting accounting;
    CorpusHashes hashes;

    bool operator==(const TrainingCorpus&) const = default;
};

TrainingCorpus collect_training_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const CollectionConfig& config);
// Test/diagnostic overload. An ordered prefix is permitted, but its
// schedule accounting will not claim full balanced-block coverage.
TrainingCorpus collect_training_corpus(
    std::shared_ptr<const LearnedModel> parent,
    std::span<const CollectionTask> tasks,
    const CollectionConfig& config);

std::vector<LearnedWeightedCriticTrainingExample>
training_examples(const TrainingCorpus& corpus);

struct HoldoutRecord {
    std::size_t physical_game = 0;
    std::size_t perspective = 0;
    DeckId deck = DeckId::Green;
    std::size_t trace_index = 0;
    std::size_t trace_size = 0;
    double target = 0.5;
    double weight = 1.0;
    std::array<double, 2> parent_leaf_predictions{};
    double parent_prediction = 0.5;
    std::array<double, 2> candidate_leaf_predictions{};
    double candidate_prediction = 0.5;
    // Predictions from the deterministically exchanged opponent-hidden
    // state. Collection requires these to be bit-identical to the original
    // predictions, but retaining them lets orchestration independently score
    // and hash the complete hidden-repartition scientific report.
    std::array<double, 2>
        repartitioned_parent_leaf_predictions{};
    double repartitioned_parent_prediction = 0.5;
    std::array<double, 2>
        repartitioned_candidate_leaf_predictions{};
    double repartitioned_candidate_prediction = 0.5;

    bool operator==(const HoldoutRecord&) const = default;
};

struct HoldoutCorpus {
    std::vector<CollectionTask> tasks;
    std::vector<GameResult> outcomes;
    std::vector<HoldoutRecord> records;
    CollectionAccounting accounting;
    CorpusHashes hashes;

    bool operator==(const HoldoutCorpus&) const = default;
};

HoldoutCorpus collect_holdout_corpus(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const CollectionConfig& config);
// Test/diagnostic overload matching collect_training_corpus.
HoldoutCorpus collect_holdout_corpus(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    std::span<const CollectionTask> tasks,
    const CollectionConfig& config);

struct WeightedClusteredValue {
    std::size_t cluster = 0;
    double weight = 1.0;
    double value = 0.0;
};

struct WeightedClusteredEstimate {
    std::size_t records = 0;
    std::size_t clusters = 0;
    double total_weight = 0.0;
    double mean = 0.0;
    double standard_error = 0.0;
    double confidence_lower_95 = 0.0;
    double confidence_upper_95 = 0.0;

    bool operator==(
        const WeightedClusteredEstimate&) const = default;
};

WeightedClusteredEstimate weighted_cr1_estimate(
    std::span<const WeightedClusteredValue> values);

struct ModelMetrics {
    WeightedClusteredEstimate brier;
    WeightedClusteredEstimate soft_log_loss;
    WeightedClusteredEstimate signed_bias;
    double prediction_mean = 0.0;
    std::size_t saturated_records = 0;
    double saturated_weight = 0.0;
    double saturation_fraction = 0.0;

    bool operator==(const ModelMetrics&) const = default;
};

struct ComparisonMetrics {
    WeightedClusteredEstimate brier_delta;
    WeightedClusteredEstimate soft_log_loss_delta;

    bool operator==(
        const ComparisonMetrics&) const = default;
};

struct ScopeReport {
    std::size_t records = 0;
    std::size_t physical_games = 0;
    std::size_t actor_perspectives = 0;
    double total_weight = 0.0;
    double target_mean = 0.0;
    ModelMetrics parent;
    ModelMetrics candidate;
    ComparisonMetrics candidate_minus_parent;

    bool operator==(const ScopeReport&) const = default;
};

struct HoldoutReport {
    ScopeReport pooled;
    std::array<ScopeReport, kDeckCount> by_deck{};

    bool operator==(const HoldoutReport&) const = default;
};

HoldoutReport score_holdout_records(
    std::span<const HoldoutRecord> records);
std::vector<HoldoutRecord> repartitioned_holdout_records(
    std::span<const HoldoutRecord> records);
std::string hash_holdout_report(const HoldoutReport& report);

// Provenance checks owned by the artifact/execution layer. Defaults are
// fail-closed so synthetic tests and callers must affirm each one.
struct IntegrityEvidence {
    bool parent_identity = false;
    bool fit_provenance = false;
    bool holdout_provenance = false;
    bool component_isolation = false;
    bool artifact = false;
    bool determinism = false;
    bool finite_values = false;
    // These hashes are produced by the orchestration/artifact layer after
    // independently replaying the hidden-repartition fit and holdout score.
    // They make the two final OC1 hidden-invariance clauses fail closed.
    std::string original_fit_parameters_hash;
    std::string repartitioned_fit_parameters_hash;
    std::string original_scientific_report_hash;
    std::string repartitioned_scientific_report_hash;

    bool passed() const;
    bool operator==(const IntegrityEvidence&) const = default;
};

struct GateConfig {
    CollectionConfig expected_fit = canonical_fit_config();
    CollectionConfig expected_holdout = canonical_holdout_config();
    std::size_t expected_physical_games = kPhysicalGames;
    std::size_t expected_perspectives_per_deck =
        kPerspectivesPerDeck;
    double deck_loss_guard = kDeckLossGuard;
    double other_deck_bias_guard = kOtherDeckBiasGuard;
    double material_bias_threshold = kMaterialBiasThreshold;

    bool operator==(const GateConfig&) const = default;
};

struct GateReport {
    bool integrity_passed = false;
    bool collection_accounting_exact = false;
    bool pooled_losses_improved = false;
    bool per_deck_loss_guard = false;
    bool blue_bias_shrank = false;
    bool green_bias_nonincreasing = false;
    bool other_deck_bias_guard = false;
    bool no_new_material_same_sign_bias = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const GateReport&) const = default;
};

GateReport evaluate_gate(
    const HoldoutReport& report,
    const CollectionAccounting& fit_accounting,
    const CollectionAccounting& holdout_accounting,
    const IntegrityEvidence& integrity,
    GateConfig config = {});

} // namespace old_school::output_calibration
