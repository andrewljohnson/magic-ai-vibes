#include "old_school/action_q_recursive_policy_improvement.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/conservative_policy_improvement.hpp"
#include "old_school/exact_combat_subgame.hpp"
#include "old_school/game.hpp"
#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace aq5 =
    old_school::action_q_recursive_policy_improvement;
namespace conservative =
    old_school::conservative_policy_improvement;
namespace exact_combat =
    old_school::exact_combat_subgame;

constexpr std::uint64_t kAq6Rb0RootSeed =
    202607290601ULL;
constexpr std::uint64_t kAq6Rb1RootSeed =
    202607290701ULL;
constexpr std::uint64_t kAq6Rb2Tc0RootSeed =
    202607290801ULL;
constexpr std::uint64_t kAq6Hscan0RootSeed =
    202607290901ULL;
constexpr exact_combat::Bounds kAq6CombatBounds{
    .maximum_attackers = 8,
    .maximum_blockers = 8,
    .maximum_block_assignments = 6'561,
    .maximum_damage_orders_per_assignment = 40'320,
    .maximum_completed_plans = 65'536,
};

std::string_view family_name(aq5::DecisionFamily family) {
    switch (family) {
    case aq5::DecisionFamily::Priority:
        return "Priority";
    case aq5::DecisionFamily::Attack:
        return "Attack";
    case aq5::DecisionFamily::Block:
        return "Block";
    }
    return "Invalid";
}

