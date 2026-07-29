#include "old_school/conservative_policy_improvement.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace selector =
    old_school::conservative_policy_improvement;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

selector::SelectorInput input_with_indicator(
    std::size_t parent,
    std::vector<std::vector<double>> returns,
    bool saturated = false) {
    return {
        .parent_index = parent,
        .paired_long_returns = std::move(returns),
        .terminal_evidence =
            selector::AllTerminalSaturated{
                .value = saturated,
            },
    };
}

selector::PerCellTerminalFlags flags_like(
    const std::vector<std::vector<double>>& returns,
    bool value) {
    selector::PerCellTerminalFlags flags;
    flags.values.reserve(returns.size());
    for (const auto& row : returns) {
        flags.values.emplace_back(row.size(), value);
    }
    return flags;
}

selector::Rb1SelectorInput rb1_input(
    std::size_t parent,
    std::vector<std::string> keys,
    std::vector<std::vector<double>> returns,
    std::uint64_t tie_seed = 0) {
    selector::Rb1SelectorInput input{
        .parent_index = parent,
        .action_keys = std::move(keys),
        .tie_seed = tie_seed,
        .paired_long_returns = std::move(returns),
    };
    input.terminal_flags.reserve(
        input.paired_long_returns.size());
    for (const auto& row :
         input.paired_long_returns) {
        input.terminal_flags.emplace_back(
            row.size(), false);
    }
    input.settled_boundary_means.assign(
        input.paired_long_returns.size(), 0.5);
    return input;
}

std::string rb1_selected_key(
    const selector::Rb1SelectorInput& input) {
    const selector::Rb1Selection selected =
        selector::select_action_rb1(input);
    return input.action_keys[selected.action_index];
}

selector::Rb2Tc0SelectorInput rb2_tc0_input(
    selector::Rb1SelectorInput primary,
    std::vector<std::string> next_turn_keys,
    std::vector<std::vector<double>>
        next_turn_returns) {
    return {
        .primary = std::move(primary),
        .next_turn_action_keys =
            std::move(next_turn_keys),
        .next_turn_paired_returns =
            std::move(next_turn_returns),
    };
}

std::string rb2_tc0_selected_key(
    const selector::Rb2Tc0SelectorInput& input) {
    const selector::Rb1Selection selected =
        selector::select_action_rb2_tc0(input);
    return input.primary.action_keys[
        selected.action_index];
}

void test_parent_fallback_and_componentwise_gate() {
    auto input = input_with_indicator(
        0,
        {
            {0.4, 0.6, 0.5},
            {0.7, 0.5, 0.7},
            {0.4, 0.6, 0.5},
        });
    const selector::Selection selected =
        selector::select_action(input);
    expect(
        selected ==
            selector::Selection{
                .action_index = 0,
                .reason =
                    selector::SelectionReason::Parent,
            },
        "a worse paired cell or exact equality escaped "
        "the parent fallback");
}

void test_strict_dominance_and_strongest_mean() {
    auto input = input_with_indicator(
        1,
        {
            {0.3, 0.8, 0.7},
            {0.3, 0.4, 0.5},
            {0.5, 0.6, 0.6},
            {0.3, 0.5, 0.5},
        });
    const selector::Selection selected =
        selector::select_action(input);
    expect(
        selected.action_index == 0 &&
            selected.reason ==
                selector::SelectionReason::
                    DominatingSearch,
        "selector did not choose the strongest "
        "componentwise-dominating mean");
}

void test_one_negative_cell_disqualifies_high_mean() {
    auto input = input_with_indicator(
        0,
        {
            {0.4, 0.4, 0.4},
            {0.39, 1.0, 1.0},
            {0.4, 0.4, 0.5},
        });
    const selector::Selection selected =
        selector::select_action(input);
    expect(
        selected.action_index == 2 &&
            selected.reason ==
                selector::SelectionReason::
                    DominatingSearch,
        "a high-mean action with a losing paired cell "
        "was treated as safe");
}

void test_exact_best_mean_tie_is_parent_safe() {
    auto input = input_with_indicator(
        0,
        {
            {0.1, 0.1},
            {0.2, 0.2},
            {0.2, 0.2},
        });
    const selector::Selection selected =
        selector::select_action(input);
    expect(
        selected.action_index == 0 &&
            selected.reason ==
                selector::SelectionReason::Parent,
        "exact search tie did not retain the parent");
}

