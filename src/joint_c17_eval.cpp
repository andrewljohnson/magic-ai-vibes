#include "old_school/joint_c17_eval.hpp"

#include "old_school/audit_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::joint_c17_eval {
namespace {

constexpr std::size_t kControlModelIndex =
    static_cast<std::size_t>(
        terminal_weight_eval::CriticModel::TW50);
constexpr std::size_t kTreatmentModelIndex =
    static_cast<std::size_t>(
        terminal_weight_eval::CriticModel::TW75);

void record_failure(
    bool condition, std::string_view message,
    std::vector<std::string>& failures) {
    if (!condition) {
        failures.emplace_back(message);
    }
}

bool finite_estimate(
    const terminal_weight_eval::ClusteredEstimate& estimate) {
    return std::isfinite(estimate.mean) &&
           std::isfinite(estimate.standard_error) &&
           std::isfinite(estimate.confidence_lower_95) &&
           std::isfinite(estimate.confidence_upper_95);
}

bool estimate_has_material_bias(
    const terminal_weight_eval::ClusteredEstimate& estimate) {
    return std::abs(estimate.mean) >= kMaterialSignedBias &&
           (estimate.confidence_lower_95 > 0.0 ||
            estimate.confidence_upper_95 < 0.0);
}

std::optional<std::size_t> deck_index(DeckId deck) {
    const auto index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        return std::nullopt;
    }
    return index;
}

bool contains_key(
    const std::vector<std::string>& keys,
    std::string_view wanted) {
    return std::any_of(
        keys.begin(), keys.end(),
        [wanted](const std::string& key) {
            return key == wanted;
        });
}

bool has_duplicate_keys(
    const std::vector<std::string>& keys) {
    for (std::size_t first = 0; first < keys.size(); ++first) {
        for (std::size_t second = first + 1;
             second < keys.size(); ++second) {
            if (keys[first] == keys[second]) {
                return true;
            }
        }
    }
    return false;
}

bool label_has_candidate(
    const probe_eval::ProbeLabel& label,
    std::string_view key) {
    return std::any_of(
        label.candidates.begin(), label.candidates.end(),
        [key](const probe_eval::CandidateLabel& candidate) {
            return candidate.key == key;
        });
}

bool valid_label(const probe_eval::ProbeLabel& label) {
    if (label.stable_id.empty() ||
        !deck_index(label.root_deck).has_value() ||
        label.candidates.empty() ||
        label.reference_best_set.empty() ||
        has_duplicate_keys(label.reference_best_set) ||
        !std::isfinite(label.reference_value) ||
        label.reference_value < 0.0 ||
        label.reference_value > 1.0) {
        return false;
    }

    std::vector<std::string> candidate_keys;
    candidate_keys.reserve(label.candidates.size());
    for (const auto& candidate : label.candidates) {
        if (candidate.key.empty() ||
            !std::isfinite(candidate.q) ||
            candidate.q < 0.0 || candidate.q > 1.0 ||
            !std::isfinite(candidate.standard_error) ||
            candidate.standard_error < 0.0) {
            return false;
        }
        candidate_keys.push_back(candidate.key);
    }
    if (has_duplicate_keys(candidate_keys)) {
        return false;
    }
    for (const auto& best : label.reference_best_set) {
        if (!label_has_candidate(label, best)) {
            return false;
        }
    }

    for (const auto& pair : label.pairs) {
        if (pair.first.empty() || pair.second.empty() ||
            pair.first == pair.second ||
            !label_has_candidate(label, pair.first) ||
            !label_has_candidate(label, pair.second) ||
            !std::isfinite(pair.delta_q) ||
            !std::isfinite(pair.paired_standard_error) ||
            pair.paired_standard_error < 0.0) {
            return false;
        }
    }

    for (std::size_t first = 0;
         first < candidate_keys.size(); ++first) {
        for (std::size_t second = first + 1;
             second < candidate_keys.size(); ++second) {
            std::size_t pair_count = 0;
            for (const auto& pair : label.pairs) {
                if ((pair.first == candidate_keys[first] &&
                     pair.second == candidate_keys[second]) ||
                    (pair.first == candidate_keys[second] &&
                     pair.second == candidate_keys[first])) {
                    ++pair_count;
                }
            }
            if (pair_count != 1) {
                return false;
            }
        }
    }
    const std::size_t expected_pairs =
        candidate_keys.size() *
        (candidate_keys.size() - 1) / 2;
    return label.pairs.size() == expected_pairs;
}

