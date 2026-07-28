#pragma once

#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_priority_collection.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev_evaluator {

inline constexpr std::string_view kEvaluatorSchema =
    "old-school-fq4-priority-dev-evaluator-v1";
inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::size_t kParentTrainingGames = 800;
inline constexpr std::uint64_t kParentTrainingSeed = 424242;
inline constexpr std::size_t kParentGenerations = 16;

inline constexpr double kProjectionRatio =
    1.1051701859880913;
inline constexpr double kBehaviorPrimaryWeight = 0.90;
inline constexpr double kResidualWeight = 0.10;
inline constexpr double kPolicyTemperature = 0.10;
inline constexpr LearnedValuePriorityHeadUpdateConfig
    kOptimizer{
        .batch_size = 64,
        .epochs = 16,
        .learning_rate = 0.001,
        .beta1 = 0.9,
        .beta2 = 0.999,
        .epsilon = 1.0e-8,
        .global_gradient_norm_clip = 5.0,
        .seed = 202607280212ULL,
        .residual_weight = kResidualWeight,
        .policy_temperature = kPolicyTemperature,
    };

struct ConstraintCensus {
    std::array<std::size_t, fq4_dev_bundle::kMaximumActions>
        rows_by_constraint_count{};
    std::array<
        std::array<std::size_t, fq4_dev_bundle::kDeckCount>,
        2>
        positive_rows_by_split_deck{};
    std::size_t positive_rows = 0;
    std::size_t maximum_constraints = 0;

    bool operator==(const ConstraintCensus&) const = default;
};

struct PreparedAction {
    bool robustly_pass_dominated = false;
    std::array<double, fq4_dev_bundle::kWorldCount>
        raw_samples{};
    double base_score = 0.0;
    double parent_residual = 0.0;
    std::vector<double> features;

    bool operator==(const PreparedAction&) const = default;
};

struct PreparedRow {
    fq4_dev_bundle::Split split =
        fq4_dev_bundle::Split::Fit;
    std::uint8_t owner_deck = 0;
    std::uint8_t roles = 0;
    std::size_t pass_index = 0;
    std::vector<PreparedAction> actions;

    bool dominance_positive() const;
    bool operator==(const PreparedRow&) const = default;
};

struct PreparedCorpus {
    std::vector<PreparedRow> fit;
    std::vector<PreparedRow> check;

    bool operator==(const PreparedCorpus&) const = default;
};

struct CorpusLogits {
    std::vector<std::vector<double>> fit;
    std::vector<std::vector<double>> check;

    bool operator==(const CorpusLogits&) const = default;
};

struct ClassCounts {
    // Safe, Class1, Class2, Class3.
    std::array<std::size_t, 4> values{};

    std::size_t total() const;
    bool operator==(const ClassCounts&) const = default;
};

using TransitionMatrix =
    std::array<std::array<std::size_t, 4>, 4>;

struct MarginMetrics {
    std::size_t roots = 0;
    std::size_t constraints = 0;
    double mean_root_margin = 0.0;
    double minimum_margin = 0.0;

    bool operator==(const MarginMetrics&) const = default;
};

struct SupportViolationMetrics {
    std::size_t violating_roots = 0;
    std::size_t positive_roots = 0;
    double fraction = 0.0;

    bool operator==(
        const SupportViolationMetrics&) const = default;
};

struct DeckMetrics {
    std::size_t positive_roots = 0;
    std::size_t positive_options = 0;
    double target_to_parent_kl = 0.0;
    double target_to_candidate_kl = 0.0;
    MarginMetrics parent_margins;
    MarginMetrics candidate_margins;
    SupportViolationMetrics parent_support_violations;
    SupportViolationMetrics candidate_support_violations;
    ClassCounts parent_classes;
    ClassCounts candidate_classes;
    TransitionMatrix transitions{};
    std::size_t repairs = 0;
    std::size_t regressions = 0;

    bool operator==(const DeckMetrics&) const = default;
};