void test_terminal_flags_select_strict_settled_max() {
    auto input = input_with_indicator(
        0,
        {
            {1.0, 1.0},
            {1.0, 1.0},
            {1.0, 1.0},
        });
    input.terminal_evidence =
        flags_like(input.paired_long_returns, true);
    input.settled_boundary_means =
        std::vector<double>{0.6, 0.8, 0.7};
    const selector::Selection selected =
        selector::select_action(input);
    expect(
        selected.action_index == 1 &&
            selected.reason ==
                selector::SelectionReason::
                    SaturatedBoundary,
        "strict settled maximum did not break terminal "
        "saturation");
}

void test_saturated_boundary_is_parent_safe() {
    auto input = input_with_indicator(
        2,
        {
            {0.5, 0.5},
            {0.5, 0.5},
            {0.5, 0.5},
        },
        true);

    expect(
        selector::select_action(input) ==
            selector::Selection{
                .action_index = 2,
                .reason =
                    selector::SelectionReason::Parent,
            },
        "missing settled evidence did not retain parent");

    input.settled_boundary_means =
        std::vector<double>{0.8, 0.8, 0.1};
    expect(
        selector::select_action(input) ==
            selector::Selection{
                .action_index = 2,
                .reason =
                    selector::SelectionReason::Parent,
            },
        "tied settled maxima did not retain parent");

    input.settled_boundary_means =
        std::vector<double>{0.1, 0.2, 0.9};
    expect(
        selector::select_action(input) ==
            selector::Selection{
                .action_index = 2,
                .reason =
                    selector::SelectionReason::Parent,
            },
        "parent settled maximum reported a treatment "
        "selection");
}

void test_saturation_requires_every_terminal_cell() {
    auto input = input_with_indicator(
        0,
        {
            {1.0, 1.0},
            {1.0, 1.0},
        });
    input.terminal_evidence =
        flags_like(input.paired_long_returns, true);
    std::get<selector::PerCellTerminalFlags>(
        input.terminal_evidence)
        .values[1][1] = false;
    input.settled_boundary_means =
        std::vector<double>{0.1, 0.9};
    expect(
        selector::select_action(input) ==
            selector::Selection{
                .action_index = 0,
                .reason =
                    selector::SelectionReason::Parent,
            },
        "partial terminal evidence activated the "
        "saturation branch");
}

void test_all_terminal_but_mixed_outcomes_use_search() {
    auto input = input_with_indicator(
        0,
        {
            {0.0, 0.5, 0.0},
            {0.5, 0.5, 1.0},
        });
    input.terminal_evidence =
        flags_like(input.paired_long_returns, true);
    input.settled_boundary_means =
        std::vector<double>{0.9, 0.1};
    const selector::Selection selected =
        selector::select_action(input);
    expect(
        selected.action_index == 1 &&
            selected.reason ==
                selector::SelectionReason::
                    DominatingSearch,
        "mixed terminal outcomes incorrectly used the "
        "settled-boundary tie-break");
}