std::optional<std::pair<double, double>> oriented_pair(
    const probe_eval::ProbeLabel& label,
    std::string_view first, std::string_view second) {
    std::optional<std::pair<double, double>> result;
    for (const auto& pair : label.pairs) {
        if (pair.first == first && pair.second == second) {
            if (result.has_value()) {
                return std::nullopt;
            }
            result = std::pair{
                pair.delta_q, pair.paired_standard_error};
        } else if (
            pair.first == second && pair.second == first) {
            if (result.has_value()) {
                return std::nullopt;
            }
            result = std::pair{
                -pair.delta_q, pair.paired_standard_error};
        }
    }
    return result;
}

bool stable_positive_pair(
    const probe_eval::ProbeLabel& label,
    std::string_view best, std::string_view outside) {
    const auto pair = oriented_pair(label, best, outside);
    if (!pair.has_value()) {
        return false;
    }
    const double delta = pair->first;
    const double standard_error = pair->second;
    return delta > 0.0 &&
           std::abs(delta) >=
               probe_eval::kStablePairMinimumDelta &&
           std::abs(delta) >
               probe_eval::kNormal95CriticalValue *
                   standard_error;
}

std::size_t label_count(
    std::span<const probe_eval::ProbeLabel> labels,
    std::string_view stable_id) {
    return static_cast<std::size_t>(std::count_if(
        labels.begin(), labels.end(),
        [stable_id](const probe_eval::ProbeLabel& label) {
            return label.stable_id == stable_id;
        }));
}

const probe_eval::ProbeLabel* find_unique_label(
    std::span<const probe_eval::ProbeLabel> labels,
    std::string_view stable_id) {
    const probe_eval::ProbeLabel* result = nullptr;
    for (const auto& label : labels) {
        if (label.stable_id != stable_id) {
            continue;
        }
        if (result != nullptr) {
            return nullptr;
        }
        result = &label;
    }
    return result;
}

std::size_t decision_count(
    std::span<
        const probe_runner::ValueProbeDecisionDetail>
        decisions,
    std::string_view stable_id) {
    return static_cast<std::size_t>(std::count_if(
        decisions.begin(), decisions.end(),
        [stable_id](
            const probe_runner::ValueProbeDecisionDetail&
                decision) {
            return decision.stable_id == stable_id;
        }));
}

const probe_runner::ValueProbeDecisionDetail*
find_unique_decision(
    std::span<
        const probe_runner::ValueProbeDecisionDetail>
        decisions,
    std::string_view stable_id) {
    const probe_runner::ValueProbeDecisionDetail* result =
        nullptr;
    for (const auto& decision : decisions) {
        if (decision.stable_id != stable_id) {
            continue;
        }
        if (result != nullptr) {
            return nullptr;
        }
        result = &decision;
    }
    return result;
}

bool valid_decision(
    const probe_runner::ValueProbeDecisionDetail& decision,
    const probe_eval::ProbeLabel& label) {
    if (decision.stable_id != label.stable_id ||
        decision.root_deck != label.root_deck ||
        decision.selected_keys.empty() ||
        has_duplicate_keys(decision.selected_keys) ||
        (decision.deterministic_selection &&
         decision.selected_keys.size() != 1)) {
        return false;
    }
    return std::all_of(
        decision.selected_keys.begin(),
        decision.selected_keys.end(),
        [&label](const std::string& key) {
            return label_has_candidate(label, key);
        });
}

bool selection_agrees(
    const probe_runner::ValueProbeDecisionDetail& decision,
    const probe_eval::ProbeLabel& label) {
    return std::any_of(
        decision.selected_keys.begin(),
        decision.selected_keys.end(),
        [&label](const std::string& selected) {
            return contains_key(
                label.reference_best_set, selected);
        });
}

