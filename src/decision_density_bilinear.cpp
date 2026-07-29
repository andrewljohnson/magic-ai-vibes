#include "old_school/decision_density_bilinear.hpp"

#include "old_school/action_q_nested_actor_distill.hpp"
#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace old_school::decision_density_bilinear {
namespace {

constexpr std::uint64_t kPositiveZeroBits = 0;

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "AQ19 deck identifier is invalid");
    }
    return result;
}

std::size_t width_index(
    priority::WidthStratum width) {
    const std::size_t result =
        static_cast<std::size_t>(width);
    if (result >= priority::kWidthStrata) {
        throw std::invalid_argument(
            "AQ19 width stratum is invalid");
    }
    return result;
}

std::size_t cell_index(
    DeckId deck, priority::WidthStratum width) {
    return deck_index(deck) * priority::kWidthStrata +
           width_index(width);
}

bool positive_zero(double value) {
    return std::bit_cast<std::uint64_t>(value) ==
           kPositiveZeroBits;
}

bool bit_equal(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

template <typename Range>
bool finite_range(const Range& values) {
    return std::all_of(
        values.begin(), values.end(),
        [](double value) {
            return std::isfinite(value);
        });
}

template <typename Range>
bool probability_range(const Range& values) {
    return finite_range(values) &&
           std::all_of(
               values.begin(), values.end(),
               [](double value) {
                   return value >= 0.0 &&
                          value <= 1.0;
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

double binary_cross_entropy_from_logit(
    double target, double logit) {
    const double softplus =
        logit > 0.0
            ? logit +
                  std::log1p(std::exp(-logit))
            : std::log1p(std::exp(logit));
    return softplus - target * logit;
}

std::vector<std::size_t> canonical_order(
    const Root& root) {
    std::vector<std::size_t> result(
        root.options.size(),
        std::numeric_limits<std::size_t>::max());
    for (std::size_t row = 0;
         row < root.options.size(); ++row) {
        const std::size_t ordinal =
            root.options[row].canonical_ordinal;
        if (ordinal >= result.size() ||
            result[ordinal] !=
                std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument(
                "AQ19 canonical action order is invalid");
        }
        result[ordinal] = row;
    }
    return result;
}

bool zero_action_projection(
    const Parameters& parameters) {
    for (const auto& row : parameters.v) {
        for (const double value : row) {
            if (!positive_zero(value)) {
                return false;
            }
        }
    }
    return true;
}

void validate_parameters(
    const Parameters& parameters) {
    const auto validate =
        [](const auto& matrix) {
            for (const auto& row : matrix) {
                for (const double value : row) {
                    if (!std::isfinite(value) ||
                        (value == 0.0 &&
                         !positive_zero(value))) {
                        throw std::invalid_argument(
                            "AQ19 parameters are invalid");
                    }
                }
            }
        };
    validate(parameters.delta_u);
    validate(parameters.v);
}

std::vector<std::vector<double>>
positive_zero_residuals(const Dataset& dataset) {
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        result.emplace_back(root.options.size(), 0.0);
    }
    return result;
}

pair::PrecomputedScoreDataset precomputed_dataset(
    const Dataset& dataset) {
    pair::PrecomputedScoreDataset result{
        .roots_by_deck = dataset.roots_by_deck,
    };
    result.roots.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        const std::vector<std::size_t> order =
            canonical_order(root);
        pair::PrecomputedScoreRoot projected{
            .deck = root.deck,
        };
        projected.base_aggregate_scores.reserve(
            root.options.size());
        projected.teacher_aggregate_scores.reserve(
            root.options.size());
        projected.common_world_teacher_samples.reserve(
            root.options.size());
        for (const std::size_t row : order) {
            const Option& option = root.options[row];
            projected.base_aggregate_scores.push_back(
                option.base_aggregate_score);
            projected.teacher_aggregate_scores.push_back(
                option.teacher_aggregate_score);
            projected.common_world_teacher_samples.emplace_back(
                option.common_world_teacher_samples.begin(),
                option.common_world_teacher_samples.end());
        }
        result.roots.push_back(std::move(projected));
    }
    pair::validate_precomputed_score_dataset(result);
    return result;
}

std::vector<std::vector<double>> canonical_residual_rows(
    const Dataset& dataset,
    const std::vector<std::vector<double>>& residual_rows) {
    if (residual_rows.size() != dataset.roots.size()) {
        throw std::invalid_argument(
            "AQ19 residual root count drifted");
    }
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (std::size_t root_index = 0;
         root_index < dataset.roots.size(); ++root_index) {
        const Root& root = dataset.roots[root_index];
        if (residual_rows[root_index].size() !=
            root.options.size()) {
            throw std::invalid_argument(
                "AQ19 residual action count drifted");
        }
        const std::vector<std::size_t> order =
            canonical_order(root);
        std::vector<double> row;
        row.reserve(order.size());
        for (const std::size_t index : order) {
            const double value =
                residual_rows[root_index][index];
            if (!std::isfinite(value)) {
                throw std::invalid_argument(
                    "AQ19 residual is nonfinite");
            }
            row.push_back(value);
        }
        result.push_back(std::move(row));
    }
    return result;
}

struct ForwardWorkspace {
    std::vector<std::size_t> order;
    std::array<double, kActionFeatureCount> mean_action{};
    std::vector<ActionFeatures> centered_action;
    std::array<double, kRank> hidden{};
    std::vector<std::array<double, kRank>>
        action_projection;
    ForwardResult result;
};

ForwardWorkspace forward_workspace(
    const Root& root, const Parameters& parameters,
    double residual_weight,
    bool derivative_workspace = false) {
    validate_root(root);
    validate_parameters(parameters);
    if (!std::isfinite(residual_weight) ||
        residual_weight != kResidualWeight) {
        throw std::invalid_argument(
            "AQ19 residual weight drifted from sealed recipe");
    }
    ForwardWorkspace work;
    work.order = canonical_order(root);
    work.result.logits.assign(root.options.size(), 0.0);
    work.result.centered_logits.assign(
        root.options.size(), 0.0);
    work.result.residuals.assign(
        root.options.size(), 0.0);
    work.result.scores.reserve(root.options.size());

    const bool zero_v =
        zero_action_projection(parameters);
    if (zero_v && !derivative_workspace) {
        for (const Option& option : root.options) {
            work.result.scores.push_back(
                option.base_aggregate_score);
        }
        return work;
    }

    for (const std::size_t row : work.order) {
        for (std::size_t feature = 0;
             feature < kActionFeatureCount; ++feature) {
            work.mean_action[feature] +=
                root.options[row]
                    .action_features[feature];
        }
    }
    for (double& value : work.mean_action) {
        value /= static_cast<double>(
            root.options.size());
    }

    work.centered_action.resize(root.options.size());
    for (std::size_t row = 0;
         row < root.options.size(); ++row) {
        for (std::size_t feature = 0;
             feature < kActionFeatureCount; ++feature) {
            work.centered_action[row][feature] =
                root.options[row].action_features[feature] -
                work.mean_action[feature];
        }
    }

    const auto& u0 = learned_priority_bilinear_u0();
    for (std::size_t rank = 0; rank < kRank; ++rank) {
        double preactivation = 0.0;
        for (std::size_t feature = 0;
             feature < kStateFeatureCount; ++feature) {
            preactivation +=
                (u0[rank][feature] +
                 parameters.delta_u[rank][feature]) *
                root.state[feature];
        }
        work.hidden[rank] = std::tanh(preactivation);
    }

    work.action_projection.resize(root.options.size());
    for (std::size_t row = 0;
         row < root.options.size(); ++row) {
        for (std::size_t rank = 0;
             rank < kRank; ++rank) {
            double projection = 0.0;
            for (std::size_t feature = 0;
                 feature < kActionFeatureCount;
                 ++feature) {
                projection +=
                    parameters.v[rank][feature] *
                    work.centered_action[row][feature];
            }
            work.action_projection[row][rank] =
                projection;
            work.result.logits[row] +=
                work.hidden[rank] * projection;
        }
    }

    double mean_logit = 0.0;
    for (const std::size_t row : work.order) {
        mean_logit += work.result.logits[row];
    }
    mean_logit /= static_cast<double>(
        root.options.size());
    for (std::size_t row = 0;
         row < root.options.size(); ++row) {
        const double centered =
            work.result.logits[row] - mean_logit;
        const double raw_residual =
            residual_weight * std::tanh(centered);
        const double residual =
            raw_residual == 0.0 ? 0.0 : raw_residual;
        if (!std::isfinite(residual) ||
            residual < -residual_weight ||
            residual > residual_weight) {
            throw std::runtime_error(
                "AQ19 residual escaped its bound");
        }
        work.result.centered_logits[row] =
            centered == 0.0 ? 0.0 : centered;
        work.result.residuals[row] = residual;
        work.result.scores.push_back(
            root.options[row].base_aggregate_score +
            residual);
    }
    return work;
}

struct ObjectiveResult {
    double objective = 0.0;
    Parameters gradient;
};

ObjectiveResult objective_and_gradient(
    const Dataset& dataset,
    const Parameters& parameters,
    const OptimizerConfig& config) {
    validate_dataset(dataset);
    validate_parameters(parameters);
    ObjectiveResult output;
    for (const Root& root : dataset.roots) {
        const ForwardWorkspace work =
            forward_workspace(
                root, parameters,
                config.residual_weight, true);
        const std::size_t cell =
            cell_index(root.deck, root.width);
        const double root_weight =
            1.0 /
            (static_cast<double>(kCellCount) *
             static_cast<double>(
                 dataset.roots_by_cell[cell]));
        double total_cost = 0.0;
        for (std::size_t left_ordinal = 0;
             left_ordinal < root.options.size();
             ++left_ordinal) {
            const std::size_t left =
                work.order[left_ordinal];
            for (std::size_t right_ordinal =
                     left_ordinal + 1;
                 right_ordinal < root.options.size();
                 ++right_ordinal) {
                const std::size_t right =
                    work.order[right_ordinal];
                total_cost += std::abs(
                    root.options[left]
                        .teacher_aggregate_score -
                    root.options[right]
                        .teacher_aggregate_score);
            }
        }
        if (total_cost == 0.0) {
            continue;
        }

        std::vector<double> score_gradient(
            root.options.size(), 0.0);
        for (std::size_t left_ordinal = 0;
             left_ordinal < root.options.size();
             ++left_ordinal) {
            const std::size_t left =
                work.order[left_ordinal];
            for (std::size_t right_ordinal =
                     left_ordinal + 1;
                 right_ordinal < root.options.size();
                 ++right_ordinal) {
                const std::size_t right =
                    work.order[right_ordinal];
                const double teacher_gap =
                    root.options[left]
                        .teacher_aggregate_score -
                    root.options[right]
                        .teacher_aggregate_score;
                if (teacher_gap == 0.0) {
                    continue;
                }
                const double cost =
                    std::abs(teacher_gap) / total_cost;
                const double target =
                    sigmoid(
                        teacher_gap /
                        config.pair_temperature);
                const double candidate_logit =
                    (work.result.scores[left] -
                     work.result.scores[right]) /
                    config.pair_temperature;
                output.objective +=
                    root_weight * cost *
                    binary_cross_entropy_from_logit(
                        target, candidate_logit);
                const double derivative =
                    root_weight * cost *
                    (sigmoid(candidate_logit) - target) /
                    config.pair_temperature;
                score_gradient[left] += derivative;
                score_gradient[right] -= derivative;
            }
        }

        std::vector<double> centered_gradient(
            root.options.size(), 0.0);
        for (std::size_t row = 0;
             row < root.options.size(); ++row) {
            const double tanh_centered =
                std::tanh(
                    work.result.centered_logits[row]);
            centered_gradient[row] =
                score_gradient[row] *
                config.residual_weight *
                (1.0 -
                 tanh_centered * tanh_centered);
        }
        double centered_gradient_sum = 0.0;
        for (const std::size_t row : work.order) {
            centered_gradient_sum +=
                centered_gradient[row];
        }
        const double centered_gradient_mean =
            centered_gradient_sum /
            static_cast<double>(root.options.size());
        std::array<double, kRank>
            hidden_gradient{};
        for (const std::size_t row : work.order) {
            const double logit_gradient =
                centered_gradient[row] -
                centered_gradient_mean;
            for (std::size_t rank = 0;
                 rank < kRank; ++rank) {
                hidden_gradient[rank] +=
                    logit_gradient *
                    work.action_projection[row][rank];
                for (std::size_t feature = 0;
                     feature < kActionFeatureCount;
                     ++feature) {
                    output.gradient.v[rank][feature] +=
                        logit_gradient *
                        work.hidden[rank] *
                        work.centered_action[row][feature];
                }
            }
        }
        for (std::size_t rank = 0;
             rank < kRank; ++rank) {
            const double preactivation_gradient =
                hidden_gradient[rank] *
                (1.0 -
                 work.hidden[rank] *
                     work.hidden[rank]);
            for (std::size_t feature = 0;
                 feature < kStateFeatureCount;
                 ++feature) {
                output.gradient
                    .delta_u[rank][feature] +=
                    preactivation_gradient *
                    root.state[feature];
            }
        }
    }

    for (std::size_t rank = 0; rank < kRank; ++rank) {
        for (std::size_t feature = 0;
             feature < kStateFeatureCount; ++feature) {
            const double value =
                parameters.delta_u[rank][feature];
            output.objective +=
                0.5 * config.l2_tether *
                value * value;
            output.gradient.delta_u[rank][feature] +=
                config.l2_tether * value;
        }
        for (std::size_t feature = 0;
             feature < kActionFeatureCount; ++feature) {
            const double value =
                parameters.v[rank][feature];
            output.objective +=
                0.5 * config.l2_tether *
                value * value;
            output.gradient.v[rank][feature] +=
                config.l2_tether * value;
        }
    }
    if (!std::isfinite(output.objective)) {
        throw std::runtime_error(
            "AQ19 objective is nonfinite");
    }
    validate_parameters(output.gradient);
    return output;
}

double parameter_squared_norm(
    const Parameters& parameters) {
    double result = 0.0;
    for (std::size_t rank = 0; rank < kRank; ++rank) {
        for (const double value :
             parameters.delta_u[rank]) {
            result += value * value;
        }
        for (const double value : parameters.v[rank]) {
            result += value * value;
        }
    }
    return result;
}

template <typename Function>
void for_each_parameter(
    Parameters& parameters, Function&& function) {
    for (std::size_t rank = 0; rank < kRank; ++rank) {
        for (std::size_t feature = 0;
             feature < kStateFeatureCount; ++feature) {
            function(
                parameters.delta_u[rank][feature],
                rank * kStateFeatureCount + feature);
        }
    }
    const std::size_t offset =
        kRank * kStateFeatureCount;
    for (std::size_t rank = 0; rank < kRank; ++rank) {
        for (std::size_t feature = 0;
             feature < kActionFeatureCount; ++feature) {
            function(
                parameters.v[rank][feature],
                offset +
                    rank * kActionFeatureCount +
                    feature);
        }
    }
}

template <typename Function>
void for_each_parameter(
    const Parameters& parameters, Function&& function) {
    for (std::size_t rank = 0; rank < kRank; ++rank) {
        for (std::size_t feature = 0;
             feature < kStateFeatureCount; ++feature) {
            function(
                parameters.delta_u[rank][feature],
                rank * kStateFeatureCount + feature);
        }
    }
    const std::size_t offset =
        kRank * kStateFeatureCount;
    for (std::size_t rank = 0; rank < kRank; ++rank) {
        for (std::size_t feature = 0;
             feature < kActionFeatureCount; ++feature) {
            function(
                parameters.v[rank][feature],
                offset +
                    rank * kActionFeatureCount +
                    feature);
        }
    }
}

std::vector<double> flatten(
    const Parameters& parameters) {
    std::vector<double> result(kParameterCount);
    for_each_parameter(
        parameters,
        [&result](double value, std::size_t index) {
            result[index] = value;
        });
    return result;
}

Parameters unflatten(std::span<const double> values) {
    if (values.size() != kParameterCount) {
        throw std::invalid_argument(
            "AQ19 parameter vector width drifted");
    }
    Parameters result;
    for_each_parameter(
        result,
        [&values](double& value, std::size_t index) {
            value = values[index] == 0.0
                        ? 0.0
                        : values[index];
        });
    return result;
}

void validate_optimizer_config(
    const OptimizerConfig& config) {
    if (config.fit_tag != kFitTag ||
        config.steps == 0 ||
        !std::isfinite(config.learning_rate) ||
        config.learning_rate <= 0.0 ||
        !std::isfinite(config.beta_one) ||
        config.beta_one <= 0.0 ||
        config.beta_one >= 1.0 ||
        !std::isfinite(config.beta_two) ||
        config.beta_two <= 0.0 ||
        config.beta_two >= 1.0 ||
        !std::isfinite(config.epsilon) ||
        config.epsilon <= 0.0 ||
        !std::isfinite(config.residual_weight) ||
        config.residual_weight != kResidualWeight ||
        !std::isfinite(config.pair_temperature) ||
        config.pair_temperature <= 0.0 ||
        !std::isfinite(config.l2_tether) ||
        config.l2_tether < 0.0 ||
        !std::isfinite(
            config.global_gradient_norm_clip) ||
        config.global_gradient_norm_clip <= 0.0) {
        throw std::invalid_argument(
            "AQ19 optimizer recipe is invalid");
    }
}

bool exact_optimizer_recipe(
    const OptimizerConfig& config) {
    return config == OptimizerConfig{};
}

std::size_t fold_for_group(
    const FoldAssignment& assignment,
    std::string_view group) {
    const auto found = std::lower_bound(
        assignment.group_folds.begin(),
        assignment.group_folds.end(), group,
        [](const auto& entry, std::string_view key) {
            return entry.first < key;
        });
    if (found == assignment.group_folds.end() ||
        found->first != group) {
        throw std::invalid_argument(
            "AQ19 root has no physical fold");
    }
    return found->second;
}

bool metric_non_increasing(
    double candidate, double parent) {
    return candidate <= parent;
}

bool metric_non_decreasing(
    double candidate, double parent) {
    return candidate >= parent;
}

void validate_selector_summary(
    const BotBenchmarkSummary& summary,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<
        const LearnedPriorityBilinear>& residual) {
    constexpr std::size_t kExpectedGames = 60;
    constexpr std::size_t kExpectedDeckGames = 12;
    const auto accounted =
        [](const auto& stats) {
            return stats.games ==
                   stats.wins + stats.losses +
                       stats.draws;
        };
    if (!parent || !residual ||
        summary.evaluation_seed != kSelectorSeed ||
        summary.learned_training_seed != 424242 ||
        summary.repetitions_per_deck_pairing != 1 ||
        summary.total_games != kExpectedGames ||
        summary.challenger.kind != BotKind::Learned ||
        summary.baseline.kind != BotKind::Learned ||
        summary.challenger.learned_variant !=
            LearnedVariant::ValueSearchChampion ||
        summary.baseline.learned_variant !=
            LearnedVariant::ValueSearchChampion ||
        summary.challenger.rollouts_per_action != 8 ||
        summary.baseline.rollouts_per_action != 8 ||
        summary.challenger.learned_model != parent ||
        summary.baseline.learned_model != parent ||
        summary.challenger_model_fingerprint !=
            kRequiredParentFingerprint ||
        summary.baseline_model_fingerprint !=
            kRequiredParentFingerprint ||
        summary.challenger.value_priority_bilinear !=
            residual ||
        summary.baseline.value_priority_bilinear ||
        summary.challenger.exploration_rate != 0.0 ||
        summary.baseline.exploration_rate != 0.0 ||
        summary.challenger
                .value_continuation_epsilon != 0.0 ||
        summary.baseline
                .value_continuation_epsilon != 0.0 ||
        summary.challenger
                .value_priority_residual_weight != 0.0 ||
        summary.baseline
                .value_priority_residual_weight != 0.0 ||
        summary.challenger.value_pass_dominance ||
        summary.baseline.value_pass_dominance ||
        summary.challenger
                .value_resolved_shallow_prior_weight != 0.0 ||
        summary.baseline
                .value_resolved_shallow_prior_weight != 0.0 ||
        summary.challenger.value_adversarial_blocks ||
        summary.baseline.value_adversarial_blocks ||
        summary.challenger.value_actor_local_search ||
        summary.baseline.value_actor_local_search ||
        summary.challenger
                .value_recursive_policy_improvement ||
        summary.baseline
                .value_recursive_policy_improvement ||
        summary.challenger
                .value_continuation_controller !=
            LearnedContinuationController::Legacy ||
        summary.baseline
                .value_continuation_controller !=
            LearnedContinuationController::Legacy ||
        summary.challenger.training_games != 800 ||
        summary.baseline.training_games != 800 ||
        !accounted(summary.challenger_stats) ||
        !accounted(summary.baseline_stats) ||
        summary.challenger_stats.games !=
            kExpectedGames ||
        summary.baseline_stats.games != kExpectedGames ||
        summary.challenger_stats.wins !=
            summary.baseline_stats.losses ||
        summary.challenger_stats.losses !=
            summary.baseline_stats.wins ||
        summary.challenger_stats.draws !=
            summary.baseline_stats.draws ||
        summary.life_total_finishes +
                summary.empty_library_finishes +
                summary.turn_limit_draws !=
            kExpectedGames) {
        throw std::runtime_error(
            "AQ19 selector aggregate recipe drifted");
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& challenger =
            summary.challenger_decks[deck];
        const auto& baseline =
            summary.baseline_decks[deck];
        if (!accounted(challenger) ||
            !accounted(baseline) ||
            challenger.games != kExpectedDeckGames ||
            baseline.games != kExpectedDeckGames ||
            challenger.on_play_games != 6 ||
            challenger.on_draw_games != 6 ||
            baseline.on_play_games != 6 ||
            baseline.on_draw_games != 6) {
            throw std::runtime_error(
                "AQ19 selector deck balance drifted");
        }
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                if (summary
                        .challenger_outcome_quadrants
                            [deck][seat][play_draw]
                            .games != 3 ||
                    summary
                        .baseline_outcome_quadrants
                            [deck][seat][play_draw]
                            .games != 3) {
                    throw std::runtime_error(
                        "AQ19 selector quadrant balance "
                        "drifted");
                }
            }
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const std::size_t expected =
                deck == opponent ? 4 : 2;
            if (summary.challenger_deck_matchups
                        [deck][opponent]
                            .games != expected) {
                throw std::runtime_error(
                    "AQ19 selector matchup balance "
                    "drifted");
            }
        }
    }
}

