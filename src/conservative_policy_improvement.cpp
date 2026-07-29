#include "old_school/conservative_policy_improvement.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

namespace old_school::conservative_policy_improvement {
namespace {

bool exact_terminal_value(double value) {
    return value == 0.0 || value == 0.5 ||
           value == 1.0;
}

std::size_t validate_long_returns(
    const SelectorInput& input) {
    const std::size_t action_count =
        input.paired_long_returns.size();
    if (action_count == 0) {
        throw std::invalid_argument(
            "conservative selector requires an action");
    }
    if (input.parent_index >= action_count) {
        throw std::invalid_argument(
            "conservative selector parent index is invalid");
    }

    const std::size_t sample_count =
        input.paired_long_returns.front().size();
    if (sample_count == 0) {
        throw std::invalid_argument(
            "conservative selector requires paired samples");
    }
    for (const auto& row : input.paired_long_returns) {
        if (row.size() != sample_count) {
            throw std::invalid_argument(
                "conservative selector sample rows are ragged");
        }
        for (const double value : row) {
            if (!std::isfinite(value)) {
                throw std::invalid_argument(
                    "conservative selector return is non-finite");
            }
        }
    }
    return sample_count;
}

void validate_settled_means(const SelectorInput& input) {
    if (!input.settled_boundary_means.has_value()) {
        return;
    }
    const auto& means = *input.settled_boundary_means;
    if (means.size() != input.paired_long_returns.size()) {
        throw std::invalid_argument(
            "conservative selector settled means have "
            "the wrong shape");
    }
    for (const double value : means) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "conservative selector settled mean is "
                "non-finite");
        }
    }
}

bool values_are_fully_saturated(
    const std::vector<std::vector<double>>& values) {
    const double reference = values.front().front();
    if (!exact_terminal_value(reference)) {
        return false;
    }
    return std::all_of(
        values.begin(), values.end(),
        [reference](const std::vector<double>& row) {
            return std::all_of(
                row.begin(), row.end(),
                [reference](double value) {
                    return value == reference;
                });
        });
}

bool saturation_from_cell_flags(
    const SelectorInput& input,
    const PerCellTerminalFlags& evidence,
    std::size_t sample_count) {
    if (evidence.values.size() !=
        input.paired_long_returns.size()) {
        throw std::invalid_argument(
            "conservative selector terminal flags have "
            "the wrong action count");
    }

    bool all_terminal = true;
    for (std::size_t action = 0;
         action < evidence.values.size(); ++action) {
        const auto& flags = evidence.values[action];
        if (flags.size() != sample_count) {
            throw std::invalid_argument(
                "conservative selector terminal flags are "
                "ragged");
        }
        for (std::size_t sample = 0;
             sample < sample_count; ++sample) {
            if (flags[sample] &&
                !exact_terminal_value(
                    input.paired_long_returns[action][sample])) {
                throw std::invalid_argument(
                    "terminal flag marks a nonterminal value");
            }
            all_terminal = all_terminal && flags[sample];
        }
    }
    return all_terminal &&
           values_are_fully_saturated(
               input.paired_long_returns);
}

bool terminal_saturated(
    const SelectorInput& input,
    std::size_t sample_count) {
    if (const auto* flags =
            std::get_if<PerCellTerminalFlags>(
                &input.terminal_evidence)) {
        return saturation_from_cell_flags(
            input, *flags, sample_count);
    }

    const auto& indicator =
        std::get<AllTerminalSaturated>(
            input.terminal_evidence);
    if (!indicator.value) {
        return false;
    }
    if (!values_are_fully_saturated(
            input.paired_long_returns)) {
        throw std::invalid_argument(
            "terminal-saturation indicator conflicts "
            "with long returns");
    }
    return true;
}

long double mean(
    const std::vector<double>& values) {
    long double sum = 0.0L;
    for (const double value : values) {
        sum += static_cast<long double>(value);
    }
    const long double result =
        sum / static_cast<long double>(values.size());
    if (!std::isfinite(result)) {
        throw std::invalid_argument(
            "conservative selector mean is non-finite");
    }
    return result;
}

