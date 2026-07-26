#pragma once

#include "old_school/game.hpp"
#include "old_school/learned_iteration.hpp"
#include "old_school/terminal_weight_eval.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::replay_weight_audit {

inline constexpr std::uint64_t kAuditSeed = 202607260731ULL;
inline constexpr std::size_t kAuditGeneration = 20;
inline constexpr std::size_t kAuditBalancedBlocks = 60;
inline constexpr std::size_t kAuditPhysicalGames =
    kAuditBalancedBlocks *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kAuditActorGames =
    2 * kAuditPhysicalGames;
inline constexpr std::size_t kAuditActorGamesPerDeck = 960;
inline constexpr std::size_t kRecordBootstrapDistance = 4;
inline constexpr std::size_t kAuditWorkerCount = 4;
inline constexpr std::string_view kParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

inline constexpr std::size_t kMinimumEligibleActorGames = 4560;
inline constexpr std::size_t kMinimumEligibleActorGamesPerDeck = 912;
inline constexpr std::size_t kMinimumEligiblePhysicalGames = 2280;
inline constexpr std::size_t kMinimumEarlyGreenEligibleRecords = 2500;
inline constexpr std::size_t kMinimumEarlyGreenEligibleActorGames = 900;

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
std::string audit_schedule_hash(
    std::span<const AuditTask> tasks);

// Card-agnostic grouping coordinates. `physical_game` is globally unique
// within a corpus. A perspective identifies one actor-game; repeated rows at
// the same public calendar turn deliberately share a coordinate.
struct ReplayCoordinate {
    std::size_t physical_game = 0;
    std::size_t perspective = 0;
    std::size_t calendar_turn = 0;

    bool operator==(const ReplayCoordinate&) const = default;
};

// Returns N/(A*T_a*n_(a,t)) in input order. It consumes no RNG, never
// reorders rows, and rejects empty/rootless or malformed coordinates.
std::vector<double> hierarchical_weights(
    std::span<const ReplayCoordinate> coordinates);

struct AuditRecord {
    std::size_t physical_game = 0;
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    std::size_t perspective = 0;
    DeckId deck = DeckId::Green;
    std::size_t root_index = 0;
    std::size_t root_turn = 0;
    double terminal_target = 0.5;
    double ro4_target = 0.5;
    std::optional<std::size_t> ro4_future_index;
    double treatment_weight = 1.0;

    bool operator==(const AuditRecord&) const = default;
};

struct DistributionSummary {
    std::size_t count = 0;
    double minimum = 0.0;
    double q25 = 0.0;
    double median = 0.0;
    double q75 = 0.0;
    double maximum = 0.0;
    double mean = 0.0;

    bool operator==(const DistributionSummary&) const = default;
};

struct WeightDiagnostics {
    std::size_t records = 0;
    std::size_t actor_games = 0;
    std::size_t actor_turns = 0;
    double total_weight = 0.0;
    double expected_total_weight = 0.0;
    double expected_actor_weight = 0.0;
    double maximum_global_mass_error = 0.0;
    double maximum_actor_mass_error = 0.0;
    double maximum_turn_mass_error = 0.0;
    double kish_effective_sample_size = 0.0;
    DistributionSummary weights;
    DistributionSummary actor_record_counts;
    DistributionSummary actor_turn_counts;
    DistributionSummary turn_record_multiplicities;
    bool finite_positive = false;
    bool global_mass_identity = false;
    bool actor_mass_identity = false;
    bool turn_mass_identity = false;

    bool operator==(const WeightDiagnostics&) const = default;
};

// Recomputes the hierarchy mass diagnostics for already weighted records.
// This is the same path used by collection and consumes no RNG.
WeightDiagnostics diagnose_hierarchical_weights(
    std::span<const AuditRecord> records);

struct CaptureConfig {
    std::size_t max_game_turns = 500;
    std::size_t worker_count = 1;
    bool verify_hidden_repartition = true;
    std::uint64_t schedule_seed = 0;
    std::size_t schedule_generation = 0;

    bool operator==(const CaptureConfig&) const = default;
};

struct Capture {
    std::vector<AuditRecord> records;
    std::array<std::array<std::array<std::size_t, 2>, 2>,
               kDeckCount>
        deck_seat_started_counts{};
    std::array<std::array<std::size_t, kDeckCount>,
               kDeckCount>
        ordered_pair_counts{};
    std::size_t physical_games = 0;
    std::size_t actor_games = 0;
    std::size_t rootless_actor_games = 0;
    std::size_t hidden_repartition_states = 0;
    WeightDiagnostics weights;
    bool trace_invariants_passed = false;
    bool ro4_identity_passed = false;
    bool terminal_tail_identity_passed = false;
    bool hidden_repartition_passed = false;
    bool hidden_grouping_identity_passed = false;
    bool hidden_target_hash_identity_passed = false;
    bool hidden_weight_identity_passed = false;
    bool hidden_scoring_hash_identity_passed = false;
    bool weight_identity_passed = false;
    std::string schedule_hash;
    std::string trace_hash;
    std::string outcome_hash;
    std::string feature_hash;
    std::string grouping_hash;
    std::string ro4_target_hash;
    std::string weight_hash;
    std::string scoring_hash;

