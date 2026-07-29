#include "old_school/decision_boundary_rank_hidden.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace old_school::decision_boundary_rank_hidden {
namespace {

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range(
            "DBC3-RANK-HIDDEN deck index is invalid");
    }
    return index;
}

bool strict_probability(double value) {
    return std::isfinite(value) &&
           value > 0.0 && value < 1.0;
}

bool finite_delta(const Delta& delta) {
    return std::all_of(
        delta.begin(), delta.end(),
        [](const auto& leaf) {
            return std::all_of(
                leaf.begin(), leaf.end(),
                [](double value) {
                    return std::isfinite(value);
                });
        });
}

bool zero_delta(const Delta& delta) {
    return std::all_of(
        delta.begin(), delta.end(),
        [](const auto& leaf) {
            return std::all_of(
                leaf.begin(), leaf.end(),
                [](double value) {
                    return value == 0.0;
                });
        });
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
            "DBC3-RANK-HIDDEN leaf probability must be "
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
            "DBC3-RANK-HIDDEN dot-product width drifted");
    }
    double result = 0.0;
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        result += left[index] * right[index];
    }
    if (!std::isfinite(result)) {
        throw std::runtime_error(
            "DBC3-RANK-HIDDEN dot product is nonfinite");
    }
    return result;
}

double l2_norm(const Delta& values) {
    double squared = 0.0;
    for (const auto& leaf : values) {
        for (const double value : leaf) {
            squared += value * value;
        }
    }
    return std::sqrt(squared);
}

std::vector<double> softmax(
    std::span<const double> values,
    double temperature) {
    if (values.empty() ||
        !std::isfinite(temperature) ||
        temperature <= 0.0) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN softmax input is invalid");
    }
    const double maximum =
        *std::max_element(values.begin(), values.end());
    std::vector<double> result(values.size());
    double total = 0.0;
    for (std::size_t index = 0;
         index < values.size(); ++index) {
        if (!std::isfinite(values[index])) {
            throw std::invalid_argument(
                "DBC3-RANK-HIDDEN softmax value is nonfinite");
        }
        result[index] =
            std::exp(
                (values[index] - maximum) /
                temperature);
        total += result[index];
    }
    if (!std::isfinite(total) || total <= 0.0) {
        throw std::runtime_error(
            "DBC3-RANK-HIDDEN softmax normalization failed");
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
            "DBC3-RANK-HIDDEN listwise mix is invalid");
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

double teacher_action_score(
    const RankHiddenAction& action) {
    double total = 0.0;
    for (const RankHiddenCell& cell : action.worlds) {
        total += cell.source.teacher_target;
    }
    return total /
           static_cast<double>(action.worlds.size());
}

double candidate_cell_value_unchecked(
    const RankHiddenCell& cell,
    const Delta& delta,
    bool delta_is_zero) {
    if (cell.source.terminal_before_boundary) {
        return cell.source.teacher_target;
    }
    if (delta_is_zero) {
        return
            (cell.source.parent_leaf_values[0] +
             cell.source.parent_leaf_values[1]) /
            static_cast<double>(kLeafCount);
    }
    double total = 0.0;
    for (std::size_t leaf = 0;
         leaf < kLeafCount; ++leaf) {
        total +=
            sigmoid(
                logit(
                    cell.source
                        .parent_leaf_values[leaf]) +
                dot(delta[leaf], cell.hidden[leaf]));
    }
    return total /
           static_cast<double>(kLeafCount);
}

