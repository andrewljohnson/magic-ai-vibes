#include "old_school/fq4_dev_evaluator.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_priority_math.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <numeric>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::fq4_dev_evaluator {
namespace {

namespace bundle = fq4_dev_bundle;
namespace collection = fq4_priority_collection;
namespace integrity = artifact_integrity;
namespace math = fq4_priority_math;

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(
        "FQ4 development evaluator: " +
        std::string(message));
}

std::size_t split_index(bundle::Split split) {
    switch (split) {
    case bundle::Split::Fit:
        return 0;
    case bundle::Split::Check:
        return 1;
    }
    fail("invalid split");
}

bool positive_role(std::uint8_t roles) {
    return
        (roles &
         static_cast<std::uint8_t>(
             bundle::Role::DominancePositive)) != 0;
}

bool background_role(std::uint8_t roles) {
    return
        (roles &
         static_cast<std::uint8_t>(
             bundle::Role::BackgroundControl)) != 0;
}

bool robustly_dominated(
    const bundle::SelectedRow& row,
    std::size_t action) {
    return action != row.census.pass_index &&
           row.actions[action].dominance.complete ==
               bundle::kWorldCount &&
           row.actions[action].dominance.strict ==
               bundle::kWorldCount;
}

std::size_t robust_constraint_count(
    const bundle::SelectedRow& row) {
    std::size_t count = 0;
    for (std::size_t action = 0;
         action < row.actions.size(); ++action) {
        if (robustly_dominated(row, action)) {
            ++count;
        }
    }
    return count;
}

void add_constraint_census_rows(
    ConstraintCensus& result,
    const std::vector<bundle::SelectedRow>& rows,
    bundle::Split split) {
    const std::size_t split_number =
        split_index(split);
    for (const bundle::SelectedRow& row : rows) {
        if (row.split != split ||
            row.census.owner_deck >=
                bundle::kDeckCount) {
            fail("constraint census row has invalid split or deck");
        }
        if (!positive_role(row.roles)) {
            continue;
        }
        const std::size_t constraints =
            robust_constraint_count(row);
        if (constraints == 0 ||
            constraints >=
                result.rows_by_constraint_count.size()) {
            fail("positive row has an invalid constraint count");
        }
        ++result.rows_by_constraint_count[constraints];
        ++result.positive_rows;
        ++result.positive_rows_by_split_deck
            [split_number][row.census.owner_deck];
        result.maximum_constraints =
            std::max(
                result.maximum_constraints,
                constraints);
    }
}

PreparedRow prepare_row(
    const bundle::SelectedRow& row,
    bundle::Split expected_split) {
    constexpr std::uint8_t kKnownRoles =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive) |
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    if (row.split != expected_split ||
        row.census.owner_deck >=
            bundle::kDeckCount ||
        row.roles == 0 ||
        (row.roles & ~kKnownRoles) != 0 ||
        row.actions.size() < 2 ||
        row.actions.size() >
            bundle::kMaximumActions ||
        row.census.pass_index >=
            row.actions.size()) {
        fail("selected row has an invalid shape");
    }

    PreparedRow result{
        .split = row.split,
        .owner_deck = row.census.owner_deck,
        .roles = row.roles,
        .pass_index = row.census.pass_index,
    };
    result.actions.reserve(row.actions.size());
    std::size_t pass_count = 0;
    std::size_t dominance_count = 0;
    for (std::size_t action_index = 0;
         action_index < row.actions.size();
         ++action_index) {
        const bundle::ActionRow& action =
            row.actions[action_index];
        if (action.is_pass) {
            ++pass_count;
        }
        if (action.is_pass !=
                (action_index == result.pass_index) ||
            action.dominance !=
                row.census.dominance[action_index]) {
            fail("selected row Pass or dominance anchor drifted");
        }
        const bool dominated =
            robustly_dominated(row, action_index);
        dominance_count += dominated ? 1U : 0U;

        PreparedAction prepared{
            .robustly_pass_dominated = dominated,
            .base_score =
                std::bit_cast<double>(
                    action.base_score_bits),
            .parent_residual =
                std::bit_cast<double>(
                    action.parent_residual_bits),
            .features =
                std::vector<double>(
                    bundle::kFeatureCount, 0.0),
        };
        for (std::size_t world = 0;
             world < bundle::kWorldCount; ++world) {
            prepared.raw_samples[world] =
                std::bit_cast<double>(
                    action.raw_sample_bits[world]);
        }
        std::uint16_t previous = 0;
        bool first = true;
        for (const bundle::SparseFeature feature :
             action.features) {
            if (feature.index >=
                    bundle::kFeatureCount ||
                (!first && feature.index <= previous) ||
                feature.value_bits == 0) {
                fail("sparse feature row is noncanonical");
            }
            first = false;
            previous = feature.index;
            prepared.features[feature.index] =
                std::bit_cast<double>(
                    feature.value_bits);
        }
        if (!std::isfinite(prepared.base_score) ||
            !std::isfinite(prepared.parent_residual) ||
            !std::all_of(
                prepared.raw_samples.begin(),
                prepared.raw_samples.end(),
                [](double value) {
                    return std::isfinite(value);
                }) ||
            !std::all_of(
                prepared.features.begin(),
                prepared.features.end(),
                [](double value) {
                    return std::isfinite(value);
                })) {
            fail("expanded row contains a nonfinite value");
        }
        result.actions.push_back(std::move(prepared));
    }
    if (pass_count != 1 ||
        positive_role(row.roles) !=
            (dominance_count != 0) ||
        (!positive_role(row.roles) &&
         !background_role(row.roles))) {
        fail("selected row roles do not match its constraints");
    }
    return result;
}

