#include "alpha/probe_eval.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace alpha::probe_eval {
namespace {

constexpr double kSchemaTolerance = 1.0e-10;

struct MetricAccumulator {
    std::size_t probe_count = 0;
    std::size_t stable_pair_count = 0;
    double top1_agreement_sum = 0.0;
    double stable_pair_agreement_sum = 0.0;
    double regret_sum = 0.0;
    double squared_error_sum = 0.0;
    double log_loss_sum = 0.0;
    double bias_sum = 0.0;
    std::array<std::size_t, kCalibrationBinCount> bin_counts{};
    std::array<double, kCalibrationBinCount> bin_prediction_sums{};
    std::array<double, kCalibrationBinCount> bin_reference_sums{};
};

bool is_probability(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

std::size_t deck_index(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return 0;
    case DeckId::Red:
        return 1;
    case DeckId::Blue:
        return 2;
    case DeckId::White:
        return 3;
    }
    throw std::invalid_argument("root_deck is outside the four-deck corpus");
}

std::string unordered_pair_key(std::string_view first,
                               std::string_view second) {
    if (second < first) {
        std::swap(first, second);
    }
    std::string key;
    key.reserve(first.size() + second.size() + 1);
    key.append(first);
    key.push_back('\0');
    key.append(second);
    return key;
}

double sample_standard_error(const std::vector<double>& samples) {
    double mean = 0.0;
    for (const double sample : samples) {
        mean += sample;
    }
    mean /= static_cast<double>(samples.size());

    double squared_deviation_sum = 0.0;
    for (const double sample : samples) {
        const double deviation = sample - mean;
        squared_deviation_sum += deviation * deviation;
    }
    const double sample_variance =
        squared_deviation_sum /
        static_cast<double>(samples.size() - 1);
    return std::sqrt(sample_variance /
                     static_cast<double>(samples.size()));
}

double mean_of(const std::vector<double>& samples) {
    double sum = 0.0;
    for (const double sample : samples) {
        sum += sample;
    }
    return sum / static_cast<double>(samples.size());
}

const PairLabel& find_pair(const ProbeLabel& label,
                           std::string_view first,
                           std::string_view second) {
    const auto found = std::find_if(
        label.pairs.begin(), label.pairs.end(),
        [first, second](const PairLabel& pair) {
            return (pair.first == first && pair.second == second) ||
                   (pair.first == second && pair.second == first);
        });
    if (found == label.pairs.end()) {
        throw std::invalid_argument(
            "probe label is missing a required candidate pair");
    }
    return *found;
}

double pair_standard_error(const ProbeLabel& label,
                           std::string_view first,
                           std::string_view second) {
    if (first == second) {
        return 0.0;
    }
    return find_pair(label, first, second).paired_standard_error;
}

std::unordered_map<std::string, double> candidate_q_map(
    const ProbeLabel& label) {
    std::unordered_map<std::string, double> result;
    result.reserve(label.candidates.size());
    for (const CandidateLabel& candidate : label.candidates) {
        result.emplace(candidate.key, candidate.q);
    }
    return result;
}

std::unordered_map<std::string, double> policy_score_map(
    const ProbePrediction& prediction) {
    std::unordered_map<std::string, double> result;
    result.reserve(prediction.policy_scores.size());
    for (const PolicyScore& policy_score : prediction.policy_scores) {
        result.emplace(policy_score.key, policy_score.score);
    }
    return result;
}

bool is_stable_pair(const PairLabel& pair) {
    const double magnitude = std::abs(pair.delta_q);
    return magnitude >= kStablePairMinimumDelta &&
           magnitude >
               kNormal95CriticalValue * pair.paired_standard_error;
}

std::size_t calibration_bin(double prediction) {
    if (prediction >= 1.0) {
        return kCalibrationBinCount - 1;
    }
    const auto bin = static_cast<std::size_t>(
        prediction * static_cast<double>(kCalibrationBinCount));
    return std::min(bin, kCalibrationBinCount - 1);
}

void add_probe_metrics(MetricAccumulator& accumulator,
                       const ProbeLabel& label,
                       const ProbePrediction& prediction) {
    const auto scores = policy_score_map(prediction);
    const auto candidate_qs = candidate_q_map(label);

    double highest_student_score =
        -std::numeric_limits<double>::infinity();
    for (const CandidateLabel& candidate : label.candidates) {
        highest_student_score =
            std::max(highest_student_score, scores.at(candidate.key));
    }

    std::size_t student_argmax_count = 0;
    std::size_t student_reference_overlap = 0;
    double selected_q_sum = 0.0;
    for (const CandidateLabel& candidate : label.candidates) {
        if (scores.at(candidate.key) != highest_student_score) {
            continue;
        }
        ++student_argmax_count;
        selected_q_sum += candidate.q;
        if (std::find(label.reference_best_set.begin(),
                      label.reference_best_set.end(),
                      candidate.key) != label.reference_best_set.end()) {
            ++student_reference_overlap;
        }
    }

    accumulator.top1_agreement_sum +=
        static_cast<double>(student_reference_overlap) /
        static_cast<double>(student_argmax_count);
    accumulator.regret_sum +=
        label.reference_value -
        selected_q_sum / static_cast<double>(student_argmax_count);

    for (const PairLabel& pair : label.pairs) {
        if (!is_stable_pair(pair)) {
            continue;
        }
        ++accumulator.stable_pair_count;
        const double student_delta =
            scores.at(pair.first) - scores.at(pair.second);
        if (student_delta == 0.0) {
            accumulator.stable_pair_agreement_sum += 0.5;
        } else if ((student_delta > 0.0) ==
                   (pair.delta_q > 0.0)) {
            accumulator.stable_pair_agreement_sum += 1.0;
        }
    }

    const double error =
        prediction.critic_value - label.reference_value;
    accumulator.squared_error_sum += error * error;
    accumulator.bias_sum += error;
    const double clamped_prediction =
        std::clamp(prediction.critic_value, kLogLossClamp,
                   1.0 - kLogLossClamp);
    accumulator.log_loss_sum +=
        -label.reference_value * std::log(clamped_prediction) -
        (1.0 - label.reference_value) *
            std::log(1.0 - clamped_prediction);

    const std::size_t bin = calibration_bin(prediction.critic_value);
    ++accumulator.bin_counts[bin];
    accumulator.bin_prediction_sums[bin] += prediction.critic_value;
    accumulator.bin_reference_sums[bin] += label.reference_value;
    ++accumulator.probe_count;
}

double expected_calibration_error(
    const MetricAccumulator& accumulator) {
    if (accumulator.probe_count == 0) {
        return 0.0;
    }
    double weighted_error = 0.0;
    for (std::size_t bin = 0; bin < kCalibrationBinCount; ++bin) {
        if (accumulator.bin_counts[bin] == 0) {
            continue;
        }
        const double count =
            static_cast<double>(accumulator.bin_counts[bin]);
        const double prediction_mean =
            accumulator.bin_prediction_sums[bin] / count;
        const double reference_mean =
            accumulator.bin_reference_sums[bin] / count;
        weighted_error +=
            count * std::abs(prediction_mean - reference_mean);
    }
    return weighted_error /
           static_cast<double>(accumulator.probe_count);
}

DeckProbeMetrics finalize_deck_metrics(
    const MetricAccumulator& accumulator, DeckId root_deck) {
    DeckProbeMetrics metrics;
    metrics.root_deck = root_deck;
    metrics.probe_count = accumulator.probe_count;
    metrics.stable_pair_count = accumulator.stable_pair_count;
    if (accumulator.probe_count == 0) {
        return metrics;
    }

    const double probe_count =
        static_cast<double>(accumulator.probe_count);
    metrics.top1_expected_agreement =
        accumulator.top1_agreement_sum / probe_count;
    metrics.mean_regret = accumulator.regret_sum / probe_count;
    metrics.critic_brier =
        accumulator.squared_error_sum / probe_count;
    metrics.critic_mse = metrics.critic_brier;
    metrics.critic_log_loss =
        accumulator.log_loss_sum / probe_count;
    metrics.critic_bias = accumulator.bias_sum / probe_count;
    metrics.critic_ece =
        expected_calibration_error(accumulator);
    if (accumulator.stable_pair_count != 0) {
        metrics.stable_pair_agreement =
            accumulator.stable_pair_agreement_sum /
            static_cast<double>(accumulator.stable_pair_count);
    }
    return metrics;
}

void copy_pooled_metrics(const DeckProbeMetrics& pooled,
                         ProbeMetricSummary& summary) {
    summary.probe_count = pooled.probe_count;
    summary.stable_pair_count = pooled.stable_pair_count;
    summary.top1_expected_agreement =
        pooled.top1_expected_agreement;
    summary.stable_pair_agreement =
        pooled.stable_pair_agreement;
    summary.mean_regret = pooled.mean_regret;
    summary.critic_brier = pooled.critic_brier;
    summary.critic_mse = pooled.critic_mse;
    summary.critic_log_loss = pooled.critic_log_loss;
    summary.critic_bias = pooled.critic_bias;
    summary.critic_ece = pooled.critic_ece;
}

} // namespace