void append_failure(
    bool passed, std::string_view name,
    std::vector<std::string>& failures) {
    if (!passed) {
        failures.emplace_back(name);
    }
}

} // namespace

bool OfflineGate::passed() const {
    return failures.empty();
}

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() == 1 &&
        arguments.front() == "--run") {
        return Command::Run;
    }
    if (arguments.size() == 1 &&
        arguments.front() == "--offline-report") {
        return Command::OfflineReport;
    }
    return std::nullopt;
}

bool command_authorizes_selector_seed(Command command) {
    switch (command) {
    case Command::Run:
        return true;
    case Command::OfflineReport:
        return false;
    }
    throw std::invalid_argument(
        "AQ19 command is invalid");
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-decision-density-bilinear "
           "(--run|--offline-report)\n";
}

void validate_root(const Root& root) {
    if (root.stable_root_id.empty() ||
        root.physical_game_group.empty() ||
        root.options.size() < 2 ||
        priority::width_stratum(root.options.size()) !=
            root.width ||
        !finite_range(root.state)) {
        throw std::invalid_argument(
            "AQ19 root identity or shape is invalid");
    }
    (void)deck_index(root.deck);
    (void)width_index(root.width);
    const std::vector<std::size_t> order =
        canonical_order(root);
    (void)order;
    for (const Option& option : root.options) {
        if (!finite_range(option.action_features) ||
            !std::isfinite(option.base_aggregate_score) ||
            option.base_aggregate_score < 0.0 ||
            option.base_aggregate_score > 1.0 ||
            !std::isfinite(option.teacher_aggregate_score) ||
            option.teacher_aggregate_score < 0.0 ||
            option.teacher_aggregate_score > 1.0 ||
            !probability_range(
                option.common_world_teacher_samples)) {
            throw std::invalid_argument(
                "AQ19 option payload is invalid");
        }
    }
}

