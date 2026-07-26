#pragma once

#include "old_school/turn_alignment_audit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::target_factorial_audit {

namespace ta4 = turn_alignment_audit;

inline constexpr std::uint64_t kAuditSeed = 202607260621ULL;
inline constexpr std::size_t kAuditGeneration = 19;
inline constexpr std::size_t kAuditBalancedBlocks = 50;
inline constexpr std::size_t kAuditPhysicalGames =
    kAuditBalancedBlocks *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kAuditPerspectives =
    2 * kAuditPhysicalGames;
inline constexpr std::size_t kAuditPerspectivesPerDeck = 800;
inline constexpr std::size_t kMinimumCommonActorGames = 3000;
inline constexpr std::size_t kMinimumCommonActorGamesPerDeck = 560;
inline constexpr std::size_t kMinimumCommonPhysicalGames = 1500;
inline constexpr std::size_t kMinimumEarlyGreenRecords = 1000;
inline constexpr std::size_t kMinimumEarlyGreenActorGames = 500;
inline constexpr std::string_view kParentFingerprint =
    ta4::kParentFingerprint;

using ta4::ArtifactSnapshot;
using ta4::AuditRecord;
using ta4::AuditTask;
using ta4::Capture;
using ta4::CaptureConfig;
using ta4::ClusteredEstimate;
using ta4::RootTurnStratum;
using ta4::kRootTurnStratumCount;

enum class TargetArm : std::uint8_t {
    RecordOffset4,
    RecordOffset8,
    CalendarTurn4,
    CalendarTurn8,
};

inline constexpr std::size_t kTargetArmCount = 4;

enum class Contrast : std::uint8_t {
    RecordOffset8MinusRecordOffset4,
    CalendarTurn4MinusRecordOffset4,
    CalendarTurn8MinusRecordOffset4,
    CalendarTurn8MinusCalendarTurn4,
    CalendarTurn8MinusRecordOffset8,
};

inline constexpr std::size_t kContrastCount = 5;

std::string_view target_arm_name(TargetArm arm);
std::string_view contrast_name(Contrast contrast);

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

struct DeltaMetrics {
    ClusteredEstimate brier_delta;
    ClusteredEstimate soft_log_loss_delta;
    ClusteredEstimate signed_bias_delta;

    bool operator==(const DeltaMetrics&) const = default;
};

struct WeightingMetrics {
    std::array<TargetMetrics, kTargetArmCount> arms{};
    std::array<DeltaMetrics, kContrastCount> contrasts{};
    // Difference-in-differences:
    //   (CT8 - CT4) - (RO8 - RO4).
    DeltaMetrics interaction;

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

struct ScopeMetrics {
    RowSetMetrics all_records;
    RowSetMetrics four_arm_common;
    std::array<RowSetMetrics, kContrastCount> pair_common{};
    std::array<RowSetMetrics, kRootTurnStratumCount>
        four_arm_common_by_root_turn{};
    std::array<std::size_t, kTargetArmCount>
        bootstrapped_records{};
    std::array<std::size_t, kTargetArmCount>
        terminal_tail_records{};
    std::array<std::size_t,
               ta4::kRecordBootstrapDistance + 1>
        record_offset4_turn_distance_histogram{};
    std::array<std::size_t,
               ta4::kRecordOffset8Distance + 1>
        record_offset8_turn_distance_histogram{};

    bool operator==(const ScopeMetrics&) const = default;
};

struct MetricsReport {
    // The established two-arm scorer is retained as a cross-instrument
    // reference for RO4 versus CT4.
    ta4::MetricsReport ro4_ct4_reference;
    ScopeMetrics pooled;
    std::array<ScopeMetrics, kDeckCount> by_deck{};

    bool operator==(const MetricsReport&) const = default;
};

// Scores four targets already captured on the same trace roots. Every
// contrast is formed row by row before physical-game CR1 clustering.
MetricsReport score_four_arm_records(
    std::span<const AuditRecord> records);

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
    bool early_green_control_qualified = false;
    bool trace_invariants_passed = false;
    bool record_offset4_identity_passed = false;
    bool calendar_turn4_distance_passed = false;
    bool calendar_turn4_earliest_passed = false;
    bool record_offset8_identity_passed = false;
    bool calendar_turn8_distance_passed = false;
    bool calendar_turn8_earliest_passed = false;
    bool tail_identity_passed = false;
    bool hidden_repartition_passed = false;
    std::size_t hidden_repartition_states = 0;
    std::string schedule_hash;
    std::string trace_hash;
    std::string outcome_hash;
    std::string record_offset4_target_hash;
    std::string record_offset8_target_hash;
    std::string calendar_turn4_target_hash;
    std::string calendar_turn8_target_hash;
    std::string scoring_hash;

    bool operator==(const ScientificReport&) const = default;
};

ScientificReport make_scientific_report(
    const Capture& capture, std::uint64_t seed,
    std::size_t generation, std::size_t balanced_blocks);

struct GateReport {
    bool early_green_primary = false;
    bool early_green_constituent_advantage = false;
    bool early_green_interaction = false;
    bool whole_green_bias_shrank = false;
    bool pooled_loss_vs_control = false;
    bool pooled_loss_vs_constituents = false;
    bool pooled_all_brier_best = false;
    bool per_deck_all_brier_guard = false;
    bool no_new_material_bias = false;
    bool blue_direction_and_shrink = false;
    bool ru_direction_and_shrink = false;
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

constexpr int audit_exit_code(
    bool infrastructure_complete,
    bool scientific_pass) noexcept {
    return !infrastructure_complete
               ? 2
               : (scientific_pass ? 0 : 1);
}

// This helper is intentionally small so missing-artifact fail-closed behavior
// can be tested without running or mutating the reserved audit.
ArtifactSnapshot require_artifact_snapshot(
    const std::string& artifact_path);

bool infrastructure_complete(const AuditReport& report);

// Canonical, exclusive, load-only route. It accepts no experiment options,
// builds the complete corpus twice from fresh Game instances, and never
// trains or writes a model.
AuditReport run_canonical_ct8_audit(std::ostream& progress);

void write_human_report(
    const AuditReport& report, std::ostream& output);
void write_tsv_report(
    const AuditReport& report, std::ostream& output);

} // namespace old_school::target_factorial_audit
