#include "old_school/fq0_dominance.hpp"
#include "old_school/fq0_dominance_transition.hpp"
#include "old_school/probe_runner.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace dominance = old_school::fq0_dominance;
namespace transition =
    old_school::fq0_dominance_transition;
namespace probes = old_school::probes;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

const probes::DecisionProbe& find_probe(
    const std::vector<probes::DecisionProbe>& corpus,
    std::string_view stable_id) {
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [&](const probes::DecisionProbe& probe) {
            return probe.stable_id == stable_id;
        });
    if (found == corpus.end()) {
        throw std::runtime_error("focused fixture is missing");
    }
    return *found;
}

std::size_t candidate_index(
    const probes::DecisionProbe& probe,
    std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [&](const probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error("focused action is missing");
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

std::size_t candidate_kind(
    const probes::DecisionProbe& probe,
    old_school::PriorityActionKind kind) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [&](const probes::Candidate& candidate) {
            const auto* action =
                std::get_if<old_school::PriorityAction>(
                    &candidate.action);
            return action != nullptr && action->kind == kind;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            "focused action kind is missing");
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

dominance::Settlement settle(
    const probes::DecisionProbe& probe, std::size_t candidate,
    std::string fingerprint = "focused-information-set") {
    return transition::advance_to_next_first_main(
        probe, probe.state, candidate, std::move(fingerprint));
}

void test_settlement_is_production_owned() {
    static_assert(
        !std::is_default_constructible_v<
            dominance::Settlement>);
    static_assert(
        !std::is_copy_assignable_v<
            dominance::Settlement>);
    static_assert(
        !std::is_move_assignable_v<
            dominance::Settlement>);

    const auto corpus = probes::make_field_regressions_v1();
    const auto& probe = find_probe(
        corpus,
        "field.green.second-main-sick-bear-growth.v1");
    const auto result = settle(
        probe, candidate_index(probe, "pass"));
    expect(
        result.complete() &&
            result.root_state() == probe.state &&
            result.resource_operations().empty() &&
            result.root_information_fingerprint() ==
                "focused-information-set",
        "production-owned Settlement accessors drifted");
}

void test_pass_dominates_sick_bear_growth() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto& probe = find_probe(
        corpus,
        "field.green.second-main-sick-bear-growth.v1");
    const auto held =
        settle(probe, candidate_index(probe, "pass"));
    const auto spent = settle(
        probe,
        candidate_index(
            probe,
            "growth-own-summoning-sick-grizzly-bears"));
    const auto comparison = dominance::compare(
        held, spent, probe.root_player);
    expect(
        comparison.orientation ==
                dominance::Orientation::
                    FirstDominatesSecond &&
            comparison.first_normalized &&
            comparison.second_normalized &&
            comparison.consequences_equal,
        "Pass did not dominate second-main sick-Bear Growth");
}

void test_pass_dominates_x_zero_spends() {
    const auto braingeyser_corpus =
        probes::make_braingeyser_x_zero_control_v1();
    const auto& braingeyser = braingeyser_corpus.front();
    const auto pass = settle(
        braingeyser,
        candidate_index(braingeyser, "pass"),
        "braingeyser-root");
    const auto self_x_zero = settle(
        braingeyser,
        candidate_index(
            braingeyser, "braingeyser-x0-self"),
        "braingeyser-root");
    const auto& operations =
        self_x_zero.resource_operations();
    expect(
        operations.size() == 2 &&
            operations[0].include_hand_and_land_costs &&
            !operations[1].include_hand_and_land_costs &&
            operations.front().before ==
                self_x_zero.root_state() &&
            operations[0].after == operations[1].before &&
            operations.back().after ==
                self_x_zero.resource_boundary_state(),
        "resource witness did not preserve cast then resolution");
    for (const std::string_view descriptor :
         {"braingeyser-x0-self",
          "braingeyser-x0-opponent"}) {
        expect(
            dominance::compare(
                pass,
                settle(
                    braingeyser,
                    candidate_index(
                        braingeyser, descriptor),
                    "braingeyser-root"),
                braingeyser.root_player)
                    .orientation ==
                dominance::Orientation::
                    FirstDominatesSecond,
            "Pass did not dominate a Braingeyser X=0 spend");
    }

    const auto disintegrate_corpus =
        probes::make_probe_validation_v1();
    const auto& disintegrate =
        disintegrate_corpus.front();
    const auto disintegrate_pass = settle(
        disintegrate,
        candidate_kind(
            disintegrate,
            old_school::PriorityActionKind::Pass),
        "disintegrate-root");
    std::size_t checked = 0;
    for (std::size_t index = 0;
         index < disintegrate.candidates.size(); ++index) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &disintegrate.candidates[index].action);
        if (action == nullptr ||
            action->kind !=
                old_school::PriorityActionKind::
                    CastDisintegrate ||
            action->x_value != 0) {
            continue;
        }
        expect(
            dominance::compare(
                disintegrate_pass,
                settle(
                    disintegrate, index,
                    "disintegrate-root"),
                disintegrate.root_player)
                    .orientation ==
                dominance::Orientation::
                    FirstDominatesSecond,
            "Pass did not dominate a Disintegrate X=0 spend");
        ++checked;
    }
    expect(checked > 0,
           "Disintegrate fixture has no X=0 action");
}

