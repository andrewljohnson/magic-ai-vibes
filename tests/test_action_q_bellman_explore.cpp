#include "old_school/action_q_bellman_explore.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq = old_school::action_q_bellman_explore;

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

void test_cli_accepts_only_declared_modes() {
    const std::vector<std::string_view> census{
        "--census",
    };
    const std::vector<std::string_view> run{"--run"};
    const std::vector<std::string_view> diagnose{
        "--diagnose-teacher",
    };
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> extra{
        "--census",
        "--run",
    };
    const std::vector<std::string_view> unknown{
        "--gameplay",
    };
    expect(
        aq::parse_command(census) ==
                aq::Command::Census &&
            aq::parse_command(run) == aq::Command::Run &&
            aq::parse_command(diagnose) ==
                aq::Command::DiagnoseTeacher &&
            !aq::parse_command(empty).has_value() &&
            !aq::parse_command(extra).has_value() &&
            !aq::parse_command(unknown).has_value(),
        "AQ1 CLI accepted an undeclared command shape");

    std::ostringstream usage;
    aq::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-bellman-explore "
            "(--census|--run|--diagnose-teacher)\n",
        "AQ1 CLI usage text drifted");
}

void test_recipe_is_frozen() {
    const auto optimizer = aq::optimizer_config();
    expect(
        aq::kCollectionRootSeed == 202607281821ULL &&
            aq::kFitBlock == 0 &&
            aq::kCheckBlock == 1 &&
            aq::kMaximumRootsPerActorGame == 4 &&
            aq::kBaseWorlds == 8 &&
            aq::kBaseHorizonTurns == 4 &&
            aq::kBaseRolloutsPerWorld == 1 &&
            aq::kBaseEvaluationThreads == 4 &&
            aq::fit_seed() == aq::kFitSeed &&
            optimizer.batch_size == 64 &&
            optimizer.epochs == 64 &&
            optimizer.learning_rate == 0.003 &&
            optimizer.residual_weight == 0.10,
        "AQ1 frozen experiment recipe drifted");
}

void test_census_total_includes_every_declared_counter() {
    aq::BlockCensus block;
    block.block = aq::kFitBlock;
    block.games = 40;
    auto& first = block.decks[0];
    auto& second = block.decks[1];
    first.actor_games = 3;
    second.actor_games = 5;
    first.nontrivial_roots = 7;
    second.nontrivial_roots = 11;
    first.retained_roots = 2;
    second.retained_roots = 4;
    first.retained_options = 5;
    second.retained_options = 9;
    first.nonzero_spread_roots = 1;
    second.nonzero_spread_roots = 3;
    first.base_score_calls = 2;
    second.base_score_calls = 4;
    first.base_sampled_worlds = 16;
    second.base_sampled_worlds = 32;
    first.base_rollout_evaluations = 40;
    second.base_rollout_evaluations = 72;
    first.base_terminal_evaluations = 10;
    second.base_terminal_evaluations = 20;
    first.base_bootstrapped_evaluations = 30;
    second.base_bootstrapped_evaluations = 52;
    first.teacher_accounting.root_actions = 5;
    second.teacher_accounting.root_actions = 9;
    first.teacher_accounting.root_determinizations = 8;
    second.teacher_accounting.root_determinizations = 16;
    first.teacher_accounting.successor_actions = 13;
    second.teacher_accounting.successor_actions = 17;
    first.teacher_accounting.same_owner_group_occurrences = 4;
    second.teacher_accounting.same_owner_group_occurrences = 6;
    first.teacher_accounting.opponent_owner_group_occurrences =
        3;
    second.teacher_accounting.opponent_owner_group_occurrences =
        5;
    first.teacher_accounting.same_owner_root_particles = 12;
    second.teacher_accounting.same_owner_root_particles = 14;
    first.teacher_accounting.opponent_owner_root_particles = 8;
    second.teacher_accounting.opponent_owner_root_particles = 10;
    first.teacher_accounting.root_macros.transitions = 20;
    second.teacher_accounting.root_macros.transitions = 36;
    first.teacher_accounting.successor_macros.critic_leaves =
        19;
    second.teacher_accounting.successor_macros.critic_leaves =
        23;

    const aq::DeckCensus total = block.total();
    expect(
        block.retained_roots() == 6 &&
            block.retained_options() == 14 &&
            total.actor_games == 8 &&
            total.nontrivial_roots == 18 &&
            total.nonzero_spread_roots == 4 &&
            total.base_score_calls == 6 &&
            total.base_sampled_worlds == 48 &&
            total.base_rollout_evaluations == 112 &&
            total.base_terminal_evaluations == 30 &&
            total.base_bootstrapped_evaluations == 82 &&
            total.teacher_accounting.root_actions == 14 &&
            total.teacher_accounting.root_determinizations ==
                24 &&
            total.teacher_accounting.successor_actions == 30 &&
            total.teacher_accounting
                    .same_owner_group_occurrences == 10 &&
            total.teacher_accounting
                    .opponent_owner_group_occurrences == 8 &&
            total.teacher_accounting
                    .same_owner_root_particles == 26 &&
            total.teacher_accounting
                    .opponent_owner_root_particles == 18 &&
            total.teacher_accounting.root_macros.transitions ==
                56 &&
            total.teacher_accounting.successor_macros
                    .critic_leaves == 42,
        "AQ1 census total dropped a declared accounting field");
}