bool unique_selection(
    const probe_runner::ForceSpikeControlDecision& decision,
    std::string_view expected) {
    return decision.selected_keys.size() == 1 &&
           decision.selected_keys.front() == expected;
}

bool finite_deep_metrics(
    const probe_eval::ProbeMetricSummary& metrics) {
    const auto finite_scope =
        [](double top_one, double stable_pair,
           double regret, double brier, double mse,
           double log_loss, double bias, double ece) {
            return std::isfinite(top_one) &&
                   std::isfinite(stable_pair) &&
                   std::isfinite(regret) &&
                   std::isfinite(brier) &&
                   std::isfinite(mse) &&
                   std::isfinite(log_loss) &&
                   std::isfinite(bias) &&
                   std::isfinite(ece);
        };
    if (!finite_scope(
            metrics.top1_expected_agreement,
            metrics.stable_pair_agreement,
            metrics.mean_regret, metrics.critic_brier,
            metrics.critic_mse, metrics.critic_log_loss,
            metrics.critic_bias, metrics.critic_ece)) {
        return false;
    }
    return std::all_of(
        metrics.by_deck.begin(), metrics.by_deck.end(),
        [&finite_scope](
            const probe_eval::DeckProbeMetrics& deck) {
            return finite_scope(
                deck.top1_expected_agreement,
                deck.stable_pair_agreement,
                deck.mean_regret, deck.critic_brier,
                deck.critic_mse, deck.critic_log_loss,
                deck.critic_bias, deck.critic_ece);
        });
}

struct CommonCriticAccumulator {
    std::size_t probe_count = 0;
    double squared_error_sum = 0.0;
    double log_loss_sum = 0.0;
    double bias_sum = 0.0;
    std::array<
        std::size_t, probe_eval::kCalibrationBinCount>
        bin_counts{};
    std::array<double, probe_eval::kCalibrationBinCount>
        bin_prediction_sums{};
    std::array<double, probe_eval::kCalibrationBinCount>
        bin_reference_sums{};
};

bool is_probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

std::size_t calibration_bin(double prediction) {
    if (prediction >= 1.0) {
        return probe_eval::kCalibrationBinCount - 1;
    }
    const auto index = static_cast<std::size_t>(
        prediction *
        static_cast<double>(
            probe_eval::kCalibrationBinCount));
    return std::min(
        index, probe_eval::kCalibrationBinCount - 1);
}

void add_common_critic_observation(
    CommonCriticAccumulator& accumulator,
    double prediction, double reference) {
    const double error = prediction - reference;
    accumulator.squared_error_sum += error * error;
    accumulator.bias_sum += error;
    accumulator.log_loss_sum +=
        audit_common::soft_log_loss(
            prediction, reference);
    const std::size_t bin = calibration_bin(prediction);
    ++accumulator.bin_counts[bin];
    accumulator.bin_prediction_sums[bin] += prediction;
    accumulator.bin_reference_sums[bin] += reference;
    ++accumulator.probe_count;
}

CommonStateCriticMetrics finalize_common_critic(
    const CommonCriticAccumulator& accumulator) {
    CommonStateCriticMetrics metrics;
    metrics.probe_count = accumulator.probe_count;
    if (accumulator.probe_count == 0) {
        return metrics;
    }
    const double count =
        static_cast<double>(accumulator.probe_count);
    metrics.brier =
        accumulator.squared_error_sum / count;
    metrics.soft_log_loss =
        accumulator.log_loss_sum / count;
    metrics.signed_bias = accumulator.bias_sum / count;
    double weighted_calibration_error = 0.0;
    for (std::size_t bin = 0;
         bin < probe_eval::kCalibrationBinCount; ++bin) {
        if (accumulator.bin_counts[bin] == 0) {
            continue;
        }
        const double bin_count = static_cast<double>(
            accumulator.bin_counts[bin]);
        const double prediction_mean =
            accumulator.bin_prediction_sums[bin] /
            bin_count;
        const double reference_mean =
            accumulator.bin_reference_sums[bin] /
            bin_count;
        weighted_calibration_error +=
            bin_count *
            std::abs(prediction_mean - reference_mean);
    }
    metrics.ece = weighted_calibration_error / count;
    return metrics;
}

