#pragma once

#include "old_school/fq0_dominance.hpp"
#include "old_school/fq4_priority_collection.hpp"
#include "old_school/game.hpp"
#include "old_school/learned_iteration.hpp"
#include "old_school/oc1_action_scoring.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_d1_field_gate {

inline constexpr std::string_view kSchema =
    "old-school-fq4-d1-p0-parent-census-v1";
inline constexpr std::string_view kRequiredParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::size_t kParentTrainingGames = 800;
inline constexpr std::uint64_t kParentTrainingSeed = 424242;
inline constexpr std::size_t kParentGenerations = 16;

inline constexpr std::array<std::uint64_t, 2> kSourceSeedBases{
    790,
    791,
};
inline constexpr std::uint64_t kSourceGenerationNamespace =
    0x4651344431ULL;
inline constexpr std::uint64_t kDominanceSeedNamespace =
    0x4651344431444f4dULL;
inline constexpr std::uint64_t kHiddenSeedNamespace =
    0x4651344431484944ULL;
inline constexpr std::string_view kScheduleSchema =
    "old-school-fq4-d1-p0-schedule-v1";
inline constexpr std::size_t kExpectedScheduleBytes = 4197;
inline constexpr std::string_view kExpectedScheduleSha256 =
    "33f3826615e9c66b6c5c0c137e6c17bc0b53fbe967804e7b39dc8a53143fb28a";
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kMaximumLegalActions =
    fq4_priority_collection::kMaximumLegalActions;
inline constexpr std::size_t kMaximumRootsPerOwnerGame =
    fq4_priority_collection::kMaximumRootsPerOwnerGame;
inline constexpr std::size_t kDominanceWorlds =
    fq4_priority_collection::kDominanceWorlds;
inline constexpr std::size_t kExpectedPhysicalGames =
    kSourceSeedBases.size() *
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kExpectedOwnerPerspectives =
    2 * kExpectedPhysicalGames;
inline constexpr std::size_t kExpectedPerspectivesPerDeck =
    kExpectedOwnerPerspectives / kDeckCount;
inline constexpr std::size_t kExpectedSeatZeroPerspectivesPerDeck =
    kExpectedPerspectivesPerDeck / 2;
inline constexpr std::size_t kExpectedOnPlayPerspectivesPerDeck =
    kExpectedPerspectivesPerDeck / 2;
inline constexpr std::size_t kMinimumHighConfidenceRoots = 5;
inline constexpr std::size_t kMinimumHighConfidenceGames = 5;
inline constexpr std::size_t kMinimumHighConfidenceDecks = 2;
inline constexpr double kParentResidualWeight = 0.10;
inline constexpr std::size_t kWatchdogSeconds = 180;

using SourceGame = fq4_priority_collection::SourceGame;

struct ScheduleBalance {
    std::size_t physical_games = 0;
    std::size_t owner_perspectives = 0;
    std::array<std::size_t, kDeckCount> perspectives_by_deck{};
    std::array<std::size_t, kDeckCount> seat_zero_by_deck{};
    std::array<std::size_t, kDeckCount> on_play_by_deck{};
    bool exact = false;

    bool operator==(const ScheduleBalance&) const = default;
};

// These functions are pure: constructing and hashing the immutable schedule
// does not open a game seed.
std::vector<SourceGame> source_schedule();
std::string serialize_source_schedule(
    const std::vector<SourceGame>& schedule);
std::string source_schedule_sha256();
ScheduleBalance audit_schedule_balance(
    const std::vector<SourceGame>& schedule);

using RootLocator = fq4_priority_collection::RootLocator;

using fq4_priority_collection::physical_game_id;
std::string stable_root_id(
    const RootLocator& locator,
    std::string_view information_action_fingerprint);

using RetentionCandidate =
    fq4_priority_collection::RetentionCandidate;
using RetentionResult =
    fq4_priority_collection::RetentionResult;

using fq4_priority_collection::retain_owner_game_roots;

using ReplayRootManifest =
    fq4_priority_collection::ReplayRootManifest;

bool validate_replay_manifest(
    const std::vector<ReplayRootManifest>& roots);
std::string serialize_replay_manifest(
    const std::vector<ReplayRootManifest>& roots);
std::string replay_manifest_sha256(
    const std::vector<ReplayRootManifest>& roots);

using DominanceWorldRow =
    fq4_priority_collection::DominanceWorldRow;
using RobustDominance =
    fq4_priority_collection::RobustDominance;

// Incomplete and incomparable rows remain nondominated. Shape errors are
// infrastructure failures and return shape_valid=false.
using fq4_priority_collection::summarize_robust_dominance;

using ParentClass = fq4_priority_collection::ParentClass;

using fq4_priority_collection::parent_class_name;

using ParentClassInput =
    fq4_priority_collection::ParentClassInput;
using ParentClassResult =
    fq4_priority_collection::ParentClassResult;

using fq4_priority_collection::classify_parent;

using RootCounts = fq4_priority_collection::RootCounts;
using ProductionAccounting =
    fq4_priority_collection::ProductionAccounting;
