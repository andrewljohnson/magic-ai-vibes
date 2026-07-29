#include "old_school/decision_boundary_action_pair.hpp"

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
#include <string>
#include <utility>

namespace old_school::decision_boundary_action_pair {
namespace {

constexpr std::uint64_t kPositiveZeroBits = UINT64_C(0);

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR deck is invalid");
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

bool valid_hidden(std::span<const double> values) {
    return finite(values) &&
           std::all_of(
               values.begin(), values.end(),
               [](double value) {
                   return value >= -1.0 &&
                          value <= 1.0;
               });
}

double dot(
    std::span<const double> left,
    std::span<const double> right) {
    if (left.size() != right.size()) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR dot width drifted");
    }
    double result = 0.0;
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        result += left[index] * right[index];
    }
    if (!std::isfinite(result)) {
        throw std::runtime_error(
            "DBC4-ACTION-PAIR dot product is nonfinite");
    }
    return result;
}

double l2_norm(std::span<const double> values) {
    double squared = 0.0;
    for (const double value : values) {
        squared += value * value;
    }
    return std::sqrt(squared);
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
        ? logit_value + std::log1p(
              std::exp(-logit_value))
        : std::log1p(std::exp(logit_value));
    return softplus - target * logit_value;
}

double teacher_action_score(
    const direct::RankAction& action) {
    double total = 0.0;
    for (const direct::RankCell& cell : action.worlds) {
        total += cell.teacher_target;
    }
    return total /
           static_cast<double>(action.worlds.size());
}

double parent_cell_score(const direct::RankCell& cell) {
    if (cell.terminal_before_boundary) {
        return cell.teacher_target;
    }
    return
        (cell.parent_leaf_values[0] +
         cell.parent_leaf_values[1]) /
        static_cast<double>(direct::kLeafCount);
}

double parent_action_score(
    const direct::RankAction& action) {
    double total = 0.0;
    for (const direct::RankCell& cell : action.worlds) {
        total += parent_cell_score(cell);
    }
    return total /
           static_cast<double>(action.worlds.size());
}

std::vector<double> softmax(
    std::span<const double> values, double temperature) {
    if (values.empty() || !std::isfinite(temperature) ||
        temperature <= 0.0) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR softmax input is invalid");
    }
    const double maximum =
        *std::max_element(values.begin(), values.end());
    std::vector<double> result(values.size());
    double total = 0.0;
    for (std::size_t index = 0;
         index < values.size(); ++index) {
        result[index] =
            std::exp(
                (values[index] - maximum) /
                temperature);
        total += result[index];
    }
    for (double& value : result) {
        value /= total;
    }
    return result;
}

std::vector<double> mixed_distribution(
    std::span<const double> values) {
    std::vector<double> result =
        softmax(values, direct::kListwiseTemperature);
    const double uniform =
        (1.0 - direct::kListwiseMix) /
        static_cast<double>(result.size());
    for (double& value : result) {
        value =
            direct::kListwiseMix * value + uniform;
    }
    return result;
}

std::vector<std::size_t> exact_max_support(
    std::span<const double> values) {
    const double maximum =
        *std::max_element(values.begin(), values.end());
    std::vector<std::size_t> result;
    for (std::size_t index = 0;
         index < values.size(); ++index) {
        if (values[index] == maximum) {
            result.push_back(index);
        }
    }
    return result;
}

double sample_standard_error(
    std::span<const double> samples) {
    if (samples.size() < 2) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR stable pair needs worlds");
    }
    const double mean =
        std::accumulate(
            samples.begin(), samples.end(), 0.0) /
        static_cast<double>(samples.size());
    double squared = 0.0;
    for (const double sample : samples) {
        const double difference = sample - mean;
        squared += difference * difference;
    }
    return std::sqrt(
        squared /
        static_cast<double>(
            samples.size() * (samples.size() - 1)));
}

direct::Dataset ranking_dataset(const Dataset& dataset) {
    std::vector<direct::RankRoot> roots;
    roots.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        roots.push_back(root.ranking);
    }
    return direct::testing::make_dataset(
        std::move(roots));
}

std::vector<double> analytic_residuals(
    const Root& root, const Delta& delta,
    double residual_weight = kResidualWeight) {
    std::vector<double> logits;
    logits.reserve(root.hidden.size());
    for (const Hidden& hidden : root.hidden) {
        logits.push_back(dot(hidden, delta));
    }
    const double mean =
        std::accumulate(
            logits.begin(), logits.end(), 0.0) /
        static_cast<double>(logits.size());
    for (double& logit : logits) {
        logit =
            residual_weight *
            std::tanh(logit - mean);
    }
    return logits;
}

std::vector<std::vector<double>> analytic_residuals(
    const Dataset& dataset, const Delta& delta,
    double residual_weight = kResidualWeight) {
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        result.push_back(
            analytic_residuals(
                root, delta, residual_weight));
    }
    return result;
}

struct ActionScores {
    std::vector<double> teacher;
    std::vector<double> candidate;
};

ActionScores action_scores(
    const Root& root,
    std::span<const double> residuals) {
    if (residuals.size() !=
        root.ranking.actions.size()) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR residual width drifted");
    }
    ActionScores result;
    result.teacher.reserve(
        root.ranking.actions.size());
    result.candidate.reserve(
        root.ranking.actions.size());
    for (std::size_t action = 0;
         action < root.ranking.actions.size();
         ++action) {
        result.teacher.push_back(
            teacher_action_score(
                root.ranking.actions[action]));
        result.candidate.push_back(
            parent_action_score(
                root.ranking.actions[action]) +
            residuals[action]);
    }
    return result;
}