bool finite_common_metrics(
    const CommonStateCriticMetrics& metrics) {
    return std::isfinite(metrics.brier) &&
           std::isfinite(metrics.soft_log_loss) &&
           std::isfinite(metrics.signed_bias) &&
           std::isfinite(metrics.ece);
}

} // namespace

HeldoutGateReport evaluate_heldout_gate(
    const terminal_weight_eval::HoldoutReport& report) {
    HeldoutGateReport gate;

    std::size_t summed_records = 0;
    std::size_t summed_perspectives = 0;
    bool deck_accounting_exact = true;
    for (const auto& deck : report.by_deck) {
        summed_records += deck.records;
        summed_perspectives += deck.perspectives;
        deck_accounting_exact =
            deck_accounting_exact &&
            deck.records != 0 &&
            deck.perspectives ==
                terminal_weight_eval::
                    kHoldoutPerspectivesPerDeck;
    }
    gate.accounting_exact =
        deck_accounting_exact &&
        report.pooled.records != 0 &&
        report.pooled.records == summed_records &&
        report.pooled.perspectives ==
            kDeckCount *
                terminal_weight_eval::
                    kHoldoutPerspectivesPerDeck &&
        report.pooled.perspectives ==
            summed_perspectives &&
        report.pooled.physical_games ==
            terminal_weight_eval::kHoldoutPhysicalGames;

    const auto& pooled_comparison =
        report.pooled.treatment_comparisons[0];
    gate.inputs_finite =
        finite_estimate(pooled_comparison.brier_delta) &&
        finite_estimate(
            pooled_comparison.soft_log_loss_delta);

    for (const auto& deck : report.by_deck) {
        const auto& comparison =
            deck.treatment_comparisons[0];
        const auto& control_bias =
            deck.models[kControlModelIndex].signed_bias;
        const auto& treatment_bias =
            deck.models[kTreatmentModelIndex].signed_bias;
        gate.inputs_finite =
            gate.inputs_finite &&
            finite_estimate(comparison.brier_delta) &&
            finite_estimate(
                comparison.soft_log_loss_delta) &&
            finite_estimate(control_bias) &&
            finite_estimate(treatment_bias);
    }

    gate.pooled_losses_improved =
        pooled_comparison.brier_delta.confidence_upper_95 <
            0.0 &&
        pooled_comparison.soft_log_loss_delta
                .confidence_upper_95 <
            0.0;

    gate.every_deck_loss_guard = std::all_of(
        report.by_deck.begin(), report.by_deck.end(),
        [](const terminal_weight_eval::HoldoutScopeMetrics&
               deck) {
            const auto& comparison =
                deck.treatment_comparisons[0];
            return comparison.brier_delta.mean <=
                       kMaximumDeckLossDelta &&
                   comparison.soft_log_loss_delta.mean <=
                       kMaximumDeckLossDelta;
        });

    const auto treatment_abs_bias =
        [&report](DeckId deck) {
            return std::abs(
                report
                    .by_deck[static_cast<std::size_t>(deck)]
                    .models[kTreatmentModelIndex]
                    .signed_bias.mean);
        };
    const auto control_abs_bias =
        [&report](DeckId deck) {
            return std::abs(
                report
                    .by_deck[static_cast<std::size_t>(deck)]
                    .models[kControlModelIndex]
                    .signed_bias.mean);
        };

    gate.green_bias_strictly_shrank =
        treatment_abs_bias(DeckId::Green) <
        control_abs_bias(DeckId::Green);
    gate.blue_bias_strictly_shrank =
        treatment_abs_bias(DeckId::Blue) <
        control_abs_bias(DeckId::Blue);
    gate.ru_bias_guard =
        treatment_abs_bias(DeckId::RUAggro) <=
        std::max(
            control_abs_bias(DeckId::RUAggro),
            kRuSignedBiasFloor);

    gate.no_new_material_bias = true;
    for (const auto& deck : report.by_deck) {
        const auto& control =
            deck.models[kControlModelIndex].signed_bias;
        const auto& treatment =
            deck.models[kTreatmentModelIndex].signed_bias;
        if (estimate_has_material_bias(treatment) &&
            !(estimate_has_material_bias(control) &&
              audit_common::same_strict_sign(
                  control.mean, treatment.mean))) {
            gate.no_new_material_bias = false;
        }
    }

    record_failure(
        gate.accounting_exact,
        "held-out accounting is not 200 games/80 perspectives per deck",
        gate.failures);
    record_failure(
        gate.inputs_finite, "held-out metrics are not finite",
        gate.failures);
    record_failure(
        gate.pooled_losses_improved,
        "pooled Brier/log-loss CR1 upper bounds are not both below zero",
        gate.failures);
    record_failure(
        gate.every_deck_loss_guard,
        "a deck loss point delta exceeds +0.005",
        gate.failures);
    record_failure(
        gate.green_bias_strictly_shrank,
        "Green absolute signed bias did not strictly shrink",
        gate.failures);
    record_failure(
        gate.blue_bias_strictly_shrank,
        "Blue absolute signed bias did not strictly shrink",
        gate.failures);
    record_failure(
        gate.ru_bias_guard,
        "RU absolute signed bias exceeds its control/floor guard",
        gate.failures);
    record_failure(
        gate.no_new_material_bias,
        "treatment introduced material signed bias",
        gate.failures);

    gate.passed =
        gate.accounting_exact &&
        gate.inputs_finite &&
        gate.pooled_losses_improved &&
        gate.every_deck_loss_guard &&
        gate.green_bias_strictly_shrank &&
        gate.blue_bias_strictly_shrank &&
        gate.ru_bias_guard &&
        gate.no_new_material_bias;
    return gate;
}

