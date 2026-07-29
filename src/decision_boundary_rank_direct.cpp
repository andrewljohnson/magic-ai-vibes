#include "old_school/decision_boundary_rank_direct.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace old_school::decision_boundary_rank_direct {
namespace {

constexpr double kProbabilityFloor = 1.0e-12;
constexpr std::size_t kCalibrationBinCount = 5;

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range(
            "DBC2-RANK-DIRECT deck index is invalid");
    }
    return index;
}

bool finite_probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

bool strict_probability(double value) {
    return std::isfinite(value) &&
           value > 0.0 && value < 1.0;
}

double sigmoid(double value) {
    if (value >= 0.0) {
        const double exponential = std::exp(-value);
        return 1.0 / (1.0 + exponential);
    }
    const double exponential = std::exp(value);
    return exponential / (1.0 + exponential);
}

double logit(double probability) {
    if (!strict_probability(probability)) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT leaf probability must be "
            "strictly inside zero and one");
    }
    return std::log(probability) -
           std::log1p(-probability);
}

double dot(
    std::span<const double> left,
    std::span<const double> right) {
    if (left.size() != right.size()) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT dot-product width drifted");
    }
    double result = 0.0;
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        result += left[index] * right[index];
    }
    if (!std::isfinite(result)) {
        throw std::runtime_error(
            "DBC2-RANK-DIRECT dot product is nonfinite");
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

double clamped_probability(double value) {
    return std::clamp(
        value, kProbabilityFloor,
        1.0 - kProbabilityFloor);
}

std::size_t calibration_bin(double prediction) {
    if (prediction >= 1.0) {
        return kCalibrationBinCount - 1;
    }
    return std::min(
        static_cast<std::size_t>(
            prediction *
            static_cast<double>(
                kCalibrationBinCount)),
        kCalibrationBinCount - 1);
}

double sample_standard_error(
    std::span<const double> samples) {
    if (samples.size() < 2) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT stable pair needs paired "
            "samples");
    }
    const double mean =
        std::accumulate(
            samples.begin(), samples.end(), 0.0) /
        static_cast<double>(samples.size());
    double squared = 0.0;
    for (const double value : samples) {
        const double difference = value - mean;
        squared += difference * difference;
    }
    return std::sqrt(
        squared /
        static_cast<double>(
            samples.size() *
            (samples.size() - 1)));
}

std::vector<double> softmax(
    std::span<const double> values,
    double temperature) {
    if (values.empty() ||
        !std::isfinite(temperature) ||
        temperature <= 0.0) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT softmax input is invalid");
    }
    const double maximum =
        *std::max_element(values.begin(), values.end());
    std::vector<double> result(values.size());
    double total = 0.0;
    for (std::size_t index = 0;
         index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            throw std::invalid_argument(
                "DBC2-RANK-DIRECT softmax value is nonfinite");
        }
        result[index] =
            std::exp(
                (values[index] - maximum) /
                temperature);
        total += result[index];
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::runtime_error(
            "DBC2-RANK-DIRECT softmax normalization failed");
    }
    for (double& value : result) {
        value /= total;
    }
    return result;
}

std::vector<double> mixed_distribution(
    std::span<const double> values,
    double temperature, double mix) {
    if (!std::isfinite(mix) ||
        mix <= 0.0 || mix > 1.0) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT listwise mix is invalid");
    }
    std::vector<double> result =
        softmax(values, temperature);
    const double uniform =
        (1.0 - mix) /
        static_cast<double>(result.size());
    for (double& value : result) {
        value = mix * value + uniform;
    }
    return result;
}

std::vector<std::size_t> exact_max_support(
    std::span<const double> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT max support is empty");
    }
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

double teacher_action_score(const RankAction& action) {
    double total = 0.0;
    for (const RankCell& cell : action.worlds) {
        total += cell.teacher_target;
    }
    return total /
           static_cast<double>(action.worlds.size());
}

double candidate_action_score(
    const RankAction& action,
    std::span<const double> delta) {
    double total = 0.0;
    for (const RankCell& cell : action.worlds) {
        total += candidate_cell_value(cell, delta);
    }
    return total /
           static_cast<double>(action.worlds.size());
}

struct RootScores {
    std::vector<double> teacher;
    std::vector<double> candidate;
    std::vector<double> teacher_distribution;
    std::vector<double> candidate_softmax;
    std::vector<double> candidate_distribution;
};