PreparedCorpus prepare_rows(
    const std::vector<bundle::SelectedRow>& fit,
    const std::vector<bundle::SelectedRow>& check) {
    PreparedCorpus result;
    result.fit.reserve(fit.size());
    result.check.reserve(check.size());
    for (const bundle::SelectedRow& row : fit) {
        result.fit.push_back(
            prepare_row(row, bundle::Split::Fit));
    }
    for (const bundle::SelectedRow& row : check) {
        result.check.push_back(
            prepare_row(row, bundle::Split::Check));
    }
    if (result.fit.empty() ||
        result.check.empty()) {
        fail("prepared corpus is missing a split");
    }
    return result;
}

void validate_prepared_rows(
    const std::vector<PreparedRow>& rows,
    bundle::Split expected_split) {
    constexpr std::uint8_t kKnownRoles =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive) |
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    if (rows.empty()) {
        fail("prepared split is empty");
    }
    std::array<bool, bundle::kDeckCount>
        positive_by_deck{};
    for (const PreparedRow& row : rows) {
        if (row.split != expected_split ||
            row.owner_deck >= bundle::kDeckCount ||
            row.roles == 0 ||
            (row.roles & ~kKnownRoles) != 0 ||
            row.actions.size() < 2 ||
            row.actions.size() >
                bundle::kMaximumActions ||
            row.pass_index >= row.actions.size()) {
            fail("prepared row has an invalid shape");
        }
        std::size_t constraints = 0;
        for (const PreparedAction& action :
             row.actions) {
            constraints +=
                action.robustly_pass_dominated
                    ? 1U
                    : 0U;
            if (action.features.size() !=
                    bundle::kFeatureCount ||
                !std::isfinite(action.base_score) ||
                !std::isfinite(
                    action.parent_residual) ||
                !std::all_of(
                    action.raw_samples.begin(),
                    action.raw_samples.end(),
                    [](double value) {
                        return std::isfinite(value);
                    }) ||
                !std::all_of(
                    action.features.begin(),
                    action.features.end(),
                    [](double value) {
                        return std::isfinite(value);
                    })) {
                fail("prepared action is malformed");
            }
        }
        if (row.actions[row.pass_index]
                .robustly_pass_dominated ||
            positive_role(row.roles) !=
                (constraints != 0) ||
            (!positive_role(row.roles) &&
             !background_role(row.roles))) {
            fail("prepared row roles or constraints drifted");
        }
        if (positive_role(row.roles)) {
            positive_by_deck[row.owner_deck] =
                true;
        }
    }
    if (!std::all_of(
            positive_by_deck.begin(),
            positive_by_deck.end(),
            [](bool covered) {
                return covered;
            })) {
        fail("prepared split lacks five-deck positive coverage");
    }
}

void validate_prepared_corpus(
    const PreparedCorpus& corpus) {
    validate_prepared_rows(
        corpus.fit, bundle::Split::Fit);
    validate_prepared_rows(
        corpus.check, bundle::Split::Check);
}

std::vector<double> base_scores(
    const PreparedRow& row) {
    std::vector<double> result;
    result.reserve(row.actions.size());
    for (const PreparedAction& action : row.actions) {
        result.push_back(action.base_score);
    }
    return result;
}

std::vector<std::vector<double>> features(
    const PreparedRow& row) {
    std::vector<std::vector<double>> result;
    result.reserve(row.actions.size());
    for (const PreparedAction& action : row.actions) {
        result.push_back(action.features);
    }
    return result;
}

std::vector<std::vector<double>> raw_samples(
    const PreparedRow& row) {
    std::vector<std::vector<double>> result;
    result.reserve(row.actions.size());
    for (const PreparedAction& action : row.actions) {
        result.emplace_back(
            action.raw_samples.begin(),
            action.raw_samples.end());
    }
    return result;
}

std::vector<bool> dominance_mask(
    const PreparedRow& row) {
    std::vector<bool> result;
    result.reserve(row.actions.size());
    for (const PreparedAction& action : row.actions) {
        result.push_back(
            action.robustly_pass_dominated);
    }
    return result;
}

std::vector<std::string> ordinal_descriptors(
    std::size_t count) {
    std::vector<std::string> result;
    result.reserve(count);
    for (std::size_t index = 0;
         index < count; ++index) {
        result.push_back(
            "action-" +
            std::string(index < 10 ? "0" : "") +
            std::to_string(index));
    }
    return result;
}

std::vector<double> combined_from_stored_parent(
    const PreparedRow& row) {
    std::vector<double> result;
    result.reserve(row.actions.size());
    for (const PreparedAction& action : row.actions) {
        const double combined =
            action.base_score +
            action.parent_residual;
        if (!std::isfinite(combined)) {
            fail("stored parent combined score is nonfinite");
        }
        result.push_back(combined);
    }
    return result;
}

std::vector<double> target_for(
    const PreparedRow& row,
    const std::vector<double>& parent_combined) {
    if (!row.dominance_positive()) {
        fail("target requested for a background-only row");
    }
    const std::vector<double> parent_raw =
        math::stable_softmax(
            parent_combined,
            kPolicyTemperature);
    std::vector<math::StarConstraint> constraints;
    for (std::size_t action = 0;
         action < row.actions.size(); ++action) {
        if (row.actions[action]
                .robustly_pass_dominated) {
            constraints.push_back({
                .pass_index = row.pass_index,
                .dominated_index = action,
            });
        }
    }
    if (constraints.empty()) {
        fail("positive row has no projection constraint");
    }
    const auto projection =
        math::reverse_kl_i_projection(
            parent_raw, constraints,
            kProjectionRatio);
    return math::behavior_mixture(
        projection.probabilities,
        kBehaviorPrimaryWeight);
}

