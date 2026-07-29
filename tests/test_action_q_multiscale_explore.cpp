#include "old_school/action_q_multiscale_explore.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_multiscale_explore;

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

void test_cli_is_sealed_to_two_modes() {
    const std::vector<std::string_view> census{
        "--census",
    };
    const std::vector<std::string_view> run{"--run"};
    const std::vector<std::string_view> diagnose{
        "--diagnose-teacher",
    };
    const std::vector<std::string_view> extra{
        "--census",
        "--run",
    };
    expect(
        aq::parse_command(census) == aq::Command::Census &&
            aq::parse_command(run) == aq::Command::Run &&
            !aq::parse_command(diagnose).has_value() &&
            !aq::parse_command(extra).has_value(),
        "AQ2 CLI accepted an undeclared mode");
    std::ostringstream usage;
    aq::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-multiscale-explore "
            "(--census|--run)\n",
        "AQ2 usage drifted");
}

void test_recipe_is_frozen_and_run_fails_closed() {
    const auto optimizer = aq::optimizer_config();
    expect(
        aq::kCollectionRootSeed == 202607281935ULL &&
            aq::kReservedSelectorSeed ==
                202607281945ULL &&
            aq::kSelectorRepetitions == 1 &&
            aq::kExpectedSelectorGames == 60 &&
            aq::kExpectedSelectorGamesPerDeck == 12 &&
            aq::kSelectorWinsRequired == 31 &&
            !aq::selector_wins_advance(30) &&
            aq::selector_wins_advance(31) &&
            aq::kMaximumRootsPerActorGame == 8 &&
            aq::kBaseWorlds == 8 &&
            aq::kBaseHorizonTurns == 4 &&
            aq::kBaseRolloutsPerWorld == 1 &&
            aq::kBaseEvaluationThreads == 4 &&
            aq::fit_seed() == 6876098192504870922ULL &&
            optimizer.batch_size == 64 &&
            optimizer.epochs == 64 &&
            optimizer.learning_rate == 0.003 &&
            optimizer.residual_weight == 0.10 &&
            aq::kTeacherPrimaryWeight == 0.90 &&
            old_school::action_q_multiscale_teacher::
                    kBellmanWeight == 0.75 &&
            old_school::action_q_multiscale_teacher::
                    kResolvedWeight == 0.25 &&
            aq::kFrozenCensusIdentity.empty(),
        "AQ2 frozen recipe or fail-closed census changed");
    expect_rejected(
        [] {
            static_cast<void>(
                aq::root_search_seed(2, 0, 0, 0));
        },
        "AQ2 accepted an undeclared corpus block");
}

void test_preflight_manifest_reuses_exact_d0_coordinates() {
    const auto manifest = aq::preflight_manifest();
    constexpr std::array<std::uint64_t, 4> kSeeds{
        13755611371498319020ULL,
        2589590173959096294ULL,
        4410279927652125381ULL,
        118189991942941696ULL,
    };
    bool exact = manifest.size() == kSeeds.size();
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        exact =
            exact &&
            manifest[index].fixture_index == index &&
            manifest[index].expected_seed ==
                kSeeds[index] &&
            aq::preflight_seed(index) == kSeeds[index];
    }
    expect(
        exact &&
            aq::kPreflightRootSeed == 202607281913ULL &&
            manifest[0].positive_key ==
                "counter-opponent-counterspell" &&
            manifest[1].excluded_keys[0] ==
                "braingeyser-x0-self" &&
            manifest[1].excluded_keys[1] ==
                "braingeyser-x0-opponent" &&
            manifest[2].positive_key == "pass" &&
            manifest[3].positive_key ==
                "force-spike-gray-ogre",
        "AQ2 did not preserve exact D0 coordinates");
    expect_rejected(
        [] {
            static_cast<void>(aq::preflight_seed(4));
        },
        "AQ2 accepted an undeclared D0 fixture");
}

void test_multiscale_direction_is_exact() {
    const auto manifest = aq::preflight_manifest();
    std::vector<aq::PreflightAction> pair{
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .bellman_value = 0.6,
            .resolved_value = 0.2,
            .value = 0.5,
        },
        {
            .probe_key =
                "counter-opponent-counterspell",
            .typed_descriptor = "counter-stack-1",
            .action =
                old_school::PriorityAction::
                    cast_counterspell(1),
            .bellman_value = 0.7,
            .resolved_value = 0.5,
            .value = 0.65,
        },
    };
    const aq::DirectionSummary direction =
        aq::evaluate_direction(manifest[0], pair);
    expect(
        direction.passed &&
            std::abs(direction.required_margin - 0.15) <
                1.0e-15 &&
            direction.exact_max_support ==
                std::vector<std::string>{
                    "counter-opponent-counterspell",
                },
        "AQ2 strict direction did not use composite values");
    pair[0].value = pair[1].value;
    expect(
        !aq::evaluate_direction(manifest[0], pair)
             .passed,
        "AQ2 accepted a tied strict direction");

    std::vector<aq::PreflightAction> x_zero{
        {
            .probe_key = "pass",
            .typed_descriptor = "pass",
            .action =
                old_school::PriorityAction::pass(),
            .bellman_value = 0.5,
            .resolved_value = 0.7,
            .value = 0.55,
        },
        {
            .probe_key = "braingeyser-x0-self",
            .typed_descriptor = "braingeyser-x0-self",
            .action =
                old_school::PriorityAction::
                    cast_braingeyser(
                        0,
                        old_school::Target::
                            player_target(0)),
            .bellman_value = 0.6,
            .resolved_value = 0.2,
            .value = 0.5,
        },
        {
            .probe_key = "braingeyser-x0-opponent",
            .typed_descriptor =
                "braingeyser-x0-opponent",
            .action =
                old_school::PriorityAction::
                    cast_braingeyser(
                        0,
                        old_school::Target::
                            player_target(1)),
            .bellman_value = 0.6,
            .resolved_value = 0.3,
            .value = 0.525,
        },
    };
    expect(
        aq::evaluate_direction(
            manifest[1], x_zero)
            .passed,
        "AQ2 rejected strict X=0 exclusion");
    x_zero[2].value = x_zero[0].value;
    expect(
        !aq::evaluate_direction(
             manifest[1], x_zero)
             .passed,
        "AQ2 accepted X=0 in exact-max support");
}