void test_validation_fails_closed() {
    expect_rejected(
        [] {
            static_cast<void>(
                selector::select_action(
                    input_with_indicator(0, {})));
        },
        "empty action set was accepted");
    expect_rejected(
        [] {
            static_cast<void>(
                selector::select_action(
                    input_with_indicator(
                        2, {{0.1}, {0.2}})));
        },
        "invalid parent index was accepted");
    expect_rejected(
        [] {
            static_cast<void>(
                selector::select_action(
                    input_with_indicator(0, {{}})));
        },
        "empty paired sample row was accepted");
    expect_rejected(
        [] {
            static_cast<void>(
                selector::select_action(
                    input_with_indicator(
                        0, {{0.1, 0.2}, {0.3}})));
        },
        "ragged paired rows were accepted");
    expect_rejected(
        [] {
            static_cast<void>(
                selector::select_action(
                    input_with_indicator(
                        0,
                        {{
                            std::numeric_limits<double>::
                                quiet_NaN(),
                        }})));
        },
        "non-finite long return was accepted");

    auto bad_flag_shape =
        input_with_indicator(
            0, {{0.0, 0.0}, {0.0, 0.0}});
    bad_flag_shape.terminal_evidence =
        selector::PerCellTerminalFlags{
            .values = {{true, true}},
        };
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action(
                    bad_flag_shape));
        },
        "wrong terminal action count was accepted");

    auto ragged_flags =
        input_with_indicator(
            0, {{0.0, 0.0}, {0.0, 0.0}});
    ragged_flags.terminal_evidence =
        selector::PerCellTerminalFlags{
            .values = {
                {true, true},
                {true},
            },
        };
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action(ragged_flags));
        },
        "ragged terminal flags were accepted");

    auto false_terminal =
        input_with_indicator(0, {{0.25}});
    false_terminal.terminal_evidence =
        selector::PerCellTerminalFlags{
            .values = {{true}},
        };
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action(false_terminal));
        },
        "terminal flag accepted a nonterminal value");

    auto false_saturation =
        input_with_indicator(
            0, {{1.0}, {0.5}}, true);
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action(false_saturation));
        },
        "false terminal-saturation claim was accepted");

    auto wrong_settled_shape =
        input_with_indicator(0, {{1.0}, {1.0}}, true);
    wrong_settled_shape.settled_boundary_means =
        std::vector<double>{0.2};
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action(
                    wrong_settled_shape));
        },
        "wrong settled-mean shape was accepted");

    auto nonfinite_settled =
        input_with_indicator(0, {{1.0}}, true);
    nonfinite_settled.settled_boundary_means =
        std::vector<double>{
            std::numeric_limits<double>::infinity(),
        };
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action(
                    nonfinite_settled));
        },
        "non-finite settled mean was accepted");
}

void test_rb1_requires_positive_one_se_lower_bound() {
    auto input = rb1_input(
        0,
        {"parent", "noisy", "safe"},
        {
            {0.5, 0.5},
            {0.4, 0.8},
            {0.6, 0.6},
        });
    const selector::Rb1Selection selected =
        selector::select_action_rb1(input);
    expect(
        selected ==
            selector::Rb1Selection{
                .action_index = 2,
                .reason =
                    selector::Rb1SelectionReason::
                        PairedLowerBound,
            },
        "RB1 accepted a positive mean whose one-SE "
        "lower bound was not positive");

    input.paired_long_returns[2] = {0.5, 0.5};
    expect(
        selector::select_action_rb1(input) ==
            selector::Rb1Selection{
                .action_index = 0,
                .reason =
                    selector::Rb1SelectionReason::Parent,
            },
        "RB1 failed to retain parent when no LCB "
        "qualified");
}

void test_rb1_ranks_lcb_before_mean_then_mean() {
    auto lcb_first = rb1_input(
        0,
        {"parent", "high-mean", "high-lcb"},
        {
            {0.5, 0.5, 0.5, 0.5},
            {0.5, 0.5, 0.9, 0.9},
            {0.6, 0.6, 0.6, 0.6},
        });
    expect(
        rb1_selected_key(lcb_first) == "high-lcb",
        "RB1 ranked mean ahead of a larger LCB");

    auto mean_second = rb1_input(
        0,
        {"parent", "higher-mean", "lower-mean"},
        {
            {0.25, 0.25},
            {0.375, 0.625},
            {0.375, 0.5},
        });
    expect(
        rb1_selected_key(mean_second) ==
            "higher-mean",
        "RB1 did not use mean after an exact LCB tie");
}

void test_rb1_exact_tie_is_seeded_and_order_independent() {
    const auto first = rb1_input(
        0,
        {"parent", "alpha", "zeta"},
        {
            {0.2, 0.2},
            {0.4, 0.4},
            {0.4, 0.4},
        },
        202607290701ULL);
    const auto reordered = rb1_input(
        2,
        {"zeta", "alpha", "parent"},
        {
            {0.4, 0.4},
            {0.4, 0.4},
            {0.2, 0.2},
        },
        202607290701ULL);
    const std::string selected =
        rb1_selected_key(first);
    expect(
        (selected == "alpha" ||
         selected == "zeta") &&
            rb1_selected_key(reordered) == selected &&
            rb1_selected_key(first) == selected,
        "RB1 exact tie depended on input order or was "
        "not deterministic");

    bool saw_alpha = false;
    bool saw_zeta = false;
    for (std::uint64_t seed = 0;
         seed < 64; ++seed) {
        auto varied = first;
        varied.tie_seed = seed;
        const std::string key =
            rb1_selected_key(varied);
        saw_alpha = saw_alpha || key == "alpha";
        saw_zeta = saw_zeta || key == "zeta";
    }
    expect(
        saw_alpha && saw_zeta,
        "RB1 tie seed did not affect exact tied keys");
}

