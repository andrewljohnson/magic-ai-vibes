#include "old_school/game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

old_school::CreaturePermanent creature(
    old_school::PermanentId id, old_school::CardId card,
    bool tapped = false) {
    return {
        .id = id,
        .card = card,
        .tapped = tapped,
        .summoning_sick = false,
    };
}

void remove_one(
    std::vector<old_school::CardId>& cards,
    old_school::CardId card) {
    const auto found =
        std::find(cards.begin(), cards.end(), card);
    if (found == cards.end()) {
        throw std::runtime_error(
            "fixture exceeds its original deck");
    }
    cards.erase(found);
}

void finish_hidden(
    old_school::GameState& state,
    const std::array<std::vector<old_school::CardId>, 2>&
        decks) {
    for (std::size_t player = 0; player < 2; ++player) {
        auto remaining = decks[player];
        const auto& source = state.players[player];
        for (const auto card : source.hand) {
            remove_one(remaining, card);
        }
        for (const auto card : source.graveyard) {
            remove_one(remaining, card);
        }
        for (const auto card : source.exile) {
            remove_one(remaining, card);
        }
        for (const auto& land : source.lands) {
            remove_one(remaining, land.card);
        }
        for (const auto& permanent : source.creatures) {
            remove_one(remaining, permanent.card);
        }
        for (const auto& permanent : source.artifacts) {
            remove_one(remaining, permanent.card);
        }
        for (const auto card : source.enchantments) {
            remove_one(remaining, card);
        }
        for (const auto& object : state.stack) {
            if (object.controller == player &&
                object.kind ==
                    old_school::StackObjectKind::Spell) {
                remove_one(remaining, object.card);
            }
        }
        state.players[player].library =
            std::move(remaining);
    }
}

std::shared_ptr<const old_school::LearnedModel> model() {
    static const auto value =
        old_school::train_learned_value_champion(
            1, 0xA0715EAULL);
    return value;
}

std::string key_for_kind(
    const old_school::LearnedGenerativeObservation& observation,
    old_school::PriorityActionKind kind) {
    for (const auto& candidate : observation.actions) {
        const auto* action =
            std::get_if<old_school::PriorityAction>(
                &candidate.action.payload);
        if (action != nullptr && action->kind == kind) {
            return candidate.action.stable_key;
        }
    }
    throw std::runtime_error(
        "fixture is missing its Priority action");
}

