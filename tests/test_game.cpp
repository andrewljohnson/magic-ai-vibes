#include "alpha/game.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct TestCase {
    std::string_view name;
    std::function<void()> run;
};

std::vector<TestCase>& tests() {
    static std::vector<TestCase> registered;
    return registered;
}

struct RegisterTest {
    RegisterTest(std::string_view name, std::function<void()> test) {
        tests().push_back({name, std::move(test)});
    }
};

#define TEST(name)                                                            \
    void name();                                                              \
    RegisterTest register_##name(#name, name);                                \
    void name()

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            throw std::runtime_error(std::string("check failed: ") +          \
                                     #expression + " at line " +              \
                                     std::to_string(__LINE__));                \
        }                                                                     \
    } while (false)

std::size_t count_card(const std::vector<alpha::CardId>& cards,
                       alpha::CardId card) {
    return static_cast<std::size_t>(std::count(cards.begin(), cards.end(),
                                               card));
}

bool has_action(const std::vector<alpha::PriorityAction>& actions,
                const alpha::PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) != actions.end();
}

alpha::CreaturePermanent bear(alpha::PermanentId id,
                              bool summoning_sick = false,
                              bool tapped = false) {
    return {
        .id = id,
        .card = alpha::CardId::GrizzlyBears,
        .tapped = tapped,
        .summoning_sick = summoning_sick,
        .damage = 0,
    };
}

alpha::CreaturePermanent creature(alpha::PermanentId id,
                                  alpha::CardId card,
                                  bool summoning_sick = false) {
    return {
        .id = id,
        .card = card,
        .tapped = false,
        .summoning_sick = summoning_sick,
        .damage = 0,
    };
}

TEST(alpha_card_definitions_are_complete) {
    const auto& forest = alpha::card_definition(alpha::CardId::Forest);
    CHECK(forest.name == "Forest");
    CHECK(forest.type == alpha::CardType::Land);

    const auto& mountain = alpha::card_definition(alpha::CardId::Mountain);
    CHECK(mountain.name == "Mountain");
    CHECK(mountain.type == alpha::CardType::Land);

    const auto& bears =
        alpha::card_definition(alpha::CardId::GrizzlyBears);
    CHECK(bears.name == "Grizzly Bears");
    CHECK(bears.type == alpha::CardType::Creature);
    CHECK(bears.cost.generic == 1);
    CHECK(bears.cost.green == 1);
    CHECK(bears.power == 2);
    CHECK(bears.toughness == 2);

    const auto& bolt =
        alpha::card_definition(alpha::CardId::LightningBolt);
    CHECK(bolt.name == "Lightning Bolt");
    CHECK(bolt.type == alpha::CardType::Instant);
    CHECK(bolt.cost.red == 1);
    CHECK(bolt.effect_damage == 3);

    const auto& treefolk =
        alpha::card_definition(alpha::CardId::IronrootTreefolk);
    CHECK(treefolk.name == "Ironroot Treefolk");
    CHECK(treefolk.type == alpha::CardType::Creature);
    CHECK(treefolk.cost.generic == 4);
    CHECK(treefolk.cost.green == 1);
    CHECK(treefolk.power == 3);
    CHECK(treefolk.toughness == 5);

    const auto& elemental =
        alpha::card_definition(alpha::CardId::FireElemental);
    CHECK(elemental.name == "Fire Elemental");
    CHECK(elemental.type == alpha::CardType::Creature);
    CHECK(elemental.cost.generic == 3);
    CHECK(elemental.cost.red == 2);
    CHECK(elemental.power == 5);
    CHECK(elemental.toughness == 4);

    const auto& island = alpha::card_definition(alpha::CardId::Island);
    CHECK(island.name == "Island");
    CHECK(island.type == alpha::CardType::Land);

    const auto& counterspell =
        alpha::card_definition(alpha::CardId::Counterspell);
    CHECK(counterspell.name == "Counterspell");
    CHECK(counterspell.type == alpha::CardType::Instant);
    CHECK(counterspell.cost.blue == 2);

    const auto& water =
        alpha::card_definition(alpha::CardId::WaterElemental);
    CHECK(water.name == "Water Elemental");
    CHECK(water.type == alpha::CardType::Creature);
    CHECK(water.cost.generic == 3);
    CHECK(water.cost.blue == 2);
    CHECK(water.power == 5);
    CHECK(water.toughness == 4);

    const auto& tsunami =
        alpha::card_definition(alpha::CardId::Tsunami);
    CHECK(tsunami.name == "Tsunami");
    CHECK(tsunami.type == alpha::CardType::Sorcery);
    CHECK(tsunami.cost.generic == 3);
    CHECK(tsunami.cost.green == 1);

    const auto& plains = alpha::card_definition(alpha::CardId::Plains);
    CHECK(plains.name == "Plains");
    CHECK(plains.type == alpha::CardType::Land);

    const auto& millstone =
        alpha::card_definition(alpha::CardId::Millstone);
    CHECK(millstone.name == "Millstone");
    CHECK(millstone.type == alpha::CardType::Artifact);
    CHECK(millstone.cost.generic == 2);

    const auto& moat = alpha::card_definition(alpha::CardId::Moat);
    CHECK(moat.name == "Moat");
    CHECK(moat.type == alpha::CardType::Enchantment);
    CHECK(moat.cost.generic == 2);
    CHECK(moat.cost.white == 2);
}