double candidate_action_score(
    const RankHiddenAction& action,
    const Delta& delta,
    bool delta_is_zero) {
    double total = 0.0;
    for (const RankHiddenCell& cell : action.worlds) {
        total +=
            candidate_cell_value_unchecked(
                cell, delta, delta_is_zero);
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
    const RankHiddenRoot& root,
    const Delta& delta,
    double temperature, double mix) {
    RootScores result;
    result.teacher.reserve(root.actions.size());
    result.candidate.reserve(root.actions.size());
    const bool delta_is_zero =
        zero_delta(delta);
    for (const RankHiddenAction& action : root.actions) {
        result.teacher.push_back(
            teacher_action_score(action));
        result.candidate.push_back(
            candidate_action_score(
                action, delta, delta_is_zero));
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
            "DBC3-RANK-HIDDEN distributions are misaligned");
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
    Delta gradient{};
};

ObjectiveResult objective_and_gradient(
    const Dataset& dataset,
    const Delta& delta,
    const OptimizerConfig& config) {
    if (!finite_delta(delta)) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN objective delta is invalid");
    }
    ObjectiveResult result;
    const double root_weight =
        1.0 /
        static_cast<double>(dataset.roots.size());
    for (const RankHiddenRoot& root :
         dataset.roots) {
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
            const RankHiddenAction& rank_action =
                root.actions[action];
            const double inverse_worlds =
                1.0 /
                static_cast<double>(
                    rank_action.worlds.size());
            for (const RankHiddenCell& cell :
                 rank_action.worlds) {
                if (cell.source
                        .terminal_before_boundary) {
                    continue;
                }
                for (std::size_t leaf = 0;
                     leaf < kLeafCount; ++leaf) {
                    const double shift =
                        dot(
                            delta[leaf],
                            cell.hidden[leaf]);
                    const double value =
                        sigmoid(
                            logit(
                                cell.source
                                    .parent_leaf_values[leaf]) +
                            shift);
                    const double multiplier =
                        derivative_by_action_score *
                        inverse_worlds *
                        value * (1.0 - value) /
                        static_cast<double>(kLeafCount);
                    for (std::size_t hidden = 0;
                         hidden < kHiddenCount; ++hidden) {
                        result.gradient[leaf][hidden] +=
                            multiplier *
                            cell.hidden[leaf][hidden];
                    }
                }
            }
        }
    }
    for (std::size_t leaf = 0;
         leaf < kLeafCount; ++leaf) {
        for (std::size_t hidden = 0;
             hidden < kHiddenCount; ++hidden) {
            result.value +=
                0.5 * config.l2_tether *
                delta[leaf][hidden] *
                delta[leaf][hidden];
            result.gradient[leaf][hidden] +=
                config.l2_tether *
                delta[leaf][hidden];
        }
    }
    if (!std::isfinite(result.value) ||
        !finite_delta(result.gradient)) {
        throw std::runtime_error(
            "DBC3-RANK-HIDDEN objective is nonfinite");
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
            "DBC3-RANK-HIDDEN requires the fixed optimizer "
            "recipe");
    }
}

direct::Dataset direct_dataset(
    const Dataset& dataset) {
    std::vector<direct::RankRoot> roots;
    roots.reserve(dataset.roots.size());
    for (const RankHiddenRoot& hidden_root :
         dataset.roots) {
        direct::RankRoot root{
            .stable_root_id =
                hidden_root.stable_root_id,
            .deck = hidden_root.deck,
        };
        root.actions.reserve(
            hidden_root.actions.size());
        for (const RankHiddenAction& hidden_action :
             hidden_root.actions) {
            direct::RankAction action;
            action.worlds.reserve(
                hidden_action.worlds.size());
            for (const RankHiddenCell& hidden_cell :
                 hidden_action.worlds) {
                action.worlds.push_back(
                    hidden_cell.source);
            }
            root.actions.push_back(std::move(action));
        }
        roots.push_back(std::move(root));
    }
    return direct::testing::make_dataset(
        std::move(roots));
}