bool strictly_dominates_parent(
    const std::vector<double>& candidate,
    const std::vector<double>& parent) {
    bool strictly_better = false;
    for (std::size_t sample = 0;
         sample < parent.size(); ++sample) {
        if (candidate[sample] < parent[sample]) {
            return false;
        }
        strictly_better =
            strictly_better ||
            candidate[sample] > parent[sample];
    }
    return strictly_better;
}

Selection select_saturated_boundary(
    const SelectorInput& input) {
    if (!input.settled_boundary_means.has_value()) {
        return {
            .action_index = input.parent_index,
            .reason = SelectionReason::Parent,
        };
    }

    const auto& means = *input.settled_boundary_means;
    const double maximum =
        *std::max_element(means.begin(), means.end());
    std::size_t maximum_index = input.parent_index;
    std::size_t maximum_count = 0;
    for (std::size_t action = 0;
         action < means.size(); ++action) {
        if (means[action] == maximum) {
            maximum_index = action;
            ++maximum_count;
        }
    }
    if (maximum_count != 1 ||
        maximum_index == input.parent_index) {
        return {
            .action_index = input.parent_index,
            .reason = SelectionReason::Parent,
        };
    }
    return {
        .action_index = maximum_index,
        .reason = SelectionReason::SaturatedBoundary,
    };
}

Selection select_dominating_search(
    const SelectorInput& input) {
    const auto& parent =
        input.paired_long_returns[input.parent_index];
    const long double parent_mean = mean(parent);
    std::size_t best_index = input.parent_index;
    long double best_mean = parent_mean;
    bool best_tied = false;

    for (std::size_t action = 0;
         action < input.paired_long_returns.size();
         ++action) {
        if (action == input.parent_index ||
            !strictly_dominates_parent(
                input.paired_long_returns[action],
                parent)) {
            continue;
        }
        const long double candidate_mean =
            mean(input.paired_long_returns[action]);
        if (candidate_mean > best_mean) {
            best_index = action;
            best_mean = candidate_mean;
            best_tied = false;
        } else if (
            candidate_mean == best_mean &&
            best_index != input.parent_index) {
            best_tied = true;
        }
    }

    if (best_index == input.parent_index || best_tied) {
        return {
            .action_index = input.parent_index,
            .reason = SelectionReason::Parent,
        };
    }
    return {
        .action_index = best_index,
        .reason = SelectionReason::DominatingSearch,
    };
}

struct Rb1PairedEstimate {
    long double mean = 0.0L;
    long double standard_error = 0.0L;
    long double lower_bound = 0.0L;
};

std::size_t validate_rb1_input(
    const Rb1SelectorInput& input) {
    const std::size_t action_count =
        input.paired_long_returns.size();
    if (action_count == 0) {
        throw std::invalid_argument(
            "RB1 selector requires an action");
    }
    if (input.parent_index >= action_count) {
        throw std::invalid_argument(
            "RB1 selector parent index is invalid");
    }
    if (input.action_keys.size() != action_count ||
        input.terminal_flags.size() != action_count ||
        input.settled_boundary_means.size() !=
            action_count ||
        (!input.bound_fallback_flags.empty() &&
         input.bound_fallback_flags.size() !=
             action_count)) {
        throw std::invalid_argument(
            "RB1 selector action evidence has the wrong "
            "shape");
    }

    std::unordered_set<std::string> unique_keys;
    unique_keys.reserve(action_count);
    for (const std::string& key : input.action_keys) {
        if (key.empty() ||
            !unique_keys.insert(key).second) {
            throw std::invalid_argument(
                "RB1 selector action keys must be unique "
                "and nonempty");
        }
    }

    const std::size_t sample_count =
        input.paired_long_returns.front().size();
    if (sample_count < 2) {
        throw std::invalid_argument(
            "RB1 selector requires at least two paired "
            "samples");
    }
    for (std::size_t action = 0;
         action < action_count; ++action) {
        const auto& returns =
            input.paired_long_returns[action];
        const auto& terminal =
            input.terminal_flags[action];
        if (returns.size() != sample_count ||
            terminal.size() != sample_count ||
            (!input.bound_fallback_flags.empty() &&
             input.bound_fallback_flags[action].size() !=
                 sample_count)) {
            throw std::invalid_argument(
                "RB1 selector paired evidence is ragged");
        }
        for (std::size_t sample = 0;
             sample < sample_count; ++sample) {
            const double value = returns[sample];
            if (!std::isfinite(value) ||
                value < 0.0 || value > 1.0) {
                throw std::invalid_argument(
                    "RB1 selector return is outside [0,1]");
            }
            if (terminal[sample] &&
                !exact_terminal_value(value)) {
                throw std::invalid_argument(
                    "RB1 terminal flag marks a "
                    "nonterminal value");
            }
        }
        const double settled =
            input.settled_boundary_means[action];
        if (!std::isfinite(settled) ||
            settled < 0.0 || settled > 1.0) {
            throw std::invalid_argument(
                "RB1 settled mean is outside [0,1]");
        }
    }
    return sample_count;
}