TEST(starting_decks_have_the_requested_cards) {
    const auto green_deck = alpha::green_alpha_deck();
    CHECK(green_deck.size() == 40);
    CHECK(count_card(green_deck, alpha::CardId::Forest) == 18);
    CHECK(count_card(green_deck, alpha::CardId::GrizzlyBears) == 9);
    CHECK(count_card(green_deck, alpha::CardId::IronrootTreefolk) == 12);
    CHECK(count_card(green_deck, alpha::CardId::Tsunami) == 1);

    const auto red_deck = alpha::red_alpha_deck();
    CHECK(red_deck.size() == 40);
    CHECK(count_card(red_deck, alpha::CardId::Mountain) == 18);
    CHECK(count_card(red_deck, alpha::CardId::LightningBolt) == 10);
    CHECK(count_card(red_deck, alpha::CardId::FireElemental) == 12);

    const auto blue_deck = alpha::blue_alpha_deck();
    CHECK(blue_deck.size() == 40);
    CHECK(count_card(blue_deck, alpha::CardId::Island) == 18);
    CHECK(count_card(blue_deck, alpha::CardId::Counterspell) == 14);
    CHECK(count_card(blue_deck, alpha::CardId::WaterElemental) == 8);

    const auto white_deck = alpha::white_control_deck();
    CHECK(white_deck.size() == 40);
    CHECK(count_card(white_deck, alpha::CardId::Plains) == 22);
    CHECK(count_card(white_deck, alpha::CardId::Millstone) == 3);
    CHECK(count_card(white_deck, alpha::CardId::Moat) == 15);
}

TEST(basic_lands_can_be_played_once_per_turn) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::Forest,
                             alpha::CardId::Mountain,
                             alpha::CardId::Island,
                             alpha::CardId::Plains};

    auto actions = alpha::legal_priority_actions(state, 0, true);
    CHECK(has_action(actions,
                     alpha::PriorityAction::play_land(alpha::CardId::Forest)));
    CHECK(has_action(actions,
                     alpha::PriorityAction::play_land(alpha::CardId::Mountain)));
    CHECK(has_action(actions,
                     alpha::PriorityAction::play_land(alpha::CardId::Island)));
    CHECK(has_action(actions,
                     alpha::PriorityAction::play_land(alpha::CardId::Plains)));
    CHECK(alpha::apply_priority_action(
        state, 0, alpha::PriorityAction::play_land(alpha::CardId::Forest),
        true));
    CHECK(state.players[0].lands.size() == 1);
    CHECK(state.players[0].lands[0].card == alpha::CardId::Forest);

    CHECK(!alpha::apply_priority_action(
        state, 0, alpha::PriorityAction::play_land(alpha::CardId::Mountain),
        true));
    alpha::begin_turn(state, 0);
    CHECK(alpha::apply_priority_action(
        state, 0, alpha::PriorityAction::play_land(alpha::CardId::Mountain),
        true));
    CHECK(state.players[0].lands[1].card == alpha::CardId::Mountain);
    alpha::begin_turn(state, 0);
    CHECK(alpha::apply_priority_action(
        state, 0, alpha::PriorityAction::play_land(alpha::CardId::Island),
        true));
    CHECK(state.players[0].lands[2].card == alpha::CardId::Island);
    alpha::begin_turn(state, 0);
    CHECK(alpha::apply_priority_action(
        state, 0, alpha::PriorityAction::play_land(alpha::CardId::Plains),
        true));
    CHECK(state.players[0].lands[3].card == alpha::CardId::Plains);
}

