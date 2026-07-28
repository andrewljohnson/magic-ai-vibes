#include "old_school/fq0_information_set.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fq0 = old_school::fq0_information_set;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_invalid(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

template <typename Function>
void expect_out_of_range(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::out_of_range&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

template <typename Function>
void expect_length_error(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::length_error&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

template <typename Values>
bool bit_identical(const Values& first, const Values& second) {
    return first.size() == second.size() &&
           std::equal(
               first.begin(), first.end(), second.begin(),
               [](double left, double right) {
                   return std::bit_cast<std::uint64_t>(left) ==
                          std::bit_cast<std::uint64_t>(right);
               });
}

std::size_t candidate_index(
    const old_school::probes::DecisionProbe& probe,
    std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [&](const old_school::probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::runtime_error(
            "missing candidate " + std::string(descriptor));
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

old_school::GameState information_state() {
    using old_school::CardId;
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 1;
    state.turn_number = 7;
    state.next_permanent_id = 30;
    state.next_stack_object_id = 10;
    state.extra_turns_pending = {1, 0};

    state.players[0].life = 13;
    state.players[0].library = {
        CardId::Forest,
        CardId::IronrootTreefolk,
        CardId::GiantGrowth,
    };
    state.players[0].hand = {
        CardId::GiantGrowth,
        CardId::Forest,
    };
    state.players[0].graveyard = {
        CardId::GrizzlyBears,
    };
    state.players[0].exile = {
        CardId::GiantGrowth,
    };
    state.players[0].lands = {
        {.card = CardId::Forest, .tapped = true},
        {.card = CardId::Forest, .tapped = false},
    };
    state.players[0].creatures = {
        {
            .id = 10,
            .card = CardId::GrizzlyBears,
            .tapped = false,
            .summoning_sick = true,
            .damage = 0,
            .temporary_power_bonus = 0,
            .temporary_toughness_bonus = 0,
            .exile_on_death_this_turn = false,
        },
    };
    state.players[0].artifacts = {
        {
            .id = 20,
            .card = CardId::Millstone,
            .tapped = false,
        },
    };
    state.players[0].enchantments = {
        CardId::Moat,
    };
    state.players[0].mana_pool.green = 1;
    state.players[0].land_played_this_turn = true;

    state.players[1].life = 9;
    state.players[1].library = {
        CardId::Mountain,
        CardId::LightningBolt,
        CardId::HillGiant,
    };
    state.players[1].hand = {
        CardId::LightningBolt,
        CardId::Mountain,
    };
    state.players[1].graveyard = {
        CardId::GrayOgre,
    };
    state.players[1].lands = {
        {.card = CardId::Mountain, .tapped = true},
    };
    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 9,
            .card = CardId::LightningBolt,
            .controller = 1,
            .target =
                old_school::Target::creature_target(0, 10),
            .spell_target = std::nullopt,
            .x_value = 0,
        },
    };
    return state;
}

old_school::GameState canonicalization_state() {
    using old_school::CardId;
    old_school::GameState state = information_state();
    state.next_permanent_id = 50;
    state.next_stack_object_id = 200;
    state.players[0].creatures = {
        {
            .id = 10,
            .card = CardId::GrizzlyBears,
            .tapped = false,
            .summoning_sick = true,
        },
        {
            .id = 11,
            .card = CardId::IronrootTreefolk,
            .tapped = true,
            .summoning_sick = false,
            .damage = 2,
        },
    };
    state.players[0].artifacts = {
        {
            .id = 20,
            .card = CardId::Millstone,
            .tapped = false,
        },
        {
            .id = 21,
            .card = CardId::SolRing,
            .tapped = true,
        },
    };
    state.players[0].enchantments = {
        CardId::Moat,
        CardId::GiantGrowth,
    };
    state.players[1].creatures = {
        {
            .id = 30,
            .card = CardId::FlyingMen,
            .tapped = false,
            .summoning_sick = false,
        },
        {
            .id = 31,
            .card = CardId::AirElemental,
            .tapped = true,
            .summoning_sick = false,
            .damage = 1,
        },
    };
    state.players[1].artifacts = {
        {
            .id = 40,
            .card = CardId::MoxSapphire,
            .tapped = false,
        },
    };
    state.stack = {
        {
            .id = 100,
            .card = CardId::LightningBolt,
            .controller = 1,
            .target =
                old_school::Target::creature_target(0, 10),
        },
        {
            .id = 101,
            .card = CardId::GiantGrowth,
            .controller = 0,
            .target =
                old_school::Target::creature_target(0, 11),
        },
        {
            .id = 102,
            .card = CardId::Counterspell,
            .controller = 1,
            .spell_target = 100,
        },
        {
            .id = 103,
            .card = CardId::ForceSpike,
            .controller = 0,
            .spell_target = 102,
        },
    };
    return state;
}

old_school::GameState relabeled_canonicalization_state() {
    old_school::GameState state = canonicalization_state();
    std::reverse(
        state.players[0].lands.begin(),
        state.players[0].lands.end());
    state.players[0].creatures[0].id = 901;
    state.players[0].creatures[1].id = 115;
    std::reverse(
        state.players[0].creatures.begin(),
        state.players[0].creatures.end());
    state.players[0].artifacts[0].id = 777;
    state.players[0].artifacts[1].id = 333;
    std::reverse(
        state.players[0].artifacts.begin(),
        state.players[0].artifacts.end());
    std::reverse(
        state.players[0].enchantments.begin(),
        state.players[0].enchantments.end());
    state.players[1].creatures[0].id = 84;
    state.players[1].creatures[1].id = 72;
    std::reverse(
        state.players[1].creatures.begin(),
        state.players[1].creatures.end());
    state.players[1].artifacts[0].id = 45;

    state.stack[0].id = 950;
    state.stack[0].target =
        old_school::Target::creature_target(0, 901);
    state.stack[1].id = 120;
    state.stack[1].target =
        old_school::Target::creature_target(0, 115);
    state.stack[2].id = 810;
    state.stack[2].spell_target = 950;
    state.stack[3].id = 4;
    state.stack[3].spell_target = 810;
    state.next_permanent_id = 5000;
    state.next_stack_object_id = 9000;
    return state;
}

old_school::GameState seat_swapped_canonicalization_state() {
    old_school::GameState state = canonicalization_state();
    std::swap(state.players[0], state.players[1]);
    std::swap(state.stats[0], state.stats[1]);
    std::swap(
        state.extra_turns_pending[0],
        state.extra_turns_pending[1]);
    std::swap(state.failed_draw[0], state.failed_draw[1]);
    state.active_player = 1 - state.active_player;
    state.starting_player = 1 - state.starting_player;
    for (old_school::StackObject& object : state.stack) {
        object.controller = 1 - object.controller;
        if (object.target.has_value()) {
            object.target->player =
                1 - object.target->player;
        }
    }
    return state;
}

old_school::LearnedDecisionContext information_context() {
    return {
        .valid = true,
        .phase = old_school::TurnPhase::SecondMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
}

fq0::InformationSetKey key_for(
    const old_school::GameState& state) {
    return fq0::make_information_set_key(
        state, information_context(),
        old_school::legal_priority_actions(
            state, 0, true));
}

void expect_key_changed(
    const fq0::InformationSetKey& baseline,
    const old_school::GameState& changed,
    const old_school::LearnedDecisionContext& context,
    std::string_view message) {
    const auto actions =
        old_school::legal_priority_actions(
            changed, context.decision_player,
            context.sorcery_actions);
    const auto key = fq0::make_information_set_key(
        changed, context, actions);
    expect(
        key != baseline &&
            fq0::information_set_sha256(key) !=
                fq0::information_set_sha256(baseline),
        message);
}

void test_information_key_redacts_hidden_identity() {
    const old_school::GameState state = information_state();
    const fq0::InformationSetKey original = key_for(state);

    old_school::GameState repartitioned = state;
    std::reverse(
        repartitioned.players[0].library.begin(),
        repartitioned.players[0].library.end());
    repartitioned.players[1].hand = {
        old_school::CardId::HillGiant,
        old_school::CardId::Mountain,
    };
    repartitioned.players[1].library = {
        old_school::CardId::LightningBolt,
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt,
    };
    const fq0::InformationSetKey hidden = key_for(repartitioned);
    expect(hidden == original,
           "hidden repartition changed the information key");
    expect(
        fq0::information_set_sha256(hidden) ==
            fq0::information_set_sha256(original),
        "hidden repartition changed the information fingerprint");

    old_school::GameState permuted_hand = state;
    std::reverse(
        permuted_hand.players[0].hand.begin(),
        permuted_hand.players[0].hand.end());
    const auto permuted_key = key_for(permuted_hand);
    expect(
        permuted_key == original &&
            fq0::information_set_sha256(permuted_key) ==
                fq0::information_set_sha256(original) &&
            !permuted_key.observation()
                 .revealed_opponent_hand
                 .has_value(),
        "hand permutation or debug disclosure entered the key");
}

void test_information_key_required_sensitivities() {
    const old_school::GameState state = information_state();
    const fq0::InformationSetKey baseline = key_for(state);
    const auto context = information_context();
    const auto actions =
        old_school::legal_priority_actions(
            state, 0, true);
    auto own_hand = state;
    own_hand.players[0].hand[1] =
        old_school::CardId::Mountain;
    expect_key_changed(
        baseline, own_hand, context,
        "own hand identity did not change the key");

    auto public_graveyard = state;
    public_graveyard.players[1].graveyard.push_back(
        old_school::CardId::LightningBolt);
    expect_key_changed(
        baseline, public_graveyard, context,
        "public zone identity did not change the key");

    auto public_permanent = state;
    public_permanent.players[0].creatures[0].damage = 1;
    expect_key_changed(
        baseline, public_permanent, context,
        "public permanent state did not change the key");

    auto public_stack = state;
    public_stack.stack[0].target =
        old_school::Target::player_target(0);
    expect_key_changed(
        baseline, public_stack, context,
        "public stack state did not change the key");

    auto opponent_hand_size = state;
    opponent_hand_size.players[1].hand.push_back(
        old_school::CardId::LightningBolt);
    expect_key_changed(
        baseline, opponent_hand_size, context,
        "opponent hidden-zone size did not change the key");

    auto owner_library_size = state;
    owner_library_size.players[0].library.push_back(
        old_school::CardId::Forest);
    expect_key_changed(
        baseline, owner_library_size, context,
        "owner future-library size did not change the key");

    auto changed_context = context;
    changed_context.phase =
        old_school::TurnPhase::FirstMain;
    const auto contextual = fq0::make_information_set_key(
        state, changed_context,
        old_school::legal_priority_actions(
            state, 0, true));
    expect(
        contextual != baseline &&
            fq0::information_set_sha256(contextual) !=
                fq0::information_set_sha256(baseline),
        "decision context did not change the key");

    auto reversed_actions = actions;
    std::reverse(
        reversed_actions.begin(), reversed_actions.end());
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, context, reversed_actions);
        },
        "reordered engine actions were accepted");
}