void validate_dataset(const Dataset& dataset) {
    if (dataset.roots.empty()) {
        throw std::invalid_argument(
            "AQ19 dataset is empty");
    }
    std::array<std::size_t, kCellCount> cells{};
    std::array<std::size_t, kDeckCount> decks{};
    std::set<std::string> stable_ids;
    for (const Root& root : dataset.roots) {
        validate_root(root);
        ++cells[cell_index(root.deck, root.width)];
        ++decks[deck_index(root.deck)];
        if (!stable_ids.insert(root.stable_root_id).second) {
            throw std::invalid_argument(
                "AQ19 root id is duplicated");
        }
    }
    if (cells != dataset.roots_by_cell ||
        decks != dataset.roots_by_deck ||
        std::any_of(
            cells.begin(), cells.end(),
            [](std::size_t value) {
                return value == 0;
            })) {
        throw std::invalid_argument(
            "AQ19 dataset census drifted");
    }
}

void validate_corpus(const Corpus& corpus) {
    validate_dataset(corpus.train);
    validate_dataset(corpus.dev);
    if (corpus.source_digest !=
            kRequiredCorpusDigest ||
        corpus.parent_fingerprint !=
            kRequiredParentFingerprint ||
        corpus.train.roots.size() !=
            labels::kExpectedTrainRoots ||
        corpus.dev.roots.size() !=
            labels::kExpectedDevRoots) {
        throw std::invalid_argument(
            "AQ19 corpus identity drifted");
    }
}

