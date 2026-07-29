#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace old_school::conservative_policy_improvement {

enum class SelectionReason {
    Parent,
    DominatingSearch,
    SaturatedBoundary,
};

struct PerCellTerminalFlags {
    std::vector<std::vector<bool>> values;
};

// This compact form is useful when the caller has already performed the
// per-cell terminal census. A true claim is still checked against the exact
// long-return values before it can affect selection.
struct AllTerminalSaturated {
    bool value = false;
};

using TerminalEvidence = std::variant<
    PerCellTerminalFlags,
    AllTerminalSaturated>;

struct SelectorInput {
    std::size_t parent_index = 0;
    // Rows are actions and columns are common paired samples. Every row must
    // have the same nonzero width.
    std::vector<std::vector<double>> paired_long_returns;
    TerminalEvidence terminal_evidence =
        AllTerminalSaturated{};
    // One mean per action. These values are consulted only when the long
    // returns are proven to be fully terminal-saturated.
    std::optional<std::vector<double>>
        settled_boundary_means;
};

struct Selection {
    std::size_t action_index = 0;
    SelectionReason reason = SelectionReason::Parent;

    bool operator==(const Selection&) const = default;
};

// Invalid, non-finite, or inconsistent evidence throws
// std::invalid_argument before an action can be selected.
Selection select_action(const SelectorInput& input);

enum class Rb1SelectionReason {
    Parent,
    PairedLowerBound,
    SaturatedFrontier,
};

struct Rb1SelectorInput {
    std::size_t parent_index = 0;
    // Stable keys are authoritative action identities. They must be unique
    // and nonempty; selection never uses caller row order to resolve a tie.
    std::vector<std::string> action_keys;
    std::uint64_t tie_seed = 0;
    // Rows are actions and columns are common paired samples. RB1 requires at
    // least two columns for its Bessel-corrected standard error.
    std::vector<std::vector<double>> paired_long_returns;
    std::vector<std::vector<bool>> terminal_flags;
    std::vector<double> settled_boundary_means;
    // Optional exact-combat evidence in the same paired shape. Any exhausted
    // bound invalidates the treatment evidence for the whole root, so RB1
    // retains its parent instead of selecting from partial/legacy cells.
    std::vector<std::vector<bool>>
        bound_fallback_flags;
};

struct Rb1Selection {
    std::size_t action_index = 0;
    Rb1SelectionReason reason =
        Rb1SelectionReason::Parent;

    bool operator==(const Rb1Selection&) const = default;
};

// AQ6-RB1 is a separate policy from select_action: an exact saturated top
// frontier is resolved first, then nonparent actions must clear a paired
// mean-minus-one-standard-error lower bound. Invalid evidence throws
// std::invalid_argument before an action can be selected.
Rb1Selection select_action_rb1(
    const Rb1SelectorInput& input);

struct Rb2Tc0SelectorInput {
    Rb1SelectorInput primary;
    // Next-turn rows may use a different caller order from Primary. Stable
    // keys align the same complete action set before the selected
    // candidate-versus-original-parent paired comparison.
    std::vector<std::string> next_turn_action_keys;
    std::vector<std::vector<double>>
        next_turn_paired_returns;
    // Optional exact-combat evidence in Next-turn's paired shape. As in
    // RB1, an empty matrix means that no fallback was reported.
    std::vector<std::vector<bool>>
        next_turn_bound_fallback_flags;
};

// AQ6-RB2-TC0 runs RB1 on Primary. Parent and SaturatedFrontier results pass
// through unchanged. A PairedLowerBound deviation survives only when its
// keyed Next-turn candidate-versus-Primary-parent paired lower bound
// (mean minus one Bessel-corrected standard error) is also strictly
// positive. Any bound fallback in either view retains the Primary parent.
// Invalid, non-finite, out-of-range, unpaired, or key-misaligned evidence
// throws std::invalid_argument before an action can be selected.
Rb1Selection select_action_rb2_tc0(
    const Rb2Tc0SelectorInput& input);

} // namespace old_school::conservative_policy_improvement