TEST(grizzly_bears_costs_one_and_a_green_and_has_summoning_sickness) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::GrizzlyBears};
    state.players[0].lands = {
        {.card = alpha::CardId::Forest, .tapped = false},
        {.card = alpha::CardId::Forest, .tapped = false},
    };

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_creature(alpha::CardId::GrizzlyBears),
        true));
    CHECK(state.players[0].hand.empty());
    CHECK(state.players[0].creatures.empty());
    CHECK(state.stack.size() == 1);
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].summoning_sick);
    CHECK(state.players[0].lands[0].tapped);
    CHECK(state.players[0].lands[1].tapped);

    const auto bear_id = state.players[0].creatures[0].id;
    CHECK(!alpha::resolve_combat(state, 0, {bear_id}, {}));
    alpha::begin_turn(state, 0);
    CHECK(alpha::resolve_combat(state, 0, {bear_id}, {}));
    CHECK(state.players[1].life == 18);
}

TEST(grizzly_bears_requires_green_mana) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::GrizzlyBears};
    state.players[0].lands = {
        {.card = alpha::CardId::Mountain, .tapped = false},
        {.card = alpha::CardId::Mountain, .tapped = false},
    };
    CHECK(!has_action(alpha::legal_priority_actions(state, 0, true),
                      alpha::PriorityAction::cast_creature(
                          alpha::CardId::GrizzlyBears)));
}

TEST(ironroot_treefolk_costs_four_and_a_green) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::IronrootTreefolk};
    state.players[0].lands = {
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
    };

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_creature(
            alpha::CardId::IronrootTreefolk),
        true));
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].card ==
          alpha::CardId::IronrootTreefolk);
    CHECK(std::all_of(state.players[0].lands.begin(),
                      state.players[0].lands.end(),
                      [](const alpha::LandPermanent& land) {
                          return land.tapped;
                      }));
}

TEST(fire_elemental_requires_two_red_mana) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::FireElemental};
    state.players[0].lands = {
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Mountain},
    };
    CHECK(!has_action(
        alpha::legal_priority_actions(state, 0, true),
        alpha::PriorityAction::cast_creature(alpha::CardId::FireElemental)));

    state.players[0].lands[1].card = alpha::CardId::Mountain;
    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_creature(alpha::CardId::FireElemental),
        true));
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures[0].card ==
          alpha::CardId::FireElemental);
}

TEST(fire_elemental_defeats_ironroot_treefolk_in_combat) {
    alpha::GameState state;
    state.players[0].creatures = {
        creature(1, alpha::CardId::FireElemental)};
    state.players[1].creatures = {
        creature(2, alpha::CardId::IronrootTreefolk)};

    CHECK(alpha::resolve_combat(state, 0, {1}, {{1, 2}}));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[1].creatures.empty());
}

TEST(water_elemental_requires_two_blue_mana) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::WaterElemental};
    state.players[0].lands = {
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
    };

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_creature(
            alpha::CardId::WaterElemental),
        true));
    CHECK(state.stack.size() == 1);
    CHECK(state.players[0].creatures.empty());
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].card ==
          alpha::CardId::WaterElemental);
}

TEST(lightning_bolt_can_damage_either_player) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = alpha::CardId::Mountain, .tapped = false},
    };
    const auto actions = alpha::legal_priority_actions(state, 0, true);
    CHECK(has_action(
        actions, alpha::PriorityAction::cast_lightning_bolt(
                     alpha::Target::player_target(0))));
    CHECK(has_action(
        actions, alpha::PriorityAction::cast_lightning_bolt(
                     alpha::Target::player_target(1))));

    CHECK(alpha::apply_priority_action(
        state, 0, alpha::PriorityAction::cast_lightning_bolt(
                      alpha::Target::player_target(1)),
        true));
    CHECK(state.players[1].life == 20);
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[1].life == 17);
    CHECK(state.players[0].lands[0].tapped);
    CHECK(count_card(state.players[0].graveyard,
                     alpha::CardId::LightningBolt) == 1);
}

TEST(lightning_bolt_kills_a_grizzly_bears) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = alpha::CardId::Mountain, .tapped = false},
    };
    state.players[1].creatures = {bear(42)};

    const auto action = alpha::PriorityAction::cast_lightning_bolt(
        alpha::Target::creature_target(1, 42));
    CHECK(has_action(alpha::legal_priority_actions(state, 0, true), action));
    CHECK(alpha::apply_priority_action(state, 0, action, true));
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[1].creatures.empty());
    CHECK(count_card(state.players[1].graveyard,
                     alpha::CardId::GrizzlyBears) == 1);
}

