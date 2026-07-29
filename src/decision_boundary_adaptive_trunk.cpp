#include "old_school/decision_boundary_adaptive_trunk.hpp"

#include "old_school/action_q_field_gate.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <span>
#include <stdexcept>
#include <utility>

namespace old_school::decision_boundary_adaptive_trunk {
namespace {

constexpr std::uint64_t kPositiveZeroBits = UINT64_C(0);

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK deck is invalid");
    }
    return result;
}

bool finite(std::span<const double> values) {
    return std::all_of(
        values.begin(), values.end(),
        [](double value) {
            return std::isfinite(value);
        });
}

bool finite(const Parameters& parameters) {
    return finite(parameters.gain) &&
           finite(parameters.bias) &&
           finite(parameters.output);
}

bool positive_zero(double value) {
    return std::bit_cast<std::uint64_t>(value) ==
           kPositiveZeroBits;
}

bool equal_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool vector_bit_identical(
    std::span<const double> first,
    std::span<const double> second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!equal_bits(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool matrix_bit_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t row = 0;
         row < first.size(); ++row) {
        if (!vector_bit_identical(
                first[row], second[row])) {
            return false;
        }
    }
    return true;
}

bool parameters_bit_identical(
    const Parameters& first,
    const Parameters& second) {
    return vector_bit_identical(
               first.gain, second.gain) &&
           vector_bit_identical(
               first.bias, second.bias) &&
           vector_bit_identical(
               first.output, second.output);
}

double& parameter_at(
    Parameters& parameters, std::size_t index) {
    if (index < kHiddenCount) {
        return parameters.gain[index];
    }
    index -= kHiddenCount;
    if (index < kHiddenCount) {
        return parameters.bias[index];
    }
    index -= kHiddenCount;
    if (index < kHiddenCount) {
        return parameters.output[index];
    }
    throw std::out_of_range(
        "DBC5-ADAPTIVE-TRUNK parameter index drifted");
}

double parameter_at(
    const Parameters& parameters, std::size_t index) {
    if (index < kHiddenCount) {
        return parameters.gain[index];
    }
    index -= kHiddenCount;
    if (index < kHiddenCount) {
        return parameters.bias[index];
    }
    index -= kHiddenCount;
    if (index < kHiddenCount) {
        return parameters.output[index];
    }
    throw std::out_of_range(
        "DBC5-ADAPTIVE-TRUNK parameter index drifted");
}

double l2_norm(const Parameters& parameters) {
    double squared = 0.0;
    for (std::size_t index = 0;
         index < kParameterCount; ++index) {
        const double value =
            parameter_at(parameters, index);
        squared += value * value;
    }
    return std::sqrt(squared);
}

double dot(
    std::span<const double> left,
    std::span<const double> right) {
    if (left.size() != right.size()) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK dot width drifted");
    }
    double result = 0.0;
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        result += left[index] * right[index];
    }
    if (!std::isfinite(result)) {
        throw std::runtime_error(
            "DBC5-ADAPTIVE-TRUNK dot is nonfinite");
    }
    return result;
}

double sigmoid(double value) {
    if (value >= 0.0) {
        const double exponential = std::exp(-value);
        return 1.0 / (1.0 + exponential);
    }
    const double exponential = std::exp(value);
    return exponential / (1.0 + exponential);
}

double binary_cross_entropy_from_logit(
    double target, double logit_value) {
    const double softplus =
        logit_value > 0.0
        ? logit_value +
              std::log1p(std::exp(-logit_value))
        : std::log1p(std::exp(logit_value));
    return softplus - target * logit_value;
}

bool parent_priority_positive_zero(
    const LearnedPriorityHeadParameters& parameters) {
    return std::all_of(
               parameters.hidden_bias.begin(),
               parameters.hidden_bias.end(),
               positive_zero) &&
           std::all_of(
               parameters.hidden_output.begin(),
               parameters.hidden_output.end(),
               positive_zero) &&
           std::all_of(
               parameters.direct.begin(),
               parameters.direct.end(),
               positive_zero) &&
           positive_zero(parameters.output_bias);
}

Hidden engine_preactivation(
    std::span<const double> option,
    const LearnedPriorityHeadParameters& parameters) {
    if (parameters.input_hidden.size() !=
            kHiddenCount ||
        parameters.hidden_bias.size() !=
            kHiddenCount) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK Priority dimensions drifted");
    }
    Hidden result{};
    for (std::size_t hidden = 0;
         hidden < kHiddenCount; ++hidden) {
        if (parameters.input_hidden[hidden].size() !=
            option.size()) {
            throw std::invalid_argument(
                "DBC5-ADAPTIVE-TRUNK Priority row width drifted");
        }
        double total = parameters.hidden_bias[hidden];
        for (std::size_t feature = 0;
             feature < option.size(); ++feature) {
            if (option[feature] != 0.0) {
                total +=
                    parameters
                        .input_hidden[hidden][feature] *
                    option[feature];
            }
        }
        result[hidden] = total;
    }
    return result;
}

Hidden activation(const Hidden& preactivation) {
    Hidden result{};
    for (std::size_t hidden = 0;
         hidden < kHiddenCount; ++hidden) {
        result[hidden] =
            std::tanh(preactivation[hidden]);
    }
    return result;
}

Hidden analytic_preactivation(
    const Hidden& parent,
    const Parameters& parameters) {
    Hidden result{};
    for (std::size_t hidden = 0;
         hidden < kHiddenCount; ++hidden) {
        result[hidden] =
            (1.0 + parameters.gain[hidden]) *
                parent[hidden] +
            parameters.bias[hidden];
    }
    return result;
}

std::vector<double> centered_residuals(
    std::span<const double> logits,
    double residual_weight = kResidualWeight) {
    if (logits.empty() || !finite(logits)) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK logits are invalid");
    }
    const double mean =
        std::accumulate(
            logits.begin(), logits.end(), 0.0) /
        static_cast<double>(logits.size());
    std::vector<double> result(logits.begin(), logits.end());
    for (double& value : result) {
        value =
            residual_weight *
            std::tanh(value - mean);
    }
    return result;
}

testing::ForwardProbe analytic_forward_impl(
    const Root& root,
    const Parameters& parameters) {
    testing::ForwardProbe result;
    result.preactivations.reserve(
        root.parent_preactivations.size());
    result.activations.reserve(
        root.parent_preactivations.size());
    result.logits.reserve(
        root.parent_preactivations.size());
    for (const Hidden& parent :
         root.parent_preactivations) {
        result.preactivations.push_back(
            analytic_preactivation(
                parent, parameters));
        result.activations.push_back(
            activation(result.preactivations.back()));
        result.logits.push_back(
            dot(result.activations.back(),
                parameters.output));
    }
    result.residuals =
        centered_residuals(result.logits);
    return result;
}

