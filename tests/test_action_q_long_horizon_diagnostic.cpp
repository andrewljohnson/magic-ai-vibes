#include "old_school/action_q_long_horizon_diagnostic.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq3 =
    old_school::action_q_long_horizon_diagnostic;

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

std::vector<double> constant_samples(double value) {
    return std::vector<double>(aq3::kWorlds, value);
}

void test_cli_recipe_manifest_and_seeds_are_sealed() {
    const std::vector<std::string_view> diagnose{
        "--diagnose",
    };
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> extra{
        "--diagnose",
        "--run",
    };
    expect(
        aq3::parse_command(diagnose) ==
                aq3::Command::Diagnose &&
            !aq3::parse_command(empty).has_value() &&
            !aq3::parse_command(extra).has_value(),
        "AQ3-D0 CLI accepted an undeclared shape");
    std::ostringstream usage;
    aq3::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-long-horizon-diagnostic "
            "--diagnose\n",
        "AQ3-D0 usage text drifted");

    const auto config =
        aq3::search_config(12345);
    expect(
        aq3::kRootSeed == 202607281951ULL &&
            aq3::kWorlds == 64 &&
            aq3::kRolloutsPerWorld == 1 &&
            aq3::kHorizonTurns == 32 &&
            aq3::kEvaluationThreads == 4 &&
            config.seed == 12345 &&
            config.worlds == 64 &&
            config.rollouts_per_world == 1 &&
            config.horizon_turns == 32 &&
            config.continuation_variant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            config.value_continuation_epsilon == 0.0 &&
            !config.blend_shallow_prior &&
            config.value_resolved_shallow_prior_weight ==
                0.0 &&
            config.value_priority_residual_weight == 0.0 &&
            !config.value_pass_dominance &&
            config.value_continuation_controller ==
                old_school::LearnedContinuationController::
                    Legacy &&
            config.evaluation_threads == 4 &&
            !config.capture_priority_h0_boundaries,
        "AQ3-D0 long-horizon recipe drifted");

    constexpr std::array<std::uint64_t, 4> kSeeds = {
        14244684161368182184ULL,
        10350313418552302294ULL,
        2017596409782985296ULL,
        2496096984247125995ULL,
    };
    const auto manifest = aq3::fixture_manifest();
    bool exact = manifest.size() == kSeeds.size();
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        exact =
            exact &&
            manifest[index].fixture_index == index &&
            manifest[index].expected_seed ==
                kSeeds[index] &&
            aq3::fixture_seed(index) == kSeeds[index];
    }
    expect(
        exact &&
            manifest[0].positive_key ==
                "counter-opponent-counterspell" &&
            manifest[0].negative_key == "pass" &&
            manifest[1].kind ==
                aq3::DirectionKind::ExcludeXZero &&
            manifest[1].excluded_keys[0] ==
                "braingeyser-x0-self" &&
            manifest[1].excluded_keys[1] ==
                "braingeyser-x0-opponent" &&
            manifest[2].positive_key == "pass" &&
            manifest[2].negative_key ==
                "growth-own-summoning-sick-grizzly-bears" &&
            manifest[3].positive_key ==
                "force-spike-gray-ogre" &&
            manifest[3].negative_key == "pass",
        "AQ3-D0 fixture manifest drifted");
    expect_rejected(
        [] {
            static_cast<void>(aq3::fixture_seed(4));
        },
        "AQ3-D0 accepted an undeclared fixture index");
    aq3::validate_fixture_witnesses();
}

void test_direction_gates_use_exact_support() {
    const auto manifest = aq3::fixture_manifest();
    std::vector<aq3::ActionScore> pair{
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .mean = 0.4,
        },
        {
            .probe_key =
                "counter-opponent-counterspell",
            .typed_descriptor = "counterspell-stack-1",
            .action =
                old_school::PriorityAction::
                    cast_counterspell(1),
            .mean = 0.6,
        },
    };
    const auto pair_direction =
        aq3::evaluate_direction(manifest[0], pair);
    expect(
        pair_direction.passed &&
            std::abs(
                pair_direction.required_margin - 0.2) <
                1.0e-15 &&
            pair_direction.exact_max_support ==
                std::vector<std::string>{
                    "counter-opponent-counterspell",
                },
        "AQ3-D0 strict-pair gate drifted");
    pair[0].mean = 0.6;
    expect(
        !aq3::evaluate_direction(
             manifest[0], pair)
             .passed,
        "AQ3-D0 accepted a tied strict pair");

    std::vector<aq3::ActionScore> x_zero{
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .mean = 0.8,
        },
        {
            .probe_key = "braingeyser-x0-self",
            .typed_descriptor =
                "braingeyser-x0-player-0",
            .action =
                old_school::PriorityAction::
                    cast_braingeyser(
                        0,
                        old_school::Target::
                            player_target(0)),
            .mean = 0.4,
        },
        {
            .probe_key =
                "braingeyser-x0-opponent",
            .typed_descriptor =
                "braingeyser-x0-player-1",
            .action =
                old_school::PriorityAction::
                    cast_braingeyser(
                        0,
                        old_school::Target::
                            player_target(1)),
            .mean = 0.7,
        },
    };
    const auto excluded =
        aq3::evaluate_direction(
            manifest[1], x_zero);
    expect(
        excluded.passed &&
            excluded.required_margin > 0.0 &&
            excluded.exact_max_support ==
                std::vector<std::string>{"pass"},
        "AQ3-D0 X=0 support gate drifted");
    x_zero[2].mean = 0.8;
    expect(
        !aq3::evaluate_direction(
             manifest[1], x_zero)
             .passed,
        "AQ3-D0 accepted an excluded exact-max tie");
}

