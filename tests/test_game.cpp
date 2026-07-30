#include "old_school/game.hpp"
#include "old_school/interactive.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

static_assert(
    sizeof(double) == sizeof(std::uint64_t) &&
        std::numeric_limits<double>::is_iec559);

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

std::size_t count_card(const std::vector<old_school::CardId>& cards,
                       old_school::CardId card) {
    return static_cast<std::size_t>(std::count(cards.begin(), cards.end(),
                                               card));
}

bool has_action(const std::vector<old_school::PriorityAction>& actions,
                const old_school::PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) != actions.end();
}

old_school::CreaturePermanent bear(old_school::PermanentId id,
                              bool summoning_sick = false,
                              bool tapped = false) {
    return {
        .id = id,
        .card = old_school::CardId::GrizzlyBears,
        .tapped = tapped,
        .summoning_sick = summoning_sick,
        .damage = 0,
    };
}

old_school::CreaturePermanent creature(old_school::PermanentId id,
                                  old_school::CardId card,
                                  bool summoning_sick = false) {
    return {
        .id = id,
        .card = card,
        .tapped = false,
        .summoning_sick = summoning_sick,
        .damage = 0,
    };
}

void remove_fixture_card(std::vector<old_school::CardId>& cards,
                         old_school::CardId card) {
    const auto position = std::find(cards.begin(), cards.end(), card);
    if (position == cards.end()) {
        throw std::runtime_error("fixture card is missing from deck");
    }
    cards.erase(position);
}

struct DeterminizationFixture {
    old_school::GameState state;
    std::array<std::vector<old_school::CardId>, 2> decks;
};

DeterminizationFixture determinization_fixture() {
    DeterminizationFixture fixture{
        .state = {},
        .decks = {
            old_school::white_control_deck(),
            old_school::blue_deck(),
        },
    };
    auto& state = fixture.state;
    state.active_player = 1;
    state.turn_number = 9;

    state.players[0].hand = {
        old_school::CardId::Plains,
        old_school::CardId::Moat,
        old_school::CardId::Plains,
    };
    state.players[0].graveyard = {old_school::CardId::Moat};
    state.players[0].exile = {old_school::CardId::Moat};
    state.players[0].lands = {
        {.card = old_school::CardId::Plains, .tapped = true},
    };
    state.players[0].artifacts = {
        {.id = 41, .card = old_school::CardId::Millstone, .tapped = true},
    };

    state.players[1].graveyard = {
        old_school::CardId::Counterspell,
        old_school::CardId::Island,
    };
    state.players[1].exile = {old_school::CardId::Island};
    state.players[1].lands = {
        {.card = old_school::CardId::Island, .tapped = true},
    };
    state.players[1].creatures = {
        creature(51, old_school::CardId::AirElemental),
    };

    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 70,
            .card = old_school::CardId::AirElemental,
            .controller = 1,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        },
        {
            .kind = old_school::StackObjectKind::ActivatedAbility,
            .id = 71,
            .card = old_school::CardId::Millstone,
            .controller = 0,
            .target = old_school::Target::player_target(1),
            .spell_target = std::nullopt,
        },
    };

    auto observer_library = fixture.decks[0];
    for (const old_school::CardId card : state.players[0].hand) {
        remove_fixture_card(observer_library, card);
    }
    remove_fixture_card(observer_library, old_school::CardId::Moat);
    remove_fixture_card(observer_library, old_school::CardId::Moat);
    remove_fixture_card(observer_library, old_school::CardId::Plains);
    remove_fixture_card(observer_library, old_school::CardId::Millstone);
    state.players[0].library = std::move(observer_library);

    auto opponent_hidden = fixture.decks[1];
    remove_fixture_card(opponent_hidden, old_school::CardId::Counterspell);
    remove_fixture_card(opponent_hidden, old_school::CardId::Island);
    remove_fixture_card(opponent_hidden, old_school::CardId::Island);
    remove_fixture_card(opponent_hidden, old_school::CardId::Island);
    remove_fixture_card(opponent_hidden, old_school::CardId::AirElemental);
    // A spell stack object is another physical Air Elemental.
    remove_fixture_card(opponent_hidden, old_school::CardId::AirElemental);
    constexpr std::size_t kOpponentHandSize = 5;
    state.players[1].hand.assign(
        opponent_hidden.begin(),
        opponent_hidden.begin() +
            static_cast<std::ptrdiff_t>(kOpponentHandSize));
    state.players[1].library.assign(
        opponent_hidden.begin() +
            static_cast<std::ptrdiff_t>(kOpponentHandSize),
        opponent_hidden.end());
    return fixture;
}

std::vector<old_school::CardId>
physical_cards(const old_school::GameState& state, std::size_t player) {
    std::vector<old_school::CardId> cards;
    const auto& player_state = state.players[player];
    cards.insert(cards.end(), player_state.library.begin(),
                 player_state.library.end());
    cards.insert(cards.end(), player_state.hand.begin(),
                 player_state.hand.end());
    cards.insert(cards.end(), player_state.graveyard.begin(),
                 player_state.graveyard.end());
    cards.insert(cards.end(), player_state.exile.begin(),
                 player_state.exile.end());
    for (const auto& land : player_state.lands) {
        cards.push_back(land.card);
    }
    for (const auto& permanent : player_state.creatures) {
        cards.push_back(permanent.card);
    }
    for (const auto& permanent : player_state.artifacts) {
        cards.push_back(permanent.card);
    }
    cards.insert(cards.end(), player_state.enchantments.begin(),
                 player_state.enchantments.end());
    for (const auto& object : state.stack) {
        if (object.controller == player &&
            object.kind == old_school::StackObjectKind::Spell) {
            cards.push_back(object.card);
        }
    }
    std::sort(cards.begin(), cards.end());
    return cards;
}

bool throws_with_text(
    const std::function<void()>& operation,
    std::string_view wanted) {
    try {
        operation();
    } catch (const std::exception& error) {
        return std::string_view(error.what()).find(wanted) !=
               std::string_view::npos;
    }
    return false;
}

old_school::GameState hidden_repartition(
    const old_school::GameState& state, std::size_t observer) {
    old_school::GameState changed = state;
    std::reverse(changed.players[observer].library.begin(),
                 changed.players[observer].library.end());
    const std::size_t opponent = 1 - observer;
    auto& hand = changed.players[opponent].hand;
    auto& library = changed.players[opponent].library;
    if (!hand.empty() && !library.empty()) {
        const auto different = std::find_if(
            library.begin(), library.end(),
            [&](old_school::CardId card) {
                return card != hand.front();
            });
        if (different != library.end()) {
            std::iter_swap(hand.begin(), different);
        }
    }
    std::reverse(hand.begin(), hand.end());
    std::reverse(library.begin(), library.end());
    return changed;
}

DeterminizationFixture attack_evaluation_fixture(
    old_school::CardId blocker_card) {
    const bool red_blocker =
        blocker_card == old_school::CardId::FireElemental;
    DeterminizationFixture fixture{
        .state = {},
        .decks = {
            old_school::green_deck(),
            red_blocker ? old_school::red_deck()
                        : old_school::green_deck(),
        },
    };
    auto& state = fixture.state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 11;
    state.players[0].land_played_this_turn = true;
    state.players[0].lands.assign(
        5, old_school::LandPermanent{
               .card = old_school::CardId::Forest,
               .tapped = false,
           });
    state.players[0].creatures = {
        creature(1, old_school::CardId::IronrootTreefolk),
    };
    state.players[1].lands.assign(
        5, old_school::LandPermanent{
               .card = red_blocker ? old_school::CardId::Mountain
                                   : old_school::CardId::Forest,
               .tapped = false,
           });
    state.players[1].creatures = {
        creature(2, blocker_card),
    };

    for (std::size_t player = 0; player < fixture.decks.size();
         ++player) {
        std::vector<old_school::CardId> hidden = fixture.decks[player];
        for (const auto& land : state.players[player].lands) {
            remove_fixture_card(hidden, land.card);
        }
        for (const auto& permanent :
             state.players[player].creatures) {
            remove_fixture_card(hidden, permanent.card);
        }
        if (player == 1) {
            state.players[player].hand.assign(
                hidden.begin(), hidden.begin() + 2);
            hidden.erase(hidden.begin(), hidden.begin() + 2);
        }
        state.players[player].library = std::move(hidden);
    }
    return fixture;
}

TEST(old_school_card_definitions_are_complete) {
    const auto& forest = old_school::card_definition(old_school::CardId::Forest);
    CHECK(forest.name == "Forest");
    CHECK(forest.type == old_school::CardType::Land);

    const auto& mountain = old_school::card_definition(old_school::CardId::Mountain);
    CHECK(mountain.name == "Mountain");
    CHECK(mountain.type == old_school::CardType::Land);

    const auto& bears =
        old_school::card_definition(old_school::CardId::GrizzlyBears);
    CHECK(bears.name == "Grizzly Bears");
    CHECK(bears.type == old_school::CardType::Creature);
    CHECK(bears.cost.generic == 1);
    CHECK(bears.cost.green == 1);
    CHECK(bears.power == 2);
    CHECK(bears.toughness == 2);

    const auto& bolt =
        old_school::card_definition(old_school::CardId::LightningBolt);
    CHECK(bolt.name == "Lightning Bolt");
    CHECK(bolt.type == old_school::CardType::Instant);
    CHECK(bolt.cost.red == 1);
    CHECK(bolt.effect_damage == 3);

    const auto& treefolk =
        old_school::card_definition(old_school::CardId::IronrootTreefolk);
    CHECK(treefolk.name == "Ironroot Treefolk");
    CHECK(treefolk.type == old_school::CardType::Creature);
    CHECK(treefolk.cost.generic == 4);
    CHECK(treefolk.cost.green == 1);
    CHECK(treefolk.power == 3);
    CHECK(treefolk.toughness == 5);

    const auto& elemental =
        old_school::card_definition(old_school::CardId::FireElemental);
    CHECK(elemental.name == "Fire Elemental");
    CHECK(elemental.type == old_school::CardType::Creature);
    CHECK(elemental.cost.generic == 3);
    CHECK(elemental.cost.red == 2);
    CHECK(elemental.power == 5);
    CHECK(elemental.toughness == 4);

    const auto& island = old_school::card_definition(old_school::CardId::Island);
    CHECK(island.name == "Island");
    CHECK(island.type == old_school::CardType::Land);

    const auto& counterspell =
        old_school::card_definition(old_school::CardId::Counterspell);
    CHECK(counterspell.name == "Counterspell");
    CHECK(counterspell.type == old_school::CardType::Instant);
    CHECK(counterspell.cost.blue == 2);

    const auto& water =
        old_school::card_definition(old_school::CardId::WaterElemental);
    CHECK(water.name == "Water Elemental");
    CHECK(water.type == old_school::CardType::Creature);
    CHECK(water.cost.generic == 3);
    CHECK(water.cost.blue == 2);
    CHECK(water.power == 5);
    CHECK(water.toughness == 4);

    const auto& tsunami =
        old_school::card_definition(old_school::CardId::Tsunami);
    CHECK(tsunami.name == "Tsunami");
    CHECK(tsunami.type == old_school::CardType::Sorcery);
    CHECK(tsunami.cost.generic == 3);
    CHECK(tsunami.cost.green == 1);

    const auto& plains = old_school::card_definition(old_school::CardId::Plains);
    CHECK(plains.name == "Plains");
    CHECK(plains.type == old_school::CardType::Land);

    const auto& millstone =
        old_school::card_definition(old_school::CardId::Millstone);
    CHECK(millstone.name == "Millstone");
    CHECK(millstone.type == old_school::CardType::Artifact);
    CHECK(millstone.cost.generic == 2);

    const auto& moat = old_school::card_definition(old_school::CardId::Moat);
    CHECK(moat.name == "Moat");
    CHECK(moat.type == old_school::CardType::Enchantment);
    CHECK(moat.cost.generic == 2);
    CHECK(moat.cost.white == 2);

    const auto& flying_men =
        old_school::card_definition(old_school::CardId::FlyingMen);
    CHECK(flying_men.name == "Flying Men");
    CHECK(flying_men.type == old_school::CardType::Creature);
    CHECK(flying_men.cost.generic == 0);
    CHECK(flying_men.cost.green == 0);
    CHECK(flying_men.cost.red == 0);
    CHECK(flying_men.cost.blue == 1);
    CHECK(flying_men.cost.white == 0);
    CHECK(flying_men.power == 1);
    CHECK(flying_men.toughness == 1);
    CHECK(flying_men.flying);
    CHECK(flying_men.cannot_block_power_at_least == 0);

    const auto& ironclaw =
        old_school::card_definition(old_school::CardId::IronclawOrcs);
    CHECK(ironclaw.name == "Ironclaw Orcs");
    CHECK(ironclaw.type == old_school::CardType::Creature);
    CHECK(ironclaw.cost.generic == 1);
    CHECK(ironclaw.cost.green == 0);
    CHECK(ironclaw.cost.red == 1);
    CHECK(ironclaw.cost.blue == 0);
    CHECK(ironclaw.cost.white == 0);
    CHECK(ironclaw.power == 2);
    CHECK(ironclaw.toughness == 2);
    CHECK(!ironclaw.flying);
    CHECK(ironclaw.cannot_block_power_at_least == 2);

    const auto& gray_ogre =
        old_school::card_definition(old_school::CardId::GrayOgre);
    CHECK(gray_ogre.name == "Gray Ogre");
    CHECK(gray_ogre.type == old_school::CardType::Creature);
    CHECK(gray_ogre.cost.generic == 2);
    CHECK(gray_ogre.cost.green == 0);
    CHECK(gray_ogre.cost.red == 1);
    CHECK(gray_ogre.cost.blue == 0);
    CHECK(gray_ogre.cost.white == 0);
    CHECK(gray_ogre.power == 2);
    CHECK(gray_ogre.toughness == 2);
    CHECK(!gray_ogre.flying);
    CHECK(gray_ogre.cannot_block_power_at_least == 0);

    const auto& hill_giant =
        old_school::card_definition(old_school::CardId::HillGiant);
    CHECK(hill_giant.name == "Hill Giant");
    CHECK(hill_giant.type == old_school::CardType::Creature);
    CHECK(hill_giant.cost.generic == 3);
    CHECK(hill_giant.cost.green == 0);
    CHECK(hill_giant.cost.red == 1);
    CHECK(hill_giant.cost.blue == 0);
    CHECK(hill_giant.cost.white == 0);
    CHECK(hill_giant.power == 3);
    CHECK(hill_giant.toughness == 3);
    CHECK(!hill_giant.flying);
    CHECK(hill_giant.cannot_block_power_at_least == 0);

    const auto& disintegrate =
        old_school::card_definition(old_school::CardId::Disintegrate);
    CHECK(disintegrate.name == "Disintegrate");
    CHECK(disintegrate.type == old_school::CardType::Sorcery);
    CHECK(disintegrate.cost.generic == 0);
    CHECK(disintegrate.cost.green == 0);
    CHECK(disintegrate.cost.red == 1);
    CHECK(disintegrate.cost.blue == 0);
    CHECK(disintegrate.cost.white == 0);
    CHECK(disintegrate.power == 0);
    CHECK(disintegrate.toughness == 0);
    CHECK(disintegrate.effect_damage == 0);

    const auto& giant_growth =
        old_school::card_definition(old_school::CardId::GiantGrowth);
    CHECK(giant_growth.name == "Giant Growth");
    CHECK(giant_growth.type == old_school::CardType::Instant);
    CHECK(giant_growth.cost.generic == 0);
    CHECK(giant_growth.cost.green == 1);
    CHECK(giant_growth.cost.red == 0);
    CHECK(giant_growth.cost.blue == 0);
    CHECK(giant_growth.cost.white == 0);
    CHECK(giant_growth.power == 0);
    CHECK(giant_growth.toughness == 0);
    CHECK(giant_growth.effect_damage == 0);

    const auto& mox =
        old_school::card_definition(old_school::CardId::MoxSapphire);
    CHECK(mox.name == "Mox Sapphire");
    CHECK(mox.type == old_school::CardType::Artifact);
    CHECK(mox.cost == old_school::ManaCost{});

    const auto& sol_ring =
        old_school::card_definition(old_school::CardId::SolRing);
    CHECK(sol_ring.name == "Sol Ring");
    CHECK(sol_ring.type == old_school::CardType::Artifact);
    CHECK(sol_ring.cost == old_school::ManaCost{.generic = 1});

    const auto& ancestral =
        old_school::card_definition(old_school::CardId::AncestralRecall);
    CHECK(ancestral.name == "Ancestral Recall");
    CHECK(ancestral.type == old_school::CardType::Instant);
    CHECK(ancestral.cost == old_school::ManaCost{.blue = 1});

    const auto& time_walk =
        old_school::card_definition(old_school::CardId::TimeWalk);
    CHECK(time_walk.name == "Time Walk");
    CHECK(time_walk.type == old_school::CardType::Sorcery);
    CHECK((time_walk.cost ==
           old_school::ManaCost{.generic = 1, .blue = 1}));

    const auto& braingeyser =
        old_school::card_definition(old_school::CardId::Braingeyser);
    CHECK(braingeyser.name == "Braingeyser");
    CHECK(braingeyser.type == old_school::CardType::Sorcery);
    CHECK(braingeyser.cost == old_school::ManaCost{.blue = 2});

    const auto& force_spike =
        old_school::card_definition(old_school::CardId::ForceSpike);
    CHECK(force_spike.name == "Force Spike");
    CHECK(force_spike.type == old_school::CardType::Instant);
    CHECK(force_spike.cost == old_school::ManaCost{.blue = 1});

    const auto& air_elemental =
        old_school::card_definition(old_school::CardId::AirElemental);
    CHECK(air_elemental.name == "Air Elemental");
    CHECK(air_elemental.type == old_school::CardType::Creature);
    CHECK((air_elemental.cost ==
           old_school::ManaCost{.generic = 3, .blue = 2}));
    CHECK(air_elemental.power == 4);
    CHECK(air_elemental.toughness == 4);
    CHECK(air_elemental.flying);
}