void test_descriptor_rows_are_canonical_and_unique() {
    const old_school::GameState state = information_state();
    const auto key = key_for(state);
    const auto canonical =
        fq0::descriptor_canonical_action_rows(key);
    expect(canonical.size() == key.ordered_actions().size(),
           "descriptor canonicalization lost an action");
    expect(
        std::is_sorted(
            canonical.begin(), canonical.end(),
            [](const fq0::CanonicalActionRow& first,
               const fq0::CanonicalActionRow& second) {
                return first.descriptor <
                       second.descriptor;
            }),
        "descriptor rows are not canonical");

}

void test_action_set_and_priority_context_fail_closed() {
    const old_school::GameState state = information_state();
    const auto context = information_context();
    const auto actions =
        old_school::legal_priority_actions(
            state, 0, true);
    expect(actions.size() >= 2,
           "focused state lost its multi-action root");

    auto missing_pass = actions;
    missing_pass.erase(missing_pass.begin());
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, context, missing_pass);
        },
        "incomplete legal action set was accepted");

    auto duplicate_pass = actions;
    duplicate_pass.back() = duplicate_pass.front();
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, context, duplicate_pass);
        },
        "duplicate/missing legal action set was accepted");

    auto malformed_pass = actions;
    malformed_pass.front().card =
        old_school::CardId::LightningBolt;
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, context, malformed_pass);
        },
        "malformed typed Pass was accepted");

    auto invalid_kind = actions;
    invalid_kind.front().kind =
        static_cast<old_school::PriorityActionKind>(255);
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, context, invalid_kind);
        },
        "invalid Priority action enum was accepted");

    auto invalid_target = actions;
    invalid_target.back().target =
        old_school::Target::creature_target(2, 10);
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, context, invalid_target);
        },
        "invalid action target was accepted");

    auto invalid_phase = context;
    invalid_phase.phase =
        old_school::TurnPhase::DeclareAttackers;
    invalid_phase.sorcery_actions = false;
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, invalid_phase, actions);
        },
        "non-Priority phase was accepted");

    auto timing_mismatch = context;
    timing_mismatch.sorcery_actions = false;
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, timing_mismatch, actions);
        },
        "phase/sorcery timing mismatch was accepted");

    auto absent = context;
    absent.valid = false;
    expect_invalid(
        [&] {
            fq0::make_information_set_key(
                state, absent, actions);
        },
        "absent successor context was accepted");
}