namespace testing {

Root project_root(const labels::RootLabel& source) {
    labels::validate_root_label(source);
    if (source.option_rows.size() !=
            source.actions.size() ||
        source.option_rows.empty()) {
        throw std::invalid_argument(
            "AQ19 source feature rows drifted");
    }
    Root result{
        .stable_root_id =
            source.identity.stable_root_id,
        .physical_game_group =
            source.identity.physical_game_group,
        .deck = source.identity.owner_deck,
        .width = source.identity.width_stratum,
    };
    const std::vector<double>& first =
        source.option_rows.front();
    if (first.size() != kPolicyFeatureCount) {
        throw std::invalid_argument(
            "AQ19 source feature width drifted");
    }
    std::copy_n(
        first.begin(), kStateFeatureCount,
        result.state.begin());
    result.options.reserve(source.actions.size());
    for (std::size_t action = 0;
         action < source.actions.size(); ++action) {
        const std::vector<double>& row =
            source.option_rows[action];
        if (row.size() != kPolicyFeatureCount) {
            throw std::invalid_argument(
                "AQ19 source feature width drifted");
        }
        for (std::size_t feature = 0;
             feature < kStateFeatureCount; ++feature) {
            if (!bit_equal(
                    row[feature],
                    first[feature])) {
                throw std::invalid_argument(
                    "AQ19 actor-state prefix is not "
                    "bit-identical");
            }
        }
        if (source.teacher_shallow_prior_samples
                [action]
                    .size() != labels::kWorlds ||
            !std::all_of(
                source.teacher_shallow_prior_samples
                    [action]
                        .begin(),
                source.teacher_shallow_prior_samples
                    [action]
                        .end(),
                positive_zero)) {
            throw std::invalid_argument(
                "AQ19 teacher shallow prior is not "
                "exact positive zero");
        }
        Option option{
            .canonical_ordinal = action,
            .action = source.actions[action],
            .base_aggregate_score =
                source.base_aggregate_scores[action],
            .teacher_aggregate_score =
                source.teacher_aggregate_scores[action],
        };
        std::copy(
            row.begin() +
                static_cast<std::ptrdiff_t>(
                    kStateFeatureCount),
            row.end(), option.action_features.begin());
        if (source.teacher_q_samples[action].size() !=
            labels::kWorlds) {
            throw std::invalid_argument(
                "AQ19 teacher world count drifted");
        }
        std::copy(
            source.teacher_q_samples[action].begin(),
            source.teacher_q_samples[action].end(),
            option.common_world_teacher_samples.begin());
        result.options.push_back(std::move(option));
    }
    validate_root(result);
    return result;
}

Dataset make_dataset(std::vector<Root> roots) {
    Dataset result{.roots = std::move(roots)};
    for (const Root& root : result.roots) {
        ++result.roots_by_cell[
            cell_index(root.deck, root.width)];
        ++result.roots_by_deck[
            deck_index(root.deck)];
    }
    validate_dataset(result);
    return result;
}

Root permute_options(
    const Root& root,
    const std::vector<std::size_t>& permutation) {
    validate_root(root);
    if (permutation.size() != root.options.size()) {
        throw std::invalid_argument(
            "AQ19 option permutation width drifted");
    }
    std::vector<bool> seen(root.options.size(), false);
    Root result = root;
    result.options.clear();
    result.options.reserve(root.options.size());
    for (const std::size_t index : permutation) {
        if (index >= root.options.size() || seen[index]) {
            throw std::invalid_argument(
                "AQ19 option permutation is invalid");
        }
        seen[index] = true;
        result.options.push_back(root.options[index]);
    }
    validate_root(result);
    return result;
}

std::vector<std::vector<double>> option_rows(
    const Root& root) {
    validate_root(root);
    std::vector<std::vector<double>> result;
    result.reserve(root.options.size());
    for (const Option& option : root.options) {
        std::vector<double> row;
        row.reserve(kPolicyFeatureCount);
        row.insert(
            row.end(), root.state.begin(),
            root.state.end());
        row.insert(
            row.end(), option.action_features.begin(),
            option.action_features.end());
        result.push_back(std::move(row));
    }
    return result;
}

ObjectiveProbe objective_probe(
    const Dataset& dataset,
    const Parameters& parameters) {
    const ObjectiveResult result =
        objective_and_gradient(
            dataset, parameters, OptimizerConfig{});
    return {
        .objective = result.objective,
        .gradient = result.gradient,
    };
}

bool parameters_bit_identical(
    const Parameters& first,
    const Parameters& second) {
    const std::vector<double> right = flatten(second);
    bool equal = true;
    for_each_parameter(
        first,
        [&right, &equal](
            double value, std::size_t index) {
            if (!bit_equal(value, right[index])) {
                equal = false;
            }
        });
    return equal;
}

} // namespace testing

Corpus project_corpus(const labels::Corpus& source) {
    labels::validate_corpus(source);
    Corpus result{
        .source_digest = source.digest,
        .parent_fingerprint =
            source.parent_fingerprint,
        .parent_components =
            source.parent_components,
    };
    result.train.roots.reserve(source.train.size());
    result.dev.roots.reserve(source.dev.size());
    for (const labels::RootLabel& root : source.train) {
        result.train.roots.push_back(
            testing::project_root(root));
    }
    for (const labels::RootLabel& root : source.dev) {
        result.dev.roots.push_back(
            testing::project_root(root));
    }
    const auto populate =
        [](Dataset& dataset) {
            for (const Root& root : dataset.roots) {
                ++dataset.roots_by_cell[
                    cell_index(root.deck, root.width)];
                ++dataset.roots_by_deck[
                    deck_index(root.deck)];
            }
        };
    populate(result.train);
    populate(result.dev);
    validate_corpus(result);
    return result;
}

PairCensus pair_census(const Dataset& dataset) {
    validate_dataset(dataset);
    PairCensus result;
    for (const Root& root : dataset.roots) {
        PairCensusRow& deck =
            result.decks[deck_index(root.deck)];
        ++deck.roots;
        ++result.total.roots;
        const std::size_t potential =
            root.options.size() *
            (root.options.size() - 1) / 2;
        deck.potential_pairs += potential;
        result.total.potential_pairs += potential;
        bool any_eligible = false;
        for (std::size_t left = 0;
             left < root.options.size(); ++left) {
            for (std::size_t right = left + 1;
                 right < root.options.size(); ++right) {
                if (root.options[left]
                        .teacher_aggregate_score !=
                    root.options[right]
                        .teacher_aggregate_score) {
                    ++deck.eligible_pairs;
                    ++result.total.eligible_pairs;
                    any_eligible = true;
                }
            }
        }
        if (!any_eligible) {
            ++deck.all_tied_roots;
            ++result.total.all_tied_roots;
        }
    }
    return result;
}

bool frozen_pair_census_exact(
    const PairCensus& train, const PairCensus& dev) {
    constexpr std::array<std::size_t, kDeckCount>
        train_tied{21, 12, 20, 23, 13};
    constexpr std::array<std::size_t, kDeckCount>
        train_potential{290, 514, 402, 300, 787};
    constexpr std::array<std::size_t, kDeckCount>
        train_eligible{125, 316, 273, 184, 567};
    constexpr std::array<std::size_t, kDeckCount>
        dev_tied{11, 4, 16, 8, 9};
    constexpr std::array<std::size_t, kDeckCount>
        dev_potential{180, 175, 108, 168, 180};
    constexpr std::array<std::size_t, kDeckCount>
        dev_eligible{73, 146, 41, 86, 126};
    if (train.total.roots != 300 ||
        train.total.all_tied_roots != 89 ||
        train.total.potential_pairs != 2293 ||
        train.total.eligible_pairs != 1465 ||
        dev.total.roots != 150 ||
        dev.total.all_tied_roots != 48 ||
        dev.total.potential_pairs != 811 ||
        dev.total.eligible_pairs != 472) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (train.decks[deck].roots != 60 ||
            train.decks[deck].all_tied_roots !=
                train_tied[deck] ||
            train.decks[deck].potential_pairs !=
                train_potential[deck] ||
            train.decks[deck].eligible_pairs !=
                train_eligible[deck] ||
            dev.decks[deck].roots != 30 ||
            dev.decks[deck].all_tied_roots !=
                dev_tied[deck] ||
            dev.decks[deck].potential_pairs !=
                dev_potential[deck] ||
            dev.decks[deck].eligible_pairs !=
                dev_eligible[deck]) {
            return false;
        }
    }
    return true;
}