testing::ForwardProbe model_forward_impl(
    const Root& root,
    std::shared_ptr<const LearnedModel> model) {
    if (!model) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK model is null");
    }
    const auto parameters =
        learned_priority_head_parameters(model);
    testing::ForwardProbe result;
    result.preactivations.reserve(
        root.paired.options.size());
    result.activations.reserve(
        root.paired.options.size());
    for (const auto& option : root.paired.options) {
        result.preactivations.push_back(
            engine_preactivation(option, parameters));
        result.activations.push_back(
            activation(result.preactivations.back()));
    }
    result.logits =
        learned_policy_head_logits(
            root.paired.options,
            LearnedPolicyDecisionKind::Priority,
            model);
    result.residuals =
        centered_residuals(result.logits);
    return result;
}

pair::Dataset paired_dataset(const Dataset& dataset) {
    std::vector<pair::Root> roots;
    roots.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        roots.push_back(root.paired);
    }
    return pair::testing::make_dataset(
        std::move(roots));
}

std::vector<std::vector<double>> analytic_residuals(
    const Dataset& dataset,
    const Parameters& parameters) {
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        result.push_back(
            analytic_forward_impl(
                root, parameters).residuals);
    }
    return result;
}

std::vector<std::vector<double>> actual_residuals(
    const Dataset& dataset,
    std::shared_ptr<const LearnedModel> model) {
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        result.push_back(
            model_forward_impl(root, model).residuals);
    }
    return result;
}

double teacher_action_score(
    const pair::direct::RankAction& action) {
    double total = 0.0;
    for (const pair::direct::RankCell& cell :
         action.worlds) {
        total += cell.teacher_target;
    }
    return total /
           static_cast<double>(action.worlds.size());
}

double parent_cell_score(
    const pair::direct::RankCell& cell) {
    if (cell.terminal_before_boundary) {
        return cell.teacher_target;
    }
    return
        (cell.parent_leaf_values[0] +
         cell.parent_leaf_values[1]) /
        static_cast<double>(pair::direct::kLeafCount);
}

double parent_action_score(
    const pair::direct::RankAction& action) {
    double total = 0.0;
    for (const pair::direct::RankCell& cell :
         action.worlds) {
        total += parent_cell_score(cell);
    }
    return total /
           static_cast<double>(action.worlds.size());
}

struct ObjectiveResult {
    double value = 0.0;
    Parameters gradient;
};

ObjectiveResult objective_and_gradient(
    const Dataset& dataset,
    const Parameters& parameters,
    const OptimizerConfig& config) {
    ObjectiveResult result;
    for (const Root& root : dataset.roots) {
        const std::size_t actions =
            root.paired.ranking.actions.size();
        std::vector<double> teacher(actions);
        std::vector<double> base(actions);
        std::vector<double> logits(actions);
        std::vector<double> tanh_centered(actions);
        std::vector<std::array<double, kParameterCount>>
            logit_derivative(actions);
        std::array<double, kParameterCount>
            mean_logit_derivative{};
        for (std::size_t action_index = 0;
             action_index < actions; ++action_index) {
            teacher[action_index] =
                teacher_action_score(
                    root.paired.ranking
                        .actions[action_index]);
            base[action_index] =
                parent_action_score(
                    root.paired.ranking
                        .actions[action_index]);
            const Hidden preactivation =
                analytic_preactivation(
                    root.parent_preactivations[
                        action_index],
                    parameters);
            const Hidden hidden =
                activation(preactivation);
            logits[action_index] =
                dot(hidden, parameters.output);
            for (std::size_t unit = 0;
                 unit < kHiddenCount; ++unit) {
                const double tanh_derivative =
                    1.0 -
                    hidden[unit] * hidden[unit];
                logit_derivative[action_index][unit] =
                    parameters.output[unit] *
                    tanh_derivative *
                    root.parent_preactivations[
                        action_index][unit];
                logit_derivative[action_index]
                                [kHiddenCount + unit] =
                    parameters.output[unit] *
                    tanh_derivative;
                logit_derivative[action_index]
                                [2 * kHiddenCount + unit] =
                    hidden[unit];
            }
            for (std::size_t coordinate = 0;
                 coordinate < kParameterCount;
                 ++coordinate) {
                mean_logit_derivative[coordinate] +=
                    logit_derivative[action_index]
                                    [coordinate] /
                    static_cast<double>(actions);
            }
        }
        const double mean_logit =
            std::accumulate(
                logits.begin(), logits.end(), 0.0) /
            static_cast<double>(actions);
        std::vector<double> score(actions);
        std::vector<std::array<double, kParameterCount>>
            score_derivative(actions);
        for (std::size_t action_index = 0;
             action_index < actions; ++action_index) {
            tanh_centered[action_index] =
                std::tanh(
                    logits[action_index] - mean_logit);
            score[action_index] =
                base[action_index] +
                config.residual_weight *
                tanh_centered[action_index];
            for (std::size_t coordinate = 0;
                 coordinate < kParameterCount;
                 ++coordinate) {
                score_derivative[action_index]
                                [coordinate] =
                    config.residual_weight *
                    (1.0 -
                     tanh_centered[action_index] *
                         tanh_centered[action_index]) *
                    (logit_derivative[action_index]
                                     [coordinate] -
                     mean_logit_derivative[coordinate]);
            }
        }
        double total_cost = 0.0;
        for (std::size_t first = 0;
             first < actions; ++first) {
            for (std::size_t second = first + 1;
                 second < actions; ++second) {
                total_cost +=
                    std::abs(
                        teacher[first] -
                        teacher[second]);
            }
        }
        if (total_cost == 0.0) {
            continue;
        }
        const double outer_weight =
            1.0 /
            static_cast<double>(
                kDeckCount *
                dataset.roots_by_deck[
                    deck_index(
                        root.paired.ranking.deck)]);
        for (std::size_t first = 0;
             first < actions; ++first) {
            for (std::size_t second = first + 1;
                 second < actions; ++second) {
                const double teacher_difference =
                    teacher[first] - teacher[second];
                if (teacher_difference == 0.0) {
                    continue;
                }
                const double pair_weight =
                    std::abs(teacher_difference) /
                    total_cost;
                const double target =
                    sigmoid(
                        teacher_difference /
                        config.pair_temperature);
                const double candidate_logit =
                    (score[first] - score[second]) /
                    config.pair_temperature;
                const double derivative =
                    outer_weight * pair_weight *
                    (sigmoid(candidate_logit) - target) /
                    config.pair_temperature;
                result.value +=
                    outer_weight * pair_weight *
                    binary_cross_entropy_from_logit(
                        target, candidate_logit);
                for (std::size_t coordinate = 0;
                     coordinate < kParameterCount;
                     ++coordinate) {
                    parameter_at(
                        result.gradient, coordinate) +=
                        derivative *
                        (score_derivative[first]
                                         [coordinate] -
                         score_derivative[second]
                                         [coordinate]);
                }
            }
        }
    }
    for (std::size_t coordinate = 0;
         coordinate < kParameterCount; ++coordinate) {
        const double value =
            parameter_at(parameters, coordinate);
        result.value +=
            0.5 * config.l2_tether *
            value * value;
        parameter_at(result.gradient, coordinate) +=
            config.l2_tether * value;
    }
    if (!std::isfinite(result.value) ||
        !finite(result.gradient)) {
        throw std::runtime_error(
            "DBC5-ADAPTIVE-TRUNK objective is nonfinite");
    }
    return result;
}

