#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace old_school::probe_eval {

inline constexpr double kStablePairMinimumDelta = 0.03;
inline constexpr double kReferenceBestMinimumTolerance = 0.01;
inline constexpr double kNormal95CriticalValue = 1.96;
inline constexpr double kLogLossClamp = 1.0e-12;
inline constexpr std::size_t kCalibrationBinCount = 5;

struct CandidateLabel {
    std::string key;
    double q = 0.0;
    double standard_error = 0.0;
};

struct PairLabel {
    std::string first;
    std::string second;
    double delta_q = 0.0;
    double paired_standard_error = 0.0;
};

struct ProbeLabel {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::vector<CandidateLabel> candidates;
    std::vector<PairLabel> pairs;
    std::vector<std::string> reference_best_set;
    // Maximum candidate Q. This defines regret and the best set; critic
    // calibration instead uses the Q of the policy's deployed-selected
    // action.
    double reference_value = 0.0;
};

struct PolicyScore {
    std::string key;
    double score = 0.0;
};

struct ProbePrediction {
    ProbePrediction() = default;
    ProbePrediction(
        std::string stable_id_value,
        std::vector<PolicyScore> policy_scores_value,
        double critic_value_value,
        std::optional<std::string> selected_key_value =
            std::nullopt)
        : stable_id(std::move(stable_id_value)),
          policy_scores(std::move(policy_scores_value)),
          critic_value(critic_value_value),
          selected_key(std::move(selected_key_value)) {}

    std::string stable_id;
    std::vector<PolicyScore> policy_scores;
    // State value under the predicted policy. Evaluation compares it with
    // the reference Q of that policy's deployed-selected action.
    double critic_value = 0.0;
    // Most policies randomize exact score ties, represented by nullopt and
    // evaluated as the uniform expectation. Deterministic deployed selectors
    // provide the exact selected candidate key.
    std::optional<std::string> selected_key;
};

struct CandidateSamples {
    std::string key;
    // Samples at the same index across candidates must come from the same
    // sampled world. That pairing is what makes action-difference uncertainty
    // substantially more useful than independent standard errors.
    std::vector<double> q_samples;
};

struct DeckProbeMetrics {
    DeckId root_deck = DeckId::Green;
    std::size_t probe_count = 0;
    std::size_t stable_pair_count = 0;
    double top1_expected_agreement = 0.0;
    double stable_pair_agreement = 0.0;
    double mean_regret = 0.0;
    // Brier and MSE are both exposed for reporting clarity. For a scalar
    // Bernoulli probability target they are the same squared-error score.
    double critic_brier = 0.0;
    double critic_mse = 0.0;
    double critic_log_loss = 0.0;
    double critic_bias = 0.0;
    double critic_ece = 0.0;
};

struct ProbeMetricSummary {
    std::size_t probe_count = 0;
    std::size_t stable_pair_count = 0;
    double top1_expected_agreement = 0.0;
    double stable_pair_agreement = 0.0;
    double mean_regret = 0.0;
    double critic_brier = 0.0;
    double critic_mse = 0.0;
    double critic_log_loss = 0.0;
    double critic_bias = 0.0;
    double critic_ece = 0.0;
    // The current fixed dev corpus covers Green, Red, Blue, and White.
    // Evaluation rejects RU Aggro until its probes are authored.
    std::array<DeckProbeMetrics, 4> by_deck{};
};

struct DeckCandidateQFitMetrics {
    DeckId root_deck = DeckId::Green;
    std::size_t candidate_count = 0;
    double mae = 0.0;
    double rmse = 0.0;
};

struct CandidateQFitSummary {
    std::size_t candidate_count = 0;
    double mae = 0.0;
    double rmse = 0.0;
    // Green, Red, Blue, and White only; see ProbeMetricSummary.
    std::array<DeckCandidateQFitMetrics, 4> by_deck{};
};

// Builds a deterministic reference label from aligned, per-action Q samples.
// Candidate means and standard errors use the sample variance. Every unordered
// candidate pair receives a paired difference and paired standard error.
// The best set contains each action whose gap from the highest-mean action is
// within max(0.01, 1.96 * paired standard error).
ProbeLabel make_probe_label(
    std::string stable_id, DeckId root_deck,
    const std::vector<CandidateSamples>& candidate_samples);

// Validation failures are programming/data errors and throw
// std::invalid_argument. Evaluation calls both validators before computing any
// metrics.
void validate_probe_label(const ProbeLabel& label);
void validate_probe_labels(const std::vector<ProbeLabel>& labels);
void validate_probe_predictions(
    const std::vector<ProbeLabel>& labels,
    const std::vector<ProbePrediction>& predictions);

// Labels and predictions are matched by stable_id and candidate key, never by
// input order. Student policy scores may be arbitrary finite logits/scores.
// Regret and best-set metrics use the maximum reference Q. Critic metrics use
// the reference Q of the policy's deployed-selected action; multiple exact
// top-scored actions are averaged uniformly. Critic values and reference Q
// values are probabilities in [0, 1].
ProbeMetricSummary evaluate_probe_predictions(
    const std::vector<ProbeLabel>& labels,
    const std::vector<ProbePrediction>& predictions);

// Measures candidate-level Q fit for scorers whose policy_scores are actual
// outcome probabilities, not logits or hand-authored rankings. Labels and
// predictions are matched by stable_id and candidate key. The pooled and
// per-deck summaries are candidate-weighted.
CandidateQFitSummary evaluate_candidate_q_fit(
    const std::vector<ProbeLabel>& labels,
    const std::vector<ProbePrediction>& q_predictions);

} // namespace old_school::probe_eval
