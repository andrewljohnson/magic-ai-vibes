#pragma once

#include "old_school/game.hpp"
#include "old_school/learned_iteration.hpp"
#include "old_school/terminal_weight_eval.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::turn_alignment_audit {

inline constexpr std::uint64_t kAuditSeed = 202607260501ULL;
inline constexpr std::size_t kAuditGeneration = 18;
inline constexpr std::size_t kAuditBalancedBlocks = 50;
inline constexpr std::size_t kAuditPhysicalGames =
    kAuditBalancedBlocks *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kAuditPerspectives =
    2 * kAuditPhysicalGames;
inline constexpr std::size_t kAuditPerspectivesPerDeck = 800;
inline constexpr std::size_t kRecordBootstrapDistance = 4;
inline constexpr std::size_t kTurnBootstrapAdvances = 4;
inline constexpr double kTerminalWeight = 0.50;
static_assert(
    kLearnedValueSearchHorizonTurns ==
    kTurnBootstrapAdvances);
inline constexpr std::string_view kParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

using terminal_weight_eval::ArtifactSnapshot;
using terminal_weight_eval::ClusteredEstimate;

struct AuditTask {
    std::size_t physical_game = 0;
    std::size_t block = 0;
    learned_iteration::ScheduledGame scheduled;

    bool operator==(const AuditTask&) const = default;
};

std::vector<AuditTask> audit_schedule(
    std::uint64_t seed, std::size_t generation,
    std::size_t balanced_blocks);

struct AuditRecord {
    std::size_t physical_game = 0;
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    std::size_t perspective = 0;
    DeckId deck = DeckId::Green;
    std::size_t root_index = 0;
    std::size_t root_turn = 0;
    double terminal_target = 0.5;
    double control_target = 0.5;
    double treatment_target = 0.5;
    std::optional<std::size_t> control_future_index;
    std::optional<std::size_t> treatment_future_index;
    std::optional<std::size_t> control_turn_distance;
    std::optional<std::size_t> treatment_turn_distance;

    bool operator==(const AuditRecord&) const = default;
};

struct TargetMetrics {
    ClusteredEstimate brier;
    ClusteredEstimate soft_log_loss;
    ClusteredEstimate signed_bias;
    double target_mean = 0.0;
    double target_variance = 0.0;
    std::size_t saturated_targets = 0;
    double saturation_fraction = 0.0;

    bool operator==(const TargetMetrics&) const = default;
};

struct PairedMetrics {
    ClusteredEstimate brier_delta;
    ClusteredEstimate soft_log_loss_delta;
    ClusteredEstimate signed_bias_delta;

    bool operator==(const PairedMetrics&) const = default;
};

struct WeightingMetrics {
    TargetMetrics control;
    TargetMetrics treatment;
    PairedMetrics treatment_minus_control;

    bool operator==(const WeightingMetrics&) const = default;
};

struct RowSetMetrics {
    bool available = false;
    std::size_t records = 0;
    std::size_t actor_games = 0;
    std::size_t excluded_actor_games = 0;
    std::size_t physical_games = 0;
    WeightingMetrics record_weighted;
    WeightingMetrics equal_actor_game;

    bool operator==(const RowSetMetrics&) const = default;
};

enum class RootTurnStratum : std::uint8_t {
    Early,
    Middle,
    Late,
};

inline constexpr std::size_t kRootTurnStratumCount = 3;

struct ScopeMetrics {
    RowSetMetrics all_records;
    RowSetMetrics common_records;
    std::array<RowSetMetrics, kRootTurnStratumCount>
        common_by_root_turn{};
    std::size_t control_bootstrapped_records = 0;
    std::size_t control_terminal_tail_records = 0;
    std::size_t treatment_bootstrapped_records = 0;
    std::size_t treatment_terminal_tail_records = 0;
    // Index is the elapsed physical-turn distance for record-offset four.
    // Any distance outside [0, 4] is rejected before scoring.
    std::array<std::size_t, kRecordBootstrapDistance + 1>
        control_turn_distance_histogram{};

    bool operator==(const ScopeMetrics&) const = default;
};

struct MetricsReport {
    ScopeMetrics pooled;
    std::array<ScopeMetrics, kDeckCount> by_deck{};

    bool operator==(const MetricsReport&) const = default;
};