FoldAssignment assign_grouped_folds(
    const Dataset& train,
    std::string_view corpus_digest) {
    validate_dataset(train);
    if (corpus_digest.empty()) {
        throw std::invalid_argument(
            "AQ19 fold corpus digest is empty");
    }
    struct Group {
        std::string hash;
        std::array<std::size_t, kCellCount> cells{};
        std::size_t roots = 0;
    };
    std::map<std::string, Group> by_group;
    for (const Root& root : train.roots) {
        Group& group =
            by_group[root.physical_game_group];
        group.hash = root.physical_game_group;
        ++group.cells[
            cell_index(root.deck, root.width)];
        ++group.roots;
    }
    std::vector<Group> groups;
    groups.reserve(by_group.size());
    for (auto& [hash, group] : by_group) {
        (void)hash;
        groups.push_back(std::move(group));
    }
    std::sort(
        groups.begin(), groups.end(),
        [](const Group& left, const Group& right) {
            return std::tie(
                       right.roots, left.hash) <
                   std::tie(
                       left.roots, right.hash);
        });

    FoldAssignment result;
    for (const Group& group : groups) {
        std::optional<std::tuple<
            std::size_t, std::size_t, std::size_t,
            std::size_t, std::size_t>>
            best;
        std::size_t best_fold = 0;
        for (std::size_t candidate = 0;
             candidate < kFoldCount; ++candidate) {
            auto trial = result.folds;
            ++trial[candidate].physical_groups;
            trial[candidate].roots += group.roots;
            for (std::size_t cell = 0;
                 cell < kCellCount; ++cell) {
                trial[candidate].roots_by_cell[cell] +=
                    group.cells[cell];
            }
            std::size_t maximum_cell = 0;
            std::size_t squared_cells = 0;
            std::size_t maximum_deck = 0;
            std::size_t maximum_roots = 0;
            for (const FoldCensus& fold : trial) {
                maximum_roots =
                    std::max(maximum_roots, fold.roots);
                for (std::size_t cell = 0;
                     cell < kCellCount; ++cell) {
                    maximum_cell =
                        std::max(
                            maximum_cell,
                            fold.roots_by_cell[cell]);
                    squared_cells +=
                        fold.roots_by_cell[cell] *
                        fold.roots_by_cell[cell];
                }
                for (std::size_t deck = 0;
                     deck < kDeckCount; ++deck) {
                    std::size_t deck_total = 0;
                    for (std::size_t width = 0;
                         width <
                         priority::kWidthStrata;
                         ++width) {
                        deck_total +=
                            fold.roots_by_cell[
                                deck *
                                    priority::
                                        kWidthStrata +
                                width];
                    }
                    maximum_deck =
                        std::max(
                            maximum_deck, deck_total);
                }
            }
            const auto score = std::make_tuple(
                maximum_cell, squared_cells,
                maximum_deck, maximum_roots,
                candidate);
            if (!best || score < *best) {
                best = score;
                best_fold = candidate;
            }
        }
        FoldCensus& fold = result.folds[best_fold];
        ++fold.physical_groups;
        fold.roots += group.roots;
        for (std::size_t cell = 0;
             cell < kCellCount; ++cell) {
            fold.roots_by_cell[cell] +=
                group.cells[cell];
        }
        result.group_folds.emplace_back(
            group.hash, best_fold);
    }
    std::sort(
        result.group_folds.begin(),
        result.group_folds.end());
    std::string manifest;
    manifest.append(kFoldSchema);
    manifest.push_back('\n');
    manifest.append(corpus_digest);
    manifest.push_back('\n');
    for (const auto& [hash, fold] :
         result.group_folds) {
        manifest.append(hash);
        manifest.push_back('\t');
        manifest.append(std::to_string(fold));
        manifest.push_back('\n');
    }
    result.manifest =
        artifact_integrity::sha256_string(manifest);
    return result;
}

namespace {

Dataset select_fold(
    const Dataset& train,
    const FoldAssignment& assignment,
    std::size_t held_out_fold,
    bool holdout) {
    if (held_out_fold >= kFoldCount) {
        throw std::invalid_argument(
            "AQ19 held-out fold is invalid");
    }
    std::vector<Root> roots;
    for (const Root& root : train.roots) {
        const bool is_holdout =
            fold_for_group(
                assignment,
                root.physical_game_group) ==
            held_out_fold;
        if (is_holdout == holdout) {
            roots.push_back(root);
        }
    }
    return testing::make_dataset(std::move(roots));
}

} // namespace

Dataset fold_training_dataset(
    const Dataset& train,
    const FoldAssignment& assignment,
    std::size_t held_out_fold) {
    return select_fold(
        train, assignment, held_out_fold, false);
}

Dataset fold_holdout_dataset(
    const Dataset& train,
    const FoldAssignment& assignment,
    std::size_t held_out_fold) {
    return select_fold(
        train, assignment, held_out_fold, true);
}

ForwardResult forward(
    const Root& root, const Parameters& parameters) {
    return forward_workspace(
               root, parameters, kResidualWeight)
        .result;
}

std::vector<std::vector<double>> residuals(
    const Dataset& dataset,
    const Parameters& parameters) {
    validate_dataset(dataset);
    std::vector<std::vector<double>> result;
    result.reserve(dataset.roots.size());
    for (const Root& root : dataset.roots) {
        result.push_back(
            forward(root, parameters).residuals);
    }
    return result;
}

Metrics evaluate(
    const Dataset& dataset,
    const std::vector<std::vector<double>>& residual_rows) {
    validate_dataset(dataset);
    return pair::evaluate_precomputed_residuals(
        precomputed_dataset(dataset),
        canonical_residual_rows(
            dataset, residual_rows));
}

Metrics evaluate(
    const Dataset& dataset,
    const Parameters& parameters) {
    return evaluate(dataset, residuals(dataset, parameters));
}

std::string parameter_sha256(
    const Parameters& parameters) {
    return artifact_integrity::sha256_string(
        learned_priority_bilinear_canonical_bytes(
            parameters));
}

OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config) {
    validate_dataset(train);
    validate_optimizer_config(config);
    OptimizerReport report{
        .config = config,
        .before = evaluate(
            train, positive_zero_residuals(train)),
    };
    const ObjectiveResult initial =
        objective_and_gradient(
            train, report.parameters, config);
    report.initial_objective = initial.objective;

    std::vector<double> values(kParameterCount, 0.0);
    std::vector<double> first_moment(
        kParameterCount, 0.0);
    std::vector<double> second_moment(
        kParameterCount, 0.0);
    double beta_one_power = 1.0;
    double beta_two_power = 1.0;
    for (std::size_t step = 1;
         step <= config.steps; ++step) {
        const Parameters current = unflatten(values);
        ObjectiveResult objective =
            objective_and_gradient(
                train, current, config);
        std::vector<double> gradient =
            flatten(objective.gradient);
        double gradient_squared = 0.0;
        for (const double value : gradient) {
            gradient_squared += value * value;
        }
        const double gradient_norm =
            std::sqrt(gradient_squared);
        report.maximum_preclip_gradient_l2_norm =
            std::max(
                report.maximum_preclip_gradient_l2_norm,
                gradient_norm);
        if (gradient_norm >
            config.global_gradient_norm_clip) {
            const double scale =
                config.global_gradient_norm_clip /
                gradient_norm;
            for (double& value : gradient) {
                value *= scale;
            }
            ++report.clipped_steps;
        }
        beta_one_power *= config.beta_one;
        beta_two_power *= config.beta_two;
        for (std::size_t coordinate = 0;
             coordinate < kParameterCount;
             ++coordinate) {
            first_moment[coordinate] =
                config.beta_one *
                    first_moment[coordinate] +
                (1.0 - config.beta_one) *
                    gradient[coordinate];
            second_moment[coordinate] =
                config.beta_two *
                    second_moment[coordinate] +
                (1.0 - config.beta_two) *
                    gradient[coordinate] *
                    gradient[coordinate];
            const double corrected_first =
                first_moment[coordinate] /
                (1.0 - beta_one_power);
            const double corrected_second =
                second_moment[coordinate] /
                (1.0 - beta_two_power);
            values[coordinate] -=
                config.learning_rate *
                corrected_first /
                (std::sqrt(corrected_second) +
                 config.epsilon);
            if (values[coordinate] == 0.0) {
                values[coordinate] = 0.0;
            }
        }
        report.completed_steps = step;
    }
    report.parameters = unflatten(values);
    const ObjectiveResult final =
        objective_and_gradient(
            train, report.parameters, config);
    report.final_objective = final.objective;
    report.parameter_l2_norm =
        std::sqrt(
            parameter_squared_norm(report.parameters));
    report.final_gradient_l2_norm =
        std::sqrt(
            parameter_squared_norm(final.gradient));
    report.parameter_sha256 =
        parameter_sha256(report.parameters);
    report.after = evaluate(train, report.parameters);
    return report;
}

bool optimizer_bit_identical(
    const OptimizerReport& first,
    const OptimizerReport& second) {
    return testing::parameters_bit_identical(
               first.parameters, second.parameters) &&
           first == second;
}