void test_public_object_and_source_permutations_are_equivalent() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto& original_probe = find_probe(
        corpus,
        "field.green.second-main-sick-bear-growth.v1");
    probes::DecisionProbe permuted_probe = original_probe;
    std::reverse(
        permuted_probe.state.players[0].lands.begin(),
        permuted_probe.state.players[0].lands.end());
    permuted_probe.state.players[0].creatures[0].id = 99;
    permuted_probe.state.next_permanent_id = 100;
    const std::size_t growth = candidate_index(
        original_probe,
        "growth-own-summoning-sick-grizzly-bears");
    auto* permuted_growth =
        std::get_if<old_school::PriorityAction>(
            &permuted_probe.candidates[growth].action);
    expect(permuted_growth != nullptr,
           "permuted fixture lost its typed action");
    permuted_growth->target =
        old_school::Target::creature_target(0, 99);

    const auto original = settle(
        original_probe, growth, "permutation-root");
    const auto permuted = settle(
        permuted_probe, growth, "permutation-root");
    const auto original_canonical =
        dominance::canonicalize_settlement(original, 0);
    const auto permuted_canonical =
        dominance::canonicalize_settlement(permuted, 0);
    expect(
        original_canonical.valid &&
            permuted_canonical.valid &&
            original_canonical == permuted_canonical &&
            dominance::compare(original, permuted, 0)
                    .orientation ==
                dominance::Orientation::Incomparable,
        "permutation-equivalent public objects or sources diverged");
}

void test_force_spike_tradeoffs_stay_incomparable() {
    const auto corpus =
        probes::make_force_spike_policy_controls_v1();
    for (const auto& probe : corpus) {
        const auto held = settle(
            probe, candidate_kind(
                       probe,
                       old_school::PriorityActionKind::Pass),
            probe.stable_id);
        const auto spike = settle(
            probe, candidate_kind(
                       probe,
                       old_school::PriorityActionKind::
                           CastForceSpike),
            probe.stable_id);
        const auto comparison = dominance::compare(
            held, spike, probe.root_player);
        const bool payable =
            probe.stable_id.find("payable") !=
            std::string::npos;
        expect(
            comparison.first_normalized &&
                comparison.second_normalized &&
                comparison.orientation ==
                    dominance::Orientation::Incomparable &&
                comparison.consequences_equal == payable,
            "Force Spike control lost its declared tradeoff");
    }
}