bool is_stable_best_set_probe(
    const probe_eval::ProbeLabel& label) {
    if (!valid_label(label)) {
        return false;
    }

    std::vector<std::string_view> outside;
    for (const auto& candidate : label.candidates) {
        if (!contains_key(
                label.reference_best_set, candidate.key)) {
            outside.push_back(candidate.key);
        }
    }
    if (outside.empty()) {
        return false;
    }

    return std::any_of(
        label.reference_best_set.begin(),
        label.reference_best_set.end(),
        [&label, &outside](const std::string& best) {
            return std::all_of(
                outside.begin(), outside.end(),
                [&label, &best](std::string_view other) {
                    return stable_positive_pair(
                        label, best, other);
                });
        });
}

ForceSpikeSelectionGateReport
evaluate_force_spike_selection_gate(
    const probe_runner::ForceSpikePolicyControlReport&
        report) {
    ForceSpikeSelectionGateReport gate;
    gate.identities_exact =
        report.live.stable_id == kLiveForceSpikeProbeId &&
        report.payable.stable_id ==
            kPayableForceSpikeProbeId;
    gate.live_uniquely_selects_force_spike =
        unique_selection(
            report.live, kForceSpikeCandidateKey);
    gate.payable_uniquely_selects_pass =
        unique_selection(report.payable, kPassCandidateKey);
    gate.hidden_repartition_passed =
        report.hidden_repartition_passed;

    record_failure(
        gate.identities_exact,
        "Force Spike control identities do not match",
        gate.failures);
    record_failure(
        gate.live_uniquely_selects_force_spike,
        "live Force Spike control is not uniquely Force Spike",
        gate.failures);
    record_failure(
        gate.payable_uniquely_selects_pass,
        "payable Force Spike control is not uniquely Pass",
        gate.failures);
    record_failure(
        gate.hidden_repartition_passed,
        "Force Spike controls failed hidden repartition",
        gate.failures);

    gate.passed =
        gate.identities_exact &&
        gate.live_uniquely_selects_force_spike &&
        gate.payable_uniquely_selects_pass &&
        gate.hidden_repartition_passed;
    return gate;
}

