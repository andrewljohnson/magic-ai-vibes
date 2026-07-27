#include "old_school/fq0_dominance_transition.hpp"
#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace transition =
    old_school::fq0_dominance_transition;
namespace dominance = old_school::fq0_dominance;
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

void test_sick_bear_pass_strictly_dominates_growth() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto& probe = find_probe(
        corpus,
        "field.green.second-main-sick-bear-growth.v1");
    const std::size_t pass =
        candidate_index(probe, "pass");
    const std::size_t growth =
        candidate_index(
            probe, "growth-own-summoning-sick-grizzly-bears");
    const auto held = transition::advance_to_next_first_main(
        probe, probe.state, pass, "sick-bear-root");
    const auto spent = transition::advance_to_next_first_main(
        probe, probe.state, growth, "sick-bear-root");
    expect(
        held.complete && spent.complete &&
            !held.unresolved_transient_choice_effect &&
            !spent.unresolved_transient_choice_effect,
        "second-main sick-Bear transition did not complete cleanly");
    expect(
        dominance::compare(held, spent, probe.root_player)
                .orientation ==
            dominance::Orientation::FirstDominatesSecond,
        "neutral next-boundary path did not prove Pass over sick Growth");
}

void test_begin_combat_growth_fails_closed() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto& probe = find_probe(
        corpus,
        "field.green.begin-combat-growth-tapped-air.v1");
    const std::size_t pass =
        candidate_index(probe, "pass");
    const std::size_t growth =
        candidate_index(
            probe, "growth-own-ironroot-treefolk");
    const auto held = transition::advance_to_next_first_main(
        probe, probe.state, pass, "combat-root");
    const auto pumped = transition::advance_to_next_first_main(
        probe, probe.state, growth, "combat-root");
    expect(
        !held.unresolved_transient_choice_effect &&
            pumped.unresolved_transient_choice_effect,
        "begin-combat temporary effect was not detected before no attacks");
    expect(
        dominance::compare(held, pumped, probe.root_player)
                .orientation ==
            dominance::Orientation::Incomparable,
        "neutral no-attack path erased combat option value");
}

probes::DecisionProbe cleanup_probe() {
    probes::DecisionProbe probe{
        .stable_id = "test.cleanup-order",
        .category = probes::Category::GreenDevelop,
        .decision_kind = probes::DecisionKind::Priority,
        .root_deck = old_school::DeckId::Green,
        .opponent_deck = old_school::DeckId::Red,
        .root_player = 0,
        .phase = old_school::TurnPhase::SecondMain,
        .consecutive_passes = 1,
    };
    probe.state.active_player = 0;
    probe.state.starting_player = 0;
    probe.state.turn_number = 5;
    probe.state.players[0].hand = {
        old_school::CardId::HillGiant,
        old_school::CardId::Forest,
        old_school::CardId::LightningBolt,
        old_school::CardId::Forest,
        old_school::CardId::GrizzlyBears,
        old_school::CardId::Mountain,
        old_school::CardId::GiantGrowth,
        old_school::CardId::Island,
        old_school::CardId::Plains,
    };
    probe.state.players[1].library = {
        old_school::CardId::Mountain};
    probe.original_decks = {
        probe.state.players[0].hand,
        probe.state.players[1].library,
    };
    probe.candidates = {{
        .descriptor = "pass",
        .action = old_school::PriorityAction::pass(),
    }};
    return probe;
}

void test_cleanup_order_and_next_turn_draw_are_exact() {
    const auto probe = cleanup_probe();
    const auto result = transition::advance_to_next_first_main(
        probe, probe.state, 0, "cleanup-root");
    expect(
        result.complete && !result.terminal &&
            result.boundary_context.valid &&
            result.boundary_context.phase ==
                old_school::TurnPhase::FirstMain &&
            result.boundary_state.active_player == 1 &&
            result.boundary_state.turn_number == 6 &&
            result.boundary_state.players[0].hand.size() == 7 &&
            result.boundary_state.players[0].graveyard ==
                std::vector<old_school::CardId>({
                    old_school::CardId::Forest,
                    old_school::CardId::Forest}) &&
            result.boundary_state.players[1].hand ==
                std::vector<old_school::CardId>{
                    old_school::CardId::Mountain} &&
            result.boundary_state.stats[1].cards_drawn == 1,
        "canonical cleanup or next-turn preparation drifted");
}

void test_hidden_repartition_changes_no_owner_consequence() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto& probe = find_probe(
        corpus,
        "field.green.second-main-sick-bear-growth.v1");
    const auto hidden =
        old_school::probe_runner::hidden_repartition_clone(
            probe);
    const std::size_t pass =
        candidate_index(probe, "pass");
    const auto original =
        transition::advance_to_next_first_main(
            probe, probe.state, pass, "hidden-root");
    const auto clone =
        transition::advance_to_next_first_main(
            probe, hidden, pass, "hidden-root");
    const auto original_canonical =
        dominance::canonicalize_settlement(
            original, probe.root_player);
    const auto clone_canonical =
        dominance::canonicalize_settlement(
            clone, probe.root_player);
    expect(
        original_canonical.valid && clone_canonical.valid &&
            original_canonical == clone_canonical,
        "neutral dominance path leaked a hidden repartition");
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
    runner.run(
        "sick-Bear next-boundary dominance",
        test_sick_bear_pass_strictly_dominates_growth);
    runner.run(
        "begin-combat transient guard",
        test_begin_combat_growth_fails_closed);
    runner.run(
        "canonical cleanup and draw",
        test_cleanup_order_and_next_turn_draw_are_exact);
    runner.run(
        "hidden-repartition consequence invariance",
        test_hidden_repartition_changes_no_owner_consequence);
    return runner.finish();
}