void validate_optimizer_config(
    const OptimizerConfig& config) {
    if (config.fit_tag != kFitTag ||
        config.steps != kAdamSteps ||
        config.learning_rate !=
            kAdamLearningRate ||
        config.beta_one != kAdamBetaOne ||
        config.beta_two != kAdamBetaTwo ||
        config.epsilon != kAdamEpsilon ||
        config.residual_weight != kResidualWeight ||
        config.pair_temperature !=
            kPairTemperature ||
        config.l2_tether != kL2Tether ||
        config.global_gradient_norm_clip !=
            kGlobalGradientNormClip) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK requires fixed optimizer recipe");
    }
}

Dataset make_dataset_impl(std::vector<Root> roots) {
    Dataset result{
        .roots = std::move(roots),
    };
    for (const Root& root : result.roots) {
        ++result.roots_by_deck[
            deck_index(root.paired.ranking.deck)];
    }
    return result;
}

Dataset project_split(
    const pair::Dataset& paired,
    const LearnedPriorityHeadParameters& parameters) {
    std::vector<Root> roots;
    roots.reserve(paired.roots.size());
    for (const pair::Root& paired_root :
         paired.roots) {
        Root root{
            .paired = paired_root,
        };
        root.parent_preactivations.reserve(
            root.paired.options.size());
        for (const auto& option :
             root.paired.options) {
            root.parent_preactivations.push_back(
                engine_preactivation(
                    option, parameters));
        }
        roots.push_back(std::move(root));
    }
    return make_dataset_impl(std::move(roots));
}

Dataset select_fold(
    const Dataset& train,
    std::size_t held_out_fold,
    bool holdout) {
    if (held_out_fold >= kFoldCount) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK fold index is invalid");
    }
    std::vector<Root> roots;
    for (const Root& root : train.roots) {
        const bool in_fold =
            root.paired.schedule_index %
                kFoldCount ==
            held_out_fold;
        if (in_fold == holdout) {
            roots.push_back(root);
        }
    }
    return make_dataset_impl(std::move(roots));
}

LearnedPriorityHeadParameters transformed_head(
    const LearnedPriorityHeadParameters& parent,
    const Parameters& parameters) {
    if (!finite(parameters) ||
        parent.input_hidden.size() !=
            kHiddenCount ||
        parent.hidden_bias.size() !=
            kHiddenCount ||
        parent.hidden_output.size() !=
            kHiddenCount ||
        parent.direct.size() !=
            kPolicyFeatureCount) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK transform dimensions drifted");
    }
    LearnedPriorityHeadParameters result = parent;
    for (std::size_t hidden = 0;
         hidden < kHiddenCount; ++hidden) {
        if (result.input_hidden[hidden].size() !=
            kPolicyFeatureCount) {
            throw std::invalid_argument(
                "DBC5-ADAPTIVE-TRUNK transform row drifted");
        }
        for (std::size_t feature = 0;
             feature < kPolicyFeatureCount; ++feature) {
            result.input_hidden[hidden][feature] =
                (1.0 + parameters.gain[hidden]) *
                parent.input_hidden[hidden][feature];
        }
        result.hidden_bias[hidden] =
            parameters.bias[hidden];
        result.hidden_output[hidden] =
            parameters.output[hidden];
    }
    return result;
}

double maximum_difference(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK score roots drifted");
    }
    double maximum = 0.0;
    for (std::size_t root = 0;
         root < first.size(); ++root) {
        if (first[root].size() != second[root].size()) {
            throw std::invalid_argument(
                "DBC5-ADAPTIVE-TRUNK score actions drifted");
        }
        for (std::size_t action_index = 0;
             action_index < first[root].size();
             ++action_index) {
            maximum =
                std::max(
                    maximum,
                    std::abs(
                        first[root][action_index] -
                        second[root][action_index]));
        }
    }
    return maximum;
}

bool nested_scores_bit_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t root = 0;
         root < first.size(); ++root) {
        if (!vector_bit_identical(
                first[root], second[root])) {
            return false;
        }
    }
    return true;
}