PairMetrics pair_metrics(
    const Dataset& dataset,
    const std::vector<std::vector<double>>& residuals) {
    if (residuals.size() != dataset.roots.size()) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR pair-score roots drifted");
    }
    struct Accumulator {
        std::size_t roots = 0;
        std::size_t all_tied_roots = 0;
        std::size_t unordered_pairs = 0;
        std::size_t eligible_pairs = 0;
        double bce = 0.0;
    };
    std::array<Accumulator, kDeckCount> accumulators{};
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size();
         ++root_index) {
        const Root& root = dataset.roots[root_index];
        const ActionScores scores =
            action_scores(root, residuals[root_index]);
        Accumulator& accumulator =
            accumulators[deck_index(root.ranking.deck)];
        ++accumulator.roots;
        const std::size_t action_count =
            scores.teacher.size();
        accumulator.unordered_pairs +=
            action_count * (action_count - 1) / 2;
        double total_cost = 0.0;
        for (std::size_t first = 0;
             first < action_count; ++first) {
            for (std::size_t second = first + 1;
                 second < action_count; ++second) {
                total_cost +=
                    std::abs(
                        scores.teacher[first] -
                        scores.teacher[second]);
            }
        }
        if (total_cost == 0.0) {
            ++accumulator.all_tied_roots;
            continue;
        }
        double root_bce = 0.0;
        for (std::size_t first = 0;
             first < action_count; ++first) {
            for (std::size_t second = first + 1;
                 second < action_count; ++second) {
                const double teacher_difference =
                    scores.teacher[first] -
                    scores.teacher[second];
                if (teacher_difference == 0.0) {
                    continue;
                }
                ++accumulator.eligible_pairs;
                const double candidate_difference =
                    scores.candidate[first] -
                    scores.candidate[second];
                const double target =
                    sigmoid(
                        teacher_difference /
                        kPairTemperature);
                const double candidate_logit =
                    candidate_difference /
                    kPairTemperature;
                root_bce +=
                    std::abs(teacher_difference) /
                    total_cost *
                    binary_cross_entropy_from_logit(
                        target, candidate_logit);
            }
        }
        accumulator.bce += root_bce;
    }

    PairMetrics result;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const Accumulator& accumulator =
            accumulators[deck];
        if (accumulator.roots == 0) {
            throw std::invalid_argument(
                "DBC4-ACTION-PAIR dataset deck is empty");
        }
        PairDeckMetrics row{
            .deck = static_cast<DeckId>(deck),
            .roots = accumulator.roots,
            .all_tied_roots =
                accumulator.all_tied_roots,
            .unordered_pairs =
                accumulator.unordered_pairs,
            .eligible_pairs =
                accumulator.eligible_pairs,
            .pair_bce =
                accumulator.bce /
                static_cast<double>(
                    accumulator.roots),
        };
        result.decks[deck] = row;
        result.roots += row.roots;
        result.all_tied_roots +=
            row.all_tied_roots;
        result.unordered_pairs +=
            row.unordered_pairs;
        result.eligible_pairs +=
            row.eligible_pairs;
        result.equal_deck_pair_bce +=
            row.pair_bce /
            static_cast<double>(kDeckCount);
    }
    return result;
}

direct::Metrics ranking_metrics(
    const Dataset& dataset,
    const std::vector<std::vector<double>>& residuals) {
    direct::Metrics result =
        direct::evaluate(
            ranking_dataset(dataset),
            std::vector<double>(
                direct::kFeatureCount, 0.0));
    struct Accumulator {
        std::size_t roots = 0;
        std::size_t stable_pairs = 0;
        double cross_entropy = 0.0;
        double agreement = 0.0;
        double stable_agreement = 0.0;
        double regret = 0.0;
    };
    std::array<Accumulator, kDeckCount> accumulators{};
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size();
         ++root_index) {
        const Root& root = dataset.roots[root_index];
        const ActionScores scores =
            action_scores(root, residuals[root_index]);
        Accumulator& accumulator =
            accumulators[deck_index(root.ranking.deck)];
        ++accumulator.roots;
        const std::vector<double> teacher_distribution =
            mixed_distribution(scores.teacher);
        const std::vector<double> candidate_distribution =
            mixed_distribution(scores.candidate);
        for (std::size_t action = 0;
             action < scores.teacher.size(); ++action) {
            accumulator.cross_entropy -=
                teacher_distribution[action] *
                std::log(candidate_distribution[action]);
        }
        const auto teacher_support =
            exact_max_support(scores.teacher);
        const auto candidate_support =
            exact_max_support(scores.candidate);
        double selected_teacher = 0.0;
        std::size_t overlap = 0;
        for (const std::size_t action :
             candidate_support) {
            selected_teacher +=
                scores.teacher[action];
            overlap +=
                std::find(
                    teacher_support.begin(),
                    teacher_support.end(), action) !=
                    teacher_support.end()
                ? 1U
                : 0U;
        }
        selected_teacher /=
            static_cast<double>(
                candidate_support.size());
        accumulator.agreement +=
            static_cast<double>(overlap) /
            static_cast<double>(
                candidate_support.size());
        accumulator.regret +=
            scores.teacher[teacher_support.front()] -
            selected_teacher;

        for (std::size_t first = 0;
             first < root.ranking.actions.size();
             ++first) {
            for (std::size_t second = first + 1;
                 second < root.ranking.actions.size();
                 ++second) {
                std::vector<double> differences;
                differences.reserve(
                    root.ranking.actions[first]
                        .worlds.size());
                for (std::size_t world = 0;
                     world <
                     root.ranking.actions[first]
                         .worlds.size();
                     ++world) {
                    differences.push_back(
                        root.ranking.actions[first]
                            .worlds[world]
                            .teacher_target -
                        root.ranking.actions[second]
                            .worlds[world]
                            .teacher_target);
                }
                const double teacher_difference =
                    scores.teacher[first] -
                    scores.teacher[second];
                const double uncertainty =
                    direct::
                        kStablePairNormal95CriticalValue *
                    sample_standard_error(differences);
                if (std::abs(teacher_difference) <
                        direct::
                            kStablePairMinimumDelta ||
                    std::abs(teacher_difference) <=
                        uncertainty) {
                    continue;
                }
                ++accumulator.stable_pairs;
                const double candidate_difference =
                    scores.candidate[first] -
                    scores.candidate[second];
                if (candidate_difference == 0.0) {
                    accumulator.stable_agreement +=
                        0.5;
                } else if (
                    (candidate_difference > 0.0) ==
                    (teacher_difference > 0.0)) {
                    accumulator.stable_agreement +=
                        1.0;
                }
            }
        }
    }

    result.equal_deck_listwise_cross_entropy = 0.0;
    result.equal_deck_top_one_expected_agreement = 0.0;
    result.equal_deck_stable_pair_agreement = 0.0;
    result.equal_deck_mean_regret = 0.0;
    result.stable_pairs = 0;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const Accumulator& accumulator =
            accumulators[deck];
        auto& row = result.decks[deck];
        row.stable_pairs =
            accumulator.stable_pairs;
        row.listwise_cross_entropy =
            accumulator.cross_entropy /
            static_cast<double>(accumulator.roots);
        row.top_one_expected_agreement =
            accumulator.agreement /
            static_cast<double>(accumulator.roots);
        row.stable_pair_agreement =
            accumulator.stable_pairs == 0
            ? 0.0
            : accumulator.stable_agreement /
              static_cast<double>(
                  accumulator.stable_pairs);
        row.mean_regret =
            accumulator.regret /
            static_cast<double>(accumulator.roots);
        result.stable_pairs += row.stable_pairs;
        result.equal_deck_listwise_cross_entropy +=
            row.listwise_cross_entropy /
            static_cast<double>(kDeckCount);
        result.equal_deck_top_one_expected_agreement +=
            row.top_one_expected_agreement /
            static_cast<double>(kDeckCount);
        result.equal_deck_stable_pair_agreement +=
            row.stable_pair_agreement /
            static_cast<double>(kDeckCount);
        result.equal_deck_mean_regret +=
            row.mean_regret /
            static_cast<double>(kDeckCount);
    }
    return result;
}

