#pragma once

#include "old_school/replay_weight_audit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::rb0_mechanical_preflight {

namespace rb0 = replay_weight_audit;

inline constexpr std::uint64_t kEngineeringSeed =
    202607260902ULL;
inline constexpr std::size_t kGeneration = 20;
inline constexpr std::size_t kBalancedBlocks = 60;
inline constexpr std::size_t kPhysicalGames =
    kBalancedBlocks *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kActorGames =
    2 * kPhysicalGames;
inline constexpr std::size_t kWorkerCount = 4;
inline constexpr std::size_t kCaptureCount = 4;

inline constexpr std::array<std::string_view, kCaptureCount>
    kCaptureNames = {
        "canonical",
        "repeat",
        "reverse",
        "single-worker",
    };

struct CaptureEvidence {
    std::size_t physical_games = 0;
    std::size_t actor_games = 0;
    std::size_t rows = 0;
    std::size_t rootless_actor_games = 0;
    std::size_t hidden_repartition_states = 0;
    rb0::WeightDiagnostics weights;
    double global_mass_tolerance = 0.0;
    double actor_mass_tolerance = 0.0;
    double turn_mass_tolerance_at_maximum_error = 0.0;
    bool physical_game_count_exact = false;
    bool actor_game_count_exact = false;
    bool rows_present = false;
    bool rootless_actor_games_zero = false;
    bool schedule_balanced = false;
    bool hashes_well_formed = false;
    bool trace_invariants_passed = false;
    bool ro4_identity_passed = false;
    bool terminal_tail_identity_passed = false;
    bool hidden_repartition_passed = false;
    bool hidden_changed_state_present = false;
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

    bool operator==(const CaptureEvidence&) const = default;
};

struct Report {
    std::uint64_t seed = 0;
    std::size_t generation = 0;
    std::size_t balanced_blocks = 0;
    rb0::ArtifactSnapshot artifact_before;
    rb0::ArtifactSnapshot artifact_after;
    std::string parent_fingerprint;
    bool exact_engineering_seed = false;
    bool quarantined_seed_excluded = false;
    bool exact_generation = false;
    bool exact_block_count = false;
    bool artifact_snapshot_bound = false;
    bool parent_fingerprint_exact = false;
    bool parent_schema_exact = false;
    bool artifact_unchanged_after_load = false;
    bool artifact_unchanged_after_canonical = false;
    bool artifact_unchanged_after_repeat = false;
    bool artifact_unchanged_after_reverse = false;
    bool artifact_unchanged_after_single_worker = false;
    bool artifact_unchanged_final = false;
    std::array<CaptureEvidence, kCaptureCount> captures;
    bool repeated_capture_bit_identical = false;
    bool reversed_capture_bit_identical = false;
    bool worker_capture_bit_identical = false;

    bool operator==(const Report&) const = default;
};

struct NamedInvariant {
    std::string name;
    bool passed = false;

    bool operator==(const NamedInvariant&) const = default;
};

// The engineering route has exactly one permitted seed. The consumed
// scientific seed is named separately so a future refactor cannot route it
// through this preflight accidentally.
void require_engineering_seed(std::uint64_t seed);

CaptureEvidence inspect_capture(
    const rb0::Capture& capture, std::uint64_t seed,
    std::size_t generation, std::size_t balanced_blocks);

std::vector<NamedInvariant> named_invariants(
    const Report& report);
bool mechanically_clean(const Report& report);
constexpr int exit_code(bool clean) noexcept {
    return clean ? 0 : 2;
}

// Loads the exact frozen C16 artifact and builds the four declared captures.
// It deliberately never constructs a ScientificReport and never invokes a
// scoring reducer, a gate evaluator, a trainer, or a scientific writer.
Report run(std::ostream& progress);

void write_report(const Report& report, std::ostream& output);

} // namespace old_school::rb0_mechanical_preflight