using GameCensus = fq4_priority_collection::GameCensus;
using DeckGameCoverage =
    fq4_priority_collection::DeckGameCoverage;

struct ScoredRoot {
    ReplayRootManifest manifest;
    RobustDominance dominance;
    // Retain the complete production row, including all eight raw samples,
    // the resolved recipe, exact descriptor order, support, and accounting.
    // The repeat digest uses IEEE-754 bits for every floating-point field.
    oc1_action_scoring::DecisionScore base_score;
    std::vector<double> base_scores;
    std::vector<std::string> base_exact_support;
    // Candidate-neutral, canonical Priority option tensors. P0 does not
    // digest or consume these rows; D1 uses them to score an outer policy
    // head without retaining GameState or invoking another rollout.
    std::vector<std::vector<double>> neutral_priority_options;
    std::vector<std::vector<double>>
        hidden_neutral_priority_options;
    std::vector<double> residuals;
    std::vector<double> combined_scores;
    ParentClassResult parent_class;
    ProductionAccounting accounting;
    bool hidden_replay_bit_identical = false;
    bool hidden_feature_bits_identical = false;

    bool operator==(const ScoredRoot&) const = default;
};

struct CensusReport {
    std::string parent_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    std::string schedule_sha256;
    std::string trajectory_sha256;
    std::string retained_corpus_sha256;
    std::string dominance_corpus_sha256;
    std::string scored_corpus_sha256;
    std::string audit_scores_sha256;
    ScheduleBalance schedule_balance;
    std::vector<GameCensus> games;
    std::array<RootCounts, kDeckCount> decks;
    std::array<DeckGameCoverage, kDeckCount>
        deck_game_coverage;
    RootCounts pooled;
    std::vector<ScoredRoot> scored_roots;
    // Descriptive bridge only: exact raw-base maximum support containing a
    // robustly Pass-dominated action, and the subset also containing a
    // nondominated action. Neither quantity participates in the P0 gate.
    std::array<std::size_t, kDeckCount>
        raw_base_dominated_support_by_deck{};
    std::array<std::size_t, kDeckCount>
        raw_base_mixed_tie_support_by_deck{};
    ProductionAccounting primary_accounting;
    ProductionAccounting hidden_control_accounting;
    ProductionAccounting reverse_control_accounting;
    ProductionAccounting accounting;
    ProductionAccounting repeat_primary_accounting;
    ProductionAccounting repeat_hidden_control_accounting;
    ProductionAccounting repeat_reverse_control_accounting;
    ProductionAccounting repeat_accounting;
    double class2_sigma_mass = 0.0;
    std::size_t distinct_high_confidence_games = 0;
    std::size_t distinct_high_confidence_decks = 0;
    std::size_t hidden_replay_roots = 0;
    std::size_t distinct_hidden_clones = 0;
    std::size_t vacuous_hidden_clones = 0;
    double runtime_seconds = 0.0;

    bool schedule_preflight_before_games = false;
    bool parent_contract_exact = false;
    bool source_config_exact = false;
    bool retention_score_blind = false;
    bool all_replays_exact = false;
    bool all_hidden_feature_bits_identical = false;
    bool first_deck_controls_bit_identical = false;
    bool recipe_and_accounting_exact = false;
    bool count_cross_sums_exact = false;
    bool repeated_construction_bit_identical = false;
    bool watchdog_ok = false;

    std::vector<std::string> infrastructure_failures;
    std::vector<std::string> underpowered_reasons;

    bool infrastructure_valid() const;
    bool support_floor_met() const;
    bool passed() const;
};

enum class ExitClassification : int {
    Pass = 0,
    Underpowered = 1,
    InfrastructureFailure = 2,
};

ExitClassification classify_exit(const CensusReport& report);

// Production-only parent census. This never constructs, loads, or scores the
// FQ4-D0b treatment.
CensusReport run_parent_census(
    std::shared_ptr<const LearnedModel> frozen_c16);

namespace testing {

// The production repeat comparison uses this exact IEEE-754 tensor check;
// numeric vector equality alone would not distinguish positive/negative zero.
bool neutral_tensor_bits_identical(
    const ScoredRoot& first,
    const ScoredRoot& second);

enum class RootDisposition : std::uint8_t {
    Malformed,
    Trivial,
    OverCap,
    RetentionCandidate,
};

RootDisposition diagnose_root_disposition(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal);

struct CanonicalHiddenDiagnostic {
    bool materialized = false;
    GameState canonical_state;
    GameState hidden_clone_state;
    std::vector<std::string> canonical_descriptors;
    std::string information_action_fingerprint;
    bool owner_hand_preserved = false;
    bool reporting_statistics_zero = false;
    bool second_replay_exact = false;
    bool hidden_feature_bits_identical = false;
    bool hidden_clone_eligible = false;
    bool hidden_clone_distinct = false;

    bool operator==(
        const CanonicalHiddenDiagnostic&) const = default;
};

CanonicalHiddenDiagnostic diagnose_canonical_hidden_root(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal);

// Tests report gating without opening production source seeds.
CensusReport complete_synthetic_report();

} // namespace testing

} // namespace old_school::fq4_d1_field_gate