CommonStateCriticReport score_common_state_critics(
    std::span<const probe_eval::ProbeLabel> labels,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        control_decisions,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        treatment_decisions) {
    CommonStateCriticReport report;
    report.accounting_exact =
        labels.size() == control_decisions.size() &&
        labels.size() == treatment_decisions.size();
    report.predictions_valid = true;

    std::array<
        CommonCriticAccumulator, kCommonStateCriticCount>
        pooled;
    std::array<
        std::array<
            CommonCriticAccumulator,
            kCommonStateCriticCount>,
        kDeckCount>
        by_deck;

    for (std::size_t label_index = 0;
         label_index < labels.size(); ++label_index) {
        const auto& label = labels[label_index];
        if (!valid_label(label)) {
            report.accounting_exact = false;
            report.predictions_valid = false;
            continue;
        }
        for (std::size_t other = label_index + 1;
             other < labels.size(); ++other) {
            if (labels[other].stable_id ==
                label.stable_id) {
                report.accounting_exact = false;
            }
        }

        const auto* control = find_unique_decision(
            control_decisions, label.stable_id);
        const auto* treatment = find_unique_decision(
            treatment_decisions, label.stable_id);
        if (decision_count(
                control_decisions, label.stable_id) != 1 ||
            decision_count(
                treatment_decisions, label.stable_id) != 1 ||
            control == nullptr || treatment == nullptr ||
            control->root_deck != label.root_deck ||
            treatment->root_deck != label.root_deck) {
            report.accounting_exact = false;
            continue;
        }
        const auto index = deck_index(label.root_deck);
        if (!index.has_value()) {
            report.accounting_exact = false;
            continue;
        }

        const std::array<double, kCommonStateCriticCount>
            predictions = {
                control->critic_prediction,
                treatment->critic_prediction,
            };
        if (!is_probability(predictions[0]) ||
            !is_probability(predictions[1])) {
            report.predictions_valid = false;
            continue;
        }
        for (std::size_t model = 0;
             model < kCommonStateCriticCount; ++model) {
            add_common_critic_observation(
                pooled[model], predictions[model],
                label.reference_value);
            add_common_critic_observation(
                by_deck[*index][model],
                predictions[model],
                label.reference_value);
        }
    }

    for (std::size_t model = 0;
         model < kCommonStateCriticCount; ++model) {
        report.pooled.models[model] =
            finalize_common_critic(pooled[model]);
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        for (std::size_t model = 0;
             model < kCommonStateCriticCount; ++model) {
            report.by_deck[deck].models[model] =
                finalize_common_critic(
                    by_deck[deck][model]);
        }
    }

    report.accounting_exact =
        report.accounting_exact &&
        report.pooled.models[kCommonStateControlIndex]
                .probe_count ==
            labels.size() &&
        report.pooled.models[kCommonStateTreatmentIndex]
                .probe_count ==
            labels.size();
    report.metrics_finite =
        report.predictions_valid;
    for (const auto& metrics : report.pooled.models) {
        report.metrics_finite =
            report.metrics_finite &&
            finite_common_metrics(metrics);
    }
    for (const auto& scope : report.by_deck) {
        for (const auto& metrics : scope.models) {
            report.metrics_finite =
                report.metrics_finite &&
                finite_common_metrics(metrics);
        }
    }
    return report;
}