// Scores precomputed targets. This is the permanent synthetic-test seam:
// paired losses are always formed row by row and CR1-clustered by the globally
// unique physical-game ID.
MetricsReport score_records(std::span<const AuditRecord> records);

struct CaptureConfig {
    std::size_t max_game_turns = 500;
    std::size_t worker_count = 1;
    bool verify_hidden_repartition = true;

    bool operator==(const CaptureConfig&) const = default;
};

struct Capture {
    std::vector<AuditRecord> records;
    std::array<std::array<std::array<std::size_t, 2>, 2>,
               kDeckCount>
        deck_seat_started_counts{};
    std::size_t physical_games = 0;
    std::size_t perspectives = 0;
    bool trace_invariants_passed = false;
    bool control_identity_passed = false;
    bool tail_identity_passed = false;
    bool treatment_distance_passed = false;
    bool treatment_earliest_passed = false;
    bool hidden_repartition_passed = false;
    std::size_t hidden_repartition_states = 0;
    std::string schedule_hash;
    std::string trace_hash;
    std::string outcome_hash;
    std::string control_target_hash;
    std::string treatment_target_hash;
    std::string scoring_hash;

    bool operator==(const Capture&) const = default;
};

// Test-scale collection seam. It accepts explicit, noncanonical tasks and
// never loads, trains, caches, or writes a model.
Capture collect(
    std::span<const AuditTask> tasks,
    std::shared_ptr<const LearnedModel> parent,
    CaptureConfig config = {});

struct ScientificReport {
    std::uint64_t seed = 0;
    std::size_t generation = 0;
    std::size_t balanced_blocks = 0;
    std::size_t physical_games = 0;
    std::size_t perspectives = 0;
    std::array<std::array<std::array<std::size_t, 2>, 2>,
               kDeckCount>
        deck_seat_started_counts{};
    MetricsReport metrics;
    bool schedule_balanced = false;
    bool common_coverage_passed = false;
    bool trace_invariants_passed = false;
    bool control_identity_passed = false;
    bool tail_identity_passed = false;
    bool treatment_distance_passed = false;
    bool treatment_earliest_passed = false;
    bool hidden_repartition_passed = false;
    std::size_t hidden_repartition_states = 0;
    std::string schedule_hash;
    std::string trace_hash;
    std::string outcome_hash;
    std::string control_target_hash;
    std::string treatment_target_hash;
    std::string scoring_hash;

    bool operator==(const ScientificReport&) const = default;
};

ScientificReport make_scientific_report(
    const Capture& capture, std::uint64_t seed,
    std::size_t generation, std::size_t balanced_blocks);

struct GateReport {
    bool green_bias_moved = false;
    bool green_bias_precise = false;
    bool green_equal_actor_direction = false;
    bool green_absolute_bias_shrank = false;
    bool pooled_common_brier_noninferior = false;
    bool pooled_all_brier_nonpositive = false;
    bool blue_direction_and_shrink = false;
    bool ru_direction_and_shrink = false;
    bool additional_deck_moved = false;
    bool per_deck_all_brier_guard = false;
    bool red_white_no_new_material_bias = false;
    bool evidence_complete = false;
    bool mechanical_invariants = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const GateReport&) const = default;
};

GateReport evaluate_gate(const ScientificReport& report);

struct AuditReport {
    ArtifactSnapshot artifact_before;
    ArtifactSnapshot artifact_after;
    std::string parent_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    ScientificReport scientific;
    GateReport gate;
    bool repeated_report_bit_identical = false;
    bool artifact_unchanged = false;
    bool passed = false;
};

// Stable CLI taxonomy: a valid mechanism pass is 0, a complete scientific
// rejection is 1, and incomplete/mechanically invalid evidence is 2.
constexpr int audit_exit_code(
    bool infrastructure_complete,
    bool scientific_pass) noexcept {
    return !infrastructure_complete
               ? 2
               : (scientific_pass ? 0 : 1);
}

// Canonical, exclusive, load-only route. It performs two independent complete
// collections from fresh Game instances and does not train or write.
AuditReport run_canonical_ta4_audit(std::ostream& progress);

void write_human_report(
    const AuditReport& report, std::ostream& output);
void write_tsv_report(
    const AuditReport& report, std::ostream& output);

} // namespace old_school::turn_alignment_audit
