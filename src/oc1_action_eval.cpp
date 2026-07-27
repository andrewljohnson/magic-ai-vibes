#include "old_school/oc1_action_eval.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace old_school::oc1_action_eval {
namespace {

constexpr std::array<DeckId, kDeckCount> kDecks = {
    DeckId::Green,
    DeckId::Red,
    DeckId::Blue,
    DeckId::White,
    DeckId::RUAggro,
};

std::size_t deck_index(DeckId deck) {
    const auto found =
        std::find(kDecks.begin(), kDecks.end(), deck);
    if (found == kDecks.end()) {
        throw std::invalid_argument(
            "root_deck is outside the five-deck environment");
    }
    return static_cast<std::size_t>(
        std::distance(kDecks.begin(), found));
}

bool is_probability(double value) {
    return std::isfinite(value) && value >= 0.0 &&
           value <= 1.0;
}

double mean_of(const std::vector<double>& values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "cannot average an empty value vector");
    }
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double sample_standard_error(
    const std::vector<double>& values) {
    if (values.size() < 2) {
        throw std::invalid_argument(
            "paired inference requires at least two worlds");
    }
    const double mean = mean_of(values);
    double squared_deviation_sum = 0.0;
    for (const double value : values) {
        const double deviation = value - mean;
        squared_deviation_sum += deviation * deviation;
    }
    const double sample_variance =
        squared_deviation_sum /
        static_cast<double>(values.size() - 1);
    return std::sqrt(
        sample_variance / static_cast<double>(values.size()));
}

const probe_eval::CandidateSamples& find_candidate(
    const ReferenceRoot& root, std::string_view action) {
    const auto found = std::lower_bound(
        root.candidates.begin(), root.candidates.end(), action,
        [](const probe_eval::CandidateSamples& candidate,
           std::string_view key) {
            return candidate.key < key;
        });
    if (found == root.candidates.end() ||
        found->key != action) {
        throw std::invalid_argument(
            "action support references an unknown candidate");
    }
    return *found;
}

void validate_support_coverage(
    const ReferenceRoot& root,
    const ActionSupport& support) {
    validate_action_support(support);
    for (const std::string& action : support.actions) {
        static_cast<void>(find_candidate(root, action));
    }
}

double candidate_mean(
    const probe_eval::CandidateSamples& candidate) {
    return mean_of(candidate.q_samples);
}

double best_candidate_mean(const ReferenceRoot& root) {
    double best =
        -std::numeric_limits<double>::infinity();
    for (const auto& candidate : root.candidates) {
        best = std::max(best, candidate_mean(candidate));
    }
    return best;
}

PairedEstimate make_paired_estimate(
    const std::vector<double>& differences) {
    const double mean = mean_of(differences);
    const double standard_error =
        sample_standard_error(differences);
    return {
        .mean = mean,
        .standard_error = standard_error,
        .lower_95 =
            mean -
            kNormal95CriticalValue * standard_error,
    };
}

void validate_reference_pair(
    const ReferenceRoot& actor,
    const ReferenceRoot& c16) {
    validate_reference_root(actor);
    validate_reference_root(c16);
    if (actor.stable_id != c16.stable_id ||
        actor.root_deck != c16.root_deck ||
        actor.candidates.size() != c16.candidates.size()) {
        throw std::invalid_argument(
            "dual references do not describe the same root");
    }
    for (std::size_t index = 0;
         index < actor.candidates.size(); ++index) {
        if (actor.candidates[index].key !=
            c16.candidates[index].key) {
            throw std::invalid_argument(
                "dual references have different action sets");
        }
    }
}

void validate_root_metrics(
    const RootPolicyMetrics& metrics) {
    if (metrics.stable_id.empty()) {
        throw std::invalid_argument(
            "root metrics require a stable ID");
    }
    static_cast<void>(deck_index(metrics.root_deck));
    validate_action_support(metrics.support);
    if (!is_probability(metrics.support_mean) ||
        !is_probability(metrics.best_candidate_mean) ||
        !std::isfinite(metrics.regret) ||
        metrics.regret < 0.0 || metrics.regret > 1.0 ||
        !is_probability(metrics.top_one_fraction)) {
        throw std::invalid_argument(
            "root metrics contain an invalid value");
    }
}