void test_rb1_saturated_top_frontier_precedes_lcb() {
    auto input = rb1_input(
        0,
        {"parent", "ring", "self-draw", "nontop"},
        {
            {0.75, 0.75},
            {1.0, 1.0},
            {1.0, 1.0},
            {0.9, 0.9},
        },
        202607290701ULL);
    input.terminal_flags[1] = {true, true};
    input.terminal_flags[2] = {true, true};
    input.settled_boundary_means =
        {1.0, 0.8, 0.9, 1.0};

    const selector::Rb1Selection selected =
        selector::select_action_rb1(input);
    expect(
        selected ==
            selector::Rb1Selection{
                .action_index = 2,
                .reason =
                    selector::Rb1SelectionReason::
                        SaturatedFrontier,
            },
        "RB1 did not select the settled maximum inside "
        "the exact top frontier");
}

void test_rb1_frontier_ignores_nontop_and_is_parent_safe() {
    auto parent_frontier = rb1_input(
        1,
        {"other", "parent", "nontop"},
        {
            {0.5, 0.5},
            {0.5, 0.5},
            {0.25, 0.25},
        },
        77);
    parent_frontier.terminal_flags[0] = {true, true};
    parent_frontier.terminal_flags[1] = {true, true};
    parent_frontier.settled_boundary_means =
        {0.8, 0.8, 1.0};
    expect(
        selector::select_action_rb1(parent_frontier) ==
            selector::Rb1Selection{
                .action_index = 1,
                .reason =
                    selector::Rb1SelectionReason::Parent,
            },
        "RB1 did not retain a tied parent in the "
        "saturated frontier");

    auto parent_outside = rb1_input(
        0,
        {"parent", "alpha", "zeta"},
        {
            {0.5, 0.5},
            {1.0, 1.0},
            {1.0, 1.0},
        },
        91);
    parent_outside.terminal_flags[1] = {true, true};
    parent_outside.terminal_flags[2] = {true, true};
    parent_outside.settled_boundary_means =
        {1.0, 0.7, 0.7};
    const std::string first =
        rb1_selected_key(parent_outside);

    auto reordered = rb1_input(
        2,
        {"zeta", "alpha", "parent"},
        {
            {1.0, 1.0},
            {1.0, 1.0},
            {0.5, 0.5},
        },
        91);
    reordered.terminal_flags[0] = {true, true};
    reordered.terminal_flags[1] = {true, true};
    reordered.settled_boundary_means =
        {0.7, 0.7, 1.0};
    expect(
        (first == "alpha" || first == "zeta") &&
            rb1_selected_key(reordered) == first &&
            selector::select_action_rb1(parent_outside)
                    .reason ==
                selector::Rb1SelectionReason::
                    SaturatedFrontier,
        "RB1 seeded frontier tie used input order or a "
        "nontop settled value");
}

void test_rb1_frontier_requires_exact_terminal_support() {
    auto one_top = rb1_input(
        0,
        {"parent", "only-top"},
        {
            {0.5, 0.5},
            {1.0, 1.0},
        });
    one_top.terminal_flags[1] = {true, true};
    one_top.settled_boundary_means = {1.0, 0.0};
    expect(
        selector::select_action_rb1(one_top).reason ==
            selector::Rb1SelectionReason::
                PairedLowerBound,
        "a one-action top was treated as a frontier");

    auto nonterminal_tie = rb1_input(
        0,
        {"parent", "mixed-a", "mixed-b"},
        {
            {0.0, 0.0},
            {0.5, 1.0},
            {0.5, 1.0},
        });
    nonterminal_tie.settled_boundary_means =
        {0.0, 0.1, 0.9};
    const selector::Rb1Selection result =
        selector::select_action_rb1(nonterminal_tie);
    expect(
        result.reason ==
            selector::Rb1SelectionReason::
                PairedLowerBound,
        "a nonterminal equal-mean support activated the "
        "saturated frontier");

    auto missing_flag = rb1_input(
        0,
        {"parent", "top-a", "top-b"},
        {
            {0.5, 0.5},
            {1.0, 1.0},
            {1.0, 1.0},
        });
    missing_flag.terminal_flags[1] = {true, true};
    missing_flag.terminal_flags[2] = {true, false};
    missing_flag.settled_boundary_means =
        {0.0, 0.1, 0.9};
    expect(
        selector::select_action_rb1(missing_flag).reason ==
            selector::Rb1SelectionReason::
                PairedLowerBound,
        "incomplete frontier terminal census was "
        "accepted");
}