DeepReferenceGateReport evaluate_deep_reference_gate(
    const probe_eval::ProbeMetricSummary& control_metrics,
    const probe_eval::ProbeMetricSummary& treatment_metrics,
    std::span<const probe_eval::ProbeLabel> labels,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        control_decisions,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        treatment_decisions,
    const probe_runner::ForceSpikePolicyControlReport&
        treatment_force_spike,
    bool hidden_repartition_passed) {
    DeepReferenceGateReport gate;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        gate.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }

    std::array<std::size_t, kDeckCount> label_counts{};
    bool labels_valid = true;
    for (std::size_t index = 0; index < labels.size();
         ++index) {
        const auto& label = labels[index];
        labels_valid = labels_valid && valid_label(label);
        const auto index_for_deck =
            deck_index(label.root_deck);
        if (index_for_deck.has_value()) {
            ++label_counts[*index_for_deck];
        }
        for (std::size_t other = index + 1;
             other < labels.size(); ++other) {
            if (label.stable_id ==
                labels[other].stable_id) {
                labels_valid = false;
            }
        }
    }

    bool decisions_exact =
        control_decisions.size() == labels.size() &&
        treatment_decisions.size() == labels.size();
    for (const auto& label : labels) {
        const bool unique_control =
            decision_count(
                control_decisions, label.stable_id) == 1;
        const bool unique_treatment =
            decision_count(
                treatment_decisions, label.stable_id) == 1;
        decisions_exact =
            decisions_exact && unique_control &&
            unique_treatment;
        if (!unique_control || !unique_treatment) {
            continue;
        }
        decisions_exact =
            decisions_exact &&
            valid_decision(
                *find_unique_decision(
                    control_decisions, label.stable_id),
                label) &&
            valid_decision(
                *find_unique_decision(
                    treatment_decisions, label.stable_id),
                label);
    }

    bool metric_accounting =
        labels.size() == kExpectedProbeCount &&
        control_metrics.probe_count == kExpectedProbeCount &&
        treatment_metrics.probe_count ==
            kExpectedProbeCount;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        metric_accounting =
            metric_accounting &&
            label_counts[deck] ==
                kExpectedProbesPerDeck &&
            control_metrics.by_deck[deck].root_deck ==
                static_cast<DeckId>(deck) &&
            treatment_metrics.by_deck[deck].root_deck ==
                static_cast<DeckId>(deck) &&
            control_metrics.by_deck[deck].probe_count ==
                label_counts[deck] &&
            treatment_metrics.by_deck[deck].probe_count ==
                label_counts[deck];
    }
    const bool frozen_label_accounting =
        labels.size() == kExpectedProbeCount &&
        std::all_of(
            label_counts.begin(), label_counts.end(),
            [](std::size_t count) {
                return count ==
                       kExpectedProbesPerDeck;
            });
    gate.accounting_exact =
        frozen_label_accounting &&
        labels_valid && decisions_exact &&
        metric_accounting;

    gate.metrics_finite =
        finite_deep_metrics(control_metrics) &&
        finite_deep_metrics(treatment_metrics);
    gate.pooled_regret_no_worse =
        treatment_metrics.mean_regret <=
        control_metrics.mean_regret;
    gate.pooled_top_one_no_lower =
        treatment_metrics.top1_expected_agreement >=
        control_metrics.top1_expected_agreement;
    gate.every_deck_regret_guard = true;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        if (treatment_metrics.by_deck[deck].mean_regret >
            control_metrics.by_deck[deck].mean_regret +
                kMaximumDeckRegretIncrease) {
            gate.every_deck_regret_guard = false;
        }
    }

    for (const auto& label : labels) {
        const auto index = deck_index(label.root_deck);
        if (!index.has_value() ||
            !is_stable_best_set_probe(label)) {
            continue;
        }
        auto& deck = gate.by_deck[*index];
        ++deck.eligible_probes;
        const auto* control = find_unique_decision(
            control_decisions, label.stable_id);
        const auto* treatment = find_unique_decision(
            treatment_decisions, label.stable_id);
        if (control == nullptr || treatment == nullptr) {
            continue;
        }
        const bool control_agrees =
            selection_agrees(*control, label);
        const bool treatment_agrees =
            selection_agrees(*treatment, label);
        if (control_agrees) {
            ++deck.control_agreements;
        }
        if (treatment_agrees) {
            ++deck.treatment_agreements;
        }
        if (control_agrees && !treatment_agrees) {
            ++deck.lost_agreements;
        }
    }
    gate.stable_best_set_loss_guard = true;
    for (auto& deck : gate.by_deck) {
        deck.passed =
            deck.lost_agreements <=
            kMaximumStableBestSetLossesPerDeck;
        gate.stable_best_set_loss_guard =
            gate.stable_best_set_loss_guard &&
            deck.passed;
    }

    gate.required_blue_probes_exact = true;
    gate.required_blue_selections_passed = true;
    for (const auto stable_id :
         kRequiredStableBlueProbeIds) {
        const auto* label =
            find_unique_label(labels, stable_id);
        const bool probe_exact =
            label_count(labels, stable_id) == 1 &&
            label != nullptr &&
            label->root_deck == DeckId::Blue &&
            is_stable_best_set_probe(*label);
        gate.required_blue_probes_exact =
            gate.required_blue_probes_exact &&
            probe_exact;
        if (!probe_exact) {
            gate.required_blue_selections_passed = false;
            continue;
        }
        const auto* treatment = find_unique_decision(
            treatment_decisions, stable_id);
        gate.required_blue_selections_passed =
            gate.required_blue_selections_passed &&
            treatment != nullptr &&
            valid_decision(*treatment, *label) &&
            selection_agrees(*treatment, *label);
    }

    gate.hidden_repartition_passed =
        hidden_repartition_passed;
    gate.force_spike =
        evaluate_force_spike_selection_gate(
            treatment_force_spike);
    gate.common_state_critics =
        score_common_state_critics(
            labels, control_decisions,
            treatment_decisions);

    record_failure(
        gate.accounting_exact,
        "deep-reference labels/decisions/metrics do not align",
        gate.failures);
    record_failure(
        gate.metrics_finite,
        "deep-reference action metrics are not finite",
        gate.failures);
    record_failure(
        gate.pooled_regret_no_worse,
        "treatment pooled regret is worse than control",
        gate.failures);
    record_failure(
        gate.pooled_top_one_no_lower,
        "treatment pooled top-one agreement is below control",
        gate.failures);
    record_failure(
        gate.every_deck_regret_guard,
        "a deck regret increase exceeds +0.01",
        gate.failures);
    record_failure(
        gate.stable_best_set_loss_guard,
        "a deck lost more than one stable best-set agreement",
        gate.failures);
    record_failure(
        gate.required_blue_probes_exact,
        "required stable Blue probe identities are missing or unstable",
        gate.failures);
    record_failure(
        gate.required_blue_selections_passed,
        "treatment missed a required Blue reference-best set",
        gate.failures);
    record_failure(
        gate.hidden_repartition_passed,
        "deep-reference hidden repartition failed",
        gate.failures);
    record_failure(
        gate.force_spike.passed,
        "supplemental Force Spike selection gate failed",
        gate.failures);
    record_failure(
        gate.common_state_critics.accounting_exact &&
            gate.common_state_critics.predictions_valid &&
            gate.common_state_critics.metrics_finite,
        "common-state-label critic report is invalid",
        gate.failures);

    gate.passed =
        gate.accounting_exact &&
        gate.metrics_finite &&
        gate.pooled_regret_no_worse &&
        gate.pooled_top_one_no_lower &&
        gate.every_deck_regret_guard &&
        gate.stable_best_set_loss_guard &&
        gate.required_blue_probes_exact &&
        gate.required_blue_selections_passed &&
        gate.hidden_repartition_passed &&
        gate.force_spike.passed &&
        gate.common_state_critics.accounting_exact &&
        gate.common_state_critics.predictions_valid &&
        gate.common_state_critics.metrics_finite;
    return gate;
}