ProbeLabel make_probe_label(
    std::string stable_id, DeckId root_deck,
    const std::vector<CandidateSamples>& candidate_samples) {
    if (stable_id.empty()) {
        throw std::invalid_argument("probe stable_id must not be empty");
    }
    (void)deck_index(root_deck);
    if (candidate_samples.size() < 2) {
        throw std::invalid_argument(
            "a probe label requires at least two candidates");
    }
    const std::size_t sample_count =
        candidate_samples.front().q_samples.size();
    if (sample_count < 2) {
        throw std::invalid_argument(
            "paired Q labels require at least two samples");
    }

    std::unordered_set<std::string> keys;
    keys.reserve(candidate_samples.size());
    for (const CandidateSamples& candidate : candidate_samples) {
        if (candidate.key.empty() ||
            !keys.insert(candidate.key).second) {
            throw std::invalid_argument(
                "candidate sample keys must be nonempty and unique");
        }
        if (candidate.q_samples.size() != sample_count) {
            throw std::invalid_argument(
                "candidate Q sample vectors must be aligned");
        }
        for (const double sample : candidate.q_samples) {
            if (!is_probability(sample)) {
                throw std::invalid_argument(
                    "candidate Q samples must be finite probabilities");
            }
        }
    }

    ProbeLabel label;
    label.stable_id = std::move(stable_id);
    label.root_deck = root_deck;
    label.candidates.reserve(candidate_samples.size());
    for (const CandidateSamples& candidate : candidate_samples) {
        label.candidates.push_back(CandidateLabel{
            candidate.key, mean_of(candidate.q_samples),
            sample_standard_error(candidate.q_samples)});
    }

    label.pairs.reserve(candidate_samples.size() *
                        (candidate_samples.size() - 1) / 2);
    for (std::size_t first = 0;
         first < candidate_samples.size(); ++first) {
        for (std::size_t second = first + 1;
             second < candidate_samples.size(); ++second) {
            std::vector<double> differences;
            differences.reserve(sample_count);
            for (std::size_t sample = 0; sample < sample_count;
                 ++sample) {
                differences.push_back(
                    candidate_samples[first].q_samples[sample] -
                    candidate_samples[second].q_samples[sample]);
            }
            label.pairs.push_back(PairLabel{
                candidate_samples[first].key,
                candidate_samples[second].key,
                mean_of(differences),
                sample_standard_error(differences)});
        }
    }

    const auto best = std::max_element(
        label.candidates.begin(), label.candidates.end(),
        [](const CandidateLabel& first,
           const CandidateLabel& second) {
            return first.q < second.q;
        });
    label.reference_value = best->q;
    for (const CandidateLabel& candidate : label.candidates) {
        const double uncertainty =
            kNormal95CriticalValue *
            pair_standard_error(label, best->key, candidate.key);
        const double tolerance =
            std::max(kReferenceBestMinimumTolerance, uncertainty);
        if (best->q - candidate.q <=
            tolerance + kSchemaTolerance) {
            label.reference_best_set.push_back(candidate.key);
        }
    }

    validate_probe_label(label);
    return label;
}