TEST(starting_decks_have_the_requested_cards) {
    const auto green_deck = old_school::green_deck();
    CHECK(green_deck.size() == 40);
    CHECK(count_card(green_deck, old_school::CardId::Forest) == 18);
    CHECK(count_card(green_deck, old_school::CardId::GrizzlyBears) == 9);
    CHECK(count_card(green_deck, old_school::CardId::IronrootTreefolk) == 8);
    CHECK(count_card(green_deck, old_school::CardId::GiantGrowth) == 4);
    CHECK(count_card(green_deck, old_school::CardId::Tsunami) == 1);

    const auto red_deck = old_school::red_deck();
    CHECK(red_deck.size() == 40);
    CHECK(count_card(red_deck, old_school::CardId::Mountain) == 15);
    CHECK(count_card(red_deck, old_school::CardId::LightningBolt) == 9);
    CHECK(count_card(red_deck, old_school::CardId::IronclawOrcs) == 7);
    CHECK(count_card(red_deck, old_school::CardId::GrayOgre) == 4);
    CHECK(count_card(red_deck, old_school::CardId::HillGiant) == 3);
    CHECK(count_card(red_deck, old_school::CardId::FireElemental) == 2);

    const auto blue_deck = old_school::blue_deck();
    CHECK(blue_deck.size() == 40);
    CHECK(count_card(blue_deck, old_school::CardId::Island) == 15);
    CHECK(count_card(blue_deck, old_school::CardId::MoxSapphire) == 1);
    CHECK(count_card(blue_deck, old_school::CardId::SolRing) == 1);
    CHECK(count_card(blue_deck, old_school::CardId::AncestralRecall) == 1);
    CHECK(count_card(blue_deck, old_school::CardId::TimeWalk) == 1);
    CHECK(count_card(blue_deck, old_school::CardId::Braingeyser) == 1);
    CHECK(count_card(blue_deck, old_school::CardId::FlyingMen) == 4);
    CHECK(count_card(blue_deck, old_school::CardId::ForceSpike) == 4);
    CHECK(count_card(blue_deck, old_school::CardId::Counterspell) == 8);
    CHECK(count_card(blue_deck, old_school::CardId::AirElemental) == 4);

    const auto white_deck = old_school::white_control_deck();
    CHECK(white_deck.size() == 40);
    CHECK(count_card(white_deck, old_school::CardId::Plains) == 22);
    CHECK(count_card(white_deck, old_school::CardId::Millstone) == 3);
    CHECK(count_card(white_deck, old_school::CardId::Moat) == 15);

    const auto ru_deck = old_school::ru_aggro_deck();
    CHECK(ru_deck.size() == 40);
    CHECK(count_card(ru_deck, old_school::CardId::Mountain) == 13);
    CHECK(count_card(ru_deck, old_school::CardId::Island) == 4);
    CHECK(count_card(ru_deck, old_school::CardId::FlyingMen) == 3);
    CHECK(count_card(ru_deck, old_school::CardId::IronclawOrcs) == 5);
    CHECK(count_card(ru_deck, old_school::CardId::GrayOgre) == 2);
    CHECK(count_card(ru_deck, old_school::CardId::HillGiant) == 8);
    CHECK(count_card(ru_deck, old_school::CardId::LightningBolt) == 3);
    CHECK(count_card(ru_deck, old_school::CardId::Disintegrate) == 2);
}

TEST(determinization_is_reproducible_and_preserves_observer_hand) {
    const auto fixture = determinization_fixture();
    constexpr std::uint64_t kSeed = 0xD37E2A11ULL;
    const auto first = old_school::sample_determinization(
        fixture.state, fixture.decks, 0, kSeed);
    const auto repeated = old_school::sample_determinization(
        fixture.state, fixture.decks, 0, kSeed);

    CHECK(first.players[0].hand == fixture.state.players[0].hand);
    CHECK(first.players[0].library == repeated.players[0].library);
    CHECK(first.players[1].hand == repeated.players[1].hand);
    CHECK(first.players[1].library == repeated.players[1].library);
    CHECK(first.players[0].library.size() ==
          fixture.state.players[0].library.size());
    CHECK(first.players[1].hand.size() ==
          fixture.state.players[1].hand.size());
    CHECK(first.players[1].library.size() ==
          fixture.state.players[1].library.size());
    CHECK(first.players[0].exile == fixture.state.players[0].exile);
    CHECK(first.players[1].exile == fixture.state.players[1].exile);
    CHECK(first.stack.size() == fixture.state.stack.size());
    CHECK(first.stack[0].id == fixture.state.stack[0].id);
    CHECK(first.stack[1].kind ==
          old_school::StackObjectKind::ActivatedAbility);

    const auto other_observer = old_school::sample_determinization(
        fixture.state, fixture.decks, 1, kSeed);
    CHECK(other_observer.players[1].hand ==
          fixture.state.players[1].hand);
    CHECK(other_observer.players[0].hand.size() ==
          fixture.state.players[0].hand.size());
}

TEST(determinization_does_not_consult_hidden_cards) {
    const auto fixture = determinization_fixture();
    auto altered = fixture.state;
    std::reverse(altered.players[0].library.begin(),
                 altered.players[0].library.end());

    const auto different_card = std::find_if(
        altered.players[1].library.begin(),
        altered.players[1].library.end(),
        [&](old_school::CardId card) {
            return card != altered.players[1].hand.front();
        });
    CHECK(different_card != altered.players[1].library.end());
    std::iter_swap(altered.players[1].hand.begin(), different_card);
    std::reverse(altered.players[1].hand.begin(),
                 altered.players[1].hand.end());
    std::reverse(altered.players[1].library.begin(),
                 altered.players[1].library.end());

    constexpr std::uint64_t kSeed = 0x1AF05E7ULL;
    const auto original_sample = old_school::sample_determinization(
        fixture.state, fixture.decks, 0, kSeed);
    const auto altered_sample = old_school::sample_determinization(
        altered, fixture.decks, 0, kSeed);
    CHECK(original_sample.players[0].library ==
          altered_sample.players[0].library);
    CHECK(original_sample.players[1].hand ==
          altered_sample.players[1].hand);
    CHECK(original_sample.players[1].library ==
          altered_sample.players[1].library);
}

TEST(determinization_conserves_spells_but_not_ability_objects) {
    const auto fixture = determinization_fixture();
    const auto sampled = old_school::sample_determinization(
        fixture.state, fixture.decks, 0, 0xC0A53A7EULL);

    for (std::size_t player = 0; player < fixture.decks.size();
         ++player) {
        auto expected = fixture.decks[player];
        std::sort(expected.begin(), expected.end());
        CHECK(physical_cards(sampled, player) == expected);
        CHECK(physical_cards(sampled, player).size() ==
              fixture.decks[player].size());
    }
    CHECK(count_card(physical_cards(sampled, 0),
                     old_school::CardId::Millstone) ==
          count_card(fixture.decks[0], old_school::CardId::Millstone));
    CHECK(count_card(physical_cards(sampled, 1),
                     old_school::CardId::AirElemental) ==
          count_card(fixture.decks[1],
                     old_school::CardId::AirElemental));
}

TEST(determinization_varies_by_seed_and_rejects_invalid_public_state) {
    const auto fixture = determinization_fixture();
    const auto baseline = old_school::sample_determinization(
        fixture.state, fixture.decks, 0, 0x5EEDULL);
    bool found_variation = false;
    for (std::uint64_t seed = 1; seed <= 16; ++seed) {
        const auto candidate = old_school::sample_determinization(
            fixture.state, fixture.decks, 0, seed);
        found_variation =
            found_variation ||
            candidate.players[0].library !=
                baseline.players[0].library ||
            candidate.players[1].hand != baseline.players[1].hand ||
            candidate.players[1].library !=
                baseline.players[1].library;
    }
    CHECK(found_variation);

    auto invalid = fixture.state;
    invalid.players[1].graveyard.push_back(old_school::CardId::Forest);
    bool rejected = false;
    try {
        static_cast<void>(old_school::sample_determinization(
            invalid, fixture.decks, 0, 0xBADULL));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

TEST(handcrafted_diagnostic_scores_match_deployed_preferences) {
    old_school::GameState priority_state;
    priority_state.active_player = 0;
    priority_state.players[0].hand = {
        old_school::CardId::LightningBolt,
    };
    priority_state.players[0].lands = {
        {.card = old_school::CardId::Mountain, .tapped = false},
    };
    priority_state.players[1].life = 3;
    const auto actions =
        old_school::legal_priority_actions(priority_state, 0, true);
    const auto scores =
        old_school::handcrafted_priority_scores(
            priority_state, 0, actions);
    const auto lethal =
        std::find(
            actions.begin(), actions.end(),
            old_school::PriorityAction::cast_lightning_bolt(
                old_school::Target::player_target(1)));
    CHECK(lethal != actions.end());
    const std::size_t lethal_index =
        static_cast<std::size_t>(
            std::distance(actions.begin(), lethal));
    CHECK(scores[lethal_index] ==
          *std::max_element(scores.begin(), scores.end()));

    const auto favorable =
        attack_evaluation_fixture(old_school::CardId::GrizzlyBears);
    CHECK((old_school::handcrafted_binary_attack_scores(
               favorable.state, 0, {}, 1, {}) ==
           std::array<double, 2>({0.0, 1.0})));
    const auto unfavorable =
        attack_evaluation_fixture(old_school::CardId::FireElemental);
    CHECK((old_school::handcrafted_binary_attack_scores(
               unfavorable.state, 0, {}, 1, {}) ==
           std::array<double, 2>({1.0, 0.0})));

    old_school::GameState evasion;
    evasion.active_player = 0;
    evasion.players[0].creatures = {
        creature(1, old_school::CardId::FlyingMen),
    };
    evasion.players[1].creatures = {bear(2)};
    CHECK((old_school::handcrafted_binary_attack_scores(
               evasion, 0, {}, 1, {}) ==
           std::array<double, 2>({0.0, 1.0})));
}

TEST(handcrafted_old_school_spell_scores_follow_the_declared_policy) {
    old_school::GameState burn;
    burn.active_player = 0;
    burn.players[0].creatures = {bear(1)};
    burn.players[1].life = 5;
    burn.players[1].creatures = {
        creature(2, old_school::CardId::HillGiant),
    };
    const std::vector<old_school::PriorityAction> burn_actions = {
        old_school::PriorityAction::cast_disintegrate(
            2, old_school::Target::player_target(0)),
        old_school::PriorityAction::cast_disintegrate(
            2, old_school::Target::creature_target(0, 1)),
        old_school::PriorityAction::cast_disintegrate(
            5, old_school::Target::player_target(1)),
        old_school::PriorityAction::cast_disintegrate(
            2, old_school::Target::player_target(1)),
        old_school::PriorityAction::cast_disintegrate(
            3, old_school::Target::creature_target(1, 2)),
        old_school::PriorityAction::cast_disintegrate(
            2, old_school::Target::creature_target(1, 2)),
    };
    const auto burn_scores =
        old_school::handcrafted_priority_scores(
            burn, 0, burn_actions);
    CHECK(burn_scores.size() == burn_actions.size());
    CHECK(burn_scores[0] == -10'000.0);
    CHECK(burn_scores[1] == -10'000.0);
    CHECK(burn_scores[2] == 10'000.0);
    CHECK(burn_scores[3] == 1'150.0);
    CHECK(burn_scores[4] == 2'550.0);
    CHECK(burn_scores[5] == 500.0);

    const auto zero_x_scores =
        old_school::handcrafted_priority_scores(
            burn, 0,
            {
                old_school::PriorityAction::pass(),
                old_school::PriorityAction::cast_disintegrate(
                    0, old_school::Target::player_target(1)),
            });
    CHECK(zero_x_scores[0] == -10.0);
    CHECK(zero_x_scores[1] == -100.0);

    old_school::GameState growth;
    growth.active_player = 1;
    growth.players[0].creatures = {bear(11)};
    growth.players[1].creatures = {
        creature(12, old_school::CardId::GrayOgre),
    };
    growth.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 20,
            .card = old_school::CardId::LightningBolt,
            .controller = 1,
            .target = old_school::Target::creature_target(0, 11),
            .spell_target = std::nullopt,
        },
    };
    const std::vector<old_school::PriorityAction> growth_actions = {
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 11)),
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(1, 12)),
    };
    const auto growth_scores =
        old_school::handcrafted_priority_scores(
            growth, 0, growth_actions);
    CHECK(growth_scores[0] == 9'000.0);
    CHECK(growth_scores[1] == -10'000.0);

    growth.stack.clear();
    growth.active_player = 0;
    growth.players[1].life = 5;
    const auto held_growth =
        old_school::handcrafted_priority_scores(
            growth, 0, {growth_actions[0]});
    CHECK(held_growth[0] == -100.0);
    const auto lethal_growth =
        old_school::handcrafted_priority_scores(
            growth, 0, {growth_actions[0]},
            old_school::TurnPhase::BeginCombat);
    CHECK(lethal_growth[0] == 9'500.0);

    growth.players[0].creatures.push_back(bear(13, true));
    const auto sick_growth =
        old_school::handcrafted_priority_scores(
            growth, 0,
            {old_school::PriorityAction::cast_giant_growth(
                old_school::Target::creature_target(0, 13))},
            old_school::TurnPhase::BeginCombat);
    CHECK(sick_growth[0] == -100.0);

    old_school::GameState lands;
    lands.active_player = 0;
    lands.players[0].hand = {
        old_school::CardId::Mountain,
        old_school::CardId::Island,
        old_school::CardId::FlyingMen,
        old_school::CardId::IronclawOrcs,
    };
    const auto land_scores =
        old_school::handcrafted_priority_scores(
            lands, 0,
            {
                old_school::PriorityAction::play_land(
                    old_school::CardId::Mountain),
                old_school::PriorityAction::play_land(
                    old_school::CardId::Island),
            });
    CHECK(land_scores[1] > land_scores[0]);
}

TEST(handcrafted_force_spike_prefers_live_tax_and_passes_dead_tax) {
    const auto make_state = [](bool payer_has_mana) {
        old_school::GameState state;
        state.active_player = 1;
        state.players[1].lands = {
            {
                .card = old_school::CardId::Mountain,
                .tapped = !payer_has_mana,
            },
        };
        state.stack = {
            {
                .kind = old_school::StackObjectKind::Spell,
                .id = 77,
                .card = old_school::CardId::AirElemental,
                .controller = 1,
            },
        };
        return state;
    };
    const std::vector<old_school::PriorityAction> candidates = {
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::cast_force_spike(77),
    };

    const auto live_scores =
        old_school::handcrafted_priority_scores(
            make_state(false), 0, candidates);
    CHECK(live_scores.size() == candidates.size());
    CHECK(live_scores[1] > live_scores[0]);
    CHECK(std::distance(
              live_scores.begin(),
              std::max_element(
                  live_scores.begin(), live_scores.end())) == 1);

    const auto dead_scores =
        old_school::handcrafted_priority_scores(
            make_state(true), 0, candidates);
    CHECK(dead_scores.size() == candidates.size());
    CHECK(dead_scores[1] < dead_scores[0]);
    CHECK(std::distance(
              dead_scores.begin(),
              std::max_element(
                  dead_scores.begin(), dead_scores.end())) == 0);
}

TEST(handcrafted_uses_live_force_spike_and_the_stack_counters_the_spell) {
    old_school::GameState state;
    state.active_player = 1;
    state.next_stack_object_id = 79;
    state.players[0].hand = {
        old_school::CardId::ForceSpike,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island},
    };
    state.players[1].lands = {
        {.card = old_school::CardId::Mountain, .tapped = true},
        {.card = old_school::CardId::Mountain, .tapped = true},
        {.card = old_school::CardId::Mountain, .tapped = true},
    };
    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 78,
            .card = old_school::CardId::GrayOgre,
            .controller = 1,
        },
    };

    const auto actions =
        old_school::legal_priority_actions(state, 0, false);
    const auto scores =
        old_school::handcrafted_priority_scores(
            state, 0, actions);
    const auto chosen = static_cast<std::size_t>(
        std::distance(
            scores.begin(),
            std::max_element(scores.begin(), scores.end())));
    const auto force_spike =
        old_school::PriorityAction::cast_force_spike(78);
    CHECK(actions[chosen] == force_spike);
    CHECK(old_school::apply_priority_action(
        state, 0, actions[chosen], false));

    old_school::PriorityState priority = {
        .player = 0,
        .consecutive_passes = 0,
    };
    CHECK(old_school::pass_priority(state, priority) ==
          old_school::PriorityPassResult::Passed);
    CHECK(old_school::pass_priority(state, priority) ==
          old_school::PriorityPassResult::StackObjectResolved);
    CHECK(state.stack.empty());
    CHECK(state.players[1].creatures.empty());
    CHECK(count_card(
              state.players[0].graveyard,
              old_school::CardId::ForceSpike) == 1);
    CHECK(count_card(
              state.players[1].graveyard,
              old_school::CardId::GrayOgre) == 1);
    CHECK(state.stats[0].spells_countered == 1);
}

TEST(white_lock_plan_diagnostic_fixture_is_valid_and_locked) {
    const auto state = old_school::white_lock_plan_diagnostic_state();
    CHECK(state.active_player == 0);
    CHECK(state.stack.empty());
    CHECK(state.players[0].land_played_this_turn);
    CHECK(state.players[0].lands.size() == 4);
    CHECK(std::all_of(
        state.players[0].lands.begin(),
        state.players[0].lands.end(),
        [](const old_school::LandPermanent& land) {
            return land.card == old_school::CardId::Plains &&
                   !land.tapped;
        }));
    CHECK(state.players[0].artifacts.size() == 1);
    CHECK(state.players[0].artifacts[0].card ==
          old_school::CardId::Millstone);
    CHECK(!state.players[0].artifacts[0].tapped);
    CHECK(count_card(
              state.players[0].enchantments,
              old_school::CardId::Moat) == 1);
    CHECK(count_card(
              state.players[0].hand,
              old_school::CardId::Moat) == 7);

    CHECK(state.players[1].creatures.size() == 1);
    const auto& attacker = state.players[1].creatures[0];
    CHECK(attacker.card == old_school::CardId::FireElemental);
    CHECK(!attacker.tapped);
    CHECK(!attacker.summoning_sick);
    CHECK(!old_school::card_definition(attacker.card).flying);

    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() == 4);
    CHECK(has_action(
        actions,
        old_school::PriorityAction::cast_enchantment(
            old_school::CardId::Moat)));
    CHECK(has_action(
        actions,
        old_school::PriorityAction::activate_millstone(
            state.players[0].artifacts[0].id,
            old_school::Target::player_target(1))));

    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto sampled =
        old_school::sample_determinization(state, decks, 0, 0x10C4ULL);
    for (std::size_t player = 0; player < decks.size(); ++player) {
        auto expected = decks[player];
        std::sort(expected.begin(), expected.end());
        CHECK(physical_cards(sampled, player) == expected);
    }

    auto attempted_combat = state;
    CHECK(!old_school::resolve_combat(
        attempted_combat, 1, {attacker.id}, {}));
}

TEST(basic_lands_can_be_played_once_per_turn) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::Forest,
                             old_school::CardId::Mountain,
                             old_school::CardId::Island,
                             old_school::CardId::Plains};

    auto actions = old_school::legal_priority_actions(state, 0, true);
    CHECK(has_action(actions,
                     old_school::PriorityAction::play_land(old_school::CardId::Forest)));
    CHECK(has_action(actions,
                     old_school::PriorityAction::play_land(old_school::CardId::Mountain)));
    CHECK(has_action(actions,
                     old_school::PriorityAction::play_land(old_school::CardId::Island)));
    CHECK(has_action(actions,
                     old_school::PriorityAction::play_land(old_school::CardId::Plains)));
    CHECK(old_school::apply_priority_action(
        state, 0, old_school::PriorityAction::play_land(old_school::CardId::Forest),
        true));
    CHECK(state.players[0].lands.size() == 1);
    CHECK(state.players[0].lands[0].card == old_school::CardId::Forest);

    CHECK(!old_school::apply_priority_action(
        state, 0, old_school::PriorityAction::play_land(old_school::CardId::Mountain),
        true));
    old_school::begin_turn(state, 0);
    CHECK(old_school::apply_priority_action(
        state, 0, old_school::PriorityAction::play_land(old_school::CardId::Mountain),
        true));
    CHECK(state.players[0].lands[1].card == old_school::CardId::Mountain);
    old_school::begin_turn(state, 0);
    CHECK(old_school::apply_priority_action(
        state, 0, old_school::PriorityAction::play_land(old_school::CardId::Island),
        true));
    CHECK(state.players[0].lands[2].card == old_school::CardId::Island);
    old_school::begin_turn(state, 0);
    CHECK(old_school::apply_priority_action(
        state, 0, old_school::PriorityAction::play_land(old_school::CardId::Plains),
        true));
    CHECK(state.players[0].lands[3].card == old_school::CardId::Plains);
}