struct SplitMetrics {
    std::array<DeckMetrics, fq4_dev_bundle::kDeckCount>
        decks;
    std::size_t positive_roots = 0;
    std::size_t positive_options = 0;
    double deck_balanced_target_to_parent_kl = 0.0;
    double deck_balanced_target_to_candidate_kl = 0.0;
    double deck_balanced_parent_mean_margin = 0.0;
    double deck_balanced_candidate_mean_margin = 0.0;
    double pooled_parent_minimum_margin = 0.0;
    double pooled_candidate_minimum_margin = 0.0;
    SupportViolationMetrics parent_support_violations;
    SupportViolationMetrics candidate_support_violations;
    ClassCounts parent_classes;
    ClassCounts candidate_classes;
    TransitionMatrix transitions{};
    std::size_t repairs = 0;
    std::size_t regressions = 0;

    bool operator==(const SplitMetrics&) const = default;
};

struct OfflineAccounting {
    std::size_t games = 0;
    std::size_t determinizations = 0;
    std::size_t search_calls = 0;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_leaves = 0;
    std::size_t bootstrap_leaves = 0;
    std::size_t dominance_transitions = 0;

    bool zero() const;
    bool operator==(const OfflineAccounting&) const = default;
};

struct EvaluationMetrics {
    SplitMetrics fit;
    SplitMetrics check;
    std::size_t parent_anchor_rows = 0;
    std::size_t parent_anchor_actions = 0;
    bool parent_anchors_exact = false;
    OfflineAccounting accounting;

    bool operator==(const EvaluationMetrics&) const = default;
};

struct ModelEvaluationReport {
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    bool parent_immutable = false;
    bool nonpriority_components_identical = false;
    EvaluationMetrics metrics;
};

struct FitAccounting {
    std::size_t fit_examples = 0;
    std::size_t fit_options = 0;
    std::size_t check_examples = 0;
    std::size_t background_only_examples = 0;
    std::size_t optimizer_calls = 0;
    std::string training_input_sha256;
    LearnedValuePriorityHeadUpdateConfig optimizer;

    bool operator==(const FitAccounting&) const = default;
};

struct CandidateFit {
    std::shared_ptr<const LearnedModel> model;
    FitAccounting accounting;
};

using FitBoundaryObserver = std::function<void(
    const std::vector<LearnedValuePriorityTrainingExample>&,
    const LearnedValuePriorityHeadUpdateConfig&)>;

ConstraintCensus constraint_census(
    const fq4_dev_bundle::Bundle& bundle);
PreparedCorpus prepare(
    const fq4_dev_bundle::Bundle& bundle);
std::vector<LearnedValuePriorityTrainingExample>
fit_examples(const PreparedCorpus& corpus);
CorpusLogits score_logits(
    const PreparedCorpus& corpus,
    std::shared_ptr<const LearnedModel> model);
EvaluationMetrics evaluate_logits(
    const PreparedCorpus& corpus,
    const CorpusLogits& parent,
    const CorpusLogits& candidate);
ModelEvaluationReport evaluate_models(
    const PreparedCorpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate);
CandidateFit fit_candidate(
    const PreparedCorpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    FitBoundaryObserver observer = {});

std::shared_ptr<const LearnedModel>
load_fixed_parent();

std::string format_constraint_census(
    const ConstraintCensus& census);
std::string format_evaluation_report(
    std::string_view mode,
    const ModelEvaluationReport& report,
    const FitAccounting& fit);

namespace testing {

PreparedCorpus prepare_selected_rows(
    const std::vector<fq4_dev_bundle::SelectedRow>& fit,
    const std::vector<fq4_dev_bundle::SelectedRow>& check);
std::string invoke_fit_boundary_token(
    const PreparedCorpus& corpus,
    const std::function<std::string(
        const std::vector<
            LearnedValuePriorityTrainingExample>&,
        const LearnedValuePriorityHeadUpdateConfig&)>&
        updater);

} // namespace testing

} // namespace old_school::fq4_dev_evaluator