TEST(lightning_bolt_does_not_kill_an_ironroot_treefolk) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = alpha::CardId::Mountain, .tapped = false},
    };
    state.players[1].creatures = {
        creature(42, alpha::CardId::IronrootTreefolk)};

    const auto action = alpha::PriorityAction::cast_lightning_bolt(
        alpha::Target::creature_target(1, 42));
    CHECK(alpha::apply_priority_action(state, 0, action, true));
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[1].creatures.size() == 1);
    CHECK(state.players[1].creatures[0].damage == 3);

    alpha::cleanup_turn(state);
    CHECK(state.players[1].creatures[0].damage == 0);
}

TEST(two_consecutive_passes_resolve_the_stack_then_end_the_window) {
    alpha::GameState state;
    state.active_player = 0;
    state.players[0].hand = {alpha::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = alpha::CardId::Mountain},
    };
    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_lightning_bolt(
            alpha::Target::player_target(1)),
        true));

    alpha::PriorityState priority = {
        .player = 0,
        .consecutive_passes = 0,
    };
    CHECK(alpha::pass_priority(state, priority) ==
          alpha::PriorityPassResult::Passed);
    CHECK(priority.player == 1);
    CHECK(state.stack.size() == 1);
    CHECK(state.players[1].life == 20);

    CHECK(alpha::pass_priority(state, priority) ==
          alpha::PriorityPassResult::StackObjectResolved);
    CHECK(priority.player == state.active_player);
    CHECK(priority.consecutive_passes == 0);
    CHECK(state.stack.empty());
    CHECK(state.players[1].life == 17);

    CHECK(alpha::pass_priority(state, priority) ==
          alpha::PriorityPassResult::Passed);
    CHECK(alpha::pass_priority(state, priority) ==
          alpha::PriorityPassResult::WindowEnded);
}

TEST(counterspell_counters_a_creature_spell_on_the_stack) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::GrizzlyBears};
    state.players[0].lands = {
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
    };
    state.players[1].hand = {alpha::CardId::Counterspell};
    state.players[1].lands = {
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
    };

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_creature(
            alpha::CardId::GrizzlyBears),
        true));
    const auto bear_spell = state.stack.back().id;
    const auto counter =
        alpha::PriorityAction::cast_counterspell(bear_spell);
    CHECK(has_action(
        alpha::legal_priority_actions(state, 1, false), counter));
    CHECK(alpha::apply_priority_action(state, 1, counter, false));
    CHECK(state.stack.size() == 2);

    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.stack.empty());
    CHECK(state.players[0].creatures.empty());
    CHECK(count_card(state.players[0].graveyard,
                     alpha::CardId::GrizzlyBears) == 1);
    CHECK(count_card(state.players[1].graveyard,
                     alpha::CardId::Counterspell) == 1);
    CHECK(state.stats[1].spells_countered == 1);
}

TEST(countering_a_counterspell_leaves_the_original_spell_to_resolve) {
    alpha::GameState state;
    state.players[0].hand = {
        alpha::CardId::LightningBolt,
        alpha::CardId::Counterspell,
    };
    state.players[0].lands = {
        {.card = alpha::CardId::Mountain},
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
    };
    state.players[1].hand = {alpha::CardId::Counterspell};
    state.players[1].lands = {
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
    };

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_lightning_bolt(
            alpha::Target::player_target(1)),
        true));
    const auto bolt_spell = state.stack.back().id;
    CHECK(alpha::apply_priority_action(
        state, 1,
        alpha::PriorityAction::cast_counterspell(bolt_spell), false));
    const auto first_counterspell = state.stack.back().id;
    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_counterspell(first_counterspell),
        false));

    CHECK(state.stack.size() == 3);
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.stack.size() == 1);
    CHECK(state.stack.back().card == alpha::CardId::LightningBolt);
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.stack.empty());
    CHECK(state.players[1].life == 17);
    CHECK(state.stats[0].spells_countered == 1);
    CHECK(state.stats[1].spells_countered == 0);
}

TEST(tsunami_uses_the_stack_and_destroys_all_islands) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::Tsunami};
    state.players[0].lands = {
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
        {.card = alpha::CardId::Forest},
    };
    state.players[1].lands = {
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Mountain},
        {.card = alpha::CardId::Island},
    };

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_sorcery(alpha::CardId::Tsunami),
        true));
    CHECK(state.stack.size() == 1);
    CHECK(state.players[1].lands.size() == 3);
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[1].lands.size() == 1);
    CHECK(state.players[1].lands[0].card == alpha::CardId::Mountain);
    CHECK(count_card(state.players[1].graveyard,
                     alpha::CardId::Island) == 2);
    CHECK(count_card(state.players[0].graveyard,
                     alpha::CardId::Tsunami) == 1);
}