void test_transient_combat_effect_fails_closed() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto& probe = find_probe(
        corpus,
        "field.green.begin-combat-growth-tapped-air.v1");
    const auto held =
        settle(probe, candidate_index(probe, "pass"));
    const auto pumped = settle(
        probe,
        candidate_index(
            probe, "growth-own-ironroot-treefolk"));
    const auto comparison = dominance::compare(
        held, pumped, probe.root_player);
    expect(
        !held.unresolved_transient_choice_effect() &&
            pumped.unresolved_transient_choice_effect() &&
            comparison.first_normalized &&
            !comparison.second_normalized &&
            comparison.orientation ==
                dominance::Orientation::Incomparable,
        "begin-combat transient option value did not fail closed");
}

void test_hidden_repartition_is_redacted() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto& probe = find_probe(
        corpus,
        "field.green.second-main-sick-bear-growth.v1");
    const old_school::GameState hidden =
        old_school::probe_runner::hidden_repartition_clone(
            probe);
    const std::size_t pass =
        candidate_index(probe, "pass");
    const auto original = transition::advance_to_next_first_main(
        probe, probe.state, pass, "hidden-root");
    const auto clone = transition::advance_to_next_first_main(
        probe, hidden, pass, "hidden-root");
    expect(
        dominance::canonicalize_settlement(
            original, probe.root_player) ==
            dominance::canonicalize_settlement(
                clone, probe.root_player),
        "hidden repartition entered the owner consequence");
}

probes::DecisionProbe actor_one_sick_bear_probe() {
    probes::DecisionProbe probe{
        .stable_id = "test.actor-one-sick-bear",
        .category = probes::Category::GreenGrowthHold,
        .decision_kind = probes::DecisionKind::Priority,
        .root_deck = old_school::DeckId::Green,
        .opponent_deck = old_school::DeckId::Red,
        .root_player = 1,
        .phase = old_school::TurnPhase::SecondMain,
        .consecutive_passes = 1,
    };
    probe.state.active_player = 1;
    probe.state.starting_player = 1;
    probe.state.turn_number = 9;
    probe.state.next_permanent_id = 2;
    probe.state.players[0].library = {
        old_school::CardId::Mountain};
    probe.state.players[1].library = {
        old_school::CardId::Forest};
    probe.state.players[1].hand = {
        old_school::CardId::GiantGrowth};
    probe.state.players[1].lands = {
        {.card = old_school::CardId::Forest}};
    probe.state.players[1].creatures = {{
        .id = 1,
        .card = old_school::CardId::GrizzlyBears,
        .summoning_sick = true,
    }};
    probe.state.players[1].land_played_this_turn = true;
    probe.candidates = {
        {
            .descriptor = "pass",
            .action = old_school::PriorityAction::pass(),
        },
        {
            .descriptor = "growth",
            .action =
                old_school::PriorityAction::cast_giant_growth(
                    old_school::Target::creature_target(
                        1, 1)),
        },
    };
    return probe;
}

void test_actor_one_ordering_is_symmetric() {
    const auto probe = actor_one_sick_bear_probe();
    const auto held = settle(probe, 0, "actor-one-root");
    const auto spent = settle(probe, 1, "actor-one-root");
    expect(
        dominance::compare(held, spent, 1).orientation ==
            dominance::Orientation::FirstDominatesSecond,
        "actor-one dominance ordering was not symmetric");
}

probes::DecisionProbe cross_color_x_zero_probe() {
    probes::DecisionProbe probe{
        .stable_id = "test.cross-color-x-zero",
        .category =
            probes::Category::ControlBlueBraingeyserXZero,
        .decision_kind = probes::DecisionKind::Priority,
        .root_deck = old_school::DeckId::Blue,
        .opponent_deck = old_school::DeckId::Red,
        .root_player = 0,
        .phase = old_school::TurnPhase::SecondMain,
        .consecutive_passes = 1,
    };
    probe.state.active_player = 0;
    probe.state.starting_player = 0;
    probe.state.turn_number = 9;
    probe.state.players[0].library = {
        old_school::CardId::Island};
    probe.state.players[1].library = {
        old_school::CardId::Mountain};
    probe.state.players[0].hand = {
        old_school::CardId::Braingeyser,
        old_school::CardId::Disintegrate,
    };
    probe.state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Mountain},
    };
    probe.state.players[0].land_played_this_turn = true;
    probe.candidates = {
        {
            .descriptor = "blue-x0",
            .action =
                old_school::PriorityAction::cast_braingeyser(
                    0,
                    old_school::Target::player_target(0)),
        },
        {
            .descriptor = "red-x0",
            .action =
                old_school::PriorityAction::cast_disintegrate(
                    0,
                    old_school::Target::player_target(0)),
        },
    };
    return probe;
}