RootScores root_scores(
    const RankRoot& root,
    std::span<const double> delta,
    double temperature, double mix) {
    RootScores result;
    result.teacher.reserve(root.actions.size());
    result.candidate.reserve(root.actions.size());
    for (const RankAction& action : root.actions) {
        result.teacher.push_back(
            teacher_action_score(action));
        result.candidate.push_back(
            candidate_action_score(action, delta));
    }
    result.teacher_distribution =
        mixed_distribution(
            result.teacher, temperature, mix);
    result.candidate_softmax =
        softmax(result.candidate, temperature);
    const double uniform =
        (1.0 - mix) /
        static_cast<double>(
            result.candidate_softmax.size());
    result.candidate_distribution =
        result.candidate_softmax;
    for (double& value :
         result.candidate_distribution) {
        value = mix * value + uniform;
    }
    return result;
}

double cross_entropy(const RootScores& scores) {
    if (scores.teacher_distribution.size() !=
            scores.candidate_distribution.size()) {
        throw std::logic_error(
            "DBC2-RANK-DIRECT distributions are misaligned");
    }
    double result = 0.0;
    for (std::size_t action = 0;
         action < scores.teacher_distribution.size();
         ++action) {
        result -=
            scores.teacher_distribution[action] *
            std::log(
                scores.candidate_distribution[action]);
    }
    return result;
}

struct ObjectiveResult {
    double value = 0.0;
    std::vector<double> gradient;
};

ObjectiveResult objective_and_gradient(
    const Dataset& dataset,
    std::span<const double> delta,
    const OptimizerConfig& config) {
    ObjectiveResult result{
        .gradient =
            std::vector<double>(kFeatureCount, 0.0),
    };
    const double root_weight =
        1.0 /
        static_cast<double>(dataset.roots.size());
    for (const RankRoot& root : dataset.roots) {
        const RootScores scores =
            root_scores(
                root, delta,
                config.temperature, config.mix);
        result.value +=
            root_weight * cross_entropy(scores);

        double target_softmax_ratio_sum = 0.0;
        for (std::size_t action = 0;
             action < root.actions.size(); ++action) {
            target_softmax_ratio_sum +=
                scores.teacher_distribution[action] *
                scores.candidate_softmax[action] /
                scores.candidate_distribution[action];
        }
        for (std::size_t action = 0;
             action < root.actions.size(); ++action) {
            const double derivative_by_action_score =
                root_weight *
                config.mix *
                scores.candidate_softmax[action] *
                (target_softmax_ratio_sum -
                 scores.teacher_distribution[action] /
                     scores.candidate_distribution[action]) /
                config.temperature;
            const RankAction& rank_action =
                root.actions[action];
            const double inverse_worlds =
                1.0 /
                static_cast<double>(
                    rank_action.worlds.size());
            for (const RankCell& cell :
                 rank_action.worlds) {
                if (cell.terminal_before_boundary) {
                    continue;
                }
                const double shift =
                    dot(cell.observation, delta);
                double sigmoid_derivative = 0.0;
                for (const double parent :
                     cell.parent_leaf_values) {
                    const double value =
                        sigmoid(logit(parent) + shift);
                    sigmoid_derivative +=
                        value * (1.0 - value) /
                        static_cast<double>(kLeafCount);
                }
                const double multiplier =
                    derivative_by_action_score *
                    inverse_worlds *
                    sigmoid_derivative;
                for (std::size_t feature = 0;
                     feature < kFeatureCount; ++feature) {
                    if (cell.observation[feature] != 0.0) {
                        result.gradient[feature] +=
                            multiplier *
                            cell.observation[feature];
                    }
                }
            }
        }
    }
    for (std::size_t feature = 0;
         feature < kFeatureCount; ++feature) {
        result.value +=
            0.5 * config.l2_tether *
            delta[feature] * delta[feature];
        result.gradient[feature] +=
            config.l2_tether * delta[feature];
    }
    if (!std::isfinite(result.value) ||
        !std::all_of(
            result.gradient.begin(),
            result.gradient.end(),
            [](double value) {
                return std::isfinite(value);
            })) {
        throw std::runtime_error(
            "DBC2-RANK-DIRECT objective is nonfinite");
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
        config.temperature !=
            kListwiseTemperature ||
        config.mix != kListwiseMix ||
        config.l2_tether != kL2Tether ||
        config.global_gradient_norm_clip !=
            kGlobalGradientNormClip) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT requires the fixed optimizer "
            "recipe");
    }
}