bool successor_predictions_equal(
    const Dataset& dataset,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    for (const Root& root : dataset.roots) {
        for (const pair::direct::RankAction& action :
             root.paired.ranking.actions) {
            for (const pair::direct::RankCell& cell :
                 action.worlds) {
                if (cell.terminal_before_boundary) {
                    continue;
                }
                const double first =
                    learned_critic_observation_value(
                        cell.observation, parent);
                const double second =
                    learned_critic_observation_value(
                        cell.observation, candidate);
                if (!equal_bits(first, second)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool successor_metrics_equal(
    const pair::direct::Metrics& first,
    const pair::direct::Metrics& second) {
    if (first.roots != second.roots ||
        first.eligible_cells !=
            second.eligible_cells ||
        !equal_bits(
            first.equal_deck_successor_bce,
            second.equal_deck_successor_bce) ||
        !equal_bits(
            first.equal_deck_successor_brier,
            second.equal_deck_successor_brier) ||
        !equal_bits(
            first.equal_deck_successor_bias,
            second.equal_deck_successor_bias) ||
        !equal_bits(
            first.equal_deck_successor_ece,
            second.equal_deck_successor_ece)) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& left = first.decks[deck];
        const auto& right = second.decks[deck];
        if (left.roots != right.roots ||
            left.eligible_cells !=
                right.eligible_cells ||
            !equal_bits(
                left.successor_bce,
                right.successor_bce) ||
            !equal_bits(
                left.successor_brier,
                right.successor_brier) ||
            !equal_bits(
                left.successor_bias,
                right.successor_bias) ||
            !equal_bits(
                left.successor_ece,
                right.successor_ece)) {
            return false;
        }
    }
    return true;
}

void compare_forward(
    const testing::ForwardProbe& analytic,
    const testing::ForwardProbe& actual,
    double& maximum_preactivation_difference,
    double& maximum_activation_difference,
    double& maximum_logit_difference,
    double& maximum_residual_difference) {
    if (analytic.preactivations.size() !=
            actual.preactivations.size() ||
        analytic.activations.size() !=
            actual.activations.size() ||
        analytic.logits.size() !=
            actual.logits.size() ||
        analytic.residuals.size() !=
            actual.residuals.size()) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK forward shape drifted");
    }
    for (std::size_t action_index = 0;
         action_index < analytic.logits.size();
         ++action_index) {
        for (std::size_t hidden = 0;
             hidden < kHiddenCount; ++hidden) {
            maximum_preactivation_difference =
                std::max(
                    maximum_preactivation_difference,
                    std::abs(
                        analytic
                            .preactivations[action_index]
                                           [hidden] -
                        actual
                            .preactivations[action_index]
                                           [hidden]));
            maximum_activation_difference =
                std::max(
                    maximum_activation_difference,
                    std::abs(
                        analytic.activations[action_index]
                                            [hidden] -
                        actual.activations[action_index]
                                          [hidden]));
        }
        maximum_logit_difference =
            std::max(
                maximum_logit_difference,
                std::abs(
                    analytic.logits[action_index] -
                    actual.logits[action_index]));
        maximum_residual_difference =
            std::max(
                maximum_residual_difference,
                std::abs(
                    analytic.residuals[action_index] -
                    actual.residuals[action_index]));
    }
}

testing::HiddenRepartitionView hidden_repartition_view(
    const action_q_field_gate::AncestralFieldRoot& root,
    const std::shared_ptr<const LearnedModel>& model) {
    testing::HiddenRepartitionView result{
        .owner_observation =
            learned_observation(root.state, root.actor),
        .actions =
            legal_priority_actions(
                root.state, root.actor,
                root.context.sorcery_actions),
    };
    result.option_rows.reserve(result.actions.size());
    for (const PriorityAction& action : result.actions) {
        result.option_rows.push_back(
            learned_priority_policy_features(
                root.state, root.actor, action,
                root.context.sorcery_actions,
                root.context.phase,
                root.context.consecutive_passes));
    }
    const LearnedValuePriorityResidualDiagnostic diagnostic =
        diagnose_learned_value_priority_residual(
            root.state, root.actor,
            root.context.sorcery_actions,
            root.context.phase,
            root.context.consecutive_passes,
            result.actions, model, kResidualWeight);
    result.logits = diagnostic.policy_logits;
    result.centered_logits =
        diagnostic.centered_policy_logits;
    result.centered_residuals =
        diagnostic.residuals;
    return result;
}

testing::HiddenRepartitionWitness
production_hidden_repartition_witness(
    const std::shared_ptr<const LearnedModel>& model) {
    const auto original =
        action_q_field_gate::
            make_ancestral_field_root();
    const bool original_eligible =
        action_q_field_gate::
            has_required_action_identities(original) &&
        original.context.valid &&
        original.context.decision_player ==
            original.actor &&
        original.legal_actions.size() >= 2 &&
        legal_priority_actions(
            original.state, original.actor,
            original.context.sorcery_actions) ==
            original.legal_actions;
    const auto repartitioned =
        action_q_field_gate::
            hidden_repartition_clone(original);
    const bool eligible =
        original_eligible &&
        action_q_field_gate::
            has_required_action_identities(
                repartitioned) &&
        repartitioned.actor == original.actor &&
        repartitioned.context == original.context &&
        legal_priority_actions(
            repartitioned.state,
            repartitioned.actor,
            repartitioned.context.sorcery_actions) ==
            repartitioned.legal_actions;
    const std::size_t opponent = 1U - original.actor;
    const PlayerState& first_hidden =
        original.state.players[opponent];
    const PlayerState& second_hidden =
        repartitioned.state.players[opponent];
    const bool nonvacuous =
        first_hidden.hand.size() ==
            second_hidden.hand.size() &&
        first_hidden.library.size() ==
            second_hidden.library.size() &&
        (first_hidden.hand != second_hidden.hand ||
         first_hidden.library !=
             second_hidden.library);
    return testing::compare_hidden_repartition_views(
        eligible, nonvacuous,
        hidden_repartition_view(original, model),
        hidden_repartition_view(
            repartitioned, model));
}

} // namespace

bool testing::HiddenRepartitionWitness::passed() const {
    return live_probe_eligible &&
           nonvacuous &&
           owner_observation_bit_identical &&
           actions_bit_identical &&
           options_bit_identical &&
           logits_bit_identical &&
           centered_logits_bit_identical &&
           residuals_bit_identical;
}

bool ModelIsolationReport::safe_for_evaluation() const {
    if (!candidate ||
        !parent_positive_zero ||
        !parent_immutable ||
        !repeated_application_bit_identical ||
        !exact_transform ||
        changed_coordinates > kParameterCount) {
        return false;
    }
    if (changed_coordinates == 0) {
        return parent_fingerprint_before ==
                   candidate_fingerprint &&
               parent_components ==
                   candidate_components;
    }
    return only_priority_adaptive_trunk_changed;
}

bool ModelIsolationReport::passed() const {
    return safe_for_evaluation() &&
           changed_coordinates > 0 &&
           changed_coordinates <= kParameterCount;
}

Corpus project_corpus(
    const dbc::Corpus& source,
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK parent is null");
    }
    dbc::validate_corpus(source);
    const auto parameters =
        learned_priority_head_parameters(parent);
    if (!parent_priority_positive_zero(parameters)) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK parent Priority head is not positive-zero");
    }
    const pair::Corpus paired =
        pair::project_corpus(source, parent);
    Corpus result{
        .train =
            project_split(paired.train, parameters),
        .dev =
            project_split(paired.dev, parameters),
        .source_digest = source.digest,
        .parent_components =
            source.parent_components,
    };
    validate_corpus(result, parent);
    return result;
}

