#include "old_school/exact_combat_subgame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace combat = old_school::exact_combat_subgame;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
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

bool has_creature(
    const old_school::GameState& state, std::size_t player,
    old_school::PermanentId permanent) {
    const auto& creatures = state.players[player].creatures;
    return std::any_of(
        creatures.begin(), creatures.end(),
        [permanent](
            const old_school::CreaturePermanent& candidate) {
            return candidate.id == permanent;
        });
}

old_school::GameState single_chump_state() {
    old_school::GameState state;
    state.active_player = 1;
    state.turn_number = 10;
    state.next_permanent_id = 3;
    state.players[0].creatures = {
        creature(
            1, old_school::CardId::FlyingMen),
    };
    state.players[1].creatures = {
        creature(
            2, old_school::CardId::AirElemental, true),
    };
    return state;
}

old_school::GameState multi_block_state() {
    old_school::GameState state = single_chump_state();
    state.next_permanent_id = 4;
    state.players[0].creatures.push_back(
        creature(3, old_school::CardId::AirElemental));
    return state;
}

old_school::GameState lethal_attack_state() {
    old_school::GameState state;
    state.active_player = 1;
    state.turn_number = 10;
    state.next_permanent_id = 2;
    state.players[0].life = 4;
    state.players[1].creatures = {
        creature(1, old_school::CardId::AirElemental),
    };
    return state;
}

old_school::GameState blocker_bound_state() {
    old_school::GameState state;
    state.active_player = 1;
    state.turn_number = 10;
    state.next_permanent_id = 11;
    for (old_school::PermanentId id = 1; id <= 9; ++id) {
        state.players[0].creatures.push_back(
            creature(id, old_school::CardId::FlyingMen));
    }
    state.players[1].creatures = {
        creature(10, old_school::CardId::AirElemental),
    };
    return state;
}

combat::Bounds single_bounds() {
    return {
        .maximum_attackers = 1,
        .maximum_blockers = 1,
        .maximum_block_assignments = 2,
        .maximum_damage_orders_per_assignment = 1,
        .maximum_completed_plans = 2,
    };
}

combat::Bounds multi_bounds() {
    return {
        .maximum_attackers = 1,
        .maximum_blockers = 2,
        .maximum_block_assignments = 4,
        .maximum_damage_orders_per_assignment = 2,
        .maximum_completed_plans = 5,
    };
}

void test_single_block_and_pure_chump() {
    const old_school::GameState original =
        single_chump_state();
    const combat::Enumeration result =
        combat::enumerate(
            original, 1, {2}, single_bounds());

    expect(
        result.attacking_player == 1 &&
            result.attackers ==
                std::vector<old_school::PermanentId>{2} &&
            result.blocker_options.size() == 1 &&
            result.blocker_options[0] ==
                combat::BlockerOptions{
                    .blocker = 1,
                    .legal_attackers = {2},
                } &&
            result.legal_block_assignments == 2 &&
            result.plans.size() == 2,
        "single-block exact census drifted");

    const combat::CompletedPlan& no_block =
        result.plans[0];
    expect(
        no_block.block_assignment_index == 0 &&
            no_block.damage_order_index == 0 &&
            no_block.declared_blocks.empty() &&
            no_block.damage_ordered_blocks.empty() &&
            !no_block.contains_pure_chump() &&
            no_block.resulting_state.players[0].life == 16 &&
            has_creature(no_block.resulting_state, 0, 1) &&
            has_creature(no_block.resulting_state, 1, 2),
        "single-block no-block plan is wrong");

    const combat::CompletedPlan& chump =
        result.plans[1];
    expect(
        chump.block_assignment_index == 1 &&
            chump.damage_order_index == 0 &&
            chump.declared_blocks ==
                std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>>{{2, 1}} &&
            chump.damage_ordered_blocks ==
                chump.declared_blocks &&
            chump.pure_chump_blockers ==
                std::vector<old_school::PermanentId>{1} &&
            chump.resulting_state.players[0].life == 20 &&
            !has_creature(chump.resulting_state, 0, 1) &&
            has_creature(chump.resulting_state, 1, 2),
        "single-block pure chump was not identified");

    expect(
        original == single_chump_state(),
        "exact combat enumeration mutated its source state");

    old_school::GameState pre_attack = original;
    pre_attack.players[1].creatures[0].tapped = false;
    expect(
        combat::enumerate(
            pre_attack, 1, {2}, single_bounds()) ==
            result,
        "pre-attack and Declare Blockers snapshots diverged");
}

