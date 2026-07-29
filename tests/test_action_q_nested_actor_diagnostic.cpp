#include "old_school/action_q_nested_actor_diagnostic.hpp"

#include "old_school/probes.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

namespace aq4 =
    old_school::action_q_nested_actor_diagnostic;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool samples_bit_identical(
    const old_school::LearnedActionSamples& first,
    const old_school::LearnedActionSamples& second) {
    if (first.q_samples.size() != second.q_samples.size() ||
        first.priority_shallow_prior_samples.size() !=
            second.priority_shallow_prior_samples.size() ||
        first.priority_continuation_samples.size() !=
            second.priority_continuation_samples.size() ||
        first.exact_priority_aggregate_scores.size() !=
            second.exact_priority_aggregate_scores.size() ||
        first.sampled_worlds != second.sampled_worlds ||
        first.rollout_evaluations !=
            second.rollout_evaluations ||
        first.terminal_evaluations !=
            second.terminal_evaluations ||
        first.bootstrapped_evaluations !=
            second.bootstrapped_evaluations ||
        first.priority_inner_rollout_evaluations !=
            second.priority_inner_rollout_evaluations ||
        first.priority_inner_search_invocations !=
            second.priority_inner_search_invocations ||
        first.priority_inner_search_max_depth !=
            second.priority_inner_search_max_depth ||
        first.inner_rollout_evaluations !=
            second.inner_rollout_evaluations ||
        first.inner_search_invocations !=
            second.inner_search_invocations ||
        first.inner_search_max_depth !=
            second.inner_search_max_depth ||
        first.priority_h0_boundaries !=
            second.priority_h0_boundaries) {
        return false;
    }
    const auto vector_bits =
        [](const std::vector<double>& left,
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
        };
    for (std::size_t row = 0;
         row < first.q_samples.size(); ++row) {
        if (!vector_bits(
                first.q_samples[row],
                second.q_samples[row]) ||
            !vector_bits(
                first.priority_shallow_prior_samples[row],
                second.priority_shallow_prior_samples[row]) ||
            !vector_bits(
                first.priority_continuation_samples[row],
                second.priority_continuation_samples[row])) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index <
         first.exact_priority_aggregate_scores.size();
         ++index) {
        if (!same_bits(
                first.exact_priority_aggregate_scores[index],
                second.exact_priority_aggregate_scores[
                    index])) {
            return false;
        }
    }
    return true;
}

old_school::probes::DecisionProbe braingeyser_probe() {
    const auto probes =
        old_school::probes::
            make_braingeyser_x_zero_control_v1();
    expect(
        probes.size() == 1,
        "AQ4-D1 Braingeyser fixture count drifted");
    return probes.front();
}

std::vector<old_school::PriorityAction> probe_actions(
    const old_school::probes::DecisionProbe& probe) {
    std::vector<old_school::PriorityAction> actions;
    for (const auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action);
        expect(
            action != nullptr,
            "AQ4-D1 fixture contains a non-Priority action");
        actions.push_back(*action);
    }
    return actions;
}