Metrics metrics_from_residuals(
    const Dataset& dataset,
    const std::vector<std::vector<double>>& residuals) {
    return {
        .pairs = pair_metrics(dataset, residuals),
        .ranking =
            ranking_metrics(dataset, residuals),
    };
}

struct ObjectiveResult {
    double value = 0.0;
    Delta gradient{};
};

ObjectiveResult objective_and_gradient(
    const Dataset& dataset, const Delta& delta,
    const OptimizerConfig& config) {
    ObjectiveResult result;
    for (const Root& root : dataset.roots) {
        const std::size_t actions =
            root.ranking.actions.size();
        std::vector<double> teacher(actions);
        std::vector<double> base(actions);
        std::vector<double> logits(actions);
        Hidden mean_hidden{};
        for (std::size_t action = 0;
             action < actions; ++action) {
            teacher[action] =
                teacher_action_score(
                    root.ranking.actions[action]);
            base[action] =
                parent_action_score(
                    root.ranking.actions[action]);
            logits[action] =
                dot(root.hidden[action], delta);
            for (std::size_t hidden = 0;
                 hidden < kHiddenCount; ++hidden) {
                mean_hidden[hidden] +=
                    root.hidden[action][hidden] /
                    static_cast<double>(actions);
            }
        }
        const double mean_logit =
            std::accumulate(
                logits.begin(), logits.end(), 0.0) /
            static_cast<double>(actions);
        std::vector<double> score(actions);
        std::vector<double> tanh_centered(actions);
        for (std::size_t action = 0;
             action < actions; ++action) {
            tanh_centered[action] =
                std::tanh(logits[action] - mean_logit);
            score[action] =
                base[action] +
                config.residual_weight *
                tanh_centered[action];
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
                    deck_index(root.ranking.deck)]);
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
                const double prediction =
                    sigmoid(
                        (score[first] - score[second]) /
                        config.pair_temperature);
                result.value +=
                    outer_weight * pair_weight *
                    binary_cross_entropy_from_logit(
                        target,
                        (score[first] - score[second]) /
                            config.pair_temperature);
                const double derivative =
                    outer_weight * pair_weight *
                    (prediction - target) /
                    config.pair_temperature;
                for (std::size_t hidden = 0;
                     hidden < kHiddenCount; ++hidden) {
                    const double first_derivative =
                        config.residual_weight *
                        (1.0 -
                         tanh_centered[first] *
                         tanh_centered[first]) *
                        (root.hidden[first][hidden] -
                         mean_hidden[hidden]);
                    const double second_derivative =
                        config.residual_weight *
                        (1.0 -
                         tanh_centered[second] *
                         tanh_centered[second]) *
                        (root.hidden[second][hidden] -
                         mean_hidden[hidden]);
                    result.gradient[hidden] +=
                        derivative *
                        (first_derivative -
                         second_derivative);
                }
            }
        }
    }
    for (std::size_t hidden = 0;
         hidden < kHiddenCount; ++hidden) {
        result.value +=
            0.5 * config.l2_tether *
            delta[hidden] * delta[hidden];
        result.gradient[hidden] +=
            config.l2_tether * delta[hidden];
    }
    if (!std::isfinite(result.value) ||
        !finite(result.gradient)) {
        throw std::runtime_error(
            "DBC4-ACTION-PAIR objective is nonfinite");
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
            "DBC4-ACTION-PAIR requires fixed optimizer recipe");
    }
}

bool positive_zero(double value) {
    return std::bit_cast<std::uint64_t>(value) ==
           kPositiveZeroBits;
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

Hidden hidden_activation(
    std::span<const double> option,
    const LearnedPriorityHeadParameters& parameters) {
    Hidden result{};
    for (std::size_t hidden = 0;
         hidden < kHiddenCount; ++hidden) {
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
        result[hidden] = std::tanh(total);
    }
    return result;
}

std::vector<std::vector<double>> actual_residuals(
    const Dataset& dataset,
    std::shared_ptr<const LearnedModel> model) {
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        std::vector<double> logits =
            learned_policy_head_logits(
                root.options,
                LearnedPolicyDecisionKind::Priority,
                model);
        const double mean =
            std::accumulate(
                logits.begin(), logits.end(), 0.0) /
            static_cast<double>(logits.size());
        for (double& logit : logits) {
            logit =
                kResidualWeight *
                std::tanh(logit - mean);
        }
        result.push_back(std::move(logits));
    }
    return result;
}

double maximum_difference(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR score roots drifted");
    }
    double maximum = 0.0;
    for (std::size_t root = 0;
         root < first.size(); ++root) {
        if (first[root].size() != second[root].size()) {
            throw std::invalid_argument(
                "DBC4-ACTION-PAIR score actions drifted");
        }
        for (std::size_t action = 0;
             action < first[root].size(); ++action) {
            maximum =
                std::max(
                    maximum,
                    std::abs(
                        first[root][action] -
                        second[root][action]));
        }
    }
    return maximum;
}