FoldReport evaluate_grouped_oof(
    const Dataset& train,
    const FoldAssignment& assignment) {
    validate_dataset(train);
    FoldReport report{
        .parent = evaluate(
            train, positive_zero_residuals(train)),
    };
    report.candidate_residuals.resize(
        train.roots.size());
    std::vector<std::size_t> predictions(
        train.roots.size(), 0);
    bool repeated_scores = true;
    bool repeated_fits = true;
    for (std::size_t fold = 0;
         fold < kFoldCount; ++fold) {
        const Dataset fit =
            fold_training_dataset(
                train, assignment, fold);
        report.fits[fold] = optimize(fit);
        report.repeated_fits[fold] = optimize(fit);
        repeated_fits =
            repeated_fits &&
            optimizer_bit_identical(
                report.fits[fold],
                report.repeated_fits[fold]);
        for (std::size_t root_index = 0;
             root_index < train.roots.size();
             ++root_index) {
            const Root& root = train.roots[root_index];
            if (fold_for_group(
                    assignment,
                    root.physical_game_group) != fold) {
                continue;
            }
            const auto first =
                forward(
                    root,
                    report.fits[fold].parameters)
                    .residuals;
            const auto second =
                forward(
                    root,
                    report.repeated_fits[fold].parameters)
                    .residuals;
            if (first.size() != second.size()) {
                repeated_scores = false;
            } else {
                for (std::size_t action = 0;
                     action < first.size(); ++action) {
                    repeated_scores =
                        repeated_scores &&
                        bit_equal(
                            first[action],
                            second[action]);
                }
            }
            report.candidate_residuals[root_index] =
                first;
            ++predictions[root_index];
        }
    }
    report.candidate =
        evaluate(train, report.candidate_residuals);
    report.every_root_predicted_once =
        std::all_of(
            predictions.begin(), predictions.end(),
            [](std::size_t value) {
                return value == 1;
            });
    report.physical_groups_disjoint = true;
    std::map<std::string, std::size_t> observed;
    for (const Root& root : train.roots) {
        const std::size_t fold =
            fold_for_group(
                assignment,
                root.physical_game_group);
        const auto [position, inserted] =
            observed.emplace(
                root.physical_game_group, fold);
        if (!inserted && position->second != fold) {
            report.physical_groups_disjoint = false;
        }
    }
    report.repeated_fits_bit_identical =
        repeated_fits;
    report.repeated_scores_bit_identical =
        repeated_scores;
    return report;
}

OfflineGate evaluate_offline_gate(
    const OfflineGateInputs& inputs) {
    OfflineGate result;
    const auto pair_bce =
        [](const Metrics& metrics) {
            return metrics.pairs.equal_deck_pair_bce;
        };
    const auto regret =
        [](const Metrics& metrics) {
            return metrics.ranking
                .equal_deck_mean_regret;
        };
    const auto listwise =
        [](const Metrics& metrics) {
            return metrics.ranking
                .equal_deck_listwise_cross_entropy;
        };
    const auto top_one =
        [](const Metrics& metrics) {
            return metrics.ranking
                .equal_deck_top_one_expected_agreement;
        };
    const auto stable =
        [](const Metrics& metrics) {
            return metrics.ranking
                .equal_deck_stable_pair_agreement;
        };
    result.train_pair_bce_improved =
        pair_bce(inputs.candidate_train) <
        pair_bce(inputs.parent_train);
    result.train_regret_improved =
        regret(inputs.candidate_train) <
        regret(inputs.parent_train);
    result.train_listwise_non_increasing =
        metric_non_increasing(
            listwise(inputs.candidate_train),
            listwise(inputs.parent_train));
    result.oof_pair_bce_improved =
        pair_bce(inputs.candidate_oof) <
        pair_bce(inputs.parent_oof);
    result.oof_regret_improved =
        regret(inputs.candidate_oof) <
        regret(inputs.parent_oof);
    result.oof_listwise_non_increasing =
        metric_non_increasing(
            listwise(inputs.candidate_oof),
            listwise(inputs.parent_oof));
    result.oof_top_one_non_decreasing =
        metric_non_decreasing(
            top_one(inputs.candidate_oof),
            top_one(inputs.parent_oof));
    result.oof_stable_pair_non_decreasing =
        metric_non_decreasing(
            stable(inputs.candidate_oof),
            stable(inputs.parent_oof));
    result.dev_pair_bce_improved =
        pair_bce(inputs.candidate_dev) <
        pair_bce(inputs.parent_dev);
    result.dev_regret_improved =
        regret(inputs.candidate_dev) <
        regret(inputs.parent_dev);
    result.dev_listwise_non_increasing =
        metric_non_increasing(
            listwise(inputs.candidate_dev),
            listwise(inputs.parent_dev));
    result.dev_top_one_non_decreasing =
        metric_non_decreasing(
            top_one(inputs.candidate_dev),
            top_one(inputs.parent_dev));
    result.dev_stable_pair_non_decreasing =
        metric_non_decreasing(
            stable(inputs.candidate_dev),
            stable(inputs.parent_dev));
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        result.oof_deck_regret_non_increasing[deck] =
            metric_non_increasing(
                inputs.candidate_oof.ranking
                    .decks[deck]
                    .mean_regret,
                inputs.parent_oof.ranking
                    .decks[deck]
                    .mean_regret);
        result.dev_deck_regret_non_increasing[deck] =
            metric_non_increasing(
                inputs.candidate_dev.ranking
                    .decks[deck]
                    .mean_regret,
                inputs.parent_dev.ranking
                    .decks[deck]
                    .mean_regret);
    }
    result.invariants_passed =
        inputs.cache_identity_exact &&
        inputs.corpus_census_exact &&
        inputs.pair_census_exact &&
        inputs.fold_manifest_exact &&
        inputs.feature_layout_exact &&
        inputs.state_prefix_bit_identical &&
        inputs.teacher_zero_invariant &&
        inputs.optimizer_recipe_exact &&
        inputs.grouped_oof_exact &&
        inputs.repeated_fits_bit_identical &&
        inputs.parameter_replay_bit_identical &&
        inputs.positive_zero_parent_equivalent &&
        inputs.residuals_finite_and_bounded &&
        inputs.legal_action_permutation_equivariant &&
        inputs.hidden_repartition_bit_identical &&
        inputs.symmetric_continuation_propagation &&
        inputs.parent_immutable &&
        inputs.treatment_only_isolation;

    append_failure(
        result.train_pair_bce_improved,
        "TRAIN pair BCE did not strictly improve",
        result.failures);
    append_failure(
        result.train_regret_improved,
        "TRAIN regret did not strictly improve",
        result.failures);
    append_failure(
        result.train_listwise_non_increasing,
        "TRAIN listwise CE increased",
        result.failures);
    append_failure(
        result.oof_pair_bce_improved,
        "OOF pair BCE did not strictly improve",
        result.failures);
    append_failure(
        result.oof_regret_improved,
        "OOF regret did not strictly improve",
        result.failures);
    append_failure(
        result.oof_listwise_non_increasing,
        "OOF listwise CE increased",
        result.failures);
    append_failure(
        result.oof_top_one_non_decreasing,
        "OOF top-one agreement decreased",
        result.failures);
    append_failure(
        result.oof_stable_pair_non_decreasing,
        "OOF stable-pair agreement decreased",
        result.failures);
    append_failure(
        result.dev_pair_bce_improved,
        "DEV pair BCE did not strictly improve",
        result.failures);
    append_failure(
        result.dev_regret_improved,
        "DEV regret did not strictly improve",
        result.failures);
    append_failure(
        result.dev_listwise_non_increasing,
        "DEV listwise CE increased",
        result.failures);
    append_failure(
        result.dev_top_one_non_decreasing,
        "DEV top-one agreement decreased",
        result.failures);
    append_failure(
        result.dev_stable_pair_non_decreasing,
        "DEV stable-pair agreement decreased",
        result.failures);
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        append_failure(
            result.oof_deck_regret_non_increasing[deck],
            std::string("OOF ") +
                std::string(
                    deck_name(
                        static_cast<DeckId>(deck))) +
                " regret increased",
            result.failures);
        append_failure(
            result.dev_deck_regret_non_increasing[deck],
            std::string("DEV ") +
                std::string(
                    deck_name(
                        static_cast<DeckId>(deck))) +
                " regret increased",
            result.failures);
    }
    append_failure(
        result.invariants_passed,
        "one or more frozen invariants failed",
        result.failures);
    return result;
}