void test_owner_partition_contract_is_conjunctive() {
    old_school::action_q_bellman_teacher::TeacherAccounting
        accounting;
    accounting.root_actions = 2;
    accounting.root_terminal_particles = 2;
    accounting.root_boundary_particles = 6;
    accounting.successor_group_occurrences = 3;
    accounting.same_owner_group_occurrences = 2;
    accounting.opponent_owner_group_occurrences = 1;
    accounting.same_owner_root_particles = 3;
    accounting.opponent_owner_root_particles = 3;
    expect(
        aq::owner_partition_complete(accounting, 2),
        "AQ1 rejected a complete owner partition");

    ++accounting.opponent_owner_group_occurrences;
    expect(
        !aq::owner_partition_complete(accounting, 2),
        "AQ1 accepted an overcounted owner-group partition");
    --accounting.opponent_owner_group_occurrences;
    ++accounting.same_owner_root_particles;
    expect(
        !aq::owner_partition_complete(accounting, 2),
        "AQ1 accepted an overcounted owner-particle partition");
    --accounting.same_owner_root_particles;
    --accounting.root_terminal_particles;
    expect(
        !aq::owner_partition_complete(accounting, 2),
        "AQ1 accepted an incomplete terminal/owner partition");
}

void test_model_gate_output_is_quantitative() {
    old_school::action_q_offline_gate::ModelGateReport
        report;
    report.frozen_dev.parent.probe_count = 20;
    report.frozen_dev.parent.mean_regret = 0.25;
    report.frozen_dev.candidate.probe_count = 20;
    report.frozen_dev.candidate.mean_regret = 0.125;
    report.frozen_dev.stable_parent_agreements = 12;
    report.frozen_dev.lost_stable_parent_agreements = 1;
    report.frozen_dev.cache_before.byte_size = 123;
    report.frozen_dev.cache_before.sha256 = "before-sha";
    report.frozen_dev.cache_after.byte_size = 123;
    report.frozen_dev.cache_after.sha256 = "after-sha";
    report.ancestral.self_score = 0.8;
    report.ancestral.opponent_score = 0.2;
    report.ancestral.information_action_fingerprint =
        "ancestral-fingerprint";
    report.ancestral.legal_actions = {
        old_school::PriorityAction::pass(),
    };
    report.ancestral.selected_support =
        report.ancestral.legal_actions;
    report.descriptor_order.model_count = 2;
    report.descriptor_order.probe_count = 8;
    report.behavior.force_spike.policy_name = "candidate";
    report.behavior.force_spike.model_fingerprint =
        "candidate-fingerprint";
    report.behavior.force_spike.live.pass_score = 0.1;
    report.behavior.force_spike.live.force_spike_score = 0.9;
    report.behavior.force_spike.payable.pass_score = 0.7;
    report.behavior.force_spike.payable.force_spike_score =
        0.3;
    report.behavior.force_spike.live.selected_keys = {
        "force-spike-gray-ogre",
    };
    report.behavior.sick_bear_growth_selected_keys = {
        "pass",
    };

    std::ostringstream output;
    aq::print_model_gate_report(output, report);
    const std::string text = output.str();
    expect(
        text.find(
            "dev_metrics policy=parent probes=20") !=
                std::string::npos &&
            text.find("mean_regret=0.25") !=
                std::string::npos &&
            text.find(
                "dev_metrics policy=candidate probes=20") !=
                std::string::npos &&
            text.find("mean_regret=0.125") !=
                std::string::npos &&
            text.find(
                "stable_parent_agreements=12 "
                "lost_stable_parent_agreements=1") !=
                std::string::npos &&
            text.find("cache_before_sha256=before-sha") !=
                std::string::npos &&
            text.find(
                "ancestral self_score=0.80000000000000004 "
                "opponent_score=0.20000000000000001") !=
                std::string::npos &&
            text.find(
                "live_force_spike_score=0.90000000000000002") !=
                std::string::npos &&
            text.find(
                "behavior_sick_bear_growth_selection=pass") !=
                std::string::npos,
        "AQ1 model-gate output omitted quantitative evidence");
}