TEST(millstone_spell_and_activated_ability_use_the_stack) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::Millstone};
    state.players[0].lands = {
        {.card = alpha::CardId::Plains},
        {.card = alpha::CardId::Plains},
        {.card = alpha::CardId::Plains},
        {.card = alpha::CardId::Plains},
    };

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_artifact(alpha::CardId::Millstone),
        true));
    CHECK(state.players[0].artifacts.empty());
    CHECK(state.stack.size() == 1);
    CHECK(state.stack.back().kind == alpha::StackObjectKind::Spell);
    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[0].artifacts.size() == 1);

    alpha::begin_turn(state, 0);
    state.players[1].library = {
        alpha::CardId::Forest,
        alpha::CardId::GrizzlyBears,
        alpha::CardId::Mountain,
    };
    state.players[1].hand = {alpha::CardId::Counterspell};
    state.players[1].lands = {
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
    };

    const auto millstone_id = state.players[0].artifacts[0].id;
    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::activate_millstone(
            millstone_id, alpha::Target::player_target(1)),
        false));
    CHECK(state.players[0].artifacts[0].tapped);
    CHECK(state.stack.size() == 1);
    CHECK(state.stack.back().kind ==
          alpha::StackObjectKind::ActivatedAbility);
    CHECK(state.players[1].library.size() == 3);

    const auto counter_ability =
        alpha::PriorityAction::cast_counterspell(state.stack.back().id);
    CHECK(!has_action(
        alpha::legal_priority_actions(state, 1, false),
        counter_ability));

    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[1].library.size() == 1);
    CHECK(state.players[1].graveyard.size() == 2);
    CHECK(state.stats[0].cards_milled == 2);
}

TEST(moat_is_a_counterable_spell_and_stops_ground_attackers) {
    alpha::GameState state;
    state.players[0].hand = {alpha::CardId::Moat};
    state.players[0].lands = {
        {.card = alpha::CardId::Plains},
        {.card = alpha::CardId::Plains},
        {.card = alpha::CardId::Plains},
        {.card = alpha::CardId::Plains},
    };
    state.players[1].hand = {alpha::CardId::Counterspell};
    state.players[1].lands = {
        {.card = alpha::CardId::Island},
        {.card = alpha::CardId::Island},
    };
    state.players[1].creatures = {bear(99)};

    CHECK(alpha::apply_priority_action(
        state, 0,
        alpha::PriorityAction::cast_enchantment(alpha::CardId::Moat),
        true));
    CHECK(state.players[0].enchantments.empty());
    const auto moat_spell = state.stack.back().id;
    CHECK(has_action(
        alpha::legal_priority_actions(state, 1, false),
        alpha::PriorityAction::cast_counterspell(moat_spell)));

    CHECK(alpha::resolve_top_of_stack(state));
    CHECK(state.players[0].enchantments.size() == 1);
    CHECK(state.players[0].enchantments[0] == alpha::CardId::Moat);
    CHECK(!alpha::resolve_combat(state, 1, {99}, {}));
}

TEST(grizzly_bears_trade_in_combat) {
    alpha::GameState state;
    state.players[0].creatures = {bear(1)};
    state.players[1].creatures = {bear(2)};
    CHECK(alpha::resolve_combat(state, 0, {1}, {{1, 2}}));
    CHECK(state.players[0].creatures.empty());
    CHECK(state.players[1].creatures.empty());
    CHECK(state.players[0].life == 20);
    CHECK(state.players[1].life == 20);
}

TEST(tapped_creatures_cannot_block) {
    alpha::GameState state;
    state.players[0].creatures = {bear(1)};
    state.players[1].creatures = {bear(2, false, true)};
    CHECK(!alpha::resolve_combat(state, 0, {1}, {{1, 2}}));
}