Dataset project_split(
    std::span<const dbc::RootExample> examples,
    std::shared_ptr<const LearnedModel> parent) {
    Dataset result;
    result.roots.reserve(examples.size());
    for (const dbc::RootExample& example : examples) {
        RankRoot root{
            .stable_root_id =
                example.manifest.stable_root_id,
            .deck =
                example.manifest.coordinate.owner_deck(),
        };
        root.actions.resize(
            example.teacher_samples.size());
        for (const dbc::BoundaryCell& source_cell :
             example.cells) {
            RankCell cell{
                .observation = source_cell.observation,
                .teacher_target =
                    source_cell.teacher_target,
                .terminal_before_boundary =
                    source_cell
                        .terminal_before_boundary,
            };
            if (cell.terminal_before_boundary) {
                cell.parent_leaf_values = {
                    cell.teacher_target,
                    cell.teacher_target,
                };
            } else {
                cell.parent_leaf_values =
                    learned_critic_observation_leaf_values(
                        cell.observation, parent);
                const double mean =
                    std::accumulate(
                        cell.parent_leaf_values.begin(),
                        cell.parent_leaf_values.end(), 0.0) /
                    static_cast<double>(kLeafCount);
                if (mean !=
                    source_cell.parent_prediction) {
                    throw std::runtime_error(
                        "DBC2-RANK-DIRECT cached parent "
                        "prediction drifted");
                }
            }
            if (source_cell.action_index >=
                root.actions.size()) {
                throw std::invalid_argument(
                    "DBC2-RANK-DIRECT source action index "
                    "is invalid");
            }
            root.actions[source_cell.action_index]
                .worlds.push_back(std::move(cell));
        }
        ++result.roots_by_deck[
            deck_index(root.deck)];
        result.roots.push_back(std::move(root));
    }
    validate_dataset(result);
    return result;
}

} // namespace

bool ModelIsolationReport::passed() const {
    return candidate &&
           parent_immutable &&
           repeated_application_bit_identical &&
           critic_changed &&
           input_hidden_frozen &&
           output_layer_frozen &&
           context_direct_path_frozen &&
           direct_path_changed &&
           shared_delta_exact &&
           changed_coordinates > 0 &&
           changed_coordinates <= kFeatureCount &&
           all_policy_heads_frozen;
}

bool OfflineGate::passed() const {
    return failures.empty();
}

Corpus project_corpus(
    const dbc::Corpus& source,
    std::shared_ptr<const LearnedModel> parent) {
    dbc::validate_corpus(source);
    if (!parent ||
        learned_model_fingerprint(parent) !=
            source.census.parent_fingerprint ||
        learned_model_component_fingerprints(parent) !=
            source.parent_components) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT parent differs from source "
            "corpus");
    }
    Corpus result{
        .train =
            project_split(source.train, parent),
        .dev =
            project_split(source.dev, parent),
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
            "DBC2-RANK-DIRECT dataset is empty");
    }
    std::array<std::size_t, kDeckCount> observed{};
    std::set<std::string> root_ids;
    for (const RankRoot& root : dataset.roots) {
        const std::size_t deck =
            deck_index(root.deck);
        ++observed[deck];
        if (root.stable_root_id.empty() ||
            !root_ids.insert(
                root.stable_root_id).second ||
            root.actions.size() < 2) {
            throw std::invalid_argument(
                "DBC2-RANK-DIRECT root identity or action "
                "shape is invalid");
        }
        std::size_t worlds = 0;
        for (const RankAction& action : root.actions) {
            if (action.worlds.empty()) {
                throw std::invalid_argument(
                    "DBC2-RANK-DIRECT action has no worlds");
            }
            if (worlds == 0) {
                worlds = action.worlds.size();
            } else if (worlds != action.worlds.size()) {
                throw std::invalid_argument(
                    "DBC2-RANK-DIRECT action world counts "
                    "differ within a root");
            }
            for (const RankCell& cell : action.worlds) {
                if (!finite_probability(
                        cell.teacher_target)) {
                    throw std::invalid_argument(
                        "DBC2-RANK-DIRECT teacher target is "
                        "invalid");
                }
                if (cell.terminal_before_boundary) {
                    if (!cell.observation.empty() ||
                        cell.parent_leaf_values !=
                            std::array<double, kLeafCount>{
                                cell.teacher_target,
                                cell.teacher_target}) {
                        throw std::invalid_argument(
                            "DBC2-RANK-DIRECT terminal cell "
                            "retained critic input");
                    }
                } else if (
                    cell.observation.size() !=
                        kFeatureCount ||
                    !std::all_of(
                        cell.observation.begin(),
                        cell.observation.end(),
                        [](double value) {
                            return std::isfinite(value);
                        }) ||
                    !std::all_of(
                        cell.parent_leaf_values.begin(),
                        cell.parent_leaf_values.end(),
                        strict_probability)) {
                    throw std::invalid_argument(
                        "DBC2-RANK-DIRECT successor cell is "
                        "invalid");
                }
            }
        }
    }
    if (observed != dataset.roots_by_deck ||
        observed.front() == 0 ||
        !std::all_of(
            observed.begin(), observed.end(),
            [&](std::size_t count) {
                return count == observed.front();
            })) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT roots are not equally "
            "deck-balanced");
    }
}

void validate_corpus(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    validate_dataset(corpus.train);
    validate_dataset(corpus.dev);
    if (corpus.source_digest.size() != 64 ||
        corpus.source_digest !=
            dbc::kFrozenCorpusDigest ||
        !parent ||
        learned_model_component_fingerprints(parent) !=
            corpus.parent_components) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT corpus identity is invalid");
    }
}