StageDecision evaluation_stage_decision(
    const StageOutcomes& outcomes) {
    StageDecision decision;
    decision.run_deep_reference =
        outcomes.heldout_passed;
    const bool deep_reference_passed =
        decision.run_deep_reference &&
        outcomes.deep_reference_passed.value_or(false);

    decision.run_treatment_vs_control =
        deep_reference_passed;
    const bool control_passed =
        decision.run_treatment_vs_control &&
        outcomes.treatment_vs_control_passed.value_or(false);

    decision.run_treatment_vs_parent = control_passed;
    const bool parent_passed =
        decision.run_treatment_vs_parent &&
        outcomes.treatment_vs_parent_passed.value_or(false);

    decision.run_treatment_vs_handcoded = parent_passed;
    const bool handcoded_passed =
        decision.run_treatment_vs_handcoded &&
        outcomes.treatment_vs_handcoded_passed.value_or(false);

    decision.run_fixed_seed_panel = handcoded_passed;
    const bool fixed_panel_passed =
        decision.run_fixed_seed_panel &&
        outcomes.fixed_seed_panel_passed.value_or(false);

    decision.run_mixed_field = fixed_panel_passed;
    decision.complete =
        decision.run_mixed_field &&
        outcomes.mixed_field_passed.has_value();
    decision.passed =
        decision.complete &&
        *outcomes.mixed_field_passed;
    return decision;
}

} // namespace old_school::joint_c17_eval