TEST(one_hundred_seeded_games_complete) {
    const auto result = alpha::run_simulation(100, 0xA11FAULL);
    CHECK(result.games == 100);
    CHECK(result.decks[0].games == 100);
    CHECK(result.decks[1].games == 100);
    CHECK(result.decks[0].wins + result.decks[1].wins + result.draws ==
          100);
    CHECK(result.decks[0].wins + result.decks[0].losses +
              result.decks[0].draws ==
          100);
    CHECK(result.decks[0].on_play_games +
              result.decks[0].on_draw_games ==
          100);
    CHECK(result.decks[1].on_play_games +
              result.decks[1].on_draw_games ==
          100);
    CHECK(result.decks[0].on_play_games ==
          result.decks[1].on_draw_games);
    CHECK(result.decks[1].on_play_games ==
          result.decks[0].on_draw_games);
    CHECK(result.decks[0].total_cards_drawn >= 700);
    CHECK(result.decks[1].total_cards_drawn >= 700);
    CHECK(result.decks[0].total_lands_played > 0);
    CHECK(result.decks[1].total_lands_played > 0);
    CHECK(result.decks[0].total_spells_cast > 0);
    CHECK(result.decks[1].total_spells_cast > 0);
    CHECK(result.decks[0].total_damage_to_opponent > 0);
    CHECK(result.decks[1].total_damage_to_opponent > 0);
    CHECK(result.total_turns > 0);
    CHECK(result.life_total_finishes + result.empty_library_finishes +
              result.turn_limit_draws ==
          100);
    CHECK(result.turn_limit_draws == 0);
}

TEST(four_deck_tournament_runs_every_pairing) {
    const auto result = alpha::run_tournament(100, 0xC0FFEEULL);
    CHECK(result.games_per_matchup == 100);
    CHECK(result.total_games == 600);
    for (const auto& matchup : result.matchups) {
        CHECK(matchup.result.games == 100);
        CHECK(matchup.result.decks[0].wins +
                  matchup.result.decks[1].wins +
                  matchup.result.draws ==
              100);
    }
    for (const auto& deck : result.decks) {
        CHECK(deck.games == 300);
        CHECK(deck.wins + deck.losses + deck.draws == 300);
    }
    CHECK(result.decks[static_cast<std::size_t>(
              alpha::DeckId::Blue)]
              .total_spells_countered >
          0);
    CHECK(result.decks[static_cast<std::size_t>(
              alpha::DeckId::White)]
              .total_cards_milled >
          0);
    CHECK(result.life_total_finishes + result.empty_library_finishes +
              result.turn_limit_draws ==
          600);
}

TEST(random_bot_records_decisions_without_monte_carlo_rollouts) {
    const auto result = alpha::run_simulation(20, 0xBAD5EEDULL);
    const auto& random =
        result.bots[static_cast<std::size_t>(alpha::BotKind::Random)];
    const auto& monte_carlo =
        result.bots[static_cast<std::size_t>(
            alpha::BotKind::MonteCarlo)];

    CHECK(random.games == 40);
    CHECK(random.wins + random.losses + random.draws == 40);
    CHECK(random.total_decisions > 0);
    CHECK(random.total_rollouts == 0);
    CHECK(monte_carlo.games == 0);
}

TEST(monte_carlo_bot_runs_complete_random_continuations) {
    alpha::GameConfig config;
    config.bots[0] = {
        .kind = alpha::BotKind::MonteCarlo,
        .rollouts_per_action = 2,
    };
    config.bots[1] = {
        .kind = alpha::BotKind::Random,
        .rollouts_per_action = 2,
    };

    const auto result =
        alpha::run_simulation(5, 0xC001D00DULL, config);
    const auto& random =
        result.bots[static_cast<std::size_t>(alpha::BotKind::Random)];
    const auto& monte_carlo =
        result.bots[static_cast<std::size_t>(
            alpha::BotKind::MonteCarlo)];

    CHECK(random.games == 5);
    CHECK(monte_carlo.games == 5);
    CHECK(monte_carlo.total_decisions > 0);
    CHECK(monte_carlo.total_rollouts > 0);
    CHECK(monte_carlo.total_rollouts >=
          monte_carlo.total_decisions * 4);
    CHECK(random.total_rollouts == 0);
}