double candidate_cell_value(
    const RankCell& cell,
    std::span<const double> delta) {
    if (delta.size() != kFeatureCount ||
        !std::all_of(
            delta.begin(), delta.end(),
            [](double value) {
                return std::isfinite(value);
            })) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT delta is invalid");
    }
    if (cell.terminal_before_boundary) {
        return cell.teacher_target;
    }
    const double shift =
        dot(cell.observation, delta);
    if (shift == 0.0) {
        return
            (cell.parent_leaf_values[0] +
             cell.parent_leaf_values[1]) /
            static_cast<double>(kLeafCount);
    }
    double total = 0.0;
    for (const double parent :
         cell.parent_leaf_values) {
        total += sigmoid(logit(parent) + shift);
    }
    return total /
           static_cast<double>(kLeafCount);
}

Metrics evaluate(
    const Dataset& dataset,
    std::span<const double> delta,
    double temperature, double mix) {
    validate_dataset(dataset);
    if (delta.size() != kFeatureCount) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT evaluation delta width "
            "drifted");
    }
    struct Accumulator {
        std::size_t roots = 0;
        std::size_t eligible_cells = 0;
        std::size_t stable_pairs = 0;
        double successor_bce_sum = 0.0;
        double successor_brier_sum = 0.0;
        double successor_bias_sum = 0.0;
        std::array<double, kCalibrationBinCount>
            bin_weights{};
        std::array<double, kCalibrationBinCount>
            bin_prediction_sums{};
        std::array<double, kCalibrationBinCount>
            bin_target_sums{};
        double cross_entropy_sum = 0.0;
        double agreement_sum = 0.0;
        double stable_pair_sum = 0.0;
        double regret_sum = 0.0;
    };
    std::array<Accumulator, kDeckCount> accumulators{};
    for (const RankRoot& root : dataset.roots) {
        const RootScores scores =
            root_scores(
                root, delta, temperature, mix);
        Accumulator& accumulator =
            accumulators[deck_index(root.deck)];
        ++accumulator.roots;
        accumulator.cross_entropy_sum +=
            cross_entropy(scores);
        double root_bce = 0.0;
        double root_brier = 0.0;
        double root_bias = 0.0;
        std::size_t root_eligible = 0;
        for (const RankAction& action :
             root.actions) {
            root_eligible +=
                static_cast<std::size_t>(
                    std::count_if(
                        action.worlds.begin(),
                        action.worlds.end(),
                        [](const RankCell& cell) {
                            return !cell
                                        .terminal_before_boundary;
                        }));
        }
        if (root_eligible == 0) {
            throw std::invalid_argument(
                "DBC2-RANK-DIRECT root has no successor "
                "critic cells");
        }
        const double cell_weight =
            1.0 /
            static_cast<double>(root_eligible);
        for (std::size_t action = 0;
             action < root.actions.size(); ++action) {
            for (const RankCell& cell :
                 root.actions[action].worlds) {
                if (cell.terminal_before_boundary) {
                    continue;
                }
                const double prediction =
                    candidate_cell_value(cell, delta);
                const double clamped =
                    clamped_probability(prediction);
                root_bce +=
                    -cell.teacher_target *
                        std::log(clamped) -
                    (1.0 - cell.teacher_target) *
                        std::log(1.0 - clamped);
                const double error =
                    prediction - cell.teacher_target;
                root_brier += error * error;
                root_bias += error;
                const std::size_t bin =
                    calibration_bin(prediction);
                accumulator.bin_weights[bin] +=
                    cell_weight;
                accumulator.bin_prediction_sums[bin] +=
                    cell_weight * prediction;
                accumulator.bin_target_sums[bin] +=
                    cell_weight * cell.teacher_target;
            }
        }
        accumulator.eligible_cells += root_eligible;
        accumulator.successor_bce_sum +=
            root_bce * cell_weight;
        accumulator.successor_brier_sum +=
            root_brier * cell_weight;
        accumulator.successor_bias_sum +=
            root_bias * cell_weight;
        const auto teacher_support =
            exact_max_support(scores.teacher);
        const auto candidate_support =
            exact_max_support(scores.candidate);
        std::size_t overlap = 0;
        double selected_teacher = 0.0;
        for (const std::size_t action :
             candidate_support) {
            selected_teacher +=
                scores.teacher[action];
            overlap +=
                std::find(
                    teacher_support.begin(),
                    teacher_support.end(),
                    action) !=
                        teacher_support.end()
                    ? 1U
                    : 0U;
        }
        selected_teacher /=
            static_cast<double>(
                candidate_support.size());
        accumulator.agreement_sum +=
            static_cast<double>(overlap) /
            static_cast<double>(
                candidate_support.size());
        accumulator.regret_sum +=
            scores.teacher[
                teacher_support.front()] -
            selected_teacher;

        for (std::size_t first = 0;
             first < root.actions.size(); ++first) {
            for (std::size_t second = first + 1;
                 second < root.actions.size(); ++second) {
                std::vector<double> differences;
                differences.reserve(
                    root.actions[first].worlds.size());
                for (std::size_t world = 0;
                     world <
                     root.actions[first].worlds.size();
                     ++world) {
                    differences.push_back(
                        root.actions[first]
                            .worlds[world]
                            .teacher_target -
                        root.actions[second]
                            .worlds[world]
                            .teacher_target);
                }
                const double teacher_delta =
                    scores.teacher[first] -
                    scores.teacher[second];
                const double uncertainty =
                    kStablePairNormal95CriticalValue *
                    sample_standard_error(differences);
                if (std::abs(teacher_delta) <
                        kStablePairMinimumDelta ||
                    std::abs(teacher_delta) <=
                        uncertainty) {
                    continue;
                }
                ++accumulator.stable_pairs;
                const double candidate_delta =
                    scores.candidate[first] -
                    scores.candidate[second];
                if (candidate_delta == 0.0) {
                    accumulator.stable_pair_sum += 0.5;
                } else if (
                    (candidate_delta > 0.0) ==
                    (teacher_delta > 0.0)) {
                    accumulator.stable_pair_sum += 1.0;
                }
            }
        }
    }

    Metrics result;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const Accumulator& accumulator =
            accumulators[deck];
        if (accumulator.roots == 0) {
            throw std::logic_error(
                "DBC2-RANK-DIRECT metric deck is empty");
        }
        DeckMetrics row{
            .deck = static_cast<DeckId>(deck),
            .roots = accumulator.roots,
            .eligible_cells =
                accumulator.eligible_cells,
            .stable_pairs =
                accumulator.stable_pairs,
            .successor_bce =
                accumulator.successor_bce_sum /
                static_cast<double>(
                    accumulator.roots),
            .successor_brier =
                accumulator.successor_brier_sum /
                static_cast<double>(
                    accumulator.roots),
            .successor_bias =
                accumulator.successor_bias_sum /
                static_cast<double>(
                    accumulator.roots),
            .listwise_cross_entropy =
                accumulator.cross_entropy_sum /
                static_cast<double>(
                    accumulator.roots),
            .top_one_expected_agreement =
                accumulator.agreement_sum /
                static_cast<double>(
                    accumulator.roots),
            .stable_pair_agreement =
                accumulator.stable_pairs == 0
                    ? 0.0
                    : accumulator.stable_pair_sum /
                          static_cast<double>(
                              accumulator.stable_pairs),
            .mean_regret =
                accumulator.regret_sum /
                static_cast<double>(
                    accumulator.roots),
        };
        for (std::size_t bin = 0;
             bin < kCalibrationBinCount; ++bin) {
            if (accumulator.bin_weights[bin] == 0.0) {
                continue;
            }
            const double prediction =
                accumulator.bin_prediction_sums[bin] /
                accumulator.bin_weights[bin];
            const double target =
                accumulator.bin_target_sums[bin] /
                accumulator.bin_weights[bin];
            row.successor_ece +=
                accumulator.bin_weights[bin] /
                static_cast<double>(
                    accumulator.roots) *
                std::abs(prediction - target);
        }
        result.decks[deck] = row;
        result.roots += row.roots;
        result.eligible_cells += row.eligible_cells;
        result.stable_pairs += row.stable_pairs;
        result.equal_deck_successor_bce +=
            row.successor_bce /
            static_cast<double>(kDeckCount);
        result.equal_deck_successor_brier +=
            row.successor_brier /
            static_cast<double>(kDeckCount);
        result.equal_deck_successor_bias +=
            row.successor_bias /
            static_cast<double>(kDeckCount);
        result.equal_deck_successor_ece +=
            row.successor_ece /
            static_cast<double>(kDeckCount);
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