double forward_kl(
    const std::vector<double>& target,
    const std::vector<double>& behavior) {
    if (target.empty() ||
        target.size() != behavior.size()) {
        fail("KL distributions have inconsistent shapes");
    }
    long double target_total = 0.0L;
    long double behavior_total = 0.0L;
    long double divergence = 0.0L;
    for (std::size_t index = 0;
         index < target.size(); ++index) {
        if (!std::isfinite(target[index]) ||
            !std::isfinite(behavior[index]) ||
            target[index] <= 0.0 ||
            behavior[index] <= 0.0) {
            fail("KL distribution is not positive and finite");
        }
        target_total += target[index];
        behavior_total += behavior[index];
        divergence +=
            static_cast<long double>(target[index]) *
            std::log(
                static_cast<long double>(target[index]) /
                static_cast<long double>(
                    behavior[index]));
    }
    if (std::abs(target_total - 1.0L) >
            1.0e-12L ||
        std::abs(behavior_total - 1.0L) >
            1.0e-12L) {
        fail("KL distribution is not normalized");
    }
    const double result =
        static_cast<double>(divergence);
    if (!std::isfinite(result) ||
        result < -1.0e-15) {
        fail("KL result is invalid");
    }
    return result < 0.0 ? 0.0 : result;
}

std::vector<std::size_t> exact_support_bits(
    const std::vector<double>& scores) {
    if (scores.empty() ||
        !std::all_of(
            scores.begin(), scores.end(),
            [](double value) {
                return std::isfinite(value);
            })) {
        fail("exact support requires finite scores");
    }
    const double maximum =
        *std::max_element(
            scores.begin(), scores.end());
    const std::uint64_t maximum_bits =
        std::bit_cast<std::uint64_t>(maximum);
    std::vector<std::size_t> result;
    for (std::size_t index = 0;
         index < scores.size(); ++index) {
        if (std::bit_cast<std::uint64_t>(
                scores[index]) == maximum_bits) {
            result.push_back(index);
        }
    }
    return result;
}

bool support_violates(
    const PreparedRow& row,
    const std::vector<double>& scores) {
    for (const std::size_t index :
         exact_support_bits(scores)) {
        if (row.actions[index]
                .robustly_pass_dominated) {
            return true;
        }
    }
    return false;
}

std::size_t class_index(
    collection::ParentClass classification) {
    switch (classification) {
    case collection::ParentClass::Safe:
        return 0;
    case collection::ParentClass::Class1:
        return 1;
    case collection::ParentClass::Class2:
        return 2;
    case collection::ParentClass::Class3:
        return 3;
    case collection::ParentClass::Invalid:
        break;
    }
    fail("classifier returned an invalid class");
}

std::size_t severity(std::size_t classification) {
    constexpr std::array<std::size_t, 4>
        kSeverity{0, 3, 2, 1};
    if (classification >= kSeverity.size()) {
        fail("class severity index is invalid");
    }
    return kSeverity[classification];
}

collection::ParentClassResult classify(
    const PreparedRow& row,
    const std::vector<double>& scores) {
    const auto result =
        collection::classify_parent({
            .canonical_descriptors =
                ordinal_descriptors(
                    row.actions.size()),
            .base_scores = base_scores(row),
            .combined_scores = scores,
            .base_samples = raw_samples(row),
            .robustly_pass_dominated =
                dominance_mask(row),
        });
    if (!result.valid) {
        fail("K8 paired classification failed");
    }
    return result;
}

void add_margin(
    MarginMetrics& metrics,
    const PreparedRow& row,
    const std::vector<double>& scores) {
    double root_total = 0.0;
    double root_minimum =
        std::numeric_limits<double>::infinity();
    std::size_t root_constraints = 0;
    for (std::size_t action = 0;
         action < row.actions.size(); ++action) {
        if (!row.actions[action]
                 .robustly_pass_dominated) {
            continue;
        }
        const double margin =
            scores[row.pass_index] -
            scores[action];
        if (!std::isfinite(margin)) {
            fail("dominance margin is nonfinite");
        }
        root_total += margin;
        root_minimum =
            std::min(root_minimum, margin);
        ++root_constraints;
    }
    if (root_constraints == 0) {
        fail("positive row has no dominance margin");
    }
    const double root_mean =
        root_total /
        static_cast<double>(root_constraints);
    if (metrics.roots == 0) {
        metrics.minimum_margin = root_minimum;
    } else {
        metrics.minimum_margin =
            std::min(
                metrics.minimum_margin,
                root_minimum);
    }
    ++metrics.roots;
    metrics.constraints += root_constraints;
    metrics.mean_root_margin += root_mean;
}

struct RowScores {
    math::CenteredResidualScores parent;
    math::CenteredResidualScores candidate;
};

RowScores score_row(
    const PreparedRow& row,
    const std::vector<double>& parent_logits,
    const std::vector<double>& candidate_logits,
    std::size_t& anchored_actions) {
    if (parent_logits.size() !=
            row.actions.size() ||
        candidate_logits.size() !=
            row.actions.size()) {
        fail("policy logit shape drifted");
    }
    const std::vector<double> base =
        base_scores(row);
    RowScores result{
        .parent =
            math::centered_tanh_scores(
                base, parent_logits,
                kResidualWeight),
        .candidate =
            math::centered_tanh_scores(
                base, candidate_logits,
                kResidualWeight),
    };
    for (std::size_t action = 0;
         action < row.actions.size(); ++action) {
        if (std::bit_cast<std::uint64_t>(
                result.parent.residuals[action]) !=
            std::bit_cast<std::uint64_t>(
                row.actions[action]
                    .parent_residual)) {
            fail("frozen parent residual anchor drifted");
        }
        ++anchored_actions;
    }
    return result;
}