TEST(mixed_tournament_rotates_all_five_bot_kinds) {
    const alpha::TournamentConfig bots = {
        .bot_field = alpha::BotField::Mixed,
        .monte_carlo_rollouts = 1,
        .deep_monte_carlo_rollouts = 4,
        .learned_training_games = 20,
    };
    const auto result =
        alpha::run_tournament(25, 0xDEC1DEULL, {}, bots);
    const auto& random =
        result.bots[static_cast<std::size_t>(alpha::BotKind::Random)];
    const auto& monte_carlo =
        result.bots[static_cast<std::size_t>(
            alpha::BotKind::MonteCarlo)];
    const auto& deep_monte_carlo =
        result.bots[static_cast<std::size_t>(
            alpha::BotKind::DeepMonteCarlo)];
    const auto& handcrafted =
        result.bots[static_cast<std::size_t>(
            alpha::BotKind::Handcrafted)];
    const auto& learned =
        result.bots[static_cast<std::size_t>(
            alpha::BotKind::Learned)];

    CHECK(result.total_games == 150);
    CHECK(random.games == 60);
    CHECK(monte_carlo.games == 60);
    CHECK(deep_monte_carlo.games == 60);
    CHECK(handcrafted.games == 60);
    CHECK(learned.games == 60);
    CHECK(random.wins + random.losses + random.draws == random.games);
    CHECK(monte_carlo.wins + monte_carlo.losses +
              monte_carlo.draws ==
          monte_carlo.games);
    CHECK(deep_monte_carlo.wins + deep_monte_carlo.losses +
              deep_monte_carlo.draws ==
          deep_monte_carlo.games);
    CHECK(handcrafted.wins + handcrafted.losses +
              handcrafted.draws ==
          handcrafted.games);
    CHECK(learned.wins + learned.losses + learned.draws ==
          learned.games);
    CHECK(random.total_rollouts == 0);
    CHECK(monte_carlo.total_decisions > 0);
    CHECK(monte_carlo.total_rollouts > 0);
    CHECK(deep_monte_carlo.total_decisions > 0);
    CHECK(deep_monte_carlo.total_rollouts > 0);
    CHECK(deep_monte_carlo.average_rollouts_per_decision() >
          monte_carlo.average_rollouts_per_decision());
    CHECK(handcrafted.total_decisions > 0);
    CHECK(handcrafted.total_rollouts == 0);
    CHECK(learned.total_decisions > 0);
    CHECK(learned.total_rollouts > 0);
    for (const auto& matchup : result.bot_matchups) {
        CHECK(matchup.games == 12);
        CHECK(matchup.first_wins + matchup.second_wins +
                  matchup.draws ==
              matchup.games);
    }
    for (std::size_t deck = 0; deck < result.decks.size(); ++deck) {
        std::size_t deck_bot_games = 0;
        for (const auto& deck_bot : result.deck_bots[deck]) {
            CHECK(deck_bot.games == 15);
            CHECK(deck_bot.wins + deck_bot.losses + deck_bot.draws ==
                  deck_bot.games);
            deck_bot_games += deck_bot.games;
        }
        CHECK(deck_bot_games == result.decks[deck].games);
    }

    const auto repeated =
        alpha::run_tournament(25, 0xDEC1DEULL, {}, bots);
    CHECK(repeated.bots[static_cast<std::size_t>(
              alpha::BotKind::Random)]
              .wins == random.wins);
    CHECK(repeated.bots[static_cast<std::size_t>(
              alpha::BotKind::MonteCarlo)]
              .wins == monte_carlo.wins);
    CHECK(repeated.bots[static_cast<std::size_t>(
              alpha::BotKind::MonteCarlo)]
              .total_rollouts == monte_carlo.total_rollouts);
    CHECK(repeated.bots[static_cast<std::size_t>(
              alpha::BotKind::DeepMonteCarlo)]
              .total_rollouts ==
          deep_monte_carlo.total_rollouts);
    CHECK(repeated.bots[static_cast<std::size_t>(
              alpha::BotKind::Handcrafted)]
              .wins == handcrafted.wins);
    CHECK(repeated.bots[static_cast<std::size_t>(
              alpha::BotKind::Learned)]
              .wins == learned.wins);
    CHECK(repeated.bot_matchups.back().second_wins ==
          result.bot_matchups.back().second_wins);
}

