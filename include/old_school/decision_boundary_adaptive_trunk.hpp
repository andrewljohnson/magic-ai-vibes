#pragma once

#include "old_school/decision_boundary_action_pair.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace old_school::decision_boundary_adaptive_trunk {

namespace dbc = decision_boundary_critic;
namespace pair = decision_boundary_action_pair;

inline constexpr std::size_t kPolicyFeatureCount =
    pair::kPolicyFeatureCount;
inline constexpr std::size_t kHiddenCount =
    pair::kHiddenCount;
inline constexpr std::size_t kParameterCount =
    3 * kHiddenCount;
inline constexpr std::size_t kFoldCount =
    pair::kFoldCount;
inline constexpr std::uint64_t kFitTag =
    UINT64_C(202607291701);
inline constexpr std::uint64_t kSelectorSeed =
    UINT64_C(202607291711);
inline constexpr std::size_t kAdamSteps = 256;
inline constexpr double kAdamLearningRate = 0.001;
inline constexpr double kAdamBetaOne = 0.9;
inline constexpr double kAdamBetaTwo = 0.999;
inline constexpr double kAdamEpsilon = 1.0e-8;
inline constexpr double kGlobalGradientNormClip = 5.0;
inline constexpr double kPairTemperature = 0.10;
inline constexpr double kResidualWeight = 0.10;
inline constexpr double kL2Tether = 0.10;
inline constexpr double kMaximumAgreementError = 1.0e-12;

static_assert(kPolicyFeatureCount == 893);
static_assert(kHiddenCount == 32);
static_assert(kParameterCount == 96);

using Hidden = pair::Hidden;
using Metrics = pair::Metrics;
using OfflineGate = pair::OfflineGate;

struct Parameters {
    Hidden gain{};
    Hidden bias{};
    Hidden output{};

    bool operator==(const Parameters&) const = default;
};

struct Root {
    pair::Root paired;
    std::vector<Hidden> parent_preactivations;

    bool operator==(const Root&) const = default;
};

struct Dataset {
    std::vector<Root> roots;
    std::array<std::size_t, kDeckCount> roots_by_deck{};

    bool operator==(const Dataset&) const = default;
};

struct Corpus {
    Dataset train;
    Dataset dev;
    std::string source_digest;
    LearnedModelComponentFingerprints parent_components;

    bool operator==(const Corpus&) const = default;
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
    Parameters parameters;
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
    bool only_priority_adaptive_trunk_changed = false;
    bool exact_transform = false;
    std::size_t changed_coordinates = 0;

    bool safe_for_evaluation() const;
    bool passed() const;
};

struct ExactEvaluationReport {
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_dev;
    Metrics candidate_dev;
    double maximum_preactivation_difference = 0.0;
    double maximum_activation_difference = 0.0;
    double maximum_logit_difference = 0.0;
    double maximum_residual_difference = 0.0;
    bool legal_action_permutation_equivariant = false;
    bool hidden_repartition_live_probe_eligible = false;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_repartition_owner_observation_bit_identical = false;
    bool hidden_repartition_actions_bit_identical = false;
    bool hidden_repartition_options_bit_identical = false;
    bool hidden_repartition_logits_bit_identical = false;
    bool hidden_repartition_centered_logits_bit_identical = false;
    bool hidden_repartition_residuals_bit_identical = false;
    bool zero_parameters_equivalent = false;
    bool successor_predictions_bit_identical = false;
    bool successor_metrics_bit_identical = false;

    bool operator==(const ExactEvaluationReport&) const = default;
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
    bool hidden_repartition_live_probe_eligible = false;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_repartition_owner_observation_bit_identical = false;
    bool hidden_repartition_actions_bit_identical = false;
    bool hidden_repartition_options_bit_identical = false;
    bool hidden_repartition_logits_bit_identical = false;
    bool hidden_repartition_centered_logits_bit_identical = false;
    bool hidden_repartition_residuals_bit_identical = false;
    bool parent_immutable = false;
    bool model_isolation_passed = false;
    bool successor_predictions_bit_identical = false;
    bool successor_metrics_bit_identical = false;

    bool operator==(const OfflineGateInputs&) const = default;
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
    double maximum_preactivation_difference = 0.0;
    double maximum_activation_difference = 0.0;
    double maximum_logit_difference = 0.0;
    double maximum_residual_difference = 0.0;

    bool operator==(const FoldReport&) const = default;
};

Corpus project_corpus(
    const dbc::Corpus& source,
    std::shared_ptr<const LearnedModel> parent);
void validate_dataset(const Dataset& dataset);
void validate_corpus(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);

pair::PairMetrics pair_census(const Dataset& dataset);
bool frozen_pair_census_exact(const Corpus& corpus);
Dataset fold_training_dataset(
    const Dataset& train, std::size_t held_out_fold);
Dataset fold_holdout_dataset(
    const Dataset& train, std::size_t held_out_fold);

Metrics evaluate(
    const Dataset& dataset,
    const Parameters& parameters);
OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config = {});
bool optimizer_bit_identical(
    const OptimizerReport& first,
    const OptimizerReport& second);
ModelIsolationReport apply_parameters(
    std::shared_ptr<const LearnedModel> parent,
    const Parameters& parameters);
ExactEvaluationReport evaluate_exact(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    const ModelIsolationReport& isolation,
    const Parameters& parameters);
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

struct HiddenRepartitionView {
    std::vector<double> owner_observation;
    std::vector<PriorityAction> actions;
    std::vector<std::vector<double>> option_rows;
    std::vector<double> logits;
    std::vector<double> centered_logits;
    std::vector<double> centered_residuals;

    bool operator==(
        const HiddenRepartitionView&) const = default;
};

struct HiddenRepartitionWitness {
    bool live_probe_eligible = false;
    bool nonvacuous = false;
    bool owner_observation_bit_identical = false;
    bool actions_bit_identical = false;
    bool options_bit_identical = false;
    bool logits_bit_identical = false;
    bool centered_logits_bit_identical = false;
    bool residuals_bit_identical = false;

    bool passed() const;
    bool operator==(
        const HiddenRepartitionWitness&) const = default;
};

struct ObjectiveProbe {
    double objective = 0.0;
    Parameters gradient;

    bool operator==(const ObjectiveProbe&) const = default;
};

struct ForwardProbe {
    std::vector<Hidden> preactivations;
    std::vector<Hidden> activations;
    std::vector<double> logits;
    std::vector<double> residuals;

    bool operator==(const ForwardProbe&) const = default;
};

Dataset make_dataset(std::vector<Root> roots);
Corpus make_corpus(
    Dataset train, Dataset dev,
    LearnedModelComponentFingerprints parent_components);
ObjectiveProbe objective_probe(
    const Dataset& dataset,
    const Parameters& parameters);
ForwardProbe analytic_forward(
    const Root& root,
    const Parameters& parameters);
ForwardProbe model_forward(
    const Root& root,
    std::shared_ptr<const LearnedModel> model);
Root permute_actions(
    const Root& root,
    const std::vector<std::size_t>& permutation);
bool legal_action_permutation_equivariant(
    const Root& root,
    const Parameters& parameters,
    std::shared_ptr<const LearnedModel> model);
HiddenRepartitionWitness
compare_hidden_repartition_views(
    bool live_probe_eligible,
    bool opponent_hidden_zones_nonvacuous,
    const HiddenRepartitionView& original,
    const HiddenRepartitionView& repartitioned);

} // namespace testing

} // namespace old_school::decision_boundary_adaptive_trunk
