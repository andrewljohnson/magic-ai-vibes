#include "old_school/action_q_bellman_teacher.hpp"

#include "old_school/action_q_field_gate.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace teacher =
    old_school::action_q_bellman_teacher;
namespace bellman = old_school::fq0_bellman;
namespace field = old_school::action_q_field_gate;
namespace probes = old_school::probes;

namespace {

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

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

std::shared_ptr<const old_school::LearnedModel> model() {
    static const auto value =
        old_school::train_learned_value_champion(
            1, 0x415131424c305431ULL);
    return value;
}

const probes::DecisionProbe& sick_bear_probe() {
    static const auto corpus =
        probes::make_field_regressions_v1();
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [](const probes::DecisionProbe& probe) {
            return probe.stable_id ==
                   "field.green.second-main-sick-bear-growth.v1";
        });
    if (found == corpus.end()) {
        throw std::runtime_error(
            "sick-Bear field fixture is missing");
    }
    return *found;
}

old_school::LearnedDecisionContext context_for(
    const probes::DecisionProbe& probe) {
    return {
        .valid = true,
        .phase = probe.phase,
        .decision_player = probe.root_player,
        .consecutive_passes =
            probe.consecutive_passes,
        .sorcery_actions =
            probe.phase ==
                old_school::TurnPhase::FirstMain ||
            probe.phase ==
                old_school::TurnPhase::SecondMain,
    };
}

teacher::RootTargets score_sick_bear(
    std::vector<old_school::PriorityAction> actions,
    std::uint64_t seed = 0x415131424c305231ULL) {
    const auto& probe = sick_bear_probe();
    return teacher::score_priority_root(
        probe.state, probe.original_decks,
        context_for(probe), actions, model(), seed);
}

std::vector<old_school::PriorityAction>
sick_bear_actions() {
    const auto& probe = sick_bear_probe();
    return old_school::legal_priority_actions(
        probe.state, probe.root_player, true);
}

double action_value(
    const teacher::RootTargets& targets,
    const old_school::PriorityAction& wanted) {
    const auto found = std::find_if(
        targets.actions.begin(), targets.actions.end(),
        [&](const teacher::RootActionTarget& action) {
            return action.action == wanted;
        });
    if (found == targets.actions.end()) {
        throw std::runtime_error(
            "Bellman target omitted a required action");
    }
    return found->value;
}

void test_fixed_recipe_and_complete_accounting() {
    static_assert(teacher::kRootWorlds == 4);
    static_assert(teacher::kSuccessorWorlds == 4);

    const auto targets =
        score_sick_bear(sick_bear_actions());
    teacher::validate_root_targets(targets);
    expect(
        targets.actions.size() == 2 &&
            targets.root_worlds.size() == 4 &&
            targets.accounting.root_actions == 2 &&
            targets.accounting.root_determinizations == 4 &&
            targets.accounting.root_macros.transitions == 8 &&
            targets.accounting.root_terminal_particles +
                    targets.accounting.root_boundary_particles ==
                8 &&
            targets.accounting.same_owner_group_occurrences +
                    targets.accounting
                        .opponent_owner_group_occurrences ==
                targets.accounting
                    .successor_group_occurrences &&
            targets.accounting.same_owner_root_particles +
                    targets.accounting
                        .opponent_owner_root_particles ==
                targets.accounting.root_boundary_particles &&
            targets.accounting.root_terminal_particles +
                    targets.accounting
                        .same_owner_root_particles +
                    targets.accounting
                        .opponent_owner_root_particles ==
                8 &&
            targets.accounting
                    .unique_successor_information_sets ==
                targets.successor_evaluations.size() &&
            targets.accounting.successor_determinizations ==
                targets.successor_evaluations.size() * 2 *
                    teacher::kSuccessorWorlds &&
            targets.accounting.successor_macros.transitions ==
                targets.accounting.successor_actions *
                    2 * teacher::kSuccessorWorlds &&
            targets.accounting.successor_macros
                    .terminal_transitions +
                    targets.accounting.successor_macros
                        .critic_leaves ==
                targets.accounting.successor_macros
                    .transitions,
        "AQ1-BL0 accounting did not cross-sum");

    for (const auto& evaluation :
         targets.successor_evaluations) {
        expect(
            evaluation.bank_a.worlds.size() == 4 &&
                evaluation.bank_b.worlds.size() == 4 &&
                evaluation.bank_a.stream_key !=
                    evaluation.bank_b.stream_key &&
                evaluation.bank_a.actions.size() ==
                    evaluation.actions.size() &&
                evaluation.bank_b.actions.size() ==
                    evaluation.actions.size(),
            "AQ1-BL0 successor banks are not independent "
            "common-world action banks");
    }
}