TEST(bot_benchmark_balances_decks_seats_and_play_draw) {
    const alpha::BotConfig challenger = {
        .kind = alpha::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const alpha::BotConfig baseline = {
        .kind = alpha::BotKind::Random,
        .rollouts_per_action = 1,
    };
    const auto result = alpha::run_bot_benchmark(
        2, 0xB07B07ULL, challenger, baseline);

    CHECK(result.total_games == 80);
    CHECK(result.challenger_stats.games == 80);
    CHECK(result.baseline_stats.games == 80);
    CHECK(result.challenger_stats.wins +
              result.challenger_stats.losses +
              result.challenger_stats.draws ==
          80);
    for (std::size_t deck = 0;
         deck < result.challenger_decks.size(); ++deck) {
        CHECK(result.challenger_decks[deck].games == 20);
        CHECK(result.baseline_decks[deck].games == 20);
        CHECK(result.challenger_decks[deck].on_play_games == 10);
        CHECK(result.challenger_decks[deck].on_draw_games == 10);
        CHECK(result.baseline_decks[deck].on_play_games == 10);
        CHECK(result.baseline_decks[deck].on_draw_games == 10);
    }
    CHECK(result.confidence_low_95() >= 0.0);
    CHECK(result.confidence_high_95() <= 100.0);
    CHECK(result.confidence_low_95() <=
          result.challenger_win_rate());
    CHECK(result.confidence_high_95() >=
          result.challenger_win_rate());
}

TEST(handcrafted_bot_beats_monte_carlo_in_seeded_benchmark) {
    const alpha::BotConfig challenger = {
        .kind = alpha::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const alpha::BotConfig baseline = {
        .kind = alpha::BotKind::MonteCarlo,
        .rollouts_per_action = 2,
    };
    const auto result = alpha::run_bot_benchmark(
        5, 424242, challenger, baseline);

    CHECK(result.total_games == 200);
    CHECK(result.challenger_win_rate() > 60.0);
    CHECK(result.challenger_is_better_95());
    CHECK(result.challenger_stats.total_rollouts == 0);
    CHECK(result.baseline_stats.total_rollouts > 0);
}

TEST(handcrafted_bot_beats_deep_monte_carlo_in_seeded_benchmark) {
    const alpha::BotConfig challenger = {
        .kind = alpha::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const alpha::BotConfig baseline = {
        .kind = alpha::BotKind::DeepMonteCarlo,
        .rollouts_per_action = 8,
    };
    const auto result = alpha::run_bot_benchmark(
        2, 424242, challenger, baseline);

    CHECK(result.total_games == 80);
    CHECK(result.challenger_is_better_95());
    CHECK(result.challenger_stats.total_rollouts == 0);
    CHECK(result.baseline_stats.average_rollouts() > 500.0);
}

TEST(learned_value_bot_beats_monte_carlo_without_handcrafted_values) {
    const alpha::BotConfig challenger = {
        .kind = alpha::BotKind::Learned,
        .rollouts_per_action = 1,
        .training_games = 200,
    };
    const alpha::BotConfig baseline = {
        .kind = alpha::BotKind::MonteCarlo,
        .rollouts_per_action = 2,
    };
    const auto result = alpha::run_bot_benchmark(
        5, 424242, challenger, baseline);

    CHECK(result.total_games == 200);
    CHECK(result.challenger_win_rate() > 70.0);
    CHECK(result.challenger_is_better_95());
    CHECK(result.challenger_stats.total_rollouts > 0);
    CHECK(result.baseline_stats.total_rollouts > 0);
}

TEST(deck_evolution_uses_the_metagame_card_pool_and_is_deterministic) {
    const alpha::DeckEvolutionConfig config = {
        .generations = 2,
        .population = 4,
        .repetitions_per_opponent = 1,
        .pilot =
            {
                .kind = alpha::BotKind::Handcrafted,
                .rollouts_per_action = 1,
            },
    };
    const auto first = alpha::evolve_deck(config, 0xE701EULL);
    const auto repeated = alpha::evolve_deck(config, 0xE701EULL);

    CHECK(first.generation_best_win_rates.size() == 2);
    CHECK(first.best.cards.size() == 40);
    CHECK(first.best.total.games == 16);
    CHECK(first.best.total.wins + first.best.total.losses +
              first.best.total.draws ==
          16);
    for (const auto& matchup : first.best.by_opponent) {
        CHECK(matchup.games == 4);
    }
    for (const alpha::CardId card : first.best.cards) {
        CHECK(static_cast<std::size_t>(card) <=
              static_cast<std::size_t>(alpha::CardId::Moat));
    }
    CHECK(first.best.cards == repeated.best.cards);
    CHECK(first.best.total.wins == repeated.best.total.wins);
    CHECK(first.generation_best_win_rates ==
          repeated.generation_best_win_rates);
}

TEST(all_six_pairings_have_a_balanced_seeded_matchup) {
    const auto result = alpha::run_tournament(10000, 424242);
    for (const auto& matchup : result.matchups) {
        CHECK(matchup.result.decks[0].win_rate() >= 45.0);
        CHECK(matchup.result.decks[0].win_rate() <= 55.0);
        CHECK(matchup.result.decks[1].win_rate() >= 45.0);
        CHECK(matchup.result.decks[1].win_rate() <= 55.0);
    }
}

} // namespace

int main() {
    std::size_t failures = 0;
    for (const auto& test : tests()) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "[FAIL] " << test.name << ": " << error.what()
                      << '\n';
        }
    }

    std::cout << '\n'
              << tests().size() - failures << '/' << tests().size()
              << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