bool rb1_has_bound_fallback(
    const Rb1SelectorInput& input) {
    return std::any_of(
        input.bound_fallback_flags.begin(),
        input.bound_fallback_flags.end(),
        [](const std::vector<bool>& row) {
            return std::any_of(
                row.begin(), row.end(),
                [](bool value) {
                    return value;
                });
        });
}

void validate_rb2_tc0_input(
    const Rb2Tc0SelectorInput& input,
    std::size_t primary_sample_count) {
    const std::size_t action_count =
        input.primary.action_keys.size();
    if (input.next_turn_action_keys.size() !=
            action_count ||
        input.next_turn_paired_returns.size() !=
            action_count ||
        (!input.next_turn_bound_fallback_flags.empty() &&
         input.next_turn_bound_fallback_flags.size() !=
             action_count)) {
        throw std::invalid_argument(
            "RB2-TC0 next-turn action evidence has the "
            "wrong shape");
    }

    std::unordered_set<std::string> next_turn_keys;
    next_turn_keys.reserve(action_count);
    for (const std::string& key :
         input.next_turn_action_keys) {
        if (key.empty() ||
            !next_turn_keys.insert(key).second) {
            throw std::invalid_argument(
                "RB2-TC0 next-turn action keys must be "
                "unique and nonempty");
        }
    }
    for (const std::string& key :
         input.primary.action_keys) {
        if (!next_turn_keys.contains(key)) {
            throw std::invalid_argument(
                "RB2-TC0 action keys do not align");
        }
    }

    for (std::size_t action = 0;
         action < action_count; ++action) {
        const auto& returns =
            input.next_turn_paired_returns[action];
        if (returns.size() != primary_sample_count ||
            (!input.next_turn_bound_fallback_flags.empty() &&
             input.next_turn_bound_fallback_flags[action]
                     .size() != primary_sample_count)) {
            throw std::invalid_argument(
                "RB2-TC0 next-turn paired evidence is "
                "ragged or not world-paired with Primary");
        }
        for (const double value : returns) {
            if (!std::isfinite(value) ||
                value < 0.0 || value > 1.0) {
                throw std::invalid_argument(
                    "RB2-TC0 next-turn return is outside "
                    "[0,1]");
            }
        }
    }
}

bool rb2_tc0_has_bound_fallback(
    const Rb2Tc0SelectorInput& input) {
    if (rb1_has_bound_fallback(input.primary)) {
        return true;
    }
    return std::any_of(
        input.next_turn_bound_fallback_flags.begin(),
        input.next_turn_bound_fallback_flags.end(),
        [](const std::vector<bool>& row) {
            return std::any_of(
                row.begin(), row.end(),
                [](bool value) {
                    return value;
                });
        });
}

std::size_t rb2_tc0_next_turn_index(
    const Rb2Tc0SelectorInput& input,
    std::string_view key) {
    const auto found = std::find(
        input.next_turn_action_keys.begin(),
        input.next_turn_action_keys.end(), key);
    if (found == input.next_turn_action_keys.end()) {
        throw std::logic_error(
            "validated RB2-TC0 action key disappeared");
    }
    return static_cast<std::size_t>(
        found - input.next_turn_action_keys.begin());
}