void validate_probe_label(const ProbeLabel& label) {
    if (label.stable_id.empty()) {
        throw std::invalid_argument("probe stable_id must not be empty");
    }
    (void)deck_index(label.root_deck);
    if (label.candidates.size() < 2) {
        throw std::invalid_argument(
            "a probe label requires at least two candidates");
    }
    if (!is_probability(label.reference_value)) {
        throw std::invalid_argument(
            "reference_value must be a finite probability");
    }

    std::unordered_map<std::string, double> candidate_qs;
    candidate_qs.reserve(label.candidates.size());
    double maximum_q = -std::numeric_limits<double>::infinity();
    for (const CandidateLabel& candidate : label.candidates) {
        if (candidate.key.empty() ||
            !candidate_qs.emplace(candidate.key, candidate.q).second) {
            throw std::invalid_argument(
                "candidate label keys must be nonempty and unique");
        }
        if (!is_probability(candidate.q) ||
            !std::isfinite(candidate.standard_error) ||
            candidate.standard_error < 0.0) {
            throw std::invalid_argument(
                "candidate label contains an invalid Q estimate");
        }
        maximum_q = std::max(maximum_q, candidate.q);
    }
    if (std::abs(label.reference_value - maximum_q) >
        kSchemaTolerance) {
        throw std::invalid_argument(
            "reference_value must equal the maximum candidate Q");
    }

    const std::size_t expected_pair_count =
        label.candidates.size() * (label.candidates.size() - 1) / 2;
    if (label.pairs.size() != expected_pair_count) {
        throw std::invalid_argument(
            "probe label must contain every unordered candidate pair");
    }
    std::unordered_set<std::string> seen_pairs;
    seen_pairs.reserve(label.pairs.size());
    for (const PairLabel& pair : label.pairs) {
        if (pair.first == pair.second ||
            !candidate_qs.contains(pair.first) ||
            !candidate_qs.contains(pair.second)) {
            throw std::invalid_argument(
                "pair label references invalid candidate keys");
        }
        if (!std::isfinite(pair.delta_q) ||
            !std::isfinite(pair.paired_standard_error) ||
            pair.paired_standard_error < 0.0) {
            throw std::invalid_argument(
                "pair label contains an invalid estimate");
        }
        if (!seen_pairs
                 .insert(unordered_pair_key(pair.first, pair.second))
                 .second) {
            throw std::invalid_argument(
                "pair labels contain a duplicate unordered pair");
        }
        const double expected_delta =
            candidate_qs.at(pair.first) -
            candidate_qs.at(pair.second);
        if (std::abs(pair.delta_q - expected_delta) >
            kSchemaTolerance) {
            throw std::invalid_argument(
                "pair delta does not match candidate Q values");
        }
    }

    if (label.reference_best_set.empty()) {
        throw std::invalid_argument(
            "reference_best_set must not be empty");
    }
    std::unordered_set<std::string> best_keys;
    best_keys.reserve(label.reference_best_set.size());
    bool contains_maximum = false;
    for (const std::string& key : label.reference_best_set) {
        if (!candidate_qs.contains(key) ||
            !best_keys.insert(key).second) {
            throw std::invalid_argument(
                "reference_best_set contains invalid or duplicate keys");
        }
        if (std::abs(candidate_qs.at(key) - maximum_q) <=
            kSchemaTolerance) {
            contains_maximum = true;
        }
    }
    if (!contains_maximum) {
        throw std::invalid_argument(
            "reference_best_set must contain a maximum-Q candidate");
    }
}

