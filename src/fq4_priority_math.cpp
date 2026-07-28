#include "old_school/fq4_priority_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace old_school::fq4_priority_math {

ReverseKlProjection reverse_kl_i_projection(
    const std::vector<double>& parent_probabilities,
    const std::vector<StarConstraint>& constraints,
    double ratio) {
    if (parent_probabilities.empty() ||
        parent_probabilities.size() > 63 ||
        !std::isfinite(ratio) || ratio <= 1.0) {
        throw std::invalid_argument(
            "reverse-KL projection dimensions or ratio are invalid");
    }
    long double total = 0.0L;
    for (const double probability :
         parent_probabilities) {
        if (!std::isfinite(probability) ||
            probability <= 0.0) {
            throw std::invalid_argument(
                "reverse-KL projection requires strictly "
                "positive finite probabilities");
        }
        total +=
            static_cast<long double>(probability);
    }
    if (std::abs(total - 1.0L) > 1.0e-12L) {
        throw std::invalid_argument(
            "reverse-KL projection probabilities must sum to one");
    }
    if (constraints.empty()) {
        return {
            .probabilities = parent_probabilities,
        };
    }
    const std::size_t pass = constraints.front().pass_index;
    if (pass >= parent_probabilities.size()) {
        throw std::invalid_argument(
            "reverse-KL projection pass index is out of range");
    }
    std::vector<std::size_t> dominated;
    dominated.reserve(constraints.size());
    for (const StarConstraint& constraint : constraints) {
        if (constraint.pass_index != pass ||
            constraint.dominated_index >=
                parent_probabilities.size() ||
            constraint.dominated_index == pass ||
            std::find(
                dominated.begin(), dominated.end(),
                constraint.dominated_index) !=
                dominated.end()) {
            throw std::invalid_argument(
                "reverse-KL projection constraints are malformed");
        }
        dominated.push_back(
            constraint.dominated_index);
    }
    if (dominated.size() >=
        std::numeric_limits<std::uint64_t>::digits) {
        throw std::invalid_argument(
            "reverse-KL projection has too many constraints");
    }
    const bool already_feasible =
        std::all_of(
            dominated.begin(), dominated.end(),
            [&](std::size_t index) {
                return parent_probabilities[pass] >=
                       ratio *
                           parent_probabilities[index];
            });
    if (already_feasible) {
        return {
            .probabilities = parent_probabilities,
        };
    }

    constexpr long double kKktTolerance = 2.0e-14L;
    std::optional<ReverseKlProjection> answer;
    long double answer_objective =
        std::numeric_limits<long double>::infinity();
    const std::uint64_t subset_count =
        std::uint64_t{1} << dominated.size();
    for (std::uint64_t mask = 1;
         mask < subset_count; ++mask) {
        std::vector<std::size_t> active;
        long double log_geometric =
            std::log(
                static_cast<long double>(
                    parent_probabilities[pass]));
        long double inactive_mass = 0.0L;
        for (std::size_t index = 0;
             index < parent_probabilities.size(); ++index) {
            if (index == pass) {
                continue;
            }
            const auto constrained = std::find(
                dominated.begin(), dominated.end(), index);
            const bool is_active =
                constrained != dominated.end() &&
                (mask &
                 (std::uint64_t{1}
                  << static_cast<std::size_t>(
                         constrained -
                         dominated.begin()))) != 0;
            if (is_active) {
                active.push_back(index);
                log_geometric +=
                    std::log(
                        static_cast<long double>(ratio) *
                        static_cast<long double>(
                            parent_probabilities[index])) /
                    static_cast<long double>(ratio);
            } else {
                inactive_mass +=
                    static_cast<long double>(
                        parent_probabilities[index]);
            }
        }
        const long double group_coefficient =
            1.0L +
            static_cast<long double>(active.size()) /
                static_cast<long double>(ratio);
        const long double geometric =
            std::exp(
                log_geometric / group_coefficient);
        const long double scale =
            1.0L /
            (inactive_mass +
             group_coefficient * geometric);
        const long double pass_probability =
            scale * geometric;

        bool kkt = true;
        for (const std::size_t index : dominated) {
            const bool is_active =
                std::find(
                    active.begin(), active.end(),
                    index) != active.end();
            const long double boundary =
                static_cast<long double>(ratio) *
                static_cast<long double>(
                    parent_probabilities[index]);
            if (is_active) {
                kkt =
                    kkt &&
                    geometric <=
                        boundary + kKktTolerance;
            } else {
                kkt =
                    kkt &&
                    geometric + kKktTolerance >=
                        boundary;
            }
        }
        if (!kkt) {
            continue;
        }

        std::vector<double> q(
            parent_probabilities.size());
        for (std::size_t index = 0;
             index < q.size(); ++index) {
            if (index == pass) {
                q[index] =
                    static_cast<double>(
                        pass_probability);
            } else if (
                std::find(
                    active.begin(), active.end(),
                    index) != active.end()) {
                q[index] =
                    static_cast<double>(
                        pass_probability /
                        static_cast<long double>(ratio));
            } else {
                q[index] =
                    static_cast<double>(
                        scale *
                        static_cast<long double>(
                            parent_probabilities[index]));
            }
        }
        long double objective = 0.0L;
        long double q_total = 0.0L;
        for (std::size_t index = 0;
             index < q.size(); ++index) {
            q_total += static_cast<long double>(q[index]);
            objective +=
                static_cast<long double>(q[index]) *
                std::log(
                    static_cast<long double>(q[index]) /
                    static_cast<long double>(
                        parent_probabilities[index]));
        }
        bool feasible =
            std::abs(q_total - 1.0L) <= 1.0e-12L;
        for (const std::size_t index : dominated) {
            feasible =
                feasible &&
                static_cast<long double>(q[pass]) +
                        1.0e-14L >=
                    static_cast<long double>(ratio) *
                        static_cast<long double>(q[index]);
        }
        if (!feasible) {
            continue;
        }
        if (!answer.has_value() ||
            objective < answer_objective) {
            answer = ReverseKlProjection{
                .probabilities = std::move(q),
                .active_dominated_indices =
                    std::move(active),
            };
            answer_objective = objective;
        }
    }
    if (!answer.has_value()) {
        throw std::logic_error(
            "reverse-KL joint active-set projection found no KKT solution");
    }
    return *answer;
}

