#pragma once

#include <cstddef>
#include <vector>

namespace old_school::fq4_priority_math {

struct StarConstraint {
    std::size_t pass_index = 0;
    std::size_t dominated_index = 0;

    bool operator==(const StarConstraint&) const = default;
};

struct ReverseKlProjection {
    // The unique I-projection argmin_q D_KL(q || p).
    std::vector<double> probabilities;
    std::vector<std::size_t> active_dominated_indices;

    bool operator==(const ReverseKlProjection&) const = default;
};

ReverseKlProjection reverse_kl_i_projection(
    const std::vector<double>& parent_probabilities,
    const std::vector<StarConstraint>& constraints,
    double ratio);

std::vector<double> stable_softmax(
    const std::vector<double>& scores,
    double temperature);

std::vector<double> behavior_mixture(
    const std::vector<double>& probabilities,
    double primary_weight);

struct CenteredResidualScores {
    std::vector<double> centered_logits;
    std::vector<double> residuals;
    std::vector<double> combined_scores;
    std::vector<std::size_t> exact_max_indices;

    bool operator==(
        const CenteredResidualScores&) const = default;
};

CenteredResidualScores centered_tanh_scores(
    const std::vector<double>& base_scores,
    const std::vector<double>& logits,
    double residual_weight);

} // namespace old_school::fq4_priority_math