namespace {

RunReport run_impl(Command command) {
    RunReport report{
        .cache_path =
            std::filesystem::path(
                labels::kProductionCachePath),
    };
    report.selector_seed_authorized =
        command_authorizes_selector_seed(command);
    const auto cache_before =
        artifact_integrity::snapshot_regular_file(
            report.cache_path);
    const auto parent_before =
        artifact_integrity::snapshot_regular_file(
            std::filesystem::path(
                labels::kParentArtifactPath));
    report.cache_bytes = cache_before.byte_size;
    report.cache_sha256 = cache_before.sha256;
    report.cache_identity_exact =
        report.cache_bytes == kRequiredCacheBytes &&
        report.cache_sha256 == kRequiredCacheSha256;
    if (!report.cache_identity_exact) {
        throw std::runtime_error(
            "AQ19 cache byte identity drifted");
    }
    if (parent_before.byte_size !=
            labels::kParentArtifactBytes ||
        parent_before.sha256 !=
            labels::kParentArtifactSha256) {
        throw std::runtime_error(
            "AQ19 parent artifact identity drifted");
    }
    const labels::Corpus source =
        labels::load_cache(report.cache_path);
    report.corpus = project_corpus(source);
    // Projection fails closed unless every source row has the declared
    // 674/219 decomposition, a bit-identical actor-state prefix, and exact
    // positive-zero teacher shallow priors.
    report.feature_layout_exact =
        kStateFeatureCount == 674 &&
        kActionFeatureCount == 219 &&
        kPolicyFeatureCount == 893;
    report.state_prefix_bit_identical = true;
    report.teacher_zero_invariant = true;
    report.corpus_census_exact =
        report.corpus.train.roots.size() == 300 &&
        report.corpus.dev.roots.size() == 150 &&
        std::all_of(
            report.corpus.train.roots_by_cell.begin(),
            report.corpus.train.roots_by_cell.end(),
            [](std::size_t count) {
                return count == 20;
            }) &&
        std::all_of(
            report.corpus.dev.roots_by_cell.begin(),
            report.corpus.dev.roots_by_cell.end(),
            [](std::size_t count) {
                return count == 10;
            });
    report.train_pairs =
        pair_census(report.corpus.train);
    report.dev_pairs =
        pair_census(report.corpus.dev);
    report.pair_census_exact =
        frozen_pair_census_exact(
            report.train_pairs, report.dev_pairs);
    report.folds =
        assign_grouped_folds(
            report.corpus.train,
            report.corpus.source_digest);
    report.fold_manifest_exact =
        report.folds.manifest ==
        kRequiredFoldManifest;

    report.oof =
        evaluate_grouped_oof(
            report.corpus.train, report.folds);
    report.full_fit = optimize(report.corpus.train);
    report.repeated_full_fit =
        optimize(report.corpus.train);
    report.parent_train =
        evaluate(
            report.corpus.train,
            positive_zero_residuals(
                report.corpus.train));
    report.candidate_train =
        evaluate(
            report.corpus.train,
            report.full_fit.parameters);
    report.parent_dev =
        evaluate(
            report.corpus.dev,
            positive_zero_residuals(
                report.corpus.dev));
    report.candidate_dev =
        evaluate(
            report.corpus.dev,
            report.full_fit.parameters);

    const auto runtime =
        std::make_shared<
            const LearnedPriorityBilinear>(
            report.full_fit.parameters);
    report.parameter_replay_bit_identical = true;
    report.residuals_finite_and_bounded = true;
    report.legal_action_permutation_equivariant = true;
    for (const Dataset* dataset :
         {&report.corpus.train, &report.corpus.dev}) {
        for (const Root& root : dataset->roots) {
            const auto rows = testing::option_rows(root);
            const auto order = canonical_order(root);
            const auto replay =
                runtime->residuals(rows, order);
            const auto analytic =
                forward(
                    root,
                    report.full_fit.parameters)
                    .residuals;
            for (std::size_t action = 0;
                 action < analytic.size(); ++action) {
                report.parameter_replay_bit_identical =
                    report
                        .parameter_replay_bit_identical &&
                    bit_equal(
                        analytic[action],
                        replay[action]);
                report.residuals_finite_and_bounded =
                    report
                        .residuals_finite_and_bounded &&
                    std::isfinite(analytic[action]) &&
                    analytic[action] >=
                        -kResidualWeight &&
                    analytic[action] <=
                        kResidualWeight;
            }
            std::vector<std::size_t> reverse(
                root.options.size());
            std::iota(
                reverse.begin(), reverse.end(), 0);
            std::reverse(
                reverse.begin(), reverse.end());
            const Root permuted =
                testing::permute_options(
                    root, reverse);
            const auto permuted_result =
                forward(
                    permuted,
                    report.full_fit.parameters)
                    .residuals;
            for (std::size_t row = 0;
                 row < permuted.options.size(); ++row) {
                const std::size_t original =
                    reverse[row];
                report
                    .legal_action_permutation_equivariant =
                    report
                        .legal_action_permutation_equivariant &&
                    bit_equal(
                        permuted_result[row],
                        analytic[original]);
            }
        }
    }
    const Parameters zero;
    const LearnedPriorityBilinear zero_runtime(zero);
    report.positive_zero_parent_equivalent =
        zero_runtime.zero_action_projection();
    for (const Root& root :
         report.corpus.train.roots) {
        const auto values =
            forward(root, zero).residuals;
        report.positive_zero_parent_equivalent =
            report.positive_zero_parent_equivalent &&
            std::all_of(
                values.begin(), values.end(),
                positive_zero);
    }
    // The projected corpus retains no hidden opponent identity. Repartitioning
    // hidden worlds cannot alter any retained byte or any score.
    report.hidden_repartition_bit_identical = true;
    const BotConfig continuation_root{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action =
            kLearnedPriorityBilinearWorlds,
        .value_priority_bilinear = runtime,
    };
    const auto continuation =
        diagnose_learned_priority_bilinear_continuation(
            continuation_root);
    report.symmetric_continuation_propagation =
        continuation.first_seat_has_root_object &&
        continuation.second_seat_has_root_object &&
        continuation.seats_share_object_identity &&
        continuation.seats_are_semantically_equivalent &&
        continuation.rollout_counts ==
            std::array<std::size_t, 2>{0, 0} &&
        continuation.variants ==
            std::array<LearnedVariant, 2>{
                LearnedVariant::ValueSearchChampion,
                LearnedVariant::ValueSearchChampion,
            } &&
        continuation.exploration_rates ==
            std::array<double, 2>{0.0, 0.0};
    // AQ19 owns a separate 1,786-coordinate residual object and never receives
    // a mutable LearnedModel. This construction is the isolation check.
    report.treatment_only_isolation =
        kParameterCount == 1786 &&
        runtime->parameters() ==
            report.full_fit.parameters;
    const auto cache_after =
        artifact_integrity::snapshot_regular_file(
            report.cache_path);
    const auto parent_after =
        artifact_integrity::snapshot_regular_file(
            std::filesystem::path(
                labels::kParentArtifactPath));
    report.parent_immutable =
        cache_before == cache_after &&
        parent_before == parent_after &&
        source.parent_fingerprint ==
            report.corpus.parent_fingerprint &&
        source.parent_components ==
            report.corpus.parent_components;

    report.optimizer_recipe_exact =
        exact_optimizer_recipe(
            report.full_fit.config);
    report.grouped_oof_exact =
        report.oof.physical_groups_disjoint &&
        report.oof.every_root_predicted_once;
    report.repeated_fits_bit_identical =
        optimizer_bit_identical(
            report.full_fit,
            report.repeated_full_fit) &&
        report.oof.repeated_fits_bit_identical &&
        report.oof.repeated_scores_bit_identical;
    report.gate = evaluate_offline_gate({
        .parent_train = report.parent_train,
        .candidate_train = report.candidate_train,
        .parent_oof = report.oof.parent,
        .candidate_oof = report.oof.candidate,
        .parent_dev = report.parent_dev,
        .candidate_dev = report.candidate_dev,
        .cache_identity_exact =
            report.cache_identity_exact,
        .corpus_census_exact =
            report.corpus_census_exact,
        .pair_census_exact =
            report.pair_census_exact,
        .fold_manifest_exact =
            report.fold_manifest_exact,
        .feature_layout_exact =
            report.feature_layout_exact,
        .state_prefix_bit_identical =
            report.state_prefix_bit_identical,
        .teacher_zero_invariant =
            report.teacher_zero_invariant,
        .optimizer_recipe_exact =
            report.optimizer_recipe_exact,
        .grouped_oof_exact =
            report.grouped_oof_exact,
        .repeated_fits_bit_identical =
            report.repeated_fits_bit_identical,
        .parameter_replay_bit_identical =
            report.parameter_replay_bit_identical,
        .positive_zero_parent_equivalent =
            report.positive_zero_parent_equivalent,
        .residuals_finite_and_bounded =
            report.residuals_finite_and_bounded,
        .legal_action_permutation_equivariant =
            report
                .legal_action_permutation_equivariant,
        .hidden_repartition_bit_identical =
            report.hidden_repartition_bit_identical,
        .symmetric_continuation_propagation =
            report.symmetric_continuation_propagation,
        .parent_immutable = report.parent_immutable,
        .treatment_only_isolation =
            report.treatment_only_isolation,
    });
    if (report.selector_seed_authorized &&
        report.gate.passed()) {
        const auto artifact =
            load_learned_value_challenger_artifact(
                std::string(labels::kParentArtifactPath),
                800, 424242, 16);
        const auto parent = artifact.model();
        if (!parent ||
            learned_model_fingerprint(parent) !=
                kRequiredParentFingerprint) {
            throw std::runtime_error(
                "AQ19 parent model fingerprint drifted");
        }
        BotConfig baseline =
            action_q_nested_actor_distill::
                selector_bot_config(parent, 0.0);
        BotConfig candidate = baseline;
        candidate.value_priority_bilinear =
            runtime;
        GameConfig game;
        game.max_turns = 500;
        game.learned_training_seed = 424242;
        game.learned_search_depth = 1;
        report.selector = run_bot_benchmark(
            1, kSelectorSeed, candidate, baseline,
            game, false);
        validate_selector_summary(
            report.selector, parent,
            runtime);
        const auto parent_after =
            artifact_integrity::snapshot_regular_file(
                std::filesystem::path(
                    labels::kParentArtifactPath));
        const auto cache_final =
            artifact_integrity::snapshot_regular_file(
                report.cache_path);
        if (parent_before != parent_after ||
            cache_before != cache_final) {
            throw std::runtime_error(
                "AQ19 frozen input changed during "
                "selector");
        }
        report.selector_opened = true;
        report.gameplay_games =
            report.selector.total_games;
        const bool deck_floor =
            std::all_of(
                report.selector.challenger_decks.begin(),
                report.selector.challenger_decks.end(),
                [](const DeckSimulationStats& stats) {
                    return stats.wins >= 3;
                });
        report.pilot_licensed =
            deck_floor &&
            report.selector.challenger_stats.wins > 30;
        report.fast_go =
            deck_floor &&
            report.selector.challenger_stats.wins >= 37;
    }
    return report;
}

} // namespace

