#pragma once

#include "old_school/action_q_nested_actor_broad_distill.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::decision_boundary_critic {

namespace source = action_q_nested_actor_broad_distill;

inline constexpr std::size_t kPolicyFeatureCount =
    source::kPolicyFeatureCount;
inline constexpr std::size_t kCriticFeatureCount = 674;
inline constexpr std::size_t kExpectedRootsPerSplit =
    source::kActorGamesPerSplit;
inline constexpr std::size_t kExpectedRootsPerDeckAndSplit =
    source::kActorGamesPerDeckAndSplit;
inline constexpr std::string_view kRequiredParentFingerprint =
    source::kRequiredParentFingerprint;
inline constexpr std::string_view kRequiredSourceManifestHash =
    source::kFrozenCensusManifestHash;
inline constexpr std::string_view kFrozenSubsetHash =
    "850fe865b474b6b49e0794dc5dff5e917b322cc7fdabcf0ab673858a3e43c76a";
inline constexpr std::uint64_t kTeacherSeed =
    202607291301ULL;
inline constexpr std::size_t kTeacherWorlds = 8;
inline constexpr std::size_t kTeacherRolloutsPerWorld = 1;
inline constexpr std::size_t kTeacherHorizonTurns = 8;
inline constexpr std::size_t kTeacherThreads = 4;
inline constexpr std::size_t kInnerWorlds = 2;
inline constexpr std::size_t kInnerHorizonTurns = 4;
inline constexpr std::size_t kOutputParameterCount =
    kLearnedOutputCalibrationLeafCount *
    kLearnedOutputCalibrationParameterCount;
inline constexpr double kMaximumDevDeckRegretIncrease = 0.01;
inline constexpr std::size_t kCalibrationBinCount = 5;

static_assert(kPolicyFeatureCount == 893);
static_assert(
    kCriticFeatureCount ==
    kLearnedCriticObservationFeatureCount);
static_assert(kExpectedRootsPerSplit == 80);
static_assert(kExpectedRootsPerDeckAndSplit == 16);
static_assert(kOutputParameterCount == 34);

using Split = source::Split;
using ManifestRoot = source::ManifestRoot;

struct DeckCensus {
    std::size_t actor_games = 0;
    std::size_t roots = 0;
    std::size_t legal_options = 0;

    bool operator==(const DeckCensus&) const = default;
};

struct SplitCensus {
    Split split = Split::Train;
    std::size_t games = 0;
    std::size_t actor_games = 0;
    std::size_t roots = 0;
    std::size_t legal_options = 0;
    std::array<DeckCensus, kDeckCount> decks{};

    bool operator==(const SplitCensus&) const = default;
};

// DBC0 deliberately retains only material already present in the G4B
// owner-information manifest. It contains no GameState, hidden opponent
// identity, source outcome, sampled world, or teacher label.
struct Census {
    std::string parent_fingerprint;
    std::string source_manifest_hash;
    std::array<SplitCensus, 2> splits;
    std::vector<ManifestRoot> roots;
    std::string subset_hash;

    bool operator==(const Census&) const = default;
};

bool parse_census_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

std::string canonical_subset_hash(const Census& census);
void validate_census(const Census& census);
void require_frozen_census(const Census& census);

// Production authenticates the complete frozen G4B census before applying
// the preregistered position-zero projection.
Census project_frozen_census(const source::Census& full);
Census collect_census(
    std::shared_ptr<const LearnedModel> parent);

void print_census(
    std::ostream& output, const Census& census);

struct BoundaryCell {
    std::size_t action_index = 0;
    std::size_t world_index = 0;
    double teacher_target = 0.5;
    double parent_prediction = 0.5;
    double weight = 0.0;
    bool terminal_before_boundary = false;
    // Empty for terminal cells. The observation is actor-local and is the
    // only input admitted to calibration. The state is transient and exists
    // solely so an immutable candidate can be scored in-memory.
    std::vector<double> observation;
    std::optional<GameState> boundary_state;

    bool operator==(const BoundaryCell&) const = default;
};

struct RootAccounting {
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    std::size_t eligible_cells = 0;
    std::size_t terminal_before_boundary_cells = 0;
    std::size_t inner_rollout_evaluations = 0;
    std::size_t inner_search_invocations = 0;
    std::size_t inner_search_max_depth = 0;

    bool operator==(const RootAccounting&) const = default;
};

struct RootExample {
    ManifestRoot manifest;
    std::vector<std::vector<double>> teacher_samples;
    std::vector<BoundaryCell> cells;
    RootAccounting accounting;

    bool operator==(const RootExample&) const = default;
};

struct Corpus {
    Census census;
    LearnedModelComponentFingerprints parent_components;
    std::vector<RootExample> train;
    std::vector<RootExample> dev;
    std::string digest;

    bool operator==(const Corpus&) const = default;
};

struct RootPrediction {
    std::string stable_root_id;
    // Action-major, then world-major. Terminal entries must equal their
    // aligned terminal teacher utility; nonterminal entries are critic
    // predictions at the captured successor boundary.
    std::vector<std::vector<double>> action_samples;

    bool operator==(const RootPrediction&) const = default;
};

struct DeckMetrics {
    DeckId deck = DeckId::Green;
    std::size_t roots = 0;
    std::size_t eligible_cells = 0;
    std::size_t stable_pairs = 0;
    double weight_mass = 0.0;
    double weighted_bce = 0.0;
    double weighted_brier = 0.0;
    double weighted_bias = 0.0;
    double weighted_ece = 0.0;
    double top_one_expected_agreement = 0.0;
    double stable_pair_agreement = 0.0;
    double mean_regret = 0.0;