std::vector<const RootPolicyMetrics*> canonical_metrics(
    const std::vector<RootPolicyMetrics>& roots) {
    if (roots.empty()) {
        throw std::invalid_argument(
            "equal-root metrics require at least one root");
    }
    std::vector<const RootPolicyMetrics*> result;
    result.reserve(roots.size());
    for (const RootPolicyMetrics& root : roots) {
        validate_root_metrics(root);
        result.push_back(&root);
    }
    std::sort(
        result.begin(), result.end(),
        [](const RootPolicyMetrics* left,
           const RootPolicyMetrics* right) {
            return left->stable_id < right->stable_id;
        });
    for (std::size_t index = 1; index < result.size();
         ++index) {
        if (result[index - 1]->stable_id ==
            result[index]->stable_id) {
            throw std::invalid_argument(
                "root metric stable IDs must be unique");
        }
    }
    return result;
}

struct MatchedMetrics {
    std::vector<const RootPolicyMetrics*> control;
    std::vector<const RootPolicyMetrics*> candidate;
};

MatchedMetrics matched_metrics(
    const std::vector<RootPolicyMetrics>& control,
    const std::vector<RootPolicyMetrics>& candidate) {
    MatchedMetrics matched{
        .control = canonical_metrics(control),
        .candidate = canonical_metrics(candidate),
    };
    if (matched.control.size() != matched.candidate.size()) {
        throw std::invalid_argument(
            "control and candidate root counts differ");
    }
    for (std::size_t index = 0;
         index < matched.control.size(); ++index) {
        if (matched.control[index]->stable_id !=
                matched.candidate[index]->stable_id ||
            matched.control[index]->root_deck !=
                matched.candidate[index]->root_deck) {
            throw std::invalid_argument(
                "control and candidate root coverage differs");
        }
    }
    return matched;
}

} // namespace

ActionSupport make_action_support(
    std::vector<std::string> actions) {
    std::sort(actions.begin(), actions.end());
    ActionSupport result{.actions = std::move(actions)};
    validate_action_support(result);
    return result;
}

void validate_action_support(const ActionSupport& support) {
    if (support.actions.empty()) {
        throw std::invalid_argument(
            "action support must not be empty");
    }
    for (const std::string& action : support.actions) {
        if (action.empty()) {
            throw std::invalid_argument(
                "action support contains an empty descriptor");
        }
    }
    if (!std::is_sorted(
            support.actions.begin(), support.actions.end()) ||
        std::adjacent_find(
            support.actions.begin(), support.actions.end()) !=
            support.actions.end()) {
        throw std::invalid_argument(
            "action support must be canonical and unique");
    }
}

ReferenceRoot make_reference_root(
    std::string stable_id, DeckId root_deck,
    std::vector<probe_eval::CandidateSamples> candidates) {
    std::sort(
        candidates.begin(), candidates.end(),
        [](const probe_eval::CandidateSamples& left,
           const probe_eval::CandidateSamples& right) {
            return left.key < right.key;
        });
    ReferenceRoot result{
        .stable_id = std::move(stable_id),
        .root_deck = root_deck,
        .candidates = std::move(candidates),
    };
    validate_reference_root(result);
    return result;
}

void validate_reference_root(const ReferenceRoot& root) {
    if (root.stable_id.empty()) {
        throw std::invalid_argument(
            "reference root requires a stable ID");
    }
    static_cast<void>(deck_index(root.root_deck));
    if (root.candidates.size() < 2) {
        throw std::invalid_argument(
            "reference root requires at least two candidates");
    }
    const std::size_t world_count =
        root.candidates.front().q_samples.size();
    if (world_count < 2) {
        throw std::invalid_argument(
            "reference root requires at least two worlds");
    }
    std::string previous;
    for (std::size_t index = 0;
         index < root.candidates.size(); ++index) {
        const auto& candidate = root.candidates[index];
        if (candidate.key.empty() ||
            (index != 0 && candidate.key <= previous)) {
            throw std::invalid_argument(
                "reference candidates must be canonical and unique");
        }
        if (candidate.q_samples.size() != world_count) {
            throw std::invalid_argument(
                "reference candidate worlds are not aligned");
        }
        for (const double sample : candidate.q_samples) {
            if (!is_probability(sample)) {
                throw std::invalid_argument(
                    "reference samples must be finite probabilities");
            }
        }
        previous = candidate.key;
    }
}