TEST(grizzly_bears_costs_one_and_a_green_and_has_summoning_sickness) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::GrizzlyBears};
    state.players[0].lands = {
        {.card = old_school::CardId::Forest, .tapped = false},
        {.card = old_school::CardId::Forest, .tapped = false},
    };

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_creature(old_school::CardId::GrizzlyBears),
        true));
    CHECK(state.players[0].hand.empty());
    CHECK(state.players[0].creatures.empty());
    CHECK(state.stack.size() == 1);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].summoning_sick);
    CHECK(state.players[0].lands[0].tapped);
    CHECK(state.players[0].lands[1].tapped);

    const auto bear_id = state.players[0].creatures[0].id;
    CHECK(!old_school::resolve_combat(state, 0, {bear_id}, {}));
    old_school::begin_turn(state, 0);
    CHECK(old_school::resolve_combat(state, 0, {bear_id}, {}));
    CHECK(state.players[1].life == 18);
}

TEST(grizzly_bears_requires_green_mana) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::GrizzlyBears};
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain, .tapped = false},
        {.card = old_school::CardId::Mountain, .tapped = false},
    };
    CHECK(!has_action(old_school::legal_priority_actions(state, 0, true),
                      old_school::PriorityAction::cast_creature(
                          old_school::CardId::GrizzlyBears)));
}

TEST(ironroot_treefolk_costs_four_and_a_green) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::IronrootTreefolk};
    state.players[0].lands = {
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
    };

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_creature(
            old_school::CardId::IronrootTreefolk),
        true));
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].card ==
          old_school::CardId::IronrootTreefolk);
    CHECK(std::all_of(state.players[0].lands.begin(),
                      state.players[0].lands.end(),
                      [](const old_school::LandPermanent& land) {
                          return land.tapped;
                      }));
}

TEST(fire_elemental_requires_two_red_mana) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::FireElemental};
    state.players[0].lands = {
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Mountain},
    };
    CHECK(!has_action(
        old_school::legal_priority_actions(state, 0, true),
        old_school::PriorityAction::cast_creature(old_school::CardId::FireElemental)));

    state.players[0].lands[1].card = old_school::CardId::Mountain;
    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_creature(old_school::CardId::FireElemental),
        true));
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures[0].card ==
          old_school::CardId::FireElemental);
}

TEST(fire_elemental_defeats_ironroot_treefolk_in_combat) {
    old_school::GameState state;
    state.players[0].creatures = {
        creature(1, old_school::CardId::FireElemental)};
    state.players[1].creatures = {
        creature(2, old_school::CardId::IronrootTreefolk)};

    CHECK(old_school::resolve_combat(state, 0, {1}, {{1, 2}}));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[1].creatures.empty());
}

TEST(attacking_player_controls_multi_block_damage_order) {
    const auto combat_state = [] {
        old_school::GameState state;
        state.players[0].creatures = {
            creature(1, old_school::CardId::FireElemental)};
        state.players[1].creatures = {
            bear(2),
            creature(3, old_school::CardId::IronrootTreefolk),
        };
        return state;
    };

    auto bears_first = combat_state();
    CHECK(old_school::resolve_combat(
        bears_first, 0, {1}, {{1, 2}, {1, 3}}));
    CHECK(bears_first.players[0].creatures.empty());
    CHECK(bears_first.players[1].creatures.size() == 1);
    CHECK(bears_first.players[1].creatures[0].card ==
          old_school::CardId::IronrootTreefolk);
    CHECK(count_card(bears_first.players[1].graveyard,
                     old_school::CardId::GrizzlyBears) == 1);

    auto treefolk_first = combat_state();
    CHECK(old_school::resolve_combat(
        treefolk_first, 0, {1}, {{1, 3}, {1, 2}}));
    CHECK(treefolk_first.players[0].creatures.empty());
    CHECK(treefolk_first.players[1].creatures.size() == 1);
    CHECK(treefolk_first.players[1].creatures[0].card ==
          old_school::CardId::GrizzlyBears);
    CHECK(count_card(treefolk_first.players[1].graveyard,
                     old_school::CardId::IronrootTreefolk) == 1);
}

TEST(water_elemental_requires_two_blue_mana) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::WaterElemental};
    state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_creature(
            old_school::CardId::WaterElemental),
        true));
    CHECK(state.stack.size() == 1);
    CHECK(state.players[0].creatures.empty());
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].card ==
          old_school::CardId::WaterElemental);
}

TEST(new_old_school_creatures_cast_with_exact_mana) {
    const auto check_cast =
        [](old_school::CardId card,
           std::vector<old_school::LandPermanent> exact_lands) {
            old_school::GameState short_state;
            short_state.players[0].hand = {card};
            short_state.players[0].lands = exact_lands;
            short_state.players[0].lands.pop_back();
            CHECK(!has_action(
                old_school::legal_priority_actions(short_state, 0, true),
                old_school::PriorityAction::cast_creature(card)));

            old_school::GameState state;
            state.players[0].hand = {card};
            state.players[0].lands = std::move(exact_lands);
            CHECK(old_school::apply_priority_action(
                state, 0, old_school::PriorityAction::cast_creature(card),
                true));
            CHECK(std::all_of(
                state.players[0].lands.begin(),
                state.players[0].lands.end(),
                [](const old_school::LandPermanent& land) {
                    return land.tapped;
                }));
            CHECK(old_school::resolve_top_of_stack(state));
            CHECK(state.players[0].creatures.size() == 1);
            CHECK(state.players[0].creatures[0].card == card);
        };

    check_cast(
        old_school::CardId::FlyingMen,
        {{.card = old_school::CardId::Island}});
    check_cast(
        old_school::CardId::IronclawOrcs,
        {{.card = old_school::CardId::Mountain},
         {.card = old_school::CardId::Island}});
    check_cast(
        old_school::CardId::GrayOgre,
        {{.card = old_school::CardId::Mountain},
         {.card = old_school::CardId::Island},
         {.card = old_school::CardId::Island}});
    check_cast(
        old_school::CardId::HillGiant,
        {{.card = old_school::CardId::Mountain},
         {.card = old_school::CardId::Island},
         {.card = old_school::CardId::Island},
         {.card = old_school::CardId::Island}});
}

TEST(mox_sapphire_and_sol_ring_produce_mana_and_float_excess) {
    old_school::GameState mox_state;
    mox_state.active_player = 0;
    mox_state.players[0].hand = {
        old_school::CardId::MoxSapphire,
        old_school::CardId::FlyingMen,
    };
    CHECK(old_school::apply_priority_action(
        mox_state, 0,
        old_school::PriorityAction::cast_artifact(
            old_school::CardId::MoxSapphire),
        true));
    CHECK(mox_state.players[0].lands.empty());
    CHECK(mox_state.players[0].artifacts.empty());
    CHECK(old_school::resolve_top_of_stack(mox_state));
    CHECK(mox_state.players[0].artifacts.size() == 1);
    CHECK(mox_state.players[0].artifacts[0].card ==
          old_school::CardId::MoxSapphire);
    CHECK(!mox_state.players[0].artifacts[0].tapped);

    CHECK(old_school::apply_priority_action(
        mox_state, 0,
        old_school::PriorityAction::cast_creature(
            old_school::CardId::FlyingMen),
        true));
    CHECK(mox_state.players[0].artifacts[0].tapped);
    CHECK(old_school::resolve_top_of_stack(mox_state));
    CHECK(mox_state.players[0].creatures.size() == 1);
    CHECK(mox_state.players[0].creatures[0].card ==
          old_school::CardId::FlyingMen);

    old_school::GameState ring_state;
    ring_state.active_player = 0;
    ring_state.players[0].hand = {
        old_school::CardId::SolRing,
        old_school::CardId::Millstone,
    };
    ring_state.players[0].lands = {
        {.card = old_school::CardId::Island},
    };
    CHECK(old_school::apply_priority_action(
        ring_state, 0,
        old_school::PriorityAction::cast_artifact(
            old_school::CardId::SolRing),
        true));
    CHECK(ring_state.players[0].lands[0].tapped);
    CHECK(old_school::resolve_top_of_stack(ring_state));
    CHECK(ring_state.players[0].artifacts.size() == 1);
    CHECK(ring_state.players[0].artifacts[0].card ==
          old_school::CardId::SolRing);
    CHECK(old_school::apply_priority_action(
        ring_state, 0,
        old_school::PriorityAction::cast_artifact(
            old_school::CardId::Millstone),
        true));
    CHECK(ring_state.players[0].artifacts[0].tapped);
    CHECK(old_school::resolve_top_of_stack(ring_state));
    CHECK(ring_state.players[0].artifacts.size() == 2);

    old_school::GameState excess_state;
    excess_state.active_player = 0;
    excess_state.players[0].hand = {
        old_school::CardId::GrizzlyBears,
    };
    excess_state.players[0].lands = {
        {.card = old_school::CardId::Forest},
    };
    excess_state.players[0].artifacts = {
        {
            .id = 7,
            .card = old_school::CardId::SolRing,
            .tapped = false,
        },
    };
    CHECK(old_school::apply_priority_action(
        excess_state, 0,
        old_school::PriorityAction::cast_creature(
            old_school::CardId::GrizzlyBears),
        true));
    CHECK(excess_state.players[0].lands[0].tapped);
    CHECK(excess_state.players[0].artifacts[0].tapped);
    CHECK(excess_state.players[0].mana_pool ==
          old_school::ManaCost{.generic = 1});
    CHECK(old_school::resolve_top_of_stack(excess_state));
    old_school::PriorityState phase_end{
        .player = 0,
        .consecutive_passes = 1,
    };
    CHECK(old_school::pass_priority(excess_state, phase_end) ==
          old_school::PriorityPassResult::WindowEnded);
    CHECK(excess_state.players[0].mana_pool ==
          old_school::ManaCost{});
}

