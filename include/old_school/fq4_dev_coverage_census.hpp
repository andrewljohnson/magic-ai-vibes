#pragma once

#include "old_school/fq4_dev_generator.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev_coverage_census {

inline constexpr std::string_view kSchema =
    "old-school-fq4-dev4-coverage-census-v1";
inline constexpr std::size_t kStackSizeFeatureIndex =
    fq4_dev_generator::kCoverageStackSizeFeatureIndex;
inline constexpr std::size_t kStackSizeEncodingDenominator =
    fq4_dev_generator::kCoverageStackSizeEncodingDenominator;
inline constexpr std::size_t kPhaseCount = 7;
inline constexpr std::size_t kScheduleBlockCount =
    fq4_dev_schedule::kScheduleBlocks;
inline constexpr std::size_t kOwnerQuadrantCount = 4;
inline constexpr std::size_t kGamesPerBalancedBlock = 8;
inline constexpr std::size_t kGamesPerOpponent = 2;
inline constexpr std::size_t kGamesPerOwnerQuadrant = 2;
inline constexpr std::size_t kPublishedSelectedRows = 192;
inline constexpr std::size_t
    kPublishedSelectedPositiveRows = 182;
inline constexpr std::size_t
    kPublishedSelectedBackgroundRows = 10;
static_assert(
    kPublishedSelectedPositiveRows +
        kPublishedSelectedBackgroundRows ==
    kPublishedSelectedRows);

struct Count {
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t distinct_games = 0;

    bool operator==(const Count&) const = default;
};

struct StackCounts {
    Count empty;
    Count active;

    bool operator==(const StackCounts&) const = default;
};

struct EligiblePhaseCounts {
    Count all;
    Count first_or_second_main;
    Count other;

    bool operator==(const EligiblePhaseCounts&) const = default;
};

struct BlockEligibility {
    std::array<
        std::array<std::size_t, kOwnerQuadrantCount>,
        fq4_dev_bundle::kDeckCount>
        games_by_opponent_quadrant{};
    std::size_t distinct_games = 0;
    bool balanced_eight_feasible = false;

    bool operator==(const BlockEligibility&) const = default;
};

struct DeckCensus {
    Count retained;
    StackCounts dominance_positive;
    StackCounts selected_background;
    StackCounts unselected_dominance_negative;
    EligiblePhaseCounts eligible_stack_empty;
    std::array<BlockEligibility, kScheduleBlockCount>
        eligible_blocks;
    bool capacity_met = false;

    bool operator==(const DeckCensus&) const = default;
};

struct SplitCensus {
    std::array<DeckCensus, fq4_dev_bundle::kDeckCount> decks;

    bool operator==(const SplitCensus&) const = default;
};

struct CoverageCensus {
    SplitCensus fit;
    SplitCensus check;
    std::size_t retained_rows = 0;
    std::size_t retained_options = 0;
    std::size_t selected_rows = 0;
    std::size_t selected_positive_rows = 0;
    std::size_t selected_background_rows = 0;
    std::size_t action_invariant_stack_rows = 0;
    std::size_t exact_public_stack_rows = 0;
    std::size_t valid_public_phase_rows = 0;
    bool capacity_licensed = false;

    bool operator==(const CoverageCensus&) const = default;
};

struct Report {
    CoverageCensus census;
    std::size_t source_games_reconstructed = 0;
    std::size_t selected_rows_reconstructed = 0;
    fq4_dev_bundle::ScoreAccounting parent_scoring;
    std::string parent_artifact_sha256;
    std::size_t bundle_bytes = 0;
    std::string bundle_sha256;
    bool schedules_exact = false;
    bool scientific_manifest_exact = false;
    bool fit_census_exact = false;
    bool check_census_exact = false;
    bool fit_selected_exact = false;
    bool check_selected_exact = false;
    bool fit_manifest_exact = false;
    bool check_manifest_exact = false;
    bool parent_immutable = false;
    bool bundle_immutable = false;
    std::size_t parent_models_loaded = 0;
    std::size_t fits = 0;
    std::size_t candidate_rollout_evaluations = 0;
    std::size_t gameplay_evaluation_seeds = 0;

    bool exact() const;
};

// Pure aggregation and validation. It rejects malformed identities, roles,
// feature-20 encodings, phases, and accounting before returning any count.
CoverageCensus measure(
    const std::vector<
        fq4_dev_generator::CoverageRootObservation>& roots);

// Exact small bipartite b-matching predicate used by the production census.
// The owner row must be empty. A pass means eight distinct games can be
// chosen with degree two for every non-owner opponent and every quadrant.
bool balanced_block_feasible(
    std::size_t owner_deck,
    const std::array<
        std::array<std::size_t, kOwnerQuadrantCount>,
        fq4_dev_bundle::kDeckCount>&
        games_by_opponent_quadrant);

// Production-only fixed reconstruction. There are no caller-supplied paths,
// seeds, schedules, model parameters, selection rules, or output fields.
Report run_fixed();

// Strict aggregate-only rendering. No internal root or game identity has an
// output path through Report.
std::string format_report(const Report& report);

} // namespace old_school::fq4_dev_coverage_census