std::shared_ptr<const old_school::LearnedModel>
load_parent() {
    const auto snapshot =
        old_school::artifact_integrity::snapshot_regular_file(
            std::string(aq5::kRequiredParentPath));
    if (snapshot.byte_size != aq5::kRequiredParentBytes ||
        snapshot.sha256 != aq5::kRequiredParentSha256) {
        throw std::runtime_error(
            "AQ5 frozen C16 artifact bytes or SHA-256 drifted");
    }
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(aq5::kRequiredParentPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    if (old_school::learned_model_fingerprint(parent) !=
        aq5::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ5 frozen C16 model fingerprint drifted");
    }
    return parent;
}

old_school::learned_iteration::SeedDomain search_domain(
    aq5::DecisionFamily family) {
    return family == aq5::DecisionFamily::Priority
               ? old_school::learned_iteration::SeedDomain::
                     PrioritySearch
               : old_school::learned_iteration::SeedDomain::
                     AttackSearch;
}

old_school::learned_iteration::SeedDomain choice_domain(
    aq5::DecisionFamily family) {
    return family == aq5::DecisionFamily::Priority
               ? old_school::learned_iteration::SeedDomain::
                     PriorityChoice
               : old_school::learned_iteration::SeedDomain::
                     AttackChoice;
}

std::uint64_t aq6_seed(
    const aq5::PreparedRoot& root, bool choice,
    std::uint64_t root_seed) {
    return old_school::learned_iteration::derive_seed(
        root_seed,
        choice ? choice_domain(root.family)
               : search_domain(root.family),
        0, root.fixture_ordinal,
        static_cast<std::uint64_t>(root.family));
}

std::string_view rb1_selection_reason_name(
    conservative::Rb1SelectionReason reason) {
    switch (reason) {
    case conservative::Rb1SelectionReason::Parent:
        return "Parent";
    case conservative::Rb1SelectionReason::PairedLowerBound:
        return "PairedLowerBound";
    case conservative::Rb1SelectionReason::SaturatedFrontier:
        return "SaturatedFrontier";
    }
    return "Invalid";
}

std::string_view selection_reason_name(
    conservative::SelectionReason reason) {
    switch (reason) {
    case conservative::SelectionReason::Parent:
        return "Parent";
    case conservative::SelectionReason::DominatingSearch:
        return "DominatingSearch";
    case conservative::SelectionReason::SaturatedBoundary:
        return "SaturatedBoundary";
    }
    return "Invalid";
}

bool all_terminal_saturated(
    const aq5::SamplerOutput& scored) {
    if (scored.accounting.rollout_evaluations == 0 ||
        scored.accounting.terminal_evaluations !=
            scored.accounting.rollout_evaluations ||
        scored.candidates.empty() ||
        scored.candidates.front().samples.empty()) {
        return false;
    }
    const double reference =
        scored.candidates.front().samples.front();
    if (reference != 0.0 &&
        reference != 0.5 &&
        reference != 1.0) {
        return false;
    }
    return std::all_of(
        scored.candidates.begin(),
        scored.candidates.end(),
        [reference](const aq5::CandidateScore& candidate) {
            return !candidate.samples.empty() &&
                   std::all_of(
                       candidate.samples.begin(),
                       candidate.samples.end(),
                       [reference](double value) {
                           return value == reference;
                       });
        });
}

std::size_t index_for_key(
    const std::vector<aq5::CandidateScore>& candidates,
    std::string_view key) {
    const auto found = std::find_if(
        candidates.begin(), candidates.end(),
        [key](const aq5::CandidateScore& candidate) {
            return candidate.key == key;
        });
    if (found == candidates.end()) {
        throw std::logic_error(
            "AQ6 parent choice is absent from search candidates");
    }
    return static_cast<std::size_t>(
        std::distance(candidates.begin(), found));
}

conservative::Selection conservative_selection(
    const aq5::SamplerOutput& scored,
    std::size_t parent_index) {
    conservative::SelectorInput input{
        .parent_index = parent_index,
        .terminal_evidence =
            conservative::AllTerminalSaturated{
                .value =
                    all_terminal_saturated(scored),
            },
    };
    input.paired_long_returns.reserve(
        scored.candidates.size());
    std::vector<double> settled_means;
    settled_means.reserve(scored.candidates.size());
    bool complete_settled = true;
    for (const aq5::CandidateScore& candidate :
         scored.candidates) {
        input.paired_long_returns.push_back(
            candidate.samples);
        if (candidate.settled_boundary_mean.has_value()) {
            settled_means.push_back(
                *candidate.settled_boundary_mean);
        } else {
            complete_settled = false;
        }
    }
    if (complete_settled) {
        input.settled_boundary_means =
            std::move(settled_means);
    }
    return conservative::select_action(input);
}

void print_conservative_diagnostic(
    const std::shared_ptr<const old_school::LearnedModel>& parent) {
    const aq5::SamplerApi api =
        aq5::engine_sampler_api();
    const auto roots = aq5::build_fixture_roots();
    std::cout << std::fixed << std::setprecision(6);
    std::cout
        << "AQ6-RB0 conservative rules-boundary diagnostic\n"
        << "  seed: " << kAq6Rb0RootSeed << '\n'
        << "  parent: "
        << old_school::learned_model_fingerprint(parent)
        << '\n';
    for (const aq5::PreparedRoot& root : roots) {
        const std::uint64_t root_search_seed =
            aq6_seed(
                root, false, kAq6Rb0RootSeed);
        const aq5::UntreatedC16RootReport untreated =
            api.capture_untreated_c16(
                root, parent, root_search_seed,
                aq6_seed(
                    root, true, kAq6Rb0RootSeed));
        old_school::LearnedSearchConfig search =
            root.family == aq5::DecisionFamily::Priority
                ? old_school::
                      learned_value_recursive_policy_improvement_priority_config(
                          root_search_seed)
                : old_school::
                      learned_value_recursive_policy_improvement_combat_config(
                          root_search_seed);
        search.capture_settled_boundary_samples =
            root.family == aq5::DecisionFamily::Priority;
        const aq5::SamplerOutput scored =
            api.score_rpi(root, parent, search);
        const std::size_t parent_index =
            index_for_key(
                scored.candidates,
                untreated.selected_key);
        const conservative::Selection selected =
            conservative_selection(scored, parent_index);
        if (selected.action_index >=
            scored.candidates.size()) {
            throw std::logic_error(
                "AQ6 selector returned an invalid action");
        }
        std::cout
            << "  " << root.stable_id
            << " family=" << family_name(root.family)
            << " parent=" << untreated.selected_key
            << " selected="
            << scored.candidates[selected.action_index].key
            << " reason="
            << selection_reason_name(selected.reason)
            << " terminal="
            << scored.accounting.terminal_evaluations
            << '/' << scored.accounting.rollout_evaluations
            << " saturated="
            << (all_terminal_saturated(scored) ? 1 : 0)
            << '\n';
        for (const aq5::CandidateScore& candidate :
             scored.candidates) {
            std::cout
                << "    " << candidate.key
                << " long=" << candidate.mean
                << " cells=";
            for (std::size_t index = 0;
                 index < candidate.samples.size();
                 ++index) {
                std::cout
                    << (index == 0 ? "" : ",")
                    << candidate.samples[index];
            }
            if (candidate.settled_boundary_mean.has_value()) {
                std::cout
                    << " settled="
                    << *candidate.settled_boundary_mean;
            }
            std::cout << '\n';
        }
    }
    std::cout
        << "result=DIAGNOSTIC_ONLY"
        << " web_licensed=0 artifact_published=0\n";
}

struct PairedEstimate {
    double mean = 0.0;
    double standard_error = 0.0;
    double lower_bound = 0.0;
};

PairedEstimate paired_estimate(
    const std::vector<double>& candidate,
    const std::vector<double>& parent) {
    if (candidate.size() != parent.size() ||
        candidate.size() < 2) {
        throw std::logic_error(
            "AQ6 paired estimate shape is invalid");
    }
    long double sum = 0.0L;
    for (std::size_t index = 0;
         index < parent.size(); ++index) {
        sum +=
            static_cast<long double>(candidate[index]) -
            static_cast<long double>(parent[index]);
    }
    const long double mean =
        sum / static_cast<long double>(parent.size());
    long double squared = 0.0L;
    for (std::size_t index = 0;
         index < parent.size(); ++index) {
        const long double difference =
            static_cast<long double>(candidate[index]) -
            static_cast<long double>(parent[index]);
        const long double deviation = difference - mean;
        squared += deviation * deviation;
    }
    const long double standard_error = std::sqrt(
        squared /
        static_cast<long double>(parent.size() - 1) /
        static_cast<long double>(parent.size()));
    return {
        .mean = static_cast<double>(mean),
        .standard_error =
            static_cast<double>(standard_error),
        .lower_bound =
            static_cast<double>(
                mean - standard_error),
    };
}

bool same_bits(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

bool double_rows_bit_identical(
    const std::vector<double>& left,
    const std::vector<double>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        if (!same_bits(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool optional_double_bit_identical(
    const std::optional<double>& left,
    const std::optional<double>& right) {
    return left.has_value() == right.has_value() &&
           (!left.has_value() ||
            same_bits(*left, *right));
}

std::vector<const aq5::CandidateScore*>
candidate_rows_by_key(
    const std::vector<aq5::CandidateScore>& candidates) {
    std::vector<const aq5::CandidateScore*> result;
    result.reserve(candidates.size());
    for (const aq5::CandidateScore& candidate :
         candidates) {
        result.push_back(&candidate);
    }
    std::sort(
        result.begin(), result.end(),
        [](const aq5::CandidateScore* left,
           const aq5::CandidateScore* right) {
            return left->key < right->key;
        });
    if (std::any_of(
            result.begin(), result.end(),
            [](const aq5::CandidateScore* candidate) {
                return candidate->key.empty();
            }) ||
        std::adjacent_find(
            result.begin(), result.end(),
            [](const aq5::CandidateScore* left,
               const aq5::CandidateScore* right) {
                return left->key == right->key;
            }) != result.end()) {
        throw std::logic_error(
            "AQ6 RB1 candidate keys are invalid");
    }
    return result;
}

bool candidate_evidence_bit_identical(
    const std::vector<aq5::CandidateScore>& left,
    const std::vector<aq5::CandidateScore>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    const auto left_rows =
        candidate_rows_by_key(left);
    const auto right_rows =
        candidate_rows_by_key(right);
    for (std::size_t index = 0;
         index < left_rows.size(); ++index) {
        const aq5::CandidateScore& first =
            *left_rows[index];
        const aq5::CandidateScore& second =
            *right_rows[index];
        if (first.key != second.key ||
            !double_rows_bit_identical(
                first.samples, second.samples) ||
            first.terminal_evaluation_flags !=
                second.terminal_evaluation_flags ||
            !double_rows_bit_identical(
                first.settled_boundary_samples,
                second.settled_boundary_samples) ||
            !optional_double_bit_identical(
                first.settled_boundary_mean,
                second.settled_boundary_mean) ||
            first.exact_combat_pure_chump_flags !=
                second.exact_combat_pure_chump_flags ||
            first.exact_combat_bound_fallback_flags !=
                second.exact_combat_bound_fallback_flags ||
            !same_bits(first.mean, second.mean) ||
            first.exact_max != second.exact_max) {
            return false;
        }
    }
    return true;
}

bool sampler_evidence_bit_identical(
    const aq5::SamplerOutput& left,
    const aq5::SamplerOutput& right) {
    return candidate_evidence_bit_identical(
               left.candidates, right.candidates) &&
           left.accounting == right.accounting &&
           left.legal_choice_count ==
               right.legal_choice_count &&
           left.rules_settled == right.rules_settled &&
           left.accounting_consistent ==
               right.accounting_consistent;
}

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

bool candidate_evidence_complete(
    const aq5::CandidateScore& candidate,
    aq5::DecisionFamily family) {
    if (candidate.key.empty() ||
        candidate.samples.size() < 2 ||
        candidate.terminal_evaluation_flags.size() !=
            candidate.samples.size() ||
        candidate.settled_boundary_samples.size() !=
            candidate.samples.size() ||
        !candidate.settled_boundary_mean.has_value() ||
        !probability(candidate.mean) ||
        !probability(
            *candidate.settled_boundary_mean) ||
        !std::all_of(
            candidate.samples.begin(),
            candidate.samples.end(), probability) ||
        !std::all_of(
            candidate.settled_boundary_samples.begin(),
            candidate.settled_boundary_samples.end(),
            probability) ||
        !std::all_of(
            candidate.terminal_evaluation_flags.begin(),
            candidate.terminal_evaluation_flags.end(),
            [](std::uint8_t flag) {
                return flag <= 1U;
            })) {
        return false;
    }

    const bool priority =
        family == aq5::DecisionFamily::Priority;
    if (priority) {
        return candidate
                   .exact_combat_pure_chump_flags.empty() &&
               candidate
                   .exact_combat_bound_fallback_flags.empty();
    }
    if (candidate.exact_combat_pure_chump_flags.size() !=
            candidate.samples.size() ||
        candidate
                .exact_combat_bound_fallback_flags.size() !=
            candidate.samples.size()) {
        return false;
    }
    for (std::size_t sample = 0;
         sample < candidate.samples.size(); ++sample) {
        const std::uint8_t chump =
            candidate.exact_combat_pure_chump_flags[
                sample];
        const std::uint8_t fallback =
            candidate
                .exact_combat_bound_fallback_flags[sample];
        if (chump > 1U || fallback > 1U ||
            (chump != 0U && fallback != 0U)) {
            return false;
        }
    }
    return true;
}

bool output_evidence_complete(
    const aq5::PreparedRoot& root,
    const aq5::SamplerOutput& output) {
    if (output.candidates.size() !=
            root.candidates.size() ||
        output.legal_choice_count !=
            root.candidates.size() ||
        !output.rules_settled ||
        !output.accounting_consistent ||
        output.accounting.sampled_worlds == 0 ||
        output.accounting.rollout_evaluations == 0 ||
        output.accounting.terminal_evaluations >
            output.accounting.rollout_evaluations ||
        output.accounting.bootstrapped_evaluations !=
            output.accounting.rollout_evaluations -
                output.accounting.terminal_evaluations ||
        output.accounting.inner_search_max_depth >
            aq5::kMaximumActiveNesting ||
        ((output.accounting.inner_search_invocations == 0) !=
         (output.accounting.inner_search_max_depth == 0)) ||
        ((output.accounting.inner_search_invocations == 0) !=
         (output.accounting.inner_rollout_evaluations == 0))) {
        return false;
    }
    try {
        static_cast<void>(
            candidate_rows_by_key(output.candidates));
    } catch (const std::logic_error&) {
        return false;
    }
    return std::all_of(
        output.candidates.begin(),
        output.candidates.end(),
        [&root](const aq5::CandidateScore& candidate) {
            return candidate_evidence_complete(
                candidate, root.family);
        });
}

bool has_bound_fallback(
    const aq5::SamplerOutput& output) {
    return std::any_of(
        output.candidates.begin(),
        output.candidates.end(),
        [](const aq5::CandidateScore& candidate) {
            return std::any_of(
                candidate
                    .exact_combat_bound_fallback_flags.begin(),
                candidate
                    .exact_combat_bound_fallback_flags.end(),
                [](std::uint8_t flag) {
                    return flag != 0U;
                });
        });
}

struct Rb1Choice {
    std::string key;
    conservative::Rb1SelectionReason reason =
        conservative::Rb1SelectionReason::Parent;

    bool operator==(const Rb1Choice&) const = default;
};

conservative::Rb1SelectorInput rb1_selector_input(
    const aq5::SamplerOutput& scored,
    std::string_view parent_key,
    std::uint64_t tie_seed) {
    conservative::Rb1SelectorInput input{
        .parent_index =
            index_for_key(scored.candidates, parent_key),
        .tie_seed = tie_seed,
    };
    input.action_keys.reserve(scored.candidates.size());
    input.paired_long_returns.reserve(
        scored.candidates.size());
    input.terminal_flags.reserve(
        scored.candidates.size());
    input.settled_boundary_means.reserve(
        scored.candidates.size());
    const bool has_combat_fallback_rows =
        std::any_of(
            scored.candidates.begin(),
            scored.candidates.end(),
            [](const aq5::CandidateScore& candidate) {
                return !candidate
                            .exact_combat_bound_fallback_flags
                            .empty();
            });
    if (has_combat_fallback_rows) {
        input.bound_fallback_flags.reserve(
            scored.candidates.size());
    }
    for (const aq5::CandidateScore& candidate :
         scored.candidates) {
        if (!candidate.settled_boundary_mean.has_value()) {
            throw std::logic_error(
                "AQ6 RB1 settled evidence is missing");
        }
        input.action_keys.push_back(candidate.key);
        input.paired_long_returns.push_back(
            candidate.samples);
        std::vector<bool> terminal;
        terminal.reserve(
            candidate.terminal_evaluation_flags.size());
        for (const std::uint8_t flag :
             candidate.terminal_evaluation_flags) {
            terminal.push_back(flag != 0U);
        }
        input.terminal_flags.push_back(
            std::move(terminal));
        input.settled_boundary_means.push_back(
            *candidate.settled_boundary_mean);
        if (has_combat_fallback_rows) {
            std::vector<bool> fallbacks;
            fallbacks.reserve(
                candidate
                    .exact_combat_bound_fallback_flags.size());
            for (const std::uint8_t flag :
                 candidate
                     .exact_combat_bound_fallback_flags) {
                fallbacks.push_back(flag != 0U);
            }
            input.bound_fallback_flags.push_back(
                std::move(fallbacks));
        }
    }
    return input;
}

Rb1Choice select_rb1(
    const aq5::SamplerOutput& scored,
    std::string_view parent_key,
    std::uint64_t tie_seed) {
    const conservative::Rb1Selection selected =
        conservative::select_action_rb1(
            rb1_selector_input(
                scored, parent_key, tie_seed));
    if (selected.action_index >=
        scored.candidates.size()) {
        throw std::logic_error(
            "AQ6 RB1 selector returned an invalid action");
    }
    return {
        .key =
            scored.candidates[selected.action_index].key,
        .reason = selected.reason,
    };
}

conservative::Rb2Tc0SelectorInput rb2_tc0_selector_input(
    const aq5::SamplerOutput& primary,
    const aq5::SamplerOutput& next_turn,
    std::string_view parent_key,
    std::uint64_t tie_seed) {
    conservative::Rb2Tc0SelectorInput input{
        .primary =
            rb1_selector_input(
                primary, parent_key, tie_seed),
    };
    input.next_turn_action_keys.reserve(
        next_turn.candidates.size());
    input.next_turn_paired_returns.reserve(
        next_turn.candidates.size());
    const bool has_next_turn_fallback_rows =
        std::any_of(
            next_turn.candidates.begin(),
            next_turn.candidates.end(),
            [](const aq5::CandidateScore& candidate) {
                return !candidate
                            .exact_combat_bound_fallback_flags
                            .empty();
            });
    if (has_next_turn_fallback_rows) {
        input.next_turn_bound_fallback_flags.reserve(
            next_turn.candidates.size());
    }
    for (const aq5::CandidateScore& candidate :
         next_turn.candidates) {
        input.next_turn_action_keys.push_back(
            candidate.key);
        input.next_turn_paired_returns.push_back(
            candidate.samples);
        if (has_next_turn_fallback_rows) {
            std::vector<bool> fallbacks;
            fallbacks.reserve(
                candidate
                    .exact_combat_bound_fallback_flags.size());
            for (const std::uint8_t flag :
                 candidate
                     .exact_combat_bound_fallback_flags) {
                fallbacks.push_back(flag != 0U);
            }
            input.next_turn_bound_fallback_flags.push_back(
                std::move(fallbacks));
        }
    }
    return input;
}

Rb1Choice select_rb2_tc0(
    const aq5::SamplerOutput& primary,
    const aq5::SamplerOutput& next_turn,
    std::string_view parent_key,
    std::uint64_t tie_seed) {
    const conservative::Rb1Selection selected =
        conservative::select_action_rb2_tc0(
            rb2_tc0_selector_input(
                primary, next_turn, parent_key,
                tie_seed));
    if (selected.action_index >=
        primary.candidates.size()) {
        throw std::logic_error(
            "AQ6 RB2-TC0 selector returned an invalid "
            "action");
    }
    return {
        .key =
            primary.candidates[selected.action_index].key,
        .reason = selected.reason,
    };
}

const aq5::CandidateScore& candidate_for_key(
    const aq5::SamplerOutput& output,
    std::string_view key) {
    return output.candidates[
        index_for_key(output.candidates, key)];
}

std::size_t count_flags(
    const std::vector<std::uint8_t>& flags) {
    return static_cast<std::size_t>(
        std::count(flags.begin(), flags.end(), 1U));
}

bool finite_paired_arithmetic(
    const aq5::SamplerOutput& output,
    std::string_view parent_key) {
    const auto& parent =
        candidate_for_key(output, parent_key).samples;
    for (const aq5::CandidateScore& candidate :
         output.candidates) {
        const PairedEstimate estimate =
            paired_estimate(candidate.samples, parent);
        if (!std::isfinite(estimate.mean) ||
            !std::isfinite(estimate.standard_error) ||
            !std::isfinite(estimate.lower_bound) ||
            estimate.standard_error < 0.0) {
            return false;
        }
    }
    return true;
}

bool cross_horizon_shape_aligned(
    const aq5::SamplerOutput& primary,
    const aq5::SamplerOutput& next_turn) {
    if (primary.candidates.size() !=
            next_turn.candidates.size() ||
        primary.legal_choice_count !=
            next_turn.legal_choice_count ||
        primary.accounting.sampled_worlds !=
            next_turn.accounting.sampled_worlds ||
        primary.accounting.rollout_evaluations !=
            next_turn.accounting.rollout_evaluations) {
        return false;
    }
    std::vector<const aq5::CandidateScore*> primary_rows;
    std::vector<const aq5::CandidateScore*> next_turn_rows;
    try {
        primary_rows =
            candidate_rows_by_key(primary.candidates);
        next_turn_rows =
            candidate_rows_by_key(next_turn.candidates);
    } catch (const std::logic_error&) {
        return false;
    }
    for (std::size_t index = 0;
         index < primary_rows.size(); ++index) {
        const aq5::CandidateScore& first =
            *primary_rows[index];
        const aq5::CandidateScore& second =
            *next_turn_rows[index];
        if (first.key != second.key ||
            first.samples.size() != second.samples.size() ||
            first.terminal_evaluation_flags.size() !=
                second.terminal_evaluation_flags.size() ||
            !double_rows_bit_identical(
                first.settled_boundary_samples,
                second.settled_boundary_samples) ||
            !optional_double_bit_identical(
                first.settled_boundary_mean,
                second.settled_boundary_mean) ||
            first.exact_combat_pure_chump_flags !=
                second.exact_combat_pure_chump_flags ||
            first.exact_combat_bound_fallback_flags !=
                second.exact_combat_bound_fallback_flags) {
            return false;
        }
    }
    return true;
}

bool same_search_configuration_except_horizon(
    const old_school::LearnedSearchConfig& primary,
    const old_school::LearnedSearchConfig& next_turn) {
    return primary.seed == next_turn.seed &&
           primary.worlds == next_turn.worlds &&
           primary.rollouts_per_world ==
               next_turn.rollouts_per_world &&
           primary.continuation_variant ==
               next_turn.continuation_variant &&
           same_bits(
               primary.value_continuation_epsilon,
               next_turn.value_continuation_epsilon) &&
           primary.blend_shallow_prior ==
               next_turn.blend_shallow_prior &&
           same_bits(
               primary.value_resolved_shallow_prior_weight,
               next_turn
                   .value_resolved_shallow_prior_weight) &&
           same_bits(
               primary.value_priority_residual_weight,
               next_turn.value_priority_residual_weight) &&
           primary.value_pass_dominance ==
               next_turn.value_pass_dominance &&
           primary.value_continuation_controller ==
               next_turn.value_continuation_controller &&
           primary.evaluation_threads ==
               next_turn.evaluation_threads &&
           primary.capture_priority_h0_boundaries ==
               next_turn.capture_priority_h0_boundaries &&
           primary.value_continuation_search_worlds ==
               next_turn.value_continuation_search_worlds &&
           primary.value_continuation_search_scope ==
               next_turn.value_continuation_search_scope &&
           primary.capture_settled_boundary_samples ==
               next_turn.capture_settled_boundary_samples &&
           primary.use_exact_combat_subgame ==
               next_turn.use_exact_combat_subgame &&
           next_turn.horizon_turns == 0;
}

bool saturated_frontier_witness(
    const aq5::SamplerOutput& output,
    const Rb1Choice& selected) {
    const auto input =
        rb1_selector_input(output, selected.key, 0);
    std::vector<long double> means;
    means.reserve(input.paired_long_returns.size());
    for (const auto& row :
         input.paired_long_returns) {
        long double sum = 0.0L;
        for (const double value : row) {
            sum += static_cast<long double>(value);
        }
        means.push_back(
            sum /
            static_cast<long double>(row.size()));
    }
    const long double maximum =
        *std::max_element(means.begin(), means.end());
    std::vector<std::size_t> frontier;
    for (std::size_t index = 0;
         index < means.size(); ++index) {
        if (means[index] == maximum) {
            frontier.push_back(index);
        }
    }
    if (frontier.size() < 2) {
        return false;
    }
    const double terminal =
        input.paired_long_returns[
            frontier.front()]
            .front();
    if (terminal != 0.0 &&
        terminal != 0.5 &&
        terminal != 1.0) {
        return false;
    }
    for (const std::size_t action : frontier) {
        for (std::size_t sample = 0;
             sample <
                 input.paired_long_returns[action].size();
             ++sample) {
            if (!input.terminal_flags[action][sample] ||
                input.paired_long_returns[action][sample] !=
                    terminal) {
                return false;
            }
        }
    }
    return selected.reason ==
               conservative::Rb1SelectionReason::
                   SaturatedFrontier &&
           std::find_if(
               frontier.begin(), frontier.end(),
               [&input, &selected](std::size_t index) {
                   return input.action_keys[index] ==
                          selected.key;
               }) != frontier.end();
}

bool all_zero(
    const std::vector<std::uint8_t>& flags) {
    return std::all_of(
        flags.begin(), flags.end(),
        [](std::uint8_t flag) {
            return flag == 0U;
        });
}

bool corrected_behavior_passed(
    const aq5::PreparedRoot& root,
    const aq5::SamplerOutput& output,
    const Rb1Choice& selected) {
    const std::string_view id = root.stable_id;
    if (id ==
        "control.blue.counter-redundant-same-target.v1") {
        return selected.key == "pass";
    }
    if (id ==
        "control.blue.counter-same-target-after-"
        "intervening-counter.v1") {
        // All three choices below can complete the same necessary
        // one-Counterspell plan. Countering our own Counterspell cannot.
        return selected.key == "pass" ||
               selected.key ==
                   "counter-same-air-elemental" ||
               selected.key ==
                   "counter-opponent-counterspell";
    }
    if (id == "control.blue.braingeyser-x0.v1") {
        return selected.key == "braingeyser-x1-self";
    }
    if (id ==
        "field.green.second-main-sick-bear-growth.v1") {
        return selected.key == "pass";
    }
    if (id ==
        "field.green.begin-combat-growth-tapped-air.v1") {
        return selected.key ==
               "growth-own-ironroot-treefolk";
    }
    if (id ==
        "control.blue.force-spike-live-gray-ogre.v1") {
        return selected.key ==
               "force-spike-gray-ogre";
    }
    if (id ==
        "control.blue.force-spike-payable-five-open-"
        "gray-ogre.aq0.v1") {
        return selected.key == "pass";
    }
    if (id ==
        "field.ru.life20-flying-men-chump-air.v1") {
        return selected.key == "no-blocks";
    }
    if (id ==
        "field.ru.life4-flying-men-chump-air.v1") {
        return selected.key ==
               "block-air-elemental-with-flying-men";
    }
    if (id ==
        "diagnostic.ru.life20-flying-men-attack-air.v1") {
        return selected.key == "no-attack";
    }
    if (id == aq5::kNewBlueBlockFixtureId) {
        return all_zero(
            candidate_for_key(output, selected.key)
                .exact_combat_pure_chump_flags);
    }
    if (root.fixture_ordinal == 2) {
        const auto& self =
            candidate_for_key(output, "ancestral-self");
        const auto& opponent =
            candidate_for_key(
                output, "ancestral-opponent");
        return selected.key == "ancestral-self" &&
               self.settled_boundary_mean.has_value() &&
               opponent
                   .settled_boundary_mean.has_value() &&
               *self.settled_boundary_mean >
                   *opponent.settled_boundary_mean &&
               saturated_frontier_witness(
                   output, selected);
    }
    return false;
}

bool hscan_direction_correct(
    const aq5::PreparedRoot& root,
    const aq5::SamplerOutput& output,
    const Rb1Choice& selected) {
    if (root.fixture_ordinal == 2) {
        return selected.key == "ancestral-self";
    }
    return corrected_behavior_passed(
        root, output, selected);
}

struct CombatRulesWitness {
    std::size_t legal_block_assignments = 0;
    std::size_t completed_plans = 0;
    bool limits_match_engine = false;
    bool cardinality_passed = false;
    bool damage_order_passed = false;

    bool gate_passed() const {
        return limits_match_engine &&
               cardinality_passed &&
               damage_order_passed;
    }
};

CombatRulesWitness combat_rules_witness() {
    const aq5::PreparedRoot root =
        aq5::make_blue_multi_choice_block_fixture();
    old_school::GameState public_state = root.state;
    for (old_school::PlayerState& player :
         public_state.players) {
        player.hand.clear();
        player.library.clear();
    }
    const exact_combat::Enumeration enumeration =
        exact_combat::enumerate(
            public_state, 1 - root.actor,
            root.attackers, kAq6CombatBounds);
    CombatRulesWitness witness{
        .legal_block_assignments =
            enumeration.legal_block_assignments,
        .completed_plans =
            enumeration.plans.size(),
        .limits_match_engine =
            kAq6CombatBounds.maximum_attackers ==
                    old_school::
                        kLearnedExactCombatMaximumAttackers &&
            kAq6CombatBounds.maximum_blockers ==
                    old_school::
                        kLearnedExactCombatMaximumBlockers &&
            kAq6CombatBounds
                    .maximum_block_assignments ==
                old_school::
                    kLearnedExactCombatMaximumBlockAssignments &&
            kAq6CombatBounds
                    .maximum_damage_orders_per_assignment ==
                old_school::
                    kLearnedExactCombatMaximumDamageOrdersPerAssignment &&
            kAq6CombatBounds.maximum_completed_plans ==
                old_school::
                    kLearnedExactCombatMaximumCompletedPlans,
        .cardinality_passed =
            enumeration.blocker_options.size() == 2 &&
            enumeration.legal_block_assignments == 4 &&
            enumeration.plans.size() == 5,
    };
    if (!witness.cardinality_passed ||
        root.attackers.size() != 1 ||
        root.remaining_blockers.size() != 1) {
        return witness;
    }
    const old_school::PermanentId attacker =
        root.attackers.front();
    const old_school::PermanentId flying =
        root.subject_blocker;
    const old_school::PermanentId air =
        root.remaining_blockers.front();
    const std::vector<std::pair<
        old_school::PermanentId,
        old_school::PermanentId>> flying_first = {
        {attacker, flying},
        {attacker, air},
    };
    const std::vector<std::pair<
        old_school::PermanentId,
        old_school::PermanentId>> air_first = {
        {attacker, air},
        {attacker, flying},
    };
    const auto& first = enumeration.plans[3];
    const auto& second = enumeration.plans[4];
    witness.damage_order_passed =
        first.block_assignment_index == 3 &&
        second.block_assignment_index == 3 &&
        first.damage_order_index == 0 &&
        second.damage_order_index == 1 &&
        first.declared_blocks ==
            second.declared_blocks &&
        first.damage_ordered_blocks ==
            flying_first &&
        second.damage_ordered_blocks == air_first;
    return witness;
}

struct Rb1RootReport {
    std::string stable_id;
    aq5::DecisionFamily family =
        aq5::DecisionFamily::Priority;
    std::string parent_key;
    Rb1Choice selected;
    aq5::SamplerOutput direct;
    bool untreated_capture_complete = false;
    bool behavior_passed = false;
    bool complete_evidence = false;
    bool finite_arithmetic = false;
    bool no_bound_fallback = false;
    bool reversed_bit_identical = false;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_observation_bit_identical = false;
    bool hidden_bit_identical = false;
    bool selected_bit_identical = false;

    bool gate_passed() const {
        return untreated_capture_complete &&
               behavior_passed &&
               complete_evidence &&
               finite_arithmetic &&
               no_bound_fallback &&
               reversed_bit_identical &&
               hidden_repartition_nonvacuous &&
               hidden_observation_bit_identical &&
               hidden_bit_identical &&
               selected_bit_identical;
    }
};

struct Rb1PreflightReport {
    std::string parent_fingerprint;
    std::vector<Rb1RootReport> roots;
    CombatRulesWitness combat_rules;
    bool exact_configuration = false;
    bool default_flags_off = false;
    bool treatment_off_game_bit_identical = false;
    bool ancestral_frontier_witness = false;
    std::size_t maximum_active_nesting = 0;

    bool gate_passed() const {
        return parent_fingerprint ==
                   aq5::kRequiredParentFingerprint &&
               roots.size() ==
                   aq5::kFixtureRootCount &&
               std::all_of(
                   roots.begin(), roots.end(),
                   [](const Rb1RootReport& root) {
                       return root.gate_passed();
                   }) &&
               combat_rules.gate_passed() &&
               exact_configuration &&
               default_flags_off &&
               treatment_off_game_bit_identical &&
               ancestral_frontier_witness &&
               maximum_active_nesting ==
                   aq5::kMaximumActiveNesting;
    }
};

Rb1PreflightReport run_rb1_preflight(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent) {
    const aq5::SamplerApi api =
        aq5::engine_sampler_api();
    const std::vector<aq5::PreparedRoot> roots =
        aq5::build_fixture_roots();
    Rb1PreflightReport report{
        .parent_fingerprint =
            old_school::learned_model_fingerprint(parent),
        .combat_rules = combat_rules_witness(),
    };
    report.roots.reserve(roots.size());
    report.exact_configuration = true;

    for (const aq5::PreparedRoot& root : roots) {
        const std::uint64_t root_search_seed =
            aq6_seed(
                root, false, kAq6Rb1RootSeed);
        const std::uint64_t root_tie_seed =
            aq6_seed(
                root, true, kAq6Rb1RootSeed);
        const aq5::UntreatedC16RootReport untreated =
            api.capture_untreated_c16(
                root, parent, root_search_seed,
                root_tie_seed);
        const bool untreated_capture_complete =
            untreated.stable_id == root.stable_id &&
            untreated.family == root.family &&
            untreated.candidates.size() ==
                root.candidates.size() &&
            untreated.complete_legal_choice_coverage &&
            untreated.finite_scores &&
            !untreated.selected_key.empty();
        if (!untreated_capture_complete) {
            throw std::logic_error(
                "AQ6 RB1 untreated parent capture is "
                "incomplete");
        }
        old_school::LearnedSearchConfig search =
            aq5::outer_search_config(
                root.family, root_search_seed);
        search.capture_settled_boundary_samples = true;
        search.use_exact_combat_subgame = true;
        report.exact_configuration =
            report.exact_configuration &&
            search.seed == root_search_seed &&
            search.capture_settled_boundary_samples &&
            search.use_exact_combat_subgame;

        const aq5::SamplerOutput direct =
            api.score_rpi(root, parent, search);
        const aq5::SamplerOutput reversed =
            api.score_rpi(
                aq5::reverse_candidate_order(root),
                parent, search);
        const aq5::PreparedRoot hidden_root =
            aq5::make_hidden_repartition_clone(root);
        const aq5::SamplerOutput hidden =
            api.score_rpi(
                hidden_root, parent, search);

        const Rb1Choice direct_choice =
            select_rb1(
                direct, untreated.selected_key,
                root_tie_seed);
        const Rb1Choice reversed_choice =
            select_rb1(
                reversed, untreated.selected_key,
                root_tie_seed);
        const Rb1Choice hidden_choice =
            select_rb1(
                hidden, untreated.selected_key,
                root_tie_seed);
        Rb1RootReport root_report{
            .stable_id = root.stable_id,
            .family = root.family,
            .parent_key = untreated.selected_key,
            .selected = direct_choice,
            .direct = direct,
            .untreated_capture_complete =
                untreated_capture_complete,
            .complete_evidence =
                output_evidence_complete(
                    root, direct) &&
                output_evidence_complete(
                    aq5::reverse_candidate_order(root),
                    reversed) &&
                output_evidence_complete(
                    hidden_root, hidden),
            .finite_arithmetic =
                finite_paired_arithmetic(
                    direct, untreated.selected_key) &&
                finite_paired_arithmetic(
                    reversed, untreated.selected_key) &&
                finite_paired_arithmetic(
                    hidden, untreated.selected_key),
            .no_bound_fallback =
                !has_bound_fallback(direct) &&
                !has_bound_fallback(reversed) &&
                !has_bound_fallback(hidden),
            .reversed_bit_identical =
                sampler_evidence_bit_identical(
                    direct, reversed),
            .hidden_repartition_nonvacuous =
                hidden_root.state != root.state,
            .hidden_observation_bit_identical =
                old_school::observe_game_state(
                    hidden_root.state, root.actor) ==
                old_school::observe_game_state(
                    root.state, root.actor),
            .hidden_bit_identical =
                sampler_evidence_bit_identical(
                    direct, hidden),
            .selected_bit_identical =
                direct_choice == reversed_choice &&
                direct_choice == hidden_choice,
        };
        root_report.behavior_passed =
            corrected_behavior_passed(
                root, root_report.direct,
                root_report.selected);
        report.maximum_active_nesting =
            std::max(
                report.maximum_active_nesting,
                root_report.direct.accounting
                    .inner_search_max_depth);
        if (root.fixture_ordinal == 2) {
            report.ancestral_frontier_witness =
                root_report.behavior_passed &&
                saturated_frontier_witness(
                    root_report.direct,
                    root_report.selected);
        }
        report.roots.push_back(
            std::move(root_report));
    }

    const old_school::LearnedSearchConfig default_search;
    const old_school::GameConfig default_game;
    const old_school::BotConfig default_bot;
    report.default_flags_off =
        !default_search
             .capture_settled_boundary_samples &&
        !default_search.use_exact_combat_subgame &&
        !default_game
             .learned_evaluation_exact_combat_subgame &&
        !default_bot.value_recursive_policy_improvement;
    report.treatment_off_game_bit_identical =
        api.treatment_off_fixed_seed_game_bit_identical(
            parent);
    return report;
}

void print_rb1_report(
    const Rb1PreflightReport& report) {
    std::cout << std::fixed << std::setprecision(6);
    std::cout
        << "AQ6-RB1 paired-LCB frontier preflight\n"
        << "  seed: " << kAq6Rb1RootSeed << '\n'
        << "  parent: " << report.parent_fingerprint
        << '\n';
    for (const Rb1RootReport& root : report.roots) {
        const std::size_t parent_index =
            index_for_key(
                root.direct.candidates,
                root.parent_key);
        std::cout
            << "  " << root.stable_id
            << " family=" << family_name(root.family)
            << " parent=" << root.parent_key
            << " selected=" << root.selected.key
            << " reason="
            << rb1_selection_reason_name(
                   root.selected.reason)
            << " untreated="
            << (root.untreated_capture_complete
                    ? "PASS"
                    : "FAIL")
            << " behavior="
            << (root.behavior_passed ? "PASS" : "FAIL")
            << " evidence="
            << (root.complete_evidence ? "PASS" : "FAIL")
            << " reverse="
            << (root.reversed_bit_identical ? "PASS" : "FAIL")
            << " hidden="
            << (root.hidden_bit_identical ? "PASS" : "FAIL")
            << " fallback="
            << (root.no_bound_fallback ? 0 : 1)
            << " terminal="
            << root.direct.accounting.terminal_evaluations
            << '/'
            << root.direct.accounting.rollout_evaluations
            << " inner="
            << root.direct.accounting
                   .inner_rollout_evaluations
            << " depth="
            << root.direct.accounting
                   .inner_search_max_depth
            << " gate="
            << (root.gate_passed() ? "PASS" : "FAIL")
            << '\n';
        for (const aq5::CandidateScore& candidate :
             root.direct.candidates) {
            const PairedEstimate estimate =
                paired_estimate(
                    candidate.samples,
                    root.direct.candidates[parent_index]
                        .samples);
            std::cout
                << "    " << candidate.key
                << " long=" << candidate.mean
                << " delta=" << estimate.mean
                << " se=" << estimate.standard_error
                << " lcb=" << estimate.lower_bound
                << " settled="
                << (candidate
                            .settled_boundary_mean
                            .has_value()
                        ? *candidate
                               .settled_boundary_mean
                        : -1.0);
            if (!candidate
                     .exact_combat_pure_chump_flags.empty()) {
                std::cout
                    << " pure_chump_cells="
                    << count_flags(
                           candidate
                               .exact_combat_pure_chump_flags)
                    << '/'
                    << candidate
                           .exact_combat_pure_chump_flags
                           .size()
                    << " fallback_cells="
                    << count_flags(
                           candidate
                               .exact_combat_bound_fallback_flags)
                    << '/'
                    << candidate
                           .exact_combat_bound_fallback_flags
                           .size();
            }
            std::cout << '\n';
        }
    }
    std::cout
        << "  combat-rules assignments="
        << report.combat_rules.legal_block_assignments
        << " plans="
        << report.combat_rules.completed_plans
        << " limits="
        << (report.combat_rules.limits_match_engine
                ? "PASS"
                : "FAIL")
        << " order="
        << (report.combat_rules.damage_order_passed
                ? "PASS"
                : "FAIL")
        << " gate="
        << (report.combat_rules.gate_passed()
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  ancestral-frontier: "
        << (report.ancestral_frontier_witness
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  maximum-nesting: "
        << report.maximum_active_nesting
        << " gate="
        << (report.maximum_active_nesting ==
                    aq5::kMaximumActiveNesting
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  default-off: "
        << (report.default_flags_off &&
                    report
                        .treatment_off_game_bit_identical
                ? "PASS"
                : "FAIL")
        << '\n'
        << "result="
        << (report.gate_passed() ? "PASS" : "REJECT")
        << " hypothesis_passed="
        << (report.gate_passed() ? 1 : 0)
        << " exact_combat_long_path=1"
        << " web_licensed=0 artifact_published=0\n";
}

bool tc0_selector_protocol_passed(
    const aq5::SamplerOutput& next_turn,
    std::string_view parent_key,
    const Rb1Choice& primary_selected,
    const Rb1Choice& selected) {
    if (primary_selected.reason !=
        conservative::Rb1SelectionReason::
            PairedLowerBound) {
        return selected == primary_selected;
    }
    if (primary_selected.key == parent_key) {
        return false;
    }
    const PairedEstimate next_turn_estimate =
        paired_estimate(
            candidate_for_key(
                next_turn, primary_selected.key)
                .samples,
            candidate_for_key(next_turn, parent_key)
                .samples);
    if (next_turn_estimate.lower_bound > 0.0) {
        return selected == primary_selected;
    }
    return selected.key == parent_key &&
           selected.reason ==
               conservative::Rb1SelectionReason::Parent;
}

bool tc0_mechanism_direction_passed(
    const aq5::PreparedRoot& root,
    const aq5::SamplerOutput& primary,
    const aq5::SamplerOutput& next_turn,
    std::string_view parent_key,
    const Rb1Choice& primary_selected,
    const Rb1Choice& selected) {
    const auto lcb_for_primary_choice =
        [&]() {
            return paired_estimate(
                       candidate_for_key(
                           next_turn,
                           primary_selected.key)
                           .samples,
                       candidate_for_key(
                           next_turn, parent_key)
                           .samples)
                .lower_bound;
        };
    const auto supported_lcb_repair =
        [&](std::string_view expected_key) {
            return primary_selected.key ==
                       expected_key &&
                   primary_selected.key != parent_key &&
                   primary_selected.reason ==
                       conservative::Rb1SelectionReason::
                           PairedLowerBound &&
                   selected == primary_selected &&
                   lcb_for_primary_choice() > 0.0;
        };

    const std::string_view id = root.stable_id;
    if (id == "control.blue.braingeyser-x0.v1") {
        return supported_lcb_repair(
            "braingeyser-x1-self");
    }
    if (id ==
        "field.green.begin-combat-growth-tapped-air.v1") {
        return supported_lcb_repair(
            "growth-own-ironroot-treefolk");
    }
    if (id == aq5::kNewBlueBlockFixtureId) {
        return supported_lcb_repair("no-block");
    }
    if (id ==
        "field.ru.life20-flying-men-chump-air.v1") {
        return parent_key == "no-blocks" &&
               primary_selected.key ==
                   "block-air-elemental-with-flying-men" &&
               primary_selected.reason ==
                   conservative::Rb1SelectionReason::
                       PairedLowerBound &&
               selected.key == parent_key &&
               selected.reason ==
                   conservative::Rb1SelectionReason::Parent &&
               lcb_for_primary_choice() <= 0.0;
    }
    if (root.fixture_ordinal == 2) {
        return primary_selected.key ==
                   "ancestral-self" &&
               primary_selected.reason ==
                   conservative::Rb1SelectionReason::
                       SaturatedFrontier &&
               selected == primary_selected &&
               saturated_frontier_witness(
                   primary, selected);
    }
    return true;
}

struct Tc0RootReport {
    std::string stable_id;
    aq5::DecisionFamily family =
        aq5::DecisionFamily::Priority;
    std::string parent_key;
    Rb1Choice primary_selected;
    Rb1Choice selected;
    aq5::SamplerOutput primary;
    aq5::SamplerOutput next_turn;
    bool untreated_capture_complete = false;
    bool behavior_passed = false;
    bool complete_evidence = false;
    bool finite_arithmetic = false;
    bool no_bound_fallback = false;
    bool primary_reversed_bit_identical = false;
    bool next_turn_reversed_bit_identical = false;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_observation_bit_identical = false;
    bool primary_hidden_bit_identical = false;
    bool next_turn_hidden_bit_identical = false;
    bool cross_horizon_shape_aligned = false;
    bool primary_selected_bit_identical = false;
    bool selected_bit_identical = false;
    bool selector_protocol_passed = false;
    bool mechanism_direction_passed = false;

    bool gate_passed() const {
        return untreated_capture_complete &&
               behavior_passed &&
               complete_evidence &&
               finite_arithmetic &&
               no_bound_fallback &&
               primary_reversed_bit_identical &&
               next_turn_reversed_bit_identical &&
               hidden_repartition_nonvacuous &&
               hidden_observation_bit_identical &&
               primary_hidden_bit_identical &&
               next_turn_hidden_bit_identical &&
               cross_horizon_shape_aligned &&
               primary_selected_bit_identical &&
               selected_bit_identical &&
               selector_protocol_passed &&
               mechanism_direction_passed;
    }
};

struct Tc0PreflightReport {
    std::string parent_fingerprint;
    std::vector<Tc0RootReport> roots;
    CombatRulesWitness combat_rules;
    bool exact_configuration = false;
    bool default_flags_off = false;
    bool treatment_off_game_bit_identical = false;
    bool ancestral_frontier_witness = false;
    std::size_t maximum_active_nesting = 0;

    bool gate_passed() const {
        return parent_fingerprint ==
                   aq5::kRequiredParentFingerprint &&
               roots.size() ==
                   aq5::kFixtureRootCount &&
               std::all_of(
                   roots.begin(), roots.end(),
                   [](const Tc0RootReport& root) {
                       return root.gate_passed();
                   }) &&
               combat_rules.gate_passed() &&
               exact_configuration &&
               default_flags_off &&
               treatment_off_game_bit_identical &&
               ancestral_frontier_witness &&
               maximum_active_nesting ==
                   aq5::kMaximumActiveNesting;
    }
};

Tc0PreflightReport run_tc0_preflight(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent) {
    const aq5::SamplerApi api =
        aq5::engine_sampler_api();
    const std::vector<aq5::PreparedRoot> roots =
        aq5::build_fixture_roots();
    Tc0PreflightReport report{
        .parent_fingerprint =
            old_school::learned_model_fingerprint(parent),
        .combat_rules = combat_rules_witness(),
    };
    report.roots.reserve(roots.size());
    report.exact_configuration = true;
    const aq5::SealedRecipe recipe =
        aq5::sealed_recipe();

    for (const aq5::PreparedRoot& root : roots) {
        const std::uint64_t root_search_seed =
            aq6_seed(
                root, false, kAq6Rb2Tc0RootSeed);
        const std::uint64_t root_tie_seed =
            aq6_seed(
                root, true, kAq6Rb2Tc0RootSeed);
        const aq5::UntreatedC16RootReport untreated =
            api.capture_untreated_c16(
                root, parent, root_search_seed,
                root_tie_seed);
        const bool untreated_capture_complete =
            untreated.stable_id == root.stable_id &&
            untreated.family == root.family &&
            untreated.candidates.size() ==
                root.candidates.size() &&
            untreated.complete_legal_choice_coverage &&
            untreated.finite_scores &&
            !untreated.selected_key.empty();
        if (!untreated_capture_complete) {
            throw std::logic_error(
                "AQ6 RB2-TC0 untreated parent capture is "
                "incomplete");
        }

        old_school::LearnedSearchConfig primary_search =
            aq5::outer_search_config(
                root.family, root_search_seed);
        primary_search.capture_settled_boundary_samples =
            true;
        primary_search.use_exact_combat_subgame = true;
        old_school::LearnedSearchConfig next_turn_search =
            primary_search;
        next_turn_search.horizon_turns = 0;
        const std::size_t expected_primary_horizon =
            root.family == aq5::DecisionFamily::Priority
                ? recipe.priority_outer.horizon_turns
                : recipe.combat_outer.horizon_turns;
        report.exact_configuration =
            report.exact_configuration &&
            primary_search.seed == root_search_seed &&
            primary_search.horizon_turns ==
                expected_primary_horizon &&
            primary_search
                .capture_settled_boundary_samples &&
            primary_search.use_exact_combat_subgame &&
            same_search_configuration_except_horizon(
                primary_search, next_turn_search);

        const aq5::PreparedRoot reversed_root =
            aq5::reverse_candidate_order(root);
        const aq5::PreparedRoot hidden_root =
            aq5::make_hidden_repartition_clone(root);
        const aq5::SamplerOutput primary =
            api.score_rpi(root, parent, primary_search);
        const aq5::SamplerOutput primary_reversed =
            api.score_rpi(
                reversed_root, parent, primary_search);
        const aq5::SamplerOutput primary_hidden =
            api.score_rpi(
                hidden_root, parent, primary_search);
        const aq5::SamplerOutput next_turn =
            api.score_rpi(
                root, parent, next_turn_search);
        const aq5::SamplerOutput next_turn_reversed =
            api.score_rpi(
                reversed_root, parent, next_turn_search);
        const aq5::SamplerOutput next_turn_hidden =
            api.score_rpi(
                hidden_root, parent, next_turn_search);

        const Rb1Choice primary_choice =
            select_rb1(
                primary, untreated.selected_key,
                root_tie_seed);
        const Rb1Choice primary_reversed_choice =
            select_rb1(
                primary_reversed, untreated.selected_key,
                root_tie_seed);
        const Rb1Choice primary_hidden_choice =
            select_rb1(
                primary_hidden, untreated.selected_key,
                root_tie_seed);
        const Rb1Choice selected =
            select_rb2_tc0(
                primary, next_turn,
                untreated.selected_key, root_tie_seed);
        const Rb1Choice reversed_selected =
            select_rb2_tc0(
                primary_reversed, next_turn_reversed,
                untreated.selected_key, root_tie_seed);
        const Rb1Choice hidden_selected =
            select_rb2_tc0(
                primary_hidden, next_turn_hidden,
                untreated.selected_key, root_tie_seed);

        Tc0RootReport root_report{
            .stable_id = root.stable_id,
            .family = root.family,
            .parent_key = untreated.selected_key,
            .primary_selected = primary_choice,
            .selected = selected,
            .primary = primary,
            .next_turn = next_turn,
            .untreated_capture_complete =
                untreated_capture_complete,
            .complete_evidence =
                output_evidence_complete(
                    root, primary) &&
                output_evidence_complete(
                    reversed_root,
                    primary_reversed) &&
                output_evidence_complete(
                    hidden_root, primary_hidden) &&
                output_evidence_complete(
                    root, next_turn) &&
                output_evidence_complete(
                    reversed_root,
                    next_turn_reversed) &&
                output_evidence_complete(
                    hidden_root, next_turn_hidden),
            .finite_arithmetic =
                finite_paired_arithmetic(
                    primary,
                    untreated.selected_key) &&
                finite_paired_arithmetic(
                    primary_reversed,
                    untreated.selected_key) &&
                finite_paired_arithmetic(
                    primary_hidden,
                    untreated.selected_key) &&
                finite_paired_arithmetic(
                    next_turn,
                    untreated.selected_key) &&
                finite_paired_arithmetic(
                    next_turn_reversed,
                    untreated.selected_key) &&
                finite_paired_arithmetic(
                    next_turn_hidden,
                    untreated.selected_key),
            .no_bound_fallback =
                !has_bound_fallback(primary) &&
                !has_bound_fallback(primary_reversed) &&
                !has_bound_fallback(primary_hidden) &&
                !has_bound_fallback(next_turn) &&
                !has_bound_fallback(
                    next_turn_reversed) &&
                !has_bound_fallback(next_turn_hidden),
            .primary_reversed_bit_identical =
                sampler_evidence_bit_identical(
                    primary, primary_reversed),
            .next_turn_reversed_bit_identical =
                sampler_evidence_bit_identical(
                    next_turn, next_turn_reversed),
            .hidden_repartition_nonvacuous =
                hidden_root.state != root.state,
            .hidden_observation_bit_identical =
                old_school::observe_game_state(
                    hidden_root.state, root.actor) ==
                old_school::observe_game_state(
                    root.state, root.actor),
            .primary_hidden_bit_identical =
                sampler_evidence_bit_identical(
                    primary, primary_hidden),
            .next_turn_hidden_bit_identical =
                sampler_evidence_bit_identical(
                    next_turn, next_turn_hidden),
            .cross_horizon_shape_aligned =
                cross_horizon_shape_aligned(
                    primary, next_turn) &&
                cross_horizon_shape_aligned(
                    primary_reversed,
                    next_turn_reversed) &&
                cross_horizon_shape_aligned(
                    primary_hidden,
                    next_turn_hidden),
            .primary_selected_bit_identical =
                primary_choice ==
                    primary_reversed_choice &&
                primary_choice == primary_hidden_choice,
            .selected_bit_identical =
                selected == reversed_selected &&
                selected == hidden_selected,
            .selector_protocol_passed =
                tc0_selector_protocol_passed(
                    next_turn, untreated.selected_key,
                    primary_choice, selected) &&
                tc0_selector_protocol_passed(
                    next_turn_reversed,
                    untreated.selected_key,
                    primary_reversed_choice,
                    reversed_selected) &&
                tc0_selector_protocol_passed(
                    next_turn_hidden,
                    untreated.selected_key,
                    primary_hidden_choice,
                    hidden_selected),
            .mechanism_direction_passed =
                tc0_mechanism_direction_passed(
                    root, primary, next_turn,
                    untreated.selected_key,
                    primary_choice, selected),
        };
        root_report.behavior_passed =
            corrected_behavior_passed(
                root, root_report.primary,
                root_report.selected);
        report.maximum_active_nesting =
            std::max({
                report.maximum_active_nesting,
                root_report.primary.accounting
                    .inner_search_max_depth,
                root_report.next_turn.accounting
                    .inner_search_max_depth,
            });
        if (root.fixture_ordinal == 2) {
            report.ancestral_frontier_witness =
                root_report.behavior_passed &&
                saturated_frontier_witness(
                    root_report.primary,
                    root_report.selected);
        }
        report.roots.push_back(
            std::move(root_report));
    }

    const old_school::LearnedSearchConfig default_search;
    const old_school::GameConfig default_game;
    const old_school::BotConfig default_bot;
    report.default_flags_off =
        !default_search
             .capture_settled_boundary_samples &&
        !default_search.use_exact_combat_subgame &&
        !default_game
             .learned_evaluation_exact_combat_subgame &&
        !default_bot.value_recursive_policy_improvement;
    report.treatment_off_game_bit_identical =
        api.treatment_off_fixed_seed_game_bit_identical(
            parent);
    return report;
}

std::string_view tc0_disposition_name(
    const Tc0RootReport& root) {
    if (root.primary_selected.reason ==
            conservative::Rb1SelectionReason::Parent) {
        return "Parent";
    }
    if (root.primary_selected.reason ==
            conservative::Rb1SelectionReason::
                SaturatedFrontier) {
        return "FrontierPassThrough";
    }
    return root.selected == root.primary_selected
               ? "Concordant"
               : "Veto";
}

void print_tc0_report(
    const Tc0PreflightReport& report) {
    std::cout << std::fixed << std::setprecision(6);
    std::cout
        << "AQ6-RB2-TC0 two-timescale concordance "
           "preflight\n"
        << "  seed: " << kAq6Rb2Tc0RootSeed << '\n'
        << "  parent: " << report.parent_fingerprint
        << '\n';
    for (const Tc0RootReport& root : report.roots) {
        const auto& next_turn_parent =
            candidate_for_key(
                root.next_turn, root.parent_key);
        const auto& next_turn_primary_choice =
            candidate_for_key(
                root.next_turn,
                root.primary_selected.key);
        const PairedEstimate next_turn_estimate =
            paired_estimate(
                next_turn_primary_choice.samples,
                next_turn_parent.samples);
        std::cout
            << "  " << root.stable_id
            << " family=" << family_name(root.family)
            << " parent=" << root.parent_key
            << " primary="
            << root.primary_selected.key
            << " primary_reason="
            << rb1_selection_reason_name(
                   root.primary_selected.reason)
            << " selected=" << root.selected.key
            << " reason="
            << rb1_selection_reason_name(
                   root.selected.reason)
            << " tc0=" << tc0_disposition_name(root)
            << " h0_delta=" << next_turn_estimate.mean
            << " h0_se="
            << next_turn_estimate.standard_error
            << " h0_lcb="
            << next_turn_estimate.lower_bound
            << " behavior="
            << (root.behavior_passed ? "PASS" : "FAIL")
            << " evidence="
            << (root.complete_evidence ? "PASS" : "FAIL")
            << " reverse="
            << (root.primary_reversed_bit_identical &&
                        root
                            .next_turn_reversed_bit_identical
                    ? "PASS"
                    : "FAIL")
            << " hidden="
            << (root.primary_hidden_bit_identical &&
                        root.next_turn_hidden_bit_identical
                    ? "PASS"
                    : "FAIL")
            << " cross-shape="
            << (root.cross_horizon_shape_aligned
                    ? "PASS"
                    : "FAIL")
            << " selector="
            << (root.selector_protocol_passed
                    ? "PASS"
                    : "FAIL")
            << " mechanism="
            << (root.mechanism_direction_passed
                    ? "PASS"
                    : "FAIL")
            << " fallback="
            << (root.no_bound_fallback ? 0 : 1)
            << " terminal="
            << root.primary.accounting
                   .terminal_evaluations
            << '/'
            << root.primary.accounting
                   .rollout_evaluations
            << " h0-terminal="
            << root.next_turn.accounting
                   .terminal_evaluations
            << '/'
            << root.next_turn.accounting
                   .rollout_evaluations
            << " inner="
            << root.primary.accounting
                   .inner_rollout_evaluations
            << '/'
            << root.next_turn.accounting
                   .inner_rollout_evaluations
            << " depth="
            << root.primary.accounting
                   .inner_search_max_depth
            << '/'
            << root.next_turn.accounting
                   .inner_search_max_depth
            << " gate="
            << (root.gate_passed() ? "PASS" : "FAIL")
            << '\n';
        const std::size_t primary_parent_index =
            index_for_key(
                root.primary.candidates,
                root.parent_key);
        for (const aq5::CandidateScore& candidate :
             root.primary.candidates) {
            const aq5::CandidateScore& h0 =
                candidate_for_key(
                    root.next_turn, candidate.key);
            const PairedEstimate primary_estimate =
                paired_estimate(
                    candidate.samples,
                    root.primary
                        .candidates[primary_parent_index]
                        .samples);
            const PairedEstimate h0_estimate =
                paired_estimate(
                    h0.samples,
                    next_turn_parent.samples);
            std::cout
                << "    " << candidate.key
                << " primary=" << candidate.mean
                << " primary_lcb="
                << primary_estimate.lower_bound
                << " h0=" << h0.mean
                << " h0_lcb="
                << h0_estimate.lower_bound;
            if (!candidate
                     .exact_combat_pure_chump_flags.empty()) {
                std::cout
                    << " pure_chump_cells="
                    << count_flags(
                           candidate
                               .exact_combat_pure_chump_flags)
                    << '/'
                    << candidate
                           .exact_combat_pure_chump_flags
                           .size()
                    << " fallback_cells="
                    << count_flags(
                           candidate
                               .exact_combat_bound_fallback_flags)
                    << '/'
                    << candidate
                           .exact_combat_bound_fallback_flags
                           .size();
            }
            std::cout << '\n';
        }
    }
    std::cout
        << "  combat-rules assignments="
        << report.combat_rules.legal_block_assignments
        << " plans="
        << report.combat_rules.completed_plans
        << " limits="
        << (report.combat_rules.limits_match_engine
                ? "PASS"
                : "FAIL")
        << " order="
        << (report.combat_rules.damage_order_passed
                ? "PASS"
                : "FAIL")
        << " gate="
        << (report.combat_rules.gate_passed()
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  ancestral-frontier: "
        << (report.ancestral_frontier_witness
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  maximum-nesting: "
        << report.maximum_active_nesting
        << " gate="
        << (report.maximum_active_nesting ==
                    aq5::kMaximumActiveNesting
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  exact-configuration: "
        << (report.exact_configuration
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  default-off: "
        << (report.default_flags_off &&
                    report
                        .treatment_off_game_bit_identical
                ? "PASS"
                : "FAIL")
        << '\n'
        << "result="
        << (report.gate_passed() ? "PASS" : "REJECT")
        << " hypothesis_passed="
        << (report.gate_passed() ? 1 : 0)
        << " primary_horizon=sealed"
        << " next_turn_horizon=0"
        << " web_licensed=0 artifact_published=0\n";
}

enum class HscanContinuationPolicy : std::uint8_t {
    FrozenC16,
    OneLevelRpi,
};

constexpr std::array<std::size_t, 5> kHscanHorizons{
    0, 1, 2, 4, 8,
};

constexpr std::array<HscanContinuationPolicy, 2>
    kHscanContinuationPolicies{
        HscanContinuationPolicy::FrozenC16,
        HscanContinuationPolicy::OneLevelRpi,
    };

constexpr std::array<aq5::DecisionFamily, 3>
    kHscanFamilies{
        aq5::DecisionFamily::Priority,
        aq5::DecisionFamily::Attack,
        aq5::DecisionFamily::Block,
    };

std::string_view hscan_policy_name(
    HscanContinuationPolicy policy) {
    switch (policy) {
    case HscanContinuationPolicy::FrozenC16:
        return "FrozenC16";
    case HscanContinuationPolicy::OneLevelRpi:
        return "OneLevelRPI";
    }
    return "Invalid";
}

bool search_config_bit_identical(
    const old_school::LearnedSearchConfig& left,
    const old_school::LearnedSearchConfig& right) {
    return left.seed == right.seed &&
           left.worlds == right.worlds &&
           left.rollouts_per_world ==
               right.rollouts_per_world &&
           left.horizon_turns == right.horizon_turns &&
           left.continuation_variant ==
               right.continuation_variant &&
           same_bits(
               left.value_continuation_epsilon,
               right.value_continuation_epsilon) &&
           left.blend_shallow_prior ==
               right.blend_shallow_prior &&
           same_bits(
               left.value_resolved_shallow_prior_weight,
               right.value_resolved_shallow_prior_weight) &&
           same_bits(
               left.value_priority_residual_weight,
               right.value_priority_residual_weight) &&
           left.value_pass_dominance ==
               right.value_pass_dominance &&
           left.value_continuation_controller ==
               right.value_continuation_controller &&
           left.evaluation_threads ==
               right.evaluation_threads &&
           left.capture_priority_h0_boundaries ==
               right.capture_priority_h0_boundaries &&
           left.value_continuation_search_worlds ==
               right.value_continuation_search_worlds &&
           left.value_continuation_search_scope ==
               right.value_continuation_search_scope &&
           left.capture_settled_boundary_samples ==
               right.capture_settled_boundary_samples &&
           left.use_exact_combat_subgame ==
               right.use_exact_combat_subgame;
}

bool hscan_keyed_sample_shapes_match(
    const aq5::SamplerOutput& left,
    const aq5::SamplerOutput& right) {
    if (left.legal_choice_count !=
            right.legal_choice_count ||
        left.accounting.sampled_worlds !=
            right.accounting.sampled_worlds ||
        left.accounting.rollout_evaluations !=
            right.accounting.rollout_evaluations ||
        left.candidates.size() !=
            right.candidates.size()) {
        return false;
    }
    std::vector<const aq5::CandidateScore*> left_rows;
    std::vector<const aq5::CandidateScore*> right_rows;
    try {
        left_rows =
            candidate_rows_by_key(left.candidates);
        right_rows =
            candidate_rows_by_key(right.candidates);
    } catch (const std::logic_error&) {
        return false;
    }
    for (std::size_t index = 0;
         index < left_rows.size(); ++index) {
        const aq5::CandidateScore& first =
            *left_rows[index];
        const aq5::CandidateScore& second =
            *right_rows[index];
        if (first.key != second.key ||
            first.samples.size() !=
                second.samples.size() ||
            first.terminal_evaluation_flags.size() !=
                second.terminal_evaluation_flags.size() ||
            first.settled_boundary_samples.size() !=
                second.settled_boundary_samples.size() ||
            !double_rows_bit_identical(
                first.settled_boundary_samples,
                second.settled_boundary_samples) ||
            !optional_double_bit_identical(
                first.settled_boundary_mean,
                second.settled_boundary_mean) ||
            first.exact_combat_pure_chump_flags.size() !=
                second.exact_combat_pure_chump_flags.size() ||
            first.exact_combat_pure_chump_flags !=
                second.exact_combat_pure_chump_flags ||
            first.exact_combat_bound_fallback_flags.size() !=
                second
                    .exact_combat_bound_fallback_flags.size() ||
            first.exact_combat_bound_fallback_flags !=
                second
                    .exact_combat_bound_fallback_flags) {
            return false;
        }
    }
    return true;
}

struct HscanParentCapture {
    std::string key;
    bool complete = false;
};

struct HscanCellReport {
    std::size_t horizon = 0;
    HscanContinuationPolicy policy =
        HscanContinuationPolicy::FrozenC16;
    Rb1Choice selected;
    PairedEstimate parent_relative;
    aq5::SamplerOutput output;
    bool direction_correct = false;
    bool rb1_mechanism_passed = false;
    bool exact_configuration = false;
    bool complete_evidence = false;
    bool finite_arithmetic = false;
    bool no_bound_fallback = false;
    bool nesting_valid = false;
    bool zero_exposure_identity = true;

    bool invariant_gate_passed() const {
        return exact_configuration &&
               complete_evidence &&
               finite_arithmetic &&
               no_bound_fallback &&
               nesting_valid &&
               zero_exposure_identity;
    }
};

struct HscanRootReport {
    std::string stable_id;
    aq5::DecisionFamily family =
        aq5::DecisionFamily::Priority;
    std::uint64_t search_seed = 0;
    std::uint64_t tie_seed = 0;
    std::string parent_key;
    bool untreated_capture_complete = false;
    bool common_keyed_sample_shapes = false;
    std::vector<HscanCellReport> cells;

    bool invariant_gate_passed() const {
        return untreated_capture_complete &&
               common_keyed_sample_shapes &&
               cells.size() ==
                   kHscanHorizons.size() *
                       kHscanContinuationPolicies.size() &&
               std::all_of(
                   cells.begin(), cells.end(),
                   [](const HscanCellReport& cell) {
                       return cell
                           .invariant_gate_passed();
                   });
    }
};

struct HscanReport {
    std::string parent_fingerprint;
    std::vector<HscanRootReport> roots;
    std::size_t parent_capture_count = 0;
    std::size_t cell_count = 0;
    bool frozen_nesting_zero = true;
    bool rpi_nesting_bounded = true;
    bool rpi_nesting_nonvacuous = false;
    std::size_t zero_exposure_cell_count = 0;
    std::size_t zero_exposure_identity_count = 0;
    bool zero_exposure_identity = true;

    bool invariant_gate_passed() const {
        return parent_fingerprint ==
                   aq5::kRequiredParentFingerprint &&
               roots.size() ==
                   aq5::kFixtureRootCount &&
               parent_capture_count ==
                   aq5::kFixtureRootCount &&
               cell_count ==
                   aq5::kFixtureRootCount *
                       kHscanHorizons.size() *
                       kHscanContinuationPolicies.size() &&
               frozen_nesting_zero &&
               rpi_nesting_bounded &&
               rpi_nesting_nonvacuous &&
               zero_exposure_identity &&
               zero_exposure_identity_count ==
                   zero_exposure_cell_count &&
               std::all_of(
                   roots.begin(), roots.end(),
                   [](const HscanRootReport& root) {
                       return root
                           .invariant_gate_passed();
                   });
    }
};

HscanReport run_horizon_census(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent) {
    const aq5::SamplerApi api =
        aq5::engine_sampler_api();
    const std::vector<aq5::PreparedRoot> roots =
        aq5::build_fixture_roots();
    std::vector<HscanParentCapture> parents;
    parents.reserve(roots.size());

    // The parent census is deliberately completed before the first
    // treatment score. Each root is captured exactly once.
    for (const aq5::PreparedRoot& root : roots) {
        const std::uint64_t search_seed =
            aq6_seed(
                root, false, kAq6Hscan0RootSeed);
        const std::uint64_t tie_seed =
            aq6_seed(
                root, true, kAq6Hscan0RootSeed);
        const aq5::UntreatedC16RootReport untreated =
            api.capture_untreated_c16(
                root, parent, search_seed, tie_seed);
        HscanParentCapture capture{
            .key = untreated.selected_key,
            .complete =
                untreated.stable_id == root.stable_id &&
                untreated.family == root.family &&
                untreated.candidates.size() ==
                    root.candidates.size() &&
                untreated.complete_legal_choice_coverage &&
                untreated.finite_scores &&
                !untreated.selected_key.empty(),
        };
        if (!capture.complete) {
            throw std::logic_error(
                "AQ6-HSCAN0 untreated parent capture is "
                "incomplete");
        }
        parents.push_back(std::move(capture));
    }

    HscanReport report{
        .parent_fingerprint =
            old_school::learned_model_fingerprint(parent),
        .parent_capture_count = parents.size(),
    };
    report.roots.reserve(roots.size());
    for (std::size_t root_index = 0;
         root_index < roots.size(); ++root_index) {
        const aq5::PreparedRoot& root =
            roots[root_index];
        const HscanParentCapture& parent_capture =
            parents[root_index];
        const std::uint64_t search_seed =
            aq6_seed(
                root, false, kAq6Hscan0RootSeed);
        const std::uint64_t tie_seed =
            aq6_seed(
                root, true, kAq6Hscan0RootSeed);
        const old_school::LearnedSearchConfig sealed =
            aq5::outer_search_config(
                root.family, search_seed);
        const std::size_t expected_worlds =
            root.family == aq5::DecisionFamily::Priority
                ? old_school::
                      kLearnedValueActorLocalSearchWorlds
                : old_school::
                      kLearnedValueRecursivePolicyImprovementCombatWorlds;

        HscanRootReport root_report{
            .stable_id = root.stable_id,
            .family = root.family,
            .search_seed = search_seed,
            .tie_seed = tie_seed,
            .parent_key = parent_capture.key,
            .untreated_capture_complete =
                parent_capture.complete,
        };
        root_report.cells.reserve(
            kHscanHorizons.size() *
            kHscanContinuationPolicies.size());

        for (const HscanContinuationPolicy policy :
             kHscanContinuationPolicies) {
            for (const std::size_t horizon :
                 kHscanHorizons) {
                old_school::LearnedSearchConfig search =
                    sealed;
                search.horizon_turns = horizon;
                search.capture_settled_boundary_samples =
                    true;
                search.use_exact_combat_subgame = true;
                if (policy ==
                    HscanContinuationPolicy::FrozenC16) {
                    search
                        .value_continuation_search_worlds =
                        0;
                }
                old_school::LearnedSearchConfig expected =
                    sealed;
                expected.horizon_turns = horizon;
                expected
                    .capture_settled_boundary_samples =
                    true;
                expected.use_exact_combat_subgame = true;
                if (policy ==
                    HscanContinuationPolicy::FrozenC16) {
                    expected
                        .value_continuation_search_worlds =
                        0;
                }
                const bool exact_configuration =
                    search_config_bit_identical(
                        search, expected) &&
                    search.seed == search_seed &&
                    search.worlds == expected_worlds &&
                    search.rollouts_per_world == 1 &&
                    search.horizon_turns == horizon &&
                    search.continuation_variant ==
                        old_school::LearnedVariant::
                            ValueSearchChampion &&
                    search.value_continuation_epsilon ==
                        0.0 &&
                    !search.blend_shallow_prior &&
                    search
                            .value_resolved_shallow_prior_weight ==
                        0.0 &&
                    search
                            .value_priority_residual_weight ==
                        0.0 &&
                    !search.value_pass_dominance &&
                    search.value_continuation_controller ==
                        old_school::
                            LearnedContinuationController::
                                Legacy &&
                    search.evaluation_threads ==
                        sealed.evaluation_threads &&
                    !search
                         .capture_priority_h0_boundaries &&
                    search
                            .value_continuation_search_scope ==
                        old_school::
                            LearnedContinuationSearchScope::
                                AllDecisions &&
                    search
                        .capture_settled_boundary_samples &&
                    search.use_exact_combat_subgame &&
                    (policy ==
                             HscanContinuationPolicy::
                                 FrozenC16
                         ? search
                                   .value_continuation_search_worlds ==
                               0
                         : search
                                   .value_continuation_search_worlds ==
                                   sealed
                                       .value_continuation_search_worlds &&
                               search
                                       .value_continuation_search_worlds >
                                   0);

                aq5::SamplerOutput output =
                    api.score_rpi(
                        root, parent, search);
                const Rb1Choice selected =
                    select_rb1(
                        output, parent_capture.key,
                        tie_seed);
                const PairedEstimate estimate =
                    paired_estimate(
                        candidate_for_key(
                            output, selected.key)
                            .samples,
                        candidate_for_key(
                            output, parent_capture.key)
                            .samples);
                const bool frozen =
                    policy ==
                    HscanContinuationPolicy::FrozenC16;
                const bool nesting_valid =
                    frozen
                        ? output.accounting
                                      .inner_rollout_evaluations ==
                                  0 &&
                              output.accounting
                                      .inner_search_invocations ==
                                  0 &&
                              output.accounting
                                      .inner_search_max_depth ==
                                  0
                        : output.accounting
                                  .inner_search_max_depth <=
                              aq5::kMaximumActiveNesting;
                report.frozen_nesting_zero =
                    report.frozen_nesting_zero &&
                    (!frozen || nesting_valid);
                report.rpi_nesting_bounded =
                    report.rpi_nesting_bounded &&
                    (frozen || nesting_valid);
                report.rpi_nesting_nonvacuous =
                    report.rpi_nesting_nonvacuous ||
                    (!frozen &&
                     output.accounting
                             .inner_search_invocations >
                         0);

                HscanCellReport cell{
                    .horizon = horizon,
                    .policy = policy,
                    .selected = selected,
                    .parent_relative = estimate,
                    .output = std::move(output),
                    .exact_configuration =
                        exact_configuration,
                    .nesting_valid = nesting_valid,
                };
                cell.direction_correct =
                    hscan_direction_correct(
                        root, cell.output,
                        cell.selected);
                cell.rb1_mechanism_passed =
                    corrected_behavior_passed(
                        root, cell.output,
                        cell.selected);
                cell.complete_evidence =
                    output_evidence_complete(
                        root, cell.output);
                cell.finite_arithmetic =
                    finite_paired_arithmetic(
                        cell.output,
                        parent_capture.key);
                cell.no_bound_fallback =
                    !has_bound_fallback(cell.output);
                root_report.cells.push_back(
                    std::move(cell));
                ++report.cell_count;
            }
        }

        for (HscanCellReport& cell :
             root_report.cells) {
            if (cell.policy !=
                    HscanContinuationPolicy::OneLevelRpi ||
                cell.output.accounting
                        .inner_search_invocations != 0) {
                continue;
            }
            ++report.zero_exposure_cell_count;
            const auto frozen = std::find_if(
                root_report.cells.begin(),
                root_report.cells.end(),
                [&cell](
                    const HscanCellReport& candidate) {
                    return candidate.policy ==
                               HscanContinuationPolicy::
                                   FrozenC16 &&
                           candidate.horizon ==
                               cell.horizon;
                });
            cell.zero_exposure_identity =
                frozen != root_report.cells.end() &&
                sampler_evidence_bit_identical(
                    cell.output, frozen->output) &&
                cell.selected == frozen->selected;
            report.zero_exposure_identity =
                report.zero_exposure_identity &&
                cell.zero_exposure_identity;
            report.zero_exposure_identity_count +=
                cell.zero_exposure_identity ? 1U : 0U;
        }

        root_report.common_keyed_sample_shapes =
            !root_report.cells.empty();
        if (!root_report.cells.empty()) {
            const aq5::SamplerOutput& reference =
                root_report.cells.front().output;
            for (const HscanCellReport& cell :
                 root_report.cells) {
                root_report.common_keyed_sample_shapes =
                    root_report
                        .common_keyed_sample_shapes &&
                    hscan_keyed_sample_shapes_match(
                        reference, cell.output);
            }
        }
        report.roots.push_back(
            std::move(root_report));
    }
    return report;
}

const HscanCellReport& hscan_cell(
    const HscanRootReport& root,
    HscanContinuationPolicy policy,
    std::size_t horizon) {
    const auto found = std::find_if(
        root.cells.begin(), root.cells.end(),
        [policy, horizon](
            const HscanCellReport& cell) {
            return cell.policy == policy &&
                   cell.horizon == horizon;
        });
    if (found == root.cells.end()) {
        throw std::logic_error(
            "AQ6-HSCAN0 report cell is missing");
    }
    return *found;
}

void print_horizon_census(
    const HscanReport& report) {
    std::cout << std::fixed << std::setprecision(6);
    std::cout
        << "AQ6-HSCAN0 horizon/continuation census\n"
        << "  seed: " << kAq6Hscan0RootSeed << '\n'
        << "  parent: " << report.parent_fingerprint
        << '\n';
    for (const HscanRootReport& root : report.roots) {
        std::cout
            << "  root " << root.stable_id
            << " family=" << family_name(root.family)
            << " search_seed=" << root.search_seed
            << " tie_seed=" << root.tie_seed
            << " parent=" << root.parent_key
            << " untreated="
            << (root.untreated_capture_complete
                    ? "PASS"
                    : "FAIL")
            << " common-shape="
            << (root.common_keyed_sample_shapes
                    ? "PASS"
                    : "FAIL")
            << '\n';
        for (const HscanCellReport& cell :
             root.cells) {
            const aq5::SearchAccounting& accounting =
                cell.output.accounting;
            const bool one_level =
                cell.policy ==
                HscanContinuationPolicy::OneLevelRpi;
            const bool rpi_exposed =
                one_level &&
                accounting.inner_search_invocations > 0;
            std::cout
                << "    policy="
                << hscan_policy_name(cell.policy)
                << " horizon=" << cell.horizon
                << " selected=" << cell.selected.key
                << " reason="
                << rb1_selection_reason_name(
                       cell.selected.reason)
                << " direction_correct="
                << (cell.direction_correct
                        ? "PASS"
                        : "FAIL")
                << " rb1_mechanism="
                << (cell.rb1_mechanism_passed
                        ? "PASS"
                        : "FAIL")
                << " delta="
                << cell.parent_relative.mean
                << " se="
                << cell.parent_relative.standard_error
                << " lcb="
                << cell.parent_relative.lower_bound
                << " terminal="
                << accounting.terminal_evaluations
                << '/'
                << accounting.rollout_evaluations
                << " inner_rollouts="
                << accounting.inner_rollout_evaluations
                << " inner_invocations="
                << accounting.inner_search_invocations
                << " inner_depth="
                << accounting.inner_search_max_depth
                << " rpi_active="
                << (rpi_exposed ? 1 : 0)
                << " rpi_exposure="
                << (one_level
                        ? (rpi_exposed
                               ? "ACTIVE"
                               : "NONE")
                        : "DISABLED");
            if (one_level) {
                const HscanCellReport& frozen =
                    hscan_cell(
                        root,
                        HscanContinuationPolicy::
                            FrozenC16,
                        cell.horizon);
                const bool agrees =
                    frozen.selected.key ==
                    cell.selected.key;
                std::cout
                    << " vs_frozen="
                    << (agrees ? "AGREE" : "DIFFER");
                if (!rpi_exposed) {
                    std::cout
                        << " interpretation="
                           "NO_POLICY_EXPOSURE"
                        << " zero_exposure_identity="
                        << (cell.zero_exposure_identity
                                ? "PASS"
                                : "FAIL");
                }
            }
            std::cout
                << " worlds="
                << accounting.sampled_worlds
                << " samples_per_action="
                << (cell.output.candidates.empty()
                        ? 0
                        : cell.output.candidates.front()
                              .samples.size())
                << " config="
                << (cell.exact_configuration
                        ? "PASS"
                        : "FAIL")
                << " invariant="
                << (cell.invariant_gate_passed()
                        ? "PASS"
                        : "FAIL")
                << '\n';
        }
    }

    std::cout << "  rpi-exposure-summary\n";
    std::size_t total_active = 0;
    for (const HscanRootReport& root : report.roots) {
        for (const std::size_t horizon :
             kHscanHorizons) {
            total_active +=
                hscan_cell(
                    root,
                    HscanContinuationPolicy::
                        OneLevelRpi,
                    horizon)
                        .output.accounting
                        .inner_search_invocations > 0
                    ? 1U
                    : 0U;
        }
    }
    std::cout
        << "    all-cells active=" << total_active
        << '/'
        << report.roots.size() *
               kHscanHorizons.size()
        << '\n';
    for (const std::size_t horizon :
         kHscanHorizons) {
        std::size_t aggregate_active = 0;
        for (const HscanRootReport& root :
             report.roots) {
            aggregate_active +=
                hscan_cell(
                    root,
                    HscanContinuationPolicy::
                        OneLevelRpi,
                    horizon)
                        .output.accounting
                        .inner_search_invocations > 0
                    ? 1U
                    : 0U;
        }
        std::cout
            << "    horizon=" << horizon
            << " family=All active="
            << aggregate_active << '/'
            << report.roots.size() << '\n';
        for (const aq5::DecisionFamily family :
             kHscanFamilies) {
            std::size_t active = 0;
            std::size_t total = 0;
            for (const HscanRootReport& root :
                 report.roots) {
                if (root.family != family) {
                    continue;
                }
                ++total;
                active +=
                    hscan_cell(
                        root,
                        HscanContinuationPolicy::
                            OneLevelRpi,
                        horizon)
                            .output.accounting
                            .inner_search_invocations > 0
                        ? 1U
                        : 0U;
            }
            std::cout
                << "    horizon=" << horizon
                << " family="
                << family_name(family)
                << " active=" << active
                << '/' << total << '\n';
        }
    }

    std::cout << "  correctness-summary\n";
    for (const HscanContinuationPolicy policy :
         kHscanContinuationPolicies) {
        for (const std::size_t horizon :
             kHscanHorizons) {
            std::size_t aggregate_correct = 0;
            for (const HscanRootReport& root :
                 report.roots) {
                aggregate_correct +=
                    hscan_cell(
                        root, policy, horizon)
                            .direction_correct
                        ? 1U
                        : 0U;
            }
            std::cout
                << "    policy="
                << hscan_policy_name(policy)
                << " horizon=" << horizon
                << " family=All correct="
                << aggregate_correct << '/'
                << report.roots.size() << '\n';
            for (const aq5::DecisionFamily family :
                 kHscanFamilies) {
                std::size_t correct = 0;
                std::size_t total = 0;
                for (const HscanRootReport& root :
                     report.roots) {
                    if (root.family != family) {
                        continue;
                    }
                    ++total;
                    correct +=
                        hscan_cell(
                            root, policy, horizon)
                                .direction_correct
                            ? 1U
                            : 0U;
                }
                std::cout
                    << "    policy="
                    << hscan_policy_name(policy)
                    << " horizon=" << horizon
                    << " family="
                    << family_name(family)
                    << " correct=" << correct
                    << '/' << total << '\n';
            }
        }
    }

    std::cout << "  root-transitions\n";
    for (const HscanRootReport& root : report.roots) {
        for (const HscanContinuationPolicy policy :
             kHscanContinuationPolicies) {
            std::vector<std::string> distinct_keys;
            std::size_t selection_transitions = 0;
            std::size_t correctness_transitions = 0;
            const HscanCellReport* previous = nullptr;
            std::cout
                << "    " << root.stable_id
                << " policy="
                << hscan_policy_name(policy)
                << " path=";
            for (const std::size_t horizon :
                 kHscanHorizons) {
                const HscanCellReport& cell =
                    hscan_cell(root, policy, horizon);
                distinct_keys.push_back(
                    cell.selected.key);
                if (previous != nullptr) {
                    selection_transitions +=
                        previous->selected.key !=
                                cell.selected.key
                            ? 1U
                            : 0U;
                    correctness_transitions +=
                        previous->direction_correct !=
                                cell.direction_correct
                            ? 1U
                            : 0U;
                }
                std::cout
                    << (previous == nullptr ? "" : ",")
                    << horizon << ':'
                    << cell.selected.key << '/'
                    << (cell.direction_correct
                            ? "C"
                            : "I");
                previous = &cell;
            }
            std::sort(
                distinct_keys.begin(),
                distinct_keys.end());
            distinct_keys.erase(
                std::unique(
                    distinct_keys.begin(),
                    distinct_keys.end()),
                distinct_keys.end());
            std::cout
                << " distinct_keys="
                << distinct_keys.size()
                << " selection_transitions="
                << selection_transitions
                << " correctness_transitions="
                << correctness_transitions << '\n';
        }
    }

    std::cout
        << "  parent-captures="
        << report.parent_capture_count
        << " expected=" << aq5::kFixtureRootCount
        << '\n'
        << "  cells=" << report.cell_count
        << " expected="
        << aq5::kFixtureRootCount *
               kHscanHorizons.size() *
               kHscanContinuationPolicies.size()
        << '\n'
        << "  frozen-nesting-zero="
        << (report.frozen_nesting_zero
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  rpi-nesting-bounded="
        << (report.rpi_nesting_bounded
                ? "PASS"
                : "FAIL")
        << " nonvacuous="
        << (report.rpi_nesting_nonvacuous
                ? "PASS"
                : "FAIL")
        << '\n'
        << "  zero-exposure-identity="
        << report.zero_exposure_identity_count
        << '/' << report.zero_exposure_cell_count
        << " gate="
        << (report.zero_exposure_identity
                    &&
                    report
                            .zero_exposure_identity_count ==
                        report.zero_exposure_cell_count
                ? "PASS"
                : "FAIL")
        << '\n'
        << "result="
        << (report.invariant_gate_passed()
                ? "DIAGNOSTIC_ONLY"
                : "INVARIANT_VOID")
        << " invariant_gate="
        << (report.invariant_gate_passed()
                ? "PASS"
                : "FAIL")
        << " direction_non_gating=1"
        << " rb1_mechanism_non_gating=1"
        << " web_licensed=0 artifact_published=0\n";
}

void print_report(const aq5::PreflightReport& report) {
    std::cout << std::fixed << std::setprecision(6);
    std::cout
        << "AQ5-RPI0 recursive policy-improvement preflight\n"
        << "  seed: " << report.recipe.root_seed << '\n'
        << "  parent: " << report.parent_fingerprint << '\n'
        << "  untreated-before-RPI: "
        << (report.untreated.gate_passed() ? "PASS" : "FAIL")
        << '\n';
    for (const auto& root : report.untreated.roots) {
        std::cout
            << "    " << root.stable_id
            << " selected=" << root.selected_key
            << " margin=" << root.selected_margin << '\n';
    }
    for (const auto& fixture : report.fixtures) {
        std::cout
            << "  fixture[" << fixture.spec.ordinal << "] "
            << family_name(fixture.spec.family)
            << " direction="
            << (fixture.direction_passed ? "PASS" : "FAIL")
            << " gate="
            << (fixture.gate_passed() ? "PASS" : "FAIL")
            << '\n';
        for (const auto& root : fixture.roots) {
            std::cout
                << "    " << root.stable_id
                << " selected=" << root.selected_key
                << " outer="
                << root.accounting.rollout_evaluations
                << " inner="
                << root.accounting.inner_rollout_evaluations
                << " invocations="
                << root.accounting.inner_search_invocations
                << " depth="
                << root.accounting.inner_search_max_depth
                << '\n';
            for (const auto& candidate : root.candidates) {
                std::cout
                    << "      " << candidate.key
                    << '=' << candidate.mean
                    << (candidate.exact_max ? " *" : "")
                    << '\n';
            }
        }
    }
    for (const auto& family : report.families) {
        std::cout
            << "  family " << family_name(family.family)
            << " roots=" << family.roots
            << " gate="
            << (family.gate_passed() ? "PASS" : "FAIL")
            << " max-depth="
            << family.maximum_active_nesting << '\n';
    }
    std::cout
        << "  isolation: "
        << (report.isolation.gate_passed() ? "PASS" : "FAIL")
        << '\n'
        << "result="
        << (report.gate_passed() ? "PASS" : "REJECT")
        << " hypothesis_passed="
        << (report.hypothesis_passed ? 1 : 0)
        << " web_licensed=0"
        << " artifact_published=0\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2 ||
        (std::string_view(argv[1]) != "--run" &&
         std::string_view(argv[1]) !=
             "--run-conservative" &&
         std::string_view(argv[1]) !=
             "--run-conservative-rb1" &&
         std::string_view(argv[1]) !=
             "--run-conservative-rb2-tc0" &&
         std::string_view(argv[1]) !=
             "--run-horizon-census")) {
        std::cerr
            << "Usage: "
            << (argc > 0 ? argv[0] : "old-school-action-q-rpi")
            << " (--run|--run-conservative|"
               "--run-conservative-rb1|"
               "--run-conservative-rb2-tc0|"
               "--run-horizon-census)\n";
        return 2;
    }
    try {
        const auto parent = load_parent();
        if (std::string_view(argv[1]) ==
            "--run-conservative") {
            print_conservative_diagnostic(parent);
            return 0;
        }
        if (std::string_view(argv[1]) ==
            "--run-conservative-rb1") {
            const Rb1PreflightReport report =
                run_rb1_preflight(parent);
            print_rb1_report(report);
            return report.gate_passed() ? 0 : 1;
        }
        if (std::string_view(argv[1]) ==
            "--run-conservative-rb2-tc0") {
            const Tc0PreflightReport report =
                run_tc0_preflight(parent);
            print_tc0_report(report);
            return report.gate_passed() ? 0 : 1;
        }
        if (std::string_view(argv[1]) ==
            "--run-horizon-census") {
            const HscanReport report =
                run_horizon_census(parent);
            print_horizon_census(report);
            return report.invariant_gate_passed()
                       ? 0
                       : 1;
        }
        const aq5::PreflightReport report =
            aq5::run_preflight(parent);
        print_report(report);
        return report.gate_passed() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=aq5_rpi_preflight_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