TEST(air_elemental_needs_two_blue_mana_and_has_flying) {
    old_school::GameState short_blue;
    short_blue.active_player = 0;
    short_blue.players[0].hand = {
        old_school::CardId::AirElemental,
    };
    short_blue.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
    };
    CHECK(!has_action(
        old_school::legal_priority_actions(short_blue, 0, true),
        old_school::PriorityAction::cast_creature(
            old_school::CardId::AirElemental)));

    old_school::GameState cast_state;
    cast_state.active_player = 0;
    cast_state.players[0].hand = {
        old_school::CardId::AirElemental,
    };
    cast_state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
    };
    CHECK(old_school::apply_priority_action(
        cast_state, 0,
        old_school::PriorityAction::cast_creature(
            old_school::CardId::AirElemental),
        true));
    CHECK(old_school::resolve_top_of_stack(cast_state));
    CHECK(cast_state.players[0].creatures.size() == 1);
    CHECK(cast_state.players[0].creatures[0].card ==
          old_school::CardId::AirElemental);

    old_school::GameState grounded_blocker;
    grounded_blocker.players[0].creatures = {
        creature(1, old_school::CardId::AirElemental),
    };
    grounded_blocker.players[1].creatures = {bear(2)};
    CHECK(!old_school::resolve_combat(
        grounded_blocker, 0, {1}, {{1, 2}}));

    old_school::GameState flying_blocker;
    flying_blocker.players[0].creatures = {
        creature(3, old_school::CardId::AirElemental),
    };
    flying_blocker.players[1].creatures = {
        creature(4, old_school::CardId::FlyingMen),
    };
    CHECK(old_school::resolve_combat(
        flying_blocker, 0, {3}, {{3, 4}}));
    CHECK(flying_blocker.players[0].creatures.size() == 1);
    CHECK(flying_blocker.players[0].creatures[0].damage == 1);
    CHECK(flying_blocker.players[1].creatures.empty());
}

TEST(ancestral_recall_draws_for_either_target_and_flags_failed_draw) {
    old_school::GameState opponent_draw;
    opponent_draw.active_player = 1;
    opponent_draw.players[0].hand = {
        old_school::CardId::AncestralRecall,
    };
    opponent_draw.players[0].lands = {
        {.card = old_school::CardId::Island},
    };
    opponent_draw.players[1].library = {
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt,
        old_school::CardId::GrayOgre,
        old_school::CardId::HillGiant,
    };
    const auto target_opponent =
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(1));
    CHECK(has_action(
        old_school::legal_priority_actions(
            opponent_draw, 0, false),
        target_opponent));
    CHECK(old_school::apply_priority_action(
        opponent_draw, 0, target_opponent, false));
    CHECK(old_school::resolve_top_of_stack(opponent_draw));
    CHECK(opponent_draw.players[1].hand.size() == 3);
    CHECK(opponent_draw.players[1].library.size() == 1);
    CHECK(opponent_draw.stats[1].cards_drawn == 3);
    CHECK(!opponent_draw.failed_draw[1]);
    CHECK(count_card(
              opponent_draw.players[0].graveyard,
              old_school::CardId::AncestralRecall) == 1);

    old_school::GameState self_decking;
    self_decking.active_player = 1;
    self_decking.players[0].hand = {
        old_school::CardId::AncestralRecall,
    };
    self_decking.players[0].library = {
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt,
    };
    self_decking.players[0].lands = {
        {.card = old_school::CardId::Island},
    };
    const auto target_self =
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(0));
    CHECK(old_school::apply_priority_action(
        self_decking, 0, target_self, false));
    CHECK(old_school::resolve_top_of_stack(self_decking));
    CHECK(self_decking.players[0].hand.size() == 2);
    CHECK(self_decking.players[0].library.empty());
    CHECK(self_decking.stats[0].cards_drawn == 2);
    CHECK(self_decking.failed_draw[0]);
}

TEST(braingeyser_enumerates_x_zero_and_draws_the_chosen_player) {
    old_school::GameState state;
    state.active_player = 0;
    state.players[0].hand = {
        old_school::CardId::Braingeyser,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    state.players[1].library = {
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt,
    };
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    for (int x_value = 0; x_value <= 2; ++x_value) {
        for (std::size_t target = 0; target < 2; ++target) {
            CHECK(has_action(
                actions,
                old_school::PriorityAction::cast_braingeyser(
                    x_value,
                    old_school::Target::player_target(target))));
        }
    }
    CHECK(!has_action(
        actions,
        old_school::PriorityAction::cast_braingeyser(
            3, old_school::Target::player_target(0))));
    CHECK(!has_action(
        old_school::legal_priority_actions(state, 0, false),
        old_school::PriorityAction::cast_braingeyser(
            0, old_school::Target::player_target(1))));

    auto zero_draw = state;
    const auto zero_action =
        old_school::PriorityAction::cast_braingeyser(
            0, old_school::Target::player_target(1));
    CHECK(old_school::apply_priority_action(
        zero_draw, 0, zero_action, true));
    CHECK(old_school::resolve_top_of_stack(zero_draw));
    CHECK(zero_draw.players[1].hand.empty());
    CHECK(zero_draw.players[1].library.size() == 2);
    CHECK(zero_draw.stats[1].cards_drawn == 0);
    CHECK(!zero_draw.failed_draw[1]);

    auto opponent_decking = state;
    opponent_decking.players[1].library = {
        old_school::CardId::Mountain,
    };
    const auto draw_two =
        old_school::PriorityAction::cast_braingeyser(
            2, old_school::Target::player_target(1));
    CHECK(old_school::apply_priority_action(
        opponent_decking, 0, draw_two, true));
    CHECK(old_school::resolve_top_of_stack(opponent_decking));
    CHECK(opponent_decking.players[1].hand.size() == 1);
    CHECK(opponent_decking.players[1].library.empty());
    CHECK(opponent_decking.stats[1].cards_drawn == 1);
    CHECK(opponent_decking.failed_draw[1]);
}

TEST(cleanup_discards_only_the_active_players_chosen_excess) {
    old_school::GameState state;
    state.active_player = 0;
    state.players[0].hand = {
        old_school::CardId::Forest,
        old_school::CardId::LightningBolt,
        old_school::CardId::Mountain,
        old_school::CardId::AncestralRecall,
        old_school::CardId::LightningBolt,
        old_school::CardId::GrayOgre,
        old_school::CardId::Island,
        old_school::CardId::Moat,
        old_school::CardId::Counterspell,
        old_school::CardId::Plains,
    };
    state.players[0].graveyard = {
        old_school::CardId::GrizzlyBears,
    };
    state.players[1].hand.assign(
        9, old_school::CardId::Forest);
    state.players[0].mana_pool = {.green = 2};
    state.players[1].mana_pool = {.red = 1};
    state.players[0].creatures = {
        creature(11, old_school::CardId::GrizzlyBears),
    };
    state.players[1].creatures = {
        creature(12, old_school::CardId::GrayOgre),
    };
    state.players[0].creatures[0].damage = 1;
    state.players[0].creatures[0].temporary_power_bonus = 3;
    state.players[0].creatures[0].temporary_toughness_bonus = 4;
    state.players[0].creatures[0]
        .exile_on_death_this_turn = true;
    state.players[1].creatures[0].damage = 2;
    state.players[1].creatures[0].temporary_power_bonus = 1;
    state.players[1].creatures[0].temporary_toughness_bonus = 2;
    const auto first_cards = physical_cards(state, 0);
    const auto second_cards = physical_cards(state, 1);

    const auto discarded = old_school::cleanup_turn(
        state, 0, {1, 4, 8});
    CHECK((discarded ==
           std::vector<old_school::CardId>{
               old_school::CardId::LightningBolt,
               old_school::CardId::LightningBolt,
               old_school::CardId::Counterspell}));
    CHECK(state.players[0].hand.size() ==
          old_school::kMaximumHandSize);
    CHECK(state.players[1].hand.size() == 9);
    CHECK((state.players[0].graveyard ==
           std::vector<old_school::CardId>{
               old_school::CardId::GrizzlyBears,
               old_school::CardId::LightningBolt,
               old_school::CardId::LightningBolt,
               old_school::CardId::Counterspell}));
    CHECK(physical_cards(state, 0) == first_cards);
    CHECK(physical_cards(state, 1) == second_cards);
    for (const auto& player : state.players) {
        CHECK(player.mana_pool == old_school::ManaCost{});
        for (const auto& permanent : player.creatures) {
            CHECK(permanent.damage == 0);
            CHECK(permanent.temporary_power_bonus == 0);
            CHECK(permanent.temporary_toughness_bonus == 0);
            CHECK(!permanent.exile_on_death_this_turn);
        }
    }
}

TEST(cleanup_rejects_malformed_choices_without_mutation) {
    old_school::GameState original;
    original.active_player = 0;
    original.players[0].hand = {
        old_school::CardId::Forest,
        old_school::CardId::Forest,
        old_school::CardId::Mountain,
        old_school::CardId::Island,
        old_school::CardId::Plains,
        old_school::CardId::LightningBolt,
        old_school::CardId::Counterspell,
        old_school::CardId::GiantGrowth,
        old_school::CardId::Moat,
    };
    original.players[0].creatures = {
        creature(17, old_school::CardId::GrizzlyBears),
    };
    original.players[0].creatures[0].damage = 1;
    original.players[0].creatures[0].temporary_power_bonus = 1;
    original.players[0].creatures[0].temporary_toughness_bonus = 1;

    for (const std::vector<std::size_t>& invalid : {
             std::vector<std::size_t>{0},
             std::vector<std::size_t>{0, 0},
             std::vector<std::size_t>{0, 9},
         }) {
        auto state = original;
        CHECK(throws_with_text(
            [&] {
                static_cast<void>(
                    old_school::cleanup_turn(
                        state, 0, invalid));
            },
            "cleanup"));
        CHECK(state == original);
    }

    auto invalid_player = original;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(old_school::cleanup_turn(
                invalid_player, 2, {0, 1}));
        },
        "active player"));
    CHECK(invalid_player == original);

    auto inactive_player = original;
    inactive_player.players[1].hand =
        original.players[0].hand;
    const auto inactive_before = inactive_player;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(old_school::cleanup_turn(
                inactive_player, 1, {0, 1}));
        },
        "active player"));
    CHECK(inactive_player == inactive_before);
}

TEST(ancestral_and_braingeyser_overdraw_clean_up_to_seven) {
    const auto exercise_overdraw =
        [](old_school::CardId draw_spell,
           const old_school::PriorityAction& action,
           std::size_t land_count) {
            old_school::GameState state;
            state.active_player = 0;
            state.players[0].hand = {
                draw_spell,
                old_school::CardId::Forest,
                old_school::CardId::Forest,
                old_school::CardId::Mountain,
                old_school::CardId::LightningBolt,
                old_school::CardId::GrizzlyBears,
                old_school::CardId::GiantGrowth,
            };
            state.players[0].library = {
                old_school::CardId::Plains,
                old_school::CardId::Island,
                old_school::CardId::Counterspell,
            };
            state.players[0].lands.assign(
                land_count,
                old_school::LandPermanent{
                    .card = old_school::CardId::Island,
                });
            const auto before = physical_cards(state, 0);
            CHECK(old_school::apply_priority_action(
                state, 0, action, true));
            CHECK(old_school::resolve_top_of_stack(state));
            CHECK(state.players[0].hand.size() == 9);
            const auto discarded = old_school::cleanup_turn(
                state, 0, {0, 8});
            CHECK(discarded.size() == 2);
            CHECK(state.players[0].hand.size() ==
                  old_school::kMaximumHandSize);
            CHECK(physical_cards(state, 0) == before);
        };

    exercise_overdraw(
        old_school::CardId::AncestralRecall,
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(0)),
        1);
    exercise_overdraw(
        old_school::CardId::Braingeyser,
        old_school::PriorityAction::cast_braingeyser(
            3, old_school::Target::player_target(0)),
        5);
}

TEST(time_walk_queues_and_consumes_an_extra_turn) {
    old_school::GameState state;
    state.active_player = 0;
    state.players[0].hand = {
        old_school::CardId::TimeWalk,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    const auto action = old_school::PriorityAction::cast_sorcery(
        old_school::CardId::TimeWalk);
    CHECK(has_action(
        old_school::legal_priority_actions(state, 0, true),
        action));
    CHECK(!has_action(
        old_school::legal_priority_actions(state, 0, false),
        action));
    CHECK(old_school::apply_priority_action(
        state, 0, action, true));
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.extra_turns_pending[0] == 1);
    CHECK(state.extra_turns_pending[1] == 0);
    CHECK(count_card(
              state.players[0].graveyard,
              old_school::CardId::TimeWalk) == 1);

    CHECK(old_school::advance_turn_player(state) == 0);
    CHECK(state.active_player == 0);
    CHECK(state.extra_turns_pending[0] == 0);
    CHECK(old_school::advance_turn_player(state) == 1);
    CHECK(state.active_player == 1);
}

TEST(force_spike_counters_only_when_the_tax_is_not_paid) {
    const auto make_state = [](bool payer_has_mana) {
        old_school::GameState state;
        state.active_player = 1;
        state.next_stack_object_id = 11;
        state.players[0].hand = {
            old_school::CardId::ForceSpike,
        };
        state.players[0].lands = {
            {.card = old_school::CardId::Island},
        };
        state.players[1].lands = {
            {
                .card = old_school::CardId::Mountain,
                .tapped = !payer_has_mana,
            },
        };
        state.stack = {
            {
                .kind = old_school::StackObjectKind::Spell,
                .id = 10,
                .card = old_school::CardId::GrayOgre,
                .controller = 1,
            },
        };
        return state;
    };
    const auto action =
        old_school::PriorityAction::cast_force_spike(10);

    auto live = make_state(false);
    CHECK(has_action(
        old_school::legal_priority_actions(live, 0, false),
        action));
    CHECK(old_school::apply_priority_action(
        live, 0, action, false));
    CHECK(old_school::resolve_top_of_stack(live));
    CHECK(live.stack.empty());
    CHECK(count_card(
              live.players[1].graveyard,
              old_school::CardId::GrayOgre) == 1);
    CHECK(count_card(
              live.players[0].graveyard,
              old_school::CardId::ForceSpike) == 1);
    CHECK(live.stats[0].spells_countered == 1);

    auto paid = make_state(true);
    CHECK(old_school::apply_priority_action(
        paid, 0, action, false));
    CHECK(old_school::resolve_top_of_stack(
        paid, old_school::ForceSpikePaymentChoice::PayIfAble));
    CHECK(paid.stack.size() == 1);
    CHECK(paid.stack.back().card ==
          old_school::CardId::GrayOgre);
    CHECK(paid.players[1].lands[0].tapped);
    CHECK(paid.stats[0].spells_countered == 0);
    CHECK(old_school::resolve_top_of_stack(paid));
    CHECK(paid.players[1].creatures.size() == 1);
    CHECK(paid.players[1].creatures[0].card ==
          old_school::CardId::GrayOgre);

    auto ring_paid = make_state(false);
    ring_paid.players[1].artifacts = {
        {
            .id = 50,
            .card = old_school::CardId::SolRing,
        },
    };
    CHECK(old_school::apply_priority_action(
        ring_paid, 0, action, false));
    CHECK(old_school::resolve_top_of_stack(ring_paid));
    CHECK(ring_paid.stack.size() == 1);
    CHECK(ring_paid.players[1].artifacts[0].tapped);
    CHECK(ring_paid.players[1].mana_pool ==
          old_school::ManaCost{.generic = 1});
    CHECK(ring_paid.stats[0].spells_countered == 0);

    auto declined = make_state(true);
    CHECK(old_school::apply_priority_action(
        declined, 0, action, false));
    CHECK(old_school::resolve_top_of_stack(
        declined, old_school::ForceSpikePaymentChoice::Decline));
    CHECK(declined.stack.empty());
    CHECK(!declined.players[1].lands[0].tapped);
    CHECK(declined.stats[0].spells_countered == 1);
}

