#pragma once

#include "old_school/decision_boundary_action_pair.hpp"
#include "old_school/decision_density_labels.hpp"
#include "old_school/learned_priority_bilinear.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::decision_density_bilinear {

namespace labels = decision_density_labels;
namespace density = decision_density_census;
namespace priority = decision_density_priority;
namespace pair = decision_boundary_action_pair;

inline constexpr std::string_view kIdentifier =
    "AQ19-DBC6-R2-BILINEAR";
inline constexpr std::uint64_t kFitTag =
    UINT64_C(202607291901);
inline constexpr std::uint64_t kSelectorSeed =
    UINT64_C(202607291911);
inline constexpr std::string_view kFoldSchema =
    "old-school-aq19-physical-fold-v1";
inline constexpr std::string_view kRequiredFoldManifest =
    "51852e6fa7fee97edf809e7f29cf5859cca7fd0e1575a388053d5d2b042c7765";
inline constexpr std::string_view kRequiredCacheSha256 =
    "591498b82d352c870c786289f54b4e5c197f1c972b06d4f74c7a3ca7731916e8";
inline constexpr std::uintmax_t kRequiredCacheBytes =
    13'006'842;
inline constexpr std::string_view kRequiredCorpusDigest =
    "519ebe666adaf567ac3b4fdf7a1e2096cf96ccc70ad23e55b5db7f45c37c3f3f";
inline constexpr std::string_view kRequiredParentFingerprint =
    labels::kRequiredParentFingerprint;

inline constexpr std::size_t kRank =
    kLearnedPriorityBilinearRank;
inline constexpr std::size_t kStateFeatureCount =
    kLearnedPriorityBilinearStateFeatureCount;
inline constexpr std::size_t kActionFeatureCount =
    kLearnedPriorityBilinearActionFeatureCount;
inline constexpr std::size_t kPolicyFeatureCount =
    kLearnedPriorityBilinearPolicyFeatureCount;
inline constexpr std::size_t kParameterCount =
    kRank * (kStateFeatureCount + kActionFeatureCount);
inline constexpr std::size_t kFoldCount = 4;
inline constexpr std::size_t kCellCount =
    kDeckCount * priority::kWidthStrata;
inline constexpr std::size_t kAdamSteps = 256;
inline constexpr double kAdamLearningRate = 0.001;
inline constexpr double kAdamBetaOne = 0.9;
inline constexpr double kAdamBetaTwo = 0.999;
inline constexpr double kAdamEpsilon = 1.0e-8;
inline constexpr double kGradientNormClip = 5.0;
inline constexpr double kPairTemperature = 0.10;
inline constexpr double kResidualWeight =
    kLearnedPriorityBilinearResidualWeight;
inline constexpr double kL2Tether = 0.10;

static_assert(kRank == 2);
static_assert(kStateFeatureCount == 674);
static_assert(kActionFeatureCount == 219);
static_assert(kPolicyFeatureCount == 893);
static_assert(kParameterCount == 1786);
static_assert(kCellCount == 15);

using Parameters = LearnedPriorityBilinearParameters;
using StateFeatures =
    std::array<double, kStateFeatureCount>;
using ActionFeatures =
    std::array<double, kActionFeatureCount>;
using Metrics = pair::Metrics;

enum class Command : std::uint8_t {
    Run,
    OfflineReport,
};

struct Option {
    std::size_t canonical_ordinal = 0;
    PriorityAction action;
    ActionFeatures action_features{};
    double base_aggregate_score = 0.0;
    double teacher_aggregate_score = 0.0;
    std::array<double, labels::kWorlds>
        common_world_teacher_samples{};

    bool operator==(const Option&) const = default;
};

// This is deliberately a data-only, actor-local projection. It cannot retain
// a GameState, source coordinate, opponent hidden identity, or model pointer.
struct Root {
    std::string stable_root_id;
    std::string physical_game_group;
    DeckId deck = DeckId::Green;
    priority::WidthStratum width =
        priority::WidthStratum::B2;
    StateFeatures state{};
    std::vector<Option> options;

    bool operator==(const Root&) const = default;
};

struct Dataset {
    std::vector<Root> roots;
    std::array<std::size_t, kCellCount> roots_by_cell{};
    std::array<std::size_t, kDeckCount> roots_by_deck{};

    bool operator==(const Dataset&) const = default;
};