bool equal_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool delta_bit_identical(
    const Delta& first, const Delta& second) {
    for (std::size_t index = 0;
         index < kHiddenCount; ++index) {
        if (!equal_bits(first[index], second[index])) {
            return false;
        }
    }
    return true;
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

bool nested_scores_bit_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t root = 0;
         root < first.size(); ++root) {
        if (first[root].size() != second[root].size()) {
            return false;
        }
        for (std::size_t action = 0;
             action < first[root].size(); ++action) {
            if (!equal_bits(
                    first[root][action],
                    second[root][action])) {
                return false;
            }
        }
    }
    return true;
}

bool successor_metrics_equal(
    const direct::Metrics& first,
    const direct::Metrics& second) {
    if (first.roots != second.roots ||
        first.eligible_cells != second.eligible_cells ||
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

bool pair_metrics_bit_identical(
    const PairMetrics& first,
    const PairMetrics& second) {
    if (first.roots != second.roots ||
        first.all_tied_roots !=
            second.all_tied_roots ||
        first.unordered_pairs !=
            second.unordered_pairs ||
        first.eligible_pairs !=
            second.eligible_pairs ||
        !equal_bits(
            first.equal_deck_pair_bce,
            second.equal_deck_pair_bce)) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& left = first.decks[deck];
        const auto& right = second.decks[deck];
        if (left.deck != right.deck ||
            left.roots != right.roots ||
            left.all_tied_roots !=
                right.all_tied_roots ||
            left.unordered_pairs !=
                right.unordered_pairs ||
            left.eligible_pairs !=
                right.eligible_pairs ||
            !equal_bits(left.pair_bce, right.pair_bce)) {
            return false;
        }
    }
    return true;
}

bool direct_metrics_bit_identical(
    const direct::Metrics& first,
    const direct::Metrics& second) {
    if (first.roots != second.roots ||
        first.eligible_cells != second.eligible_cells ||
        first.stable_pairs != second.stable_pairs ||
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
            second.equal_deck_successor_ece) ||
        !equal_bits(
            first.equal_deck_listwise_cross_entropy,
            second.equal_deck_listwise_cross_entropy) ||
        !equal_bits(
            first.equal_deck_top_one_expected_agreement,
            second.equal_deck_top_one_expected_agreement) ||
        !equal_bits(
            first.equal_deck_stable_pair_agreement,
            second.equal_deck_stable_pair_agreement) ||
        !equal_bits(
            first.equal_deck_mean_regret,
            second.equal_deck_mean_regret)) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& left = first.decks[deck];
        const auto& right = second.decks[deck];
        if (left.deck != right.deck ||
            left.roots != right.roots ||
            left.eligible_cells !=
                right.eligible_cells ||
            left.stable_pairs != right.stable_pairs ||
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
                right.successor_ece) ||
            !equal_bits(
                left.listwise_cross_entropy,
                right.listwise_cross_entropy) ||
            !equal_bits(
                left.top_one_expected_agreement,
                right.top_one_expected_agreement) ||
            !equal_bits(
                left.stable_pair_agreement,
                right.stable_pair_agreement) ||
            !equal_bits(
                left.mean_regret,
                right.mean_regret)) {
            return false;
        }
    }
    return true;
}

bool metrics_bit_identical(
    const Metrics& first, const Metrics& second) {
    return pair_metrics_bit_identical(
               first.pairs, second.pairs) &&
           direct_metrics_bit_identical(
               first.ranking, second.ranking);
}