Metrics evaluate_model(
    const Dataset& dataset,
    std::shared_ptr<const LearnedModel> model,
    double temperature, double mix) {
    if (!model) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT exact scoring requires a "
            "model");
    }
    Dataset scored = dataset;
    for (RankRoot& root : scored.roots) {
        for (RankAction& action : root.actions) {
            for (RankCell& cell : action.worlds) {
                if (cell.terminal_before_boundary) {
                    continue;
                }
                const double exact =
                    learned_critic_observation_value(
                        cell.observation, model);
                cell.parent_leaf_values = {
                    exact, exact};
            }
        }
    }
    return evaluate(
        scored,
        std::vector<double>(
            kFeatureCount, 0.0),
        temperature, mix);
}

ExactEvaluationReport evaluate_exact(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    std::span<const double> delta) {
    validate_corpus(corpus, parent);
    if (!candidate ||
        delta.size() != kFeatureCount) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT exact evaluation input is "
            "invalid");
    }
    ExactEvaluationReport report{
        .parent_fingerprint =
            learned_model_fingerprint(parent),
        .candidate_fingerprint =
            learned_model_fingerprint(candidate),
        .parent_train =
            evaluate_model(corpus.train, parent),
        .candidate_train =
            evaluate_model(corpus.train, candidate),
        .parent_dev =
            evaluate_model(corpus.dev, parent),
        .candidate_dev =
            evaluate_model(corpus.dev, candidate),
    };
    const auto compare_split =
        [&](const Dataset& dataset) {
            for (const RankRoot& root : dataset.roots) {
                for (const RankAction& action :
                     root.actions) {
                    for (const RankCell& cell :
                         action.worlds) {
                        if (cell
                                .terminal_before_boundary) {
                            continue;
                        }
                        const double surrogate =
                            candidate_cell_value(
                                cell, delta);
                        const double engine =
                            learned_critic_observation_value(
                                cell.observation,
                                candidate);
                        report
                            .maximum_surrogate_engine_cell_difference =
                            std::max(
                                report
                                    .maximum_surrogate_engine_cell_difference,
                                std::abs(
                                    surrogate - engine));
                    }
                }
            }
        };
    compare_split(corpus.train);
    compare_split(corpus.dev);
    return report;
}

OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config) {
    validate_dataset(train);
    validate_optimizer_config(config);
    std::vector<double> delta(kFeatureCount, 0.0);
    std::vector<double> first_moment(
        kFeatureCount, 0.0);
    std::vector<double> second_moment(
        kFeatureCount, 0.0);
    const ObjectiveResult initial =
        objective_and_gradient(
            train, delta, config);
    double beta_one_power = 1.0;
    double beta_two_power = 1.0;
    ObjectiveResult current = initial;
    double maximum_preclip_gradient_l2_norm = 0.0;
    std::size_t clipped_steps = 0;
    for (std::size_t step = 1;
         step <= config.steps; ++step) {
        beta_one_power *= config.beta_one;
        beta_two_power *= config.beta_two;
        const double raw_gradient_norm =
            l2_norm(current.gradient);
        maximum_preclip_gradient_l2_norm =
            std::max(
                maximum_preclip_gradient_l2_norm,
                raw_gradient_norm);
        const bool clipped =
            raw_gradient_norm >
            config.global_gradient_norm_clip;
        clipped_steps += clipped ? 1U : 0U;
        const double gradient_scale =
            clipped
                ? config.global_gradient_norm_clip /
                      raw_gradient_norm
                : 1.0;
        for (std::size_t feature = 0;
             feature < kFeatureCount; ++feature) {
            const double gradient =
                current.gradient[feature] *
                gradient_scale;
            first_moment[feature] =
                config.beta_one *
                    first_moment[feature] +
                (1.0 - config.beta_one) *
                    gradient;
            second_moment[feature] =
                config.beta_two *
                    second_moment[feature] +
                (1.0 - config.beta_two) *
                    gradient * gradient;
            const double corrected_first =
                first_moment[feature] /
                (1.0 - beta_one_power);
            const double corrected_second =
                second_moment[feature] /
                (1.0 - beta_two_power);
            delta[feature] -=
                config.learning_rate *
                corrected_first /
                (std::sqrt(corrected_second) +
                 config.epsilon);
        }
        current =
            objective_and_gradient(
                train, delta, config);
    }
    return {
        .config = config,
        .delta = delta,
        .completed_steps = config.steps,
        .initial_objective = initial.value,
        .final_objective = current.value,
        .delta_l2_norm = l2_norm(delta),
        .final_gradient_l2_norm =
            l2_norm(current.gradient),
        .maximum_preclip_gradient_l2_norm =
            maximum_preclip_gradient_l2_norm,
        .clipped_steps = clipped_steps,
        .before =
            evaluate(
                train,
                std::vector<double>(
                    kFeatureCount, 0.0),
                config.temperature, config.mix),
        .after =
            evaluate(
                train, delta,
                config.temperature, config.mix),
    };
}