void test_multi_block_orders_and_selection() {
    const combat::Enumeration result =
        combat::enumerate(
            multi_block_state(), 1, {2}, multi_bounds());
    expect(
        result.blocker_options ==
                std::vector<combat::BlockerOptions>{
                    {
                        .blocker = 1,
                        .legal_attackers = {2},
                    },
                    {
                        .blocker = 3,
                        .legal_attackers = {2},
                    },
                } &&
            result.legal_block_assignments == 4 &&
            result.plans.size() == 5,
        "multi-block exact census drifted");

    expect(
        result.plans[0].declared_blocks.empty() &&
            result.plans[1].declared_blocks ==
                std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>>{{2, 3}} &&
            result.plans[2].declared_blocks ==
                std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>>{{2, 1}} &&
            result.plans[2].pure_chump_blockers ==
                std::vector<old_school::PermanentId>{1},
        "multi-block canonical assignment order drifted");

    const combat::CompletedPlan& flying_first =
        result.plans[3];
    expect(
        flying_first.block_assignment_index == 3 &&
            flying_first.damage_order_index == 0 &&
            flying_first.declared_blocks ==
                std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>>{
                    {2, 1}, {2, 3}} &&
            flying_first.damage_ordered_blocks ==
                flying_first.declared_blocks &&
            !flying_first.contains_pure_chump() &&
            !has_creature(
                flying_first.resulting_state, 1, 2) &&
            !has_creature(
                flying_first.resulting_state, 0, 1) &&
            has_creature(
                flying_first.resulting_state, 0, 3),
        "Flying-first double-block outcome is wrong");

    const combat::CompletedPlan& air_first =
        result.plans[4];
    expect(
        air_first.block_assignment_index == 3 &&
            air_first.damage_order_index == 1 &&
            air_first.damage_ordered_blocks ==
                std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>>{
                    {2, 3}, {2, 1}} &&
            !air_first.contains_pure_chump() &&
            !has_creature(
                air_first.resulting_state, 1, 2) &&
            has_creature(
                air_first.resulting_state, 0, 1) &&
            !has_creature(
                air_first.resulting_state, 0, 3),
        "Air-first double-block outcome is wrong");

    const auto score =
        [](const old_school::GameState& state,
           std::size_t perspective) {
            const std::size_t opponent = 1 - perspective;
            const auto power =
                [&state](std::size_t player) {
                    int total = 0;
                    for (const auto& permanent :
                         state.players[player].creatures) {
                        total += old_school::card_definition(
                                     permanent.card)
                                     .power +
                                 permanent
                                     .temporary_power_bonus;
                    }
                    return total;
                };
            return static_cast<double>(
                       power(perspective) -
                       power(opponent)) +
                   0.1 *
                       static_cast<double>(
                           state.players[perspective].life -
                           state.players[opponent].life);
        };
    const combat::Selection selected =
        combat::select_defender_max_after_attacker_max(
            result, score);
    expect(
        selected.attacker_best_responses.size() == 4 &&
            selected.attacker_best_responses[3]
                    .completed_plan_index == 4 &&
            selected.defender_selected_assignment == 1 &&
            selected.selected_response()
                    .block_assignment_index == 1 &&
            selected.selected_response()
                    .completed_plan_index == 1,
        "actor-max combat selection did not choose attacker "
        "Air-first and defender Air-only");

    const combat::Selection attacker_tie =
        combat::select_defender_max_after_attacker_max(
            result,
            [](const old_school::GameState& state,
               std::size_t perspective) {
                if (perspective == 1) {
                    return 0.0;
                }
                return has_creature(state, 0, 1)
                           ? 0.0
                           : 1.0;
            });
    expect(
        attacker_tie.attacker_best_responses[3]
                .completed_plan_index == 3,
        "attacker score tie inspected the defender critic");

    const combat::Selection defender_tie =
        combat::select_defender_max_after_attacker_max(
            result,
            [](const old_school::GameState& state,
               std::size_t perspective) {
                if (perspective == 0) {
                    return 0.0;
                }
                return static_cast<double>(
                    state.players[0].life);
            });
    expect(
        defender_tie.defender_selected_assignment == 0,
        "defender score tie inspected the attacker critic");
}