void validate_probe_labels(const std::vector<ProbeLabel>& labels) {
    if (labels.empty()) {
        throw std::invalid_argument(
            "probe evaluation requires at least one label");
    }
    std::unordered_set<std::string> stable_ids;
    stable_ids.reserve(labels.size());
    for (const ProbeLabel& label : labels) {
        validate_probe_label(label);
        if (!stable_ids.insert(label.stable_id).second) {
            throw std::invalid_argument(
                "probe label stable_ids must be unique");
        }
    }
}

void validate_probe_predictions(
    const std::vector<ProbeLabel>& labels,
    const std::vector<ProbePrediction>& predictions) {
    validate_probe_labels(labels);
    if (predictions.size() != labels.size()) {
        throw std::invalid_argument(
            "predictions must cover every label exactly once");
    }

    std::unordered_map<std::string, const ProbeLabel*> labels_by_id;
    labels_by_id.reserve(labels.size());
    for (const ProbeLabel& label : labels) {
        labels_by_id.emplace(label.stable_id, &label);
    }

    std::unordered_set<std::string> prediction_ids;
    prediction_ids.reserve(predictions.size());
    for (const ProbePrediction& prediction : predictions) {
        if (!prediction_ids.insert(prediction.stable_id).second) {
            throw std::invalid_argument(
                "prediction stable_ids must be unique");
        }
        const auto label = labels_by_id.find(prediction.stable_id);
        if (label == labels_by_id.end()) {
            throw std::invalid_argument(
                "prediction references an unknown stable_id");
        }
        if (!is_probability(prediction.critic_value)) {
            throw std::invalid_argument(
                "critic_value must be a finite probability");
        }
        if (prediction.policy_scores.size() !=
            label->second->candidates.size()) {
            throw std::invalid_argument(
                "policy scores must cover every candidate exactly once");
        }

        std::unordered_set<std::string> label_keys;
        label_keys.reserve(label->second->candidates.size());
        for (const CandidateLabel& candidate :
             label->second->candidates) {
            label_keys.insert(candidate.key);
        }
        std::unordered_set<std::string> prediction_keys;
        prediction_keys.reserve(prediction.policy_scores.size());
        for (const PolicyScore& score : prediction.policy_scores) {
            if (!label_keys.contains(score.key) ||
                !prediction_keys.insert(score.key).second ||
                !std::isfinite(score.score)) {
                throw std::invalid_argument(
                    "policy scores contain invalid candidate coverage");
            }
        }
    }
}