void test_repeated_construction_is_bit_stable() {
    const old_school::GameState state = information_state();
    const auto first = key_for(state);
    const auto second = key_for(state);
    expect(
        first == second &&
            fq0::information_set_sha256(first) ==
                fq0::information_set_sha256(second) &&
            fq0::information_set_sha256(first).size() == 64,
        "repeated information-key construction drifted");

    const auto first_leaf =
        fq0::redacted_leaf_consequence_sha256(
            state, 0, information_context());
    const auto second_leaf =
        fq0::redacted_leaf_consequence_sha256(
            state, 0, information_context());
    expect(
        first_leaf == second_leaf &&
            first_leaf.size() == 64,
        "repeated redacted leaf hashing drifted");
}

void test_leaf_hash_redacts_future_libraries() {
    old_school::GameState state = information_state();
    const auto baseline =
        fq0::redacted_leaf_consequence_sha256(
            state, 0, information_context());

    state.players[0].library = {
        old_school::CardId::AirElemental,
        old_school::CardId::Counterspell,
        old_school::CardId::Island,
    };
    state.players[1].library = {
        old_school::CardId::Forest,
        old_school::CardId::GiantGrowth,
        old_school::CardId::GrizzlyBears,
    };
    std::reverse(
        state.players[0].hand.begin(),
        state.players[0].hand.end());
    const auto hidden =
        fq0::redacted_leaf_consequence_sha256(
            state, 0, information_context());
    expect(hidden == baseline,
           "future library identity entered the leaf hash");

    auto public_state = state;
    --public_state.players[1].life;
    expect(
        fq0::redacted_leaf_consequence_sha256(
            public_state, 0, information_context()) !=
            baseline,
        "public leaf consequence did not change the hash");

    old_school::GameResult terminal;
    terminal.winner = 0;
    terminal.reason = old_school::EndReason::LifeTotal;
    terminal.turns = state.turn_number;
    terminal.starting_player = state.starting_player;
    terminal.ending_life = {
        state.players[0].life, state.players[1].life};
    const auto win =
        fq0::redacted_leaf_consequence_sha256(
            state, 0, {}, terminal);
    terminal.winner = 1;
    const auto loss =
        fq0::redacted_leaf_consequence_sha256(
            state, 0, {}, terminal);
    expect(
        win != baseline && loss != baseline && win != loss,
        "terminal outcome did not enter the leaf hash");
}