    bool operator==(const DeckMetrics&) const = default;
};

struct Metrics {
    std::array<DeckMetrics, kDeckCount> decks{};
    std::size_t roots = 0;
    std::size_t eligible_cells = 0;
    std::size_t stable_pairs = 0;
    double weight_mass = 0.0;
    double equal_deck_weighted_bce = 0.0;
    double equal_deck_weighted_brier = 0.0;
    double equal_deck_weighted_bias = 0.0;
    double equal_deck_weighted_ece = 0.0;
    double equal_deck_top_one_expected_agreement = 0.0;
    double equal_deck_stable_pair_agreement = 0.0;
    double equal_deck_mean_regret = 0.0;

    bool operator==(const Metrics&) const = default;
};

struct FitReport {
    std::shared_ptr<const LearnedModel> candidate;
    LearnedOutputCalibrationDiagnostics optimizer;
    std::string parent_fingerprint_before;
    std::string parent_fingerprint_after;
    std::string candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    LearnedCriticTensorFingerprints parent_critic_tensors;
    LearnedCriticTensorFingerprints candidate_critic_tensors;
    std::size_t authorized_output_parameters =
        kOutputParameterCount;
    std::size_t changed_output_parameters = 0;
    bool parent_immutable = false;
    bool repeated_fit_bit_identical = false;
    bool parameter_replay_bit_identical = false;
    bool only_output_layer_changed = false;

    bool operator==(const FitReport&) const = default;
};

struct OfflineGate {
    bool repeated_collection_bit_identical = false;
    bool hidden_repartition_bit_identical = false;
    bool source_and_subset_exact = false;
    bool accounting_exact = false;
    bool parent_immutable = false;
    bool repeated_fit_bit_identical = false;
    bool exact_output_component_isolation = false;
    bool train_bce_strictly_improved = false;
    bool train_regret_strictly_improved = false;
    bool dev_bce_strictly_improved = false;
    bool dev_regret_strictly_improved = false;
    bool dev_top_one_non_decreasing = false;
    std::array<bool, kDeckCount> dev_deck_regret_guard{};
    std::array<bool, kDeckCount> parent_train_regret_nonzero{};
    std::array<bool, kDeckCount> parent_dev_regret_nonzero{};
    std::vector<std::string> failures;

    bool passed() const;
    bool operator==(const OfflineGate&) const = default;
};

struct RunReport {
    Corpus corpus;
    FitReport fit;
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_dev;
    Metrics candidate_dev;
    OfflineGate gate;
};

std::uint64_t teacher_search_seed(
    const source::RootCoordinate& coordinate);
LearnedSearchConfig teacher_search_config(std::uint64_t seed);
std::string canonical_corpus_digest(const Corpus& corpus);
void validate_corpus(const Corpus& corpus);
// Persists only the authenticated owner-safe census, neutral observations,
// labels, weights, and accounting. Transient GameState witnesses are never
// serialized. Production publication and loading require the exact frozen
// subset; loading also binds the cache to the supplied immutable parent.
void write_corpus_cache_atomic(
    const std::filesystem::path& path,
    const Corpus& corpus);
Corpus load_corpus_cache(
    const std::filesystem::path& path,
    std::shared_ptr<const LearnedModel> parent);
Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const Census& frozen_subset,
    bool hidden_repartition_source = false);
std::vector<LearnedWeightedCriticTrainingExample>
training_examples(const Corpus& corpus);
std::vector<RootPrediction> score(
    std::span<const RootExample> examples,
    std::shared_ptr<const LearnedModel> model);
Metrics evaluate(
    std::span<const RootExample> examples,
    std::span<const RootPrediction> predictions);
FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);
OfflineGate evaluate_offline_gate(
    const Corpus& corpus,
    const FitReport& fit,
    const Metrics& parent_train,
    const Metrics& candidate_train,
    const Metrics& parent_dev,
    const Metrics& candidate_dev,
    bool repeated_collection_bit_identical,
    bool hidden_repartition_bit_identical);
RunReport run(std::shared_ptr<const LearnedModel> parent);
void print_run(std::ostream& output, const RunReport& report);

namespace testing {

// Pure selection seam used to prove that production selection depends only
// on the preexisting retained-position coordinate.
std::vector<ManifestRoot> select_position_zero(
    std::span<const ManifestRoot> roots);

// Pure assembly seam for validation and hash mutation tests. Production
// never accepts caller-supplied summaries or roots.
Census make_census(
    std::array<SplitCensus, 2> splits,
    std::vector<ManifestRoot> roots);

// Pure construction and prediction seams. They exercise corpus validation,
// weighting, action metrics, and gates without opening a scientific seed.
Corpus make_corpus(
    Census census,
    LearnedModelComponentFingerprints parent_components,
    std::vector<RootExample> train,
    std::vector<RootExample> dev);

// The same byte codec and atomic writer with only the production frozen-hash
// gate relaxed, so synthetic corpora can exercise persistence fail-closed.
void write_unfrozen_corpus_cache_atomic(
    const std::filesystem::path& path,
    const Corpus& corpus);
Corpus load_unfrozen_corpus_cache(
    const std::filesystem::path& path,
    const Census& expected_census,
    const LearnedModelComponentFingerprints&
        expected_parent_components);

GameState hidden_repartition(
    const GameState& state, std::size_t observer);

} // namespace testing

} // namespace old_school::decision_boundary_critic