void test_priority_observation_and_hidden_safety() {
    const std::array<std::vector<old_school::CardId>, 2>
        decks = {
            old_school::blue_deck(),
            old_school::blue_deck(),
        };
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 3;
    state.players[0].hand = {
        old_school::CardId::Island,
        old_school::CardId::FlyingMen,
    };
    state.players[1].hand = {
        old_school::CardId::Island,
        old_school::CardId::Counterspell,
    };
    finish_hidden(state, decks);
    const auto position =
        old_school::make_learned_generative_priority_position(
            state, decks, 0,
            {
                .valid = true,
                .phase = old_school::TurnPhase::FirstMain,
                .decision_player = 0,
                .consecutive_passes = 0,
                .sorcery_actions = true,
            });
    const auto original = position.truth;
    const auto sampled =
        old_school::learned_generative_actor_determinization(
            position, 71);
    expect(
        position.truth == original &&
            sampled.players[0].hand ==
                original.players[0].hand &&
            old_school::observe_game_state(sampled, 0) ==
                old_school::observe_game_state(original, 0),
        "actor-local determinization changed truth or known state");

    const auto observation =
        old_school::observe_learned_generative_position(
            position, model(), 91);
    const auto explicit_exact_observation =
        old_school::observe_learned_generative_position(
            position, model(), 91,
            old_school::LearnedTerminalUtilityMode::
                ExactOutcome);
    const double prior_sum = std::accumulate(
        observation.actions.begin(),
        observation.actions.end(), 0.0,
        [](double total, const auto& action) {
            return total + action.prior;
        });
    const double maximum = std::max_element(
        observation.actions.begin(),
        observation.actions.end(),
        [](const auto& left, const auto& right) {
            return left.successor_value <
                   right.successor_value;
        })->successor_value;
    double expected_sum = 0.0;
    for (const auto& action : observation.actions) {
        expected_sum +=
            1.0e-6 +
            std::exp(action.successor_value - maximum);
    }
    expect(
        explicit_exact_observation == observation &&
            observation.actor == 0 &&
            !observation.information_set_key.empty() &&
            !observation.legal_signature.empty() &&
            std::abs(prior_sum - 1.0) < 1.0e-12 &&
            std::all_of(
                observation.actions.begin(),
                observation.actions.end(),
                [&](const auto& action) {
                    const double expected =
                        (1.0e-6 +
                         std::exp(
                             action.successor_value -
                             maximum)) /
                        expected_sum;
                    return action.prior > 0.0 &&
                           std::abs(
                               action.prior - expected) <
                               1.0e-14;
                }),
        "generative prior formula or normalization drifted");

    auto hidden = state;
    const auto different = std::find_if(
        hidden.players[1].library.begin(),
        hidden.players[1].library.end(),
        [&](old_school::CardId card) {
            return card != hidden.players[1].hand[0];
        });
    expect(
        different != hidden.players[1].library.end(),
        "hidden fixture is vacuous");
    std::swap(
        hidden.players[1].hand[0], *different);
    auto hidden_position =
        old_school::make_learned_generative_priority_position(
            hidden, decks, 0,
            std::get<
                old_school::LearnedGenerativePriorityDecision>(
                position.decision)
                .context);
    hidden_position.truth.stats[0].decisions = 999;
    hidden_position.truth.stats[1].monte_carlo_rollouts =
        999;
    const auto hidden_observation =
        old_school::observe_learned_generative_position(
            hidden_position, model(), 91);
    expect(
        hidden_observation == observation,
        "opponent repartition or telemetry entered the root "
        "observation");
}