ActionSupport exact_best_set(const ReferenceRoot& root) {
    validate_reference_root(root);
    std::vector<double> means;
    means.reserve(root.candidates.size());
    double best =
        -std::numeric_limits<double>::infinity();
    for (const auto& candidate : root.candidates) {
        means.push_back(candidate_mean(candidate));
        best = std::max(best, means.back());
    }
    std::vector<std::string> actions;
    for (std::size_t index = 0; index < means.size(); ++index) {
        if (means[index] == best) {
            actions.push_back(root.candidates[index].key);
        }
    }
    return make_action_support(std::move(actions));
}

std::vector<double> uniform_support_values(
    const ReferenceRoot& root, const ActionSupport& support) {
    validate_reference_root(root);
    validate_support_coverage(root, support);
    const std::size_t world_count =
        root.candidates.front().q_samples.size();
    std::vector<double> result(world_count, 0.0);
    for (const std::string& action : support.actions) {
        const auto& candidate = find_candidate(root, action);
        for (std::size_t world = 0; world < world_count;
             ++world) {
            result[world] += candidate.q_samples[world];
        }
    }
    const double count =
        static_cast<double>(support.actions.size());
    for (double& value : result) {
        value /= count;
    }
    return result;
}

RootPolicyMetrics evaluate_support(
    const ReferenceRoot& root, const ActionSupport& support,
    const ActionSupport& supplied_best_set) {
    validate_reference_root(root);
    validate_support_coverage(root, support);
    validate_support_coverage(root, supplied_best_set);
    const std::vector<double> values =
        uniform_support_values(root, support);
    const double support_mean = mean_of(values);
    const double best = best_candidate_mean(root);
    const std::size_t overlap = static_cast<std::size_t>(
        std::count_if(
            support.actions.begin(), support.actions.end(),
            [&](const std::string& action) {
                return std::binary_search(
                    supplied_best_set.actions.begin(),
                    supplied_best_set.actions.end(), action);
            }));
    return {
        .stable_id = root.stable_id,
        .root_deck = root.root_deck,
        .support = support,
        .support_mean = support_mean,
        .best_candidate_mean = best,
        .regret = std::max(0.0, best - support_mean),
        .top_one_fraction =
            static_cast<double>(overlap) /
            static_cast<double>(support.actions.size()),
    };
}

PairedEstimate paired_support_difference(
    const ReferenceRoot& root, const ActionSupport& first,
    const ActionSupport& second) {
    const std::vector<double> first_values =
        uniform_support_values(root, first);
    const std::vector<double> second_values =
        uniform_support_values(root, second);
    std::vector<double> differences;
    differences.reserve(first_values.size());
    for (std::size_t world = 0;
         world < first_values.size(); ++world) {
        differences.push_back(
            first_values[world] - second_values[world]);
    }
    return make_paired_estimate(differences);
}

bool is_robust_positive(const PairedEstimate& estimate) {
    return std::isfinite(estimate.mean) &&
           std::isfinite(estimate.lower_95) &&
           estimate.mean >= kRobustMinimumEffect &&
           estimate.lower_95 > 0.0;
}

bool entire_support_contained(
    const ActionSupport& support,
    const ActionSupport& containing_set) {
    validate_action_support(support);
    validate_action_support(containing_set);
    return std::includes(
        containing_set.actions.begin(),
        containing_set.actions.end(),
        support.actions.begin(), support.actions.end());
}

JointDominance joint_dominance(
    const ReferenceRoot& actor, const ReferenceRoot& c16,
    std::string_view first_action,
    std::string_view second_action) {
    validate_reference_pair(actor, c16);
    if (first_action == second_action) {
        throw std::invalid_argument(
            "joint dominance requires two distinct actions");
    }
    const ActionSupport first =
        make_action_support({std::string(first_action)});
    const ActionSupport second =
        make_action_support({std::string(second_action)});
    const PairedEstimate actor_estimate =
        paired_support_difference(actor, first, second);
    const PairedEstimate c16_estimate =
        paired_support_difference(c16, first, second);
    return {
        .actor_first_minus_second = actor_estimate,
        .c16_first_minus_second = c16_estimate,
        .first_dominates_second =
            is_robust_positive(actor_estimate) &&
            is_robust_positive(c16_estimate),
    };
}

ActionSupport joint_robust_best_set(
    const ReferenceRoot& actor, const ReferenceRoot& c16) {
    validate_reference_pair(actor, c16);
    std::vector<std::string> nondominated;
    for (const auto& candidate : actor.candidates) {
        bool dominated = false;
        for (const auto& challenger : actor.candidates) {
            if (challenger.key == candidate.key) {
                continue;
            }
            if (joint_dominance(
                    actor, c16, challenger.key,
                    candidate.key)
                    .first_dominates_second) {
                dominated = true;
                break;
            }
        }
        if (!dominated) {
            nondominated.push_back(candidate.key);
        }
    }
    return make_action_support(std::move(nondominated));
}