void test_leaf_consequence_canonicalizes_public_object_ids() {
    const old_school::GameState original =
        canonicalization_state();
    const old_school::GameState relabeled =
        relabeled_canonicalization_state();
    const auto context = information_context();
    const auto original_hash =
        fq0::redacted_leaf_consequence_sha256(
            original, 0, context);
    const auto relabeled_hash =
        fq0::redacted_leaf_consequence_sha256(
            relabeled, 0, context);
    expect(
        original_hash == relabeled_hash,
        "physical public-object IDs or battlefield vector order "
        "changed the leaf consequence");

    const auto original_key = key_for(original);
    const auto relabeled_key = key_for(relabeled);
    expect(
        original_key != relabeled_key &&
            fq0::information_set_sha256(original_key) !=
                fq0::information_set_sha256(relabeled_key),
        "consequence canonicalization leaked into exact "
        "information-set/action identity");

    const auto original_growth =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 10));
    const auto relabeled_growth =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 901));
    expect(
        fq0::canonical_priority_consequence_sha256(
            original, 0, context, original_growth) ==
            fq0::canonical_priority_consequence_sha256(
                relabeled, 0, context, relabeled_growth),
        "immediate Priority consequence retained physical IDs");

    auto dangling_first = original;
    dangling_first.stack[0].target =
        old_school::Target::creature_target(0, 50000);
    auto dangling_second = original;
    dangling_second.stack[0].target =
        old_school::Target::creature_target(1, 60000);
    expect(
        fq0::redacted_leaf_consequence_sha256(
            dangling_first, 0, context) ==
            fq0::redacted_leaf_consequence_sha256(
                dangling_second, 0, context),
        "rules-equivalent missing creature targets retained "
        "physical IDs or former target players");

    dangling_first.stack[2].spell_target = 50001;
    dangling_second.stack[2].spell_target = 60001;
    expect(
        fq0::redacted_leaf_consequence_sha256(
            dangling_first, 0, context) ==
            fq0::redacted_leaf_consequence_sha256(
                dangling_second, 0, context),
        "rules-equivalent missing spell targets retained "
        "physical IDs");
}

void test_leaf_consequence_preserves_public_semantics() {
    const old_school::GameState state =
        canonicalization_state();
    const auto context = information_context();
    const auto baseline =
        fq0::redacted_leaf_consequence_sha256(
            state, 0, context);

    auto creature_target = state;
    creature_target.stack[0].target =
        old_school::Target::creature_target(0, 11);
    expect(
        fq0::redacted_leaf_consequence_sha256(
            creature_target, 0, context) != baseline,
        "targeting a semantically different creature collapsed");

    auto spell_target = state;
    spell_target.stack[2].spell_target = 101;
    expect(
        fq0::redacted_leaf_consequence_sha256(
            spell_target, 0, context) != baseline,
        "targeting a different live stack object collapsed");

    auto artifact_state = state;
    artifact_state.players[0].artifacts[0].tapped = true;
    expect(
        fq0::redacted_leaf_consequence_sha256(
            artifact_state, 0, context) != baseline,
        "artifact rules state did not enter the consequence");

    auto stack_order = state;
    std::swap(stack_order.stack[0], stack_order.stack[1]);
    expect(
        fq0::redacted_leaf_consequence_sha256(
            stack_order, 0, context) != baseline,
        "rules-significant stack order was canonicalized away");

    auto graveyard_order = state;
    graveyard_order.players[0].graveyard = {
        old_school::CardId::GrizzlyBears,
        old_school::CardId::GiantGrowth,
    };
    const auto first_order =
        fq0::redacted_leaf_consequence_sha256(
            graveyard_order, 0, context);
    std::reverse(
        graveyard_order.players[0].graveyard.begin(),
        graveyard_order.players[0].graveyard.end());
    expect(
        fq0::redacted_leaf_consequence_sha256(
            graveyard_order, 0, context) != first_order,
        "public zone order was canonicalized away");

    auto exile_order = state;
    exile_order.players[0].exile = {
        old_school::CardId::GiantGrowth,
        old_school::CardId::GrizzlyBears,
    };
    const auto first_exile_order =
        fq0::redacted_leaf_consequence_sha256(
            exile_order, 0, context);
    std::reverse(
        exile_order.players[0].exile.begin(),
        exile_order.players[0].exile.end());
    expect(
        fq0::redacted_leaf_consequence_sha256(
            exile_order, 0, context) ==
            first_exile_order,
        "unordered exile permutation changed the consequence");

    auto duplicate_permanent = state;
    duplicate_permanent.players[0].artifacts[0].id =
        duplicate_permanent.players[0].creatures[0].id;
    expect_invalid(
        [&] {
            fq0::redacted_leaf_consequence_sha256(
                duplicate_permanent, 0, context);
        },
        "ambiguous duplicate permanent IDs were accepted");

    auto duplicate_stack = state;
    duplicate_stack.stack[1].id =
        duplicate_stack.stack[0].id;
    expect_invalid(
        [&] {
            fq0::redacted_leaf_consequence_sha256(
                duplicate_stack, 0, context);
        },
        "ambiguous duplicate stack IDs were accepted");
}