void test_rb1_validation_fails_closed() {
    auto valid = rb1_input(
        0,
        {"parent", "candidate"},
        {
            {0.25, 0.25},
            {0.5, 0.5},
        });

    auto empty_key = valid;
    empty_key.action_keys[1].clear();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(empty_key));
        },
        "RB1 accepted an empty action key");

    auto duplicate_key = valid;
    duplicate_key.action_keys[1] = "parent";
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    duplicate_key));
        },
        "RB1 accepted duplicate action keys");

    auto one_sample = valid;
    one_sample.paired_long_returns = {
        {0.25}, {0.5}};
    one_sample.terminal_flags = {
        {false}, {false}};
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(one_sample));
        },
        "RB1 accepted fewer than two paired samples");

    auto ragged = valid;
    ragged.paired_long_returns[1].pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(ragged));
        },
        "RB1 accepted ragged returns");

    auto ragged_flags = valid;
    ragged_flags.terminal_flags[1].pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    ragged_flags));
        },
        "RB1 accepted ragged terminal flags");

    auto out_of_range = valid;
    out_of_range.paired_long_returns[1][0] = 1.01;
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    out_of_range));
        },
        "RB1 accepted a return outside [0,1]");

    auto nonfinite = valid;
    nonfinite.paired_long_returns[1][0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(nonfinite));
        },
        "RB1 accepted a non-finite return");

    auto false_terminal = valid;
    false_terminal.terminal_flags[0][0] = true;
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    false_terminal));
        },
        "RB1 accepted an invalid terminal flag");

    auto bad_settled = valid;
    bad_settled.settled_boundary_means[0] = -0.01;
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    bad_settled));
        },
        "RB1 accepted a settled mean outside [0,1]");

    auto wrong_key_shape = valid;
    wrong_key_shape.action_keys.pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    wrong_key_shape));
        },
        "RB1 accepted wrong key shape");

    auto wrong_parent = valid;
    wrong_parent.parent_index = 2;
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    wrong_parent));
        },
        "RB1 accepted an invalid parent index");
}

void test_rb1_bound_fallback_retains_parent() {
    auto input = rb1_input(
        0,
        {"parent", "candidate"},
        {
            {0.25, 0.25},
            {1.0, 1.0},
        });
    input.bound_fallback_flags = {
        {false, false},
        {false, true},
    };
    expect(
        selector::select_action_rb1(input) ==
            selector::Rb1Selection{
                .action_index = 0,
                .reason =
                    selector::Rb1SelectionReason::Parent,
            },
        "RB1 selected treatment evidence after an exact-"
        "combat bound fallback");

    auto wrong_shape = input;
    wrong_shape.bound_fallback_flags[1].pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb1(
                    wrong_shape));
        },
        "RB1 accepted ragged bound-fallback evidence");
}

void test_rb2_tc0_accepts_concordant_lcb() {
    const auto input = rb2_tc0_input(
        rb1_input(
            0,
            {"parent", "candidate"},
            {
                {0.2, 0.2, 0.2, 0.2},
                {0.4, 0.4, 0.4, 0.4},
            }),
        {"candidate", "parent"},
        {
            {0.35, 0.35, 0.35, 0.35},
            {0.2, 0.2, 0.2, 0.2},
        });
    expect(
        selector::select_action_rb2_tc0(input) ==
            selector::Rb1Selection{
                .action_index = 1,
                .reason =
                    selector::Rb1SelectionReason::
                        PairedLowerBound,
            },
        "RB2-TC0 vetoed a candidate with positive "
        "paired LCBs at both time scales");
}