ModelIsolationReport apply_delta(
    std::shared_ptr<const LearnedModel> parent,
    std::span<const double> delta) {
    if (!parent ||
        delta.size() != kFeatureCount ||
        !std::all_of(
            delta.begin(), delta.end(),
            [](double value) {
                return std::isfinite(value);
            }) ||
        std::none_of(
            delta.begin(), delta.end(),
            [](double value) {
                return value != 0.0;
            })) {
        throw std::invalid_argument(
            "DBC2-RANK-DIRECT model application input is "
            "invalid");
    }
    ModelIsolationReport result{
        .parent_fingerprint_before =
            learned_model_fingerprint(parent),
        .parent_components =
            learned_model_component_fingerprints(parent),
        .parent_critic_tensors =
            learned_critic_tensor_fingerprints(parent),
    };
    result.candidate =
        with_learned_shared_critic_direct_delta(
            parent, delta);
    const auto repeated =
        with_learned_shared_critic_direct_delta(
            parent, delta);
    if (!result.candidate || !repeated) {
        throw std::runtime_error(
            "DBC2-RANK-DIRECT engine returned no candidate");
    }
    result.parent_fingerprint_after =
        learned_model_fingerprint(parent);
    result.candidate_fingerprint =
        learned_model_fingerprint(result.candidate);
    result.repeated_candidate_fingerprint =
        learned_model_fingerprint(repeated);
    result.candidate_components =
        learned_model_component_fingerprints(
            result.candidate);
    result.candidate_critic_tensors =
        learned_critic_tensor_fingerprints(
            result.candidate);
    const auto parent_direct =
        learned_critic_direct_path_parameters(parent);
    const auto candidate_direct =
        learned_critic_direct_path_parameters(
            result.candidate);
    const auto repeated_direct =
        learned_critic_direct_path_parameters(repeated);
    const auto parent_context_direct =
        learned_critic_context_direct_path_parameters(
            parent);
    const auto candidate_context_direct =
        learned_critic_context_direct_path_parameters(
            result.candidate);
    const auto repeated_context_direct =
        learned_critic_context_direct_path_parameters(
            repeated);
    result.parent_immutable =
        result.parent_fingerprint_before ==
        result.parent_fingerprint_after;
    result.repeated_application_bit_identical =
        result.candidate_fingerprint ==
        result.repeated_candidate_fingerprint;
    result.critic_changed =
        result.parent_components.critic !=
        result.candidate_components.critic;
    result.input_hidden_frozen =
        result.parent_critic_tensors.input_hidden ==
        result.candidate_critic_tensors.input_hidden;
    result.output_layer_frozen =
        result.parent_critic_tensors.output_layer ==
        result.candidate_critic_tensors.output_layer;
    result.context_direct_path_frozen =
        parent_context_direct ==
            candidate_context_direct &&
        candidate_context_direct ==
            repeated_context_direct;
    result.direct_path_changed =
        result.parent_critic_tensors.direct_paths !=
        result.candidate_critic_tensors.direct_paths;
    result.shared_delta_exact =
        candidate_direct == repeated_direct;
    for (std::size_t leaf = 0;
         leaf < kLeafCount &&
         result.shared_delta_exact; ++leaf) {
        for (std::size_t feature = 0;
             feature < kFeatureCount; ++feature) {
            if (candidate_direct.leaves[leaf][feature] !=
                parent_direct.leaves[leaf][feature] +
                    delta[feature]) {
                result.shared_delta_exact = false;
                break;
            }
        }
    }
    for (std::size_t feature = 0;
         feature < kFeatureCount; ++feature) {
        bool changed = false;
        for (std::size_t leaf = 0;
             leaf < kLeafCount; ++leaf) {
            changed =
                changed ||
                candidate_direct.leaves[leaf][feature] !=
                    parent_direct.leaves[leaf][feature];
        }
        result.changed_coordinates +=
            changed ? 1U : 0U;
    }
    result.all_policy_heads_frozen =
        result.parent_components.priority ==
            result.candidate_components.priority &&
        result.parent_components.attack ==
            result.candidate_components.attack &&
        result.parent_components.block ==
            result.candidate_components.block &&
        result.parent_components.damage_order ==
            result.candidate_components.damage_order;
    return result;
}

OfflineGate evaluate_offline_gate(
    const OptimizerReport& fit,
    const OptimizerReport& repeated_fit,
    const ExactEvaluationReport& exact,
    const ModelIsolationReport& isolation) {
    return evaluate_offline_gate(
        OfflineGateInputs{
            .repeated_optimizer_bit_identical =
                fit == repeated_fit,
            .optimizer_recipe_exact =
                fit.config == OptimizerConfig{} &&
                fit.completed_steps == kAdamSteps &&
                fit.delta.size() == kFeatureCount,
            .objective_strictly_improved =
                fit.final_objective <
                fit.initial_objective,
            .surrogate_engine_agreement =
                exact
                        .maximum_surrogate_engine_cell_difference <=
                1.0e-12,
            .exact_model_identity =
                exact.parent_fingerprint ==
                    isolation.parent_fingerprint_before &&
                exact.candidate_fingerprint ==
                    isolation.candidate_fingerprint,
            .model_isolation_passed =
                isolation.passed(),
            .parent_train = exact.parent_train,
            .candidate_train = exact.candidate_train,
            .parent_dev = exact.parent_dev,
            .candidate_dev = exact.candidate_dev,
        },
        "shared direct-path model isolation failed");
}

