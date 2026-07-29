#pragma once

#include "old_school/action_q_offline_gate.hpp"
#include "old_school/decision_boundary_rank_direct.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace old_school::decision_boundary_action_pair {

namespace dbc = decision_boundary_critic;
namespace direct = decision_boundary_rank_direct;

inline constexpr std::size_t kPolicyFeatureCount =
    dbc::kPolicyFeatureCount;
inline constexpr std::size_t kHiddenCount = 32;
inline constexpr std::size_t kFoldCount = 4;
inline constexpr std::uint64_t kFitTag =
    UINT64_C(202607291601);
inline constexpr std::uint64_t kSelectorSeed =
    UINT64_C(202607291611);
inline constexpr std::size_t kAdamSteps = 256;
inline constexpr double kAdamLearningRate = 0.001;
inline constexpr double kAdamBetaOne = 0.9;
inline constexpr double kAdamBetaTwo = 0.999;
inline constexpr double kAdamEpsilon = 1.0e-8;
inline constexpr double kGlobalGradientNormClip = 5.0;
inline constexpr double kPairTemperature = 0.10;
inline constexpr double kResidualWeight = 0.10;
inline constexpr double kL2Tether = 0.10;

static_assert(kPolicyFeatureCount == 893);
static_assert(kHiddenCount == 32);

using Delta = std::array<double, kHiddenCount>;
using Hidden = std::array<double, kHiddenCount>;

struct Root {
    direct::RankRoot ranking;
    std::size_t schedule_index = 0;
    std::size_t actor = 0;
    std::vector<std::vector<double>> options;
    std::vector<Hidden> hidden;

    bool operator==(const Root&) const = default;
};

struct Dataset {
    std::vector<Root> roots;
    std::array<std::size_t, kDeckCount> roots_by_deck{};

    bool operator==(const Dataset&) const = default;
};

// Minimal score-only view used by later action-head experiments. Aggregate
// vectors are authoritative production/search reductions; the paired teacher
// samples exist only for the stable-pair uncertainty calculation and must
// never be averaged to reconstruct either aggregate.
struct PrecomputedScoreRoot {
    DeckId deck = DeckId::Green;
    std::vector<double> base_aggregate_scores;
    std::vector<double> teacher_aggregate_scores;
    std::vector<std::vector<double>>
        common_world_teacher_samples;

    bool operator==(const PrecomputedScoreRoot&) const = default;
};

struct PrecomputedScoreDataset {
    std::vector<PrecomputedScoreRoot> roots;
    std::array<std::size_t, kDeckCount> roots_by_deck{};

    bool operator==(const PrecomputedScoreDataset&) const = default;
};

struct Corpus {
    Dataset train;
    Dataset dev;
    std::string source_digest;
    LearnedModelComponentFingerprints parent_components;

    bool operator==(const Corpus&) const = default;
};

struct PairDeckMetrics {
    DeckId deck = DeckId::Green;
    std::size_t roots = 0;
    std::size_t all_tied_roots = 0;
    std::size_t unordered_pairs = 0;
    std::size_t eligible_pairs = 0;
    double pair_bce = 0.0;

    bool operator==(const PairDeckMetrics&) const = default;
};

struct PairMetrics {
    std::array<PairDeckMetrics, kDeckCount> decks{};
    std::size_t roots = 0;
    std::size_t all_tied_roots = 0;
    std::size_t unordered_pairs = 0;
    std::size_t eligible_pairs = 0;
    double equal_deck_pair_bce = 0.0;

    bool operator==(const PairMetrics&) const = default;
};

struct Metrics {
    PairMetrics pairs;
    direct::Metrics ranking;

    bool operator==(const Metrics&) const = default;
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
        kGlobalGradientNormClip;

    bool operator==(const OptimizerConfig&) const = default;
};

struct OptimizerReport {
    OptimizerConfig config;
    Delta delta{};
    std::size_t completed_steps = 0;
    double initial_objective = 0.0;
    double final_objective = 0.0;
    double delta_l2_norm = 0.0;
    double final_gradient_l2_norm = 0.0;
    double maximum_preclip_gradient_l2_norm = 0.0;
    std::size_t clipped_steps = 0;
    Metrics before;
    Metrics after;

    bool operator==(const OptimizerReport&) const = default;
};

struct ModelIsolationReport {
    std::shared_ptr<const LearnedModel> candidate;
    std::string parent_fingerprint_before;
    std::string parent_fingerprint_after;
    std::string candidate_fingerprint;
    std::string repeated_candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    bool parent_positive_zero = false;
    bool parent_immutable = false;
    bool repeated_application_bit_identical = false;
    bool only_priority_hidden_output_changed = false;
    bool exact_delta = false;
    std::size_t changed_coordinates = 0;