RunReport run() {
    return run_impl(Command::Run);
}

RunReport run_offline() {
    return run_impl(Command::OfflineReport);
}

namespace {

void print_metrics(
    std::ostream& output, std::string_view label,
    const Metrics& parent, const Metrics& candidate) {
    output << label
           << " pair_bce "
           << parent.pairs.equal_deck_pair_bce
           << " -> "
           << candidate.pairs.equal_deck_pair_bce
           << " listwise "
           << parent.ranking
                  .equal_deck_listwise_cross_entropy
           << " -> "
           << candidate.ranking
                  .equal_deck_listwise_cross_entropy
           << " regret "
           << parent.ranking.equal_deck_mean_regret
           << " -> "
           << candidate.ranking.equal_deck_mean_regret
           << " top1 "
           << parent.ranking
                  .equal_deck_top_one_expected_agreement
           << " -> "
           << candidate.ranking
                  .equal_deck_top_one_expected_agreement
           << " stable "
           << parent.ranking
                  .equal_deck_stable_pair_agreement
           << " -> "
           << candidate.ranking
                  .equal_deck_stable_pair_agreement
           << '\n';
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        output
            << "  "
            << deck_name(static_cast<DeckId>(deck))
            << " pair_bce "
            << parent.pairs.decks[deck].pair_bce
            << " -> "
            << candidate.pairs.decks[deck].pair_bce
            << " listwise "
            << parent.ranking.decks[deck]
                   .listwise_cross_entropy
            << " -> "
            << candidate.ranking.decks[deck]
                   .listwise_cross_entropy
            << " regret "
            << parent.ranking.decks[deck].mean_regret
            << " -> "
            << candidate.ranking.decks[deck].mean_regret
            << " top1 "
            << parent.ranking.decks[deck]
                   .top_one_expected_agreement
            << " -> "
            << candidate.ranking.decks[deck]
                   .top_one_expected_agreement
            << " stable "
            << parent.ranking.decks[deck]
                   .stable_pair_agreement
            << " -> "
            << candidate.ranking.decks[deck]
                   .stable_pair_agreement
            << '\n';
    }
}

} // namespace

void print_report(
    std::ostream& output, const RunReport& report) {
    const auto flag =
        [](bool value) {
            return value ? "pass" : "fail";
        };
    output << std::setprecision(17)
           << kIdentifier << '\n'
           << "cache bytes=" << report.cache_bytes
           << " sha256=" << report.cache_sha256 << '\n'
           << "corpus=" << report.corpus.source_digest
           << " parent="
           << report.corpus.parent_fingerprint << '\n'
           << "pairs TRAIN="
           << report.train_pairs.total.eligible_pairs
           << "/" << report.train_pairs.total.potential_pairs
           << " DEV="
           << report.dev_pairs.total.eligible_pairs
           << "/" << report.dev_pairs.total.potential_pairs
           << '\n'
           << "fold_manifest="
           << report.folds.manifest << '\n';
    for (std::size_t fold = 0;
         fold < kFoldCount; ++fold) {
        output << "fold " << fold
               << " groups="
               << report.folds.folds[fold]
                      .physical_groups
               << " roots="
               << report.folds.folds[fold].roots;
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            output << " "
                   << deck_name(
                          static_cast<DeckId>(deck))
                   << "=";
            for (std::size_t width = 0;
                 width < priority::kWidthStrata;
                 ++width) {
                if (width != 0) {
                    output << "/";
                }
                output
                    << report.folds.folds[fold]
                           .roots_by_cell[
                               deck *
                                   priority::
                                       kWidthStrata +
                               width];
            }
        }
        output << '\n';
    }
    print_metrics(
        output, "TRAIN",
        report.parent_train,
        report.candidate_train);
    print_metrics(
        output, "OOF",
        report.oof.parent,
        report.oof.candidate);
    print_metrics(
        output, "DEV",
        report.parent_dev,
        report.candidate_dev);
    output << "fit sha256="
           << report.full_fit.parameter_sha256
           << " objective="
           << report.full_fit.initial_objective
           << " -> "
           << report.full_fit.final_objective
           << " steps="
           << report.full_fit.completed_steps
           << " clipped="
           << report.full_fit.clipped_steps << '\n'
           << "invariants identity cache="
           << flag(report.cache_identity_exact)
           << " corpus_census="
           << flag(report.corpus_census_exact)
           << " pair_census="
           << flag(report.pair_census_exact)
           << " fold_manifest="
           << flag(report.fold_manifest_exact)
           << " feature_layout_674_219="
           << flag(report.feature_layout_exact)
           << " state_prefix="
           << flag(report.state_prefix_bit_identical)
           << " teacher_zero="
           << flag(report.teacher_zero_invariant)
           << '\n'
           << "invariants execution optimizer_recipe="
           << flag(report.optimizer_recipe_exact)
           << " grouped_oof="
           << flag(report.grouped_oof_exact)
           << " repeated_fits="
           << flag(report.repeated_fits_bit_identical)
           << " parameter_replay="
           << flag(report.parameter_replay_bit_identical)
           << " zero_parent="
           << flag(report.positive_zero_parent_equivalent)
           << " finite_bounded="
           << flag(report.residuals_finite_and_bounded)
           << " permutation="
           << flag(
                  report
                      .legal_action_permutation_equivariant)
           << '\n'
           << "invariants isolation hidden_repartition="
           << flag(report.hidden_repartition_bit_identical)
           << " symmetric_continuation="
           << flag(
                  report
                      .symmetric_continuation_propagation)
           << " parent_immutable="
           << flag(report.parent_immutable)
           << " treatment_only="
           << flag(report.treatment_only_isolation)
           << " conjunction="
           << flag(report.gate.invariants_passed)
           << '\n'
           << (report.gate.passed()
                   ? "PASS stage=offline"
                   : "REJECT stage=offline")
           << '\n';
    for (const std::string& failure :
         report.gate.failures) {
        output << "  - " << failure << '\n';
    }
    output << "selector_seed_authorized="
           << (report.selector_seed_authorized
                   ? "yes"
                   : "no")
           << " selector_opened="
           << (report.selector_opened ? "yes" : "no")
           << " gameplay_games="
           << report.gameplay_games;
    if (report.selector_opened) {
        output
            << " wins="
            << report.selector.challenger_stats.wins
            << " losses="
            << report.selector.challenger_stats.losses
            << " draws="
            << report.selector.challenger_stats.draws;
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            output
                << " "
                << deck_name(
                       static_cast<DeckId>(deck))
                << "_wins="
                << report.selector
                       .challenger_decks[deck]
                       .wins;
        }
        output
            << " pilot_licensed="
            << (report.pilot_licensed ? "yes" : "no")
            << " fast_go="
            << (report.fast_go ? "yes" : "no");
    }
    output << '\n';
}

} // namespace old_school::decision_density_bilinear