void add_positive_row(
    DeckMetrics& metrics,
    const PreparedRow& row,
    const RowScores& scores) {
    const std::vector<double> target =
        target_for(
            row,
            scores.parent.combined_scores);
    const std::vector<double> parent_behavior =
        math::behavior_mixture(
            math::stable_softmax(
                scores.parent.combined_scores,
                kPolicyTemperature),
            kBehaviorPrimaryWeight);
    const std::vector<double> candidate_behavior =
        math::behavior_mixture(
            math::stable_softmax(
                scores.candidate.combined_scores,
                kPolicyTemperature),
            kBehaviorPrimaryWeight);
    ++metrics.positive_roots;
    metrics.positive_options +=
        row.actions.size();
    metrics.target_to_parent_kl +=
        forward_kl(target, parent_behavior);
    metrics.target_to_candidate_kl +=
        forward_kl(target, candidate_behavior);
    add_margin(
        metrics.parent_margins, row,
        scores.parent.combined_scores);
    add_margin(
        metrics.candidate_margins, row,
        scores.candidate.combined_scores);

    ++metrics.parent_support_violations
          .positive_roots;
    ++metrics.candidate_support_violations
          .positive_roots;
    metrics.parent_support_violations
        .violating_roots +=
        support_violates(
            row,
            scores.parent.combined_scores)
            ? 1U
            : 0U;
    metrics.candidate_support_violations
        .violating_roots +=
        support_violates(
            row,
            scores.candidate.combined_scores)
            ? 1U
            : 0U;

    const std::size_t parent_class =
        class_index(
            classify(
                row,
                scores.parent.combined_scores)
                .classification);
    const std::size_t candidate_class =
        class_index(
            classify(
                row,
                scores.candidate.combined_scores)
                .classification);
    ++metrics.parent_classes.values[parent_class];
    ++metrics.candidate_classes.values[candidate_class];
    ++metrics.transitions[parent_class][candidate_class];
    metrics.repairs +=
        ((parent_class == 1 || parent_class == 2) &&
         candidate_class == 0)
            ? 1U
            : 0U;
    metrics.regressions +=
        severity(candidate_class) >
                severity(parent_class)
            ? 1U
            : 0U;
}

void finish_deck(DeckMetrics& metrics) {
    if (metrics.positive_roots == 0 ||
        metrics.parent_margins.roots !=
            metrics.positive_roots ||
        metrics.candidate_margins.roots !=
            metrics.positive_roots ||
        metrics.parent_classes.total() !=
            metrics.positive_roots ||
        metrics.candidate_classes.total() !=
            metrics.positive_roots) {
        fail("deck metrics lack positive-root coverage");
    }
    const double inverse_roots =
        1.0 /
        static_cast<double>(
            metrics.positive_roots);
    metrics.target_to_parent_kl *=
        inverse_roots;
    metrics.target_to_candidate_kl *=
        inverse_roots;
    metrics.parent_margins.mean_root_margin *=
        inverse_roots;
    metrics.candidate_margins.mean_root_margin *=
        inverse_roots;
    metrics.parent_support_violations.fraction =
        static_cast<double>(
            metrics.parent_support_violations
                .violating_roots) *
        inverse_roots;
    metrics.candidate_support_violations.fraction =
        static_cast<double>(
            metrics.candidate_support_violations
                .violating_roots) *
        inverse_roots;
}

void add_class_counts(
    ClassCounts& target,
    const ClassCounts& source) {
    for (std::size_t index = 0;
         index < target.values.size(); ++index) {
        target.values[index] +=
            source.values[index];
    }
}

void add_transitions(
    TransitionMatrix& target,
    const TransitionMatrix& source) {
    for (std::size_t parent = 0;
         parent < target.size(); ++parent) {
        for (std::size_t candidate = 0;
             candidate < target[parent].size();
             ++candidate) {
            target[parent][candidate] +=
                source[parent][candidate];
        }
    }
}

void finish_split(SplitMetrics& metrics) {
    bool first = true;
    for (DeckMetrics& deck : metrics.decks) {
        finish_deck(deck);
        metrics.positive_roots +=
            deck.positive_roots;
        metrics.positive_options +=
            deck.positive_options;
        metrics.deck_balanced_target_to_parent_kl +=
            deck.target_to_parent_kl;
        metrics.deck_balanced_target_to_candidate_kl +=
            deck.target_to_candidate_kl;
        metrics.deck_balanced_parent_mean_margin +=
            deck.parent_margins.mean_root_margin;
        metrics.deck_balanced_candidate_mean_margin +=
            deck.candidate_margins.mean_root_margin;
        if (first) {
            metrics.pooled_parent_minimum_margin =
                deck.parent_margins.minimum_margin;
            metrics.pooled_candidate_minimum_margin =
                deck.candidate_margins.minimum_margin;
            first = false;
        } else {
            metrics.pooled_parent_minimum_margin =
                std::min(
                    metrics.pooled_parent_minimum_margin,
                    deck.parent_margins.minimum_margin);
            metrics.pooled_candidate_minimum_margin =
                std::min(
                    metrics.pooled_candidate_minimum_margin,
                    deck.candidate_margins.minimum_margin);
        }
        metrics.parent_support_violations
            .violating_roots +=
            deck.parent_support_violations
                .violating_roots;
        metrics.candidate_support_violations
            .violating_roots +=
            deck.candidate_support_violations
                .violating_roots;
        metrics.parent_support_violations
            .positive_roots +=
            deck.positive_roots;
        metrics.candidate_support_violations
            .positive_roots +=
            deck.positive_roots;
        add_class_counts(
            metrics.parent_classes,
            deck.parent_classes);
        add_class_counts(
            metrics.candidate_classes,
            deck.candidate_classes);
        add_transitions(
            metrics.transitions,
            deck.transitions);
        metrics.repairs += deck.repairs;
        metrics.regressions +=
            deck.regressions;
    }
    constexpr double kInverseDecks =
        1.0 /
        static_cast<double>(
            bundle::kDeckCount);
    metrics.deck_balanced_target_to_parent_kl *=
        kInverseDecks;
    metrics.deck_balanced_target_to_candidate_kl *=
        kInverseDecks;
    metrics.deck_balanced_parent_mean_margin *=
        kInverseDecks;
    metrics.deck_balanced_candidate_mean_margin *=
        kInverseDecks;
    const double inverse_roots =
        1.0 /
        static_cast<double>(
            metrics.positive_roots);
    metrics.parent_support_violations.fraction =
        static_cast<double>(
            metrics.parent_support_violations
                .violating_roots) *
        inverse_roots;
    metrics.candidate_support_violations.fraction =
        static_cast<double>(
            metrics.candidate_support_violations
                .violating_roots) *
        inverse_roots;
}

