#pragma once

#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_schedule.hpp"
#include "old_school/fq4_priority_collection.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev_generator {

inline constexpr std::string_view kGeneratorSchema =
    "old-school-fq4-priority-dev-generator-v2";
inline constexpr std::string_view kOwnerInformationSchema =
    "old-school-fq4-priority-dev-owner-information-action-v2";
inline constexpr std::string_view kStableRootSchema =
    fq4_dev_bundle::kStableRootSchema;
inline constexpr std::string_view kReplayManifestSchema =
    "old-school-fq4-priority-dev-retained-manifest-v2";
inline constexpr std::string_view kHiddenSeedScope =
    "old-school-fq4-priority-dev-hidden-v2";
inline constexpr std::string_view kDominanceSeedScope =
    "old-school-fq4-priority-dev-dominance-v2";

inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::size_t kParentTrainingGames = 800;
inline constexpr std::uint64_t kParentTrainingSeed = 424242;
inline constexpr std::size_t kParentGenerations = 16;
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr double kParentPriorityResidualWeight = 0.10;

// These held-out coordinates are duplicated literally rather than imported:
// linking a D1/P0 header or implementation into the development generator is
// a structural firewall violation.
inline constexpr std::array<std::uint64_t, 4>
    kForbiddenSourceSeedBases{
        790,
        791,
        202607280210ULL,
        202607280211ULL,
    };
inline constexpr std::string_view kForbiddenHeldOutScheduleSha256 =
    "33f3826615e9c66b6c5c0c137e6c17bc0b53fbe967804e7b39dc8a53143fb28a";
inline constexpr std::uint64_t kForbiddenHeldOutGenerationNamespace =
    0x4651344431ULL;
inline constexpr std::uint64_t kForbiddenHeldOutHiddenNamespace =
    0x4651344431484944ULL;
inline constexpr std::uint64_t kForbiddenHeldOutDominanceNamespace =
    0x4651344431444f4dULL;

inline constexpr std::size_t kMinimumHighConfidenceRoots = 5;
inline constexpr std::size_t kMinimumHighConfidenceGames = 5;
inline constexpr std::size_t kMinimumHighConfidenceDecks = 2;
inline constexpr std::size_t kCompleteConstructions = 2;
inline constexpr std::size_t kGenerationSplitCount = 2;
inline constexpr std::size_t kWatchdogSeconds = 1200;

struct SchedulePreflight {
    std::string fit_sha256;
    std::string check_sha256;
    fq4_dev_schedule::ScheduleBalance fit_balance;
    fq4_dev_schedule::ScheduleBalance check_balance;
    bool exact = false;
    std::vector<std::string> failures;

    bool operator==(const SchedulePreflight&) const = default;
};

// Pure and game-free. Production calls this before loading C16 or opening a
// source-game seed; focused tests may safely exercise it.
SchedulePreflight preflight_schedules(
    const std::vector<fq4_dev_schedule::SourceGame>& fit,
    const std::vector<fq4_dev_schedule::SourceGame>& check);

const fq4_priority_collection::CollectionSpec& collection_spec();

// Canonical LF-final binding of every development collection-domain literal
// and structural limit consumed by collection_spec().
std::string collection_spec_contract_bytes();
fq4_dev_bundle::Hash256 collection_spec_contract_sha256();

// Portable, source-independent description of the exact 893-dimensional
// neutral Priority feature layout stored in the bundle.
std::string feature_contract_bytes();
fq4_dev_bundle::Hash256 feature_contract_sha256();

// Pure wire adapters. They expose no scoring or game-running capability and
// are used by synthetic tests to protect the production boundary.
fq4_dev_bundle::CensusRow make_census_row(
    const fq4_priority_collection::CanonicalRoot& root,
    const fq4_priority_collection::RobustDominance& dominance);
std::vector<fq4_dev_bundle::SparseFeature>
sparsify_priority_features(
    const std::vector<double>& dense_features);

struct ParentWitness {
    DeckId owner_deck = DeckId::Green;
    fq4_dev_bundle::Hash256 stable_root_id{};
    fq4_dev_bundle::Hash256 physical_game_sha256{};
    fq4_priority_collection::ParentClass classification =
        fq4_priority_collection::ParentClass::Invalid;