void test_terminal_utility_modes_and_legacy_identity() {
    for (const std::size_t turn :
         {std::size_t{1}, std::size_t{20},
          std::size_t{500}}) {
        const old_school::GameResult outcome{
            .winner = 0,
            .reason = old_school::EndReason::LifeTotal,
            .turns = turn,
        };
        const double win =
            old_school::learned_generative_terminal_utility(
                outcome, 0,
                old_school::LearnedTerminalUtilityMode::
                    C16DiscountedAbsoluteTurn);
        const double loss =
            old_school::learned_generative_terminal_utility(
                outcome, 1,
                old_school::LearnedTerminalUtilityMode::
                    C16DiscountedAbsoluteTurn);
        const double expected =
            0.5 + 0.5 * std::pow(0.985, turn);
        expect(
            std::abs(win - expected) < 1.0e-15 &&
                win + loss == 1.0,
            "C16 terminal win/loss did not complement at "
            "the registered absolute turn");
    }

    old_school::GameResult decisive{
        .winner = 0,
        .reason = old_school::EndReason::LifeTotal,
        .turns = 10,
    };
    const double exact_zero =
        old_school::learned_generative_terminal_utility(
            decisive, 0,
            old_school::LearnedTerminalUtilityMode::
                ExactOutcome);
    const double exact_one =
        old_school::learned_generative_terminal_utility(
            decisive, 1,
            old_school::LearnedTerminalUtilityMode::
                ExactOutcome);
    const double soft_zero =
        old_school::learned_generative_terminal_utility(
            decisive, 0,
            old_school::LearnedTerminalUtilityMode::
                C16DiscountedAbsoluteTurn);
    const double soft_one =
        old_school::learned_generative_terminal_utility(
            decisive, 1,
            old_school::LearnedTerminalUtilityMode::
                C16DiscountedAbsoluteTurn);
    const double expected_soft_zero =
        0.5 +
        0.5 * std::pow(0.985, decisive.turns);
    expect(
        exact_zero == 1.0 && exact_one == 0.0 &&
            std::abs(soft_zero - expected_soft_zero) <
                1.0e-15 &&
            soft_zero + soft_one == 1.0,
        "terminal utility modes lost exact outcome, absolute "
        "turn discount, or player complement");

    old_school::GameResult draw{
        .winner = -1,
        .reason = old_school::EndReason::TurnLimit,
    };
    for (const std::size_t turn :
         {std::size_t{1}, std::size_t{20},
          std::size_t{500}}) {
        draw.turns = turn;
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            expect(
                old_school::
                        learned_generative_terminal_utility(
                            draw, perspective,
                            old_school::
                                LearnedTerminalUtilityMode::
                                    C16DiscountedAbsoluteTurn) ==
                    0.5,
                "draw utility changed at a registered "
                "absolute turn or perspective");
        }
    }

    bool invalid_mode_rejected = false;
    try {
        static_cast<void>(
            old_school::learned_generative_terminal_utility(
                decisive, 0,
                static_cast<
                    old_school::LearnedTerminalUtilityMode>(
                    255)));
    } catch (const std::invalid_argument&) {
        invalid_mode_rejected = true;
    }
    expect(
        invalid_mode_rejected,
        "invalid terminal utility mode was accepted");

    const std::array<std::vector<old_school::CardId>, 2>
        decks = {
            old_school::red_deck(),
            old_school::red_deck(),
        };
    old_school::GameState terminal_state;
    terminal_state.turn_number = decisive.turns;
    terminal_state.players[1].life = 0;
    const old_school::LearnedGenerativePosition
        terminal_position{
            .truth = terminal_state,
            .original_decks = decks,
            .root_observer = 0,
            .decision =
                old_school::LearnedGenerativePriorityDecision{
                    .context = {
                        .valid = true,
                        .phase =
                            old_school::TurnPhase::FirstMain,
                        .decision_player = 0,
                        .consecutive_passes = 0,
                        .sorcery_actions = true,
                    },
                },
        };
    const auto legacy_leaf =
        old_school::evaluate_learned_generative_leaf(
            terminal_position, 0, model(), 119);
    const auto explicit_exact_leaf =
        old_school::evaluate_learned_generative_leaf(
            terminal_position, 0, model(), 119,
            old_school::LearnedTerminalUtilityMode::
                ExactOutcome);
    const auto soft_leaf =
        old_school::evaluate_learned_generative_leaf(
            terminal_position, 0, model(), 119,
            old_school::LearnedTerminalUtilityMode::
                C16DiscountedAbsoluteTurn);
    expect(
        legacy_leaf == explicit_exact_leaf &&
            legacy_leaf.terminal &&
            legacy_leaf.value == 1.0 &&
            soft_leaf.terminal &&
            soft_leaf.value == soft_zero,
        "legacy terminal leaf changed or treatment failed "
        "to use C16 utility");

    old_school::GameState pending = terminal_state;
    pending.players[1].life = 3;
    pending.stack = {{
        .kind = old_school::StackObjectKind::Spell,
        .id = 1,
        .card = old_school::CardId::LightningBolt,
        .controller = 0,
        .target =
            old_school::Target::player_target(1),
    }};
    pending.next_stack_object_id = 2;
    finish_hidden(pending, decks);
    const auto pending_position =
        old_school::make_learned_generative_priority_position(
            pending, decks, 0,
            {
                .valid = true,
                .phase = old_school::TurnPhase::FirstMain,
                .decision_player = 0,
                .consecutive_passes = 1,
                .sorcery_actions = true,
            });
    const auto legacy_observation =
        old_school::observe_learned_generative_position(
            pending_position, model(), 120);
    const auto exact_observation =
        old_school::observe_learned_generative_position(
            pending_position, model(), 120,
            old_school::LearnedTerminalUtilityMode::
                ExactOutcome);
    const auto soft_observation =
        old_school::observe_learned_generative_position(
            pending_position, model(), 120,
            old_school::LearnedTerminalUtilityMode::
                C16DiscountedAbsoluteTurn);
    expect(
        legacy_observation == exact_observation &&
            legacy_observation.actions.size() == 1 &&
            legacy_observation.actions.front()
                    .successor_value ==
                1.0 &&
            soft_observation.actions.size() == 1 &&
            soft_observation.actions.front()
                    .successor_value ==
                soft_zero,
        "immediate-terminal prior did not preserve legacy "
        "identity or align with C16 utility");
}