void validate_dataset(const Dataset& dataset) {
    if (dataset.roots.empty()) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK dataset is empty");
    }
    std::array<std::size_t, kDeckCount> roots_by_deck{};
    std::vector<pair::Root> paired_roots;
    paired_roots.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        ++roots_by_deck[
            deck_index(root.paired.ranking.deck)];
        if (root.parent_preactivations.size() !=
                root.paired.options.size() ||
            root.paired.hidden.size() !=
                root.parent_preactivations.size()) {
            throw std::invalid_argument(
                "DBC5-ADAPTIVE-TRUNK preactivation shape drifted");
        }
        for (std::size_t action_index = 0;
             action_index <
                 root.parent_preactivations.size();
             ++action_index) {
            if (!finite(
                    root.parent_preactivations[
                        action_index])) {
                throw std::invalid_argument(
                    "DBC5-ADAPTIVE-TRUNK preactivation is nonfinite");
            }
            const Hidden expected =
                activation(
                    root.parent_preactivations[
                        action_index]);
            if (!vector_bit_identical(
                    expected,
                    root.paired.hidden[action_index])) {
                throw std::invalid_argument(
                    "DBC5-ADAPTIVE-TRUNK retained parent activation drifted");
            }
        }
        paired_roots.push_back(root.paired);
    }
    if (roots_by_deck != dataset.roots_by_deck ||
        std::any_of(
            roots_by_deck.begin(),
            roots_by_deck.end(),
            [](std::size_t count) {
                return count == 0;
            })) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK deck census drifted");
    }
    pair::validate_dataset(
        pair::testing::make_dataset(
            std::move(paired_roots)));
}

void validate_corpus(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    validate_dataset(corpus.train);
    validate_dataset(corpus.dev);
    if (!parent ||
        corpus.source_digest !=
            dbc::kFrozenCorpusDigest ||
        corpus.parent_components !=
            learned_model_component_fingerprints(parent)) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK corpus identity drifted");
    }
}

pair::PairMetrics pair_census(
    const Dataset& dataset) {
    validate_dataset(dataset);
    return pair::pair_census(
        paired_dataset(dataset));
}

bool frozen_pair_census_exact(const Corpus& corpus) {
    return pair::frozen_pair_census_exact({
        .train = paired_dataset(corpus.train),
        .dev = paired_dataset(corpus.dev),
        .source_digest = corpus.source_digest,
        .parent_components = corpus.parent_components,
    });
}

Dataset fold_training_dataset(
    const Dataset& train,
    std::size_t held_out_fold) {
    return select_fold(
        train, held_out_fold, false);
}

Dataset fold_holdout_dataset(
    const Dataset& train,
    std::size_t held_out_fold) {
    return select_fold(
        train, held_out_fold, true);
}

Metrics evaluate(
    const Dataset& dataset,
    const Parameters& parameters) {
    validate_dataset(dataset);
    if (!finite(parameters)) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK parameters are nonfinite");
    }
    return pair::evaluate_residuals(
        paired_dataset(dataset),
        analytic_residuals(dataset, parameters));
}

OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config) {
    validate_dataset(train);
    validate_optimizer_config(config);
    OptimizerReport report{
        .config = config,
        .before = evaluate(train, Parameters{}),
    };
    const ObjectiveResult initial =
        objective_and_gradient(
            train, report.parameters, config);
    report.initial_objective = initial.value;
    Parameters first_moment;
    Parameters second_moment;
    double beta_one_power = 1.0;
    double beta_two_power = 1.0;
    for (std::size_t step = 0;
         step < config.steps; ++step) {
        ObjectiveResult current =
            objective_and_gradient(
                train, report.parameters, config);
        const double preclip =
            l2_norm(current.gradient);
        report.maximum_preclip_gradient_l2_norm =
            std::max(
                report.maximum_preclip_gradient_l2_norm,
                preclip);
        if (preclip >
            config.global_gradient_norm_clip) {
            const double scale =
                config.global_gradient_norm_clip /
                preclip;
            for (std::size_t coordinate = 0;
                 coordinate < kParameterCount;
                 ++coordinate) {
                parameter_at(
                    current.gradient, coordinate) *=
                    scale;
            }
            ++report.clipped_steps;
        }
        beta_one_power *= config.beta_one;
        beta_two_power *= config.beta_two;
        for (std::size_t coordinate = 0;
             coordinate < kParameterCount;
             ++coordinate) {
            double& first =
                parameter_at(
                    first_moment, coordinate);
            double& second =
                parameter_at(
                    second_moment, coordinate);
            const double gradient =
                parameter_at(
                    current.gradient, coordinate);
            first =
                config.beta_one * first +
                (1.0 - config.beta_one) * gradient;
            second =
                config.beta_two * second +
                (1.0 - config.beta_two) *
                    gradient * gradient;
            const double corrected_first =
                first / (1.0 - beta_one_power);
            const double corrected_second =
                second / (1.0 - beta_two_power);
            parameter_at(
                report.parameters, coordinate) -=
                config.learning_rate *
                corrected_first /
                (std::sqrt(corrected_second) +
                 config.epsilon);
        }
        ++report.completed_steps;
    }
    const ObjectiveResult final =
        objective_and_gradient(
            train, report.parameters, config);
    report.final_objective = final.value;
    report.parameter_l2_norm =
        l2_norm(report.parameters);
    report.final_gradient_l2_norm =
        l2_norm(final.gradient);
    report.after =
        evaluate(train, report.parameters);
    return report;
}

bool optimizer_bit_identical(
    const OptimizerReport& first,
    const OptimizerReport& second) {
    return first.config == second.config &&
           parameters_bit_identical(
               first.parameters,
               second.parameters) &&
           first.completed_steps ==
               second.completed_steps &&
           equal_bits(
               first.initial_objective,
               second.initial_objective) &&
           equal_bits(
               first.final_objective,
               second.final_objective) &&
           equal_bits(
               first.parameter_l2_norm,
               second.parameter_l2_norm) &&
           equal_bits(
               first.final_gradient_l2_norm,
               second.final_gradient_l2_norm) &&
           equal_bits(
               first.maximum_preclip_gradient_l2_norm,
               second.maximum_preclip_gradient_l2_norm) &&
           first.clipped_steps ==
               second.clipped_steps &&
           first.before == second.before &&
           first.after == second.after;
}