bool successor_predictions_equal(
    const Dataset& dataset,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    for (const Root& root : dataset.roots) {
        for (const direct::RankAction& action :
             root.ranking.actions) {
            for (const direct::RankCell& cell :
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

Dataset select_fold(
    const Dataset& train, std::size_t held_out_fold,
    bool holdout) {
    if (held_out_fold >= kFoldCount) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR fold index is invalid");
    }
    std::vector<Root> roots;
    for (const Root& root : train.roots) {
        const bool in_fold =
            root.schedule_index % kFoldCount ==
            held_out_fold;
        if (in_fold == holdout) {
            roots.push_back(root);
        }
    }
    return testing::make_dataset(std::move(roots));
}

Dataset project_split(
    std::span<const dbc::RootExample> examples,
    const direct::Dataset& ranked,
    const LearnedPriorityHeadParameters& parameters) {
    if (examples.size() != ranked.roots.size()) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR projection roots drifted");
    }
    std::vector<Root> roots;
    roots.reserve(examples.size());
    for (std::size_t index = 0;
         index < examples.size(); ++index) {
        const dbc::RootExample& example =
            examples[index];
        if (example.manifest.stable_root_id !=
                ranked.roots[index].stable_root_id ||
            example.manifest.options.size() !=
                ranked.roots[index].actions.size() ||
            example.teacher_samples.size() !=
                ranked.roots[index].actions.size()) {
            throw std::invalid_argument(
                "DBC4-ACTION-PAIR source alignment drifted");
        }
        for (std::size_t action = 0;
             action < ranked.roots[index].actions.size();
             ++action) {
            const auto& worlds =
                ranked.roots[index].actions[action].worlds;
            if (worlds.size() !=
                example.teacher_samples[action].size()) {
                throw std::invalid_argument(
                    "DBC4-ACTION-PAIR teacher worlds drifted");
            }
            for (std::size_t world = 0;
                 world < worlds.size(); ++world) {
                if (worlds[world].teacher_target !=
                    example.teacher_samples[action][world]) {
                    throw std::invalid_argument(
                        "DBC4-ACTION-PAIR teacher alignment drifted");
                }
            }
        }
        std::vector<std::size_t> seen_worlds(
            ranked.roots[index].actions.size(), 0);
        for (const dbc::BoundaryCell& cell :
             example.cells) {
            if (cell.action_index >=
                    ranked.roots[index].actions.size() ||
                seen_worlds[cell.action_index] >=
                    ranked.roots[index]
                        .actions[cell.action_index]
                        .worlds.size()) {
                throw std::invalid_argument(
                    "DBC4-ACTION-PAIR cached cell order drifted");
            }
            const direct::RankCell& ranked_cell =
                ranked.roots[index]
                    .actions[cell.action_index]
                    .worlds[
                        seen_worlds[cell.action_index]++];
            if (ranked_cell.teacher_target !=
                    cell.teacher_target ||
                parent_cell_score(ranked_cell) !=
                    cell.parent_prediction) {
                throw std::invalid_argument(
                    "DBC4-ACTION-PAIR cached parent rescore drifted");
            }
        }
        Root root{
            .ranking = ranked.roots[index],
            .schedule_index =
                example.manifest.coordinate
                    .schedule_index,
            .actor =
                example.manifest.coordinate.actor,
            .options = example.manifest.options,
        };
        root.hidden.reserve(root.options.size());
        for (const auto& option : root.options) {
            root.hidden.push_back(
                hidden_activation(option, parameters));
        }
        roots.push_back(std::move(root));
    }
    return testing::make_dataset(std::move(roots));
}

bool metrics_non_increasing(
    double candidate, double parent) {
    return candidate <= parent;
}

} // namespace

bool ModelIsolationReport::safe_for_evaluation() const {
    if (!candidate ||
        !parent_positive_zero ||
        !parent_immutable ||
        !repeated_application_bit_identical ||
        !exact_delta ||
        changed_coordinates > kHiddenCount) {
        return false;
    }
    if (changed_coordinates == 0) {
        return parent_fingerprint_before ==
                   candidate_fingerprint &&
               parent_components ==
                   candidate_components;
    }
    return only_priority_hidden_output_changed;
}

bool ModelIsolationReport::passed() const {
    return safe_for_evaluation() &&
           parent_positive_zero &&
           changed_coordinates > 0 &&
           changed_coordinates <= kHiddenCount;
}

bool OfflineGate::passed() const {
    return failures.empty();
}

Corpus project_corpus(
    const dbc::Corpus& source,
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR parent is null");
    }
    dbc::validate_corpus(source);
    const auto parameters =
        learned_priority_head_parameters(parent);
    if (!parent_priority_positive_zero(parameters)) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR parent Priority head is not positive-zero");
    }
    const direct::Corpus ranked =
        direct::project_corpus(source, parent);
    Corpus result{
        .train =
            project_split(
                source.train, ranked.train,
                parameters),
        .dev =
            project_split(
                source.dev, ranked.dev,
                parameters),
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
            "DBC4-ACTION-PAIR dataset is empty");
    }
    std::array<std::size_t, kDeckCount> roots_by_deck{};
    for (const Root& root : dataset.roots) {
        if (root.ranking.stable_root_id.empty() ||
            root.actor >= 2 ||
            root.ranking.actions.size() < 2 ||
            root.options.size() !=
                root.ranking.actions.size() ||
            root.hidden.size() != root.options.size()) {
            throw std::invalid_argument(
                "DBC4-ACTION-PAIR root shape is invalid");
        }
        ++roots_by_deck[
            deck_index(root.ranking.deck)];
        std::size_t worlds = 0;
        for (std::size_t action = 0;
             action < root.ranking.actions.size();
             ++action) {
            if (root.options[action].size() !=
                    kPolicyFeatureCount ||
                !finite(root.options[action]) ||
                !valid_hidden(root.hidden[action]) ||
                root.ranking.actions[action]
                    .worlds.size() < 2) {
                throw std::invalid_argument(
                    "DBC4-ACTION-PAIR action shape is invalid");
            }
            if (action == 0) {
                worlds =
                    root.ranking.actions[action]
                        .worlds.size();
            } else if (
                root.ranking.actions[action]
                    .worlds.size() != worlds) {
                throw std::invalid_argument(
                    "DBC4-ACTION-PAIR worlds are unpaired");
            }
        }
    }
    if (roots_by_deck != dataset.roots_by_deck ||
        std::any_of(
            roots_by_deck.begin(),
            roots_by_deck.end(),
            [](std::size_t count) {
                return count == 0;
            })) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR deck census drifted");
    }
    direct::validate_dataset(
        ranking_dataset(dataset));
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
            "DBC4-ACTION-PAIR corpus identity drifted");
    }
}

PairMetrics pair_census(const Dataset& dataset) {
    validate_dataset(dataset);
    return pair_metrics(
        dataset,
        analytic_residuals(dataset, Delta{}));
}

bool frozen_pair_census_exact(const Corpus& corpus) {
    const PairMetrics train = pair_census(corpus.train);
    const PairMetrics dev = pair_census(corpus.dev);
    constexpr std::array<std::size_t, kDeckCount>
        train_tied{1, 0, 0, 0, 0};
    constexpr std::array<std::size_t, kDeckCount>
        train_pairs{38, 79, 35, 72, 38};
    constexpr std::array<std::size_t, kDeckCount>
        train_eligible{35, 76, 31, 66, 38};
    constexpr std::array<std::size_t, kDeckCount>
        dev_tied{1, 0, 3, 3, 0};
    constexpr std::array<std::size_t, kDeckCount>
        dev_pairs{66, 74, 49, 126, 279};
    constexpr std::array<std::size_t, kDeckCount>
        dev_eligible{40, 71, 41, 78, 240};
    if (train.roots != 80 ||
        train.all_tied_roots != 1 ||
        train.unordered_pairs != 262 ||
        train.eligible_pairs != 246 ||
        dev.roots != 80 ||
        dev.all_tied_roots != 7 ||
        dev.unordered_pairs != 594 ||
        dev.eligible_pairs != 470) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (train.decks[deck].roots != 16 ||
            train.decks[deck].all_tied_roots !=
                train_tied[deck] ||
            train.decks[deck].unordered_pairs !=
                train_pairs[deck] ||
            train.decks[deck].eligible_pairs !=
                train_eligible[deck] ||
            dev.decks[deck].roots != 16 ||
            dev.decks[deck].all_tied_roots !=
                dev_tied[deck] ||
            dev.decks[deck].unordered_pairs !=
                dev_pairs[deck] ||
            dev.decks[deck].eligible_pairs !=
                dev_eligible[deck]) {
            return false;
        }
    }
    return true;
}

Dataset fold_training_dataset(
    const Dataset& train, std::size_t held_out_fold) {
    return select_fold(
        train, held_out_fold, false);
}

Dataset fold_holdout_dataset(
    const Dataset& train, std::size_t held_out_fold) {
    return select_fold(
        train, held_out_fold, true);
}