TEST(force_spike_handles_a_missing_target_and_is_counterable) {
    old_school::GameState missing;
    missing.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 20,
            .card = old_school::CardId::ForceSpike,
            .controller = 0,
            .spell_target = 999,
        },
    };
    CHECK(old_school::resolve_top_of_stack(missing));
    CHECK(missing.stack.empty());
    CHECK(count_card(
              missing.players[0].graveyard,
              old_school::CardId::ForceSpike) == 1);
    CHECK(missing.stats[0].spells_countered == 0);

    old_school::GameState countered;
    countered.active_player = 1;
    countered.next_stack_object_id = 31;
    countered.players[0].hand = {
        old_school::CardId::ForceSpike,
    };
    countered.players[0].lands = {
        {.card = old_school::CardId::Island},
    };
    countered.players[1].hand = {
        old_school::CardId::Counterspell,
    };
    countered.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    countered.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 30,
            .card = old_school::CardId::GrayOgre,
            .controller = 1,
        },
    };
    CHECK(old_school::apply_priority_action(
        countered, 0,
        old_school::PriorityAction::cast_force_spike(30),
        false));
    const auto force_id = countered.stack.back().id;
    const auto counter_force =
        old_school::PriorityAction::cast_counterspell(force_id);
    CHECK(has_action(
        old_school::legal_priority_actions(
            countered, 1, false),
        counter_force));
    CHECK(old_school::apply_priority_action(
        countered, 1, counter_force, false));
    CHECK(old_school::resolve_top_of_stack(countered));
    CHECK(countered.stack.size() == 1);
    CHECK(countered.stack.back().id == 30);
    CHECK(count_card(
              countered.players[0].graveyard,
              old_school::CardId::ForceSpike) == 1);
    CHECK(count_card(
              countered.players[1].graveyard,
              old_school::CardId::Counterspell) == 1);

    old_school::GameState ability;
    ability.players[0].hand = {
        old_school::CardId::ForceSpike,
    };
    ability.players[0].lands = {
        {.card = old_school::CardId::Island},
    };
    ability.stack = {
        {
            .kind =
                old_school::StackObjectKind::ActivatedAbility,
            .id = 40,
            .card = old_school::CardId::Millstone,
            .controller = 1,
            .target =
                old_school::Target::player_target(0),
        },
    };
    CHECK(!has_action(
        old_school::legal_priority_actions(ability, 0, false),
        old_school::PriorityAction::cast_force_spike(40)));
}

TEST(lightning_bolt_can_damage_either_player) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain, .tapped = false},
    };
    const auto actions = old_school::legal_priority_actions(state, 0, true);
    CHECK(has_action(
        actions, old_school::PriorityAction::cast_lightning_bolt(
                     old_school::Target::player_target(0))));
    CHECK(has_action(
        actions, old_school::PriorityAction::cast_lightning_bolt(
                     old_school::Target::player_target(1))));

    CHECK(old_school::apply_priority_action(
        state, 0, old_school::PriorityAction::cast_lightning_bolt(
                      old_school::Target::player_target(1)),
        true));
    CHECK(state.players[1].life == 20);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[1].life == 17);
    CHECK(state.players[0].lands[0].tapped);
    CHECK(count_card(state.players[0].graveyard,
                     old_school::CardId::LightningBolt) == 1);
}

TEST(lightning_bolt_kills_a_grizzly_bears) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain, .tapped = false},
    };
    state.players[1].creatures = {bear(42)};

    const auto action = old_school::PriorityAction::cast_lightning_bolt(
        old_school::Target::creature_target(1, 42));
    CHECK(has_action(old_school::legal_priority_actions(state, 0, true), action));
    CHECK(old_school::apply_priority_action(state, 0, action, true));
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[1].creatures.empty());
    CHECK(count_card(state.players[1].graveyard,
                     old_school::CardId::GrizzlyBears) == 1);
}

TEST(lightning_bolt_does_not_kill_an_ironroot_treefolk) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain, .tapped = false},
    };
    state.players[1].creatures = {
        creature(42, old_school::CardId::IronrootTreefolk)};

    const auto action = old_school::PriorityAction::cast_lightning_bolt(
        old_school::Target::creature_target(1, 42));
    CHECK(old_school::apply_priority_action(state, 0, action, true));
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[1].creatures.size() == 1);
    CHECK(state.players[1].creatures[0].damage == 3);

    old_school::cleanup_turn(
        state, state.active_player, {});
    CHECK(state.players[1].creatures[0].damage == 0);
}

TEST(disintegrate_enumerates_every_affordable_x_and_target_at_sorcery_speed) {
    old_school::GameState state;
    state.active_player = 0;
    state.players[0].hand = {old_school::CardId::Disintegrate};
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Island},
    };
    state.players[0].creatures = {bear(1)};
    state.players[1].creatures = {
        creature(2, old_school::CardId::HillGiant),
    };

    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    const std::array<old_school::Target, 4> targets = {
        old_school::Target::player_target(0),
        old_school::Target::creature_target(0, 1),
        old_school::Target::player_target(1),
        old_school::Target::creature_target(1, 2),
    };
    for (int x_value = 0; x_value <= 2; ++x_value) {
        for (const auto& target : targets) {
            CHECK(has_action(
                actions,
                old_school::PriorityAction::cast_disintegrate(
                    x_value, target)));
        }
    }
    CHECK(std::count_if(
              actions.begin(), actions.end(),
              [](const old_school::PriorityAction& action) {
                  return action.kind ==
                         old_school::PriorityActionKind::CastDisintegrate;
              }) == 12);
    for (const auto& target : targets) {
        CHECK(!has_action(
            actions,
            old_school::PriorityAction::cast_disintegrate(3, target)));
    }
    const auto instant_actions =
        old_school::legal_priority_actions(state, 0, false);
    CHECK(std::none_of(
        instant_actions.begin(), instant_actions.end(),
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                   old_school::PriorityActionKind::CastDisintegrate;
        }));

    auto occupied_stack = state;
    occupied_stack.stack.push_back({
        .kind = old_school::StackObjectKind::Spell,
        .id = 90,
        .card = old_school::CardId::LightningBolt,
        .controller = 1,
        .target = old_school::Target::player_target(0),
        .spell_target = std::nullopt,
    });
    const auto occupied_actions =
        old_school::legal_priority_actions(occupied_stack, 0, true);
    CHECK(std::none_of(
        occupied_actions.begin(), occupied_actions.end(),
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                   old_school::PriorityActionKind::CastDisintegrate;
        }));

    state.players[1].hand = {old_school::CardId::Disintegrate};
    state.players[1].lands = {
        {.card = old_school::CardId::Mountain},
    };
    const auto inactive_actions =
        old_school::legal_priority_actions(state, 1, true);
    CHECK(std::none_of(
        inactive_actions.begin(), inactive_actions.end(),
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                   old_school::PriorityActionKind::CastDisintegrate;
        }));

    const auto cast = old_school::PriorityAction::cast_disintegrate(
        2, old_school::Target::player_target(1));
    CHECK(old_school::apply_priority_action(state, 0, cast, true));
    CHECK(state.players[0].hand.empty());
    CHECK(std::all_of(
        state.players[0].lands.begin(),
        state.players[0].lands.end(),
        [](const old_school::LandPermanent& land) {
            return land.tapped;
        }));
    CHECK(state.stack.size() == 1);
    CHECK(state.stack.back().card == old_school::CardId::Disintegrate);
    CHECK(state.stack.back().target ==
          old_school::Target::player_target(1));
    CHECK(state.stack.back().x_value == 2);
    CHECK(state.players[1].life == 20);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[1].life == 18);
    CHECK(state.stats[0].damage_to_opponent == 2);
    CHECK(count_card(
              state.players[0].graveyard,
              old_school::CardId::Disintegrate) == 1);
}

TEST(disintegrate_x_zero_spends_only_red_and_deals_no_damage) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::Disintegrate};
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Island},
    };
    state.players[1].creatures = {bear(7)};
    const auto cast = old_school::PriorityAction::cast_disintegrate(
        0, old_school::Target::creature_target(1, 7));
    CHECK(old_school::apply_priority_action(state, 0, cast, true));
    CHECK(state.players[0].lands[0].tapped);
    CHECK(!state.players[0].lands[1].tapped);
    CHECK(state.stack.back().x_value == 0);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[1].creatures.size() == 1);
    CHECK(state.players[1].creatures[0].damage == 0);
    CHECK(!state.players[1].creatures[0].exile_on_death_this_turn);
}

TEST(disintegrate_is_counterable_and_fizzles_on_a_missing_target) {
    old_school::GameState countered;
    countered.players[0].hand = {old_school::CardId::Disintegrate};
    countered.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
    };
    countered.players[1].hand = {old_school::CardId::Counterspell};
    countered.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    CHECK(old_school::apply_priority_action(
        countered, 0,
        old_school::PriorityAction::cast_disintegrate(
            2, old_school::Target::player_target(1)),
        true));
    const auto disintegrate_id = countered.stack.back().id;
    const auto counter =
        old_school::PriorityAction::cast_counterspell(disintegrate_id);
    CHECK(has_action(
        old_school::legal_priority_actions(countered, 1, false),
        counter));
    CHECK(old_school::apply_priority_action(
        countered, 1, counter, false));
    CHECK(old_school::resolve_top_of_stack(countered));
    CHECK(countered.stack.empty());
    CHECK(countered.players[1].life == 20);
    CHECK(count_card(
              countered.players[0].graveyard,
              old_school::CardId::Disintegrate) == 1);
    CHECK(count_card(
              countered.players[1].graveyard,
              old_school::CardId::Counterspell) == 1);
    CHECK(countered.stats[1].spells_countered == 1);

    old_school::GameState fizzled;
    fizzled.players[0].hand = {old_school::CardId::Disintegrate};
    fizzled.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
    };
    fizzled.players[1].creatures = {bear(17)};
    CHECK(old_school::apply_priority_action(
        fizzled, 0,
        old_school::PriorityAction::cast_disintegrate(
            1, old_school::Target::creature_target(1, 17)),
        true));
    fizzled.players[1].graveyard.push_back(
        fizzled.players[1].creatures[0].card);
    fizzled.players[1].creatures.clear();
    CHECK(old_school::resolve_top_of_stack(fizzled));
    CHECK(fizzled.players[1].exile.empty());
    CHECK(count_card(
              fizzled.players[0].graveyard,
              old_school::CardId::Disintegrate) == 1);
}

TEST(disintegrate_exiles_immediate_and_later_same_turn_deaths) {
    old_school::GameState immediate;
    immediate.players[0].hand = {old_school::CardId::Disintegrate};
    immediate.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
    };
    immediate.players[1].creatures = {bear(8)};
    CHECK(old_school::apply_priority_action(
        immediate, 0,
        old_school::PriorityAction::cast_disintegrate(
            2, old_school::Target::creature_target(1, 8)),
        true));
    CHECK(old_school::resolve_top_of_stack(immediate));
    CHECK(immediate.players[1].creatures.empty());
    CHECK(immediate.players[1].graveyard.empty());
    CHECK((immediate.players[1].exile ==
           std::vector<old_school::CardId>{
               old_school::CardId::GrizzlyBears}));

    old_school::GameState later;
    later.players[0].hand = {old_school::CardId::Disintegrate};
    later.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
    };
    later.players[1].hand = {old_school::CardId::LightningBolt};
    later.players[1].lands = {
        {.card = old_school::CardId::Mountain},
    };
    later.players[1].creatures = {
        creature(18, old_school::CardId::IronrootTreefolk),
    };
    CHECK(old_school::apply_priority_action(
        later, 0,
        old_school::PriorityAction::cast_disintegrate(
            2, old_school::Target::creature_target(1, 18)),
        true));
    CHECK(old_school::resolve_top_of_stack(later));
    CHECK(later.players[1].creatures.size() == 1);
    CHECK(later.players[1].creatures[0].damage == 2);
    CHECK(later.players[1].creatures[0].exile_on_death_this_turn);
    CHECK(old_school::apply_priority_action(
        later, 1,
        old_school::PriorityAction::cast_lightning_bolt(
            old_school::Target::creature_target(1, 18)),
        false));
    CHECK(old_school::resolve_top_of_stack(later));
    CHECK(later.players[1].creatures.empty());
    CHECK(count_card(
              later.players[1].exile,
              old_school::CardId::IronrootTreefolk) == 1);
    CHECK(count_card(
              later.players[1].graveyard,
              old_school::CardId::IronrootTreefolk) == 0);

    old_school::GameState cleaned;
    cleaned.players[0].hand = {old_school::CardId::Disintegrate};
    cleaned.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Mountain},
    };
    cleaned.players[1].creatures = {
        creature(28, old_school::CardId::IronrootTreefolk),
    };
    CHECK(old_school::apply_priority_action(
        cleaned, 0,
        old_school::PriorityAction::cast_disintegrate(
            1, old_school::Target::creature_target(1, 28)),
        true));
    CHECK(old_school::resolve_top_of_stack(cleaned));
    CHECK(cleaned.players[1].creatures[0].damage == 1);
    CHECK(cleaned.players[1].creatures[0].exile_on_death_this_turn);
    old_school::cleanup_turn(
        cleaned, cleaned.active_player, {});
    CHECK(cleaned.players[1].creatures[0].damage == 0);
    CHECK(!cleaned.players[1].creatures[0].exile_on_death_this_turn);
}

TEST(two_consecutive_passes_resolve_the_stack_then_end_the_window) {
    old_school::GameState state;
    state.active_player = 0;
    state.players[0].hand = {old_school::CardId::LightningBolt};
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain},
    };
    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_lightning_bolt(
            old_school::Target::player_target(1)),
        true));

    old_school::PriorityState priority = {
        .player = 0,
        .consecutive_passes = 0,
    };
    CHECK(old_school::pass_priority(state, priority) ==
          old_school::PriorityPassResult::Passed);
    CHECK(priority.player == 1);
    CHECK(state.stack.size() == 1);
    CHECK(state.players[1].life == 20);

    CHECK(old_school::pass_priority(state, priority) ==
          old_school::PriorityPassResult::StackObjectResolved);
    CHECK(priority.player == state.active_player);
    CHECK(priority.consecutive_passes == 0);
    CHECK(state.stack.empty());
    CHECK(state.players[1].life == 17);

    CHECK(old_school::pass_priority(state, priority) ==
          old_school::PriorityPassResult::Passed);
    CHECK(old_school::pass_priority(state, priority) ==
          old_school::PriorityPassResult::WindowEnded);
}

TEST(counterspell_counters_a_creature_spell_on_the_stack) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::GrizzlyBears};
    state.players[0].lands = {
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
    };
    state.players[1].hand = {old_school::CardId::Counterspell};
    state.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_creature(
            old_school::CardId::GrizzlyBears),
        true));
    const auto bear_spell = state.stack.back().id;
    const auto counter =
        old_school::PriorityAction::cast_counterspell(bear_spell);
    CHECK(has_action(
        old_school::legal_priority_actions(state, 1, false), counter));
    CHECK(old_school::apply_priority_action(state, 1, counter, false));
    CHECK(state.stack.size() == 2);

    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.stack.empty());
    CHECK(state.players[0].creatures.empty());
    CHECK(count_card(state.players[0].graveyard,
                     old_school::CardId::GrizzlyBears) == 1);
    CHECK(count_card(state.players[1].graveyard,
                     old_school::CardId::Counterspell) == 1);
    CHECK(state.stats[1].spells_countered == 1);
}

TEST(countering_a_counterspell_leaves_the_original_spell_to_resolve) {
    old_school::GameState state;
    state.players[0].hand = {
        old_school::CardId::LightningBolt,
        old_school::CardId::Counterspell,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    state.players[1].hand = {old_school::CardId::Counterspell};
    state.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_lightning_bolt(
            old_school::Target::player_target(1)),
        true));
    const auto bolt_spell = state.stack.back().id;
    CHECK(old_school::apply_priority_action(
        state, 1,
        old_school::PriorityAction::cast_counterspell(bolt_spell), false));
    const auto first_counterspell = state.stack.back().id;
    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_counterspell(first_counterspell),
        false));

    CHECK(state.stack.size() == 3);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.stack.size() == 1);
    CHECK(state.stack.back().card == old_school::CardId::LightningBolt);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.stack.empty());
    CHECK(state.players[1].life == 17);
    CHECK(state.stats[0].spells_countered == 1);
    CHECK(state.stats[1].spells_countered == 0);
}