    bool operator==(const ParentWitness&) const = default;
};

inline constexpr std::uint8_t kCoverageGateFailed =
    1U << 0U;
inline constexpr std::uint8_t kParentErrorGateFailed =
    1U << 1U;

struct SplitSupport {
    std::array<std::size_t, kDeckCount> census_by_deck{};
    std::array<std::size_t, kDeckCount> selected_by_deck{};
    std::array<std::size_t, kDeckCount> positive_by_deck{};
    std::array<std::size_t, kDeckCount> background_by_deck{};
    std::size_t high_confidence_roots = 0;
    std::size_t high_confidence_games = 0;
    std::size_t high_confidence_decks = 0;
    bool coverage_met = false;
    bool parent_error_floor_met = false;
    std::uint8_t failed_gate_mask =
        kCoverageGateFailed |
        kParentErrorGateFailed;

    bool publishable() const {
        return coverage_met && parent_error_floor_met;
    }

    bool operator==(const SplitSupport&) const = default;
};

// Canonical count-only report. It intentionally cannot receive roots,
// descriptors, states, scores, outcomes, or source choices.
std::string format_support_report(
    const SplitSupport& fit,
    const SplitSupport& check);

SplitSupport summarize_support(
    const std::vector<fq4_dev_bundle::CensusRow>& census,
    const std::vector<fq4_dev_bundle::SelectedRow>& selected,
    const std::vector<ParentWitness>& witnesses);

bool complete_constructions_byte_identical(
    std::string_view first, std::string_view second);

// May live in MAP_SHARED anonymous memory in the one-shot executable. The
// supervisor reads it only after the worker has exited or been killed.
struct GenerationProgress {
    std::array<
        std::array<std::uint64_t, kGenerationSplitCount>,
        kCompleteConstructions>
        source_games_completed{};
    std::uint64_t candidate_rollout_evaluations = 0;

    bool operator==(const GenerationProgress&) const = default;
};

struct FailureScopeReport {
    std::string executable_after_sha256;
    std::string parent_after_sha256;
    bool executable_snapshot_ok = false;
    bool parent_snapshot_ok = false;
    bool artifact_status_known = false;
    bool artifact_present = false;
    bool temporary_status_known = false;
    bool temporary_absent = false;
    GenerationProgress progress;

    bool operator==(const FailureScopeReport&) const = default;
};

// Read-only, best-effort post-run scope inspection. It never throws.
FailureScopeReport inspect_failure_scope(
    const std::filesystem::path& executable_path,
    const GenerationProgress& progress) noexcept;
std::string format_failure_scope_report(
    const FailureScopeReport& report);
std::string format_support_rejection_output(
    const SplitSupport& fit,
    const SplitSupport& check,
    const FailureScopeReport& scope);

class GenerationFailure final : public std::runtime_error {
  public:
    GenerationFailure(
        std::string message,
        FailureScopeReport scope);

    const FailureScopeReport& scope() const noexcept {
        return scope_;
    }

  private:
    FailureScopeReport scope_;
};

struct GenerationReport {
    std::size_t artifact_bytes = 0;
    std::string artifact_sha256;
    SplitSupport fit;
    SplitSupport check;
    std::size_t source_games_per_construction = 0;
    std::size_t complete_constructions = 0;
    std::size_t source_game_executions = 0;
    std::size_t scored_rows = 0;
    std::size_t candidate_rollout_evaluations = 0;
    bool repeated_construction_bit_identical = false;
    bool published = false;
    FailureScopeReport scope;
};

// Production-only fixed-path operation. The executable and commit identities
// are provenance, not knobs for schedules, models, recipes, or publication.
// This function performs two complete independent constructions and publishes
// only after exact byte identity and both frozen support floors hold.
GenerationReport generate_and_publish(
    const std::filesystem::path& executable_path,
    std::string_view producer_commit,
    GenerationProgress* progress = nullptr);

} // namespace old_school::fq4_dev_generator