void test_teacher_diagnostic_manifest_and_seeds_are_frozen() {
    const auto manifest =
        aq::teacher_diagnostic_manifest();
    constexpr std::array<std::string_view, 4> kIds = {
        "control.blue.counter-same-target-after-intervening-"
        "counter.v1",
        "control.blue.braingeyser-x0.v1",
        "field.green.second-main-sick-bear-growth.v1",
        "control.blue.force-spike-live-gray-ogre.v1",
    };
    constexpr std::array<std::uint64_t, 4> kSeeds = {
        13755611371498319020ULL,
        2589590173959096294ULL,
        4410279927652125381ULL,
        118189991942941696ULL,
    };
    bool exact = manifest.size() == kIds.size();
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        exact =
            exact &&
            manifest[index].fixture_index == index &&
            manifest[index].stable_id == kIds[index] &&
            manifest[index].expected_seed ==
                kSeeds[index] &&
            aq::teacher_diagnostic_seed(index) ==
                kSeeds[index];
    }
    expect(
        exact &&
            aq::kTeacherDiagnosticRootSeed ==
                202607281913ULL &&
            manifest[0].kind ==
                aq::TeacherDiagnosticKind::StrictPair &&
            manifest[0].positive_key ==
                "counter-opponent-counterspell" &&
            manifest[0].negative_key == "pass" &&
            manifest[1].kind ==
                aq::TeacherDiagnosticKind::ExcludeXZero &&
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
        "AQ1-D0 fixture manifest or seed schedule drifted");
    expect_rejected(
        [] {
            static_cast<void>(
                aq::teacher_diagnostic_seed(4));
        },
        "AQ1-D0 accepted an undeclared fixture index");
}

void test_teacher_direction_gates_are_exact() {
    const auto manifest =
        aq::teacher_diagnostic_manifest();
    std::vector<aq::TeacherDiagnosticAction> pair{
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .value = 0.4,
        },
        {
            .probe_key =
                "counter-opponent-counterspell",
            .typed_descriptor = "counterspell-stack-1",
            .action =
                old_school::PriorityAction::
                    cast_counterspell(1),
            .value = 0.6,
        },
    };
    const aq::TeacherDirectionSummary pair_direction =
        aq::evaluate_teacher_direction(
            manifest[0], pair);
    expect(
        pair_direction.passed &&
            std::abs(
                pair_direction.required_margin - 0.2) <
                1.0e-15 &&
            pair_direction.exact_max_support ==
                std::vector<std::string>{
                    "counter-opponent-counterspell",
                },
        "AQ1-D0 strict-pair direction is not exact");

    pair[0].value = 0.6;
    expect(
        !aq::evaluate_teacher_direction(
             manifest[0], pair)
             .passed,
        "AQ1-D0 accepted a tied strict pair");

    std::vector<aq::TeacherDiagnosticAction> x_zero{
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .value = 0.8,
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
            .value = 0.4,
        },
        {
            .probe_key = "braingeyser-x0-opponent",
            .typed_descriptor =
                "braingeyser-x0-player-1",
            .action =
                old_school::PriorityAction::
                    cast_braingeyser(
                        0,
                        old_school::Target::
                            player_target(1)),
            .value = 0.7,
        },
    };
    const aq::TeacherDirectionSummary excluded =
        aq::evaluate_teacher_direction(
            manifest[1], x_zero);
    expect(
        excluded.passed &&
            excluded.excluded_margins[0] >
                excluded.excluded_margins[1] &&
            excluded.required_margin > 0.0 &&
            excluded.exact_max_support ==
                std::vector<std::string>{"pass"},
        "AQ1-D0 X=0 support exclusion is not exact");
    x_zero[2].value = 0.8;
    expect(
        !aq::evaluate_teacher_direction(
             manifest[1], x_zero)
             .passed,
        "AQ1-D0 accepted an X=0 exact-max tie");
}

