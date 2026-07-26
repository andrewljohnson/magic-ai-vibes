#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

std::vector<double> four_state_bootstrap_targets(
    std::span<const double> chronological_parent_values,
    double terminal_z) {
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

    constexpr std::size_t kBootstrapStepStates = 4;
    std::vector<double> targets(
        chronological_parent_values.size(), terminal_z);
    for (std::size_t index = 0;
         index + kBootstrapStepStates <
         chronological_parent_values.size();
         ++index) {
        targets[index] =
            0.5 * terminal_z +
            0.5 * chronological_parent_values[
                      index + kBootstrapStepStates];
    }
    return targets;
}

} // namespace old_school::learned_iteration