struct Corpus {
    Dataset train;
    Dataset dev;
    std::string source_digest;
    std::string parent_fingerprint;
    LearnedModelComponentFingerprints parent_components;

    bool operator==(const Corpus&) const = default;
};

struct PairCensusRow {
    std::size_t roots = 0;
    std::size_t all_tied_roots = 0;
    std::size_t potential_pairs = 0;
    std::size_t eligible_pairs = 0;

    bool operator==(const PairCensusRow&) const = default;
};

struct PairCensus {
    std::array<PairCensusRow, kDeckCount> decks{};
    PairCensusRow total;

    bool operator==(const PairCensus&) const = default;
};

struct FoldCensus {
    std::size_t physical_groups = 0;
    std::size_t roots = 0;
    std::array<std::size_t, kCellCount> roots_by_cell{};

    bool operator==(const FoldCensus&) const = default;
};

struct FoldAssignment {
    // Sorted by opaque physical-game hash.
    std::vector<std::pair<std::string, std::size_t>>
        group_folds;
    std::array<FoldCensus, kFoldCount> folds{};
    std::string manifest;

    bool operator==(const FoldAssignment&) const = default;
};

struct ForwardResult {
    std::vector<double> logits;
    std::vector<double> centered_logits;
    std::vector<double> residuals;
    std::vector<double> scores;

    bool operator==(const ForwardResult&) const = default;
};

struct OptimizerConfig {
    std::uint64_t fit_tag = kFitTag;
    std::size_t steps = kAdamSteps;
    double learning_rate = kAdamLearningRate;
    double beta_one = kAdamBetaOne;
    double beta_two = kAdamBetaTwo;
    double epsilon = kAdamEpsilon;
    double residual_weight = kResidualWeight;
    double pair_temperature = kPairTemperature;
    double l2_tether = kL2Tether;
    double global_gradient_norm_clip =
        kGradientNormClip;

    bool operator==(const OptimizerConfig&) const = default;
};

struct OptimizerReport {
    OptimizerConfig config;
    Parameters parameters;
    std::string parameter_sha256;
    std::size_t completed_steps = 0;
    double initial_objective = 0.0;
    double final_objective = 0.0;
    double parameter_l2_norm = 0.0;
    double final_gradient_l2_norm = 0.0;
    double maximum_preclip_gradient_l2_norm = 0.0;
    std::size_t clipped_steps = 0;
    Metrics before;
    Metrics after;

    bool operator==(const OptimizerReport&) const = default;
};

struct FoldReport {
    std::array<OptimizerReport, kFoldCount> fits{};
    std::array<OptimizerReport, kFoldCount> repeated_fits{};
    Metrics parent;
    Metrics candidate;
    std::vector<std::vector<double>> candidate_residuals;
    bool physical_groups_disjoint = false;
    bool every_root_predicted_once = false;
    bool repeated_fits_bit_identical = false;
    bool repeated_scores_bit_identical = false;

    bool operator==(const FoldReport&) const = default;
};

struct OfflineGateInputs {
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_oof;
    Metrics candidate_oof;
    Metrics parent_dev;
    Metrics candidate_dev;
    bool cache_identity_exact = false;
    bool corpus_census_exact = false;
    bool pair_census_exact = false;
    bool fold_manifest_exact = false;
    bool feature_layout_exact = false;
    bool state_prefix_bit_identical = false;
    bool teacher_zero_invariant = false;
    bool optimizer_recipe_exact = false;
    bool grouped_oof_exact = false;
    bool repeated_fits_bit_identical = false;
    bool parameter_replay_bit_identical = false;
    bool positive_zero_parent_equivalent = false;
    bool residuals_finite_and_bounded = false;
    bool legal_action_permutation_equivariant = false;
    bool hidden_repartition_bit_identical = false;
    bool symmetric_continuation_propagation = false;
    bool parent_immutable = false;
    bool treatment_only_isolation = false;

    bool operator==(const OfflineGateInputs&) const = default;
};