Dataset project_split(
    const direct::Dataset& source,
    std::shared_ptr<const LearnedModel> parent) {
    Dataset result;
    result.roots_by_deck =
        source.roots_by_deck;
    result.roots.reserve(source.roots.size());
    for (const direct::RankRoot& source_root :
         source.roots) {
        RankHiddenRoot root{
            .stable_root_id =
                source_root.stable_root_id,
            .deck = source_root.deck,
        };
        root.actions.reserve(
            source_root.actions.size());
        for (const direct::RankAction& source_action :
             source_root.actions) {
            RankHiddenAction action;
            action.worlds.reserve(
                source_action.worlds.size());
            for (const direct::RankCell& source_cell :
                 source_action.worlds) {
                RankHiddenCell cell{
                    .source = source_cell,
                };
                if (!source_cell
                         .terminal_before_boundary) {
                    cell.hidden =
                        learned_critic_observation_hidden_activations(
                            source_cell.observation,
                            parent);
                }
                action.worlds.push_back(
                    std::move(cell));
            }
            root.actions.push_back(std::move(action));
        }
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
           topology_frozen &&
           input_hidden_frozen &&
           hidden_output_changed &&
           output_bias_frozen &&
           direct_path_frozen &&
           context_direct_path_frozen &&
           independent_delta_exact &&
           changed_coordinates > 0 &&
           changed_coordinates <=
               kTrainableCoordinateCount &&
           all_policy_heads_frozen;
}

Corpus project_corpus(
    const direct::Corpus& source,
    std::shared_ptr<const LearnedModel> parent) {
    direct::validate_corpus(source, parent);
    Corpus result{
        .train =
            project_split(source.train, parent),
        .dev =
            project_split(source.dev, parent),
        .source_digest = source.source_digest,
        .parent_components =
            source.parent_components,
    };
    validate_corpus(result, parent);
    return result;
}

void validate_dataset(const Dataset& dataset) {
    if (dataset.roots.empty()) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN dataset is empty");
    }
    const direct::Dataset source =
        direct_dataset(dataset);
    if (source.roots_by_deck !=
        dataset.roots_by_deck) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN deck census drifted");
    }
    for (const RankHiddenRoot& root :
         dataset.roots) {
        for (const RankHiddenAction& action :
             root.actions) {
            for (const RankHiddenCell& cell :
                 action.worlds) {
                const bool all_zero =
                    std::all_of(
                        cell.hidden.begin(),
                        cell.hidden.end(),
                        [](const auto& leaf) {
                            return std::all_of(
                                leaf.begin(),
                                leaf.end(),
                                [](double value) {
                                    return value == 0.0;
                                });
                        });
                if (cell.source
                        .terminal_before_boundary) {
                    if (!all_zero) {
                        throw std::invalid_argument(
                            "DBC3-RANK-HIDDEN terminal cell "
                            "retained hidden activation");
                    }
                    continue;
                }
                if (!std::all_of(
                        cell.hidden.begin(),
                        cell.hidden.end(),
                        [](const auto& leaf) {
                            return std::all_of(
                                leaf.begin(),
                                leaf.end(),
                                [](double value) {
                                    return std::isfinite(value) &&
                                           value >= -1.0 &&
                                           value <= 1.0;
                                });
                        })) {
                    throw std::invalid_argument(
                        "DBC3-RANK-HIDDEN hidden activation is "
                        "invalid");
                }
            }
        }
    }
}

void validate_corpus(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    validate_dataset(corpus.train);
    validate_dataset(corpus.dev);
    const direct::Corpus source{
        .train = direct_dataset(corpus.train),
        .dev = direct_dataset(corpus.dev),
        .source_digest = corpus.source_digest,
        .parent_components =
            corpus.parent_components,
    };
    direct::validate_corpus(source, parent);
    const auto verify_split =
        [&](const Dataset& dataset) {
            for (const RankHiddenRoot& root :
                 dataset.roots) {
                for (const RankHiddenAction& action :
                     root.actions) {
                    for (const RankHiddenCell& cell :
                         action.worlds) {
                        if (cell.source
                                .terminal_before_boundary) {
                            continue;
                        }
                        if (cell.hidden !=
                                learned_critic_observation_hidden_activations(
                                    cell.source.observation,
                                    parent) ||
                            cell.source.parent_leaf_values !=
                                learned_critic_observation_leaf_values(
                                    cell.source.observation,
                                    parent)) {
                            throw std::invalid_argument(
                                "DBC3-RANK-HIDDEN frozen parent "
                                "projection drifted");
                        }
                    }
                }
            }
        };
    verify_split(corpus.train);
    verify_split(corpus.dev);
}

