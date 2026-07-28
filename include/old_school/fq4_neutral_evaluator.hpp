#pragma once

#include "old_school/fq4_dev_evaluator.hpp"
#include "old_school/fq4_neutral_supplement.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_neutral_evaluator {

inline constexpr std::string_view kSchema =
    "old-school-fq4-dev5-neutral-evaluator-v1";
inline constexpr std::string_view
    kRequiredPositiveOnlyTrainingInputSha256 =
        "586b121c3c9bdb1a61305cac86882cd20b5d2ba332b4d5a54defc2c7756393a1";
inline constexpr std::string_view
    kRequiredPositiveOnlyCandidateFingerprint =
        "712600783152e89ff1a53394149764db227e55289a656530342226b7e1ee6151";

inline constexpr std::size_t kPositiveFitExamples = 88;
inline constexpr std::size_t kPositiveFitOptions = 548;
inline constexpr std::size_t kPositiveCheckExamples = 94;
inline constexpr std::size_t kPositiveCheckOptions = 571;
inline constexpr std::array<
    std::size_t, fq4_dev_bundle::kDeckCount>
    kPositiveFitExamplesByDeck{11, 4, 31, 13, 29};
inline constexpr std::array<
    std::size_t, fq4_dev_bundle::kDeckCount>
    kPositiveCheckExamplesByDeck{20, 5, 31, 7, 31};
inline constexpr std::array<
    std::size_t, fq4_dev_bundle::kDeckCount>
    kPositiveFitSupportViolationCeilings{1, 0, 2, 0, 0};

inline constexpr std::size_t kRequiredFitRepairs = 30;
inline constexpr std::size_t kRequiredCheckRepairs = 37;
inline constexpr std::size_t kRequiredFitSupportViolations = 3;
inline constexpr std::size_t kNeutralRowsPerDeck =
    fq4_neutral_supplement::kRowsPerDeckAndSplit;
inline constexpr std::size_t kNeutralRowsPerSplit =
    fq4_neutral_supplement::kRowsPerSplit;

struct PreparedNeutralAction {
    double base_score = 0.0;
    double parent_residual = 0.0;
    std::vector<double> features;

    bool operator==(
        const PreparedNeutralAction&) const = default;
};

struct PreparedNeutralRow {
    fq4_dev_bundle::Split split =
        fq4_dev_bundle::Split::Fit;
    std::uint8_t owner_deck = 0;
    std::vector<PreparedNeutralAction> actions;

    bool operator==(
        const PreparedNeutralRow&) const = default;
};

struct PreparedNeutralCorpus {
    std::vector<PreparedNeutralRow> fit;
    std::vector<PreparedNeutralRow> check;

    bool operator==(
        const PreparedNeutralCorpus&) const = default;
};

struct TrainingBatch {
    std::vector<LearnedValuePriorityTrainingExample> examples;
    fq4_dev_evaluator::FitAccounting accounting;
    std::size_t positive_examples = 0;
    std::size_t positive_options = 0;
    std::size_t neutral_examples = 0;
    std::size_t neutral_options = 0;
    std::array<std::size_t, fq4_dev_bundle::kDeckCount>
        neutral_examples_by_deck{};
    std::array<double, fq4_dev_bundle::kDeckCount>
        neutral_loss_mass_by_deck{};
};

struct NeutralScoreTriplet {
    std::uint8_t owner_deck = 0;
    std::vector<double> parent_combined_scores;
    std::vector<double> baseline_combined_scores;
    std::vector<double> anchored_combined_scores;

    bool operator==(const NeutralScoreTriplet&) const = default;
};

struct NeutralDeckMetrics {
    std::size_t rows = 0;
    std::size_t options = 0;
    double baseline_parent_to_candidate_kl = 0.0;
    double anchored_parent_to_candidate_kl = 0.0;
    std::size_t baseline_exact_support_changes = 0;
    std::size_t anchored_exact_support_changes = 0;