OfflineGate evaluate_offline_gate(
    const OfflineGateInputs& inputs,
    std::string_view isolation_failure) {
    OfflineGate gate{
        .repeated_optimizer_bit_identical =
            inputs.repeated_optimizer_bit_identical,
        .optimizer_recipe_exact =
            inputs.optimizer_recipe_exact,
        .objective_strictly_improved =
            inputs.objective_strictly_improved,
        .surrogate_engine_agreement =
            inputs.surrogate_engine_agreement,
        .exact_model_identity =
            inputs.exact_model_identity,
        .model_isolation_passed =
            inputs.model_isolation_passed,
        .train_listwise_strictly_improved =
            inputs.candidate_train
                    .equal_deck_listwise_cross_entropy <
            inputs.parent_train
                    .equal_deck_listwise_cross_entropy,
        .train_regret_strictly_improved =
            inputs.candidate_train
                    .equal_deck_mean_regret <
            inputs.parent_train
                    .equal_deck_mean_regret,
        .dev_listwise_strictly_improved =
            inputs.candidate_dev
                    .equal_deck_listwise_cross_entropy <
            inputs.parent_dev
                    .equal_deck_listwise_cross_entropy,
        .dev_regret_strictly_improved =
            inputs.candidate_dev
                    .equal_deck_mean_regret <
            inputs.parent_dev
                    .equal_deck_mean_regret,
        .dev_top_one_non_decreasing =
            inputs.candidate_dev
                    .equal_deck_top_one_expected_agreement >=
            inputs.parent_dev
                    .equal_deck_top_one_expected_agreement,
        .dev_stable_pair_non_decreasing =
            inputs.candidate_dev
                    .equal_deck_stable_pair_agreement >=
            inputs.parent_dev
                    .equal_deck_stable_pair_agreement,
        .dev_successor_bce_guard =
            inputs.candidate_dev
                    .equal_deck_successor_bce <=
            inputs.parent_dev
                    .equal_deck_successor_bce +
                kMaximumDevSuccessorBceIncrease,
    };
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        gate.dev_deck_regret_guard[deck] =
            inputs.candidate_dev
                    .decks[deck].mean_regret <=
            inputs.parent_dev.decks[deck].mean_regret +
                kMaximumDevDeckRegretIncrease;
    }
    const auto fail =
        [&](bool condition, std::string message) {
            if (!condition) {
                gate.failures.push_back(
                    std::move(message));
            }
        };
    fail(
        gate.repeated_optimizer_bit_identical,
        "repeated optimizer was not bit-identical");
    fail(
        gate.optimizer_recipe_exact,
        "optimizer recipe or accounting drifted");
    fail(
        gate.objective_strictly_improved,
        "regularized TRAIN objective did not improve");
    fail(
        gate.surrogate_engine_agreement,
        "surrogate and engine candidate values diverged");
    fail(
        gate.exact_model_identity,
        "exact metrics and isolation model identities differ");
    fail(
        gate.model_isolation_passed,
        std::string(isolation_failure));
    fail(
        gate.train_listwise_strictly_improved,
        "TRAIN listwise cross-entropy did not improve");
    fail(
        gate.train_regret_strictly_improved,
        "TRAIN teacher regret did not improve");
    fail(
        gate.dev_listwise_strictly_improved,
        "DEV listwise cross-entropy did not improve");
    fail(
        gate.dev_regret_strictly_improved,
        "DEV teacher regret did not improve");
    fail(
        gate.dev_top_one_non_decreasing,
        "DEV top-one agreement decreased");
    fail(
        gate.dev_stable_pair_non_decreasing,
        "DEV stable-pair agreement decreased");
    fail(
        gate.dev_successor_bce_guard,
        "DEV successor BCE exceeded its guard");
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        fail(
            gate.dev_deck_regret_guard[deck],
            "DEV deck regret guard failed for " +
                std::string(
                    deck_name(
                        static_cast<DeckId>(deck))));
    }
    return gate;
}

namespace testing {

Dataset make_dataset(
    std::vector<RankRoot> roots) {
    Dataset result{.roots = std::move(roots)};
    for (const RankRoot& root : result.roots) {
        ++result.roots_by_deck[
            deck_index(root.deck)];
    }
    validate_dataset(result);
    return result;
}

Corpus make_corpus(
    Dataset train, Dataset dev,
    LearnedModelComponentFingerprints parent_components) {
    Corpus result{
        .train = std::move(train),
        .dev = std::move(dev),
        .source_digest =
            std::string(dbc::kFrozenCorpusDigest),
        .parent_components =
            std::move(parent_components),
    };
    return result;
}

} // namespace testing

} // namespace old_school::decision_boundary_rank_direct