void test_root_bit_identity_includes_samples_and_accounting() {
    aq3::RootScore first;
    first.accounting = {
        .sampled_worlds = aq3::kWorlds,
        .rollout_evaluations = aq3::kWorlds,
        .terminal_evaluations = 3,
        .bootstrapped_evaluations =
            aq3::kWorlds - 3,
    };
    first.actions = {{
        .probe_key = "pass",
        .typed_descriptor = "pass",
        .action = old_school::PriorityAction::pass(),
        .samples = constant_samples(0.5),
        .mean = 0.5,
    }};
    aq3::RootScore second = first;
    expect(
        aq3::root_scores_bit_identical(
            first, second),
        "AQ3-D0 rejected bit-identical root evidence");
    second.actions[0].samples[0] = -0.0;
    first.actions[0].samples[0] = 0.0;
    expect(
        !aq3::root_scores_bit_identical(
            first, second),
        "AQ3-D0 numeric equality masqueraded as bit identity");
    second = first;
    ++second.accounting.terminal_evaluations;
    --second.accounting.bootstrapped_evaluations;
    expect(
        !aq3::root_scores_bit_identical(
            first, second),
        "AQ3-D0 omitted accounting from bit identity");

    second = first;
    aq3::require_invariant_root_scores(
        first, second, second);
    second.actions[0].mean = 0.25;
    expect_rejected(
        [&] {
            aq3::require_invariant_root_scores(
                first, second, first);
        },
        "AQ3-D0 treated hidden drift as directional evidence");
    expect_rejected(
        [&] {
            aq3::require_invariant_root_scores(
                first, first, second);
        },
        "AQ3-D0 treated action-order drift as directional evidence");
}

void test_gate_and_output_require_complete_evidence() {
    const auto spec = aq3::fixture_manifest()[0];
    aq3::FixtureReport fixture;
    fixture.spec = spec;
    fixture.seed = spec.expected_seed;
    fixture.actions = {
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .samples = constant_samples(0.25),
            .mean = 0.25,
        },
        {
            .probe_key =
                "counter-opponent-counterspell",
            .typed_descriptor = "counterspell-stack-1",
            .action =
                old_school::PriorityAction::
                    cast_counterspell(1),
            .samples = constant_samples(0.75),
            .mean = 0.75,
            .exact_max = true,
        },
    };
    fixture.accounting = {
        .sampled_worlds = aq3::kWorlds,
        .rollout_evaluations =
            fixture.actions.size() * aq3::kWorlds,
        .terminal_evaluations = 17,
        .bootstrapped_evaluations =
            fixture.actions.size() * aq3::kWorlds -
            17,
    };
    fixture.direction =
        aq3::evaluate_direction(
            fixture.spec, fixture.actions);
    fixture.hidden_repartition_nonvacuous = true;
    fixture.hidden_repartition_bit_identical = true;
    fixture.reversed_action_bit_identical = true;
    expect(
        fixture.gate_passed(),
        "AQ3-D0 rejected complete fixture evidence");

    aq3::Report report;
    report.parent_fingerprint =
        std::string(aq3::kRequiredParentFingerprint);
    report.fixtures = {fixture};
    report.direction_passed[0] = true;
    report.hypothesis_passed = true;
    std::ostringstream output;
    aq3::print_report(output, report);
    const std::string text = output.str();
    expect(
        text.find(
            "schema=old-school-action-q-aq3-d0-long-horizon-v1") !=
                std::string::npos &&
            text.find("worlds=64") !=
                std::string::npos &&
            text.find("horizon_turns=32") !=
                std::string::npos &&
            text.find("evaluation_threads=4") !=
                std::string::npos &&
            text.find(
                "typed_descriptor=counterspell-stack-1") !=
                std::string::npos &&
            text.find("terminal_evaluations=17") !=
                std::string::npos &&
            text.find("bootstrapped_evaluations=111") !=
                std::string::npos &&
            text.find(
                "accounting fixtures=1 actions=2 "
                "sampled_worlds=64 "
                "rollout_evaluations=128 "
                "terminal_evaluations=17 "
                "bootstrapped_evaluations=111") !=
                std::string::npos &&
            text.find("required_margin=0.5") !=
                std::string::npos &&
            text.find("fit_performed=0") !=
                std::string::npos &&
            text.find("corpus_collected=0") !=
                std::string::npos,
        "AQ3-D0 output omitted sealed evidence");

    fixture.reversed_action_bit_identical = false;
    expect(
        !fixture.gate_passed(),
        "AQ3-D0 accepted action-order drift");
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
        "cli_recipe_manifest_and_seeds_are_sealed",
        test_cli_recipe_manifest_and_seeds_are_sealed);
    run(
        "direction_gates_use_exact_support",
        test_direction_gates_use_exact_support);
    run(
        "root_bit_identity_includes_samples_and_accounting",
        test_root_bit_identity_includes_samples_and_accounting);
    run(
        "gate_and_output_require_complete_evidence",
        test_gate_and_output_require_complete_evidence);
    std::cout
        << "action-Q long-horizon diagnostic tests: "
        << passed << "/4 passed\n";
    return 0;
}