void test_action_order_and_repeat_are_bit_identical() {
    auto actions = sick_bear_actions();
    const auto first = score_sick_bear(actions);
    const auto repeated = score_sick_bear(actions);
    std::reverse(actions.begin(), actions.end());
    const auto reversed = score_sick_bear(actions);
    expect(
        first == repeated && first == reversed,
        "AQ1-BL0 changed under repeat or candidate order");

    const auto different_seed =
        score_sick_bear(actions, 0x415131424c305232ULL);
    expect(
        first.root_worlds != different_seed.root_worlds,
        "AQ1-BL0 root seed did not change world coordinates");
}

void test_partition_and_owner_perspective_fail_closed() {
    const auto baseline =
        score_sick_bear(sick_bear_actions());
    expect(
        !baseline.actions.front().successor_groups.empty(),
        "AQ1-BL0 fixture produced no successor group");

    auto overlapping = baseline;
    auto& members =
        overlapping.actions.front()
            .successor_groups.front()
            .root_world_indices;
    members.push_back(members.front());
    expect_rejected(
        [&] {
            teacher::validate_root_targets(overlapping);
        },
        "AQ1-BL0 accepted an overlapping particle partition");

    auto wrong_perspective = baseline;
    auto& group =
        wrong_perspective.actions.front()
            .successor_groups.front();
    group.relation =
        group.relation ==
                bellman::OwnerRelation::SameOwner
            ? bellman::OwnerRelation::OpponentOwner
            : bellman::OwnerRelation::SameOwner;
    expect_rejected(
        [&] {
            teacher::validate_root_targets(
                wrong_perspective);
        },
        "AQ1-BL0 accepted a flipped owner perspective");

    auto wrong_owner_census = baseline;
    ++wrong_owner_census.accounting
          .opponent_owner_group_occurrences;
    expect_rejected(
        [&] {
            teacher::validate_root_targets(
                wrong_owner_census);
        },
        "AQ1-BL0 accepted inconsistent group-owner "
        "accounting");

    auto incomplete_bank = baseline;
    incomplete_bank.successor_evaluations.front()
        .bank_a.actions.front()
        .samples.pop_back();
    expect_rejected(
        [&] {
            teacher::validate_root_targets(incomplete_bank);
        },
        "AQ1-BL0 accepted unequal successor action banks");
}

void test_hidden_repartition_is_bit_identical() {
    const field::AncestralFieldRoot root =
        field::make_ancestral_field_root();
    const field::AncestralFieldRoot hidden =
        field::hidden_repartition_clone(root);
    expect(
        root.state != hidden.state &&
            old_school::observe_game_state(
                root.state, root.actor) ==
                old_school::observe_game_state(
                    hidden.state, hidden.actor),
        "Ancestral fixture did not create a nonvacuous hidden "
        "repartition");

    const auto direct = teacher::score_priority_root(
        root.state, root.original_decks, root.context,
        root.legal_actions, model(),
        0x415131424c304849ULL);
    const auto repartitioned =
        teacher::score_priority_root(
            hidden.state, hidden.original_decks,
            hidden.context, hidden.legal_actions, model(),
            0x415131424c304849ULL);
    expect(
        direct == repartitioned,
        "AQ1-BL0 exposed an opponent hidden-zone "
        "repartition");

    const bool has_opponent_owned_group =
        std::any_of(
            direct.actions.begin(), direct.actions.end(),
            [](const teacher::RootActionTarget& action) {
                return std::any_of(
                    action.successor_groups.begin(),
                    action.successor_groups.end(),
                    [](const auto& group) {
                        return group.relation ==
                               bellman::OwnerRelation::
                                   OpponentOwner;
                    });
            });
    expect(
        has_opponent_owned_group &&
            direct.accounting
                    .opponent_owner_group_occurrences >
                0 &&
            direct.accounting
                    .opponent_owner_root_particles >
                0,
        "AQ1-BL0 fixture did not exercise an "
        "opponent-owned successor group");
}

void test_sick_bear_temporal_direction() {
    const auto targets =
        score_sick_bear(sick_bear_actions());
    const double pass =
        action_value(
            targets,
            old_school::PriorityAction::pass());
    const double growth = action_value(
        targets,
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 1)));
    expect(
        pass > growth,
        "reduced AQ1-BL0 coordinates did not prefer holding "
        "Growth on the sick-Bear fixture");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "fixed recipe and complete accounting",
        test_fixed_recipe_and_complete_accounting);
    tests.run(
        "repeat and action-order identity",
        test_action_order_and_repeat_are_bit_identical);
    tests.run(
        "partition and perspective rejection",
        test_partition_and_owner_perspective_fail_closed);
    tests.run(
        "hidden-repartition identity",
        test_hidden_repartition_is_bit_identical);
    tests.run(
        "sick-Bear temporal direction",
        test_sick_bear_temporal_direction);
    return tests.finish();
}