void test_authoritative_legality_and_bounds() {
    old_school::GameState grounded = single_chump_state();
    grounded.players[0].creatures[0] =
        creature(1, old_school::CardId::GrayOgre);
    combat::Bounds grounded_bounds = single_bounds();
    grounded_bounds.maximum_block_assignments = 1;
    grounded_bounds.maximum_completed_plans = 1;
    const combat::Enumeration cannot_block =
        combat::enumerate(
            grounded, 1, {2}, grounded_bounds);
    expect(
        cannot_block.blocker_options.size() == 1 &&
            cannot_block.blocker_options[0]
                .legal_attackers.empty() &&
            cannot_block.legal_block_assignments == 1 &&
            cannot_block.plans.size() == 1 &&
            cannot_block.plans[0].declared_blocks.empty(),
        "authoritative Flying block legality was not preserved");

    combat::Bounds too_few_assignments = multi_bounds();
    too_few_assignments.maximum_block_assignments = 3;
    expect_rejected(
        [&] {
            static_cast<void>(combat::enumerate(
                multi_block_state(), 1, {2},
                too_few_assignments));
        },
        "exact combat accepted an exhausted assignment bound");

    combat::Bounds too_few_orders = multi_bounds();
    too_few_orders.maximum_damage_orders_per_assignment = 1;
    expect_rejected(
        [&] {
            static_cast<void>(combat::enumerate(
                multi_block_state(), 1, {2},
                too_few_orders));
        },
        "exact combat accepted an exhausted damage-order bound");

    combat::Bounds too_few_plans = multi_bounds();
    too_few_plans.maximum_completed_plans = 4;
    expect_rejected(
        [&] {
            static_cast<void>(combat::enumerate(
                multi_block_state(), 1, {2},
                too_few_plans));
        },
        "exact combat accepted an exhausted completed-plan bound");

    combat::Bounds too_few_blockers = multi_bounds();
    too_few_blockers.maximum_blockers = 1;
    expect_rejected(
        [&] {
            static_cast<void>(combat::enumerate(
                multi_block_state(), 1, {2},
                too_few_blockers));
        },
        "exact combat accepted an exhausted blocker bound");

    expect_rejected(
        [&] {
            static_cast<void>(combat::enumerate(
                single_chump_state(), 1, {2, 2},
                multi_bounds()));
        },
        "exact combat accepted duplicate attackers");

    old_school::GameState sick = single_chump_state();
    sick.players[1].creatures[0].summoning_sick = true;
    expect_rejected(
        [&] {
            static_cast<void>(combat::enumerate(
                sick, 1, {2}, single_bounds()));
        },
        "exact combat accepted a summoning-sick attacker");

    expect_rejected(
        [&] {
            static_cast<void>(
                combat::select_defender_max_after_attacker_max(
                    combat::enumerate(
                        single_chump_state(), 1, {2},
                        single_bounds()),
                    [](const old_school::GameState&,
                       std::size_t) {
                        return std::numeric_limits<
                            double>::quiet_NaN();
                    }));
        },
        "exact combat accepted a nonfinite scorer");
}