double candidate_cell_value(
    const RankHiddenCell& cell,
    const Delta& delta) {
    if (!finite_delta(delta)) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN delta is invalid");
    }
    return candidate_cell_value_unchecked(
        cell, delta, zero_delta(delta));
}

Metrics evaluate(
    const Dataset& dataset,
    const Delta& delta,
    double temperature, double mix) {
    validate_dataset(dataset);
    if (!finite_delta(delta)) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN evaluation delta is invalid");
    }
    const bool delta_is_zero =
        zero_delta(delta);
    direct::Dataset scored =
        direct_dataset(dataset);
    for (std::size_t root = 0;
         root < dataset.roots.size(); ++root) {
        for (std::size_t action = 0;
             action <
                 dataset.roots[root].actions.size();
             ++action) {
            for (std::size_t world = 0;
                 world <
                     dataset.roots[root]
                         .actions[action].worlds.size();
                 ++world) {
                const RankHiddenCell& hidden_cell =
                    dataset.roots[root]
                        .actions[action].worlds[world];
                if (hidden_cell.source
                        .terminal_before_boundary) {
                    continue;
                }
                const double value =
                    candidate_cell_value_unchecked(
                        hidden_cell, delta,
                        delta_is_zero);
                scored.roots[root]
                    .actions[action].worlds[world]
                    .parent_leaf_values = {
                        value, value};
            }
        }
    }
    return direct::evaluate(
        scored,
        std::vector<double>(
            direct::kFeatureCount, 0.0),
        temperature, mix);
}

Metrics evaluate_model(
    const Dataset& dataset,
    std::shared_ptr<const LearnedModel> model,
    double temperature, double mix) {
    return direct::evaluate_model(
        direct_dataset(dataset), std::move(model),
        temperature, mix);
}