ModelIsolationReport apply_parameters(
    std::shared_ptr<const LearnedModel> parent,
    const Parameters& parameters) {
    if (!parent || !finite(parameters)) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK application input is invalid");
    }
    for (std::size_t coordinate = 0;
         coordinate < kParameterCount; ++coordinate) {
        const double value =
            parameter_at(parameters, coordinate);
        if (value == 0.0 && !positive_zero(value)) {
            throw std::invalid_argument(
                "DBC5-ADAPTIVE-TRUNK parameter contains negative zero");
        }
    }
    const auto parent_parameters =
        learned_priority_head_parameters(parent);
    ModelIsolationReport report{
        .parent_fingerprint_before =
            learned_model_fingerprint(parent),
        .parent_components =
            learned_model_component_fingerprints(parent),
        .parent_positive_zero =
            parent_priority_positive_zero(
                parent_parameters),
    };
    if (!report.parent_positive_zero) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK parent is not positive-zero");
    }
    const auto candidate_parameters =
        transformed_head(
            parent_parameters, parameters);
    report.candidate =
        with_learned_priority_head_parameters(
            parent, candidate_parameters);
    const auto repeated =
        with_learned_priority_head_parameters(
            parent, candidate_parameters);
    report.parent_fingerprint_after =
        learned_model_fingerprint(parent);
    report.candidate_fingerprint =
        learned_model_fingerprint(report.candidate);
    report.repeated_candidate_fingerprint =
        learned_model_fingerprint(repeated);
    report.candidate_components =
        learned_model_component_fingerprints(
            report.candidate);
    report.parent_immutable =
        report.parent_fingerprint_before ==
        report.parent_fingerprint_after;
    report.repeated_application_bit_identical =
        report.candidate_fingerprint ==
        report.repeated_candidate_fingerprint;
    const auto actual =
        learned_priority_head_parameters(
            report.candidate);
    report.exact_transform =
        matrix_bit_identical(
            actual.input_hidden,
            candidate_parameters.input_hidden) &&
        vector_bit_identical(
            actual.hidden_bias,
            candidate_parameters.hidden_bias) &&
        vector_bit_identical(
            actual.hidden_output,
            candidate_parameters.hidden_output) &&
        vector_bit_identical(
            actual.direct,
            parent_parameters.direct) &&
        equal_bits(
            actual.output_bias,
            parent_parameters.output_bias);
    for (std::size_t coordinate = 0;
         coordinate < kParameterCount; ++coordinate) {
        report.changed_coordinates +=
            std::bit_cast<std::uint64_t>(
                parameter_at(parameters, coordinate)) !=
            kPositiveZeroBits
            ? 1U
            : 0U;
    }
    report.only_priority_adaptive_trunk_changed =
        report.exact_transform &&
        report.parent_components.critic ==
            report.candidate_components.critic &&
        report.parent_components.attack ==
            report.candidate_components.attack &&
        report.parent_components.block ==
            report.candidate_components.block &&
        report.parent_components.damage_order ==
            report.candidate_components.damage_order &&
        report.parent_components.priority !=
            report.candidate_components.priority;
    return report;
}

ExactEvaluationReport evaluate_exact(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    const ModelIsolationReport& isolation,
    const Parameters& parameters) {
    validate_corpus(corpus, parent);
    if (!isolation.candidate ||
        !isolation.safe_for_evaluation()) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK exact evaluation needs a structurally safe candidate");
    }
    ExactEvaluationReport report;
    const ModelIsolationReport zero_isolation =
        apply_parameters(parent, Parameters{});
    report.zero_parameters_equivalent =
        zero_isolation.safe_for_evaluation() &&
        zero_isolation.candidate_fingerprint ==
            learned_model_fingerprint(parent);
    report.legal_action_permutation_equivariant = true;
    const auto score_split =
        [&](const Dataset& dataset,
            Metrics& before,
            Metrics& after) {
            const auto parent_actual =
                actual_residuals(dataset, parent);
            const auto candidate_actual =
                actual_residuals(
                    dataset, isolation.candidate);
            before =
                pair::evaluate_residuals(
                    paired_dataset(dataset),
                    parent_actual);
            after =
                pair::evaluate_residuals(
                    paired_dataset(dataset),
                    candidate_actual);
            const auto zero_analytic =
                analytic_residuals(
                    dataset, Parameters{});
            report.maximum_residual_difference =
                std::max(
                    report.maximum_residual_difference,
                    maximum_difference(
                        zero_analytic, parent_actual));
            report.zero_parameters_equivalent =
                report.zero_parameters_equivalent &&
                nested_scores_bit_identical(
                    zero_analytic, parent_actual);
            for (std::size_t root_index = 0;
                 root_index < dataset.roots.size();
                 ++root_index) {
                const Root& root =
                    dataset.roots[root_index];
                const auto analytic =
                    analytic_forward_impl(
                        root, parameters);
                const auto actual =
                    model_forward_impl(
                        root, isolation.candidate);
                compare_forward(
                    analytic, actual,
                    report
                        .maximum_preactivation_difference,
                    report
                        .maximum_activation_difference,
                    report.maximum_logit_difference,
                    report.maximum_residual_difference);
                report
                    .legal_action_permutation_equivariant =
                    report
                        .legal_action_permutation_equivariant &&
                    testing::
                        legal_action_permutation_equivariant(
                            root, parameters,
                            isolation.candidate);
                for (const double residual :
                     parent_actual[root_index]) {
                    report.zero_parameters_equivalent =
                        report.zero_parameters_equivalent &&
                        positive_zero(residual);
                }
            }
        };
    score_split(
        corpus.train,
        report.parent_train,
        report.candidate_train);
    score_split(
        corpus.dev,
        report.parent_dev,
        report.candidate_dev);
    report.successor_predictions_bit_identical =
        successor_predictions_equal(
            corpus.train, parent,
            isolation.candidate) &&
        successor_predictions_equal(
            corpus.dev, parent,
            isolation.candidate);
    report.successor_metrics_bit_identical =
        successor_metrics_equal(
            report.parent_train.ranking,
            report.candidate_train.ranking) &&
        successor_metrics_equal(
            report.parent_dev.ranking,
            report.candidate_dev.ranking);
    const testing::HiddenRepartitionWitness hidden =
        production_hidden_repartition_witness(
            isolation.candidate);
    report.hidden_repartition_live_probe_eligible =
        hidden.live_probe_eligible;
    report.hidden_repartition_nonvacuous =
        hidden.nonvacuous;
    report
        .hidden_repartition_owner_observation_bit_identical =
        hidden.owner_observation_bit_identical;
    report.hidden_repartition_actions_bit_identical =
        hidden.actions_bit_identical;
    report.hidden_repartition_options_bit_identical =
        hidden.options_bit_identical;
    report.hidden_repartition_logits_bit_identical =
        hidden.logits_bit_identical;
    report
        .hidden_repartition_centered_logits_bit_identical =
        hidden.centered_logits_bit_identical;
    report.hidden_repartition_residuals_bit_identical =
        hidden.residuals_bit_identical;
    return report;
}