void test_recipe_manifest_cli_and_witnesses_are_sealed() {
    const std::vector<std::string_view> diagnose{
        "--diagnose",
    };
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> extra{
        "--diagnose",
        "--again",
    };
    expect(
        aq4::parse_command(diagnose) ==
                aq4::Command::Diagnose &&
            !aq4::parse_command(empty).has_value() &&
            !aq4::parse_command(extra).has_value(),
        "AQ4-D1 CLI accepted an undeclared shape");
    std::ostringstream usage;
    aq4::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-nested-actor-diagnostic "
            "--diagnose\n",
        "AQ4-D1 usage drifted");

    constexpr std::array<std::uint64_t, 4> kSeeds = {
        3875276633833541024ULL,
        10554634509341308714ULL,
        15818607149009889277ULL,
        14402092525871157609ULL,
    };
    const auto manifest = aq4::fixture_manifest();
    bool exact = manifest.size() == kSeeds.size();
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        exact =
            exact &&
            manifest[index].fixture_index == index &&
            manifest[index].expected_seed ==
                kSeeds[index] &&
            aq4::fixture_seed(index) == kSeeds[index];
    }
    const auto outer = aq4::outer_search_config(123);
    const auto inner = aq4::actor_search_config(456);
    expect(
        exact &&
            aq4::kRootSeed == 202607282011ULL &&
            aq4::actor_local_seed() ==
                aq4::kActorLocalSeed &&
            manifest[0].stable_id ==
                "control.blue.counter-redundant-same-target.v1" &&
            manifest[0].positive_key == "pass" &&
            manifest[0].negative_key ==
                "counter-same-air-elemental" &&
            manifest[0].secondary_negative_key ==
                "counter-own-counterspell" &&
            outer.seed == 123 &&
            outer.worlds == 32 &&
            outer.rollouts_per_world == 1 &&
            outer.horizon_turns == 8 &&
            outer.value_continuation_search_worlds == 2 &&
            outer.evaluation_threads == 4 &&
            !outer.blend_shallow_prior &&
            inner.seed == 456 &&
            inner.worlds == 2 &&
            inner.rollouts_per_world == 1 &&
            inner.horizon_turns == 4 &&
            inner.value_continuation_search_worlds == 0 &&
            inner.blend_shallow_prior &&
            inner.evaluation_threads == 1,
        "AQ4-D1 recipe or manifest drifted");
    expect_rejected(
        [] {
            static_cast<void>(aq4::fixture_seed(4));
        },
        "AQ4-D1 accepted an undeclared fixture index");
    aq4::validate_fixture_witnesses();
}

void test_direction_gates_cover_both_redundant_counters() {
    const auto manifest = aq4::fixture_manifest();
    std::vector<aq4::ActionScore> actions{
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action = old_school::PriorityAction::pass(),
            .mean = 0.8,
        },
        {
            .probe_key =
                "counter-same-air-elemental",
            .typed_descriptor = "counterspell-stack-1",
            .action =
                old_school::PriorityAction::
                    cast_counterspell(1),
            .mean = 0.6,
        },
        {
            .probe_key =
                "counter-own-counterspell",
            .typed_descriptor = "counterspell-stack-2",
            .action =
                old_school::PriorityAction::
                    cast_counterspell(2),
            .mean = 0.7,
        },
    };
    const auto passed =
        aq4::evaluate_direction(manifest[0], actions);
    expect(
        passed.passed &&
            std::abs(
                passed.required_margin - 0.1) <
                1.0e-15 &&
            passed.exact_max_support ==
                std::vector<std::string>{"pass"},
        "AQ4-D1 redundant-counter conjunction drifted");
    actions[2].mean = 0.8;
    expect(
        !aq4::evaluate_direction(
             manifest[0], actions)
             .passed,
        "AQ4-D1 accepted a tie with one redundant counter");
}

void test_root_identity_includes_nested_accounting() {
    aq4::RootScore first;
    first.accounting = {
        .sampled_worlds = 2,
        .rollout_evaluations = 2,
        .terminal_evaluations = 0,
        .bootstrapped_evaluations = 2,
        .inner_rollout_evaluations = 8,
        .inner_search_invocations = 2,
        .inner_search_max_depth = 1,
    };
    first.selected_probe_key = "pass";
    first.actions = {{
        .probe_key = "pass",
        .typed_descriptor = "pass",
        .action = old_school::PriorityAction::pass(),
        .samples = {0.5, 0.5},
        .inner_rollout_evaluations = {4, 4},
        .inner_search_invocations = {1, 1},
        .inner_search_max_depth = {1, 1},
        .mean = 0.5,
        .exact_max = true,
    }};
    aq4::RootScore second = first;
    expect(
        aq4::root_scores_bit_identical(
            first, second),
        "AQ4-D1 rejected identical root evidence");
    ++second.actions[0].inner_search_invocations[0];
    expect(
        !aq4::root_scores_bit_identical(
            first, second),
        "AQ4-D1 omitted per-cell invocation accounting");
    second = first;
    second.accounting.inner_search_max_depth = 2;
    expect(
        !aq4::root_scores_bit_identical(
            first, second),
        "AQ4-D1 omitted maximum nesting depth");
}