void evaluate_rows(
    SplitMetrics& metrics,
    const std::vector<PreparedRow>& rows,
    const std::vector<std::vector<double>>& parent_logits,
    const std::vector<std::vector<double>>& candidate_logits,
    std::size_t& anchored_rows,
    std::size_t& anchored_actions) {
    if (rows.size() != parent_logits.size() ||
        rows.size() != candidate_logits.size()) {
        fail("policy logit corpus shape drifted");
    }
    for (std::size_t index = 0;
         index < rows.size(); ++index) {
        const PreparedRow& row = rows[index];
        const RowScores scores =
            score_row(
                row, parent_logits[index],
                candidate_logits[index],
                anchored_actions);
        ++anchored_rows;
        if (row.dominance_positive()) {
            add_positive_row(
                metrics.decks[row.owner_deck],
                row, scores);
        }
    }
    finish_split(metrics);
}

CorpusLogits score_logits_impl(
    const PreparedCorpus& corpus,
    const std::shared_ptr<const LearnedModel>& model) {
    if (!model) {
        throw std::invalid_argument(
            "FQ4 development evaluator requires a model");
    }
    validate_prepared_corpus(corpus);
    CorpusLogits result;
    result.fit.reserve(corpus.fit.size());
    result.check.reserve(corpus.check.size());
    for (const PreparedRow& row : corpus.fit) {
        result.fit.push_back(
            learned_policy_head_logits(
                features(row),
                LearnedPolicyDecisionKind::Priority,
                model));
    }
    for (const PreparedRow& row : corpus.check) {
        result.check.push_back(
            learned_policy_head_logits(
                features(row),
                LearnedPolicyDecisionKind::Priority,
                model));
    }
    return result;
}

bool expected_components(
    const LearnedModelComponentFingerprints& components) {
    return components.critic ==
               bundle::kParentCriticFingerprint &&
           components.priority ==
               bundle::kParentPriorityFingerprint &&
           components.attack ==
               bundle::kParentAttackFingerprint &&
           components.block ==
               bundle::kParentBlockFingerprint &&
           components.damage_order ==
               bundle::kParentDamageOrderFingerprint;
}

bool same_nonpriority_components(
    const LearnedModelComponentFingerprints& left,
    const LearnedModelComponentFingerprints& right) {
    return left.critic == right.critic &&
           left.attack == right.attack &&
           left.block == right.block &&
           left.damage_order == right.damage_order;
}

bool canonical_sha256(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return
                       (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
               });
}

void digest_u64(
    integrity::Sha256Accumulator& digest,
    std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0;
         index < bytes.size(); ++index) {
        bytes[index] =
            static_cast<std::byte>(
                value >>
                (8U * (bytes.size() - index - 1U)));
    }
    digest.update(bytes);
}

void digest_double(
    integrity::Sha256Accumulator& digest,
    double value) {
    digest_u64(
        digest,
        std::bit_cast<std::uint64_t>(value));
}

std::string training_input_sha256(
    const std::vector<
        LearnedValuePriorityTrainingExample>& examples,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    integrity::Sha256Accumulator digest;
    digest.update(
        "old-school-fq4-priority-dev-fit-input-v1\n");
    digest_u64(digest, examples.size());
    for (const auto& example : examples) {
        digest_u64(
            digest, example.options.size());
        for (const auto& option : example.options) {
            digest_u64(digest, option.size());
            for (const double value : option) {
                digest_double(digest, value);
            }
        }
        digest_u64(
            digest, example.base_scores.size());
        for (const double value :
             example.base_scores) {
            digest_double(digest, value);
        }
        digest_u64(
            digest,
            example.target_probabilities.size());
        for (const double value :
             example.target_probabilities) {
            digest_double(digest, value);
        }
        digest_double(digest, example.weight);
    }
    digest_u64(digest, optimizer.batch_size);
    digest_u64(digest, optimizer.epochs);
    digest_double(digest, optimizer.learning_rate);
    digest_double(digest, optimizer.beta1);
    digest_double(digest, optimizer.beta2);
    digest_double(digest, optimizer.epsilon);
    digest_double(
        digest,
        optimizer.global_gradient_norm_clip);
    digest_u64(digest, optimizer.seed);
    digest_double(digest, optimizer.residual_weight);
    digest_double(
        digest, optimizer.policy_temperature);
    return digest.finish();
}