    bool operator==(const Capture&) const = default;
};

// A separate RB0 collector. It intentionally does not call or depend on the
// TA4/CT8 collector and computes only canonical RO4 targets.
Capture collect(
    std::span<const AuditTask> tasks,
    std::shared_ptr<const LearnedModel> parent,
    CaptureConfig config);

enum class RootTurnStratum : std::uint8_t {
    Early,
    Middle,
    Late,
};

inline constexpr std::size_t kRootTurnStratumCount = 3;

struct MetricComparison {
    ClusteredEstimate control;
    ClusteredEstimate treatment;
    ClusteredEstimate treatment_minus_control;

    bool operator==(const MetricComparison&) const = default;
};

struct MetricSet {
    MetricComparison signed_bias;
    MetricComparison brier;
    MetricComparison soft_log_loss;

    bool operator==(const MetricSet&) const = default;
};

struct RowSetMetrics {
    bool available = false;
    std::size_t records = 0;
    std::size_t actor_games = 0;
    std::size_t physical_games = 0;
    std::size_t bootstrapped_records = 0;
    std::size_t terminal_tail_records = 0;
    MetricSet metrics;
    WeightDiagnostics weights;
    DistributionSummary terminal_targets;
    DistributionSummary ro4_targets;
    double achieved_mde_95_80 = 0.0;

    bool operator==(const RowSetMetrics&) const = default;
};

struct ScopeMetrics {
    RowSetMetrics all_records;
    RowSetMetrics ro4_eligible;
    std::array<RowSetMetrics, kRootTurnStratumCount>
        all_by_root_turn{};
    std::array<RowSetMetrics, kRootTurnStratumCount>
        eligible_by_root_turn{};

    bool operator==(const ScopeMetrics&) const = default;
};

struct MetricsReport {
    ScopeMetrics pooled;
    std::array<ScopeMetrics, kDeckCount> by_deck{};

    bool operator==(const MetricsReport&) const = default;
};

MetricsReport score_records(
    std::span<const AuditRecord> records);

struct ScientificReport {
    std::uint64_t seed = 0;
    std::size_t generation = 0;
    std::size_t balanced_blocks = 0;
    std::size_t physical_games = 0;
    std::size_t actor_games = 0;
    std::array<std::array<std::array<std::size_t, 2>, 2>,
               kDeckCount>
        deck_seat_started_counts{};
    std::array<std::array<std::size_t, kDeckCount>,
               kDeckCount>
        ordered_pair_counts{};
    MetricsReport metrics;
    WeightDiagnostics weights;
    bool schedule_balanced = false;
    bool eligible_coverage_passed = false;
    bool early_green_control_qualified = false;
    bool kish_qualification_passed = false;
    bool trace_invariants_passed = false;
    bool ro4_identity_passed = false;
    bool terminal_tail_identity_passed = false;
    bool hidden_repartition_passed = false;
    bool hidden_grouping_identity_passed = false;
    bool hidden_target_hash_identity_passed = false;
    bool hidden_weight_identity_passed = false;
    bool hidden_scoring_hash_identity_passed = false;
    std::size_t hidden_repartition_states = 0;
    bool weight_identity_passed = false;
    std::string schedule_hash;
    std::string trace_hash;
    std::string outcome_hash;
    std::string feature_hash;
    std::string grouping_hash;
    std::string ro4_target_hash;
    std::string weight_hash;
    std::string scoring_hash;

    bool operator==(const ScientificReport&) const = default;
};

ScientificReport make_scientific_report(
    const Capture& capture, std::uint64_t seed,
    std::size_t generation, std::size_t balanced_blocks);

struct GateReport {
    bool early_green_point_effect = false;
    bool early_green_interval = false;
    bool early_green_absolute_bias_shrank = false;
    bool whole_green_bias_shrank = false;
    bool pooled_loss_noninferior = false;
    bool per_deck_bias_band = false;
    bool per_deck_brier_guard = false;
    bool per_deck_soft_log_guard = false;
    bool no_new_material_bias = false;
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
    bool reversed_input_bit_identical = false;
    bool worker_reduction_bit_identical = false;
    bool artifact_unchanged = false;
    bool passed = false;
};

constexpr int audit_exit_code(
    bool complete, bool scientific_pass) noexcept {
    return !complete ? 2 : (scientific_pass ? 0 : 1);
}

ArtifactSnapshot require_artifact_snapshot(
    const std::string& artifact_path);
bool infrastructure_complete(const AuditReport& report);

// Canonical, load-only RB0 route. It builds four fresh corpora to establish
// repetition, input-order, and fixed-one-versus-N-worker determinism
// independently.
AuditReport run_canonical_rb0_audit(std::ostream& progress);

void write_human_report(
    const AuditReport& report, std::ostream& output);
void write_tsv_report(
    const AuditReport& report, std::ostream& output);

} // namespace old_school::replay_weight_audit
