#include "old_school/fq0_information_set.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
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
                "indexed seed coordinates",
                test_indexed_seed_uses_every_coordinate,
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