FitAccounting fit_accounting(
    const std::vector<
        LearnedValuePriorityTrainingExample>& examples,
    std::size_t optimizer_calls) {
    FitAccounting result{
        .fit_examples = examples.size(),
        .optimizer_calls = optimizer_calls,
        .training_input_sha256 =
            training_input_sha256(
                examples, kOptimizer),
        .optimizer = kOptimizer,
    };
    for (const auto& example : examples) {
        result.fit_options +=
            example.options.size();
    }
    return result;
}

std::string split_name(std::size_t split) {
    return split == 0 ? "fit" : "check";
}

constexpr std::array<std::string_view, bundle::kDeckCount>
    kDeckNames{
        "Green",
        "Red",
        "Blue",
        "White",
        "RU_Aggro",
    };

void write_classes(
    std::ostringstream& output,
    const ClassCounts& counts) {
    for (std::size_t index = 0;
         index < counts.values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << counts.values[index];
    }
}

void write_transitions(
    std::ostringstream& output,
    const TransitionMatrix& transitions) {
    bool first = true;
    for (const auto& row : transitions) {
        for (const std::size_t count : row) {
            if (!first) {
                output << ',';
            }
            first = false;
            output << count;
        }
    }
}

void write_deck_metrics(
    std::ostringstream& output,
    std::string_view split,
    std::string_view deck,
    const DeckMetrics& metrics) {
    output
        << "split=" << split
        << " deck=" << deck
        << " positive_roots=" << metrics.positive_roots
        << " positive_options=" << metrics.positive_options
        << " kl_parent="
        << metrics.target_to_parent_kl
        << " kl_candidate="
        << metrics.target_to_candidate_kl
        << " margin_parent_mean="
        << metrics.parent_margins.mean_root_margin
        << " margin_parent_min="
        << metrics.parent_margins.minimum_margin
        << " margin_candidate_mean="
        << metrics.candidate_margins.mean_root_margin
        << " margin_candidate_min="
        << metrics.candidate_margins.minimum_margin
        << " support_parent="
        << metrics.parent_support_violations
               .violating_roots
        << '/' << metrics.positive_roots
        << " support_parent_fraction="
        << metrics.parent_support_violations.fraction
        << " support_candidate="
        << metrics.candidate_support_violations
               .violating_roots
        << '/' << metrics.positive_roots
        << " support_candidate_fraction="
        << metrics.candidate_support_violations.fraction
        << " parent_classes=";
    write_classes(output, metrics.parent_classes);
    output << " candidate_classes=";
    write_classes(output, metrics.candidate_classes);
    output << " transitions=";
    write_transitions(output, metrics.transitions);
    output
        << " repairs=" << metrics.repairs
        << " regressions=" << metrics.regressions
        << '\n';
}

void write_split_metrics(
    std::ostringstream& output,
    std::string_view split,
    const SplitMetrics& metrics) {
    for (std::size_t deck = 0;
         deck < metrics.decks.size(); ++deck) {
        write_deck_metrics(
            output, split, kDeckNames[deck],
            metrics.decks[deck]);
    }
    output
        << "split=" << split
        << " aggregate=deck_balanced"
        << " positive_roots=" << metrics.positive_roots
        << " positive_options=" << metrics.positive_options
        << " kl_parent="
        << metrics.deck_balanced_target_to_parent_kl
        << " kl_candidate="
        << metrics.deck_balanced_target_to_candidate_kl
        << " margin_parent_mean="
        << metrics.deck_balanced_parent_mean_margin
        << " margin_parent_min="
        << metrics.pooled_parent_minimum_margin
        << " margin_candidate_mean="
        << metrics.deck_balanced_candidate_mean_margin
        << " margin_candidate_min="
        << metrics.pooled_candidate_minimum_margin
        << " support_parent="
        << metrics.parent_support_violations
               .violating_roots
        << '/' << metrics.positive_roots
        << " support_parent_fraction="
        << metrics.parent_support_violations.fraction
        << " support_candidate="
        << metrics.candidate_support_violations
               .violating_roots
        << '/' << metrics.positive_roots
        << " support_candidate_fraction="
        << metrics.candidate_support_violations.fraction
        << " parent_classes=";
    write_classes(output, metrics.parent_classes);
    output << " candidate_classes=";
    write_classes(output, metrics.candidate_classes);
    output << " transitions=";
    write_transitions(output, metrics.transitions);
    output
        << " repairs=" << metrics.repairs
        << " regressions=" << metrics.regressions
        << '\n';
}

} // namespace

bool PreparedRow::dominance_positive() const {
    return positive_role(roles);
}

std::size_t ClassCounts::total() const {
    return std::accumulate(
        values.begin(), values.end(),
        std::size_t{0});
}

bool OfflineAccounting::zero() const {
    return games == 0 &&
           determinizations == 0 &&
           search_calls == 0 &&
           sampled_worlds == 0 &&
           rollout_evaluations == 0 &&
           terminal_leaves == 0 &&
           bootstrap_leaves == 0 &&
           dominance_transitions == 0;
}

ConstraintCensus constraint_census(
    const bundle::Bundle& source) {
    ConstraintCensus result;
    add_constraint_census_rows(
        result, source.fit_rows,
        bundle::Split::Fit);
    add_constraint_census_rows(
        result, source.check_rows,
        bundle::Split::Check);
    if (result.positive_rows == 0 ||
        result.maximum_constraints == 0) {
        fail("constraint census is empty");
    }
    return result;
}

PreparedCorpus prepare(
    const bundle::Bundle& source) {
    bundle::validate(source);
    return prepare_rows(
        source.fit_rows,
        source.check_rows);
}