EqualRootSummary summarize_equal_roots(
    const std::vector<RootPolicyMetrics>& roots) {
    const auto canonical = canonical_metrics(roots);
    EqualRootSummary summary;
    summary.root_count = canonical.size();
    for (std::size_t deck = 0; deck < kDecks.size();
         ++deck) {
        summary.by_deck[deck].root_deck = kDecks[deck];
    }

    double regret_sum = 0.0;
    double top_one_sum = 0.0;
    std::array<double, kDeckCount> deck_regret_sums{};
    std::array<double, kDeckCount> deck_top_one_sums{};
    for (const RootPolicyMetrics* root : canonical) {
        const std::size_t deck = deck_index(root->root_deck);
        regret_sum += root->regret;
        top_one_sum += root->top_one_fraction;
        ++summary.by_deck[deck].root_count;
        deck_regret_sums[deck] += root->regret;
        deck_top_one_sums[deck] += root->top_one_fraction;
    }
    const double count =
        static_cast<double>(canonical.size());
    summary.mean_regret = regret_sum / count;
    summary.mean_top_one_fraction = top_one_sum / count;
    for (std::size_t deck = 0; deck < kDecks.size();
         ++deck) {
        if (summary.by_deck[deck].root_count == 0) {
            continue;
        }
        const double deck_count = static_cast<double>(
            summary.by_deck[deck].root_count);
        summary.by_deck[deck].mean_regret =
            deck_regret_sums[deck] / deck_count;
        summary.by_deck[deck].mean_top_one_fraction =
            deck_top_one_sums[deck] / deck_count;
    }
    return summary;
}

EqualRootComparison compare_equal_roots(
    const std::vector<RootPolicyMetrics>& control,
    const std::vector<RootPolicyMetrics>& candidate) {
    static_cast<void>(matched_metrics(control, candidate));
    EqualRootComparison comparison{
        .control = summarize_equal_roots(control),
        .candidate = summarize_equal_roots(candidate),
    };
    comparison.pooled_regret_no_worse =
        comparison.candidate.mean_regret <=
        comparison.control.mean_regret;
    comparison.pooled_top_one_no_lower =
        comparison.candidate.mean_top_one_fraction >=
        comparison.control.mean_top_one_fraction;
    comparison.every_deck_regret_within_allowance = true;
    for (std::size_t deck = 0; deck < kDecks.size();
         ++deck) {
        if (comparison.control.by_deck[deck].root_count == 0) {
            continue;
        }
        if (comparison.candidate.by_deck[deck].mean_regret >
            comparison.control.by_deck[deck].mean_regret +
                kPerDeckRegretAllowance) {
            comparison.every_deck_regret_within_allowance =
                false;
        }
    }
    comparison.passed =
        comparison.pooled_regret_no_worse &&
        comparison.pooled_top_one_no_lower &&
        comparison.every_deck_regret_within_allowance;
    return comparison;
}

double total_regret(
    const std::vector<RootPolicyMetrics>& roots) {
    const auto canonical = canonical_metrics(roots);
    double result = 0.0;
    for (const RootPolicyMetrics* root : canonical) {
        result += root->regret;
    }
    return result;
}

bool total_regret_no_worse(
    const std::vector<RootPolicyMetrics>& control,
    const std::vector<RootPolicyMetrics>& candidate) {
    static_cast<void>(matched_metrics(control, candidate));
    return total_regret(candidate) <= total_regret(control);
}

DualReferenceMaterialRegression material_regression(
    const ReferenceRoot& actor, const ReferenceRoot& c16,
    const ActionSupport& control,
    const ActionSupport& candidate) {
    validate_reference_pair(actor, c16);
    const PairedEstimate actor_estimate =
        paired_support_difference(actor, control, candidate);
    const PairedEstimate c16_estimate =
        paired_support_difference(c16, control, candidate);
    return {
        .actor_control_minus_candidate = actor_estimate,
        .c16_control_minus_candidate = c16_estimate,
        .material_under_both =
            is_robust_positive(actor_estimate) &&
            is_robust_positive(c16_estimate),
    };
}

} // namespace old_school::oc1_action_eval