void test_cross_color_costs_are_incomparable() {
    const auto probe = cross_color_x_zero_probe();
    const auto blue = settle(probe, 0, "cross-color-root");
    const auto red = settle(probe, 1, "cross-color-root");
    const auto comparison =
        dominance::compare(blue, red, 0);
    expect(
        comparison.first_normalized &&
            comparison.second_normalized &&
            comparison.consequences_equal &&
            comparison.orientation ==
                dominance::Orientation::Incomparable,
        "cross-color/card/source costs were falsely ordered");
    expect(
        dominance::compare(blue, blue, 0).orientation ==
            dominance::Orientation::Incomparable,
        "equal costs produced strict dominance");
}

probes::DecisionProbe mixed_terminal_probe() {
    probes::DecisionProbe probe{
        .stable_id = "test.mixed-terminal-precedence",
        .category =
            probes::Category::ControlBlueBraingeyserXZero,
        .decision_kind = probes::DecisionKind::Priority,
        .root_deck = old_school::DeckId::Blue,
        .opponent_deck = old_school::DeckId::Red,
        .root_player = 0,
        .phase = old_school::TurnPhase::FirstMain,
        .consecutive_passes = 1,
    };
    probe.state.active_player = 0;
    probe.state.starting_player = 0;
    probe.state.turn_number = 9;
    probe.state.next_stack_object_id = 2;
    probe.state.players[0].life = 0;
    probe.state.stack = {{
        .kind = old_school::StackObjectKind::Spell,
        .id = 1,
        .card = old_school::CardId::AncestralRecall,
        .controller = 0,
        .target = old_school::Target::player_target(1),
    }};
    probe.candidates = {{
        .descriptor = "pass",
        .action = old_school::PriorityAction::pass(),
    }};
    return probe;
}

void test_failed_draw_has_production_terminal_precedence() {
    const auto probe = mixed_terminal_probe();
    const auto terminal = settle(
        probe, 0, "mixed-terminal-root");
    expect(
        terminal.terminal() &&
            terminal.terminal_winner() == 0 &&
            terminal.boundary_state().failed_draw[1] &&
            terminal.boundary_state().players[0].life == 0 &&
            dominance::canonicalize_settlement(terminal, 0)
                .valid,
        "failed-draw precedence diverged from production rules");
}

class Runner {
  public:
    void run(std::string name,
             const std::function<void()>& test) {
        try {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& failure) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << failure.what() << '\n';
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

} // namespace

int main() {
    Runner runner;
    runner.run("Settlement is production-owned",
               test_settlement_is_production_owned);
    runner.run("Pass dominates sick-Bear Growth",
               test_pass_dominates_sick_bear_growth);
    runner.run("Pass dominates X=0 spends",
               test_pass_dominates_x_zero_spends);
    runner.run("Force Spike controls stay incomparable",
               test_force_spike_tradeoffs_stay_incomparable);
    runner.run("public object/source permutations",
               test_public_object_and_source_permutations_are_equivalent);
    runner.run("transient combat effect fails closed",
               test_transient_combat_effect_fails_closed);
    runner.run("hidden repartition is redacted",
               test_hidden_repartition_is_redacted);
    runner.run("actor-one ordering is symmetric",
               test_actor_one_ordering_is_symmetric);
    runner.run("cross-color costs stay incomparable",
               test_cross_color_costs_are_incomparable);
    runner.run("terminal precedence matches production",
               test_failed_draw_has_production_terminal_precedence);
    return runner.finish();
}