struct OfflineGate {
    bool train_pair_bce_improved = false;
    bool train_regret_improved = false;
    bool train_listwise_non_increasing = false;
    bool oof_pair_bce_improved = false;
    bool oof_regret_improved = false;
    bool oof_listwise_non_increasing = false;
    bool oof_top_one_non_decreasing = false;
    bool oof_stable_pair_non_decreasing = false;
    std::array<bool, kDeckCount>
        oof_deck_regret_non_increasing{};
    bool dev_pair_bce_improved = false;
    bool dev_regret_improved = false;
    bool dev_listwise_non_increasing = false;
    bool dev_top_one_non_decreasing = false;
    bool dev_stable_pair_non_decreasing = false;
    std::array<bool, kDeckCount>
        dev_deck_regret_non_increasing{};
    bool invariants_passed = false;
    std::vector<std::string> failures;

    bool passed() const;
    bool operator==(const OfflineGate&) const = default;
};

struct RunReport {
    std::filesystem::path cache_path;
    std::uintmax_t cache_bytes = 0;
    std::string cache_sha256;
    Corpus corpus;
    PairCensus train_pairs;
    PairCensus dev_pairs;
    FoldAssignment folds;
    FoldReport oof;
    OptimizerReport full_fit;
    OptimizerReport repeated_full_fit;
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_dev;
    Metrics candidate_dev;
    OfflineGate gate;
    bool cache_identity_exact = false;
    bool corpus_census_exact = false;
    bool pair_census_exact = false;
    bool fold_manifest_exact = false;
    bool feature_layout_exact = false;
    bool state_prefix_bit_identical = false;
    bool teacher_zero_invariant = false;
    bool optimizer_recipe_exact = false;
    bool grouped_oof_exact = false;
    bool repeated_fits_bit_identical = false;
    bool parameter_replay_bit_identical = false;
    bool positive_zero_parent_equivalent = false;
    bool residuals_finite_and_bounded = false;
    bool legal_action_permutation_equivariant = false;
    bool hidden_repartition_bit_identical = false;
    bool symmetric_continuation_propagation = false;
    bool parent_immutable = false;
    bool treatment_only_isolation = false;
    bool selector_seed_authorized = false;
    bool selector_opened = false;
    std::size_t gameplay_games = 0;
    BotBenchmarkSummary selector;
    bool pilot_licensed = false;
    bool fast_go = false;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
bool command_authorizes_selector_seed(Command command);
void print_usage(std::ostream& output);

void validate_root(const Root& root);
void validate_dataset(const Dataset& dataset);
void validate_corpus(const Corpus& corpus);
Corpus project_corpus(const labels::Corpus& source);
PairCensus pair_census(const Dataset& dataset);
bool frozen_pair_census_exact(
    const PairCensus& train, const PairCensus& dev);

FoldAssignment assign_grouped_folds(
    const Dataset& train, std::string_view corpus_digest);
Dataset fold_training_dataset(
    const Dataset& train,
    const FoldAssignment& assignment,
    std::size_t held_out_fold);
Dataset fold_holdout_dataset(
    const Dataset& train,
    const FoldAssignment& assignment,
    std::size_t held_out_fold);

ForwardResult forward(
    const Root& root, const Parameters& parameters);
std::vector<std::vector<double>> residuals(
    const Dataset& dataset,
    const Parameters& parameters);
Metrics evaluate(
    const Dataset& dataset,
    const std::vector<std::vector<double>>&
        residual_rows);
Metrics evaluate(
    const Dataset& dataset,
    const Parameters& parameters);
std::string parameter_sha256(
    const Parameters& parameters);
OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config = {});
bool optimizer_bit_identical(
    const OptimizerReport& first,
    const OptimizerReport& second);
FoldReport evaluate_grouped_oof(
    const Dataset& train,
    const FoldAssignment& assignment);
OfflineGate evaluate_offline_gate(
    const OfflineGateInputs& inputs);

RunReport run();
RunReport run_offline();
void print_report(
    std::ostream& output, const RunReport& report);

namespace testing {

struct ObjectiveProbe {
    double objective = 0.0;
    Parameters gradient;

    bool operator==(const ObjectiveProbe&) const = default;
};

Root project_root(const labels::RootLabel& source);
Dataset make_dataset(std::vector<Root> roots);
Root permute_options(
    const Root& root,
    const std::vector<std::size_t>& permutation);
std::vector<std::vector<double>> option_rows(
    const Root& root);
ObjectiveProbe objective_probe(
    const Dataset& dataset,
    const Parameters& parameters);
bool parameters_bit_identical(
    const Parameters& first,
    const Parameters& second);

} // namespace testing

} // namespace old_school::decision_density_bilinear