FoldReport evaluate_grouped_oof(
    const Dataset& train,
    std::shared_ptr<const LearnedModel> parent) {
    validate_dataset(train);
    if (!parent) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK OOF parent is null");
    }
    FoldReport report;
    const auto parent_residuals =
        actual_residuals(train, parent);
    std::vector<std::vector<double>> candidate_residuals(
        train.roots.size());
    std::vector<std::size_t> prediction_counts(
        train.roots.size(), 0);
    report.exact_balance = true;
    report.repeated_fits_bit_identical = true;
    report.repeated_scores_bit_identical = true;
    report.model_isolation_passed = true;
    std::array<
        std::map<std::size_t, std::array<std::size_t, 2>>,
        kFoldCount>
        source_games;
    for (const Root& root : train.roots) {
        ++source_games[
              root.paired.schedule_index % kFoldCount]
              [root.paired.schedule_index]
              [root.paired.actor];
    }
    for (const auto& fold_games : source_games) {
        report.exact_balance =
            report.exact_balance &&
            fold_games.size() == 10;
        for (const auto& [schedule, actors] :
             fold_games) {
            static_cast<void>(schedule);
            report.exact_balance =
                report.exact_balance &&
                actors[0] == 1 &&
                actors[1] == 1;
        }
    }
    for (const auto& root_scores :
         parent_residuals) {
        for (const double residual : root_scores) {
            report.exact_balance =
                report.exact_balance &&
                positive_zero(residual);
        }
    }
    for (std::size_t fold = 0;
         fold < kFoldCount; ++fold) {
        const Dataset fitting =
            fold_training_dataset(train, fold);
        const Dataset held =
            fold_holdout_dataset(train, fold);
        report.exact_balance =
            report.exact_balance &&
            fitting.roots.size() == 60 &&
            held.roots.size() == 20;
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            report.exact_balance =
                report.exact_balance &&
                fitting.roots_by_deck[deck] == 12 &&
                held.roots_by_deck[deck] == 4;
        }
        report.fits[fold] = optimize(fitting);
        report.repeated_fits[fold] =
            optimize(fitting);
        report.repeated_fits_bit_identical =
            report.repeated_fits_bit_identical &&
            optimizer_bit_identical(
                report.fits[fold],
                report.repeated_fits[fold]);
        const ModelIsolationReport isolation =
            apply_parameters(
                parent,
                report.fits[fold].parameters);
        const ModelIsolationReport repeated_isolation =
            apply_parameters(
                parent,
                report.repeated_fits[fold].parameters);
        report.model_isolation_passed =
            report.model_isolation_passed &&
            isolation.passed() &&
            repeated_isolation.passed();
        const auto actual =
            actual_residuals(
                held, isolation.candidate);
        const auto repeated_actual =
            actual_residuals(
                held, repeated_isolation.candidate);
        report.repeated_scores_bit_identical =
            report.repeated_scores_bit_identical &&
            nested_scores_bit_identical(
                actual, repeated_actual) &&
            isolation.candidate_fingerprint ==
                repeated_isolation.candidate_fingerprint;
        for (const Root& root : held.roots) {
            compare_forward(
                analytic_forward_impl(
                    root,
                    report.fits[fold].parameters),
                model_forward_impl(
                    root, isolation.candidate),
                report.maximum_preactivation_difference,
                report.maximum_activation_difference,
                report.maximum_logit_difference,
                report.maximum_residual_difference);
        }
        std::size_t held_index = 0;
        for (std::size_t root = 0;
             root < train.roots.size(); ++root) {
            if (train.roots[root]
                    .paired.schedule_index %
                    kFoldCount != fold) {
                continue;
            }
            candidate_residuals[root] =
                actual[held_index++];
            ++prediction_counts[root];
        }
        report.exact_balance =
            report.exact_balance &&
            held_index == held.roots.size();
    }
    report.exact_balance =
        report.exact_balance &&
        std::all_of(
            prediction_counts.begin(),
            prediction_counts.end(),
            [](std::size_t count) {
                return count == 1;
            });
    const pair::Dataset paired =
        paired_dataset(train);
    report.parent =
        pair::evaluate_residuals(
            paired, parent_residuals);
    report.candidate =
        pair::evaluate_residuals(
            paired, candidate_residuals);
    return report;
}

OfflineGate evaluate_offline_gate(
    const OfflineGateInputs& inputs) {
    const bool hidden_repartition_passed =
        inputs.hidden_repartition_live_probe_eligible &&
        inputs.hidden_repartition_nonvacuous &&
        inputs
            .hidden_repartition_owner_observation_bit_identical &&
        inputs.hidden_repartition_actions_bit_identical &&
        inputs.hidden_repartition_options_bit_identical &&
        inputs.hidden_repartition_logits_bit_identical &&
        inputs
            .hidden_repartition_centered_logits_bit_identical &&
        inputs.hidden_repartition_residuals_bit_identical;
    OfflineGate gate =
        pair::evaluate_offline_gate({
            .parent_train = inputs.parent_train,
            .candidate_train = inputs.candidate_train,
            .parent_oof = inputs.parent_oof,
            .candidate_oof = inputs.candidate_oof,
            .parent_dev = inputs.parent_dev,
            .candidate_dev = inputs.candidate_dev,
            .cache_identity_exact =
                inputs.cache_identity_exact,
            .pair_census_exact =
                inputs.pair_census_exact,
            .optimizer_recipe_exact =
                inputs.optimizer_recipe_exact,
            .grouped_folds_exact =
                inputs.grouped_folds_exact,
            .repeat_fits_bit_identical =
                inputs.repeat_fits_bit_identical,
            .parameter_replay_bit_identical =
                inputs.parameter_replay_bit_identical,
            .zero_delta_equivalent =
                inputs.zero_delta_equivalent,
            .actual_model_agreement =
                inputs.actual_model_agreement &&
                hidden_repartition_passed,
            .parent_immutable =
                inputs.parent_immutable,
            .model_isolation_passed =
                inputs.model_isolation_passed,
            .successor_predictions_bit_identical =
                inputs
                    .successor_predictions_bit_identical,
            .successor_metrics_bit_identical =
                inputs
                    .successor_metrics_bit_identical,
        });
    if (!hidden_repartition_passed) {
        gate.failures.push_back(
            "live hidden-repartition witness failed");
    }
    return gate;
}

