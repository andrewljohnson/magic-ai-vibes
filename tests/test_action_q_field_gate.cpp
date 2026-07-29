#include "old_school/action_q_field_gate.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

namespace gate = old_school::action_q_field_gate;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::size_t card_count(
    const std::vector<old_school::CardId>& cards,
    old_school::CardId wanted) {
    return static_cast<std::size_t>(
        std::count(cards.begin(), cards.end(), wanted));
}

void test_captured_root_has_exact_owner_visible_state() {
    const gate::AncestralFieldRoot root =
        gate::make_ancestral_field_root();
    const auto observation =
        old_school::observe_game_state(root.state, root.actor);

    expect(
        gate::kStableId ==
                "field.blue.ancestral-opponent-seed24.aq0.v1" &&
            root.actor == 0 &&
            root.context.valid &&
            root.context.phase ==
                old_school::TurnPhase::FirstMain &&
            root.context.decision_player == 0 &&
            root.context.consecutive_passes == 0 &&
            root.context.sorcery_actions &&
            root.state.active_player == 0 &&
            root.state.starting_player == 0 &&
            root.state.turn_number == gate::kCaptureTurn,
        "captured Ancestral root context drifted");
    expect(
        root.state.players[0].life == 15 &&
            root.state.players[1].life == 4 &&
            root.state.players[0].library.size() == 27 &&
            root.state.players[1].library.size() == 27 &&
            root.state.players[0].hand ==
                std::vector<old_school::CardId>{
                    old_school::CardId::Island,
                    old_school::CardId::SolRing,
                    old_school::CardId::AncestralRecall,
                } &&
            observation.players[1].hand_size == 5 &&
            !observation.revealed_opponent_hand.has_value(),
        "captured Ancestral root zones drifted");
    expect(
        root.state.players[0].lands.size() == 4 &&
            root.state.players[0].artifacts.size() == 1 &&
            root.state.players[0].artifacts.front().card ==
                old_school::CardId::MoxSapphire &&
            root.state.players[0].creatures.size() == 2 &&
            root.state.players[1].lands.size() == 3 &&
            root.state.players[1].creatures.size() == 1 &&
            root.state.players[1].creatures.front().card ==
                old_school::CardId::IronclawOrcs,
        "captured Ancestral root battlefield drifted");
    expect(
        card_count(
            root.state.players[0].graveyard,
            old_school::CardId::ForceSpike) == 2 &&
            card_count(
                root.state.players[0].graveyard,
                old_school::CardId::Counterspell) == 1 &&
            card_count(
                root.state.players[1].graveyard,
                old_school::CardId::LightningBolt) == 4,
        "captured Ancestral root graveyards drifted");
}

void test_complete_legal_actions_contain_both_targets() {
    const gate::AncestralFieldRoot root =
        gate::make_ancestral_field_root();
    const auto engine_actions =
        old_school::legal_priority_actions(
            root.state, root.actor,
            root.context.sorcery_actions);

    expect(
        engine_actions == root.legal_actions &&
            root.legal_actions.size() == 5 &&
            gate::has_required_action_identities(root),
        "captured Ancestral root lost its complete legal set");
    expect(
        root.legal_actions[root.pass_index] ==
                old_school::PriorityAction::pass() &&
            root.legal_actions[root.self_target_index] ==
                old_school::PriorityAction::
                    cast_ancestral_recall(
                        old_school::Target::player_target(0)) &&
            root.legal_actions[root.opponent_target_index] ==
                old_school::PriorityAction::
                    cast_ancestral_recall(
                        old_school::Target::player_target(1)),
        "captured Ancestral action indices drifted");
}

void test_fixture_is_deterministic_and_hidden_safe() {
    const gate::AncestralFieldRoot first =
        gate::make_ancestral_field_root();
    const gate::AncestralFieldRoot second =
        gate::make_ancestral_field_root();
    const gate::AncestralFieldRoot hidden =
        gate::hidden_repartition_clone(first);

    expect(first == second,
           "captured Ancestral root is nondeterministic");
    expect(
        hidden.state != first.state &&
            hidden.legal_actions == first.legal_actions &&
            gate::has_required_action_identities(hidden),
        "hidden clone is not physically distinct and action-identical");
    expect(
        old_school::observe_game_state(first.state, first.actor) ==
            old_school::observe_game_state(
                hidden.state, hidden.actor),
        "hidden clone changed the actor's observation");

    const auto first_world =
        old_school::sample_determinization(
            first.state, first.original_decks, first.actor,
            gate::kReferenceSearchSeed);
    const auto hidden_world =
        old_school::sample_determinization(
            hidden.state, hidden.original_decks, hidden.actor,
            gate::kReferenceSearchSeed);
    expect(
        first_world == hidden_world,
        "hidden clone changed same-seed information-set sampling");
}

} // namespace

int main() {
    try {
        test_captured_root_has_exact_owner_visible_state();
        test_complete_legal_actions_contain_both_targets();
        test_fixture_is_deterministic_and_hidden_safe();
        std::cout
            << "action-Q Ancestral field-gate tests: "
            << "3/3 passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "action-Q Ancestral field-gate test failed: "
            << error.what() << '\n';
        return 1;
    }
}