Metrics evaluate(
    const Dataset& dataset, const Delta& delta) {
    validate_dataset(dataset);
    if (!finite(delta)) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR delta is nonfinite");
    }
    return metrics_from_residuals(
        dataset,
        analytic_residuals(dataset, delta));
}

OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config) {
    validate_dataset(train);
    validate_optimizer_config(config);
    OptimizerReport report{
        .config = config,
        .before = evaluate(train, Delta{}),
    };
    const ObjectiveResult initial =
        objective_and_gradient(
            train, report.delta, config);
    report.initial_objective = initial.value;
    Delta first_moment{};
    Delta second_moment{};
    double beta_one_power = 1.0;
    double beta_two_power = 1.0;
    for (std::size_t step = 0;
         step < config.steps; ++step) {
        ObjectiveResult current =
            objective_and_gradient(
                train, report.delta, config);
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
            for (double& gradient :
                 current.gradient) {
                gradient *= scale;
            }
            ++report.clipped_steps;
        }
        beta_one_power *= config.beta_one;
        beta_two_power *= config.beta_two;
        for (std::size_t hidden = 0;
             hidden < kHiddenCount; ++hidden) {
            first_moment[hidden] =
                config.beta_one *
                    first_moment[hidden] +
                (1.0 - config.beta_one) *
                    current.gradient[hidden];
            second_moment[hidden] =
                config.beta_two *
                    second_moment[hidden] +
                (1.0 - config.beta_two) *
                    current.gradient[hidden] *
                    current.gradient[hidden];
            const double corrected_first =
                first_moment[hidden] /
                (1.0 - beta_one_power);
            const double corrected_second =
                second_moment[hidden] /
                (1.0 - beta_two_power);
            report.delta[hidden] -=
                config.learning_rate *
                corrected_first /
                (std::sqrt(corrected_second) +
                 config.epsilon);
        }
        ++report.completed_steps;
    }
    const ObjectiveResult final =
        objective_and_gradient(
            train, report.delta, config);
    report.final_objective = final.value;
    report.delta_l2_norm =
        l2_norm(report.delta);
    report.final_gradient_l2_norm =
        l2_norm(final.gradient);
    report.after = evaluate(train, report.delta);
    return report;
}

bool optimizer_bit_identical(
    const OptimizerReport& first,
    const OptimizerReport& second) {
    return first.config == second.config &&
           delta_bit_identical(
               first.delta, second.delta) &&
           first.completed_steps ==
               second.completed_steps &&
           equal_bits(
               first.initial_objective,
               second.initial_objective) &&
           equal_bits(
               first.final_objective,
               second.final_objective) &&
           equal_bits(
               first.delta_l2_norm,
               second.delta_l2_norm) &&
           equal_bits(
               first.final_gradient_l2_norm,
               second.final_gradient_l2_norm) &&
           equal_bits(
               first.maximum_preclip_gradient_l2_norm,
               second.maximum_preclip_gradient_l2_norm) &&
           first.clipped_steps == second.clipped_steps &&
           metrics_bit_identical(
               first.before, second.before) &&
           metrics_bit_identical(
               first.after, second.after);
}

ModelIsolationReport apply_delta(
    std::shared_ptr<const LearnedModel> parent,
    const Delta& delta) {
    if (!parent || !finite(delta)) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR application input is invalid");
    }
    for (const double value : delta) {
        if (value == 0.0 && !positive_zero(value)) {
            throw std::invalid_argument(
                "DBC4-ACTION-PAIR delta contains negative zero");
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
            "DBC4-ACTION-PAIR parent is not positive-zero");
    }
    auto candidate_parameters = parent_parameters;
    candidate_parameters.hidden_output.assign(
        delta.begin(), delta.end());
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
    report.exact_delta =
        matrix_bit_identical(
            actual.input_hidden,
            parent_parameters.input_hidden) &&
        vector_bit_identical(
            actual.hidden_bias,
            parent_parameters.hidden_bias) &&
        vector_bit_identical(
            actual.hidden_output,
            candidate_parameters.hidden_output) &&
        vector_bit_identical(
            actual.direct,
            parent_parameters.direct) &&
        std::bit_cast<std::uint64_t>(
            actual.output_bias) ==
            std::bit_cast<std::uint64_t>(
                parent_parameters.output_bias);
    for (std::size_t hidden = 0;
         hidden < kHiddenCount; ++hidden) {
        report.changed_coordinates +=
            std::bit_cast<std::uint64_t>(
                actual.hidden_output[hidden]) !=
            kPositiveZeroBits
            ? 1U
            : 0U;
    }
    report.only_priority_hidden_output_changed =
        report.exact_delta &&
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
    const Delta& delta) {
    validate_corpus(corpus, parent);
    if (!isolation.candidate ||
        !isolation.safe_for_evaluation()) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR exact evaluation needs a structurally safe candidate");
    }
    ExactEvaluationReport report;
    report.zero_delta_equivalent = true;
    const auto score_split =
        [&](const Dataset& dataset,
            Metrics& before, Metrics& after) {
            const auto zero =
                analytic_residuals(dataset, Delta{});
            const auto parent_actual =
                actual_residuals(dataset, parent);
            report.maximum_residual_difference =
                std::max(
                    report.maximum_residual_difference,
                    maximum_difference(
                        zero, parent_actual));
            report.zero_delta_equivalent =
                report.zero_delta_equivalent &&
                nested_scores_bit_identical(
                    zero, parent_actual);
            for (std::size_t root = 0;
                 root < dataset.roots.size(); ++root) {
                for (std::size_t action = 0;
                     action <
                     dataset.roots[root]
                         .ranking.actions.size();
                     ++action) {
                    const double base =
                        parent_action_score(
                            dataset.roots[root]
                                .ranking.actions[action]);
                    report.zero_delta_equivalent =
                        report.zero_delta_equivalent &&
                        positive_zero(
                            parent_actual[root][action]) &&
                        equal_bits(
                            base,
                            base +
                                parent_actual[root][action]);
                }
            }
            const auto analytic =
                analytic_residuals(dataset, delta);
            const auto actual =
                actual_residuals(
                    dataset, isolation.candidate);
            report.maximum_residual_difference =
                std::max(
                    report.maximum_residual_difference,
                    maximum_difference(
                        analytic, actual));
            before =
                metrics_from_residuals(
                    dataset, parent_actual);
            after =
                metrics_from_residuals(
                    dataset, actual);
        };
    score_split(
        corpus.train,
        report.parent_train,
        report.candidate_train);
    score_split(
        corpus.dev,
        report.parent_dev,
        report.candidate_dev);
    report.zero_delta_equivalent =
        report.zero_delta_equivalent &&
        learned_model_fingerprint(
            with_learned_priority_head_parameters(
                parent,
                learned_priority_head_parameters(parent))) ==
            learned_model_fingerprint(parent);
    const auto candidate_parameters =
        learned_priority_head_parameters(
            isolation.candidate);
    for (const Dataset* dataset :
         {&corpus.train, &corpus.dev}) {
        for (const Root& root : dataset->roots) {
            const std::vector<double> logits =
                learned_policy_head_logits(
                    root.options,
                    LearnedPolicyDecisionKind::Priority,
                    isolation.candidate);
            for (std::size_t action = 0;
                 action < root.hidden.size();
                 ++action) {
                const Hidden recomputed =
                    hidden_activation(
                        root.options[action],
                        candidate_parameters);
                for (std::size_t hidden = 0;
                     hidden < kHiddenCount; ++hidden) {
                    report.maximum_activation_difference =
                        std::max(
                            report.maximum_activation_difference,
                            std::abs(
                                root.hidden[action][hidden] -
                                recomputed[hidden]));
                }
                report.maximum_logit_difference =
                    std::max(
                        report.maximum_logit_difference,
                        std::abs(
                            dot(
                                root.hidden[action],
                                delta) -
                            logits[action]));
            }
        }
    }
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
    return report;
}