void test_engine_zero_identity_parallelism_and_depth_bound() {
    const auto probe = braingeyser_probe();
    const auto actions = probe_actions(probe);
    const auto model =
        old_school::train_learned_value_champion(
            1, 0x4151344431544553ULL);
    old_school::LearnedSearchConfig legacy{
        .seed = 0x4151345A45524FULL,
        .worlds = 2,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .blend_shallow_prior = false,
        .evaluation_threads = 1,
    };
    auto explicit_zero = legacy;
    explicit_zero.value_continuation_search_worlds = 0;
    const auto score =
        [&](const old_school::LearnedSearchConfig& config) {
            return old_school::learned_priority_action_samples(
                probe.state, probe.original_decks,
                probe.root_player, true, probe.phase,
                probe.consecutive_passes, actions, model,
                config);
        };
    const auto historical = score(legacy);
    const auto zero = score(explicit_zero);
    expect(
        samples_bit_identical(historical, zero) &&
            zero.inner_rollout_evaluations == 0 &&
            zero.inner_search_invocations == 0 &&
            zero.inner_search_max_depth == 0 &&
            zero.priority_inner_rollout_evaluations.empty() &&
            zero.priority_inner_search_invocations.empty() &&
            zero.priority_inner_search_max_depth.empty(),
        "AQ4-D1 explicit zero changed the legacy sampler");

    auto nested_serial = legacy;
    nested_serial.seed = 0x4151344E455354ULL;
    nested_serial.value_continuation_search_worlds = 2;
    auto nested_parallel = nested_serial;
    nested_parallel.evaluation_threads = 4;
    const auto serial = score(nested_serial);
    const auto parallel = score(nested_parallel);
    expect(
        samples_bit_identical(serial, parallel) &&
            serial.inner_rollout_evaluations > 0 &&
            serial.inner_search_invocations > 0 &&
            serial.inner_search_max_depth == 1,
        "AQ4-D1 nested search changed under four workers or "
        "violated its depth bound");
    std::size_t rollout_sum = 0;
    std::size_t invocation_sum = 0;
    std::size_t maximum_depth = 0;
    for (std::size_t action = 0;
         action <
         serial.priority_inner_rollout_evaluations.size();
         ++action) {
        for (std::size_t sample = 0;
             sample <
             serial.priority_inner_rollout_evaluations[
                 action]
                 .size();
             ++sample) {
            rollout_sum +=
                serial.priority_inner_rollout_evaluations[
                    action][sample];
            invocation_sum +=
                serial.priority_inner_search_invocations[
                    action][sample];
            maximum_depth =
                std::max(
                    maximum_depth,
                    serial.priority_inner_search_max_depth[
                        action][sample]);
        }
    }
    expect(
        rollout_sum == serial.inner_rollout_evaluations &&
            invocation_sum ==
                serial.inner_search_invocations &&
            maximum_depth == serial.inner_search_max_depth,
        "AQ4-D1 nested accounting did not cross-sum");

    auto too_many = nested_serial;
    too_many.value_continuation_search_worlds = 4097;
    expect_rejected(
        [&] {
            static_cast<void>(score(too_many));
        },
        "AQ4-D1 accepted an unbounded inner world count");
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&passed](std::string_view name, auto test) {
            try {
                test();
                ++passed;
            } catch (const std::exception& error) {
                std::cerr
                    << "FAIL " << name << ": "
                    << error.what() << '\n';
                throw;
            }
        };
    run(
        "recipe_manifest_cli_and_witnesses_are_sealed",
        test_recipe_manifest_cli_and_witnesses_are_sealed);
    run(
        "direction_gates_cover_both_redundant_counters",
        test_direction_gates_cover_both_redundant_counters);
    run(
        "root_identity_includes_nested_accounting",
        test_root_identity_includes_nested_accounting);
    run(
        "engine_zero_identity_parallelism_and_depth_bound",
        test_engine_zero_identity_parallelism_and_depth_bound);
    std::cout
        << "action-Q nested-actor diagnostic tests: "
        << passed << "/4 passed\n";
    return 0;
}