void test_rb2_tc0_vetoes_next_turn_disagreement() {
    const auto input = rb2_tc0_input(
        rb1_input(
            0,
            {"parent", "candidate"},
            {
                {0.2, 0.2, 0.2, 0.2},
                {0.4, 0.4, 0.4, 0.4},
            }),
        {"parent", "candidate"},
        {
            {0.2, 0.2, 0.2, 0.2},
            {0.1, 0.5, 0.1, 0.5},
        });
    expect(
        selector::select_action_rb2_tc0(input) ==
            selector::Rb1Selection{
                .action_index = 0,
                .reason =
                    selector::Rb1SelectionReason::Parent,
            },
        "RB2-TC0 accepted a Primary deviation whose "
        "Next-turn one-SE lower bound was not positive");
}

void test_rb2_tc0_parent_passthrough() {
    const auto input = rb2_tc0_input(
        rb1_input(
            0,
            {"parent", "candidate"},
            {
                {0.5, 0.5},
                {0.4, 0.4},
            }),
        {"candidate", "parent"},
        {
            {1.0, 1.0},
            {0.0, 0.0},
        });
    expect(
        selector::select_action_rb2_tc0(input) ==
            selector::Rb1Selection{
                .action_index = 0,
                .reason =
                    selector::Rb1SelectionReason::Parent,
            },
        "RB2-TC0 searched for a Next-turn alternative "
        "after Primary retained the parent");
}

void test_rb2_tc0_saturated_frontier_passthrough() {
    auto primary = rb1_input(
        0,
        {"parent", "self-draw", "other-top"},
        {
            {0.5, 0.5},
            {1.0, 1.0},
            {1.0, 1.0},
        });
    primary.terminal_flags[1] = {true, true};
    primary.terminal_flags[2] = {true, true};
    primary.settled_boundary_means =
        {0.5, 0.9, 0.8};
    const auto input = rb2_tc0_input(
        std::move(primary),
        {"other-top", "parent", "self-draw"},
        {
            {1.0, 1.0},
            {1.0, 1.0},
            {0.0, 0.0},
        });
    expect(
        selector::select_action_rb2_tc0(input) ==
            selector::Rb1Selection{
                .action_index = 1,
                .reason =
                    selector::Rb1SelectionReason::
                        SaturatedFrontier,
            },
        "RB2-TC0 applied Next-turn confirmation to a "
        "Primary saturated-frontier selection");
}

void test_rb2_tc0_bound_fallbacks_retain_parent() {
    auto primary_fallback = rb2_tc0_input(
        rb1_input(
            0,
            {"parent", "candidate"},
            {
                {0.2, 0.2},
                {0.4, 0.4},
            }),
        {"parent", "candidate"},
        {
            {0.2, 0.2},
            {0.4, 0.4},
        });
    primary_fallback.primary.bound_fallback_flags = {
        {false, false},
        {true, false},
    };
    expect(
        selector::select_action_rb2_tc0(
            primary_fallback) ==
            selector::Rb1Selection{
                .action_index = 0,
                .reason =
                    selector::Rb1SelectionReason::Parent,
            },
        "RB2-TC0 ignored a Primary bound fallback");

    auto next_fallback = rb2_tc0_input(
        rb1_input(
            0,
            {"parent", "candidate"},
            {
                {0.2, 0.2},
                {0.4, 0.4},
            }),
        {"candidate", "parent"},
        {
            {0.4, 0.4},
            {0.2, 0.2},
        });
    next_fallback.next_turn_bound_fallback_flags = {
        {false, true},
        {false, false},
    };
    expect(
        selector::select_action_rb2_tc0(
            next_fallback) ==
            selector::Rb1Selection{
                .action_index = 0,
                .reason =
                    selector::Rb1SelectionReason::Parent,
            },
        "RB2-TC0 ignored a Next-turn bound fallback");
}