ProbeMetricSummary evaluate_probe_predictions(
    const std::vector<ProbeLabel>& labels,
    const std::vector<ProbePrediction>& predictions) {
    validate_probe_predictions(labels, predictions);

    std::unordered_map<std::string, const ProbePrediction*>
        predictions_by_id;
    predictions_by_id.reserve(predictions.size());
    for (const ProbePrediction& prediction : predictions) {
        predictions_by_id.emplace(prediction.stable_id, &prediction);
    }

    MetricAccumulator pooled;
    std::array<MetricAccumulator, 4> by_deck;
    for (const ProbeLabel& label : labels) {
        const ProbePrediction& prediction =
            *predictions_by_id.at(label.stable_id);
        add_probe_metrics(pooled, label, prediction);
        add_probe_metrics(by_deck[deck_index(label.root_deck)],
                          label, prediction);
    }

    ProbeMetricSummary summary;
    copy_pooled_metrics(
        finalize_deck_metrics(pooled, DeckId::Green), summary);
    constexpr std::array<DeckId, 4> kDecks = {
        DeckId::Green, DeckId::Red, DeckId::Blue, DeckId::White};
    for (std::size_t deck = 0; deck < kDecks.size(); ++deck) {
        summary.by_deck[deck] =
            finalize_deck_metrics(by_deck[deck], kDecks[deck]);
    }
    return summary;
}

} // namespace alpha::probe_eval
