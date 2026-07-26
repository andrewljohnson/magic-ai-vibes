#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace old_school::learned_iteration {
namespace {

std::uint64_t mix_seed(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value =
        (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value =
        (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

bool valid_probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

void validate_probability_distribution(
    std::span<const double> probabilities) {
    if (probabilities.empty()) {
        throw std::invalid_argument(
            "probability distribution must be nonempty");
    }
    long double total = 0.0L;
    for (const double probability : probabilities) {
        if (!valid_probability(probability)) {
            throw std::invalid_argument(
                "probability distribution entries must be finite "
                "and in [0, 1]");
        }
        total += static_cast<long double>(probability);
    }
    constexpr long double kNormalizationTolerance = 1.0e-9L;
    if (std::abs(total - 1.0L) >
        kNormalizationTolerance) {
        throw std::invalid_argument(
            "probability distribution must sum to one");
    }
}

void validate_p16_mechanism_observation(
    const P16MechanismObservation& observation) {
    const std::size_t action_count =
        observation.parent_combined_scores.size();
    if (action_count == 0 ||
        observation.candidate_combined_scores.size() !=
            action_count ||
        observation.candidate_centered_policy_logits.size() !=
            action_count ||
        observation.target_probabilities.size() !=
            action_count) {
        throw std::invalid_argument(
            "P16 mechanism vectors must have one shared nonzero size");
    }
    if (observation.chosen >= action_count) {
        throw std::invalid_argument(
            "P16 mechanism chosen action is out of range");
    }
    if (!std::isfinite(observation.advantage) ||
        observation.advantage < -kP16AdvantageLimit ||
        observation.advantage > kP16AdvantageLimit) {
        throw std::invalid_argument(
            "P16 mechanism advantage is invalid");
    }
    if (!std::isfinite(observation.weight) ||
        observation.weight <= 0.0) {
        throw std::invalid_argument(
            "P16 mechanism weight must be finite and positive");
    }
    const auto finite =
        [](double value) { return std::isfinite(value); };
    if (!std::all_of(
            observation.parent_combined_scores.begin(),
            observation.parent_combined_scores.end(), finite) ||
        !std::all_of(
            observation.candidate_combined_scores.begin(),
            observation.candidate_combined_scores.end(), finite) ||
        !std::all_of(
            observation.candidate_centered_policy_logits.begin(),
            observation.candidate_centered_policy_logits.end(),
            finite)) {
        throw std::invalid_argument(
            "P16 mechanism scores and logits must be finite");
    }
    validate_probability_distribution(
        observation.target_probabilities);
}

double distribution_kl(
    std::span<const double> target,
    std::span<const double> model_distribution) {
    validate_probability_distribution(target);
    validate_probability_distribution(model_distribution);
    if (target.size() != model_distribution.size()) {
        throw std::invalid_argument(
            "P16 mechanism KL distributions must have equal sizes");
    }
    long double divergence = 0.0L;
    for (std::size_t index = 0; index < target.size(); ++index) {
        if (target[index] == 0.0) {
            continue;
        }
        if (model_distribution[index] <= 0.0) {
            throw std::invalid_argument(
                "P16 mechanism model probability must be positive");
        }
        divergence +=
            static_cast<long double>(target[index]) *
            std::log(
                static_cast<long double>(target[index]) /
                static_cast<long double>(
                    model_distribution[index]));
    }
    return static_cast<double>(divergence);
}

bool exact_argmax_sets_differ(
    std::span<const double> first,
    std::span<const double> second) {
    const double first_maximum =
        *std::max_element(first.begin(), first.end());
    const double second_maximum =
        *std::max_element(second.begin(), second.end());
    for (std::size_t index = 0; index < first.size(); ++index) {
        if ((first[index] == first_maximum) !=
            (second[index] == second_maximum)) {
            return true;
        }
    }
    return false;
}

constexpr double kOracleFullRangeCenteredLogitLimit = 12.0;
constexpr std::size_t kOracleProjectionBisections = 96;
constexpr std::size_t kOracleMaximumIterations = 512;
constexpr std::size_t kOracleMaximumBacktracks = 48;
constexpr double kOracleProjectedGradientTolerance = 1.0e-10;
constexpr double kOracleArmijoCoefficient = 1.0e-4;

struct OraclePoint {
    double kl = 0.0;
    std::vector<double> gradient;
};

struct RootOracleSolution {
    double kl = std::numeric_limits<double>::infinity();
    double maximum_abs_squashed_residual = 0.0;
    bool reached_iteration_limit = false;
};

void validate_centered_tanh_oracle_config(
    const CenteredTanhOracleConfig& config) {
    if (!std::isfinite(config.residual_weight) ||
        config.residual_weight <= 0.0 ||
        !std::isfinite(config.policy_temperature) ||
        config.policy_temperature <= 0.0 ||
        !std::isfinite(config.policy_mixture_weight) ||
        config.policy_mixture_weight <= 0.0 ||
        config.policy_mixture_weight >= 1.0 ||
        !std::isfinite(config.saturation_threshold) ||
        config.saturation_threshold <= 0.0 ||
        config.saturation_threshold >= 1.0) {
        throw std::invalid_argument(
            "centered-tanh oracle configuration is invalid");
    }
}

void validate_centered_tanh_oracle_observation(
    const CenteredTanhOracleObservation& observation) {
    if (observation.base_scores.size() < 2 ||
        observation.target_probabilities.size() !=
            observation.base_scores.size() ||
        !std::isfinite(observation.weight) ||
        observation.weight <= 0.0 ||
        !std::all_of(
            observation.base_scores.begin(),
            observation.base_scores.end(),
            [](double score) {
                return std::isfinite(score);
            })) {
        throw std::invalid_argument(
            "centered-tanh oracle observation is invalid");
    }
    validate_probability_distribution(
        observation.target_probabilities);
}

std::vector<double> project_zero_sum_box(
    std::span<const double> values, double limit) {
    if (values.empty() || !std::isfinite(limit) ||
        limit <= 0.0) {
        throw std::invalid_argument(
            "centered-tanh oracle projection is invalid");
    }
    const auto [minimum, maximum] =
        std::minmax_element(values.begin(), values.end());
    double lower = *minimum - limit;
    double upper = *maximum + limit;
    for (std::size_t iteration = 0;
         iteration < kOracleProjectionBisections; ++iteration) {
        const double midpoint =
            lower + 0.5 * (upper - lower);
        long double sum = 0.0L;
        for (const double value : values) {
            sum += static_cast<long double>(
                std::clamp(value - midpoint, -limit, limit));
        }
        if (sum > 0.0L) {
            lower = midpoint;
        } else {
            upper = midpoint;
        }
    }

    const double offset =
        lower + 0.5 * (upper - lower);
    std::vector<double> projected;
    projected.reserve(values.size());
    double sum = 0.0;
    for (const double value : values) {
        projected.push_back(
            std::clamp(value - offset, -limit, limit));
        sum += projected.back();
    }

    // Remove the final few ulps left by the fixed bisection count while
    // respecting the box. This is deterministic and normally touches only
    // the first nonsaturated coordinate.
    double correction = -sum;
    for (double& value : projected) {
        if (correction == 0.0) {
            break;
        }
        const double available =
            correction > 0.0 ? limit - value : value + limit;
        const double magnitude =
            std::min(std::abs(correction), available);
        const double delta =
            std::copysign(magnitude, correction);
        value += delta;
        correction -= delta;
    }
    return projected;
}

std::vector<double> oracle_behavior_distribution(
    std::span<const double> base_scores,
    std::span<const double> centered_logits,
    const CenteredTanhOracleConfig& config,
    std::vector<double>* bare_probabilities = nullptr) {
    if (base_scores.size() != centered_logits.size() ||
        base_scores.empty()) {
        throw std::logic_error(
            "centered-tanh oracle vectors are inconsistent");
    }
    std::vector<double> scaled_scores;
    scaled_scores.reserve(base_scores.size());
    double maximum =
        -std::numeric_limits<double>::infinity();
    for (std::size_t option = 0;
         option < base_scores.size(); ++option) {
        const double scaled =
            (base_scores[option] +
             config.residual_weight *
                 std::tanh(centered_logits[option])) /
            config.policy_temperature;
        scaled_scores.push_back(scaled);
        maximum = std::max(maximum, scaled);
    }

    long double total = 0.0L;
    for (double& scaled : scaled_scores) {
        scaled = std::exp(scaled - maximum);
        total += static_cast<long double>(scaled);
    }
    std::vector<double> bare;
    bare.reserve(scaled_scores.size());
    const double uniform =
        (1.0 - config.policy_mixture_weight) /
        static_cast<double>(scaled_scores.size());
    std::vector<double> behavior;
    behavior.reserve(scaled_scores.size());
    for (const double exponential : scaled_scores) {
        const double probability =
            static_cast<double>(
                static_cast<long double>(exponential) / total);
        bare.push_back(probability);
        behavior.push_back(
            config.policy_mixture_weight * probability +
            uniform);
    }
    if (bare_probabilities != nullptr) {
        *bare_probabilities = std::move(bare);
    }
    return behavior;
}

OraclePoint evaluate_oracle_point(
    const CenteredTanhOracleObservation& observation,
    std::span<const double> centered_logits,
    const CenteredTanhOracleConfig& config,
    bool with_gradient) {
    std::vector<double> bare;
    const auto behavior =
        oracle_behavior_distribution(
            observation.base_scores, centered_logits, config,
            with_gradient ? &bare : nullptr);
    OraclePoint point;
    point.kl = distribution_kl(
        observation.target_probabilities, behavior);
    if (!with_gradient) {
        return point;
    }

    long double ratio_sum = 0.0L;
    for (std::size_t option = 0;
         option < bare.size(); ++option) {
        ratio_sum +=
            static_cast<long double>(bare[option]) *
            static_cast<long double>(
                observation.target_probabilities[option]) /
            static_cast<long double>(behavior[option]);
    }
    point.gradient.reserve(bare.size());
    for (std::size_t option = 0;
         option < bare.size(); ++option) {
        const double squashed =
            std::tanh(centered_logits[option]);
        point.gradient.push_back(
            config.policy_mixture_weight * bare[option] *
            (static_cast<double>(ratio_sum) -
             observation.target_probabilities[option] /
                 behavior[option]) *
            config.residual_weight /
            config.policy_temperature *
            (1.0 - squashed * squashed));
    }
    return point;
}

void append_unique_oracle_start(
    std::vector<std::vector<double>>& starts,
    std::vector<double> start, double limit) {
    start = project_zero_sum_box(start, limit);
    if (std::find(starts.begin(), starts.end(), start) ==
        starts.end()) {
        starts.push_back(std::move(start));
    }
}

std::vector<std::vector<double>> oracle_starts(
    const CenteredTanhOracleObservation& observation,
    const CenteredTanhOracleConfig& config,
    double limit) {
    const std::size_t count =
        observation.base_scores.size();
    std::vector<std::vector<double>> starts;
    append_unique_oracle_start(
        starts, std::vector<double>(count, 0.0), limit);

    const std::vector<double> zero(count, 0.0);
    const auto parent =
        oracle_behavior_distribution(
            observation.base_scores, zero, config);
    std::vector<double> log_ratio;
    log_ratio.reserve(count);
    constexpr double kProbabilityFloor =
        std::numeric_limits<double>::min();
    for (std::size_t option = 0; option < count; ++option) {
        log_ratio.push_back(
            std::log(std::max(
                observation.target_probabilities[option],
                kProbabilityFloor)) -
            std::log(parent[option]));
    }
    for (const double scale :
         std::array<double, 3>{0.5, 1.0, 2.0}) {
        auto directed = log_ratio;
        for (double& value : directed) {
            value *= scale;
        }
        append_unique_oracle_start(
            starts, std::move(directed), limit);
    }

    const std::array<double, 2> amplitudes = {
        std::min(2.0, limit),
        limit,
    };
    for (const double amplitude : amplitudes) {
        for (std::size_t focus = 0; focus < count; ++focus) {
            std::vector<double> positive(
                count,
                -amplitude /
                    static_cast<double>(count - 1));
            positive[focus] = amplitude;
            append_unique_oracle_start(
                starts, positive, limit);
            for (double& value : positive) {
                value = -value;
            }
            append_unique_oracle_start(
                starts, std::move(positive), limit);
        }
    }
    return starts;
}

RootOracleSolution solve_root_oracle(
    const CenteredTanhOracleObservation& observation,
    const CenteredTanhOracleConfig& config,
    double centered_logit_limit) {
    RootOracleSolution best;
    for (std::vector<double> centered :
         oracle_starts(
             observation, config, centered_logit_limit)) {
        OraclePoint point =
            evaluate_oracle_point(
                observation, centered, config, true);
        bool reached_iteration_limit = true;
        for (std::size_t iteration = 0;
             iteration < kOracleMaximumIterations; ++iteration) {
            std::vector<double> gradient_step(centered.size());
            for (std::size_t option = 0;
                 option < centered.size(); ++option) {
                gradient_step[option] =
                    centered[option] - point.gradient[option];
            }
            const auto projected_gradient_step =
                project_zero_sum_box(
                    gradient_step, centered_logit_limit);
            double projected_gradient_squared_norm = 0.0;
            for (std::size_t option = 0;
                 option < centered.size(); ++option) {
                const double difference =
                    projected_gradient_step[option] -
                    centered[option];
                projected_gradient_squared_norm +=
                    difference * difference;
            }
            if (std::sqrt(projected_gradient_squared_norm) <=
                kOracleProjectedGradientTolerance) {
                reached_iteration_limit = false;
                break;
            }

            bool accepted = false;
            double step = 1.0;
            for (std::size_t backtrack = 0;
                 backtrack < kOracleMaximumBacktracks;
                 ++backtrack) {
                std::vector<double> proposed(centered.size());
                for (std::size_t option = 0;
                     option < centered.size(); ++option) {
                    proposed[option] =
                        centered[option] -
                        step * point.gradient[option];
                }
                proposed = project_zero_sum_box(
                    proposed, centered_logit_limit);
                double directional_derivative = 0.0;
                for (std::size_t option = 0;
                     option < centered.size(); ++option) {
                    directional_derivative +=
                        point.gradient[option] *
                        (proposed[option] - centered[option]);
                }
                const OraclePoint candidate =
                    evaluate_oracle_point(
                        observation, proposed, config, false);
                if (candidate.kl <=
                    point.kl +
                        kOracleArmijoCoefficient *
                            directional_derivative) {
                    centered = std::move(proposed);
                    point = evaluate_oracle_point(
                        observation, centered, config, true);
                    accepted = true;
                    break;
                }
                step *= 0.5;
            }
            if (!accepted) {
                reached_iteration_limit = false;
                break;
            }
        }

        if (point.kl < best.kl) {
            best.kl = point.kl;
            best.maximum_abs_squashed_residual = 0.0;
            for (const double value : centered) {
                best.maximum_abs_squashed_residual =
                    std::max(
                        best.maximum_abs_squashed_residual,
                        std::abs(std::tanh(value)));
            }
            best.reached_iteration_limit =
                reached_iteration_limit;
        }
    }
    if (!std::isfinite(best.kl)) {
        throw std::logic_error(
            "centered-tanh oracle found no finite solution");
    }
    return best;
}

double binary_kl(double target, double model) {
    long double divergence = 0.0L;
    if (target > 0.0) {
        divergence +=
            static_cast<long double>(target) *
            std::log(
                static_cast<long double>(target) /
                static_cast<long double>(model));
    }
    if (target < 1.0) {
        divergence +=
            static_cast<long double>(1.0 - target) *
            std::log(
                static_cast<long double>(1.0 - target) /
                static_cast<long double>(1.0 - model));
    }
    return static_cast<double>(divergence);
}

double certified_root_kl_lower_bound(
    const CenteredTanhOracleObservation& observation,
    const CenteredTanhOracleConfig& config,
    double maximum_abs_squashed_residual) {
    const std::vector<double> zero(
        observation.base_scores.size(), 0.0);
    std::vector<double> bare;
    static_cast<void>(
        oracle_behavior_distribution(
            observation.base_scores, zero, config, &bare));
    const double exponent =
        std::exp(
            2.0 * config.residual_weight *
            maximum_abs_squashed_residual /
            config.policy_temperature);
    const double uniform =
        (1.0 - config.policy_mixture_weight) /
        static_cast<double>(bare.size());
    double lower_bound = 0.0;
    for (std::size_t option = 0;
         option < bare.size(); ++option) {
        const double probability = bare[option];
        const double minimum_probability =
            probability /
            (probability +
             (1.0 - probability) * exponent);
        const double maximum_probability =
            probability * exponent /
            (probability * exponent +
             (1.0 - probability));
        const double minimum_behavior =
            uniform +
            config.policy_mixture_weight *
                minimum_probability;
        const double maximum_behavior =
            uniform +
            config.policy_mixture_weight *
                maximum_probability;
        const double closest =
            std::clamp(
                observation.target_probabilities[option],
                minimum_behavior, maximum_behavior);
        lower_bound = std::max(
            lower_bound,
            binary_kl(
                observation.target_probabilities[option],
                closest));
    }
    return lower_bound;
}

} // namespace

std::uint64_t derive_seed(
    std::uint64_t root_seed, SeedDomain domain,
    std::uint64_t generation, std::uint64_t index,
    std::uint64_t subindex) noexcept {
    std::uint64_t seed = mix_seed(
        root_seed ^
        mix_seed(static_cast<std::uint64_t>(domain)));
    seed = mix_seed(
        seed ^ mix_seed(
                   generation ^ 0xd1b54a32d192ed03ULL));
    seed = mix_seed(
        seed ^ mix_seed(index ^ 0x94d049bb133111ebULL));
    return mix_seed(
        seed ^ mix_seed(
                   subindex ^ 0xbf58476d1ce4e5b9ULL));
}

std::array<ScheduledGame, kBalancedScheduleGames>
balanced_schedule(std::uint64_t root_seed,
                  std::uint64_t generation,
                  std::uint64_t block_index) {
    constexpr std::array<std::pair<DeckId, DeckId>,
                         kBalancedPairings>
        pairings = {{
            {DeckId::Green, DeckId::Red},
            {DeckId::Green, DeckId::Blue},
            {DeckId::Green, DeckId::White},
            {DeckId::Green, DeckId::RUAggro},
            {DeckId::Red, DeckId::Blue},
            {DeckId::Red, DeckId::White},
            {DeckId::Red, DeckId::RUAggro},
            {DeckId::Blue, DeckId::White},
            {DeckId::Blue, DeckId::RUAggro},
            {DeckId::White, DeckId::RUAggro},
        }};

    std::array<ScheduledGame, kBalancedScheduleGames> games;
    std::size_t schedule_index = 0;
    for (std::size_t pairing_index = 0;
         pairing_index < pairings.size(); ++pairing_index) {
        const auto [first, second] = pairings[pairing_index];
        for (std::size_t orientation = 0;
             orientation < 2; ++orientation) {
            const std::array<DeckId, 2> seats =
                orientation == 0
                    ? std::array<DeckId, 2>{first, second}
                    : std::array<DeckId, 2>{second, first};
            for (std::size_t starting_player = 0;
                 starting_player < 2; ++starting_player) {
                games[schedule_index] = {
                    .schedule_index = schedule_index,
                    .pairing_index = pairing_index,
                    .seat_decks = seats,
                    .starting_player = starting_player,
                    .seed = derive_seed(
                        root_seed, SeedDomain::SelfPlayGame,
                        generation, block_index,
                        schedule_index),
                };
                ++schedule_index;
            }
        }
    }
    return games;
}

double terminal_value_for_perspective(
    int winner, std::size_t perspective) {
    if (perspective >= 2) {
        throw std::invalid_argument(
            "terminal perspective must be 0 or 1");
    }
    if (winner == -1) {
        return 0.5;
    }
    if (winner != 0 && winner != 1) {
        throw std::invalid_argument(
            "terminal winner must be -1, 0, or 1");
    }
    return winner == static_cast<int>(perspective)
               ? 1.0
               : 0.0;
}

std::vector<double> td_lambda_targets(
    std::span<const double> chronological_values,
    double terminal_z, double lambda) {
    if (!valid_probability(terminal_z)) {
        throw std::invalid_argument(
            "terminal value must be a probability");
    }
    if (!std::isfinite(lambda) ||
        lambda < 0.0 || lambda > 1.0) {
        throw std::invalid_argument(
            "TD lambda must be in [0, 1]");
    }
    for (const double value : chronological_values) {
        if (!valid_probability(value)) {
            throw std::invalid_argument(
                "critic values must be probabilities");
        }
    }

    std::vector<double> targets(
        chronological_values.size());
    if (targets.empty()) {
        return targets;
    }
    targets.back() = terminal_z;
    for (std::size_t index = targets.size() - 1;
         index > 0; --index) {
        targets[index - 1] =
            (1.0 - lambda) * chronological_values[index] +
            lambda * targets[index];
    }
    return targets;
}

std::vector<std::size_t> evenly_spaced_retained_indices(
    std::size_t total, std::size_t cap) {
    if (cap == 0) {
        throw std::invalid_argument(
            "evenly spaced retention cap must be positive");
    }

    std::vector<std::size_t> retained;
    retained.reserve(std::min(total, cap));
    if (total <= cap) {
        for (std::size_t index = 0; index < total; ++index) {
            retained.push_back(index);
        }
        return retained;
    }

    // `phase` is (index * cap) modulo total. Since cap < total, the
    // comparison and update below exactly detect the floor-quotient change
    // without evaluating either potentially overflowing product.
    const std::size_t wrap_threshold = total - cap;
    std::size_t phase = 0;
    for (std::size_t index = 0; index < total; ++index) {
        if (phase >= wrap_threshold) {
            retained.push_back(index);
            phase -= wrap_threshold;
        } else {
            phase += cap;
        }
    }
    if (retained.size() != cap) {
        throw std::logic_error(
            "evenly spaced retention count is inconsistent");
    }
    return retained;
}

std::vector<double> p16_exploration_distribution(
    std::span<const double> scores) {
    if (scores.empty()) {
        throw std::invalid_argument(
            "P16 exploration scores must be nonempty");
    }
    for (const double score : scores) {
        if (!std::isfinite(score)) {
            throw std::invalid_argument(
                "P16 exploration scores must be finite");
        }
    }

    const double maximum =
        *std::max_element(scores.begin(), scores.end());
    std::vector<double> probabilities;
    probabilities.reserve(scores.size());
    long double exponential_total = 0.0L;
    for (const double score : scores) {
        const double exponential = std::exp(
            (score - maximum) /
            kP16ExplorationTemperature);
        probabilities.push_back(exponential);
        exponential_total +=
            static_cast<long double>(exponential);
    }
    if (!std::isfinite(exponential_total) ||
        exponential_total <= 0.0L) {
        throw std::logic_error(
            "P16 exploration softmax normalization failed");
    }

    const double uniform =
        (1.0 - kP16ExplorationTeacherWeight) /
        static_cast<double>(scores.size());
    for (double& probability : probabilities) {
        probability =
            kP16ExplorationTeacherWeight *
                static_cast<double>(
                    static_cast<long double>(probability) /
                    exponential_total) +
            uniform;
    }
    return probabilities;
}

P16OutcomeSignal p16_outcome_signal(
    std::span<const double> chronological_baselines,
    double terminal_z, double lambda) {
    const auto lambda_returns =
        td_lambda_targets(
            chronological_baselines, terminal_z, lambda);
    P16OutcomeSignal signal;
    signal.returns.reserve(lambda_returns.size());
    signal.advantages.reserve(lambda_returns.size());
    for (std::size_t index = 0;
         index < lambda_returns.size(); ++index) {
        const double outcome_return =
            (1.0 - kP16TerminalReturnWeight) *
                lambda_returns[index] +
            kP16TerminalReturnWeight * terminal_z;
        signal.returns.push_back(outcome_return);
        signal.advantages.push_back(std::clamp(
            outcome_return - chronological_baselines[index],
            -kP16AdvantageLimit, kP16AdvantageLimit));
    }
    return signal;
}

std::vector<double> p16_all_action_target(
    std::span<const double> behavior_distribution,
    std::size_t chosen, double advantage) {
    validate_probability_distribution(
        behavior_distribution);
    if (chosen >= behavior_distribution.size()) {
        throw std::invalid_argument(
            "P16 chosen action is outside the distribution");
    }
    if (!std::isfinite(advantage) ||
        advantage < -kP16AdvantageLimit ||
        advantage > kP16AdvantageLimit) {
        throw std::invalid_argument(
            "P16 advantage must be finite and in [-0.5, 0.5]");
    }

    std::vector<double> target(
        behavior_distribution.begin(),
        behavior_distribution.end());
    if (advantage == 0.0) {
        return target;
    }

    const double chosen_multiplier =
        std::exp(advantage / kP16AdvantageTemperature);
    target[chosen] *= chosen_multiplier;
    long double total = 0.0L;
    for (const double probability : target) {
        total += static_cast<long double>(probability);
    }
    if (!std::isfinite(total) || total <= 0.0L) {
        throw std::logic_error(
            "P16 all-action target normalization failed");
    }
    for (double& probability : target) {
        probability = static_cast<double>(
            static_cast<long double>(probability) / total);
    }
    return target;
}

P16MechanismMetrics evaluate_p16_mechanism_metrics(
    std::span<const P16MechanismObservation> observations) {
    P16MechanismMetrics metrics;
    metrics.observation_count = observations.size();

    for (const P16MechanismObservation& observation :
         observations) {
        validate_p16_mechanism_observation(observation);
        const auto parent_distribution =
            p16_exploration_distribution(
                observation.parent_combined_scores);
        const auto candidate_distribution =
            p16_exploration_distribution(
                observation.candidate_combined_scores);
        // Keep validation here even though the canonical converter already
        // guarantees these properties: this evaluator is the fail-closed
        // mechanism gate for the generated probabilities.
        validate_probability_distribution(parent_distribution);
        validate_probability_distribution(candidate_distribution);

        metrics.total_weight += observation.weight;
        metrics.parent_kl +=
            observation.weight *
            distribution_kl(
                observation.target_probabilities,
                parent_distribution);
        metrics.candidate_kl +=
            observation.weight *
            distribution_kl(
                observation.target_probabilities,
                candidate_distribution);

        const double parent_maximum =
            *std::max_element(
                observation.parent_combined_scores.begin(),
                observation.parent_combined_scores.end());
        const bool chosen_is_parent_argmax =
            observation.parent_combined_scores
                [observation.chosen] == parent_maximum;
        if (observation.advantage > 0.0) {
            metrics.positive_advantage_weight +=
                observation.weight;
            if (!chosen_is_parent_argmax) {
                metrics.conflict_weight += observation.weight;
            }
        } else if (observation.advantage < 0.0) {
            metrics.negative_advantage_weight +=
                observation.weight;
            if (chosen_is_parent_argmax) {
                metrics.conflict_weight += observation.weight;
            }
        } else {
            metrics.zero_advantage_weight +=
                observation.weight;
        }

        const double chosen_probability_movement =
            candidate_distribution[observation.chosen] -
            parent_distribution[observation.chosen];
        if (observation.advantage >
            kP16MechanismMovementTolerance) {
            metrics.eligible_signed_movement_weight +=
                observation.weight;
            metrics.eligible_positive_movement_weight +=
                observation.weight;
            if (chosen_probability_movement >
                kP16MechanismMovementTolerance) {
                metrics.correct_signed_movement_weight +=
                    observation.weight;
                metrics.correct_positive_movement_weight +=
                    observation.weight;
            }
        } else if (observation.advantage <
                   -kP16MechanismMovementTolerance) {
            metrics.eligible_signed_movement_weight +=
                observation.weight;
            metrics.eligible_negative_movement_weight +=
                observation.weight;
            if (chosen_probability_movement <
                -kP16MechanismMovementTolerance) {
                metrics.correct_signed_movement_weight +=
                    observation.weight;
                metrics.correct_negative_movement_weight +=
                    observation.weight;
            }
        }

        if (exact_argmax_sets_differ(
                observation.parent_combined_scores,
                observation.candidate_combined_scores)) {
            metrics.changed_argmax_weight += observation.weight;
        }

        const double option_weight =
            observation.weight /
            static_cast<double>(
                observation
                    .candidate_centered_policy_logits.size());
        metrics.residual_option_weight += observation.weight;
        metrics.residual_option_count +=
            observation.candidate_centered_policy_logits.size();
        for (const double centered_logit :
             observation.candidate_centered_policy_logits) {
            if (std::abs(std::tanh(centered_logit)) >=
                kP16ResidualSaturationThreshold) {
                ++metrics.saturated_residual_count;
                metrics.saturated_residual_weight +=
                    option_weight;
            }
        }
    }

    if (observations.empty()) {
        return metrics;
    }
    if (!std::isfinite(metrics.total_weight) ||
        metrics.total_weight <= 0.0 ||
        !std::isfinite(metrics.parent_kl) ||
        !std::isfinite(metrics.candidate_kl) ||
        !std::isfinite(metrics.saturated_residual_weight)) {
        throw std::invalid_argument(
            "P16 mechanism aggregate became non-finite");
    }

    metrics.parent_kl /= metrics.total_weight;
    metrics.candidate_kl /= metrics.total_weight;
    metrics.kl_reduction_defined =
        metrics.parent_kl >
        kP16MechanismKlDefinedTolerance;
    if (metrics.kl_reduction_defined) {
        metrics.kl_reduction_fraction =
            (metrics.parent_kl - metrics.candidate_kl) /
            metrics.parent_kl;
    }
    if (metrics.eligible_signed_movement_weight > 0.0) {
        metrics.signed_movement_correct_rate =
            metrics.correct_signed_movement_weight /
            metrics.eligible_signed_movement_weight;
    }
    if (metrics.eligible_positive_movement_weight > 0.0) {
        metrics.positive_movement_correct_rate =
            metrics.correct_positive_movement_weight /
            metrics.eligible_positive_movement_weight;
    }
    if (metrics.eligible_negative_movement_weight > 0.0) {
        metrics.negative_movement_correct_rate =
            metrics.correct_negative_movement_weight /
            metrics.eligible_negative_movement_weight;
    }
    metrics.changed_argmax_weight_fraction =
        metrics.changed_argmax_weight / metrics.total_weight;
    metrics.residual_saturation_fraction =
        metrics.saturated_residual_weight /
        metrics.residual_option_weight;
    return metrics;
}

CenteredTanhRootwiseOracleMetrics
evaluate_centered_tanh_rootwise_oracle(
    std::span<const CenteredTanhOracleObservation> observations,
    CenteredTanhOracleConfig config) {
    validate_centered_tanh_oracle_config(config);
    CenteredTanhRootwiseOracleMetrics metrics;
    metrics.observation_count = observations.size();
    if (observations.empty()) {
        return metrics;
    }

    const double zero_saturation_squash_limit =
        std::nextafter(
            std::nextafter(
                config.saturation_threshold, 0.0),
            0.0);
    const double zero_saturation_centered_logit_limit =
        std::atanh(zero_saturation_squash_limit);

    for (const CenteredTanhOracleObservation& observation :
         observations) {
        validate_centered_tanh_oracle_observation(observation);
        const std::vector<double> zero(
            observation.base_scores.size(), 0.0);
        const auto parent_distribution =
            oracle_behavior_distribution(
                observation.base_scores, zero, config);
        const double parent_kl =
            distribution_kl(
                observation.target_probabilities,
                parent_distribution);
        const RootOracleSolution full =
            solve_root_oracle(
                observation, config,
                kOracleFullRangeCenteredLogitLimit);
        const RootOracleSolution zero_saturation =
            solve_root_oracle(
                observation, config,
                zero_saturation_centered_logit_limit);
        const double full_certified_lower =
            certified_root_kl_lower_bound(
                observation, config, 1.0);
        const double zero_saturation_certified_lower =
            certified_root_kl_lower_bound(
                observation, config,
                zero_saturation_squash_limit);

        metrics.total_weight += observation.weight;
        metrics.parent_kl +=
            observation.weight * parent_kl;
        metrics.full_range.numerical_best_kl +=
            observation.weight * full.kl;
        metrics.full_range.certified_kl_lower_bound +=
            observation.weight *
            std::min(parent_kl, full_certified_lower);
        metrics.full_range.maximum_abs_squashed_residual =
            std::max(
                metrics.full_range
                    .maximum_abs_squashed_residual,
                full.maximum_abs_squashed_residual);
        if (full.reached_iteration_limit) {
            ++metrics.full_range
                  .roots_reaching_iteration_limit;
        }

        metrics.zero_saturation.numerical_best_kl +=
            observation.weight * zero_saturation.kl;
        metrics.zero_saturation.certified_kl_lower_bound +=
            observation.weight *
            std::min(
                parent_kl,
                zero_saturation_certified_lower);
        metrics.zero_saturation
            .maximum_abs_squashed_residual =
            std::max(
                metrics.zero_saturation
                    .maximum_abs_squashed_residual,
                zero_saturation
                    .maximum_abs_squashed_residual);
        if (zero_saturation.reached_iteration_limit) {
            ++metrics.zero_saturation
                  .roots_reaching_iteration_limit;
        }
    }

    if (!std::isfinite(metrics.total_weight) ||
        metrics.total_weight <= 0.0 ||
        !std::isfinite(metrics.parent_kl) ||
        !std::isfinite(
            metrics.full_range.numerical_best_kl) ||
        !std::isfinite(
            metrics.zero_saturation.numerical_best_kl)) {
        throw std::logic_error(
            "centered-tanh oracle aggregate became invalid");
    }
    metrics.parent_kl /= metrics.total_weight;
    metrics.full_range.numerical_best_kl /=
        metrics.total_weight;
    metrics.full_range.certified_kl_lower_bound /=
        metrics.total_weight;
    metrics.zero_saturation.numerical_best_kl /=
        metrics.total_weight;
    metrics.zero_saturation.certified_kl_lower_bound /=
        metrics.total_weight;

    metrics.reduction_defined =
        metrics.parent_kl >
        kP16MechanismKlDefinedTolerance;
    if (metrics.reduction_defined) {
        const auto populate_reductions =
            [&](CenteredTanhOracleBound& bound) {
                bound.achievable_reduction_fraction =
                    std::clamp(
                        (metrics.parent_kl -
                         bound.numerical_best_kl) /
                            metrics.parent_kl,
                        0.0, 1.0);
                bound.certified_reduction_upper_bound =
                    std::clamp(
                        (metrics.parent_kl -
                         bound.certified_kl_lower_bound) /
                            metrics.parent_kl,
                        0.0, 1.0);
            };
        populate_reductions(metrics.full_range);
        populate_reductions(metrics.zero_saturation);
    }
    return metrics;
}

std::vector<double> weighted_n_state_bootstrap_targets(
    std::span<const double> chronological_parent_values,
    double terminal_z, std::size_t distance,
    double terminal_weight) {
    if (distance == 0) {
        throw std::invalid_argument(
            "bootstrap distance must be nonzero");
    }
    if (!valid_probability(terminal_weight)) {
        throw std::invalid_argument(
            "terminal weight must be finite and in [0, 1]");
    }
    if (!valid_probability(terminal_z)) {
        throw std::invalid_argument(
            "terminal value must be a probability");
    }
    for (const double value : chronological_parent_values) {
        if (!valid_probability(value)) {
            throw std::invalid_argument(
                "bootstrap values must be probabilities");
        }
    }

    std::vector<double> targets(
        chronological_parent_values.size(), terminal_z);
    if (distance >= chronological_parent_values.size()) {
        return targets;
    }
    const std::size_t bootstrapped_count =
        chronological_parent_values.size() - distance;
    for (std::size_t index = 0;
         index < bootstrapped_count;
         ++index) {
        targets[index] =
            terminal_weight * terminal_z +
            (1.0 - terminal_weight) *
                chronological_parent_values[
                    index + distance];
    }
    return targets;
}

std::vector<double> n_state_bootstrap_targets(
    std::span<const double> chronological_parent_values,
    double terminal_z, std::size_t distance) {
    return weighted_n_state_bootstrap_targets(
        chronological_parent_values, terminal_z, distance, 0.5);
}

std::vector<std::optional<std::size_t>>
turn_aligned_bootstrap_indices(
    std::span<const std::size_t> chronological_turn_numbers,
    std::size_t turn_advances) {
    if (turn_advances == 0) {
        throw std::invalid_argument(
            "turn-aligned bootstrap advances must be nonzero");
    }
    for (std::size_t index = 1;
         index < chronological_turn_numbers.size(); ++index) {
        const std::size_t previous =
            chronological_turn_numbers[index - 1];
        const std::size_t current =
            chronological_turn_numbers[index];
        if (current < previous) {
            throw std::invalid_argument(
                "turn-aligned bootstrap turns must not regress");
        }
        if (current - previous > 1) {
            throw std::invalid_argument(
                "turn-aligned bootstrap trace must not skip turns");
        }
    }

    std::vector<std::optional<std::size_t>> indices(
        chronological_turn_numbers.size());
    std::size_t future = 0;
    for (std::size_t root = 0;
         root < chronological_turn_numbers.size(); ++root) {
        const std::size_t root_turn =
            chronological_turn_numbers[root];
        if (root_turn >
            std::numeric_limits<std::size_t>::max() -
                turn_advances) {
            throw std::overflow_error(
                "turn-aligned bootstrap turn overflow");
        }
        const std::size_t target_turn =
            root_turn + turn_advances;
        future = std::max(future, root + 1);
        while (future < chronological_turn_numbers.size() &&
               chronological_turn_numbers[future] <
                   target_turn) {
            ++future;
        }
        if (future == chronological_turn_numbers.size()) {
            continue;
        }
        if (chronological_turn_numbers[future] != target_turn) {
            throw std::invalid_argument(
                "turn-aligned bootstrap trace skipped its target "
                "turn");
        }
        indices[root] = future;
    }
    return indices;
}

std::vector<double> weighted_turn_aligned_bootstrap_targets(
    std::span<const double> chronological_parent_values,
    std::span<const std::size_t> chronological_turn_numbers,
    double terminal_z, std::size_t turn_advances,
    double terminal_weight) {
    if (chronological_parent_values.size() !=
        chronological_turn_numbers.size()) {
        throw std::invalid_argument(
            "turn-aligned bootstrap values and turns must have "
            "equal size");
    }
    if (!valid_probability(terminal_weight)) {
        throw std::invalid_argument(
            "terminal weight must be finite and in [0, 1]");
    }
    if (!valid_probability(terminal_z)) {
        throw std::invalid_argument(
            "terminal value must be a probability");
    }
    for (const double value : chronological_parent_values) {
        if (!valid_probability(value)) {
            throw std::invalid_argument(
                "bootstrap values must be probabilities");
        }
    }

    const auto indices =
        turn_aligned_bootstrap_indices(
            chronological_turn_numbers, turn_advances);
    std::vector<double> targets(
        chronological_parent_values.size(), terminal_z);
    for (std::size_t root = 0; root < indices.size(); ++root) {
        if (!indices[root].has_value()) {
            continue;
        }
        targets[root] =
            terminal_weight * terminal_z +
            (1.0 - terminal_weight) *
                chronological_parent_values[*indices[root]];
    }
    return targets;
}

double annealed_terminal_weight(
    std::size_t published_generation,
    std::size_t total_generations) {
    if (total_generations < 2) {
        throw std::invalid_argument(
            "terminal-weight schedule requires at least two "
            "generations");
    }
    if (published_generation == 0 ||
        published_generation > total_generations) {
        throw std::invalid_argument(
            "published generation is outside the terminal-weight "
            "schedule");
    }
    return 0.50 +
           0.25 *
               static_cast<double>(published_generation - 1) /
               static_cast<double>(total_generations - 1);
}

std::vector<double> four_state_bootstrap_targets(
    std::span<const double> chronological_parent_values,
    double terminal_z) {
    return n_state_bootstrap_targets(
        chronological_parent_values, terminal_z, 4);
}

} // namespace old_school::learned_iteration