void test_priority_and_counter_transition() {
    const std::array<std::vector<old_school::CardId>, 2>
        decks = {
            old_school::blue_deck(),
            old_school::blue_deck(),
        };
    old_school::GameState state;
    state.active_player = 1;
    state.starting_player = 1;
    state.turn_number = 8;
    state.players[0].hand = {
        old_school::CardId::Counterspell,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    state.players[1].hand = {
        old_school::CardId::Counterspell,
    };
    state.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    state.stack = {{
        .kind = old_school::StackObjectKind::Spell,
        .id = 1,
        .card = old_school::CardId::AirElemental,
        .controller = 1,
    }};
    state.next_stack_object_id = 2;
    finish_hidden(state, decks);
    const auto position =
        old_school::make_learned_generative_priority_position(
            state, decks, 0,
            {
                .valid = true,
                .phase = old_school::TurnPhase::SecondMain,
                .decision_player = 0,
                .consecutive_passes = 0,
                .sorcery_actions = true,
            });
    const auto observation =
        old_school::observe_learned_generative_position(
            position, model(), 101);
    const std::string counter_key = key_for_kind(
        observation,
        old_school::PriorityActionKind::CastCounterspell);
    const auto one_edge =
        old_school::advance_learned_generative_position(
            position, counter_key, model(), 102, false);
    expect(
        one_edge.disposition ==
                old_school::LearnedGenerativeDisposition::
                    DecisionBoundary &&
            one_edge.position.has_value() &&
            old_school::learned_generative_actor(
                *one_edge.position) == 1 &&
            one_edge.position->truth.stack.size() == 2 &&
            one_edge.witness.applied_priority_action ==
                std::optional<old_school::PriorityAction>(
                    old_school::PriorityAction::
                        cast_counterspell(1)),
        "Counter transition did not preserve stack progression");

    const auto hidden_opponent =
        old_school::advance_learned_generative_position(
            position, counter_key, model(), 103, true);
    expect(
        hidden_opponent.witness.applied_priority_action ==
            one_edge.witness.applied_priority_action &&
            hidden_opponent.witness
                    .opponent_decisions_applied ==
                hidden_opponent.witness
                    .opponent_decisions.size() &&
            hidden_opponent.witness
                    .opponent_decisions_applied <=
                hidden_opponent.actions_applied &&
            (hidden_opponent.disposition !=
                 old_school::LearnedGenerativeDisposition::
                     DecisionBoundary ||
             (hidden_opponent.position.has_value() &&
              old_school::learned_generative_actor(
                  *hidden_opponent.position) == 0)),
        "opponent rollout overwrote the root Counter or exposed "
        "an opponent node");
}

void test_attack_block_exact_completion_and_bound() {
    const std::array<std::vector<old_school::CardId>, 2>
        decks = {
            old_school::ru_aggro_deck(),
            old_school::blue_deck(),
        };
    old_school::GameState attack_state;
    attack_state.active_player = 0;
    attack_state.turn_number = 6;
    attack_state.players[0].hand = {
        old_school::CardId::LightningBolt,
    };
    attack_state.players[0].lands = {
        {.card = old_school::CardId::Mountain},
    };
    attack_state.players[0].creatures = {
        creature(1, old_school::CardId::FlyingMen),
    };
    finish_hidden(attack_state, decks);
    const auto attack =
        old_school::make_learned_generative_attack_position(
            attack_state, decks, 0, 0, {}, 1, {});
    const auto attack_observation =
        old_school::observe_learned_generative_position(
            attack, model(), 111);
    const auto skip = std::find_if(
        attack_observation.actions.begin(),
        attack_observation.actions.end(),
        [](const auto& action) {
            const auto* attack_action =
                std::get_if<
                    old_school::LearnedGenerativeAttackAction>(
                    &action.action.payload);
            return attack_action != nullptr &&
                   !attack_action->include;
        });
    const auto skipped =
        old_school::advance_learned_generative_position(
            attack, skip->action.stable_key,
            model(), 112, false);
    expect(
        skipped.witness.exact_combat_completed &&
            skipped.witness
                    .exact_combat_completed_plan_count ==
                1 &&
            !skipped.witness.applied_priority_action
                 .has_value(),
        "empty attack is not a nonvacuous exact completion");

    old_school::GameState block_state;
    const std::array<std::vector<old_school::CardId>, 2>
        block_decks = {
            old_school::blue_deck(),
            old_school::blue_deck(),
        };
    block_state.active_player = 1;
    block_state.turn_number = 7;
    block_state.players[0].creatures = {
        creature(1, old_school::CardId::FlyingMen),
        creature(3, old_school::CardId::AirElemental),
    };
    block_state.players[1].creatures = {
        creature(
            2, old_school::CardId::AirElemental, true),
    };
    finish_hidden(block_state, block_decks);
    const auto block =
        old_school::make_learned_generative_block_position(
            block_state, block_decks, 0, 1, {2}, {{2, 1}}, 3,
            {});
    const auto leaf =
        old_school::evaluate_learned_generative_leaf(
            block, 0, model(), 113);
    expect(
        leaf.exact_combat_completed &&
            leaf.exact_combat_completed_plan_count > 0 &&
            std::find(
                leaf.completed_damage_ordered_blocks.begin(),
                leaf.completed_damage_ordered_blocks.end(),
                std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>{2, 1}) !=
                leaf.completed_damage_ordered_blocks.end(),
        "exact leaf discarded the fixed block prefix");

    // The Blue ISP0 root first declines the Flying Men block. The resulting
    // partial position must still be completed by the exact leaf evaluator,
    // and its completed cutoff must use the Air Elemental trade rather than
    // inventing the declined chump.
    const auto blue_root =
        old_school::make_learned_generative_block_position(
            block_state, block_decks, 0, 1, {2}, {}, 1,
            {3});
    const auto declined =
        old_school::advance_learned_generative_position(
            blue_root, "block.subject-1.none",
            model(), 117, false);
    expect(
        declined.disposition ==
                old_school::LearnedGenerativeDisposition::
                    DecisionBoundary &&
            declined.position.has_value(),
        "Blue no-block edge did not reach its partial combat "
        "cutoff");
    const auto blue_cutoff =
        old_school::evaluate_learned_generative_leaf(
            *declined.position, 0, model(), 118);
    expect(
        blue_cutoff.exact_combat_completed &&
            blue_cutoff.exact_combat_completed_plan_count >
                0 &&
            !blue_cutoff.exact_combat_contains_pure_chump &&
            std::find(
                blue_cutoff.completed_damage_ordered_blocks
                    .begin(),
                blue_cutoff.completed_damage_ordered_blocks
                    .end(),
                std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>{2, 3}) !=
                blue_cutoff.completed_damage_ordered_blocks
                    .end() &&
            std::find(
                blue_cutoff.completed_damage_ordered_blocks
                    .begin(),
                blue_cutoff.completed_damage_ordered_blocks
                    .end(),
                std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>{2, 1}) ==
                blue_cutoff.completed_damage_ordered_blocks
                    .end(),
        "Blue exact cutoff did not preserve the declined "
        "Flying Men block and complete the Air Elemental "
        "trade");

    const std::array<std::vector<old_school::CardId>, 2>
        red_decks = {
            old_school::red_deck(),
            old_school::red_deck(),
        };
    old_school::GameState bound_state;
    bound_state.active_player = 0;
    bound_state.turn_number = 8;
    const std::array<old_school::CardId, 9> cards = {
        old_school::CardId::IronclawOrcs,
        old_school::CardId::IronclawOrcs,
        old_school::CardId::IronclawOrcs,
        old_school::CardId::IronclawOrcs,
        old_school::CardId::IronclawOrcs,
        old_school::CardId::IronclawOrcs,
        old_school::CardId::IronclawOrcs,
        old_school::CardId::GrayOgre,
        old_school::CardId::GrayOgre,
    };
    for (std::size_t index = 0; index < cards.size();
         ++index) {
        bound_state.players[0].creatures.push_back(
            creature(index + 1, cards[index]));
    }
    finish_hidden(bound_state, red_decks);
    const std::vector<old_school::PermanentId> prefix = {
        1, 2, 3, 4, 5, 6, 7, 8,
    };
    const auto bound_position =
        old_school::make_learned_generative_attack_position(
            bound_state, red_decks, 0, 0, prefix, 9, {});
    bool rejected_prior = false;
    try {
        static_cast<void>(
            old_school::observe_learned_generative_position(
                bound_position, model(), 114));
    } catch (const std::length_error&) {
        rejected_prior = true;
    }
    const auto bounded =
        old_school::advance_learned_generative_position(
            bound_position,
            "attack.subject-9.include",
            model(), 115, false);
    expect(
        rejected_prior &&
            bounded.disposition ==
                old_school::LearnedGenerativeDisposition::
                    Bound &&
            bounded.exhausted_bound ==
                old_school::LearnedGenerativeBound::
                    ExactCombat,
        "exact-combat bound did not fail closed");
}

void test_hidden_opponent_full_block_accounting() {
    const std::array<std::vector<old_school::CardId>, 2>
        decks = {
            old_school::red_deck(),
            old_school::red_deck(),
        };
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 6;
    state.players[0].hand = {
        old_school::CardId::LightningBolt,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain},
    };
    state.players[0].creatures = {
        creature(1, old_school::CardId::HillGiant),
    };
    state.players[1].creatures = {
        creature(2, old_school::CardId::GrayOgre),
        creature(3, old_school::CardId::GrayOgre),
    };
    finish_hidden(state, decks);

    const auto attack =
        old_school::make_learned_generative_attack_position(
            state, decks, 0, 0, {}, 1, {});
    const auto transition =
        old_school::advance_learned_generative_position(
            attack, "attack.subject-1.include",
            model(), 116, true);

    expect(
        transition.disposition ==
                old_school::LearnedGenerativeDisposition::
                    DecisionBoundary &&
            transition.position.has_value() &&
            old_school::learned_generative_actor(
                *transition.position) == 0 &&
            transition.witness.opponent_decisions.size() == 1,
        "opponent full Block did not return a hidden root "
        "boundary");
    const auto& block =
        transition.witness.opponent_decisions.front();
    expect(
        block.kind ==
                old_school::LearnedGenerativeDecisionKind::
                    Block &&
            block.selected_stable_key.find(".1-2") !=
                std::string::npos &&
            block.selected_stable_key.find(".1-3") !=
                std::string::npos,
        "opponent fixture did not select the intended "
        "multi-block");
    expect(
        transition.actions_applied == 3 &&
            transition.phase_transitions == 3 &&
            transition.witness.opponent_decisions_applied ==
                transition.witness.opponent_decisions.size() &&
            transition.witness.opponent_decisions_applied <=
                transition.actions_applied,
        "opponent full Block omitted its declaration or "
        "damage-order accounting");
    expect(
        block.accounting_through_decision ==
            old_school::LearnedGenerativeDecisionAccounting{
                .actions_applied = 2,
                .phase_transitions = 1,
                .turn_advances = 0,
                .opponent_decisions_applied = 1,
            },
        "opponent full Block did not snapshot accounting at "
        "its decision boundary");

    auto later_continuation = transition;
    later_continuation.actions_applied += 9;
    later_continuation.phase_transitions += 4;
    later_continuation.turn_advances += 1;
    expect(
        later_continuation.witness.opponent_decisions.front()
                .accounting_through_decision ==
            block.accounting_through_decision,
        "later root-private continuation mutated the witnessed "
        "opponent accounting");
}

} // namespace

int main() {
    try {
        test_priority_observation_and_hidden_safety();
        std::cout << "priority observation: pass\n";
        test_terminal_utility_modes_and_legacy_identity();
        std::cout << "terminal utility modes: pass\n";
        test_priority_and_counter_transition();
        std::cout << "counter transition: pass\n";
        test_attack_block_exact_completion_and_bound();
        std::cout << "combat completion/bound: pass\n";
        test_hidden_opponent_full_block_accounting();
        std::cout << "opponent full Block accounting: pass\n";
        std::cout << "5 learned generative groups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "learned generative test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