void test_leaf_consequence_is_observer_relative() {
    const old_school::GameState original =
        canonicalization_state();
    const old_school::GameState swapped =
        seat_swapped_canonicalization_state();
    const auto original_context = information_context();
    auto swapped_context = original_context;
    swapped_context.decision_player = 1;
    expect(
        fq0::redacted_leaf_consequence_sha256(
            original, 0, original_context) ==
            fq0::redacted_leaf_consequence_sha256(
                swapped, 1, swapped_context),
        "seat-swapped owner-equivalent leaf changed the "
        "consequence");

    const auto original_growth =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 10));
    const auto swapped_growth =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(1, 10));
    expect(
        fq0::canonical_priority_consequence_sha256(
            original, 0, original_context,
            original_growth) ==
            fq0::canonical_priority_consequence_sha256(
                swapped, 1, swapped_context,
                swapped_growth),
        "seat-swapped owner-equivalent Priority action changed "
        "the consequence");

    old_school::GameResult original_terminal;
    original_terminal.winner = 0;
    original_terminal.reason =
        old_school::EndReason::LifeTotal;
    original_terminal.turns = 11;
    original_terminal.starting_player = 1;
    original_terminal.ending_life = {7, -2};
    original_terminal.player_stats[0].spells_cast = 3;
    original_terminal.player_stats[1].spells_cast = 5;
    old_school::GameResult swapped_terminal =
        original_terminal;
    swapped_terminal.winner = 1;
    swapped_terminal.starting_player = 0;
    std::swap(
        swapped_terminal.ending_life[0],
        swapped_terminal.ending_life[1]);
    std::swap(
        swapped_terminal.player_stats[0],
        swapped_terminal.player_stats[1]);
    expect(
        fq0::redacted_leaf_consequence_sha256(
            original, 0, {}, original_terminal) ==
            fq0::redacted_leaf_consequence_sha256(
                swapped, 1, {}, swapped_terminal),
        "seat-swapped terminal outcome changed the consequence");
}

void test_canonical_priority_consequence_transition_semantics() {
    const auto context = information_context();
    const old_school::GameState cast_state =
        canonicalization_state();
    const auto growth =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 10));
    const auto cast_hash =
        fq0::canonical_priority_consequence_sha256(
            cast_state, 0, context, growth);
    expect(
        cast_hash ==
                fq0::canonical_priority_consequence_sha256(
                    cast_state, 0, context, growth) &&
            cast_hash.size() == 64,
        "cast-retains-priority consequence was not stable");

    const auto pass =
        old_school::PriorityAction::pass();
    const auto transferred =
        fq0::canonical_priority_consequence_sha256(
            cast_state, 0, context, pass);
    auto resolving_context = context;
    resolving_context.consecutive_passes = 1;
    const auto resolved =
        fq0::canonical_priority_consequence_sha256(
            cast_state, 0, resolving_context, pass);

    auto empty_stack = cast_state;
    empty_stack.stack.clear();
    const auto window_ended =
        fq0::canonical_priority_consequence_sha256(
            empty_stack, 0, resolving_context, pass);

    auto lethal = information_state();
    lethal.players[0].life = 3;
    lethal.stack[0].target =
        old_school::Target::player_target(0);
    const auto terminal =
        fq0::canonical_priority_consequence_sha256(
            lethal, 0, resolving_context, pass);
    expect(
        cast_hash != transferred &&
            transferred != resolved &&
            resolved != window_ended &&
            window_ended != terminal,
        "Priority transition dispositions collapsed");

    const auto illegal =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 99999));
    expect_invalid(
        [&] {
            fq0::canonical_priority_consequence_sha256(
                cast_state, 0, context, illegal);
        },
        "non-authoritative Priority action was accepted");
    expect_invalid(
        [&] {
            fq0::canonical_priority_consequence_sha256(
                cast_state, 1, context, pass);
        },
        "non-owner consequence observer was accepted");
}

void test_indexed_seed_uses_every_coordinate() {
    constexpr std::uint64_t kBase = 0xF00B411ULL;
    const fq0::IndexedSeedCoordinates baseline{
        .domain = fq0::SeedDomain::RootDeterminization,
        .scope = "root-17",
        .group = "root",
        .bank = fq0::SeedBank::Root,
        .block = 3,
        .world = 11,
    };
    const std::uint64_t repeated =
        fq0::derive_indexed_seed(kBase, baseline);
    expect(
        repeated ==
            fq0::derive_indexed_seed(kBase, baseline),
        "indexed seed is not repeatable");

    std::set<std::uint64_t> seeds = {repeated};
    const auto add_changed =
        [&](fq0::IndexedSeedCoordinates changed) {
            seeds.insert(
                fq0::derive_indexed_seed(kBase, changed));
        };
    auto changed = baseline;
    changed.domain =
        fq0::SeedDomain::RootMacroTransition;
    add_changed(changed);
    changed = baseline;
    changed.scope = "root-18";
    add_changed(changed);
    changed = baseline;
    changed.group = "successor-deadbeef";
    add_changed(changed);
    changed = baseline;
    changed.bank = fq0::SeedBank::A;
    add_changed(changed);
    changed = baseline;
    changed.block = 4;
    add_changed(changed);
    changed = baseline;
    changed.world = 12;
    add_changed(changed);
    seeds.insert(fq0::derive_indexed_seed(kBase + 1, baseline));
    expect(
        seeds.size() == 8,
        "a seed domain or coordinate was not bound");

    changed = baseline;
    changed.scope.clear();
    expect_invalid(
        [&] {
            fq0::derive_indexed_seed(kBase, changed);
        },
        "empty explicit seed scope was accepted");
}