void test_owner_partition_is_conjunctive() {
    old_school::action_q_bellman_teacher::TeacherAccounting
        accounting;
    accounting.root_terminal_particles = 2;
    accounting.root_boundary_particles = 6;
    accounting.successor_group_occurrences = 3;
    accounting.same_owner_group_occurrences = 2;
    accounting.opponent_owner_group_occurrences = 1;
    accounting.same_owner_root_particles = 3;
    accounting.opponent_owner_root_particles = 3;
    expect(
        aq::owner_partition_complete(accounting, 2),
        "AQ2 rejected a complete owner partition");
    ++accounting.opponent_owner_group_occurrences;
    expect(
        !aq::owner_partition_complete(accounting, 2),
        "AQ2 accepted an overcounted group partition");
    --accounting.opponent_owner_group_occurrences;
    --accounting.root_terminal_particles;
    expect(
        !aq::owner_partition_complete(accounting, 2),
        "AQ2 accepted an incomplete particle partition");
}

void test_census_total_keeps_both_teacher_scales() {
    aq::BlockCensus block;
    auto& first = block.decks[0];
    auto& second = block.decks[1];
    first.actor_games = 3;
    second.actor_games = 5;
    first.retained_roots = 2;
    second.retained_roots = 4;
    first.retained_options = 5;
    second.retained_options = 9;
    first.bellman_accounting.root_actions = 5;
    second.bellman_accounting.root_actions = 9;
    first.bellman_accounting.successor_actions = 13;
    second.bellman_accounting.successor_actions = 17;
    first.resolved_accounting.root_actions = 5;
    second.resolved_accounting.root_actions = 9;
    first.resolved_accounting.evaluations = 20;
    second.resolved_accounting.evaluations = 36;
    first.resolved_accounting.priority_passes = 7;
    second.resolved_accounting.priority_passes = 11;
    const aq::DeckCensus total = block.total();
    expect(
        block.retained_roots() == 6 &&
            block.retained_options() == 14 &&
            total.actor_games == 8 &&
            total.bellman_accounting.root_actions == 14 &&
            total.bellman_accounting.successor_actions == 30 &&
            total.resolved_accounting.root_actions == 14 &&
            total.resolved_accounting.evaluations == 56 &&
            total.resolved_accounting.priority_passes == 18,
        "AQ2 census total dropped teacher accounting");
}

void test_model_gate_output_is_quantitative() {
    old_school::action_q_offline_gate::ModelGateReport report;
    report.frozen_dev.parent.probe_count = 20;
    report.frozen_dev.parent.mean_regret = 0.25;
    report.frozen_dev.candidate.probe_count = 20;
    report.frozen_dev.candidate.mean_regret = 0.125;
    report.frozen_dev.stable_parent_agreements = 12;
    report.frozen_dev.lost_stable_parent_agreements = 1;
    report.frozen_dev.cache_before.byte_size = 123;
    report.frozen_dev.cache_before.sha256 = "before-sha";
    report.ancestral.self_score = 0.8;
    report.ancestral.opponent_score = 0.2;
    report.descriptor_order.model_count = 2;
    report.descriptor_order.probe_count = 8;
    report.behavior.sick_bear_growth_selects_pass = true;
    report.behavior.braingeyser_x_zero_excluded = true;
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
                "stable_parent_agreements=12") !=
                std::string::npos &&
            text.find("cache_before_sha256=before-sha") !=
                std::string::npos &&
            text.find("sick_bear_growth_pass=1") !=
                std::string::npos &&
            text.find("braingeyser_x0_excluded=1") !=
                std::string::npos,
        "AQ2 model-gate output omitted quantitative evidence");
}

} // namespace

int main() {
    const std::vector<std::pair<
        std::string_view, void (*)()>>
        tests{
            {
                "cli is sealed",
                test_cli_is_sealed_to_two_modes,
            },
            {
                "recipe is frozen",
                test_recipe_is_frozen_and_run_fails_closed,
            },
            {
                "D0 coordinates exact",
                test_preflight_manifest_reuses_exact_d0_coordinates,
            },
            {
                "direction exact",
                test_multiscale_direction_is_exact,
            },
            {
                "owner partition",
                test_owner_partition_is_conjunctive,
            },
            {
                "census accounting",
                test_census_total_keeps_both_teacher_scales,
            },
            {
                "quantitative output",
                test_model_gate_output_is_quantitative,
            },
        };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr
                << "FAIL: " << name << ": "
                << error.what() << '\n';
        }
    }
    std::cout
        << passed << '/' << tests.size()
        << " action-Q multiscale explore tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