std::vector<double> stable_softmax(
    const std::vector<double>& scores,
    double temperature) {
    if (scores.empty() ||
        !std::isfinite(temperature) ||
        temperature <= 0.0 ||
        !std::all_of(
            scores.begin(), scores.end(),
            [](double value) {
                return std::isfinite(value);
            })) {
        throw std::invalid_argument(
            "stable softmax requires finite scores and positive temperature");
    }
    const double maximum =
        *std::max_element(scores.begin(), scores.end());
    std::vector<double> probabilities;
    probabilities.reserve(scores.size());
    long double sum = 0.0L;
    for (const double score : scores) {
        const double probability =
            std::exp(
                (score - maximum) / temperature);
        probabilities.push_back(probability);
        sum += static_cast<long double>(probability);
    }
    for (double& probability : probabilities) {
        probability =
            static_cast<double>(
                static_cast<long double>(probability) /
                sum);
    }
    return probabilities;
}

std::vector<double> behavior_mixture(
    const std::vector<double>& probabilities,
    double primary_weight) {
    if (probabilities.empty() ||
        !std::isfinite(primary_weight) ||
        primary_weight < 0.0 ||
        primary_weight > 1.0 ||
        !std::all_of(
            probabilities.begin(),
            probabilities.end(),
            [](double value) {
                return std::isfinite(value) &&
                       value >= 0.0;
            })) {
        throw std::invalid_argument(
            "behavior mixture inputs are invalid");
    }
    long double total = 0.0L;
    for (const double probability : probabilities) {
        total += static_cast<long double>(probability);
    }
    if (std::abs(total - 1.0L) > 1.0e-12L) {
        throw std::invalid_argument(
            "behavior mixture probabilities must sum to one");
    }
    std::vector<double> result = probabilities;
    const double uniform =
        (1.0 - primary_weight) /
        static_cast<double>(result.size());
    for (double& probability : result) {
        probability =
            primary_weight * probability + uniform;
    }
    return result;
}

CenteredResidualScores centered_tanh_scores(
    const std::vector<double>& base_scores,
    const std::vector<double>& logits,
    double residual_weight) {
    if (base_scores.empty() ||
        base_scores.size() != logits.size() ||
        !std::isfinite(residual_weight) ||
        residual_weight < 0.0 ||
        residual_weight > 1.0 ||
        !std::all_of(
            base_scores.begin(), base_scores.end(),
            [](double value) {
                return std::isfinite(value);
            }) ||
        !std::all_of(
            logits.begin(), logits.end(),
            [](double value) {
                return std::isfinite(value);
            })) {
        throw std::invalid_argument(
            "centered residual inputs are invalid");
    }
    double sum = 0.0;
    for (const double logit : logits) {
        sum += logit;
    }
    const double mean =
        sum / static_cast<double>(logits.size());

    CenteredResidualScores result;
    result.centered_logits.reserve(logits.size());
    result.residuals.reserve(logits.size());
    result.combined_scores.reserve(logits.size());
    for (std::size_t index = 0;
         index < logits.size(); ++index) {
        const double centered = logits[index] - mean;
        const double residual =
            residual_weight * std::tanh(centered);
        const double combined =
            base_scores[index] + residual;
        if (!std::isfinite(centered) ||
            !std::isfinite(residual) ||
            !std::isfinite(combined)) {
            throw std::invalid_argument(
                "centered residual result is nonfinite");
        }
        result.centered_logits.push_back(centered);
        result.residuals.push_back(residual);
        result.combined_scores.push_back(combined);
    }
    const double maximum =
        *std::max_element(
            result.combined_scores.begin(),
            result.combined_scores.end());
    for (std::size_t index = 0;
         index < result.combined_scores.size();
         ++index) {
        if (result.combined_scores[index] == maximum) {
            result.exact_max_indices.push_back(index);
        }
    }
    return result;
}

} // namespace old_school::fq4_priority_math