void test_rb2_tc0_input_order_identity() {
    const auto first = rb2_tc0_input(
        rb1_input(
            0,
            {"parent", "alpha", "zeta"},
            {
                {0.2, 0.2},
                {0.4, 0.4},
                {0.4, 0.4},
            },
            202607290701ULL),
        {"zeta", "parent", "alpha"},
        {
            {0.4, 0.4},
            {0.2, 0.2},
            {0.4, 0.4},
        });
    const auto reordered = rb2_tc0_input(
        rb1_input(
            1,
            {"zeta", "parent", "alpha"},
            {
                {0.4, 0.4},
                {0.2, 0.2},
                {0.4, 0.4},
            },
            202607290701ULL),
        {"alpha", "zeta", "parent"},
        {
            {0.4, 0.4},
            {0.4, 0.4},
            {0.2, 0.2},
        });
    const std::string selected =
        rb2_tc0_selected_key(first);
    expect(
        (selected == "alpha" ||
         selected == "zeta") &&
            rb2_tc0_selected_key(reordered) ==
                selected,
        "RB2-TC0 selection identity depended on "
        "Primary or Next-turn input order");
}

void test_rb2_tc0_validation_fails_closed() {
    const auto valid = rb2_tc0_input(
        rb1_input(
            0,
            {"parent", "candidate"},
            {
                {0.2, 0.2},
                {0.4, 0.4},
            }),
        {"parent", "candidate"},
        {
            {0.2, 0.2},
            {0.4, 0.4},
        });

    auto missing_key = valid;
    missing_key.next_turn_action_keys[1] = "other";
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb2_tc0(
                    missing_key));
        },
        "RB2-TC0 accepted misaligned action keys");

    auto duplicate_key = valid;
    duplicate_key.next_turn_action_keys[1] = "parent";
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb2_tc0(
                    duplicate_key));
        },
        "RB2-TC0 accepted duplicate Next-turn keys");

    auto ragged = valid;
    ragged.next_turn_paired_returns[1].pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb2_tc0(
                    ragged));
        },
        "RB2-TC0 accepted ragged or unpaired "
        "Next-turn returns");

    auto nonfinite = valid;
    nonfinite.next_turn_paired_returns[1][0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb2_tc0(
                    nonfinite));
        },
        "RB2-TC0 accepted a non-finite Next-turn "
        "return");

    auto out_of_range = valid;
    out_of_range.next_turn_paired_returns[1][0] =
        1.01;
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb2_tc0(
                    out_of_range));
        },
        "RB2-TC0 accepted a Next-turn return outside "
        "[0,1]");

    auto ragged_fallback = valid;
    ragged_fallback
        .next_turn_bound_fallback_flags = {
            {false, false},
            {false},
        };
    expect_rejected(
        [&] {
            static_cast<void>(
                selector::select_action_rb2_tc0(
                    ragged_fallback));
        },
        "RB2-TC0 accepted ragged Next-turn fallback "
        "evidence");
}

} // namespace

int main() {
    try {
        test_parent_fallback_and_componentwise_gate();
        test_strict_dominance_and_strongest_mean();
        test_one_negative_cell_disqualifies_high_mean();
        test_exact_best_mean_tie_is_parent_safe();
        test_terminal_flags_select_strict_settled_max();
        test_saturated_boundary_is_parent_safe();
        test_saturation_requires_every_terminal_cell();
        test_all_terminal_but_mixed_outcomes_use_search();
        test_validation_fails_closed();
        test_rb1_requires_positive_one_se_lower_bound();
        test_rb1_ranks_lcb_before_mean_then_mean();
        test_rb1_exact_tie_is_seeded_and_order_independent();
        test_rb1_saturated_top_frontier_precedes_lcb();
        test_rb1_frontier_ignores_nontop_and_is_parent_safe();
        test_rb1_frontier_requires_exact_terminal_support();
        test_rb1_validation_fails_closed();
        test_rb1_bound_fallback_retains_parent();
        test_rb2_tc0_accepts_concordant_lcb();
        test_rb2_tc0_vetoes_next_turn_disagreement();
        test_rb2_tc0_parent_passthrough();
        test_rb2_tc0_saturated_frontier_passthrough();
        test_rb2_tc0_bound_fallbacks_retain_parent();
        test_rb2_tc0_input_order_identity();
        test_rb2_tc0_validation_fails_closed();
        std::cout
            << "All conservative policy-improvement "
               "selector tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