std::vector<LearnedValuePriorityTrainingExample>
fit_examples(const PreparedCorpus& corpus) {
    validate_prepared_corpus(corpus);
    std::vector<LearnedValuePriorityTrainingExample>
        result;
    result.reserve(corpus.fit.size());
    for (const PreparedRow& row : corpus.fit) {
        if (!row.dominance_positive()) {
            continue;
        }
        const std::vector<double> parent_combined =
            combined_from_stored_parent(row);
        result.push_back({
            .options = features(row),
            .base_scores = base_scores(row),
            .target_probabilities =
                target_for(
                    row, parent_combined),
            .weight = 1.0,
        });
    }
    if (result.empty()) {
        fail("FIT has no training examples");
    }
    return result;
}

CorpusLogits score_logits(
    const PreparedCorpus& corpus,
    std::shared_ptr<const LearnedModel> model) {
    return score_logits_impl(corpus, model);
}

EvaluationMetrics evaluate_logits(
    const PreparedCorpus& corpus,
    const CorpusLogits& parent,
    const CorpusLogits& candidate) {
    validate_prepared_corpus(corpus);
    EvaluationMetrics result;
    evaluate_rows(
        result.fit, corpus.fit,
        parent.fit, candidate.fit,
        result.parent_anchor_rows,
        result.parent_anchor_actions);
    evaluate_rows(
        result.check, corpus.check,
        parent.check, candidate.check,
        result.parent_anchor_rows,
        result.parent_anchor_actions);
    result.parent_anchors_exact = true;
    if (!result.accounting.zero()) {
        fail("offline evaluator accounting is nonzero");
    }
    return result;
}

ModelEvaluationReport evaluate_models(
    const PreparedCorpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate) {
    if (!parent || !candidate) {
        throw std::invalid_argument(
            "FQ4 development model evaluation requires two models");
    }
    ModelEvaluationReport result{
        .parent_fingerprint =
            learned_model_fingerprint(parent),
        .candidate_fingerprint =
            learned_model_fingerprint(candidate),
        .parent_components =
            learned_model_component_fingerprints(parent),
        .candidate_components =
            learned_model_component_fingerprints(candidate),
    };
    if (result.parent_fingerprint !=
            bundle::kParentModelFingerprint ||
        !expected_components(
            result.parent_components)) {
        fail("model evaluation requires immutable C16");
    }
    const CorpusLogits parent_logits =
        score_logits_impl(corpus, parent);
    const CorpusLogits candidate_logits =
        score_logits_impl(corpus, candidate);
    result.metrics =
        evaluate_logits(
            corpus, parent_logits,
            candidate_logits);
    result.parent_immutable =
        learned_model_fingerprint(parent) ==
        result.parent_fingerprint;
    result.nonpriority_components_identical =
        same_nonpriority_components(
            result.parent_components,
            result.candidate_components);
    if (!result.parent_immutable ||
        !result.nonpriority_components_identical) {
        fail("model isolation check failed");
    }
    return result;
}

CandidateFit fit_candidate(
    const PreparedCorpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    FitBoundaryObserver observer) {
    if (!parent) {
        throw std::invalid_argument(
            "FQ4 development fit requires a parent model");
    }
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const LearnedModelComponentFingerprints
        parent_components =
            learned_model_component_fingerprints(parent);
    if (parent_fingerprint !=
            bundle::kParentModelFingerprint ||
        !expected_components(parent_components)) {
        fail("FIT requires immutable C16");
    }

    // Reproduce every FIT and CHECK parent anchor before any target may cross
    // the update boundary.
    const CorpusLogits parent_logits =
        score_logits_impl(corpus, parent);
    const EvaluationMetrics anchors =
        evaluate_logits(
            corpus, parent_logits,
            parent_logits);
    if (!anchors.parent_anchors_exact) {
        fail("parent anchors were not reproduced");
    }

    const auto examples =
        fit_examples(corpus);
    if (observer) {
        observer(examples, kOptimizer);
    }
    const FitAccounting accounting =
        fit_accounting(examples, 1);
    const auto candidate =
        update_learned_value_priority_head(
            parent, examples, kOptimizer);
    const auto candidate_components =
        learned_model_component_fingerprints(
            candidate);
    if (learned_model_fingerprint(parent) !=
            parent_fingerprint ||
        candidate_components.critic !=
            parent_components.critic ||
        candidate_components.attack !=
            parent_components.attack ||
        candidate_components.block !=
            parent_components.block ||
        candidate_components.damage_order !=
            parent_components.damage_order) {
        fail("FIT update changed parent or a non-Priority component");
    }
    return {
        .model = candidate,
        .accounting = accounting,
    };
}

std::shared_ptr<const LearnedModel>
load_fixed_parent() {
    const integrity::RegularFileSnapshot before =
        integrity::snapshot_regular_file(
            std::string(kParentArtifactPath));
    if (before.sha256 !=
        bundle::kParentArtifactSha256) {
        fail("immutable C16 artifact identity drifted");
    }
    const auto model =
        load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            kParentTrainingGames,
            kParentTrainingSeed,
            kParentGenerations)
            .model();
    const integrity::RegularFileSnapshot after =
        integrity::snapshot_regular_file(
            std::string(kParentArtifactPath));
    const std::string fingerprint =
        learned_model_fingerprint(model);
    const auto components =
        learned_model_component_fingerprints(model);
    if (before != after ||
        fingerprint !=
            bundle::kParentModelFingerprint ||
        !expected_components(components)) {
        fail("immutable C16 model or component identity drifted");
    }
    return model;
}