TEST(giant_growth_is_an_instant_that_can_save_a_bear_from_bolt) {
    old_school::GameState state;
    state.active_player = 1;
    state.players[0].hand = {old_school::CardId::GiantGrowth};
    state.players[0].lands = {
        {.card = old_school::CardId::Forest},
    };
    state.players[0].creatures = {bear(31)};
    state.players[1].hand = {old_school::CardId::LightningBolt};
    state.players[1].lands = {
        {.card = old_school::CardId::Mountain},
    };
    state.players[1].creatures = {
        creature(32, old_school::CardId::HillGiant),
    };

    CHECK(old_school::apply_priority_action(
        state, 1,
        old_school::PriorityAction::cast_lightning_bolt(
            old_school::Target::creature_target(0, 31)),
        false));
    const auto growth_on_bear =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 31));
    const auto growth_on_opponent =
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(1, 32));
    const auto response_actions =
        old_school::legal_priority_actions(state, 0, false);
    CHECK(has_action(response_actions, growth_on_bear));
    CHECK(has_action(response_actions, growth_on_opponent));
    CHECK(old_school::apply_priority_action(
        state, 0, growth_on_bear, false));
    CHECK(state.players[0].lands[0].tapped);
    CHECK(state.stack.size() == 2);
    CHECK(state.stack.back().card == old_school::CardId::GiantGrowth);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures[0].temporary_power_bonus == 3);
    CHECK(state.players[0].creatures[0].temporary_toughness_bonus == 3);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].damage == 3);
    CHECK(count_card(
              state.players[0].graveyard,
              old_school::CardId::GiantGrowth) == 1);
    CHECK(count_card(
              state.players[1].graveyard,
              old_school::CardId::LightningBolt) == 1);

    old_school::cleanup_turn(
        state, state.active_player, {});
    CHECK(state.players[0].creatures[0].damage == 0);
    CHECK(state.players[0].creatures[0].temporary_power_bonus == 0);
    CHECK(state.players[0].creatures[0].temporary_toughness_bonus == 0);

    old_school::GameState wrong_mana;
    wrong_mana.players[0].hand = {old_school::CardId::GiantGrowth};
    wrong_mana.players[0].lands = {
        {.card = old_school::CardId::Island},
    };
    wrong_mana.players[0].creatures = {bear(33)};
    CHECK(!has_action(
        old_school::legal_priority_actions(wrong_mana, 0, false),
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 33))));
}

TEST(giant_growth_is_counterable_and_fizzles_on_a_dead_creature) {
    old_school::GameState countered;
    countered.players[0].hand = {old_school::CardId::GiantGrowth};
    countered.players[0].lands = {
        {.card = old_school::CardId::Forest},
    };
    countered.players[0].creatures = {bear(41)};
    countered.players[1].hand = {old_school::CardId::Counterspell};
    countered.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    CHECK(old_school::apply_priority_action(
        countered, 0,
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 41)),
        false));
    const auto growth_id = countered.stack.back().id;
    CHECK(old_school::apply_priority_action(
        countered, 1,
        old_school::PriorityAction::cast_counterspell(growth_id),
        false));
    CHECK(old_school::resolve_top_of_stack(countered));
    CHECK(countered.stack.empty());
    CHECK(countered.players[0].creatures[0].temporary_power_bonus == 0);
    CHECK(countered.players[0].creatures[0].temporary_toughness_bonus == 0);
    CHECK(count_card(
              countered.players[0].graveyard,
              old_school::CardId::GiantGrowth) == 1);

    old_school::GameState fizzled;
    fizzled.players[0].hand = {old_school::CardId::GiantGrowth};
    fizzled.players[0].lands = {
        {.card = old_school::CardId::Forest},
    };
    fizzled.players[0].creatures = {bear(51)};
    fizzled.players[1].hand = {old_school::CardId::LightningBolt};
    fizzled.players[1].lands = {
        {.card = old_school::CardId::Mountain},
    };
    CHECK(old_school::apply_priority_action(
        fizzled, 0,
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 51)),
        false));
    CHECK(old_school::apply_priority_action(
        fizzled, 1,
        old_school::PriorityAction::cast_lightning_bolt(
            old_school::Target::creature_target(0, 51)),
        false));
    CHECK(old_school::resolve_top_of_stack(fizzled));
    CHECK(fizzled.players[0].creatures.empty());
    CHECK(old_school::resolve_top_of_stack(fizzled));
    CHECK(fizzled.stack.empty());
    CHECK(count_card(
              fizzled.players[0].graveyard,
              old_school::CardId::GiantGrowth) == 1);
    CHECK(count_card(
              fizzled.players[0].graveyard,
              old_school::CardId::GrizzlyBears) == 1);
}

TEST(giant_growth_changes_combat_damage_and_expires_at_cleanup) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::GiantGrowth};
    state.players[0].lands = {
        {.card = old_school::CardId::Forest},
    };
    state.players[0].creatures = {bear(61)};
    state.players[1].creatures = {
        creature(62, old_school::CardId::HillGiant),
    };
    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 61)),
        false));
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(old_school::resolve_combat(state, 0, {61}, {{61, 62}}));
    CHECK(state.players[0].creatures.size() == 1);
    CHECK(state.players[0].creatures[0].damage == 3);
    CHECK(state.players[0].creatures[0].temporary_power_bonus == 3);
    CHECK(state.players[1].creatures.empty());
    CHECK(count_card(
              state.players[1].graveyard,
              old_school::CardId::HillGiant) == 1);
    old_school::cleanup_turn(
        state, state.active_player, {});
    CHECK(state.players[0].creatures[0].damage == 0);
    CHECK(state.players[0].creatures[0].temporary_power_bonus == 0);
    CHECK(state.players[0].creatures[0].temporary_toughness_bonus == 0);
}

TEST(tsunami_uses_the_stack_and_destroys_all_islands) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::Tsunami};
    state.players[0].lands = {
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
    };
    state.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Mountain},
        {.card = old_school::CardId::Island},
    };

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_sorcery(old_school::CardId::Tsunami),
        true));
    CHECK(state.stack.size() == 1);
    CHECK(state.players[1].lands.size() == 3);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[1].lands.size() == 1);
    CHECK(state.players[1].lands[0].card == old_school::CardId::Mountain);
    CHECK(count_card(state.players[1].graveyard,
                     old_school::CardId::Island) == 2);
    CHECK(count_card(state.players[0].graveyard,
                     old_school::CardId::Tsunami) == 1);
}

TEST(millstone_spell_and_activated_ability_use_the_stack) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::Millstone};
    state.players[0].lands = {
        {.card = old_school::CardId::Plains},
        {.card = old_school::CardId::Plains},
        {.card = old_school::CardId::Plains},
        {.card = old_school::CardId::Plains},
    };

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_artifact(old_school::CardId::Millstone),
        true));
    CHECK(state.players[0].artifacts.empty());
    CHECK(state.stack.size() == 1);
    CHECK(state.stack.back().kind == old_school::StackObjectKind::Spell);
    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].artifacts.size() == 1);

    old_school::begin_turn(state, 0);
    state.players[1].library = {
        old_school::CardId::Forest,
        old_school::CardId::GrizzlyBears,
        old_school::CardId::Mountain,
    };
    state.players[1].hand = {old_school::CardId::Counterspell};
    state.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };

    const auto millstone_id = state.players[0].artifacts[0].id;
    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::activate_millstone(
            millstone_id, old_school::Target::player_target(1)),
        false));
    CHECK(state.players[0].artifacts[0].tapped);
    CHECK(state.stack.size() == 1);
    CHECK(state.stack.back().kind ==
          old_school::StackObjectKind::ActivatedAbility);
    CHECK(state.players[1].library.size() == 3);

    const auto counter_ability =
        old_school::PriorityAction::cast_counterspell(state.stack.back().id);
    CHECK(!has_action(
        old_school::legal_priority_actions(state, 1, false),
        counter_ability));

    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[1].library.size() == 1);
    CHECK(state.players[1].graveyard.size() == 2);
    CHECK(state.stats[0].cards_milled == 2);
}

TEST(moat_is_a_counterable_spell_and_stops_ground_attackers) {
    old_school::GameState state;
    state.players[0].hand = {old_school::CardId::Moat};
    state.players[0].lands = {
        {.card = old_school::CardId::Plains},
        {.card = old_school::CardId::Plains},
        {.card = old_school::CardId::Plains},
        {.card = old_school::CardId::Plains},
    };
    state.players[1].hand = {old_school::CardId::Counterspell};
    state.players[1].lands = {
        {.card = old_school::CardId::Island},
        {.card = old_school::CardId::Island},
    };
    state.players[1].creatures = {bear(99)};

    CHECK(old_school::apply_priority_action(
        state, 0,
        old_school::PriorityAction::cast_enchantment(old_school::CardId::Moat),
        true));
    CHECK(state.players[0].enchantments.empty());
    const auto moat_spell = state.stack.back().id;
    CHECK(has_action(
        old_school::legal_priority_actions(state, 1, false),
        old_school::PriorityAction::cast_counterspell(moat_spell)));

    CHECK(old_school::resolve_top_of_stack(state));
    CHECK(state.players[0].enchantments.size() == 1);
    CHECK(state.players[0].enchantments[0] == old_school::CardId::Moat);
    CHECK(!old_school::resolve_combat(state, 1, {99}, {}));
}

TEST(flying_and_ironclaw_block_restrictions_are_enforced_without_mutation) {
    old_school::GameState moat;
    moat.players[1].enchantments = {old_school::CardId::Moat};
    moat.players[0].creatures = {
        creature(1, old_school::CardId::FlyingMen),
        bear(2),
    };
    CHECK(old_school::resolve_combat(moat, 0, {1}, {}));
    CHECK(moat.players[1].life == 19);
    CHECK(moat.players[0].creatures[0].tapped);

    const int life_before_ground_attack = moat.players[1].life;
    CHECK(!old_school::resolve_combat(moat, 0, {2}, {}));
    CHECK(moat.players[1].life == life_before_ground_attack);
    CHECK(!moat.players[0].creatures[1].tapped);
    CHECK(moat.players[0].creatures[1].damage == 0);
    CHECK(moat.players[0].graveyard.empty());

    old_school::GameState illegal_flying_block;
    illegal_flying_block.players[0].creatures = {
        creature(11, old_school::CardId::FlyingMen),
    };
    illegal_flying_block.players[1].creatures = {bear(12)};
    CHECK(!old_school::resolve_combat(
        illegal_flying_block, 0, {11}, {{11, 12}}));
    CHECK(!illegal_flying_block.players[0].creatures[0].tapped);
    CHECK(illegal_flying_block.players[0].creatures[0].damage == 0);
    CHECK(illegal_flying_block.players[1].creatures[0].damage == 0);
    CHECK(illegal_flying_block.players[0].graveyard.empty());
    CHECK(illegal_flying_block.players[1].graveyard.empty());
    CHECK(illegal_flying_block.players[1].life == 20);

    old_school::GameState legal_flying_block;
    legal_flying_block.players[0].creatures = {
        creature(21, old_school::CardId::FlyingMen),
    };
    legal_flying_block.players[1].creatures = {
        creature(22, old_school::CardId::FlyingMen),
    };
    CHECK(old_school::resolve_combat(
        legal_flying_block, 0, {21}, {{21, 22}}));
    CHECK(legal_flying_block.players[0].creatures.empty());
    CHECK(legal_flying_block.players[1].creatures.empty());

    old_school::GameState illegal_ironclaw_block;
    illegal_ironclaw_block.players[0].creatures = {bear(31)};
    illegal_ironclaw_block.players[1].creatures = {
        creature(32, old_school::CardId::IronclawOrcs),
    };
    CHECK(!old_school::resolve_combat(
        illegal_ironclaw_block, 0, {31}, {{31, 32}}));
    CHECK(!illegal_ironclaw_block.players[0].creatures[0].tapped);
    CHECK(illegal_ironclaw_block.players[0].creatures[0].damage == 0);
    CHECK(illegal_ironclaw_block.players[1].creatures[0].damage == 0);
    CHECK(illegal_ironclaw_block.players[0].graveyard.empty());
    CHECK(illegal_ironclaw_block.players[1].graveyard.empty());
    CHECK(illegal_ironclaw_block.players[1].life == 20);

    old_school::GameState ordinary_block;
    ordinary_block.players[0].creatures = {bear(41)};
    ordinary_block.players[1].creatures = {
        creature(42, old_school::CardId::GrayOgre),
    };
    CHECK(old_school::resolve_combat(
        ordinary_block, 0, {41}, {{41, 42}}));
    CHECK(ordinary_block.players[0].creatures.empty());
    CHECK(ordinary_block.players[1].creatures.empty());
}

TEST(legal_attackers_is_engine_authoritative_and_preserves_order) {
    old_school::GameState state;
    state.players[0].creatures = {
        creature(1, old_school::CardId::FlyingMen),
        bear(2),
        creature(3, old_school::CardId::HillGiant, true),
        bear(4, false, true),
    };
    CHECK(old_school::legal_attackers(state, 0) ==
          std::vector<old_school::PermanentId>({1, 2}));

    state.players[1].enchantments = {old_school::CardId::Moat};
    CHECK(old_school::legal_attackers(state, 0) ==
          std::vector<old_school::PermanentId>({1}));

    bool rejected_invalid_player = false;
    try {
        static_cast<void>(old_school::legal_attackers(state, 2));
    } catch (const std::out_of_range&) {
        rejected_invalid_player = true;
    }
    CHECK(rejected_invalid_player);
}

TEST(grizzly_bears_trade_in_combat) {
    old_school::GameState state;
    state.players[0].creatures = {bear(1)};
    state.players[1].creatures = {bear(2)};
    CHECK(old_school::resolve_combat(state, 0, {1}, {{1, 2}}));
    CHECK(state.players[0].creatures.empty());
    CHECK(state.players[1].creatures.empty());
    CHECK(state.players[0].life == 20);
    CHECK(state.players[1].life == 20);
}

TEST(tapped_creatures_cannot_block) {
    old_school::GameState state;
    state.players[0].creatures = {bear(1)};
    state.players[1].creatures = {bear(2, false, true)};
    CHECK(!old_school::resolve_combat(state, 0, {1}, {{1, 2}}));
}