FoldReport evaluate_grouped_oof(
    const Dataset& train,
    std::shared_ptr<const LearnedModel> parent) {
    validate_dataset(train);
    FoldReport report;
    std::vector<std::vector<double>> parent_residuals =
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
              root.schedule_index % kFoldCount]
              [root.schedule_index][root.actor];
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
    for (const auto& root_scores : parent_residuals) {
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
            apply_delta(
                parent, report.fits[fold].delta);
        report.model_isolation_passed =
            report.model_isolation_passed &&
            isolation.passed();
        const auto actual =
            actual_residuals(
                held, isolation.candidate);
        const auto analytic =
            analytic_residuals(
                held, report.fits[fold].delta);
        report.maximum_residual_difference =
            std::max(
                report.maximum_residual_difference,
                maximum_difference(
                    analytic, actual));
        const auto parameters =
            learned_priority_head_parameters(
                isolation.candidate);
        for (std::size_t root_index = 0;
             root_index < held.roots.size();
             ++root_index) {
            const Root& root = held.roots[root_index];
            const std::vector<double> logits =
                learned_policy_head_logits(
                    root.options,
                    LearnedPolicyDecisionKind::Priority,
                    isolation.candidate);
            for (std::size_t action = 0;
                 action < root.options.size();
                 ++action) {
                const Hidden recomputed =
                    hidden_activation(
                        root.options[action],
                        parameters);
                for (std::size_t hidden = 0;
                     hidden < kHiddenCount; ++hidden) {
                    report.maximum_activation_difference =
                        std::max(
                            report.maximum_activation_difference,
                            std::abs(
                                root.hidden[action][hidden] -
                                recomputed[hidden]));
                }
                report.maximum_logit_difference =
                    std::max(
                        report.maximum_logit_difference,
                        std::abs(
                            dot(
                                root.hidden[action],
                                report.fits[fold].delta) -
                            logits[action]));
            }
        }
        const ModelIsolationReport repeated_isolation =
            apply_delta(
                parent,
                report.repeated_fits[fold].delta);
        const auto repeated_actual =
            actual_residuals(
                held,
                repeated_isolation.candidate);
        report.repeated_scores_bit_identical =
            report.repeated_scores_bit_identical &&
            nested_scores_bit_identical(
                actual, repeated_actual) &&
            isolation.candidate_fingerprint ==
                repeated_isolation.candidate_fingerprint;
        std::size_t held_index = 0;
        for (std::size_t root = 0;
             root < train.roots.size(); ++root) {
            if (train.roots[root].schedule_index %
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
    report.parent =
        metrics_from_residuals(
            train, parent_residuals);
    report.candidate =
        metrics_from_residuals(
            train, candidate_residuals);
    return report;
}

OfflineGate evaluate_offline_gate(
    const OfflineGateInputs& inputs) {
    OfflineGate gate;
    const auto& pt = inputs.parent_train;
    const auto& ct = inputs.candidate_train;
    const auto& po = inputs.parent_oof;
    const auto& co = inputs.candidate_oof;
    const auto& pd = inputs.parent_dev;
    const auto& cd = inputs.candidate_dev;
    gate.train_pair_bce_improved =
        ct.pairs.equal_deck_pair_bce <
        pt.pairs.equal_deck_pair_bce;
    gate.train_regret_improved =
        ct.ranking.equal_deck_mean_regret <
        pt.ranking.equal_deck_mean_regret;
    gate.train_listwise_non_increasing =
        metrics_non_increasing(
            ct.ranking
                .equal_deck_listwise_cross_entropy,
            pt.ranking
                .equal_deck_listwise_cross_entropy);
    gate.oof_pair_bce_improved =
        co.pairs.equal_deck_pair_bce <
        po.pairs.equal_deck_pair_bce;
    gate.oof_regret_improved =
        co.ranking.equal_deck_mean_regret <
        po.ranking.equal_deck_mean_regret;
    gate.oof_listwise_non_increasing =
        co.ranking.equal_deck_listwise_cross_entropy <=
        po.ranking.equal_deck_listwise_cross_entropy;
    gate.oof_top_one_non_decreasing =
        co.ranking
                .equal_deck_top_one_expected_agreement >=
        po.ranking
                .equal_deck_top_one_expected_agreement;
    gate.oof_stable_pair_non_decreasing =
        co.ranking
                .equal_deck_stable_pair_agreement >=
        po.ranking
                .equal_deck_stable_pair_agreement;
    gate.dev_pair_bce_improved =
        cd.pairs.equal_deck_pair_bce <
        pd.pairs.equal_deck_pair_bce;
    gate.dev_regret_improved =
        cd.ranking.equal_deck_mean_regret <
        pd.ranking.equal_deck_mean_regret;
    gate.dev_listwise_non_increasing =
        cd.ranking.equal_deck_listwise_cross_entropy <=
        pd.ranking.equal_deck_listwise_cross_entropy;
    gate.dev_top_one_non_decreasing =
        cd.ranking
                .equal_deck_top_one_expected_agreement >=
        pd.ranking
                .equal_deck_top_one_expected_agreement;
    gate.dev_stable_pair_non_decreasing =
        cd.ranking
                .equal_deck_stable_pair_agreement >=
        pd.ranking
                .equal_deck_stable_pair_agreement;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        gate.oof_deck_regret_non_increasing[deck] =
            co.ranking.decks[deck].mean_regret <=
            po.ranking.decks[deck].mean_regret;
        gate.dev_deck_regret_non_increasing[deck] =
            cd.ranking.decks[deck].mean_regret <=
            pd.ranking.decks[deck].mean_regret;
    }
    gate.successor_unchanged =
        inputs.successor_predictions_bit_identical &&
        inputs.successor_metrics_bit_identical;
    gate.invariants_passed =
        inputs.cache_identity_exact &&
        inputs.pair_census_exact &&
        inputs.optimizer_recipe_exact &&
        inputs.grouped_folds_exact &&
        inputs.repeat_fits_bit_identical &&
        inputs.parameter_replay_bit_identical &&
        inputs.zero_delta_equivalent &&
        inputs.actual_model_agreement &&
        inputs.parent_immutable &&
        inputs.model_isolation_passed;
    const auto require =
        [&gate](bool passed, std::string failure) {
            if (!passed) {
                gate.failures.push_back(
                    std::move(failure));
            }
        };
    require(gate.train_pair_bce_improved,
            "TRAIN pair BCE did not improve");
    require(gate.train_regret_improved,
            "TRAIN regret did not improve");
    require(gate.train_listwise_non_increasing,
            "TRAIN listwise CE increased");
    require(gate.oof_pair_bce_improved,
            "OOF pair BCE did not improve");
    require(gate.oof_regret_improved,
            "OOF regret did not improve");
    require(gate.oof_listwise_non_increasing,
            "OOF listwise CE increased");
    require(gate.oof_top_one_non_decreasing,
            "OOF top-one decreased");
    require(gate.oof_stable_pair_non_decreasing,
            "OOF stable-pair decreased");
    require(
        std::all_of(
            gate.oof_deck_regret_non_increasing.begin(),
            gate.oof_deck_regret_non_increasing.end(),
            [](bool value) { return value; }),
        "OOF deck regret increased");
    require(gate.dev_pair_bce_improved,
            "DEV pair BCE did not improve");
    require(gate.dev_regret_improved,
            "DEV regret did not improve");
    require(gate.dev_listwise_non_increasing,
            "DEV listwise CE increased");
    require(gate.dev_top_one_non_decreasing,
            "DEV top-one decreased");
    require(gate.dev_stable_pair_non_decreasing,
            "DEV stable-pair decreased");
    require(
        std::all_of(
            gate.dev_deck_regret_non_increasing.begin(),
            gate.dev_deck_regret_non_increasing.end(),
            [](bool value) { return value; }),
        "DEV deck regret increased");
    require(gate.successor_unchanged,
            "successor critic changed");
    require(gate.invariants_passed,
            "frozen implementation invariant failed");
    return gate;
}

bool selector_config_exact(
    const BotConfig& challenger,
    const BotConfig& baseline,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate) {
    const auto exact =
        [](const BotConfig& bot,
           const std::shared_ptr<const LearnedModel>& model,
           double residual_weight) {
            return bot.kind == BotKind::Learned &&
                   bot.learned_variant ==
                       LearnedVariant::
                           ValueSearchChampion &&
                   bot.rollouts_per_action == 8 &&
                   bot.exploration_rate == 0.0 &&
                   bot.value_continuation_epsilon == 0.0 &&
                   bot.value_priority_residual_weight ==
                       residual_weight &&
                   !bot.value_pass_dominance &&
                   bot.value_resolved_shallow_prior_weight ==
                       0.0 &&
                   !bot.value_adversarial_blocks &&
                   !bot.value_actor_local_search &&
                   !bot.value_recursive_policy_improvement &&
                   bot.value_continuation_controller ==
                       LearnedContinuationController::
                           Legacy &&
                   bot.training_games == 800 &&
                   bot.learned_model == model;
        };
    return parent && candidate &&
           parent != candidate &&
           exact(
               challenger, candidate,
               kResidualWeight) &&
           exact(baseline, parent, 0.0);
}

bool model_scores_bit_identical(
    const Dataset& dataset,
    const std::shared_ptr<const LearnedModel>& first,
    const std::shared_ptr<const LearnedModel>& second) {
    validate_dataset(dataset);
    if (!first || !second) {
        throw std::invalid_argument(
            "DBC4-ACTION-PAIR score comparison model is null");
    }
    for (const Root& root : dataset.roots) {
        const std::vector<double> first_logits =
            learned_policy_head_logits(
                root.options,
                LearnedPolicyDecisionKind::Priority,
                first);
        const std::vector<double> second_logits =
            learned_policy_head_logits(
                root.options,
                LearnedPolicyDecisionKind::Priority,
                second);
        if (!vector_bit_identical(
                first_logits, second_logits)) {
            return false;
        }
    }
    return nested_scores_bit_identical(
        actual_residuals(dataset, first),
        actual_residuals(dataset, second));
}

namespace testing {

Dataset make_dataset(std::vector<Root> roots) {
    Dataset result{
        .roots = std::move(roots),
    };
    for (const Root& root : result.roots) {
        ++result.roots_by_deck[
            deck_index(root.ranking.deck)];
    }
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
    const Dataset& dataset, const Delta& delta) {
    validate_dataset(dataset);
    const ObjectiveResult result =
        objective_and_gradient(
            dataset, delta, OptimizerConfig{});
    return {
        .objective = result.value,
        .gradient = result.gradient,
    };
}

} // namespace testing

} // namespace old_school::decision_boundary_action_pair