    bool operator==(const NeutralDeckMetrics&) const = default;
};

struct NeutralDriftMetrics {
    std::array<NeutralDeckMetrics, fq4_dev_bundle::kDeckCount>
        decks;
    std::size_t rows = 0;
    std::size_t options = 0;
    double baseline_equal_deck_kl = 0.0;
    double anchored_equal_deck_kl = 0.0;
    std::size_t baseline_exact_support_changes = 0;
    std::size_t anchored_exact_support_changes = 0;
    bool finite_probabilities = false;

    bool operator==(const NeutralDriftMetrics&) const = default;
};

struct IsolationChecks {
    bool parent_immutable = false;
    bool positive_only_candidate_exact = false;
    bool omitted_neutral_control_exact = false;
    bool parent_anchors_exact = false;
    bool hidden_repartition_contract_exact = false;
    bool fit_check_isolated = false;
    bool nonpriority_components_identical = false;
    bool priority_component_changed = false;

    bool operator==(const IsolationChecks&) const = default;
};

struct GateReport {
    bool baseline_positive_contract_exact = false;
    bool check_positive_clean = false;
    bool fit_positive_preserved = false;
    bool neutral_baseline_nonzero = false;
    bool neutral_per_deck_nonworsening = false;
    bool neutral_kl_halved = false;
    bool neutral_support_changes_halved = false;
    bool isolation_exact = false;
    std::vector<std::string> failures;

    bool passed() const {
        return failures.empty();
    }

    bool operator==(const GateReport&) const = default;
};

struct Report {
    std::shared_ptr<const LearnedModel> anchored_candidate;
    std::string parent_fingerprint;
    std::string positive_only_candidate_fingerprint;
    std::string anchored_candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints
        positive_only_candidate_components;
    LearnedModelComponentFingerprints
        anchored_candidate_components;
    TrainingBatch omitted_neutral_control;
    TrainingBatch anchored_training;
    fq4_dev_evaluator::ModelEvaluationReport
        positive_only_evaluation;
    fq4_dev_evaluator::ModelEvaluationReport
        anchored_evaluation;
    NeutralDriftMetrics neutral_check;
    IsolationChecks isolation;
    GateReport gate;
};

using FitBoundaryObserver = std::function<void(
    bool,
    const std::vector<LearnedValuePriorityTrainingExample>&,
    const LearnedValuePriorityHeadUpdateConfig&)>;

PreparedNeutralCorpus prepare(
    const fq4_neutral_supplement::Artifact& artifact);

std::vector<double> neutral_behavior_target(
    const PreparedNeutralRow& row);

NeutralDriftMetrics measure_neutral_check(
    std::span<const NeutralScoreTriplet> rows);

GateReport evaluate_gate(
    const fq4_dev_evaluator::EvaluationMetrics&
        positive_only,
    const fq4_dev_evaluator::EvaluationMetrics& anchored,
    const NeutralDriftMetrics& neutral,
    const IsolationChecks& isolation);

Report evaluate(
    const fq4_dev_evaluator::PreparedCorpus& positive_corpus,
    const fq4_neutral_supplement::Artifact&
        neutral_artifact,
    std::shared_ptr<const LearnedModel> frozen_c16,
    std::shared_ptr<const LearnedModel>
        exact_positive_only_candidate,
    FitBoundaryObserver observer = {});

namespace testing {

PreparedNeutralCorpus prepare_rows(
    std::span<const fq4_neutral_supplement::NeutralRow>
        rows);

TrainingBatch build_training_batch(
    const std::vector<LearnedValuePriorityTrainingExample>&
        positive_examples,
    const PreparedNeutralCorpus& neutral,
    bool include_neutral);

std::string training_input_sha256(
    const std::vector<LearnedValuePriorityTrainingExample>&
        examples,
    const LearnedValuePriorityHeadUpdateConfig& optimizer);

} // namespace testing

} // namespace old_school::fq4_neutral_evaluator