TEST(one_hundred_seeded_games_complete) {
    const auto result = old_school::run_simulation(100, 0xA11FAULL);
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

TEST(random_bot_records_decisions_without_monte_carlo_rollouts) {
    const auto result = old_school::run_simulation(20, 0xBAD5EEDULL);
    const auto& random =
        result.bots[static_cast<std::size_t>(old_school::BotKind::Random)];
    const auto& monte_carlo =
        result.bots[static_cast<std::size_t>(
            old_school::BotKind::MonteCarlo)];

    CHECK(random.games == 40);
    CHECK(random.wins + random.losses + random.draws == 40);
    CHECK(random.total_decisions > 0);
    CHECK(random.total_rollouts == 0);
    CHECK(monte_carlo.games == 0);
}

TEST(monte_carlo_bot_runs_complete_random_continuations) {
    old_school::GameConfig config;
    config.bots[0] = {
        .kind = old_school::BotKind::MonteCarlo,
        .rollouts_per_action = 2,
    };
    config.bots[1] = {
        .kind = old_school::BotKind::Random,
        .rollouts_per_action = 2,
    };

    const auto result =
        old_school::run_simulation(5, 0xC001D00DULL, config);
    const auto& random =
        result.bots[static_cast<std::size_t>(old_school::BotKind::Random)];
    const auto& monte_carlo =
        result.bots[static_cast<std::size_t>(
            old_school::BotKind::MonteCarlo)];

    CHECK(random.games == 5);
    CHECK(monte_carlo.games == 5);
    CHECK(monte_carlo.total_decisions > 0);
    CHECK(monte_carlo.total_rollouts > 0);
    CHECK(monte_carlo.total_rollouts >=
          monte_carlo.total_decisions * 4);
    CHECK(random.total_rollouts == 0);
}

TEST(deck_evolution_uses_the_metagame_card_pool_and_is_deterministic) {
    const old_school::DeckEvolutionConfig config = {
        .generations = 2,
        .population = 5,
        .repetitions_per_opponent = 1,
        .pilot =
            {
                .kind = old_school::BotKind::Handcrafted,
                .rollouts_per_action = 1,
            },
    };
    const auto first = old_school::evolve_deck(config, 0xE701EULL);
    const auto repeated = old_school::evolve_deck(config, 0xE701EULL);

    CHECK(first.generation_best_win_rates.size() == 2);
    CHECK(first.best.cards.size() == 40);
    CHECK(first.best.by_opponent.size() == 5);
    CHECK(first.best.total.games == 20);
    CHECK(first.best.total.wins + first.best.total.losses +
              first.best.total.draws ==
          20);
    for (const auto& matchup : first.best.by_opponent) {
        CHECK(matchup.games == 4);
    }
    std::vector<old_school::CardId> metagame_pool;
    for (auto deck : {
             old_school::green_deck(),
             old_school::red_deck(),
             old_school::blue_deck(),
             old_school::white_control_deck(),
             old_school::ru_aggro_deck(),
         }) {
        metagame_pool.insert(
            metagame_pool.end(), deck.begin(), deck.end());
    }
    for (const old_school::CardId card : first.best.cards) {
        CHECK(std::find(
                  metagame_pool.begin(), metagame_pool.end(),
                  card) != metagame_pool.end());
    }
    CHECK(first.best.cards == repeated.best.cards);
    CHECK(first.best.total.wins == repeated.best.total.wins);
    CHECK(first.generation_best_win_rates ==
          repeated.generation_best_win_rates);
}

std::vector<old_school::CardId> two_card_deck(
    old_school::CardId first, old_school::CardId second) {
    std::vector<old_school::CardId> deck(20, first);
    deck.insert(deck.end(), 20, second);
    return deck;
}

std::size_t priority_action_index(
    const std::vector<old_school::PriorityAction>& actions,
    old_school::PriorityActionKind kind) {
    const auto action = std::find_if(
        actions.begin(), actions.end(),
        [kind](const old_school::PriorityAction& candidate) {
            return candidate.kind == kind;
        });
    if (action == actions.end()) {
        throw std::runtime_error("scripted action is unavailable");
    }
    return static_cast<std::size_t>(
        std::distance(actions.begin(), action));
}

old_school::GameState pass_dominance_braingeyser_state() {
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 8;
    state.next_stack_object_id = 20;
    state.players[0].hand = {
        old_school::CardId::Braingeyser,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    state.players[1].hand = {
        old_school::CardId::Mountain,
    };

    state.players[0].library = old_school::blue_deck();
    remove_fixture_card(
        state.players[0].library,
        old_school::CardId::Braingeyser);
    remove_fixture_card(
        state.players[0].library,
        old_school::CardId::Island);
    remove_fixture_card(
        state.players[0].library,
        old_school::CardId::Island);
    remove_fixture_card(
        state.players[0].library,
        old_school::CardId::Island);
    state.players[1].library = old_school::red_deck();
    remove_fixture_card(
        state.players[1].library,
        old_school::CardId::Mountain);
    return state;
}

const old_school::ValuePassDominanceActionDiagnostic&
pass_dominance_action(
    const old_school::ValuePassDominanceDiagnostic& diagnostic,
    const std::function<bool(
        const old_school::PriorityAction&)>& predicate) {
    const auto found = std::find_if(
        diagnostic.actions.begin(), diagnostic.actions.end(),
        [&](const auto& action) {
            return predicate(action.action);
        });
    if (found == diagnostic.actions.end()) {
        throw std::runtime_error(
            "Pass-dominance action is unavailable");
    }
    return *found;
}

TEST(pass_dominance_filters_x_zero_and_preserves_productive_x) {
    const auto state = pass_dominance_braingeyser_state();
    const auto diagnostic =
        old_school::diagnose_value_pass_dominance(
            state, 0, true,
            old_school::TurnPhase::SecondMain, 0);
    const auto is_braingeyser =
        [](const old_school::PriorityAction& action,
           int x_value, std::size_t target) {
            return action.kind ==
                       old_school::PriorityActionKind::
                           CastBraingeyser &&
                   action.x_value == x_value &&
                   action.target.has_value() &&
                   !action.target->creature.has_value() &&
                   action.target->player == target;
        };

    CHECK(diagnostic.pass_settled);
    CHECK(diagnostic.failed_comparisons == 0);
    CHECK(diagnostic.actions.size() ==
          old_school::legal_priority_actions(state, 0, true).size());
    for (const std::size_t target : {std::size_t{0},
                                     std::size_t{1}}) {
        const auto& x_zero = pass_dominance_action(
            diagnostic,
            [&](const old_school::PriorityAction& action) {
                return is_braingeyser(action, 0, target);
            });
        CHECK(x_zero.comparison_settled);
        CHECK(x_zero.strictly_dominated_by_pass);

        const auto& x_one = pass_dominance_action(
            diagnostic,
            [&](const old_school::PriorityAction& action) {
                return is_braingeyser(action, 1, target);
            });
        CHECK(x_one.comparison_settled);
        CHECK(!x_one.strictly_dominated_by_pass);
    }
    CHECK(diagnostic.retained_actions().size() + 2 ==
          diagnostic.actions.size());

    const auto hidden =
        hidden_repartition(state, 0);
    auto own_library_hidden = state;
    std::reverse(
        own_library_hidden.players[0].library.begin(),
        own_library_hidden.players[0].library.end());
    auto opponent_hidden = state;
    std::swap(
        opponent_hidden.players[1].hand.front(),
        opponent_hidden.players[1].library.front());
    CHECK(old_school::diagnose_value_pass_dominance(
              hidden, 0, true,
              old_school::TurnPhase::SecondMain, 0) ==
          diagnostic);
    CHECK(old_school::diagnose_value_pass_dominance(
              own_library_hidden, 0, true,
              old_school::TurnPhase::SecondMain, 0) ==
          diagnostic);
    CHECK(old_school::diagnose_value_pass_dominance(
              opponent_hidden, 0, true,
              old_school::TurnPhase::SecondMain, 0) ==
          diagnostic);

}

TEST(pass_dominance_preserves_declared_tradeoff_controls) {
    old_school::GameState disintegrate;
    disintegrate.active_player = 0;
    disintegrate.turn_number = 8;
    disintegrate.players[0].hand = {
        old_school::CardId::Disintegrate,
    };
    disintegrate.players[0].lands = {
        {.card = old_school::CardId::Mountain, .tapped = false},
        {.card = old_school::CardId::Mountain, .tapped = false},
    };
    const auto disintegrate_diagnostic =
        old_school::diagnose_value_pass_dominance(
            disintegrate, 0, true,
            old_school::TurnPhase::SecondMain, 0);
    const auto& x_zero = pass_dominance_action(
        disintegrate_diagnostic,
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                       old_school::PriorityActionKind::
                           CastDisintegrate &&
                   action.x_value == 0;
        });
    const auto& x_one = pass_dominance_action(
        disintegrate_diagnostic,
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                       old_school::PriorityActionKind::
                           CastDisintegrate &&
                   action.x_value == 1;
        });
    CHECK(x_zero.strictly_dominated_by_pass);
    CHECK(!x_one.strictly_dominated_by_pass);

    const auto force_spike_state = [](bool payable) {
        old_school::GameState state;
        state.active_player = 1;
        state.turn_number = 6;
        state.next_permanent_id = 2;
        state.next_stack_object_id = 2;
        state.players[0].hand = {
            old_school::CardId::ForceSpike,
        };
        state.players[0].lands = {
            {.card = old_school::CardId::Island,
             .tapped = false},
        };
        if (payable) {
            state.players[1].lands.push_back(
                {.card = old_school::CardId::Mountain,
                 .tapped = false});
        }
        state.stack = {
            {
                .kind = old_school::StackObjectKind::Spell,
                .id = 1,
                .card = old_school::CardId::GrayOgre,
                .controller = 1,
            },
        };
        return state;
    };
    for (const bool payable : {false, true}) {
        const auto force_spike =
            old_school::diagnose_value_pass_dominance(
                force_spike_state(payable), 0, false,
                old_school::TurnPhase::FirstMain, 0);
        const auto& spike = pass_dominance_action(
            force_spike,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                       old_school::PriorityActionKind::
                           CastForceSpike;
            });
        CHECK(spike.comparison_settled);
        CHECK(!spike.strictly_dominated_by_pass);
    }

    old_school::GameState own_spell;
    own_spell.active_player = 0;
    own_spell.turn_number = 5;
    own_spell.next_stack_object_id = 2;
    own_spell.players[0].hand = {
        old_school::CardId::Counterspell,
    };
    own_spell.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    own_spell.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::FlyingMen,
            .controller = 0,
        },
    };
    const auto own_counter =
        old_school::diagnose_value_pass_dominance(
            own_spell, 0, false,
            old_school::TurnPhase::FirstMain, 0);
    const auto& counter = pass_dominance_action(
        own_counter,
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                       old_school::PriorityActionKind::
                           CastCounterspell;
        });
    CHECK(counter.comparison_settled);
    CHECK(!counter.strictly_dominated_by_pass);
}

TEST(pass_dominance_filters_only_redundant_same_target_counter) {
    old_school::GameState state;
    state.active_player = 1;
    state.starting_player = 1;
    state.turn_number = 7;
    state.next_stack_object_id = 4;
    state.players[0].hand = {
        old_school::CardId::Counterspell,
        old_school::CardId::Island,
    };
    state.players[0].library = {
        old_school::CardId::FlyingMen,
        old_school::CardId::Island,
    };
    state.players[1].hand = {
        old_school::CardId::Mountain,
    };
    state.players[1].library = {
        old_school::CardId::LightningBolt,
        old_school::CardId::Mountain,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::AirElemental,
            .controller = 1,
        },
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 2,
            .card = old_school::CardId::Counterspell,
            .controller = 0,
            .spell_target = 1,
        },
    };

    const auto diagnostic =
        old_school::diagnose_value_pass_dominance(
            state, 0, false,
            old_school::TurnPhase::FirstMain, 0);
    const auto& redundant = pass_dominance_action(
        diagnostic,
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                       old_school::PriorityActionKind::
                           CastCounterspell &&
                   action.spell_target == 1;
        });
    const auto& counter_own_counter = pass_dominance_action(
        diagnostic,
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                       old_school::PriorityActionKind::
                           CastCounterspell &&
                   action.spell_target == 2;
        });
    CHECK(redundant.comparison_settled);
    CHECK(redundant.strictly_dominated_by_pass);
    CHECK(counter_own_counter.comparison_settled);
    CHECK(!counter_own_counter.strictly_dominated_by_pass);
    CHECK(old_school::diagnose_value_pass_dominance(
              hidden_repartition(state, 0), 0, false,
              old_school::TurnPhase::FirstMain, 0) ==
          diagnostic);

    old_school::GameState opposing_counter = state;
    opposing_counter.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::FlyingMen,
            .controller = 0,
        },
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 2,
            .card = old_school::CardId::Counterspell,
            .controller = 1,
            .spell_target = 1,
        },
    };
    const auto productive =
        old_school::diagnose_value_pass_dominance(
            opposing_counter, 0, false,
            old_school::TurnPhase::FirstMain, 0);
    const auto& answer = pass_dominance_action(
        productive,
        [](const old_school::PriorityAction& action) {
            return action.kind ==
                       old_school::PriorityActionKind::
                           CastCounterspell &&
                   action.spell_target == 2;
        });
    CHECK(answer.comparison_settled);
    CHECK(!answer.strictly_dominated_by_pass);

    old_school::GameState counter_war = state;
    counter_war.next_stack_object_id = 5;
    counter_war.stack.push_back({
        .kind = old_school::StackObjectKind::Spell,
        .id = 3,
        .card = old_school::CardId::Counterspell,
        .controller = 1,
        .spell_target = 2,
    });
    const auto counter_war_diagnostic =
        old_school::diagnose_value_pass_dominance(
            counter_war, 0, false,
            old_school::TurnPhase::FirstMain, 0);
    const auto& counter_war_same_target =
        pass_dominance_action(
            counter_war_diagnostic,
            [](const old_school::PriorityAction& action) {
                return action.kind ==
                           old_school::PriorityActionKind::
                               CastCounterspell &&
                       action.spell_target == 1;
            });
    CHECK(counter_war_same_target.comparison_settled);
    CHECK(!counter_war_same_target
               .strictly_dominated_by_pass);
}

old_school::HumanController developing_human_controller() {
    return {
        .choose_priority_action =
            [](const old_school::PlayerObservation&,
               old_school::TurnPhase,
               const std::vector<old_school::PriorityAction>& actions) {
                for (const auto kind : {
                         old_school::PriorityActionKind::PlayLand,
                         old_school::PriorityActionKind::CastCreature,
                     }) {
                    const auto action = std::find_if(
                        actions.begin(), actions.end(),
                        [kind](const old_school::PriorityAction& candidate) {
                            return candidate.kind == kind;
                        });
                    if (action != actions.end()) {
                        return static_cast<std::size_t>(
                            std::distance(actions.begin(), action));
                    }
                }
                return priority_action_index(
                    actions,
                    old_school::PriorityActionKind::Pass);
            },
        .choose_attackers =
            [](const old_school::PlayerObservation&,
               const std::vector<old_school::PermanentId>& attackers) {
                return attackers;
            },
        .choose_blockers =
            [](const old_school::PlayerObservation&,
               const std::vector<old_school::PermanentId>&,
               const std::vector<old_school::LegalBlockerChoice>&) {
                return std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>>{};
            },
        .choose_damage_order =
            [](const old_school::PlayerObservation&,
               old_school::PermanentId,
               const std::vector<old_school::PermanentId>& blockers) {
                return blockers;
            },
        .choose_cleanup_discards =
            [](const old_school::PlayerObservation&,
               std::size_t excess) {
                std::vector<std::size_t> indices(excess);
                std::iota(
                    indices.begin(), indices.end(), 0);
                return indices;
            },
    };
}

old_school::HumanController burn_human_controller(
    std::size_t player,
    std::vector<old_school::GameEvent>* events = nullptr,
    std::vector<old_school::PlayerObservation>* observations = nullptr) {
    auto controller = developing_human_controller();
    controller.choose_priority_action =
        [player](
            const old_school::PlayerObservation&,
            old_school::TurnPhase,
            const std::vector<old_school::PriorityAction>& actions) {
            const auto land = std::find_if(
                actions.begin(), actions.end(),
                [](const old_school::PriorityAction& action) {
                    return action.kind ==
                           old_school::PriorityActionKind::PlayLand;
                });
            if (land != actions.end()) {
                return static_cast<std::size_t>(
                    std::distance(actions.begin(), land));
            }
            const auto bolt = std::find_if(
                actions.begin(), actions.end(),
                [player](const old_school::PriorityAction& action) {
                    return action.kind ==
                               old_school::PriorityActionKind::
                                   CastLightningBolt &&
                           action.target.has_value() &&
                           !action.target->creature.has_value() &&
                           action.target->player == 1 - player;
                });
            if (bolt != actions.end()) {
                return static_cast<std::size_t>(
                    std::distance(actions.begin(), bolt));
            }
            return priority_action_index(
                actions, old_school::PriorityActionKind::Pass);
        };
    if (events != nullptr && observations != nullptr) {
        controller.observe =
            [events, observations](
                const old_school::PlayerObservation& observation,
                const old_school::GameEvent& event) {
                events->push_back(event);
                observations->push_back(observation);
            };
    }
    return controller;
}