void test_teacher_diagnostic_gate_and_output_contract() {
    const auto spec =
        aq::teacher_diagnostic_manifest()[0];
    aq::TeacherDiagnosticFixtureReport fixture;
    fixture.spec = spec;
    fixture.seed = spec.expected_seed;
    fixture.information_set_fingerprint =
        "information-fingerprint";
    fixture.actions = {
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .value = 0.4,
        },
        {
            .probe_key =
                "counter-opponent-counterspell",
            .typed_descriptor = "counterspell-stack-1",
            .action =
                old_school::PriorityAction::
                    cast_counterspell(1),
            .value = 0.6,
            .exact_max = true,
        },
    };
    fixture.direction =
        aq::evaluate_teacher_direction(
            fixture.spec, fixture.actions);
    fixture.hidden_repartition_nonvacuous = true;
    fixture.hidden_repartition_bit_identical = true;
    fixture.reversed_action_bit_identical = true;
    fixture.accounting.root_actions = 2;
    fixture.accounting.root_determinizations = 4;
    fixture.accounting.root_terminal_particles = 8;
    fixture.accounting.root_macros.transitions = 8;
    expect(
        fixture.gate_passed(),
        "AQ1-D0 rejected complete direction evidence");

    aq::TeacherDiagnosticReport report;
    report.parent_fingerprint =
        std::string(aq::kRequiredParentFingerprint);
    report.fixtures = {fixture};
    report.direction_passed[0] = true;
    report.hypothesis_passed = true;
    std::ostringstream output;
    aq::print_teacher_diagnostic_report(
        output, report);
    const std::string text = output.str();
    expect(
        text.find(
            "schema=old-school-action-q-aq1-d0-teacher-v1") !=
                std::string::npos &&
            text.find(
                "stable_id=control.blue.counter-same-target-"
                "after-intervening-counter.v1") !=
                std::string::npos &&
            text.find(
                "probe_key=counter-opponent-counterspell "
                "typed_descriptor=counterspell-stack-1") !=
                std::string::npos &&
            text.find("value=0.59999999999999998") !=
                std::string::npos &&
            text.find(
                "keys=counter-opponent-counterspell") !=
                std::string::npos &&
            text.find("required_margin=") !=
                std::string::npos &&
            text.find("root_actions=2") !=
                std::string::npos &&
            text.find("fit_performed=0 corpus_collected=0") !=
                std::string::npos &&
            text.find("gameplay_seed_opened=0") !=
                std::string::npos,
        "AQ1-D0 output omitted sealed evidence");

    fixture.reversed_action_bit_identical = false;
    expect(
        !fixture.gate_passed(),
        "AQ1-D0 accepted action-order drift");
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
        "cli_accepts_only_declared_modes",
        test_cli_accepts_only_declared_modes);
    run("recipe_is_frozen", test_recipe_is_frozen);
    run(
        "census_total_includes_every_declared_counter",
        test_census_total_includes_every_declared_counter);
    run(
        "owner_partition_contract_is_conjunctive",
        test_owner_partition_contract_is_conjunctive);
    run(
        "model_gate_output_is_quantitative",
        test_model_gate_output_is_quantitative);
    run(
        "teacher_diagnostic_manifest_and_seeds_are_frozen",
        test_teacher_diagnostic_manifest_and_seeds_are_frozen);
    run(
        "teacher_direction_gates_are_exact",
        test_teacher_direction_gates_are_exact);
    run(
        "teacher_diagnostic_gate_and_output_contract",
        test_teacher_diagnostic_gate_and_output_contract);
    std::cout
        << "action-Q Bellman experiment tests: "
        << passed << "/8 passed\n";
    return 0;
}