std::string format_constraint_census(
    const ConstraintCensus& census) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output
        << "schema=" << kEvaluatorSchema
        << " mode=constraint-census"
        << " artifact_bytes="
        << bundle::kPublishedArtifactBytes
        << " artifact_sha256="
        << bundle::kPublishedArtifactSha256
        << " positive_rows="
        << census.positive_rows
        << " maximum_constraints="
        << census.maximum_constraints
        << '\n';
    for (std::size_t split = 0;
         split < census.positive_rows_by_split_deck.size();
         ++split) {
        for (std::size_t deck = 0;
             deck < bundle::kDeckCount; ++deck) {
            output
                << "split=" << split_name(split)
                << " deck=" << kDeckNames[deck]
                << " positive_rows="
                << census.positive_rows_by_split_deck
                       [split][deck]
                << '\n';
        }
    }
    for (std::size_t constraints = 0;
         constraints <
             census.rows_by_constraint_count.size();
         ++constraints) {
        if (census.rows_by_constraint_count
                [constraints] == 0) {
            continue;
        }
        output
            << "constraint_count=" << constraints
            << " roots="
            << census.rows_by_constraint_count
                   [constraints]
            << '\n';
    }
    output
        << "accounting games=0 determinizations=0"
           " search_calls=0 sampled_worlds=0"
           " rollout_evaluations=0 terminal_leaves=0"
           " bootstrap_leaves=0 dominance_transitions=0\n"
        << "result=PASS\n";
    return output.str();
}

std::string format_evaluation_report(
    std::string_view mode,
    const ModelEvaluationReport& report,
    const FitAccounting& fit) {
    if ((mode != "evaluate-parent" &&
         mode != "fit") ||
        report.parent_fingerprint !=
            bundle::kParentModelFingerprint ||
        !canonical_sha256(
            report.candidate_fingerprint) ||
        !expected_components(
            report.parent_components) ||
        !canonical_sha256(
            report.candidate_components.priority) ||
        !report.parent_immutable ||
        !report.nonpriority_components_identical ||
        !same_nonpriority_components(
            report.parent_components,
            report.candidate_components) ||
        !report.metrics.parent_anchors_exact ||
        !report.metrics.accounting.zero() ||
        fit.optimizer != kOptimizer ||
        fit.check_examples != 0 ||
        fit.background_only_examples != 0 ||
        (mode == "evaluate-parent" &&
         (report.candidate_fingerprint !=
              report.parent_fingerprint ||
          report.candidate_components !=
              report.parent_components)) ||
        (mode == "fit" &&
         (fit.fit_examples !=
              report.metrics.fit.positive_roots ||
          fit.fit_options !=
              report.metrics.fit.positive_options ||
          fit.optimizer_calls != 1 ||
          !canonical_sha256(
              fit.training_input_sha256))) ||
        (mode == "evaluate-parent" &&
         (fit.fit_examples != 0 ||
          fit.fit_options != 0 ||
          fit.optimizer_calls != 0 ||
          !fit.training_input_sha256.empty()))) {
        throw std::invalid_argument(
            "invalid FQ4 development evaluation report");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17);
    output
        << "schema=" << kEvaluatorSchema
        << " mode=" << mode
        << " artifact_bytes="
        << bundle::kPublishedArtifactBytes
        << " artifact_sha256="
        << bundle::kPublishedArtifactSha256
        << '\n'
        << "identity parent_fingerprint="
        << report.parent_fingerprint
        << " candidate_fingerprint="
        << report.candidate_fingerprint
        << " parent_immutable=1"
           " nonpriority_components_identical=1"
           " parent_anchors_exact=1"
        << " parent_anchor_rows="
        << report.metrics.parent_anchor_rows
        << " parent_anchor_actions="
        << report.metrics.parent_anchor_actions
        << '\n'
        << "training fit_examples="
        << fit.fit_examples
        << " fit_options=" << fit.fit_options
        << " check_examples=0"
           " background_only_examples=0"
           " optimizer_calls="
        << fit.optimizer_calls
        << " training_input_sha256="
        << (fit.training_input_sha256.empty()
                ? "none"
                : fit.training_input_sha256)
        << " batch_size=" << kOptimizer.batch_size
        << " epochs=" << kOptimizer.epochs
        << " learning_rate="
        << kOptimizer.learning_rate
        << " beta1=" << kOptimizer.beta1
        << " beta2=" << kOptimizer.beta2
        << " epsilon=" << kOptimizer.epsilon
        << " gradient_clip="
        << kOptimizer.global_gradient_norm_clip
        << " seed=" << kOptimizer.seed
        << " residual_weight="
        << kOptimizer.residual_weight
        << " policy_temperature="
        << kOptimizer.policy_temperature
        << '\n';
    write_split_metrics(
        output, "fit",
        report.metrics.fit);
    write_split_metrics(
        output, "check",
        report.metrics.check);
    output
        << "accounting games=0 determinizations=0"
           " search_calls=0 sampled_worlds=0"
           " rollout_evaluations=0 terminal_leaves=0"
           " bootstrap_leaves=0 dominance_transitions=0\n"
        << "result=PASS\n";
    return output.str();
}

PreparedCorpus testing::prepare_selected_rows(
    const std::vector<bundle::SelectedRow>& fit,
    const std::vector<bundle::SelectedRow>& check) {
    return prepare_rows(fit, check);
}

std::string testing::invoke_fit_boundary_token(
    const PreparedCorpus& corpus,
    const std::function<std::string(
        const std::vector<
            LearnedValuePriorityTrainingExample>&,
        const LearnedValuePriorityHeadUpdateConfig&)>&
        updater) {
    if (!updater) {
        throw std::invalid_argument(
            "FQ4 development testing updater is empty");
    }
    const auto examples =
        fit_examples(corpus);
    return updater(examples, kOptimizer);
}

} // namespace old_school::fq4_dev_evaluator