void test_flagged_learned_sampler_integration() {
    const auto model =
        old_school::train_learned_value_champion(
            1, 0xA061C0B047ULL);
    const std::array<std::vector<old_school::CardId>, 2>
        decks = {
            std::vector<old_school::CardId>{
                old_school::CardId::FlyingMen,
                old_school::CardId::AirElemental,
            },
            std::vector<old_school::CardId>{
                old_school::CardId::AirElemental,
            },
        };
    old_school::LearnedSearchConfig search{
        .seed = 0xA061C0B048ULL,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .blend_shallow_prior = false,
        .value_continuation_search_scope =
            old_school::
                LearnedContinuationSearchScope::
                    AllDecisions,
        .capture_settled_boundary_samples = true,
        .use_exact_combat_subgame = true,
    };

    expect(
        !old_school::LearnedSearchConfig{}
             .use_exact_combat_subgame,
        "exact combat sampler treatment is not default-off");

    old_school::GameState attack = multi_block_state();
    attack.players[1].creatures[0].tapped = false;
    const old_school::LearnedActionSamples attack_samples =
        old_school::learned_binary_attack_samples(
            attack, decks, 1, {}, 2, {}, model, search);
    expect(
        attack_samples.q_samples.size() == 2 &&
            attack_samples.q_samples[0].size() == 1 &&
            attack_samples.q_samples[1].size() == 1 &&
            attack_samples.terminal_evaluation_flags.size() ==
                2 &&
            attack_samples.settled_boundary_samples.size() ==
                2 &&
            attack_samples
                    .exact_combat_pure_chump_flags.size() ==
                2 &&
            attack_samples
                    .exact_combat_bound_fallback_flags.size() ==
                2 &&
            attack_samples
                    .exact_combat_bound_fallback_flags[0][0] ==
                0 &&
            attack_samples
                    .exact_combat_bound_fallback_flags[1][0] ==
                0 &&
            attack_samples.rollout_evaluations == 2 &&
            attack_samples.terminal_evaluations +
                    attack_samples.bootstrapped_evaluations ==
                2,
        "flagged exact Attack sampler accounting is wrong");

    const old_school::LearnedBlockChoiceSamples block_samples =
        old_school::learned_block_choice_samples(
            multi_block_state(), decks, 0, {2}, {}, 1,
            {3}, model, search);
    expect(
        block_samples.legal_attackers ==
                std::vector<old_school::PermanentId>{2} &&
            block_samples.samples.q_samples.size() == 2 &&
            block_samples.samples.q_samples[0].size() == 1 &&
            block_samples.samples.q_samples[1].size() == 1 &&
            block_samples.samples
                    .settled_boundary_samples.size() == 2 &&
            block_samples.samples
                    .exact_combat_pure_chump_flags.size() == 2 &&
            block_samples.samples
                    .exact_combat_bound_fallback_flags.size() ==
                2 &&
            block_samples.samples
                    .exact_combat_bound_fallback_flags[0][0] ==
                0 &&
            block_samples.samples
                    .exact_combat_bound_fallback_flags[1][0] ==
                0 &&
            block_samples.samples.rollout_evaluations == 2 &&
            block_samples.samples.terminal_evaluations +
                    block_samples.samples
                        .bootstrapped_evaluations ==
                2,
        "flagged exact Block sampler accounting is wrong");

    old_school::LearnedSearchConfig wrong_scope = search;
    wrong_scope.value_continuation_search_scope =
        old_school::LearnedContinuationSearchScope::
            PriorityOnly;
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::learned_binary_attack_samples(
                    attack, decks, 1, {}, 2, {}, model,
                    wrong_scope));
        },
        "exact combat flag accepted Priority-only scope");

    old_school::LearnedSearchConfig wrong_variant = search;
    wrong_variant.continuation_variant =
        old_school::LearnedVariant::UnifiedActor;
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::learned_binary_attack_samples(
                    attack, decks, 1, {}, 2, {}, model,
                    wrong_variant));
        },
        "exact combat flag accepted Unified Actor");

    const std::array<std::vector<old_school::CardId>, 2>
        lethal_decks = {
            std::vector<old_school::CardId>{},
            std::vector<old_school::CardId>{
                old_school::CardId::AirElemental,
            },
        };
    const old_school::LearnedActionSamples lethal =
        old_school::learned_binary_attack_samples(
            lethal_attack_state(), lethal_decks, 1, {}, 1,
            {}, model, search);
    expect(
        lethal.q_samples[1] ==
                std::vector<double>{1.0} &&
            lethal.terminal_evaluation_flags[1] ==
                std::vector<std::uint8_t>{1U} &&
            lethal.settled_boundary_samples[1] ==
                std::vector<double>{1.0} &&
            lethal.exact_combat_pure_chump_flags[1] ==
                std::vector<std::uint8_t>{0U} &&
            lethal.exact_combat_bound_fallback_flags[1] ==
                std::vector<std::uint8_t>{0U},
        "exact Attack sampler discarded its selected lethal "
        "resulting state");

    const std::array<std::vector<old_school::CardId>, 2>
        bound_decks = {
            std::vector<old_school::CardId>(
                9, old_school::CardId::FlyingMen),
            std::vector<old_school::CardId>{
                old_school::CardId::AirElemental,
            },
        };
    const old_school::LearnedActionSamples bounded =
        old_school::learned_binary_attack_samples(
            blocker_bound_state(), bound_decks, 1, {}, 10,
            {}, model, search);
    expect(
        bounded.exact_combat_bound_fallback_flags ==
                std::vector<std::vector<std::uint8_t>>{
                    {1U}, {1U}} &&
            bounded.exact_combat_pure_chump_flags ==
                std::vector<std::vector<std::uint8_t>>{
                    {0U}, {0U}} &&
            bounded.rollout_evaluations == 2,
        "exact Attack bound exhaustion did not fail closed to "
        "legacy C16 completion");

    old_school::LearnedSearchConfig legacy = search;
    legacy.capture_settled_boundary_samples = false;
    legacy.use_exact_combat_subgame = false;
    const old_school::LearnedActionSamples legacy_samples =
        old_school::learned_binary_attack_samples(
            attack, decks, 1, {}, 2, {}, model, legacy);
    expect(
        legacy_samples.settled_boundary_samples.empty() &&
            legacy_samples
                .exact_combat_pure_chump_flags.empty() &&
            legacy_samples
                .exact_combat_bound_fallback_flags.empty(),
        "default legacy Attack path returned exact-combat "
        "evidence");

    old_school::LearnedSearchConfig invalid_capture = legacy;
    invalid_capture.capture_settled_boundary_samples = true;
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::learned_binary_attack_samples(
                    attack, decks, 1, {}, 2, {}, model,
                    invalid_capture));
        },
        "legacy Attack accepted exact settled-boundary capture");
}

} // namespace

int main() {
    try {
        test_single_block_and_pure_chump();
        test_multi_block_orders_and_selection();
        test_authoritative_legality_and_bounds();
        test_flagged_learned_sampler_integration();
        std::cout
            << "4 exact combat subgame test groups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "exact combat subgame test failure: "
            << error.what() << '\n';
        return 1;
    }
}