Rb1PairedEstimate paired_lower_bound(
    const std::vector<double>& candidate,
    const std::vector<double>& parent) {
    const std::size_t sample_count = parent.size();
    long double difference_sum = 0.0L;
    for (std::size_t sample = 0;
         sample < sample_count; ++sample) {
        difference_sum +=
            static_cast<long double>(candidate[sample]) -
            static_cast<long double>(parent[sample]);
    }
    const long double difference_mean =
        difference_sum /
        static_cast<long double>(sample_count);

    long double squared_deviation_sum = 0.0L;
    for (std::size_t sample = 0;
         sample < sample_count; ++sample) {
        const long double difference =
            static_cast<long double>(candidate[sample]) -
            static_cast<long double>(parent[sample]);
        const long double deviation =
            difference - difference_mean;
        squared_deviation_sum += deviation * deviation;
    }
    const long double sample_variance =
        squared_deviation_sum /
        static_cast<long double>(sample_count - 1);
    const long double standard_error = std::sqrt(
        sample_variance /
        static_cast<long double>(sample_count));
    const long double lower_bound =
        difference_mean - standard_error;
    if (!std::isfinite(difference_mean) ||
        !std::isfinite(sample_variance) ||
        !std::isfinite(standard_error) ||
        !std::isfinite(lower_bound) ||
        sample_variance < 0.0L ||
        standard_error < 0.0L) {
        throw std::invalid_argument(
            "RB1 paired estimate is invalid");
    }
    return {
        .mean = difference_mean,
        .standard_error = standard_error,
        .lower_bound = lower_bound,
    };
}

std::uint64_t stable_key_hash(std::string_view key) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : key) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value =
        (value ^ (value >> 30U)) *
        0xbf58476d1ce4e5b9ULL;
    value =
        (value ^ (value >> 27U)) *
        0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::size_t seeded_key_choice(
    const Rb1SelectorInput& input,
    const std::vector<std::size_t>& tied) {
    if (tied.empty()) {
        throw std::logic_error(
            "RB1 tie chooser received no actions");
    }
    std::size_t selected = tied.front();
    std::uint64_t selected_rank = splitmix64(
        input.tie_seed ^
        stable_key_hash(input.action_keys[selected]));
    for (std::size_t offset = 1;
         offset < tied.size(); ++offset) {
        const std::size_t candidate = tied[offset];
        const std::uint64_t rank = splitmix64(
            input.tie_seed ^
            stable_key_hash(input.action_keys[candidate]));
        if (rank > selected_rank ||
            (rank == selected_rank &&
             input.action_keys[candidate] <
                 input.action_keys[selected])) {
            selected = candidate;
            selected_rank = rank;
        }
    }
    return selected;
}

std::vector<long double> rb1_action_means(
    const Rb1SelectorInput& input) {
    std::vector<long double> means;
    means.reserve(input.paired_long_returns.size());
    for (const auto& row : input.paired_long_returns) {
        means.push_back(mean(row));
    }
    return means;
}

std::optional<Rb1Selection>
select_rb1_saturated_frontier(
    const Rb1SelectorInput& input,
    const std::vector<long double>& means) {
    const long double maximum =
        *std::max_element(means.begin(), means.end());
    std::vector<std::size_t> frontier;
    for (std::size_t action = 0;
         action < means.size(); ++action) {
        if (means[action] == maximum) {
            frontier.push_back(action);
        }
    }
    if (frontier.size() < 2) {
        return std::nullopt;
    }

    const double shared_terminal =
        input.paired_long_returns[
            frontier.front()]
            .front();
    if (!exact_terminal_value(shared_terminal)) {
        return std::nullopt;
    }
    for (const std::size_t action : frontier) {
        for (std::size_t sample = 0;
             sample <
                 input.paired_long_returns[action].size();
             ++sample) {
            if (!input.terminal_flags[action][sample] ||
                input.paired_long_returns[action][sample] !=
                    shared_terminal) {
                return std::nullopt;
            }
        }
    }

    double largest_settled =
        -std::numeric_limits<double>::infinity();
    std::vector<std::size_t> settled_tie;
    for (const std::size_t action : frontier) {
        const double settled =
            input.settled_boundary_means[action];
        if (settled > largest_settled) {
            largest_settled = settled;
            settled_tie = {action};
        } else if (settled == largest_settled) {
            settled_tie.push_back(action);
        }
    }

    std::size_t selected = settled_tie.front();
    if (settled_tie.size() > 1) {
        const auto parent =
            std::find(
                settled_tie.begin(), settled_tie.end(),
                input.parent_index);
        selected =
            parent != settled_tie.end()
                ? input.parent_index
                : seeded_key_choice(input, settled_tie);
    }
    return Rb1Selection{
        .action_index = selected,
        .reason =
            selected == input.parent_index
                ? Rb1SelectionReason::Parent
                : Rb1SelectionReason::
                      SaturatedFrontier,
    };
}