void test_graveyard_order_features_are_exact_and_hidden_safe() {
    using old_school::CardId;
    constexpr std::size_t cards = old_school::kCardCount;

    old_school::GameState state;
    state.players[0].graveyard = {
        CardId::GrizzlyBears,
        CardId::LightningBolt,
        CardId::GrizzlyBears,
    };
    state.players[1].graveyard = {
        CardId::Counterspell,
        CardId::Counterspell,
    };
    state.players[1].hand = {
        CardId::Island,
        CardId::ForceSpike,
    };
    state.players[1].library = {
        CardId::AirElemental,
        CardId::FlyingMen,
    };
    state.players[0].artifacts = {
        {
            .id = 41,
            .card = CardId::Millstone,
            .tapped = false,
        },
    };
    state.next_permanent_id = 42;

    const auto from_zero =
        old_school::learned_graveyard_order_features(state, 0);
    expect(
        from_zero[static_cast<std::size_t>(
                      CardId::GrizzlyBears)] ==
                0.625 &&
            from_zero[static_cast<std::size_t>(
                          CardId::LightningBolt)] ==
                0.25 &&
            from_zero[cards +
                      static_cast<std::size_t>(
                          CardId::Counterspell)] ==
                0.75,
        "graveyard order did not use exact top-relative binary "
        "weights");

    const auto from_one =
        old_school::learned_graveyard_order_features(state, 1);
    for (std::size_t card = 0; card < cards; ++card) {
        expect(
            std::bit_cast<std::uint64_t>(from_zero[card]) ==
                    std::bit_cast<std::uint64_t>(
                        from_one[cards + card]) &&
                std::bit_cast<std::uint64_t>(
                    from_zero[cards + card]) ==
                    std::bit_cast<std::uint64_t>(
                        from_one[card]),
            "graveyard order features are not observer-relative");
    }

    old_school::GameState hidden = state;
    hidden.players[1].hand = {
        CardId::AirElemental,
        CardId::FlyingMen,
    };
    hidden.players[1].library = {
        CardId::Island,
        CardId::ForceSpike,
    };
    expect(
        bit_identical(
            from_zero,
            old_school::learned_graveyard_order_features(hidden, 0)),
        "opponent hidden repartition changed graveyard-order "
        "features");

    old_school::GameState physical = state;
    physical.players[0].artifacts.front().id = 987654;
    physical.next_permanent_id = 987655;
    expect(
        bit_identical(
            from_zero,
            old_school::learned_graveyard_order_features(physical, 0)),
        "physical permanent IDs changed graveyard-order features");

    old_school::GameState empty;
    const old_school::LearnedGraveyardOrderFeatures zeros{};
    expect(
        bit_identical(
            zeros,
            old_school::learned_graveyard_order_features(empty, 0)),
        "empty graveyards did not encode as exact zero");

    expect_out_of_range(
        [&] {
            static_cast<void>(
                old_school::learned_graveyard_order_features(
                    state, 2));
        },
        "invalid graveyard-order perspective was accepted");

    old_school::GameState exact_limit_first;
    exact_limit_first.players[0].graveyard.assign(
        std::numeric_limits<double>::digits,
        CardId::Forest);
    exact_limit_first.players[0].graveyard.front() =
        CardId::Counterspell;
    old_school::GameState exact_limit_second = exact_limit_first;
    std::swap(
        exact_limit_second.players[0].graveyard[0],
        exact_limit_second.players[0].graveyard[1]);
    const auto exact_first =
        old_school::learned_graveyard_order_features(
            exact_limit_first, 0);
    const auto exact_second =
        old_school::learned_graveyard_order_features(
            exact_limit_second, 0);
    expect(
        exact_first[static_cast<std::size_t>(
                        CardId::Counterspell)] ==
                std::ldexp(
                    1.0,
                    -std::numeric_limits<double>::digits) &&
            !bit_identical(exact_first, exact_second),
        "graveyard-order features lost the exact depth-53 bit");

    old_school::GameState too_deep;
    too_deep.players[0].graveyard.assign(
        std::numeric_limits<double>::digits + 1,
        CardId::Forest);
    expect_length_error(
        [&] {
            static_cast<void>(
                old_school::learned_graveyard_order_features(
                    too_deep, 0));
        },
        "inexact graveyard-order depth was accepted");
}