TEST(interactive_observation_hides_both_libraries_and_opponent_hand) {
    old_school::GameState state;
    state.active_player = 1;
    state.starting_player = 0;
    state.turn_number = 8;
    state.players[0].hand = {
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt,
    };
    state.players[0].library = {
        old_school::CardId::Disintegrate,
        old_school::CardId::FlyingMen,
    };
    state.players[1].hand = {
        old_school::CardId::Counterspell,
        old_school::CardId::Island,
    };
    state.players[1].library = {
        old_school::CardId::AirElemental,
        old_school::CardId::Counterspell,
    };
    state.players[1].graveyard = {
        old_school::CardId::FlyingMen,
    };

    const auto observed =
        old_school::observe_game_state(state, 0);
    auto hidden_repartition = state;
    std::swap(hidden_repartition.players[0].library[0],
              hidden_repartition.players[0].library[1]);
    std::swap(hidden_repartition.players[1].hand[0],
              hidden_repartition.players[1].library[0]);
    std::reverse(hidden_repartition.players[1].library.begin(),
                 hidden_repartition.players[1].library.end());
    const auto repeated =
        old_school::observe_game_state(hidden_repartition, 0);

    CHECK(observed == repeated);
    CHECK(observed.hand == state.players[0].hand);
    CHECK(!observed.revealed_opponent_hand.has_value());
    CHECK(observed.players[0].hand_size == 2);
    CHECK(observed.players[0].library_size == 2);
    CHECK(observed.players[1].hand_size == 2);
    CHECK(observed.players[1].library_size == 2);
    CHECK(observed.players[1].graveyard ==
          state.players[1].graveyard);

    bool rejected = false;
    try {
        static_cast<void>(
            old_school::observe_game_state(state, 2));
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    CHECK(rejected);
}

TEST(interactive_matchup_selection_is_seeded_nonmirror_and_complete) {
    std::array<std::array<bool, old_school::kDeckCount>,
               old_school::kDeckCount>
        seen{};
    for (std::uint64_t seed = 0; seed < 4096; ++seed) {
        const auto matchup =
            old_school::choose_interactive_matchup(seed);
        CHECK(matchup ==
              old_school::choose_interactive_matchup(seed));
        const std::size_t human =
            static_cast<std::size_t>(matchup.human_deck);
        const std::size_t opponent =
            static_cast<std::size_t>(matchup.opponent_deck);
        CHECK(human < old_school::kDeckCount);
        CHECK(opponent < old_school::kDeckCount);
        CHECK(human != opponent);
        seen[human][opponent] = true;
    }
    for (std::size_t human = 0;
         human < old_school::kDeckCount; ++human) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount; ++opponent) {
            CHECK(seen[human][opponent] ==
                  (human != opponent));
        }
    }
}

TEST(human_debug_reveal_is_opt_in_and_controller_only) {
    const auto deck = two_card_deck(
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt);
    bool saw_revealed_hand = false;
    old_school::Game* game_under_test = nullptr;
    auto controller = developing_human_controller();
    controller.reveal_opponent_hand = true;
    controller.choose_priority_action =
        [&saw_revealed_hand, &game_under_test](
            const old_school::PlayerObservation& observation,
            old_school::TurnPhase,
            const std::vector<old_school::PriorityAction>&) {
            CHECK(game_under_test != nullptr);
            CHECK(observation.revealed_opponent_hand.has_value());
            CHECK(observation.revealed_opponent_hand->size() ==
                  observation.players[1].hand_size);
            CHECK(!observation.revealed_opponent_hand->empty());
            CHECK(*observation.revealed_opponent_hand ==
                  game_under_test->state().players[1].hand);
            saw_revealed_hand = true;
            return std::size_t{0};
        };

    old_school::GameConfig config;
    config.max_turns = 1;
    config.starting_player = 0;
    config.human_controllers[0] = std::move(controller);
    old_school::Game game(deck, deck, 0xD38B6A11ULL, config);
    game_under_test = &game;
    static_cast<void>(game.run());

    CHECK(saw_revealed_hand);
    CHECK(!old_school::observe_game_state(game.state(), 0)
               .revealed_opponent_hand.has_value());
}

TEST(scripted_human_game_is_deterministic_and_observes_public_stack) {
    const auto deck = two_card_deck(
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt);
    const auto run = [&deck] {
        std::vector<old_school::GameEvent> events;
        std::vector<old_school::PlayerObservation> observations;
        old_school::GameConfig config;
        config.max_turns = 40;
        config.starting_player = 0;
        config.human_controllers[0] =
            burn_human_controller(0, &events, &observations);
        config.human_controllers[1] =
            burn_human_controller(1);
        old_school::Game game(deck, deck, 0x1A7E2AC7ULL, config);
        return std::tuple{
            game.run(), std::move(events),
            std::move(observations)};
    };

    const auto [first_result, first_events, first_observations] =
        run();
    const auto [second_result, second_events,
                second_observations] = run();
    CHECK(first_result.winner == second_result.winner);
    CHECK(first_result.reason == second_result.reason);
    CHECK(first_result.turns == second_result.turns);
    CHECK(first_result.ending_life == second_result.ending_life);
    CHECK(first_events == second_events);
    CHECK(first_observations == second_observations);
    CHECK(!first_events.empty());
    CHECK(first_events.size() == first_observations.size());
    CHECK(std::all_of(
        first_observations.begin(), first_observations.end(),
        [](const old_school::PlayerObservation& observation) {
            return observation.observer == 0;
        }));
    CHECK(std::any_of(
        first_events.begin(), first_events.end(),
        [](const old_school::GameEvent& event) {
            return event.kind ==
                       old_school::GameEventKind::
                           PriorityActionSelected &&
                   event.player == 1 &&
                   event.priority_action.has_value() &&
                   event.priority_action->kind ==
                       old_school::PriorityActionKind::
                           CastLightningBolt;
        }));
    CHECK(std::any_of(
        first_events.begin(), first_events.end(),
        [](const old_school::GameEvent& event) {
            return event.kind ==
                       old_school::GameEventKind::
                           StackObjectResolved &&
                   event.stack_object.has_value() &&
                   event.stack_object->card ==
                       old_school::CardId::LightningBolt;
        }));
}

TEST(human_cleanup_choice_is_public_and_malformed_input_fails_closed) {
    const auto deck = two_card_deck(
        old_school::CardId::Forest,
        old_school::CardId::GrizzlyBears);
    const auto pass_priority =
        [](const old_school::PlayerObservation&,
           old_school::TurnPhase,
           const std::vector<old_school::PriorityAction>& actions) {
            return priority_action_index(
                actions,
                old_school::PriorityActionKind::Pass);
        };

    std::size_t cleanup_calls = 0;
    old_school::CardId chosen_card =
        old_school::CardId::Forest;
    std::vector<old_school::GameEvent> events;
    std::vector<old_school::PlayerObservation> observations;
    auto human = developing_human_controller();
    human.choose_priority_action = pass_priority;
    human.choose_attackers =
        [](const old_school::PlayerObservation&,
           const std::vector<old_school::PermanentId>&) {
            return std::vector<old_school::PermanentId>{};
        };
    human.choose_cleanup_discards =
        [&cleanup_calls, &chosen_card](
            const old_school::PlayerObservation& observation,
            std::size_t excess) {
            CHECK(excess == 1);
            CHECK(observation.hand.size() == 8);
            ++cleanup_calls;
            chosen_card = observation.hand[1];
            return std::vector<std::size_t>{1};
        };
    human.observe =
        [&events, &observations](
            const old_school::PlayerObservation& observation,
            const old_school::GameEvent& event) {
            events.push_back(event);
            observations.push_back(observation);
        };

    old_school::GameConfig config;
    config.max_turns = 3;
    config.starting_player = 0;
    config.human_controllers[0] = std::move(human);
    old_school::Game game(deck, deck, 0xC1EA7E57ULL, config);
    static_cast<void>(game.run());

    CHECK(cleanup_calls == 1);
    CHECK(game.state().players[0].hand.size() ==
          old_school::kMaximumHandSize);
    CHECK(game.state().players[0].graveyard ==
          std::vector<old_school::CardId>{chosen_card});
    const auto discard_event = std::find_if(
        events.begin(), events.end(),
        [](const old_school::GameEvent& event) {
            return event.kind ==
                       old_school::GameEventKind::CardsDiscarded &&
                   event.player == 0;
        });
    CHECK(discard_event != events.end());
    const std::size_t discard_index =
        static_cast<std::size_t>(
            std::distance(events.begin(), discard_event));
    CHECK(discard_event->cards ==
          std::vector<old_school::CardId>{chosen_card});
    CHECK(observations[discard_index].players[0].hand_size ==
          old_school::kMaximumHandSize);
    CHECK(observations[discard_index].players[0].graveyard ==
          std::vector<old_school::CardId>{chosen_card});

    auto malformed = developing_human_controller();
    malformed.choose_priority_action = pass_priority;
    malformed.choose_attackers =
        [](const old_school::PlayerObservation&,
           const std::vector<old_school::PermanentId>&) {
            return std::vector<old_school::PermanentId>{};
        };
    malformed.choose_cleanup_discards =
        [](const old_school::PlayerObservation& observation,
           std::size_t) {
            return std::vector<std::size_t>{
                observation.hand.size()};
        };
    old_school::GameConfig malformed_config;
    malformed_config.max_turns = 3;
    malformed_config.starting_player = 0;
    malformed_config.human_controllers[0] =
        std::move(malformed);
    old_school::Game malformed_game(
        deck, deck, 0xC1EA7E57ULL, malformed_config);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(malformed_game.run());
        },
        "cleanup"));
    CHECK(malformed_game.state().players[0].hand.size() == 8);
    CHECK(malformed_game.state().players[0].graveyard.empty());
}

TEST(terminal_interactive_prompts_for_cleanup_discard) {
    std::ostringstream commands;
    for (std::size_t decision = 0; decision < 48;
         ++decision) {
        commands << "0\n";
    }
    commands << "q\n";
    std::istringstream input(commands.str());
    std::ostringstream output;
    const auto result = old_school::run_interactive_match(
        input, output, 0xC1E47E42ULL,
        {
            .human_deck = old_school::DeckId::Blue,
            .opponent_deck = old_school::DeckId::Green,
        });
    const std::string transcript = output.str();
    CHECK(result.abandoned || result.game.has_value());
    CHECK(transcript.find("CLEANUP | DISCARD 1 MORE") !=
          std::string::npos);
    CHECK(transcript.find("[DISCARD] You discard:") !=
          std::string::npos);
}

TEST(all_bot_kinds_complete_cleanup_deterministically) {
    for (const auto kind : std::array{
             old_school::BotKind::Random,
             old_school::BotKind::MonteCarlo,
             old_school::BotKind::DeepMonteCarlo,
             old_school::BotKind::Handcrafted,
         }) {
        old_school::GameConfig config;
        config.max_turns = 4;
        config.starting_player = 0;
        for (auto& bot : config.bots) {
            bot = {
                .kind = kind,
                .rollouts_per_action = 1,
            };
        }
        const auto run = [&config] {
            old_school::Game game(
                old_school::blue_deck(),
                old_school::blue_deck(),
                0xB07C1EA4ULL, config);
            const auto result = game.run();
            return std::pair{
                result, game.state()};
        };
        const auto first = run();
        const auto repeated = run();
        CHECK(first == repeated);
        for (const auto& player : first.second.players) {
            CHECK(player.hand.size() <=
                  old_school::kMaximumHandSize);
        }
    }
}

TEST(human_priority_attack_block_and_damage_choices_are_validated) {
    const auto lands =
        std::vector<old_school::CardId>(
            40, old_school::CardId::Forest);
    const auto creatures = two_card_deck(
        old_school::CardId::Forest,
        old_school::CardId::GrizzlyBears);

    {
        auto invalid = developing_human_controller();
        invalid.choose_priority_action =
            [](const old_school::PlayerObservation&,
               old_school::TurnPhase,
               const std::vector<old_school::PriorityAction>& actions) {
                return actions.size();
            };
        old_school::GameConfig config;
        config.starting_player = 0;
        config.human_controllers[0] = std::move(invalid);
        bool rejected = false;
        try {
            old_school::Game game(lands, lands, 1, config);
            static_cast<void>(game.run());
        } catch (const std::invalid_argument& error) {
            CHECK(std::string_view(error.what()).find(
                      "priority action") != std::string_view::npos);
            rejected = true;
        }
        CHECK(rejected);
    }

    {
        auto invalid = developing_human_controller();
        invalid.choose_attackers =
            [](const old_school::PlayerObservation&,
               const std::vector<old_school::PermanentId>& attackers) {
                if (attackers.empty()) {
                    return attackers;
                }
                return std::vector<old_school::PermanentId>{
                    attackers.front(), attackers.front()};
            };
        old_school::GameConfig config;
        config.max_turns = 40;
        config.starting_player = 0;
        config.human_controllers[0] = std::move(invalid);
        bool rejected = false;
        try {
            old_school::Game game(creatures, lands, 2, config);
            static_cast<void>(game.run());
        } catch (const std::invalid_argument& error) {
            CHECK(std::string_view(error.what()).find(
                      "attacker set") != std::string_view::npos);
            rejected = true;
        }
        CHECK(rejected);
    }

    {
        auto attacker = developing_human_controller();
        auto defender = developing_human_controller();
        defender.choose_attackers =
            [](const old_school::PlayerObservation&,
               const std::vector<old_school::PermanentId>&) {
                return std::vector<old_school::PermanentId>{};
            };
        defender.choose_blockers =
            [](const old_school::PlayerObservation&,
               const std::vector<old_school::PermanentId>&,
               const std::vector<old_school::LegalBlockerChoice>& choices) {
                if (choices.empty() ||
                    choices.front().legal_attackers.empty()) {
                    return std::vector<std::pair<
                        old_school::PermanentId,
                        old_school::PermanentId>>{};
                }
                return std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>>{
                    {999'999, choices.front().blocker}};
            };
        old_school::GameConfig config;
        config.max_turns = 40;
        config.starting_player = 0;
        config.human_controllers[0] = std::move(attacker);
        config.human_controllers[1] = std::move(defender);
        bool rejected = false;
        try {
            old_school::Game game(
                creatures, creatures, 3, config);
            static_cast<void>(game.run());
        } catch (const std::invalid_argument& error) {
            CHECK(std::string_view(error.what()).find(
                      "blocker assignment") !=
                  std::string_view::npos);
            rejected = true;
        }
        CHECK(rejected);
    }

    {
        auto attacker = developing_human_controller();
        attacker.choose_attackers =
            [](const old_school::PlayerObservation& observation,
               const std::vector<old_school::PermanentId>& attackers) {
                if (attackers.empty() ||
                    observation.players[1].creatures.size() < 2) {
                    return std::vector<
                        old_school::PermanentId>{};
                }
                return std::vector<old_school::PermanentId>{
                    attackers.front()};
            };
        attacker.choose_damage_order =
            [](const old_school::PlayerObservation&,
               old_school::PermanentId,
               const std::vector<old_school::PermanentId>& blockers) {
                return std::vector<old_school::PermanentId>(
                    blockers.size(), blockers.front());
            };
        auto defender = developing_human_controller();
        defender.choose_attackers =
            [](const old_school::PlayerObservation&,
               const std::vector<old_school::PermanentId>&) {
                return std::vector<old_school::PermanentId>{};
            };
        defender.choose_blockers =
            [](const old_school::PlayerObservation&,
               const std::vector<old_school::PermanentId>& attackers,
               const std::vector<old_school::LegalBlockerChoice>& choices) {
                std::vector<std::pair<
                    old_school::PermanentId,
                    old_school::PermanentId>> blocks;
                if (attackers.empty()) {
                    return blocks;
                }
                for (const auto& choice : choices) {
                    if (std::find(
                            choice.legal_attackers.begin(),
                            choice.legal_attackers.end(),
                            attackers.front()) !=
                        choice.legal_attackers.end()) {
                        blocks.emplace_back(
                            attackers.front(), choice.blocker);
                    }
                }
                return blocks;
            };
        old_school::GameConfig config;
        config.max_turns = 40;
        config.starting_player = 0;
        config.human_controllers[0] = std::move(attacker);
        config.human_controllers[1] = std::move(defender);
        bool rejected = false;
        try {
            old_school::Game game(
                creatures, creatures, 4, config);
            static_cast<void>(game.run());
        } catch (const std::invalid_argument& error) {
            CHECK(std::string_view(error.what()).find(
                      "damage order") != std::string_view::npos);
            rejected = true;
        }
        CHECK(rejected);
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