bool selector_config_exact(
    const BotConfig& challenger,
    const BotConfig& baseline,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    return pair::selector_config_exact(
        challenger, baseline, parent, candidate);
}

bool model_scores_bit_identical(
    const Dataset& dataset,
    const std::shared_ptr<const LearnedModel>& first,
    const std::shared_ptr<const LearnedModel>& second) {
    validate_dataset(dataset);
    if (!first || !second) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK score comparison model is null");
    }
    for (const Root& root : dataset.roots) {
        if (!vector_bit_identical(
                learned_policy_head_logits(
                    root.paired.options,
                    LearnedPolicyDecisionKind::Priority,
                    first),
                learned_policy_head_logits(
                    root.paired.options,
                    LearnedPolicyDecisionKind::Priority,
                    second))) {
            return false;
        }
    }
    return nested_scores_bit_identical(
        actual_residuals(dataset, first),
        actual_residuals(dataset, second));
}

namespace testing {

HiddenRepartitionWitness
compare_hidden_repartition_views(
    bool live_probe_eligible,
    bool opponent_hidden_zones_nonvacuous,
    const HiddenRepartitionView& original,
    const HiddenRepartitionView& repartitioned) {
    return {
        .live_probe_eligible =
            live_probe_eligible,
        .nonvacuous =
            opponent_hidden_zones_nonvacuous,
        .owner_observation_bit_identical =
            vector_bit_identical(
                original.owner_observation,
                repartitioned.owner_observation),
        .actions_bit_identical =
            original.actions == repartitioned.actions,
        .options_bit_identical =
            matrix_bit_identical(
                original.option_rows,
                repartitioned.option_rows),
        .logits_bit_identical =
            vector_bit_identical(
                original.logits,
                repartitioned.logits),
        .centered_logits_bit_identical =
            vector_bit_identical(
                original.centered_logits,
                repartitioned.centered_logits),
        .residuals_bit_identical =
            vector_bit_identical(
                original.centered_residuals,
                repartitioned.centered_residuals),
    };
}

Dataset make_dataset(std::vector<Root> roots) {
    Dataset result =
        make_dataset_impl(std::move(roots));
    validate_dataset(result);
    return result;
}

Corpus make_corpus(
    Dataset train, Dataset dev,
    LearnedModelComponentFingerprints parent_components) {
    return {
        .train = std::move(train),
        .dev = std::move(dev),
        .source_digest =
            std::string(dbc::kFrozenCorpusDigest),
        .parent_components =
            std::move(parent_components),
    };
}

ObjectiveProbe objective_probe(
    const Dataset& dataset,
    const Parameters& parameters) {
    validate_dataset(dataset);
    if (!finite(parameters)) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK probe parameters are nonfinite");
    }
    const ObjectiveResult result =
        objective_and_gradient(
            dataset, parameters,
            OptimizerConfig{});
    return {
        .objective = result.value,
        .gradient = result.gradient,
    };
}

ForwardProbe analytic_forward(
    const Root& root,
    const Parameters& parameters) {
    if (!finite(parameters)) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK analytic parameters are nonfinite");
    }
    return analytic_forward_impl(root, parameters);
}

ForwardProbe model_forward(
    const Root& root,
    std::shared_ptr<const LearnedModel> model) {
    return model_forward_impl(root, std::move(model));
}

Root permute_actions(
    const Root& root,
    const std::vector<std::size_t>& permutation) {
    const std::size_t actions =
        root.paired.ranking.actions.size();
    if (permutation.size() != actions) {
        throw std::invalid_argument(
            "DBC5-ADAPTIVE-TRUNK permutation width drifted");
    }
    std::vector<bool> seen(actions, false);
    Root result = root;
    for (std::size_t index = 0;
         index < actions; ++index) {
        if (permutation[index] >= actions ||
            seen[permutation[index]]) {
            throw std::invalid_argument(
                "DBC5-ADAPTIVE-TRUNK permutation is invalid");
        }
        seen[permutation[index]] = true;
        result.paired.ranking.actions[index] =
            root.paired.ranking.actions[
                permutation[index]];
        result.paired.options[index] =
            root.paired.options[permutation[index]];
        result.paired.hidden[index] =
            root.paired.hidden[permutation[index]];
        result.parent_preactivations[index] =
            root.parent_preactivations[
                permutation[index]];
    }
    return result;
}

bool legal_action_permutation_equivariant(
    const Root& root,
    const Parameters& parameters,
    std::shared_ptr<const LearnedModel> model) {
    const std::size_t actions =
        root.paired.ranking.actions.size();
    std::vector<std::size_t> permutation(actions);
    std::iota(
        permutation.begin(), permutation.end(), 0);
    std::reverse(
        permutation.begin(), permutation.end());
    const Root permuted =
        permute_actions(root, permutation);
    const ForwardProbe analytic =
        analytic_forward_impl(root, parameters);
    const ForwardProbe permuted_analytic =
        analytic_forward_impl(permuted, parameters);
    const ForwardProbe actual =
        model_forward_impl(root, model);
    const ForwardProbe permuted_actual =
        model_forward_impl(permuted, std::move(model));
    for (std::size_t index = 0;
         index < actions; ++index) {
        const std::size_t source = permutation[index];
        for (std::size_t hidden = 0;
             hidden < kHiddenCount; ++hidden) {
            if (!equal_bits(
                    analytic
                        .preactivations[source][hidden],
                    permuted_analytic
                        .preactivations[index][hidden]) ||
                !equal_bits(
                    analytic.activations[source][hidden],
                    permuted_analytic
                        .activations[index][hidden]) ||
                !equal_bits(
                    actual
                        .preactivations[source][hidden],
                    permuted_actual
                        .preactivations[index][hidden]) ||
                !equal_bits(
                    actual.activations[source][hidden],
                    permuted_actual
                        .activations[index][hidden])) {
                return false;
            }
        }
        if (!equal_bits(
                analytic.logits[source],
                permuted_analytic.logits[index]) ||
            !equal_bits(
                actual.logits[source],
                permuted_actual.logits[index]) ||
            std::abs(
                analytic.residuals[source] -
                permuted_analytic.residuals[index]) >
                kMaximumAgreementError ||
            std::abs(
                actual.residuals[source] -
                permuted_actual.residuals[index]) >
                kMaximumAgreementError) {
            return false;
        }
    }
    return true;
}

} // namespace testing

} // namespace old_school::decision_boundary_adaptive_trunk