Rb1Selection select_rb1_paired_lower_bound(
    const Rb1SelectorInput& input) {
    const auto& parent =
        input.paired_long_returns[input.parent_index];
    long double largest_lower_bound =
        -std::numeric_limits<long double>::infinity();
    long double largest_mean =
        -std::numeric_limits<long double>::infinity();
    std::vector<std::size_t> tied;

    for (std::size_t action = 0;
         action < input.paired_long_returns.size();
         ++action) {
        if (action == input.parent_index) {
            continue;
        }
        const Rb1PairedEstimate estimate =
            paired_lower_bound(
                input.paired_long_returns[action],
                parent);
        if (!(estimate.lower_bound > 0.0L)) {
            continue;
        }
        if (estimate.lower_bound >
                largest_lower_bound ||
            (estimate.lower_bound ==
                 largest_lower_bound &&
             estimate.mean > largest_mean)) {
            largest_lower_bound =
                estimate.lower_bound;
            largest_mean = estimate.mean;
            tied = {action};
        } else if (
            estimate.lower_bound ==
                largest_lower_bound &&
            estimate.mean == largest_mean) {
            tied.push_back(action);
        }
    }

    if (tied.empty()) {
        return {
            .action_index = input.parent_index,
            .reason = Rb1SelectionReason::Parent,
        };
    }
    const std::size_t selected =
        tied.size() == 1
            ? tied.front()
            : seeded_key_choice(input, tied);
    return {
        .action_index = selected,
        .reason =
            Rb1SelectionReason::PairedLowerBound,
    };
}

} // namespace

Selection select_action(const SelectorInput& input) {
    const std::size_t sample_count =
        validate_long_returns(input);
    validate_settled_means(input);
    if (terminal_saturated(input, sample_count)) {
        return select_saturated_boundary(input);
    }
    return select_dominating_search(input);
}

Rb1Selection select_action_rb1(
    const Rb1SelectorInput& input) {
    static_cast<void>(validate_rb1_input(input));
    if (rb1_has_bound_fallback(input)) {
        return {
            .action_index = input.parent_index,
            .reason = Rb1SelectionReason::Parent,
        };
    }
    const std::vector<long double> means =
        rb1_action_means(input);
    if (const auto frontier =
            select_rb1_saturated_frontier(
                input, means);
        frontier.has_value()) {
        return *frontier;
    }
    return select_rb1_paired_lower_bound(input);
}

Rb1Selection select_action_rb2_tc0(
    const Rb2Tc0SelectorInput& input) {
    const std::size_t primary_sample_count =
        validate_rb1_input(input.primary);
    validate_rb2_tc0_input(
        input, primary_sample_count);

    if (rb2_tc0_has_bound_fallback(input)) {
        return {
            .action_index = input.primary.parent_index,
            .reason = Rb1SelectionReason::Parent,
        };
    }

    const Rb1Selection primary_selection =
        select_action_rb1(input.primary);
    if (primary_selection.reason !=
        Rb1SelectionReason::PairedLowerBound) {
        return primary_selection;
    }

    const std::string& candidate_key =
        input.primary
            .action_keys[primary_selection.action_index];
    const std::string& parent_key =
        input.primary
            .action_keys[input.primary.parent_index];
    const std::size_t candidate_index =
        rb2_tc0_next_turn_index(
            input, candidate_key);
    const std::size_t parent_index =
        rb2_tc0_next_turn_index(input, parent_key);
    const Rb1PairedEstimate confirmation =
        paired_lower_bound(
            input.next_turn_paired_returns[
                candidate_index],
            input.next_turn_paired_returns[
                parent_index]);
    if (!(confirmation.lower_bound > 0.0L)) {
        return {
            .action_index = input.primary.parent_index,
            .reason = Rb1SelectionReason::Parent,
        };
    }
    return primary_selection;
}

} // namespace old_school::conservative_policy_improvement
