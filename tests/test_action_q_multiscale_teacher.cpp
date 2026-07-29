#include "old_school/action_q_multiscale_teacher.hpp"

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

namespace multiscale =
    old_school::action_q_multiscale_teacher;
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

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

std::shared_ptr<const old_school::LearnedModel> model() {
    static const auto value =
        old_school::train_learned_value_champion(
            1, 0x4151324d53305431ULL);
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

const probes::DecisionProbe& lethal_bolt_probe() {
    static const auto corpus = probes::make_probe_dev_v3();
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [](const probes::DecisionProbe& probe) {
            return probe.stable_id ==
                   "red.bolt-face-lethal.v3";
        });
    if (found == corpus.end()) {
        throw std::runtime_error(
            "lethal-Bolt development fixture is missing");
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

std::vector<old_school::PriorityAction> actions_for(
    const probes::DecisionProbe& probe) {
    return old_school::legal_priority_actions(
        probe.state, probe.root_player,
        context_for(probe).sorcery_actions);
}

multiscale::RootTargets score_sick_bear(
    std::vector<old_school::PriorityAction> actions,
    std::uint64_t seed = 0x4151324d53305231ULL) {
    const auto& probe = sick_bear_probe();
    return multiscale::score_priority_root(
        probe.state, probe.original_decks,
        context_for(probe), actions, model(), seed);
}

void test_fixed_blend_and_complete_accounting() {
    static_assert(multiscale::kBellmanWeight == 0.75);
    static_assert(multiscale::kResolvedWeight == 0.25);

    const auto targets =
        score_sick_bear(actions_for(sick_bear_probe()));
    multiscale::validate_root_targets(targets);
    expect(
        targets.actions.size() ==
                targets.bellman.actions.size() &&
            targets.bellman.root_worlds.size() == 4 &&
            targets.resolved_accounting.root_actions ==
                targets.actions.size() &&
            targets.resolved_accounting
                    .root_determinizations == 4 &&
            targets.resolved_accounting.evaluations ==
                targets.actions.size() * 4 &&
            targets.resolved_accounting
                    .terminal_evaluations +
                    targets.resolved_accounting
                        .critic_evaluations ==
                targets.resolved_accounting.evaluations,
        "AQ2-MS0 aggregate accounting did not cross-sum");

    for (std::size_t action = 0;
         action < targets.actions.size(); ++action) {
        const auto& target = targets.actions[action];
        expect(
            target.descriptor ==
                    targets.bellman.actions[action].descriptor &&
                target.action ==
                    targets.bellman.actions[action].action &&
                same_bits(
                    target.bellman_value,
                    targets.bellman.actions[action].value) &&
                target.resolved_samples.size() == 4,
            "AQ2-MS0 action did not align with AQ1");
        for (std::size_t world = 0; world < 4; ++world) {
            expect(
                target.resolved_samples[world].source_world ==
                    targets.bellman.root_worlds[world],
                "AQ2-MS0 did not reuse an AQ1 root world");
        }
        const double expected =
            0.75 * target.bellman_value +
            0.25 * target.resolved_value;
        expect(
            same_bits(target.value, expected),
            "AQ2-MS0 did not use the fixed binary64 blend");
    }
}

void test_repeat_order_and_complete_mapping() {
    auto actions = actions_for(sick_bear_probe());
    const auto first = score_sick_bear(actions);
    const auto repeated = score_sick_bear(actions);
    std::reverse(actions.begin(), actions.end());
    const auto reversed = score_sick_bear(actions);
    expect(
        first == repeated && first == reversed,
        "AQ2-MS0 changed under repeat or candidate order");

    actions.pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(score_sick_bear(actions));
        },
        "AQ2-MS0 accepted an incomplete legal-action set");
}

void test_retained_result_validation_fails_closed() {
    const auto baseline =
        score_sick_bear(actions_for(sick_bear_probe()));

    auto wrong_world = baseline;
    ++wrong_world.actions.front()
          .resolved_samples.front()
          .source_world.determinization_seed;
    expect_rejected(
        [&] {
            multiscale::validate_root_targets(wrong_world);
        },
        "AQ2-MS0 accepted a different resolved world seed");

    auto wrong_leaf_class = baseline;
    wrong_leaf_class.actions.front()
        .resolved_samples.front()
        .critic_leaf =
        wrong_leaf_class.actions.front()
            .resolved_samples.front()
            .terminal;
    expect_rejected(
        [&] {
            multiscale::validate_root_targets(
                wrong_leaf_class);
        },
        "AQ2-MS0 accepted an unclassified resolved leaf");

    auto wrong_value = baseline;
    wrong_value.actions.front().value = 0.5;
    expect_rejected(
        [&] {
            multiscale::validate_root_targets(wrong_value);
        },
        "AQ2-MS0 accepted a changed composite value");

    auto wrong_accounting = baseline;
    ++wrong_accounting.resolved_accounting
          .critic_evaluations;
    expect_rejected(
        [&] {
            multiscale::validate_root_targets(
                wrong_accounting);
        },
        "AQ2-MS0 accepted inconsistent aggregate accounting");
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

    const auto direct = multiscale::score_priority_root(
        root.state, root.original_decks, root.context,
        root.legal_actions, model(),
        0x4151324d53304849ULL);
    const auto repartitioned =
        multiscale::score_priority_root(
            hidden.state, hidden.original_decks,
            hidden.context, hidden.legal_actions, model(),
            0x4151324d53304849ULL);
    expect(
        direct == repartitioned,
        "AQ2-MS0 exposed an opponent hidden-zone repartition");
}

void test_terminal_and_critic_leaves_are_accounted() {
    const auto& probe = lethal_bolt_probe();
    const auto targets =
        multiscale::score_priority_root(
            probe.state, probe.original_decks,
            context_for(probe), actions_for(probe), model(),
            0x4151324d53304c54ULL);
    const auto lethal = std::find_if(
        targets.actions.begin(), targets.actions.end(),
        [](const multiscale::ActionTarget& target) {
            return target.action ==
                   old_school::PriorityAction::
                       cast_lightning_bolt(
                           old_school::Target::player_target(1));
        });
    expect(
        lethal != targets.actions.end() &&
            lethal->resolved_accounting
                    .terminal_evaluations == 4 &&
            lethal->resolved_accounting
                    .critic_evaluations == 0 &&
            std::all_of(
                lethal->resolved_samples.begin(),
                lethal->resolved_samples.end(),
                [](const multiscale::ResolvedSample& sample) {
                    return sample.terminal &&
                           !sample.critic_leaf &&
                           sample.value == 1.0;
                }) &&
            targets.resolved_accounting
                    .terminal_evaluations >= 4 &&
            targets.resolved_accounting
                    .critic_evaluations > 0,
        "AQ2-MS0 did not account terminal and critic leaves");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "fixed blend and complete accounting",
        test_fixed_blend_and_complete_accounting);
    tests.run(
        "repeat order and complete mapping",
        test_repeat_order_and_complete_mapping);
    tests.run(
        "retained result fails closed",
        test_retained_result_validation_fails_closed);
    tests.run(
        "hidden repartition identity",
        test_hidden_repartition_is_bit_identical);
    tests.run(
        "terminal and critic accounting",
        test_terminal_and_critic_leaves_are_accounted);
    return tests.finish();
}