    bool safe_for_evaluation() const;
    bool passed() const;
};

struct ExactEvaluationReport {
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_dev;
    Metrics candidate_dev;
    double maximum_activation_difference = 0.0;
    double maximum_logit_difference = 0.0;
    double maximum_residual_difference = 0.0;
    bool zero_delta_equivalent = false;
    bool successor_predictions_bit_identical = false;
    bool successor_metrics_bit_identical = false;

    bool operator==(const ExactEvaluationReport&) const = default;
};

struct FoldReport {
    std::array<OptimizerReport, kFoldCount> fits{};
    std::array<OptimizerReport, kFoldCount> repeated_fits{};
    Metrics parent;
    Metrics candidate;
    bool exact_balance = false;
    bool repeated_fits_bit_identical = false;
    bool repeated_scores_bit_identical = false;
    bool model_isolation_passed = false;
    double maximum_activation_difference = 0.0;
    double maximum_logit_difference = 0.0;
    double maximum_residual_difference = 0.0;

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
    bool pair_census_exact = false;
    bool optimizer_recipe_exact = false;
    bool grouped_folds_exact = false;
    bool repeat_fits_bit_identical = false;
    bool parameter_replay_bit_identical = false;
    bool zero_delta_equivalent = false;
    bool actual_model_agreement = false;
    bool parent_immutable = false;
    bool model_isolation_passed = false;
    bool successor_predictions_bit_identical = false;
    bool successor_metrics_bit_identical = false;

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
    std::array<bool, kDeckCount> oof_deck_regret_non_increasing{};
    bool dev_pair_bce_improved = false;
    bool dev_regret_improved = false;
    bool dev_listwise_non_increasing = false;
    bool dev_top_one_non_decreasing = false;
    bool dev_stable_pair_non_decreasing = false;
    std::array<bool, kDeckCount> dev_deck_regret_non_increasing{};
    bool successor_unchanged = false;
    bool invariants_passed = false;
    std::vector<std::string> failures;

    bool passed() const;
    bool operator==(const OfflineGate&) const = default;
};

Corpus project_corpus(
    const dbc::Corpus& source,
    std::shared_ptr<const LearnedModel> parent);
void validate_dataset(const Dataset& dataset);
void validate_precomputed_score_dataset(
    const PrecomputedScoreDataset& dataset);
void validate_corpus(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);

PairMetrics pair_census(const Dataset& dataset);
bool frozen_pair_census_exact(const Corpus& corpus);
Dataset fold_training_dataset(
    const Dataset& train, std::size_t held_out_fold);
Dataset fold_holdout_dataset(
    const Dataset& train, std::size_t held_out_fold);

Metrics evaluate(const Dataset& dataset, const Delta& delta);
// Reuses DBC4's exact pair/ranking aggregation for already computed
// current-root residuals. This is read-only and exists so later frozen
// action-head experiments do not clone the metric implementation.
Metrics evaluate_residuals(
    const Dataset& dataset,
    const std::vector<std::vector<double>>& residuals);
// Uses the authoritative aggregate vectors verbatim. Common-world samples
// affect only stable-pair uncertainty and are not an aggregate source. The
// returned ranking successor-calibration fields are zero because this
// score-only dataset intentionally carries no successor observations.
Metrics evaluate_precomputed_residuals(
    const PrecomputedScoreDataset& dataset,
    const std::vector<std::vector<double>>& residuals);
OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config = {});
bool optimizer_bit_identical(
    const OptimizerReport& first,
    const OptimizerReport& second);
ModelIsolationReport apply_delta(
    std::shared_ptr<const LearnedModel> parent,
    const Delta& delta);
ExactEvaluationReport evaluate_exact(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    const ModelIsolationReport& isolation,
    const Delta& delta);
FoldReport evaluate_grouped_oof(
    const Dataset& train,
    std::shared_ptr<const LearnedModel> parent);
OfflineGate evaluate_offline_gate(
    const OfflineGateInputs& inputs);
bool selector_config_exact(
    const BotConfig& challenger,
    const BotConfig& baseline,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate);
bool model_scores_bit_identical(
    const Dataset& dataset,
    const std::shared_ptr<const LearnedModel>& first,
    const std::shared_ptr<const LearnedModel>& second);

namespace testing {

struct ObjectiveProbe {
    double objective = 0.0;
    Delta gradient{};

    bool operator==(const ObjectiveProbe&) const = default;
};

Dataset make_dataset(std::vector<Root> roots);
Corpus make_corpus(
    Dataset train, Dataset dev,
    LearnedModelComponentFingerprints parent_components);
ObjectiveProbe objective_probe(
    const Dataset& dataset, const Delta& delta);

} // namespace testing

} // namespace old_school::decision_boundary_action_pair