ExactEvaluationReport evaluate_exact(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const Delta& delta) {
    validate_corpus(corpus, parent);
    if (!candidate || !finite_delta(delta)) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN exact evaluation input is "
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
            for (const RankHiddenRoot& root :
                 dataset.roots) {
                for (const RankHiddenAction& action :
                     root.actions) {
                    for (const RankHiddenCell& cell :
                         action.worlds) {
                        if (cell.source
                                .terminal_before_boundary) {
                            continue;
                        }
                        const double surrogate =
                            candidate_cell_value(
                                cell, delta);
                        const double engine =
                            learned_critic_observation_value(
                                cell.source.observation,
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
    Delta delta{};
    Delta first_moment{};
    Delta second_moment{};
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
        for (std::size_t leaf = 0;
             leaf < kLeafCount; ++leaf) {
            for (std::size_t hidden = 0;
                 hidden < kHiddenCount; ++hidden) {
                const double gradient =
                    current.gradient[leaf][hidden] *
                    gradient_scale;
                first_moment[leaf][hidden] =
                    config.beta_one *
                        first_moment[leaf][hidden] +
                    (1.0 - config.beta_one) *
                        gradient;
                second_moment[leaf][hidden] =
                    config.beta_two *
                        second_moment[leaf][hidden] +
                    (1.0 - config.beta_two) *
                        gradient * gradient;
                const double corrected_first =
                    first_moment[leaf][hidden] /
                    (1.0 - beta_one_power);
                const double corrected_second =
                    second_moment[leaf][hidden] /
                    (1.0 - beta_two_power);
                delta[leaf][hidden] -=
                    config.learning_rate *
                    corrected_first /
                    (std::sqrt(corrected_second) +
                     config.epsilon);
            }
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
                train, Delta{},
                config.temperature, config.mix),
        .after =
            evaluate(
                train, delta,
                config.temperature, config.mix),
    };
}

ModelIsolationReport apply_delta(
    std::shared_ptr<const LearnedModel> parent,
    const Delta& delta) {
    if (!parent ||
        !finite_delta(delta) ||
        zero_delta(delta)) {
        throw std::invalid_argument(
            "DBC3-RANK-HIDDEN model application input is "
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
    const auto parent_hidden_output =
        learned_critic_hidden_output_parameters(parent);
    const auto parent_output_bias =
        learned_critic_output_bias_parameters(parent);
    const auto parent_direct =
        learned_critic_direct_path_parameters(parent);
    const auto parent_context_direct =
        learned_critic_context_direct_path_parameters(
            parent);

    result.candidate =
        with_learned_critic_hidden_output_delta(
            parent, delta);
    const auto repeated =
        with_learned_critic_hidden_output_delta(
            parent, delta);
    if (!result.candidate || !repeated) {
        throw std::runtime_error(
            "DBC3-RANK-HIDDEN engine returned no candidate");
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

    const auto candidate_hidden_output =
        learned_critic_hidden_output_parameters(
            result.candidate);
    const auto repeated_hidden_output =
        learned_critic_hidden_output_parameters(
            repeated);
    const auto candidate_output_bias =
        learned_critic_output_bias_parameters(
            result.candidate);
    const auto repeated_output_bias =
        learned_critic_output_bias_parameters(
            repeated);
    const auto candidate_direct =
        learned_critic_direct_path_parameters(
            result.candidate);
    const auto repeated_direct =
        learned_critic_direct_path_parameters(
            repeated);
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
    result.topology_frozen =
        learned_critic_schema(parent) ==
            LearnedCriticSchema::LegacyStateOnly &&
        learned_critic_schema(result.candidate) ==
            LearnedCriticSchema::LegacyStateOnly;
    result.input_hidden_frozen =
        result.parent_critic_tensors.input_hidden ==
        result.candidate_critic_tensors.input_hidden;
    result.hidden_output_changed =
        result.parent_critic_tensors.output_layer !=
        result.candidate_critic_tensors.output_layer;
    result.output_bias_frozen =
        parent_output_bias ==
            candidate_output_bias &&
        candidate_output_bias ==
            repeated_output_bias;
    result.direct_path_frozen =
        parent_direct == candidate_direct &&
        candidate_direct == repeated_direct;
    result.context_direct_path_frozen =
        parent_context_direct ==
            candidate_context_direct &&
        candidate_context_direct ==
            repeated_context_direct;
    result.independent_delta_exact =
        candidate_hidden_output ==
            repeated_hidden_output;
    for (std::size_t leaf = 0;
         leaf < kLeafCount &&
         result.independent_delta_exact; ++leaf) {
        for (std::size_t hidden = 0;
             hidden < kHiddenCount; ++hidden) {
            if (candidate_hidden_output[leaf][hidden] !=
                parent_hidden_output[leaf][hidden] +
                    delta[leaf][hidden]) {
                result.independent_delta_exact = false;
                break;
            }
        }
    }
    for (std::size_t leaf = 0;
         leaf < kLeafCount; ++leaf) {
        for (std::size_t hidden = 0;
             hidden < kHiddenCount; ++hidden) {
            result.changed_coordinates +=
                candidate_hidden_output[leaf][hidden] !=
                        parent_hidden_output[leaf][hidden]
                    ? 1U
                    : 0U;
        }
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
    return direct::evaluate_offline_gate(
        direct::OfflineGateInputs{
            .repeated_optimizer_bit_identical =
                fit == repeated_fit,
            .optimizer_recipe_exact =
                fit.config == OptimizerConfig{} &&
                fit.completed_steps == kAdamSteps,
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
        });
}

namespace testing {

Dataset make_dataset(
    std::vector<RankHiddenRoot> roots) {
    Dataset result{.roots = std::move(roots)};
    for (const RankHiddenRoot& root :
         result.roots) {
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
            std::string(
                direct::dbc::kFrozenCorpusDigest),
        .parent_components =
            std::move(parent_components),
    };
    return result;
}

ObjectiveProbe objective_probe(
    const Dataset& dataset,
    const Delta& delta) {
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

} // namespace old_school::decision_boundary_rank_hidden
