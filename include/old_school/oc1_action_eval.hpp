#pragma once

#include "old_school/probe_eval.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::oc1_action_eval {

inline constexpr double kRobustMinimumEffect =
    probe_eval::kStablePairMinimumDelta;
inline constexpr double kNormal95CriticalValue =
    probe_eval::kNormal95CriticalValue;
inline constexpr double kPerDeckRegretAllowance = 0.010;

// A deployed policy may randomize uniformly over multiple exact score
// maxima. Supports are canonical sets: nonempty, lexicographically sorted,
// and duplicate-free.
struct ActionSupport {
    std::vector<std::string> actions;

    bool operator==(const ActionSupport&) const = default;
};

// Raw common-world samples are retained because the uncertainty of a
// multi-action support contrast depends on covariance that cannot be
// reconstructed from candidate or pair standard errors.
struct ReferenceRoot {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::vector<probe_eval::CandidateSamples> candidates;
};

struct PairedEstimate {
    double mean = 0.0;
    double standard_error = 0.0;
    double lower_95 = 0.0;

    bool operator==(const PairedEstimate&) const = default;
};

struct RootPolicyMetrics {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    ActionSupport support;
    double support_mean = 0.0;
    double best_candidate_mean = 0.0;
    double regret = 0.0;
    double top_one_fraction = 0.0;

    bool operator==(const RootPolicyMetrics&) const = default;
};

struct DeckPolicySummary {
    DeckId root_deck = DeckId::Green;
    std::size_t root_count = 0;
    double mean_regret = 0.0;
    double mean_top_one_fraction = 0.0;

    bool operator==(const DeckPolicySummary&) const = default;
};

struct EqualRootSummary {
    std::size_t root_count = 0;
    double mean_regret = 0.0;
    double mean_top_one_fraction = 0.0;
    std::array<DeckPolicySummary, kDeckCount> by_deck{};

    bool operator==(const EqualRootSummary&) const = default;
};

struct EqualRootComparison {
    EqualRootSummary control;
    EqualRootSummary candidate;
    bool pooled_regret_no_worse = false;
    bool pooled_top_one_no_lower = false;
    bool every_deck_regret_within_allowance = false;
    bool passed = false;

    bool operator==(const EqualRootComparison&) const = default;
};

struct JointDominance {
    PairedEstimate actor_first_minus_second;
    PairedEstimate c16_first_minus_second;
    bool first_dominates_second = false;

    bool operator==(const JointDominance&) const = default;
};

struct DualReferenceMaterialRegression {
    PairedEstimate actor_control_minus_candidate;
    PairedEstimate c16_control_minus_candidate;
    bool material_under_both = false;

    bool operator==(
        const DualReferenceMaterialRegression&) const = default;
};

// Factories canonicalize input order and reject duplicates. The validators
// are public so decoded or aggregate-initialized data can fail closed too.
ActionSupport make_action_support(
    std::vector<std::string> actions);
void validate_action_support(const ActionSupport& support);

ReferenceRoot make_reference_root(
    std::string stable_id, DeckId root_deck,
    std::vector<probe_eval::CandidateSamples> candidates);
void validate_reference_root(const ReferenceRoot& root);

// The exact maximum-mean set has no tolerance band. Actor label best sets
// with their frozen uncertainty-aware semantics are supplied separately to
// evaluate_support().
ActionSupport exact_best_set(const ReferenceRoot& root);

// One value per common world, each the uniform average over every action in
// the supplied deployed support.
std::vector<double> uniform_support_values(
    const ReferenceRoot& root, const ActionSupport& support);

// Regret is measured against the maximum individual candidate mean. Top-one
// fraction is |support intersect supplied_best_set| / |support|.
RootPolicyMetrics evaluate_support(
    const ReferenceRoot& root, const ActionSupport& support,
    const ActionSupport& supplied_best_set);

// Returns first-support minus second-support using common-world differences.
PairedEstimate paired_support_difference(
    const ReferenceRoot& root, const ActionSupport& first,
    const ActionSupport& second);

// The boundary is literal: an effect of exactly 0.03 qualifies, while a
// lower confidence bound of exactly zero does not.
bool is_robust_positive(const PairedEstimate& estimate);

bool entire_support_contained(
    const ActionSupport& support,
    const ActionSupport& containing_set);

// References must describe the same stable root, deck, and complete action
// set. An action dominates only when its paired advantage is robust under
// both references.
JointDominance joint_dominance(
    const ReferenceRoot& actor, const ReferenceRoot& c16,
    std::string_view first_action,
    std::string_view second_action);

// Returns every action not jointly robustly dominated by another action.
ActionSupport joint_robust_best_set(
    const ReferenceRoot& actor, const ReferenceRoot& c16);

// Roots are always weighted equally, independent of their action/support
// counts. Canonical stable-ID accumulation makes input ordering irrelevant.
EqualRootSummary summarize_equal_roots(
    const std::vector<RootPolicyMetrics>& roots);

// Requires exactly matching root IDs and decks. The per-deck regret guard
// uses literal candidate <= control + 0.010; equality passes.
EqualRootComparison compare_equal_roots(
    const std::vector<RootPolicyMetrics>& control,
    const std::vector<RootPolicyMetrics>& candidate);

// DVR roots use an unnormalized total-regret guard.
double total_regret(
    const std::vector<RootPolicyMetrics>& roots);
bool total_regret_no_worse(
    const std::vector<RootPolicyMetrics>& control,
    const std::vector<RootPolicyMetrics>& candidate);

// A candidate support is a material regression only when the control-minus-
// candidate support contrast is robust under both independent references.
DualReferenceMaterialRegression material_regression(
    const ReferenceRoot& actor, const ReferenceRoot& c16,
    const ActionSupport& control,
    const ActionSupport& candidate);

} // namespace old_school::oc1_action_eval