void test_ordered_graveyard_alias_micro_corpus() {
    using old_school::CardId;
    using old_school::LearnedDecisionContext;
    using old_school::PriorityAction;
    using old_school::TurnPhase;
    using old_school::probes::Dc1CanonicalSettlement;

    const auto controls =
        old_school::probes::
            make_counter_composition_controls_v1();
    expect(
        old_school::probes::
                validate_counter_composition_controls_v1(controls)
                    .empty() &&
            controls.size() == 2,
        "counter-composition control corpus is invalid");
    const old_school::probes::DecisionProbe& blue = controls[1];
    const old_school::GameState blue_world =
        old_school::sample_determinization(
            blue.state, blue.original_decks, blue.root_player,
            577215);
    const Dc1CanonicalSettlement same_target =
        old_school::probes::settle_dc1_priority_candidate(
            blue, blue_world,
            candidate_index(
                blue, "counter-same-air-elemental"));
    const Dc1CanonicalSettlement answer_counter =
        old_school::probes::settle_dc1_priority_candidate(
            blue, blue_world,
            candidate_index(
                blue, "counter-opponent-counterspell"));
    expect(
        same_target.window_ended && answer_counter.window_ended &&
            same_target.settled_state.stack.empty() &&
            answer_counter.settled_state.stack.empty() &&
            same_target.settled_state.players[1].graveyard ==
                std::vector<CardId>{
                    CardId::AirElemental,
                    CardId::Counterspell,
                } &&
            answer_counter.settled_state.players[1].graveyard ==
                std::vector<CardId>{
                    CardId::Counterspell,
                    CardId::AirElemental,
                },
        "counter settlements did not reproduce the registered "
        "graveyard-order alias");

    old_school::GameState blue_first =
        same_target.settled_state;
    old_school::GameState blue_second =
        answer_counter.settled_state;
    const std::size_t blue_owner = blue_first.active_player;
    expect(
        blue_owner == blue_second.active_player,
        "counter settlements changed the next active player");
    blue_first.players[blue_owner].hand.push_back(
        CardId::MoxSapphire);
    blue_second.players[blue_owner].hand.push_back(
        CardId::MoxSapphire);

    auto same_public = old_school::observe_game_state(
        blue_first, 1);
    auto answer_public = old_school::observe_game_state(
        blue_second, 1);
    answer_public.players[1].graveyard =
        same_public.players[1].graveyard;
    expect(
        same_public == answer_public,
        "counter settlements differ by more than public graveyard "
        "order");

    const bool blue_sorcery = true;
    const auto same_actions =
        old_school::legal_priority_actions(
            blue_first, blue_owner, blue_sorcery);
    const auto answer_actions =
        old_school::legal_priority_actions(
            blue_second, blue_owner, blue_sorcery);
    expect(
        same_actions.size() >= 2 &&
            same_actions == answer_actions &&
            bit_identical(
                old_school::learned_observation(
                    blue_first, blue_owner),
                old_school::learned_observation(
                    blue_second, blue_owner)),
        "current learned observation did not reproduce the Blue "
        "alias");

    const LearnedDecisionContext blue_context{
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player = blue_owner,
        .consecutive_passes = 0,
        .sorcery_actions = blue_sorcery,
    };
    expect(
        fq0::information_set_sha256(
            fq0::make_information_set_key(
                blue_first, blue_context, same_actions)) !=
            fq0::information_set_sha256(
                fq0::make_information_set_key(
                    blue_second, blue_context, answer_actions)),
        "FQ0 information identity lost public graveyard order");
    for (const PriorityAction& action : same_actions) {
        expect(
            bit_identical(
                old_school::learned_priority_policy_features(
                    blue_first, blue_owner, action, blue_sorcery,
                    blue_context.phase, 0),
                old_school::learned_priority_policy_features(
                    blue_second, blue_owner, action, blue_sorcery,
                    blue_context.phase, 0)),
            "current Blue policy row did not reproduce the "
            "registered alias");
    }
    expect(
        !bit_identical(
            old_school::learned_graveyard_order_features(
                blue_first, blue_owner),
            old_school::learned_graveyard_order_features(
                blue_second, blue_owner)),
        "candidate planes did not split the Blue graveyard alias");

    old_school::GameState white_first;
    white_first.active_player = 0;
    white_first.starting_player = 0;
    white_first.turn_number = 9;
    white_first.next_permanent_id = 2;
    white_first.players[0].hand = {CardId::Plains};
    white_first.players[0].library = {CardId::Moat};
    white_first.players[0].lands = {
        {.card = CardId::Plains},
        {.card = CardId::Plains},
    };
    white_first.players[0].artifacts = {
        {
            .id = 1,
            .card = CardId::Millstone,
            .tapped = false,
        },
    };
    white_first.players[0].graveyard = {
        CardId::AirElemental,
        CardId::Counterspell,
        CardId::Island,
    };
    white_first.players[1].library = {
        CardId::Forest,
        CardId::GiantGrowth,
    };
    white_first.players[1].hand = {
        CardId::LightningBolt,
        CardId::Mountain,
    };
    old_school::GameState white_second = white_first;
    std::swap(
        white_second.players[0].graveyard[0],
        white_second.players[0].graveyard[1]);
    expect(
        white_first.players[0].graveyard.back() ==
            white_second.players[0].graveyard.back(),
        "White witness does not bury the swapped graveyard pair");

    const auto white_actions =
        old_school::legal_priority_actions(
            white_first, 0, true);
    const auto white_second_actions =
        old_school::legal_priority_actions(
            white_second, 0, true);
    expect(
        white_actions.size() == 4 &&
            white_actions == white_second_actions &&
            bit_identical(
                old_school::learned_observation(
                    white_first, 0),
                old_school::learned_observation(
                    white_second, 0)),
        "current learned observation did not reproduce the buried "
        "White alias");
    const LearnedDecisionContext white_context{
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    expect(
        fq0::information_set_sha256(
            fq0::make_information_set_key(
                white_first, white_context,
                white_actions)) !=
            fq0::information_set_sha256(
                fq0::make_information_set_key(
                    white_second, white_context,
                    white_second_actions)),
        "White information identity lost buried graveyard order");
    for (const PriorityAction& action : white_actions) {
        expect(
            bit_identical(
                old_school::learned_priority_policy_features(
                    white_first, 0, action, true,
                    TurnPhase::FirstMain, 0),
                old_school::learned_priority_policy_features(
                    white_second, 0, action, true,
                    TurnPhase::FirstMain, 0)),
            "current White policy row did not reproduce the "
            "registered alias");
    }
    expect(
        !bit_identical(
            old_school::learned_graveyard_order_features(
                white_first, 0),
            old_school::learned_graveyard_order_features(
                white_second, 0)),
        "candidate planes did not split the buried White alias");

    old_school::GameState hidden = white_first;
    hidden.players[1].hand = {
        CardId::Forest,
        CardId::GiantGrowth,
    };
    hidden.players[1].library = {
        CardId::LightningBolt,
        CardId::Mountain,
    };
    expect(
        bit_identical(
            old_school::learned_graveyard_order_features(
                white_first, 0),
            old_school::learned_graveyard_order_features(
                hidden, 0)),
        "hidden opponent repartition changed the White candidate "
        "planes");
}

void test_terminal_values_draw_and_complement() {
    old_school::GameResult result;
    result.winner = -1;
    const double draw0 =
        fq0::terminal_root_owner_value(result, 0);
    const double draw1 =
        fq0::terminal_root_owner_value(result, 1);
    expect(
        std::bit_cast<std::uint64_t>(draw0) ==
                std::bit_cast<std::uint64_t>(0.5) &&
            std::bit_cast<std::uint64_t>(draw1) ==
                std::bit_cast<std::uint64_t>(0.5),
        "terminal draw is not exact");

    result.winner = 0;
    const double winner =
        fq0::terminal_root_owner_value(result, 0);
    const double loser =
        fq0::terminal_root_owner_value(result, 1);
    expect(
        winner == 1.0 && loser == 0.0 &&
            winner == 1.0 - loser,
        "terminal perspectives are not exact complements");

    result.winner = 2;
    expect_invalid(
        [&] {
            fq0::terminal_root_owner_value(result, 0);
        },
        "invalid terminal winner was accepted");
}

void test_legacy_contextual_critic_is_bit_identical() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xF00C16ULL);
    expect(
        old_school::learned_critic_schema(model) ==
            old_school::LearnedCriticSchema::
                LegacyStateOnly,
        "test model is not a legacy critic");
    const old_school::GameState state = information_state();
    const auto context = information_context();
    const auto evaluated =
        fq0::evaluate_legacy_leaf_critic(
            state, 0, context, model);
    const double direct_contextual =
        old_school::learned_contextual_critic_value(
            state, 0, context, model);
    const double direct_legacy =
        old_school::learned_critic_value(
            state, 0, model);
    expect(
        evaluated.legacy_bit_identity &&
            evaluated.contextual_bits ==
                evaluated.legacy_bits &&
            evaluated.contextual_bits ==
                std::bit_cast<std::uint64_t>(
                    direct_contextual) &&
            evaluated.legacy_bits ==
                std::bit_cast<std::uint64_t>(
                    direct_legacy) &&
            evaluated ==
                fq0::evaluate_legacy_leaf_critic(
                    state, 0, context, model),
        "legacy contextual critic identity was not proven");

    auto absent = context;
    absent.valid = false;
    expect_invalid(
        [&] {
            fq0::evaluate_legacy_leaf_critic(
                state, 0, absent, model);
        },
        "absent critic context made identity vacuous");

    auto declaration = context;
    declaration.phase =
        old_school::TurnPhase::DamageOrder;
    declaration.sorcery_actions = false;
    expect_invalid(
        [&] {
            fq0::evaluate_legacy_leaf_critic(
                state, 0, declaration, model);
        },
        "non-Priority critic phase was accepted");

    auto timing_mismatch = context;
    timing_mismatch.sorcery_actions = false;
    expect_invalid(
        [&] {
            fq0::evaluate_legacy_leaf_critic(
                state, 0, timing_mismatch, model);
        },
        "critic phase/sorcery mismatch was accepted");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests = {
            {
                "information key hidden redaction",
                test_information_key_redacts_hidden_identity,
            },
            {
                "information key sensitivities",
                test_information_key_required_sensitivities,
            },
            {
                "descriptor canonical rows",
                test_descriptor_rows_are_canonical_and_unique,
            },
            {
                "actions and Priority context fail closed",
                test_action_set_and_priority_context_fail_closed,
            },
            {
                "repeated construction",
                test_repeated_construction_is_bit_stable,
            },
            {
                "leaf future-library redaction",
                test_leaf_hash_redacts_future_libraries,
            },
            {
                "leaf public-object canonicalization",
                test_leaf_consequence_canonicalizes_public_object_ids,
            },
            {
                "leaf public semantic distinction",
                test_leaf_consequence_preserves_public_semantics,
            },
            {
                "leaf observer-relative canonicalization",
                test_leaf_consequence_is_observer_relative,
            },
            {
                "Priority consequence transition",
                test_canonical_priority_consequence_transition_semantics,
            },
            {
                "indexed seed coordinates",
                test_indexed_seed_uses_every_coordinate,
            },
            {
                "graveyard order exact hidden-safe features",
                test_graveyard_order_features_are_exact_and_hidden_safe,
            },
            {
                "ordered graveyard alias micro corpus",
                test_ordered_graveyard_alias_micro_corpus,
            },
            {
                "terminal values",
                test_terminal_values_draw_and_complement,
            },
            {
                "legacy critic identity",
                test_legacy_contextual_critic_is_bit_identical,
            },
        };
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
        } catch (const std::exception& failure) {
            std::cerr << "FAIL: " << name << ": "
                      << failure.what() << '\n';
        }
    }
    if (passed != tests.size()) {
        std::cerr << passed << '/' << tests.size()
                  << " tests passed\n";
        return 1;
    }
    std::cout << passed << '/' << tests.size()
              << " FQ0 information-set tests passed\n";
    return 0;
}
