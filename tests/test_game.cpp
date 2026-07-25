#include "old_school/game.hpp"
#include "old_school/interactive.hpp"
#include "old_school/learned_iteration.hpp"

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
        creature(51, old_school::CardId::WaterElemental),
    };

    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 70,
            .card = old_school::CardId::WaterElemental,
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
    remove_fixture_card(opponent_hidden, old_school::CardId::WaterElemental);
    // A spell stack object is another physical Water Elemental.
    remove_fixture_card(opponent_hidden, old_school::CardId::WaterElemental);
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

std::shared_ptr<const old_school::LearnedModel> small_actor_model() {
    static const auto model =
        old_school::train_learned_actor_model(1, 0xAC70E7A1ULL);
    return model;
}

std::shared_ptr<const old_school::LearnedModel> small_value_model() {
    static const auto model =
        old_school::train_learned_value_champion(1, 0xC4A6E7A1ULL);
    return model;
}

const old_school::LearnedValueChallengerArtifact&
small_value_challenger_c2_artifact() {
    static const auto artifact =
        old_school::train_learned_value_challenger_artifact(
            1, 424242, 2);
    return artifact;
}

std::shared_ptr<const old_school::LearnedModel>
small_value_challenger_c2() {
    return small_value_challenger_c2_artifact().model();
}

const old_school::LearnedValueG8Result& small_value_g8() {
    static const auto result =
        old_school::train_learned_value_g8(1, 0x68A11EADULL);
    return result;
}

const old_school::LearnedValueG8Result& small_value_g8_eight_games() {
    static const auto result =
        old_school::train_learned_value_g8(8, 0x68A15050ULL);
    return result;
}

const old_school::LearnedValueG8Result& small_value_g8_mix50() {
    static const auto result =
        old_school::train_learned_value_g8_mix50(
            8, 0x68A15050ULL);
    return result;
}

std::vector<std::uint8_t> read_binary_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    CHECK(input);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void write_binary_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(
        path, std::ios::binary | std::ios::trunc);
    CHECK(output);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    CHECK(output);
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
    CHECK(count_card(red_deck, old_school::CardId::Mountain) == 18);
    CHECK(count_card(red_deck, old_school::CardId::LightningBolt) == 10);
    CHECK(count_card(red_deck, old_school::CardId::FireElemental) == 12);

    const auto blue_deck = old_school::blue_deck();
    CHECK(blue_deck.size() == 40);
    CHECK(count_card(blue_deck, old_school::CardId::Island) == 18);
    CHECK(count_card(blue_deck, old_school::CardId::Counterspell) == 14);
    CHECK(count_card(blue_deck, old_school::CardId::WaterElemental) == 8);

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
                     old_school::CardId::WaterElemental) ==
          count_card(fixture.decks[1],
                     old_school::CardId::WaterElemental));
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

TEST(learned_observation_excludes_opponent_hidden_card_identities) {
    const auto fixture = determinization_fixture();
    const auto baseline =
        old_school::learned_observation(fixture.state, 0);

    auto changed_hidden = fixture.state;
    std::fill(changed_hidden.players[1].hand.begin(),
              changed_hidden.players[1].hand.end(),
              old_school::CardId::LightningBolt);
    std::fill(changed_hidden.players[1].library.begin(),
              changed_hidden.players[1].library.end(),
              old_school::CardId::Forest);
    CHECK(old_school::learned_observation(changed_hidden, 0) == baseline);

    auto changed_own_hand = fixture.state;
    changed_own_hand.players[0].hand[0] =
        old_school::CardId::LightningBolt;
    CHECK(old_school::learned_observation(changed_own_hand, 0) != baseline);

    auto changed_public_zone = fixture.state;
    changed_public_zone.players[1].graveyard[0] =
        old_school::CardId::LightningBolt;
    CHECK(old_school::learned_observation(changed_public_zone, 0) !=
          baseline);
}

TEST(learned_features_encode_old_school_public_state_and_action_x) {
    old_school::GameState state;
    state.active_player = 0;
    state.turn_number = 6;
    state.players[0].hand = {
        old_school::CardId::Disintegrate,
        old_school::CardId::GiantGrowth,
    };
    state.players[0].library = {
        old_school::CardId::Mountain,
        old_school::CardId::Island,
    };
    state.players[0].creatures = {
        creature(10, old_school::CardId::GrizzlyBears),
    };
    state.players[1].hand = {
        old_school::CardId::Counterspell,
        old_school::CardId::LightningBolt,
    };
    state.players[1].library = {
        old_school::CardId::Island,
        old_school::CardId::Mountain,
    };
    state.players[1].creatures = {
        creature(20, old_school::CardId::HillGiant),
    };

    const auto baseline_observation =
        old_school::learned_observation(state, 0);
    const auto baseline_policy =
        old_school::learned_priority_policy_features(
            state, 0, old_school::PriorityAction::pass(), true,
            old_school::TurnPhase::FirstMain, 0);

    auto exile_changed = state;
    exile_changed.players[1].exile.push_back(
        old_school::CardId::FlyingMen);
    CHECK(old_school::learned_observation(exile_changed, 0) !=
          baseline_observation);
    CHECK(old_school::learned_priority_policy_features(
              exile_changed, 0, old_school::PriorityAction::pass(), true,
              old_school::TurnPhase::FirstMain, 0) != baseline_policy);

    auto bonus_changed = state;
    bonus_changed.players[0].creatures[0].temporary_power_bonus = 3;
    bonus_changed.players[0].creatures[0].temporary_toughness_bonus = 3;
    CHECK(old_school::learned_observation(bonus_changed, 0) !=
          baseline_observation);
    CHECK(old_school::learned_priority_policy_features(
              bonus_changed, 0, old_school::PriorityAction::pass(), true,
              old_school::TurnPhase::FirstMain, 0) != baseline_policy);

    auto stack_x_one = state;
    stack_x_one.stack.push_back({
        .kind = old_school::StackObjectKind::Spell,
        .id = 30,
        .card = old_school::CardId::Disintegrate,
        .controller = 1,
        .target = old_school::Target::player_target(0),
        .spell_target = std::nullopt,
        .x_value = 1,
    });
    auto stack_x_four = stack_x_one;
    stack_x_four.stack[0].x_value = 4;
    CHECK(old_school::learned_observation(stack_x_one, 0) !=
          old_school::learned_observation(stack_x_four, 0));
    CHECK(old_school::learned_priority_policy_features(
              stack_x_one, 0, old_school::PriorityAction::pass(), false,
              old_school::TurnPhase::BeginCombat, 0) !=
          old_school::learned_priority_policy_features(
              stack_x_four, 0, old_school::PriorityAction::pass(), false,
              old_school::TurnPhase::BeginCombat, 0));

    const auto disintegrate_one =
        old_school::PriorityAction::cast_disintegrate(
            1, old_school::Target::player_target(1));
    const auto disintegrate_four =
        old_school::PriorityAction::cast_disintegrate(
            4, old_school::Target::player_target(1));
    CHECK(old_school::learned_priority_policy_features(
              state, 0, disintegrate_one, true,
              old_school::TurnPhase::FirstMain, 0) !=
          old_school::learned_priority_policy_features(
              state, 0, disintegrate_four, true,
              old_school::TurnPhase::FirstMain, 0));

    auto hidden_changed = state;
    hidden_changed.players[1].hand = {
        old_school::CardId::Forest,
        old_school::CardId::Moat,
    };
    hidden_changed.players[1].library = {
        old_school::CardId::GiantGrowth,
        old_school::CardId::Tsunami,
    };
    CHECK(old_school::learned_observation(hidden_changed, 0) ==
          baseline_observation);
    CHECK(old_school::learned_priority_policy_features(
              hidden_changed, 0, disintegrate_four, true,
              old_school::TurnPhase::FirstMain, 0) ==
          old_school::learned_priority_policy_features(
              state, 0, disintegrate_four, true,
              old_school::TurnPhase::FirstMain, 0));
}

TEST(learned_priority_policy_encodes_phase_and_pass_context) {
    const auto fixture = determinization_fixture();
    const auto pass = old_school::PriorityAction::pass();
    const auto beginning_of_combat =
        old_school::learned_priority_policy_features(
            fixture.state, 0, pass, false,
            old_school::TurnPhase::BeginCombat, 0);
    const auto first_main =
        old_school::learned_priority_policy_features(
            fixture.state, 0, pass, true,
            old_school::TurnPhase::FirstMain, 0);
    const auto second_main =
        old_school::learned_priority_policy_features(
            fixture.state, 0, pass, true,
            old_school::TurnPhase::SecondMain, 0);
    const auto resolving_pass =
        old_school::learned_priority_policy_features(
            fixture.state, 0, pass, false,
            old_school::TurnPhase::BeginCombat, 1);
    CHECK(beginning_of_combat != first_main);
    CHECK(first_main != second_main);
    CHECK(beginning_of_combat != resolving_pass);

    auto changed_hidden = fixture.state;
    std::fill(changed_hidden.players[1].hand.begin(),
              changed_hidden.players[1].hand.end(),
              old_school::CardId::LightningBolt);
    std::fill(changed_hidden.players[1].library.begin(),
              changed_hidden.players[1].library.end(),
              old_school::CardId::Forest);
    CHECK(old_school::learned_priority_policy_features(
              changed_hidden, 0, pass, false,
              old_school::TurnPhase::BeginCombat, 1) ==
          resolving_pass);
}

TEST(learned_soft_priority_target_is_smoothed_and_ordered) {
    const auto targets =
        old_school::learned_soft_priority_target({0.2, 0.4, 0.4});
    CHECK(targets.size() == 3);
    CHECK(std::abs(targets[0] + targets[1] + targets[2] - 1.0) <
          1.0e-12);
    CHECK(targets[0] >= 0.1 / 3.0);
    CHECK(targets[1] > targets[0]);
    CHECK(std::abs(targets[1] - targets[2]) < 1.0e-12);

    const auto uniform =
        old_school::learned_soft_priority_target({0.5, 0.5, 0.5});
    CHECK(uniform.size() == 3);
    for (const double target : uniform) {
        CHECK(std::abs(target - 1.0 / 3.0) < 1.0e-12);
    }
    CHECK(old_school::learned_soft_priority_target({}).empty());
}

TEST(learned_defaults_to_value_search_champion) {
    const old_school::BotConfig learned = {
        .kind = old_school::BotKind::Learned,
    };
    CHECK(learned.learned_variant ==
          old_school::LearnedVariant::ValueSearchChampion);
    CHECK(learned.value_continuation_epsilon == 0.0);
    CHECK(old_school::bot_config_name(learned) == "Learned Value");

    const old_school::BotConfig actor = {
        .kind = old_school::BotKind::Learned,
        .learned_variant = old_school::LearnedVariant::UnifiedActor,
    };
    CHECK(old_school::bot_config_name(actor) == "Learned Actor");
}

TEST(value_continuation_epsilon_rejects_invalid_or_non_value_use) {
    const auto rejects_game_config =
        [](old_school::BotConfig bot) {
            old_school::GameConfig config;
            config.bots[0] = std::move(bot);
            try {
                old_school::Game game(
                    old_school::green_deck(),
                    old_school::red_deck(), 1, config);
                static_cast<void>(game);
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };

    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .value_continuation_epsilon =
            std::numeric_limits<double>::quiet_NaN(),
        .learned_model = small_value_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .value_continuation_epsilon = -0.01,
        .learned_model = small_value_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .value_continuation_epsilon = 1.01,
        .learned_model = small_value_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::UnifiedActor,
        .rollouts_per_action = 0,
        .value_continuation_epsilon = 0.1,
        .learned_model = small_actor_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Random,
        .rollouts_per_action = 1,
        .value_continuation_epsilon = 0.1,
    }));

    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    old_school::LearnedSearchConfig search{
        .seed = 1,
        .worlds = 2,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .value_continuation_epsilon = 0.1,
    };
    bool rejected_actor_search = false;
    try {
        static_cast<void>(
            old_school::learned_priority_action_samples(
                state, decks, 0, true,
                old_school::TurnPhase::FirstMain, 1, actions,
                small_actor_model(), search));
    } catch (const std::invalid_argument&) {
        rejected_actor_search = true;
    }
    CHECK(rejected_actor_search);
}

TEST(learned_value_search_is_hidden_invariant_phase_aware_and_bounded) {
    auto fixture = determinization_fixture();
    fixture.state.active_player = 0;
    fixture.state.starting_player = 0;
    fixture.state.players[0].artifacts[0].tapped = false;
    fixture.state.players[0].lands[0].tapped = false;
    fixture.state.players[0].lands.push_back(
        {.card = old_school::CardId::Plains, .tapped = false});
    remove_fixture_card(
        fixture.state.players[0].library,
        old_school::CardId::Plains);
    // Make phase continuation observably different: the First Main path has
    // a guaranteed lethal combat, while Second Main has already passed it.
    remove_fixture_card(
        fixture.decks[0], old_school::CardId::Moat);
    fixture.decks[0].push_back(old_school::CardId::GrizzlyBears);
    remove_fixture_card(
        fixture.state.players[0].library,
        old_school::CardId::Moat);
    fixture.state.players[0].creatures.push_back(bear(91));
    fixture.state.players[1].creatures[0].tapped = true;
    fixture.state.players[1].life = 2;
    const auto model =
        old_school::train_learned_value_champion(1, 0xC4A6A10ULL);

    constexpr std::size_t kRollouts = 2;
    constexpr std::uint64_t kEvaluationSeed = 0x1F05AFEULL;
    const auto first_main =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, kRollouts,
            kEvaluationSeed);
    const auto explicit_zero =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, kRollouts,
            kEvaluationSeed, 0.0);
    CHECK(explicit_zero.actions == first_main.actions);
    CHECK(explicit_zero.scores == first_main.scores);

    auto hidden_variant = fixture.state;
    std::reverse(hidden_variant.players[1].hand.begin(),
                 hidden_variant.players[1].hand.end());
    std::reverse(hidden_variant.players[1].library.begin(),
                 hidden_variant.players[1].library.end());
    if (!hidden_variant.players[1].hand.empty() &&
        !hidden_variant.players[1].library.empty()) {
        std::swap(hidden_variant.players[1].hand.front(),
                  hidden_variant.players[1].library.front());
    }
    const auto hidden_repeated =
        old_school::diagnose_learned_value_priority(
            hidden_variant, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, kRollouts,
            kEvaluationSeed);

    CHECK(first_main.actions == hidden_repeated.actions);
    CHECK(first_main.scores == hidden_repeated.scores);
    CHECK(first_main.sampled_worlds == kRollouts);
    CHECK(first_main.rollout_evaluations ==
          first_main.actions.size() * kRollouts);
    CHECK(first_main.actions.size() > 1);
    CHECK(std::all_of(
        first_main.scores.begin(), first_main.scores.end(),
        [](double score) { return std::isfinite(score); }));

    const auto epsilon_one =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, kRollouts,
            kEvaluationSeed, 1.0);
    const auto epsilon_one_repeated =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, kRollouts,
            kEvaluationSeed, 1.0);
    const auto epsilon_one_hidden =
        old_school::diagnose_learned_value_priority(
            hidden_variant, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, kRollouts,
            kEvaluationSeed, 1.0);
    CHECK(epsilon_one.actions == first_main.actions);
    CHECK(epsilon_one.actions == epsilon_one_repeated.actions);
    CHECK(epsilon_one.scores == epsilon_one_repeated.scores);
    CHECK(epsilon_one.scores == epsilon_one_hidden.scores);
    CHECK(epsilon_one.sampled_worlds == kRollouts);
    CHECK(epsilon_one.rollout_evaluations ==
          epsilon_one.actions.size() * kRollouts);
    // Root candidates and accounting are unchanged. Only the depth-zero
    // Value-mirror continuation seats explore.
    CHECK(epsilon_one.scores != first_main.scores);

    const auto second_main =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, model, kRollouts,
            kEvaluationSeed);
    CHECK(second_main.actions == first_main.actions);
    CHECK(second_main.scores != first_main.scores);
}

TEST(generic_priority_samples_use_common_worlds_and_hide_repartition) {
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() == 4);
    const old_school::LearnedSearchConfig config = {
        .seed = 0xC0110A5EULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto model = small_actor_model();
    const auto baseline =
        old_school::learned_priority_action_samples(
            state, decks, 0, true, old_school::TurnPhase::FirstMain,
            0, actions, model, config);
    CHECK(baseline.sampled_worlds == 2);
    CHECK(baseline.rollout_evaluations == actions.size() * 4);
    CHECK(baseline.q_samples.size() == actions.size());
    for (const auto& samples : baseline.q_samples) {
        CHECK(samples.size() == 4);
        CHECK(std::all_of(
            samples.begin(), samples.end(),
            [](double value) {
                return std::isfinite(value) &&
                       value >= 0.0 && value <= 1.0;
            }));
    }

    const old_school::GameState hidden =
        hidden_repartition(state, 0);
    const auto repeated =
        old_school::learned_priority_action_samples(
            hidden, decks, 0, true, old_school::TurnPhase::FirstMain,
            0, actions, model, config);
    CHECK(repeated.q_samples == baseline.q_samples);
    const auto logits =
        old_school::learned_actor_priority_logits(
            state, 0, true, old_school::TurnPhase::FirstMain, 0,
            actions, model);
    CHECK(logits.size() == actions.size());
    CHECK(std::all_of(
        logits.begin(), logits.end(),
        [](double value) { return std::isfinite(value); }));
    CHECK(old_school::learned_actor_priority_logits(
              hidden, 0, true, old_school::TurnPhase::FirstMain, 0,
              actions, model) == logits);
    const double critic =
        old_school::learned_critic_value(state, 0, model);
    CHECK(critic > 0.0 && critic < 1.0);
    CHECK(old_school::learned_critic_value(hidden, 0, model) == critic);

    auto reordered_actions = actions;
    std::reverse(reordered_actions.begin(), reordered_actions.end());
    const auto reordered =
        old_school::learned_priority_action_samples(
            state, decks, 0, true, old_school::TurnPhase::FirstMain,
            0, reordered_actions, model, config);
    for (std::size_t index = 0; index < actions.size(); ++index) {
        CHECK(reordered.q_samples[index] ==
              baseline.q_samples[actions.size() - index - 1]);
    }

    bool rejected_illegal = false;
    try {
        static_cast<void>(
            old_school::learned_priority_action_samples(
                state, decks, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                {old_school::PriorityAction::cast_sorcery(
                    old_school::CardId::Tsunami)},
                model, config));
    } catch (const std::invalid_argument&) {
        rejected_illegal = true;
    }
    CHECK(rejected_illegal);
}

TEST(learned_model_fingerprint_binds_exact_frozen_weights) {
    const auto actor = small_actor_model();
    const auto repeated =
        old_school::train_learned_actor_model(1, 0xAC70E7A1ULL);
    const auto changed =
        old_school::train_learned_actor_model(1, 0xAC70E7A2ULL);
    const std::string fingerprint =
        old_school::learned_model_fingerprint(actor);
    CHECK(fingerprint.size() == 64);
    CHECK(std::all_of(
        fingerprint.begin(), fingerprint.end(),
        [](char character) {
            return (character >= '0' && character <= '9') ||
                   (character >= 'a' && character <= 'f');
        }));
    CHECK(old_school::learned_model_fingerprint(repeated) == fingerprint);
    CHECK(old_school::learned_model_fingerprint(changed) != fingerprint);
    CHECK(old_school::learned_model_fingerprint(small_value_model()) !=
          fingerprint);

    bool rejected_null = false;
    try {
        static_cast<void>(
            old_school::learned_model_fingerprint(nullptr));
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    CHECK(rejected_null);
}

TEST(value_trainer_is_seeded_deterministic_in_the_old_school_schema) {
    const auto model =
        old_school::train_learned_value_champion(1, 424242);
    const auto repeated =
        old_school::train_learned_value_champion(1, 424242);
    const auto changed =
        old_school::train_learned_value_champion(1, 424243);
    const auto fingerprint =
        old_school::learned_model_fingerprint(model);
    CHECK(fingerprint ==
          "b2eec9390d1c7edc358aa27220f9f25b1c31022627a4701e9590efa669e982ba");
    CHECK(old_school::learned_model_fingerprint(repeated) ==
          fingerprint);
    CHECK(old_school::learned_model_fingerprint(changed) != fingerprint);

    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    CHECK(old_school::learned_critic_value(state, 0, repeated) ==
          old_school::learned_critic_value(state, 0, model));
    CHECK(old_school::learned_critic_value(state, 1, repeated) ==
          old_school::learned_critic_value(state, 1, model));
}

TEST(value_challenger_is_explicit_deterministic_and_generation_bound) {
    const auto generation_one =
        old_school::train_learned_value_challenger(
            1, 424242, 1);
    const auto repeated =
        old_school::train_learned_value_challenger(
            1, 424242, 1);
    const auto changed_seed =
        old_school::train_learned_value_challenger(
            1, 424243, 1);
    const auto generation_two = small_value_challenger_c2();

    const std::string generation_one_fingerprint =
        old_school::learned_model_fingerprint(generation_one);
    CHECK(old_school::learned_model_fingerprint(repeated) ==
          generation_one_fingerprint);
    CHECK(old_school::learned_model_fingerprint(changed_seed) !=
          generation_one_fingerprint);
    CHECK(old_school::learned_model_fingerprint(generation_two) !=
          generation_one_fingerprint);
    CHECK(old_school::learned_model_fingerprint(generation_two) ==
          "88528336069e681b4c4a54264a6fbdd4cd5d8613d6e6d2b8e46c578689adf817");

    bool rejected_zero_generations = false;
    try {
        static_cast<void>(
            old_school::train_learned_value_challenger(
                1, 424242, 0));
    } catch (const std::invalid_argument&) {
        rejected_zero_generations = true;
    }
    CHECK(rejected_zero_generations);
}

TEST(value_challenger_artifact_is_versioned_bit_exact_and_fail_closed) {
    const auto& original_artifact =
        small_value_challenger_c2_artifact();
    const auto original = original_artifact.model();
    const std::filesystem::path directory =
        "build/test-model-cache";
    const std::filesystem::path good =
        directory / "value-challenger-c2-good.bin";
    const std::filesystem::path corrupt =
        directory / "value-challenger-c2-corrupt.bin";
    const std::filesystem::path canonical_path =
        directory / "value-g8-canonical-cross-family.bin";
    const std::filesystem::path mix50_path =
        directory / "value-g8-mix50-cross-family.bin";
    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(canonical_path);
    std::filesystem::remove(mix50_path);

    old_school::write_learned_value_challenger_artifact_atomic(
        good.string(), original_artifact);
    const auto loaded_artifact =
        old_school::load_learned_value_challenger_artifact(
            good.string(), 1, 424242, 2);
    const auto loaded = loaded_artifact.model();
    const std::string fingerprint =
        old_school::learned_model_fingerprint(original);
    CHECK(fingerprint ==
          "88528336069e681b4c4a54264a6fbdd4cd5d8613d6e6d2b8e46c578689adf817");
    CHECK(old_school::learned_model_fingerprint(loaded) ==
          fingerprint);
    CHECK(loaded_artifact.training_games() == 1);
    CHECK(loaded_artifact.seed() == 424242);
    CHECK(loaded_artifact.self_play_generations() == 2);
    CHECK(old_school::learned_value_challenger_cache_path(
              800, 424242, 16) ==
          "build/model-cache/"
          "old-school-value-challenger-v1-c16-t800-s424242.bin");

    const std::array<old_school::GameState, 2> states = {
        old_school::white_lock_plan_diagnostic_state(),
        determinization_fixture().state,
    };
    for (const auto& state : states) {
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            const double before =
                old_school::learned_critic_value(
                    state, perspective, original);
            const double after =
                old_school::learned_critic_value(
                    state, perspective, loaded);
            CHECK(std::bit_cast<std::uint64_t>(after) ==
                  std::bit_cast<std::uint64_t>(before));
        }
    }

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_challenger_artifact(
                    good.string(), 2, 424242, 2));
        },
        "training_games mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_challenger_artifact(
                    good.string(), 1, 424243, 2));
        },
        "training seed mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_challenger_artifact(
                    good.string(), 1, 424242, 1));
        },
        "generation mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    good.string(), 1, 424242));
        },
        "wrong magic"));

    old_school::write_learned_value_g8_bundle_atomic(
        canonical_path.string(), small_value_g8());
    old_school::write_learned_value_g8_mix50_bundle_atomic(
        mix50_path.string(), small_value_g8_mix50());
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_challenger_artifact(
                    canonical_path.string(), 1,
                    0x68A11EADULL, 2));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_challenger_artifact(
                    mix50_path.string(), 8,
                    0x68A15050ULL, 2));
        },
        "wrong magic"));

    auto changed = read_binary_file(good);
    CHECK(changed.size() > 64);
    changed.back() ^= 0x01U;
    write_binary_file(corrupt, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_challenger_artifact(
                    corrupt.string(), 1, 424242, 2));
        },
        "checksum"));
    CHECK(old_school::learned_model_fingerprint(
              old_school::load_learned_value_challenger_artifact(
                  good.string(), 1, 424242, 2)
                  .model()) ==
          fingerprint);

    CHECK(throws_with_text(
        [] {
            static_cast<void>(
                old_school::learned_value_challenger_cache_path(
                    1, 424242, 0));
        },
        "generations"));
    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(canonical_path);
    std::filesystem::remove(mix50_path);
}

TEST(learned_value_update_deep_clones_without_mutating_parent) {
    const auto parent = small_value_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const double parent_value =
        old_school::learned_critic_value(state, 0, parent);

    const auto frozen_clone =
        old_school::update_learned_value_model(
            parent, {}, {});
    CHECK(frozen_clone.get() != parent.get());
    CHECK(old_school::learned_model_fingerprint(frozen_clone) ==
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(
              state, 0, frozen_clone) == parent_value);

    const auto candidate =
        old_school::update_learned_value_model(
            frozen_clone,
            {{
                .features =
                    old_school::learned_observation(state, 0),
                .target = parent_value < 0.5 ? 1.0 : 0.0,
            }},
            {
                .epochs = 4,
                .learning_rate = 0.05,
                .root_seed = 0xC1171CULL,
                .member_training_tag = 0x5E1F0000ULL,
            });
    CHECK(old_school::learned_model_fingerprint(candidate) !=
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(
              state, 0, candidate) != parent_value);
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(old_school::learned_model_fingerprint(frozen_clone) ==
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(state, 0, parent) ==
          parent_value);

    const auto rejects_invalid =
        [](const std::function<void()>& operation) {
            try {
                operation();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };
    CHECK(rejects_invalid([] {
        static_cast<void>(
            old_school::update_learned_value_model(
                nullptr, {}, {}));
    }));
    CHECK(rejects_invalid([] {
        static_cast<void>(
            old_school::update_learned_value_model(
                small_actor_model(), {}, {}));
    }));

    auto malformed_features =
        old_school::learned_observation(state, 0);
    malformed_features.pop_back();
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_model(
                parent,
                {{
                    .features = malformed_features,
                    .target = 0.5,
                }},
                {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_model(
                parent,
                {{
                    .features =
                        old_school::learned_observation(state, 0),
                    .target = 1.01,
                }},
                {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_model(
                parent,
                {{
                    .features =
                        old_school::learned_observation(state, 0),
                    .target = 0.5,
                }},
                {
                    .epochs = 0,
                    .learning_rate = 0.006,
                }));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_model(
                parent,
                {{
                    .features =
                        old_school::learned_observation(state, 0),
                    .target = 0.5,
                }},
                {
                    .epochs = 3,
                    .learning_rate =
                        std::numeric_limits<double>::
                            quiet_NaN(),
                }));
    }));
}

TEST(learned_value_g8_has_immutable_replay_and_search_checkpoints) {
    const auto& result = small_value_g8();
    CHECK(result.model);
    CHECK(result.checkpoints.size() ==
          old_school::kLearnedValueG8Generations + 1);
    CHECK(result.report.generations.size() ==
          old_school::kLearnedValueG8Generations);
    CHECK(result.model == result.checkpoints.back());
    CHECK(result.report.recipe ==
          old_school::LearnedValueG8Recipe::
              CanonicalAllSearchLate);
    CHECK(result.report.training_games == 1);
    CHECK(result.report.root_seed == 0x68A11EADULL);
    CHECK(result.report.base_examples > 0);
    CHECK(result.report.base_fingerprint ==
          old_school::learned_model_fingerprint(
              result.checkpoints.front()));
    CHECK(result.report.final_fingerprint ==
          old_school::learned_model_fingerprint(result.model));

    constexpr std::array<std::size_t, 8>
        expected_replay_occupancy = {
            1, 2, 3, 3, 3, 3, 3, 3,
        };
    std::vector<std::string> checkpoint_fingerprints;
    checkpoint_fingerprints.reserve(
        result.checkpoints.size());
    for (const auto& checkpoint : result.checkpoints) {
        checkpoint_fingerprints.push_back(
            old_school::learned_model_fingerprint(checkpoint));
    }
    for (std::size_t index = 0;
         index < result.report.generations.size();
         ++index) {
        const auto& generation =
            result.report.generations[index];
        CHECK(generation.generation == index + 1);
        CHECK(generation.self_play_games == 1);
        CHECK(generation.generation_examples > 0);
        CHECK(generation.anchor_examples ==
              result.report.base_examples);
        CHECK(generation.replay_generations ==
              expected_replay_occupancy[index]);
        CHECK(generation.replay_examples >=
              generation.generation_examples);
        CHECK(generation.raw_collection_games == 0);
        CHECK(generation.search_collection_games == 0);
        CHECK(generation.raw_collection_examples == 0);
        CHECK(generation.search_collection_examples == 0);
        CHECK(generation.parent_fingerprint ==
              checkpoint_fingerprints[index]);
        CHECK(generation.candidate_fingerprint ==
              checkpoint_fingerprints[index + 1]);
        CHECK(generation.parent_fingerprint !=
              generation.candidate_fingerprint);
        CHECK(generation.exploration_rate ==
              (index < 2 ? 0.10 : 0.05));
        if (index < 4) {
            CHECK(!generation.search_enabled);
            CHECK(generation.search_worlds == 0);
            CHECK(generation.search_horizon_turns == 0);
            CHECK(generation.rollout_evaluations == 0);
        } else {
            CHECK(generation.search_enabled);
            CHECK(generation.search_worlds == 1);
            CHECK(generation.search_horizon_turns == 4);
            CHECK(generation.rollout_evaluations > 0);
        }
    }

    // Every old checkpoint must still serialize to the fingerprint recorded
    // when it was the parent or candidate of a later update.
    for (std::size_t index = 0;
         index < result.checkpoints.size(); ++index) {
        CHECK(old_school::learned_model_fingerprint(
                  result.checkpoints[index]) ==
              checkpoint_fingerprints[index]);
    }
}

TEST(learned_value_g8_is_deterministic_seeded_and_hidden_safe) {
    const auto& first = small_value_g8();
    const auto repeated =
        old_school::train_learned_value_g8(
            1, 0x68A11EADULL);
    const auto changed =
        old_school::train_learned_value_g8(
            1, 0x68A11EAEULL);
    CHECK(repeated.report == first.report);
    CHECK(repeated.checkpoints.size() ==
          first.checkpoints.size());
    for (std::size_t index = 0;
         index < first.checkpoints.size(); ++index) {
        CHECK(old_school::learned_model_fingerprint(
                  repeated.checkpoints[index]) ==
              old_school::learned_model_fingerprint(
                  first.checkpoints[index]));
    }
    CHECK(old_school::learned_model_fingerprint(changed.model) !=
          old_school::learned_model_fingerprint(first.model));

    const auto fixture = determinization_fixture();
    const auto hidden =
        hidden_repartition(fixture.state, 0);
    CHECK(physical_cards(hidden, 1) ==
          physical_cards(fixture.state, 1));
    for (const auto& checkpoint : first.checkpoints) {
        CHECK(old_school::learned_critic_value(
                  fixture.state, 0, checkpoint) ==
              old_school::learned_critic_value(
                  hidden, 0, checkpoint));
    }
}

TEST(learned_value_g8_mix50_is_single_axis_deterministic_and_hidden_safe) {
    CHECK(throws_with_text(
        [] {
            static_cast<void>(
                old_school::train_learned_value_g8_mix50(
                    1, 0x68A15050ULL));
        },
        "even"));

    const auto& canonical = small_value_g8_eight_games();
    const auto& mix50 = small_value_g8_mix50();
    CHECK(canonical.report.recipe ==
          old_school::LearnedValueG8Recipe::
              CanonicalAllSearchLate);
    CHECK(mix50.report.recipe ==
          old_school::LearnedValueG8Recipe::LateMix50);
    CHECK(mix50.checkpoints.size() ==
          old_school::kLearnedValueG8Generations + 1);
    CHECK(mix50.report.generations.size() ==
          old_school::kLearnedValueG8Generations);

    std::vector<std::string> frozen_fingerprints;
    frozen_fingerprints.reserve(mix50.checkpoints.size());
    for (const auto& checkpoint : mix50.checkpoints) {
        frozen_fingerprints.push_back(
            old_school::learned_model_fingerprint(checkpoint));
    }
    for (std::size_t checkpoint = 0;
         checkpoint <= 4; ++checkpoint) {
        CHECK(old_school::learned_model_fingerprint(
                  mix50.checkpoints[checkpoint]) ==
              old_school::learned_model_fingerprint(
                  canonical.checkpoints[checkpoint]));
    }
    CHECK(old_school::learned_model_fingerprint(
              mix50.checkpoints[5]) !=
          old_school::learned_model_fingerprint(
              canonical.checkpoints[5]));

    constexpr std::array<std::size_t, 8>
        expected_replay_occupancy = {
            1, 2, 3, 3, 3, 3, 3, 3,
        };
    for (std::size_t index = 0;
         index < mix50.report.generations.size(); ++index) {
        const auto& generation =
            mix50.report.generations[index];
        CHECK(generation.generation == index + 1);
        CHECK(generation.self_play_games == 2);
        CHECK(generation.raw_collection_games ==
              (index < 4 ? 2U : 1U));
        CHECK(generation.search_collection_games ==
              (index < 4 ? 0U : 1U));
        CHECK(generation.raw_collection_examples > 0);
        if (index < 4) {
            CHECK(generation.search_collection_examples == 0);
        } else {
            CHECK(generation.search_collection_examples > 0);
        }
        CHECK(generation.raw_collection_examples +
                  generation.search_collection_examples ==
              generation.generation_examples);
        CHECK(generation.replay_generations ==
              expected_replay_occupancy[index]);
        CHECK(generation.parent_fingerprint ==
              frozen_fingerprints[index]);
        CHECK(generation.candidate_fingerprint ==
              frozen_fingerprints[index + 1]);
        CHECK(generation.search_enabled == (index >= 4));
        CHECK(generation.search_worlds ==
              (index >= 4 ? 1U : 0U));
        CHECK(generation.search_horizon_turns ==
              (index >= 4 ? 4U : 0U));
        if (index < 4) {
            CHECK(generation.rollout_evaluations == 0);
        } else {
            CHECK(generation.rollout_evaluations > 0);
        }
        if (index < 4) {
            const auto& canonical_generation =
                canonical.report.generations[index];
            CHECK(generation.generation_examples ==
                  canonical_generation.generation_examples);
            CHECK(generation.replay_examples ==
                  canonical_generation.replay_examples);
            CHECK(generation.parent_fingerprint ==
                  canonical_generation.parent_fingerprint);
            CHECK(generation.candidate_fingerprint ==
                  canonical_generation.candidate_fingerprint);
        }
    }

    const auto repeated =
        old_school::train_learned_value_g8_mix50(
            8, 0x68A15050ULL);
    const auto changed =
        old_school::train_learned_value_g8_mix50(
            8, 0x68A15051ULL);
    CHECK(repeated.report == mix50.report);
    for (std::size_t index = 0;
         index < mix50.checkpoints.size(); ++index) {
        CHECK(old_school::learned_model_fingerprint(
                  repeated.checkpoints[index]) ==
              frozen_fingerprints[index]);
        CHECK(old_school::learned_model_fingerprint(
                  mix50.checkpoints[index]) ==
              frozen_fingerprints[index]);
    }
    CHECK(old_school::learned_model_fingerprint(changed.model) !=
          frozen_fingerprints.back());

    const auto fixture = determinization_fixture();
    const auto hidden =
        hidden_repartition(fixture.state, 0);
    for (const auto& checkpoint : mix50.checkpoints) {
        CHECK(old_school::learned_critic_value(
                  fixture.state, 0, checkpoint) ==
              old_school::learned_critic_value(
                  hidden, 0, checkpoint));
    }
}

TEST(learned_value_g8_mix50_artifact_is_distinct_and_fail_closed) {
    const auto& canonical = small_value_g8_eight_games();
    const auto& mix50 = small_value_g8_mix50();
    const std::filesystem::path directory =
        "build/test-model-cache";
    const std::filesystem::path canonical_path =
        directory / "value-g8-canonical-eight.bin";
    const std::filesystem::path mix50_path =
        directory / "value-g8-mix50-eight.bin";
    const std::filesystem::path corrupt_path =
        directory / "value-g8-mix50-corrupt.bin";
    std::filesystem::remove(canonical_path);
    std::filesystem::remove(mix50_path);
    std::filesystem::remove(corrupt_path);

    old_school::write_learned_value_g8_bundle_atomic(
        canonical_path.string(), canonical);
    old_school::write_learned_value_g8_mix50_bundle_atomic(
        mix50_path.string(), mix50);
    const auto loaded =
        old_school::load_learned_value_g8_mix50_bundle(
            mix50_path.string(), 8, 0x68A15050ULL);
    CHECK(loaded.report == mix50.report);
    for (std::size_t index = 0;
         index < mix50.checkpoints.size(); ++index) {
        CHECK(old_school::learned_model_fingerprint(
                  loaded.checkpoints[index]) ==
              old_school::learned_model_fingerprint(
                  mix50.checkpoints[index]));
    }
    CHECK(old_school::learned_value_g8_mix50_cache_path(
              800, 424242) ==
          "build/model-cache/"
          "old-school-value-g8-mix50-v1-t800-s424242.bin");

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_mix50_bundle(
                    mix50_path.string(), 12,
                    0x68A15050ULL));
        },
        "training_games mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_mix50_bundle(
                    mix50_path.string(), 8,
                    0x68A15051ULL));
        },
        "training seed mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    mix50_path.string(), 8,
                    0x68A15050ULL));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_mix50_bundle(
                    canonical_path.string(), 8,
                    0x68A15050ULL));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            old_school::write_learned_value_g8_bundle_atomic(
                canonical_path.string(), mix50);
        },
        "recipe"));
    CHECK(throws_with_text(
        [&] {
            old_school::write_learned_value_g8_mix50_bundle_atomic(
                mix50_path.string(), canonical);
        },
        "recipe"));
    auto invalid_accounting = mix50;
    ++invalid_accounting.report.generations[4]
          .raw_collection_games;
    CHECK(throws_with_text(
        [&] {
            old_school::write_learned_value_g8_mix50_bundle_atomic(
                mix50_path.string(), invalid_accounting);
        },
        "collection accounting"));

    auto corrupt = read_binary_file(mix50_path);
    CHECK(corrupt.size() > 64);
    corrupt.back() ^= 0x01U;
    write_binary_file(corrupt_path, corrupt);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_mix50_bundle(
                    corrupt_path.string(), 8,
                    0x68A15050ULL));
        },
        "checksum"));

    // Failed cross-recipe publication validated before I/O and left both
    // previously published artifacts readable.
    CHECK(old_school::load_learned_value_g8_bundle(
              canonical_path.string(), 8,
              0x68A15050ULL)
              .report == canonical.report);
    CHECK(old_school::load_learned_value_g8_mix50_bundle(
              mix50_path.string(), 8,
              0x68A15050ULL)
              .report == mix50.report);

    std::filesystem::remove(canonical_path);
    std::filesystem::remove(mix50_path);
    std::filesystem::remove(corrupt_path);
}

TEST(learned_value_g8_artifact_roundtrips_every_checkpoint_bit_exact) {
    const auto& original = small_value_g8();
    const std::filesystem::path path =
        "build/test-model-cache/value-g8-roundtrip.bin";
    std::filesystem::remove(path);
    old_school::write_learned_value_g8_bundle_atomic(
        path.string(), original);
    const auto loaded =
        old_school::load_learned_value_g8_bundle(
            path.string(), 1, 0x68A11EADULL);

    CHECK(loaded.report == original.report);
    CHECK(loaded.model == loaded.checkpoints.back());
    CHECK(loaded.checkpoints.size() ==
          original.checkpoints.size());
    const std::array<old_school::GameState, 2> states = {
        old_school::white_lock_plan_diagnostic_state(),
        determinization_fixture().state,
    };
    for (std::size_t checkpoint = 0;
         checkpoint < original.checkpoints.size();
         ++checkpoint) {
        CHECK(old_school::learned_model_fingerprint(
                  loaded.checkpoints[checkpoint]) ==
              old_school::learned_model_fingerprint(
                  original.checkpoints[checkpoint]));
        for (const auto& state : states) {
            for (std::size_t perspective = 0;
                 perspective < 2; ++perspective) {
                const double before =
                    old_school::learned_critic_value(
                        state, perspective,
                        original.checkpoints[checkpoint]);
                const double after =
                    old_school::learned_critic_value(
                        state, perspective,
                        loaded.checkpoints[checkpoint]);
                CHECK(std::bit_cast<std::uint64_t>(after) ==
                      std::bit_cast<std::uint64_t>(before));
            }
        }
    }
    CHECK(old_school::learned_value_g8_cache_path(800, 424242) ==
          "build/model-cache/old-school-value-g8-v1-t800-s424242.bin");
    std::filesystem::remove(path);
}

TEST(learned_value_g8_generation_selector_maps_exact_checkpoint) {
    const auto& bundle = small_value_g8();
    const auto g3 =
        old_school::learned_value_g8_generation_checkpoint(bundle, 3);
    CHECK(g3 == bundle.checkpoints[3]);
    CHECK(old_school::learned_model_fingerprint(g3) ==
          bundle.report.generations[2].candidate_fingerprint);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_value_g8_generation_checkpoint(
                    bundle, 0));
        },
        "between one and eight"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_value_g8_generation_checkpoint(
                    bundle, 9));
        },
        "between one and eight"));
}

TEST(learned_value_g8_artifact_rejects_mismatch_and_corruption_fail_closed) {
    const auto& original = small_value_g8();
    const std::filesystem::path directory =
        "build/test-model-cache";
    const std::filesystem::path good =
        directory / "value-g8-good.bin";
    const std::filesystem::path corrupt =
        directory / "value-g8-corrupt.bin";
    const std::filesystem::path truncated =
        directory / "value-g8-truncated.bin";
    const std::filesystem::path trailing =
        directory / "value-g8-trailing.bin";
    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(truncated);
    std::filesystem::remove(trailing);
    old_school::write_learned_value_g8_bundle_atomic(
        good.string(), original);

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    good.string(), 2, 0x68A11EADULL));
        },
        "training_games mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    good.string(), 1, 0x68A11EAEULL));
        },
        "training seed mismatch"));

    std::string embedded_nul_path = good.string();
    embedded_nul_path.push_back('\0');
    embedded_nul_path += ".not-the-same-file";
    CHECK(throws_with_text(
        [&] {
            old_school::write_learned_value_g8_bundle_atomic(
                embedded_nul_path, original);
        },
        "embedded NUL"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    embedded_nul_path, 1,
                    0x68A11EADULL));
        },
        "embedded NUL"));

    const auto bytes = read_binary_file(good);
    CHECK(bytes.size() > 64);
    auto changed = bytes;
    changed.back() ^= 0x01U;
    write_binary_file(corrupt, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    corrupt.string(), 1, 0x68A11EADULL));
        },
        "checksum"));

    changed = bytes;
    changed.resize(changed.size() - 17);
    write_binary_file(truncated, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    truncated.string(), 1,
                    0x68A11EADULL));
        },
        "payload length"));

    changed = bytes;
    changed.push_back(0);
    write_binary_file(trailing, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    trailing.string(), 1,
                    0x68A11EADULL));
        },
        "payload length"));

    auto invalid = original;
    invalid.report.final_fingerprint =
        std::string(64, '0');
    CHECK(throws_with_text(
        [&] {
            old_school::write_learned_value_g8_bundle_atomic(
                good.string(), invalid);
        },
        "final_fingerprint"));
    const auto still_good =
        old_school::load_learned_value_g8_bundle(
            good.string(), 1, 0x68A11EADULL);
    CHECK(still_good.report == original.report);

    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(truncated);
    std::filesystem::remove(trailing);
}

TEST(learned_actor_updates_deep_clone_critic_and_policy_without_mutating_parent) {
    const auto parent = small_actor_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() >= 2);

    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const double parent_critic =
        old_school::learned_critic_value(state, 0, parent);
    const auto parent_logits =
        old_school::learned_actor_priority_logits(
            state, 0, true, old_school::TurnPhase::FirstMain, 0,
            actions, parent);

    std::vector<std::vector<double>> policy_options;
    policy_options.reserve(actions.size());
    for (const auto& action : actions) {
        policy_options.push_back(
            old_school::learned_priority_policy_features(
                state, 0, action, true,
                old_school::TurnPhase::FirstMain, 0));
    }

    // Even a no-op update is a recursive clone, but its serialized content
    // and both prediction paths are initially bit-identical.
    const auto frozen_clone =
        old_school::update_learned_actor_model(parent, {}, {}, {});
    CHECK(frozen_clone.get() != parent.get());
    CHECK(old_school::learned_model_fingerprint(frozen_clone) ==
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(state, 0, frozen_clone) ==
          parent_critic);
    CHECK(old_school::learned_actor_priority_logits(
              state, 0, true, old_school::TurnPhase::FirstMain, 0,
              actions, frozen_clone) == parent_logits);

    const double critic_target =
        parent_critic < 0.5 ? 1.0 : 0.0;
    const auto critic_candidate =
        old_school::update_learned_actor_model(
            frozen_clone,
            {{
                .features = old_school::learned_observation(state, 0),
                .target = critic_target,
            }},
            {},
            {
                .critic_epochs = 4,
                .critic_learning_rate = 0.05,
                .critic_seed = 0xC1171CULL,
                .policy_epochs = 1,
                .policy_learning_rate = 0.001,
                .policy_seed = 0x5011C9ULL,
            });
    CHECK(old_school::learned_model_fingerprint(critic_candidate) !=
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(
              state, 0, critic_candidate) != parent_critic);
    CHECK(old_school::learned_actor_priority_logits(
              state, 0, true, old_school::TurnPhase::FirstMain, 0,
              actions, critic_candidate) == parent_logits);

    std::vector<double> soft_target(actions.size(), 0.0);
    const std::size_t target_option =
        static_cast<std::size_t>(std::min_element(
            parent_logits.begin(), parent_logits.end()) -
                                 parent_logits.begin());
    soft_target[target_option] = 1.0;
    const auto policy_candidate =
        old_school::update_learned_actor_model(
            frozen_clone, {},
            {{
                .options = policy_options,
                .target_probabilities = soft_target,
                .decision_kind =
                    old_school::LearnedPolicyDecisionKind::Priority,
                .weight = 1.0,
            }},
            {
                .critic_epochs = 1,
                .critic_learning_rate = 0.001,
                .critic_seed = 0xC1171CULL,
                .policy_epochs = 4,
                .policy_learning_rate = 0.01,
                .policy_seed = 0x5011C9ULL,
            });
    CHECK(old_school::learned_model_fingerprint(policy_candidate) !=
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(
              state, 0, policy_candidate) == parent_critic);
    CHECK(old_school::learned_actor_priority_logits(
              state, 0, true, old_school::TurnPhase::FirstMain, 0,
              actions, policy_candidate) != parent_logits);

    // Neither the original publication nor the no-op cloned publication was
    // aliased by either mutable training candidate.
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(old_school::learned_model_fingerprint(frozen_clone) ==
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(state, 0, parent) ==
          parent_critic);
    CHECK(old_school::learned_actor_priority_logits(
              state, 0, true, old_school::TurnPhase::FirstMain, 0,
          actions, parent) == parent_logits);
}

TEST(learned_actor_fit_diagnostics_show_synthetic_loss_reduction) {
    const auto parent = small_actor_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() >= 2);

    std::vector<std::vector<double>> options;
    options.reserve(actions.size());
    for (const auto& action : actions) {
        options.push_back(
            old_school::learned_priority_policy_features(
                state, 0, action, true,
                old_school::TurnPhase::FirstMain, 0));
    }
    const auto parent_logits =
        old_school::learned_actor_priority_logits(
            state, 0, true, old_school::TurnPhase::FirstMain, 0,
            actions, parent);
    const std::size_t disfavored_priority =
        static_cast<std::size_t>(std::min_element(
            parent_logits.begin(), parent_logits.end()) -
                                 parent_logits.begin());
    std::vector<double> priority_target(actions.size(), 0.0);
    priority_target[disfavored_priority] = 1.0;
    std::vector<double> attack_target(actions.size(), 0.0);
    attack_target[0] = 1.0;

    const std::vector<old_school::LearnedCriticTrainingExample>
        critic_examples = {{
            .features = old_school::learned_observation(state, 0),
            .target =
                old_school::learned_critic_value(state, 0, parent) <
                        0.5
                    ? 1.0
                    : 0.0,
        }};
    const std::vector<old_school::LearnedPolicyTrainingExample>
        policy_examples = {
            {
                .options = options,
                .target_probabilities = priority_target,
                .decision_kind =
                    old_school::LearnedPolicyDecisionKind::Priority,
                .weight = 0.25,
            },
            {
                .options = options,
                .target_probabilities = priority_target,
                .decision_kind =
                    old_school::LearnedPolicyDecisionKind::Priority,
                .weight = 0.75,
            },
            {
                .options = options,
                .target_probabilities = attack_target,
                .decision_kind =
                    old_school::LearnedPolicyDecisionKind::Attack,
                .weight = 2.0,
            },
        };
    const auto candidate =
        old_school::update_learned_actor_model(
            parent, critic_examples, policy_examples,
            {
                .critic_epochs = 4,
                .critic_learning_rate = 0.02,
                .critic_seed = 0xF17C1171CULL,
                .policy_epochs = 4,
                .policy_learning_rate = 0.005,
                .policy_seed = 0xF175011C9ULL,
            });
    const auto fit = old_school::diagnose_learned_actor_fit(
        parent, candidate, critic_examples, policy_examples);

    CHECK(fit.priority.example_count == 2);
    CHECK(fit.priority.total_weight == 1.0);
    CHECK(fit.attack.example_count == 1);
    CHECK(fit.attack.total_weight == 2.0);
    CHECK(fit.critic.example_count == 1);
    CHECK(fit.priority.candidate_weighted_cross_entropy <
          fit.priority.parent_weighted_cross_entropy);
    CHECK(fit.attack.candidate_weighted_cross_entropy <
          fit.attack.parent_weighted_cross_entropy);
    CHECK(fit.critic.candidate_mean_squared_error <
          fit.critic.parent_mean_squared_error);
    CHECK(fit.critic.candidate_binary_cross_entropy <
          fit.critic.parent_binary_cross_entropy);
    CHECK(fit.priority.weighted_teacher_entropy == 0.0);
    CHECK(fit.attack.weighted_teacher_entropy == 0.0);
    CHECK(fit.priority.parent_excess_cross_entropy ==
          fit.priority.parent_weighted_cross_entropy);
    CHECK(fit.priority.candidate_excess_cross_entropy ==
          fit.priority.candidate_weighted_cross_entropy);
    CHECK(fit.critic.target_variance == 0.0);
    CHECK(fit.priority.parent_expected_top_one_agreement >=
          0.0);
    CHECK(fit.priority.parent_expected_top_one_agreement <=
          1.0);
    CHECK(fit.priority.candidate_expected_top_one_agreement >=
          0.0);
    CHECK(fit.priority.candidate_expected_top_one_agreement <=
          1.0);
    CHECK(fit.priority.changed_argmax_examples <=
          fit.priority.example_count);
    CHECK(fit.attack.changed_argmax_examples <=
          fit.attack.example_count);
    CHECK(fit.priority.changed_argmax_weight >= 0.0);
    CHECK(fit.priority.changed_argmax_weight <=
          fit.priority.total_weight);
    CHECK(fit.priority.changed_argmax_weight_fraction >= 0.0);
    CHECK(fit.priority.changed_argmax_weight_fraction <= 1.0);
}

TEST(learned_actor_fit_diagnostics_match_exact_tie_semantics) {
    const auto model = small_actor_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() >= 2);

    std::vector<std::vector<double>> options;
    options.reserve(actions.size());
    for (const auto& action : actions) {
        options.push_back(
            old_school::learned_priority_policy_features(
                state, 0, action, true,
                old_school::TurnPhase::FirstMain, 0));
    }

    const std::vector<old_school::LearnedPolicyTrainingExample>
        tied_model_examples = {{
            .options = {options[0], options[0]},
            .target_probabilities = {1.0, 0.0},
            .decision_kind =
                old_school::LearnedPolicyDecisionKind::Priority,
            .weight = 3.0,
        }};
    const auto tied_model_fit =
        old_school::diagnose_learned_actor_fit(
            model, model, {}, tied_model_examples);
    CHECK(std::abs(
              tied_model_fit.priority
                  .parent_expected_top_one_agreement -
              0.5) <
          1.0e-12);
    CHECK(std::abs(
              tied_model_fit.priority
                  .candidate_expected_top_one_agreement -
              0.5) <
          1.0e-12);
    CHECK(tied_model_fit.priority.changed_argmax_examples == 0);
    CHECK(tied_model_fit.priority.changed_argmax_weight == 0.0);

    const auto logits =
        old_school::learned_actor_priority_logits(
            state, 0, true, old_school::TurnPhase::FirstMain, 0,
            actions, model);
    std::size_t first = 0;
    std::size_t second = 0;
    bool found_unequal_logits = false;
    for (std::size_t left = 0;
         left < logits.size() && !found_unequal_logits; ++left) {
        for (std::size_t right = left + 1;
             right < logits.size(); ++right) {
            if (logits[left] != logits[right]) {
                first = left;
                second = right;
                found_unequal_logits = true;
                break;
            }
        }
    }
    CHECK(found_unequal_logits);
    const std::vector<old_school::LearnedPolicyTrainingExample>
        tied_teacher_examples = {{
            .options = {options[first], options[second]},
            .target_probabilities = {0.5, 0.5},
            .decision_kind =
                old_school::LearnedPolicyDecisionKind::Priority,
            .weight = 1.0,
        }};
    const auto tied_teacher_fit =
        old_school::diagnose_learned_actor_fit(
            model, model, {}, tied_teacher_examples);
    CHECK(tied_teacher_fit.priority
              .parent_expected_top_one_agreement == 1.0);
    CHECK(tied_teacher_fit.priority
              .candidate_expected_top_one_agreement == 1.0);
    CHECK(std::abs(
              tied_teacher_fit.priority
                  .weighted_teacher_entropy -
              std::log(2.0)) <
          1.0e-12);
}

TEST(learned_actor_generation_is_balanced_bounded_immutable_and_deterministic) {
    const old_school::LearnedActorGenerationConfig defaults;
    CHECK(defaults.search_worlds == 8);
    CHECK(defaults.rollouts_per_world == 1);
    CHECK(defaults.horizon_turns == 0);
    CHECK(defaults.max_roots_per_seat_kind == 24);
    CHECK(defaults.td_lambda == 0.90);
    CHECK(defaults.critic_epochs == 2);
    CHECK(defaults.critic_learning_rate == 0.002);
    CHECK(defaults.policy_epochs == 2);
    CHECK(defaults.policy_learning_rate == 0.001);

    const auto parent = small_actor_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const double parent_critic =
        old_school::learned_critic_value(state, 0, parent);
    const auto parent_logits =
        old_school::learned_actor_priority_logits(
            state, 0, true, old_school::TurnPhase::FirstMain, 0,
            actions, parent);

    constexpr std::uint64_t kRootSeed = 0x617E2A710ULL;
    const old_school::LearnedActorGenerationConfig fast = {
        .search_worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .max_roots_per_seat_kind = 1,
        .td_lambda = 0.90,
        .critic_epochs = 1,
        .critic_learning_rate = 0.002,
        .policy_epochs = 1,
        .policy_learning_rate = 0.001,
        .generation = 1,
    };
    const auto first =
        old_school::train_learned_actor_generation(
            parent, kRootSeed, fast);
    const auto expected =
        old_school::learned_iteration::balanced_schedule(
            kRootSeed, fast.generation);

    CHECK(first.model);
    CHECK(first.report.games.size() == expected.size());
    CHECK(first.report.games.size() == 40);
    CHECK(first.report.parent_fingerprint ==
          parent_fingerprint);
    CHECK(first.report.candidate_fingerprint ==
          old_school::learned_model_fingerprint(first.model));
    CHECK(first.report.candidate_fingerprint !=
          parent_fingerprint);
    CHECK(first.report.replay_generations == 1);
    CHECK(first.report.priority_policy_examples ==
          first.report.priority_roots);
    CHECK(first.report.attack_policy_examples ==
          first.report.attack_roots);
    CHECK(first.report.fit.priority.example_count ==
          first.report.priority_policy_examples);
    CHECK(first.report.fit.attack.example_count ==
          first.report.attack_policy_examples);
    CHECK(first.report.fit.critic.example_count ==
          first.report.critic_examples);
    CHECK(first.report.critic_examples +
              first.report.deduplicated_critic_observations ==
          first.report.priority_roots +
              first.report.attack_roots);
    CHECK(std::abs(
              first.report.minimum_policy_target_sum - 1.0) <
          1.0e-12);
    CHECK(std::abs(
              first.report.maximum_policy_target_sum - 1.0) <
          1.0e-12);
    CHECK(first.report.attack_rollout_evaluations ==
          2 * first.report.attack_roots);
    CHECK(first.report.priority_rollout_evaluations >=
          2 * first.report.priority_roots);

    std::size_t priority_roots = 0;
    std::size_t attack_roots = 0;
    std::size_t attack_includes = 0;
    double priority_weight_sum = 0.0;
    double attack_weight_sum = 0.0;
    for (std::size_t index = 0;
         index < first.report.games.size(); ++index) {
        const auto& actual = first.report.games[index];
        const auto& scheduled = expected[index];
        CHECK(actual.schedule_index ==
              scheduled.schedule_index);
        CHECK(actual.pairing_index ==
              scheduled.pairing_index);
        CHECK(actual.seat_decks == scheduled.seat_decks);
        CHECK(actual.starting_player ==
              scheduled.starting_player);
        CHECK(actual.game_seed == scheduled.seed);
        for (std::size_t player = 0; player < 2; ++player) {
            CHECK(actual.priority_roots_by_seat[player] <= 1);
            CHECK(actual.attack_roots_by_seat[player] <= 1);
            CHECK(actual.attack_includes_by_seat[player] <=
                  actual.attack_roots_by_seat[player]);
            if (actual.priority_roots_by_seat[player] != 0) {
                CHECK(std::abs(
                          actual.priority_policy_weight_sums[player] -
                          1.0) <
                      1.0e-12);
            }
            if (actual.attack_roots_by_seat[player] != 0) {
                CHECK(std::abs(
                          actual.attack_policy_weight_sums[player] -
                          1.0) <
                      1.0e-12);
            }
            priority_roots +=
                actual.priority_roots_by_seat[player];
            attack_roots +=
                actual.attack_roots_by_seat[player];
            attack_includes +=
                actual.attack_includes_by_seat[player];
            priority_weight_sum +=
                actual.priority_policy_weight_sums[player];
            attack_weight_sum +=
                actual.attack_policy_weight_sums[player];
        }
    }
    CHECK(priority_roots == first.report.priority_roots);
    CHECK(attack_roots == first.report.attack_roots);
    CHECK(attack_includes <= attack_roots);
    CHECK(std::abs(
              first.report.fit.priority.total_weight -
              priority_weight_sum) <
          1.0e-12);
    CHECK(std::abs(
              first.report.fit.attack.total_weight -
              attack_weight_sum) <
          1.0e-12);
    const std::array<double, 24> fit_metrics = {
        first.report.fit.priority
            .parent_expected_top_one_agreement,
        first.report.fit.priority
            .candidate_expected_top_one_agreement,
        first.report.fit.priority.weighted_teacher_entropy,
        first.report.fit.priority
            .parent_weighted_cross_entropy,
        first.report.fit.priority
            .candidate_weighted_cross_entropy,
        first.report.fit.priority
            .parent_excess_cross_entropy,
        first.report.fit.priority
            .candidate_excess_cross_entropy,
        first.report.fit.priority.changed_argmax_weight,
        first.report.fit.priority
            .changed_argmax_weight_fraction,
        first.report.fit.attack
            .parent_expected_top_one_agreement,
        first.report.fit.attack
            .candidate_expected_top_one_agreement,
        first.report.fit.attack.weighted_teacher_entropy,
        first.report.fit.attack
            .parent_weighted_cross_entropy,
        first.report.fit.attack
            .candidate_weighted_cross_entropy,
        first.report.fit.attack.parent_excess_cross_entropy,
        first.report.fit.attack
            .candidate_excess_cross_entropy,
        first.report.fit.attack.changed_argmax_weight,
        first.report.fit.attack.changed_argmax_weight_fraction,
        first.report.fit.critic.target_mean,
        first.report.fit.critic.target_variance,
        first.report.fit.critic.parent_mean_squared_error,
        first.report.fit.critic.candidate_mean_squared_error,
        first.report.fit.critic.parent_binary_cross_entropy,
        first.report.fit.critic
            .candidate_binary_cross_entropy,
    };
    CHECK(std::all_of(
        fit_metrics.begin(), fit_metrics.end(),
        [](double value) { return std::isfinite(value); }));
    CHECK(first.report.fit.priority.changed_argmax_examples <=
          first.report.fit.priority.example_count);
    CHECK(first.report.fit.attack.changed_argmax_examples <=
          first.report.fit.attack.example_count);

    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(old_school::learned_critic_value(state, 0, parent) ==
          parent_critic);
    CHECK(old_school::learned_actor_priority_logits(
              state, 0, true, old_school::TurnPhase::FirstMain, 0,
              actions, parent) == parent_logits);

    const auto repeated =
        old_school::train_learned_actor_generation(
            parent, kRootSeed, fast);
    CHECK(repeated.model.get() != first.model.get());
    CHECK(repeated.report == first.report);
    CHECK(old_school::learned_model_fingerprint(repeated.model) ==
          old_school::learned_model_fingerprint(first.model));
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
}

TEST(learned_actor_generation_attack_search_controls_real_lethal_combat) {
    auto fixture =
        attack_evaluation_fixture(old_school::CardId::GrizzlyBears);
    fixture.state.players[1].library.push_back(
        old_school::CardId::GrizzlyBears);
    fixture.state.players[1].creatures.clear();
    fixture.state.players[1].life = 3;

    const auto diagnostic =
        old_school::diagnose_learned_actor_generation_attack(
            fixture.state, fixture.decks, small_actor_model(),
            {
                .seed = 0xA77AC6ULL,
                .worlds = 2,
                .rollouts_per_world = 1,
                .horizon_turns = 0,
                .continuation_variant =
                    old_school::LearnedVariant::UnifiedActor,
                .blend_shallow_prior = false,
            });
    CHECK(diagnostic.searched_roots == 1);
    CHECK(diagnostic.rollout_evaluations == 4);
    CHECK(diagnostic.included_attackers == 1);
    CHECK(diagnostic.terminal_result.has_value());
    CHECK(diagnostic.terminal_result->winner == 0);
    CHECK(diagnostic.terminal_result->reason ==
          old_school::EndReason::LifeTotal);
    CHECK(diagnostic.final_state.players[1].life == 0);
    CHECK(diagnostic.final_state.players[0].creatures[0].tapped);
}

TEST(learned_actor_generation_priority_search_applies_real_counterspell) {
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::blue_deck(),
        old_school::red_deck(),
    };
    old_school::GameState state;
    state.active_player = 1;
    state.starting_player = 1;
    state.turn_number = 8;
    state.next_stack_object_id = 2;
    state.players[0].life = 3;
    state.players[0].hand = {
        old_school::CardId::Counterspell,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    state.players[0].library = decks[0];
    remove_fixture_card(
        state.players[0].library, old_school::CardId::Counterspell);
    remove_fixture_card(
        state.players[0].library, old_school::CardId::Island);
    remove_fixture_card(
        state.players[0].library, old_school::CardId::Island);

    state.players[1].lands = {
        {.card = old_school::CardId::Mountain, .tapped = true},
    };
    state.players[1].library = decks[1];
    remove_fixture_card(
        state.players[1].library, old_school::CardId::Mountain);
    remove_fixture_card(
        state.players[1].library, old_school::CardId::LightningBolt);
    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::LightningBolt,
            .controller = 1,
            .target = old_school::Target::player_target(0),
            .spell_target = std::nullopt,
        },
    };

    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() == 2);
    CHECK(std::find(
              actions.begin(), actions.end(),
              old_school::PriorityAction::cast_counterspell(1)) !=
          actions.end());

    const auto diagnostic =
        old_school::diagnose_learned_actor_generation_priority(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            small_actor_model(),
            {
                .seed = 0xC0A17E5EEDULL,
                .worlds = 2,
                .rollouts_per_world = 1,
                .horizon_turns = 0,
                .continuation_variant =
                    old_school::LearnedVariant::UnifiedActor,
                .blend_shallow_prior = false,
            });

    CHECK(diagnostic.searched_roots == 1);
    CHECK(diagnostic.rollout_evaluations == 4);
    CHECK(diagnostic.selected_action ==
          old_school::PriorityAction::cast_counterspell(1));
    CHECK(diagnostic.transition_applied);
    CHECK(!diagnostic.pass_result.has_value());
    CHECK(!diagnostic.terminal_result.has_value());
    CHECK(diagnostic.final_state.players[0].life == 3);
    CHECK(diagnostic.final_state.players[0].hand.empty());
    CHECK(std::all_of(
        diagnostic.final_state.players[0].lands.begin(),
        diagnostic.final_state.players[0].lands.end(),
        [](const old_school::LandPermanent& land) {
            return land.tapped;
        }));
    CHECK(diagnostic.final_state.stack.size() == 2);
    CHECK(diagnostic.final_state.stack[0].card ==
          old_school::CardId::LightningBolt);
    CHECK(diagnostic.final_state.stack[1].card ==
          old_school::CardId::Counterspell);
    CHECK(diagnostic.final_state.stack[1].controller == 0);
    CHECK(diagnostic.final_state.stack[1].spell_target ==
          std::optional<old_school::StackObjectId>{1});
    CHECK(diagnostic.final_state.next_stack_object_id == 3);
    CHECK(diagnostic.final_state.stats[0].spells_cast == 1);
}

TEST(generic_priority_samples_resolve_stack_and_bound_horizon) {
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::red_deck(),
        old_school::red_deck(),
    };
    old_school::GameState state;
    state.active_player = 1;
    state.starting_player = 1;
    state.turn_number = 10;
    state.players[0].life = 3;
    state.players[0].library = decks[0];
    state.players[1].lands = {
        {.card = old_school::CardId::Mountain, .tapped = true},
    };
    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::LightningBolt,
            .controller = 1,
            .target = old_school::Target::player_target(0),
            .spell_target = std::nullopt,
        },
    };
    state.next_stack_object_id = 2;
    state.players[1].library = decks[1];
    remove_fixture_card(
        state.players[1].library, old_school::CardId::Mountain);
    remove_fixture_card(
        state.players[1].library, old_school::CardId::LightningBolt);
    const std::vector<old_school::PriorityAction> actions = {
        old_school::PriorityAction::pass(),
    };
    const old_school::LearnedSearchConfig actor_config = {
        .seed = 0x57ACCA55ULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto actor_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1, actions,
            small_actor_model(), actor_config);
    CHECK(actor_samples.rollout_evaluations == 4);
    CHECK(actor_samples.q_samples.size() == 1);
    CHECK(actor_samples.q_samples[0] ==
          std::vector<double>({0.0, 0.0, 0.0, 0.0}));

    old_school::LearnedSearchConfig value_config = actor_config;
    value_config.worlds = 1;
    value_config.rollouts_per_world = 1;
    value_config.continuation_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    const auto value_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1, actions,
            small_value_model(), value_config);
    CHECK(value_samples.q_samples ==
          std::vector<std::vector<double>>({{0.0}}));

    bool rejected_mismatch = false;
    try {
        static_cast<void>(
            old_school::learned_priority_action_samples(
                state, decks, 0, false,
                old_school::TurnPhase::BeginCombat, 1, actions,
                small_actor_model(), value_config));
    } catch (const std::invalid_argument&) {
        rejected_mismatch = true;
    }
    CHECK(rejected_mismatch);

    bool rejected_unbounded = false;
    old_school::LearnedSearchConfig unbounded = actor_config;
    unbounded.horizon_turns = 129;
    try {
        static_cast<void>(
            old_school::learned_priority_action_samples(
                state, decks, 0, false,
                old_school::TurnPhase::BeginCombat, 1, actions,
                small_actor_model(), unbounded));
    } catch (const std::invalid_argument&) {
        rejected_unbounded = true;
    }
    CHECK(rejected_unbounded);

    old_school::GameState boundary;
    boundary.active_player = 0;
    boundary.starting_player = 0;
    boundary.turn_number = 1;
    boundary.players[0].graveyard = decks[0];
    boundary.players[1].library = {
        old_school::CardId::Mountain,
    };
    boundary.players[1].graveyard = decks[1];
    remove_fixture_card(
        boundary.players[1].graveyard,
        old_school::CardId::Mountain);
    old_school::LearnedSearchConfig horizon_zero = actor_config;
    horizon_zero.worlds = 1;
    horizon_zero.rollouts_per_world = 1;
    const double h0 =
        old_school::learned_priority_action_samples(
            boundary, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_zero)
            .q_samples[0][0];
    CHECK(h0 > 0.0 && h0 < 1.0);

    old_school::LearnedSearchConfig horizon_one = horizon_zero;
    horizon_one.horizon_turns = 1;
    const double h1 =
        old_school::learned_priority_action_samples(
            boundary, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_one)
            .q_samples[0][0];
    // H1 bootstraps after turn two cleanup. Preparing turn three here would
    // make player zero draw from its empty library and return exactly zero.
    CHECK(h1 > 0.0 && h1 < 1.0);

    auto empty_next_library = boundary;
    empty_next_library.players[1].graveyard.push_back(
        empty_next_library.players[1].library.back());
    empty_next_library.players[1].library.clear();
    const double deck_out =
        old_school::learned_priority_action_samples(
            empty_next_library, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_zero)
            .q_samples[0][0];
    CHECK(deck_out == 1.0);
}

TEST(generic_binary_attack_samples_use_deployed_combat_and_obey_moat) {
    const DeterminizationFixture fixture =
        attack_evaluation_fixture(old_school::CardId::GrizzlyBears);
    const old_school::LearnedSearchConfig actor_config = {
        .seed = 0xA77AC5EEDULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto baseline =
        old_school::learned_binary_attack_samples(
            fixture.state, fixture.decks, 0, {}, 1, {},
            small_actor_model(), actor_config);
    CHECK(baseline.sampled_worlds == 2);
    CHECK(baseline.rollout_evaluations == 8);
    CHECK(baseline.q_samples.size() == 2);
    for (const auto& samples : baseline.q_samples) {
        CHECK(samples.size() == 4);
        CHECK(std::all_of(
            samples.begin(), samples.end(),
            [](double value) {
                return std::isfinite(value) &&
                       value >= 0.0 && value <= 1.0;
            }));
    }
    const auto hidden =
        hidden_repartition(fixture.state, 0);
    CHECK(old_school::learned_binary_attack_samples(
              hidden, fixture.decks, 0, {}, 1, {},
              small_actor_model(), actor_config)
              .q_samples == baseline.q_samples);
    const auto logits =
        old_school::learned_actor_binary_attack_logits(
            fixture.state, 0, {}, 1, {}, small_actor_model());
    CHECK(std::isfinite(logits[0]));
    CHECK(std::isfinite(logits[1]));

    old_school::LearnedSearchConfig value_config = actor_config;
    value_config.worlds = 1;
    value_config.rollouts_per_world = 1;
    value_config.continuation_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    const auto value_samples =
        old_school::learned_binary_attack_samples(
            fixture.state, fixture.decks, 0, {}, 1, {},
            small_value_model(), value_config);
    CHECK(value_samples.q_samples.size() == 2);
    CHECK(value_samples.q_samples[0].size() == 1);
    CHECK(value_samples.q_samples[1].size() == 1);

    auto moated = fixture.state;
    moated.players[1].enchantments.push_back(old_school::CardId::Moat);
    bool rejected_moat = false;
    try {
        static_cast<void>(
            old_school::learned_binary_attack_samples(
                moated, fixture.decks, 0, {}, 1, {},
                small_actor_model(), actor_config));
    } catch (const std::invalid_argument&) {
        rejected_moat = true;
    }
    CHECK(rejected_moat);
}

TEST(learned_value_attack_set_scores_match_deployed_argmax_and_hide_cards) {
    const std::vector<std::vector<old_school::PermanentId>> candidates = {
        {},
        {1},
    };
    constexpr std::uint64_t seed = 0xB10C5C0EULL;
    for (const old_school::CardId blocker : {
             old_school::CardId::GrizzlyBears,
             old_school::CardId::FireElemental,
         }) {
        const auto fixture = attack_evaluation_fixture(blocker);
        const auto scored =
            old_school::learned_value_attack_set_scores(
                fixture.state, 0, candidates,
                small_value_model(), seed);
        CHECK(scored.scores.size() == candidates.size());
        CHECK(std::all_of(
            scored.scores.begin(), scored.scores.end(),
            [](double score) {
                return std::isfinite(score) &&
                       score >= 0.0 && score <= 1.0;
            }));

        // This is the exact strict comparison used by the deployed selector:
        // std::max_element also retains the first candidate on a tie.
        const std::size_t deployed_argmax =
            static_cast<std::size_t>(std::distance(
                scored.scores.begin(),
                std::max_element(
                    scored.scores.begin(), scored.scores.end())));
        CHECK(scored.selected_candidate == deployed_argmax);

        const auto hidden =
            old_school::learned_value_attack_set_scores(
                hidden_repartition(fixture.state, 0), 0,
                candidates, small_value_model(), seed);
        CHECK(hidden.scores == scored.scores);
        CHECK(hidden.selected_candidate ==
              scored.selected_candidate);
    }

    bool rejected_actor = false;
    try {
        static_cast<void>(
            old_school::learned_value_attack_set_scores(
                attack_evaluation_fixture(
                    old_school::CardId::GrizzlyBears)
                    .state,
                0, candidates, small_actor_model(), seed));
    } catch (const std::invalid_argument&) {
        rejected_actor = true;
    }
    CHECK(rejected_actor);
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

    old_school::cleanup_turn(state);
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
    old_school::cleanup_turn(cleaned);
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

    old_school::cleanup_turn(state);
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
    old_school::cleanup_turn(state);
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

TEST(five_deck_tournament_runs_all_ten_pairings) {
    const auto result = old_school::run_tournament(100, 0xC0FFEEULL);
    CHECK(result.games_per_matchup == 100);
    CHECK(result.total_games == 1000);
    const std::array<std::pair<old_school::DeckId, old_school::DeckId>, 10>
        expected_pairings = {{
            {old_school::DeckId::Green, old_school::DeckId::Red},
            {old_school::DeckId::Green, old_school::DeckId::Blue},
            {old_school::DeckId::Green, old_school::DeckId::White},
            {old_school::DeckId::Green, old_school::DeckId::RUAggro},
            {old_school::DeckId::Red, old_school::DeckId::Blue},
            {old_school::DeckId::Red, old_school::DeckId::White},
            {old_school::DeckId::Red, old_school::DeckId::RUAggro},
            {old_school::DeckId::Blue, old_school::DeckId::White},
            {old_school::DeckId::Blue, old_school::DeckId::RUAggro},
            {old_school::DeckId::White, old_school::DeckId::RUAggro},
        }};
    for (std::size_t index = 0;
         index < result.matchups.size(); ++index) {
        const auto& matchup = result.matchups[index];
        CHECK(matchup.first_deck == expected_pairings[index].first);
        CHECK(matchup.second_deck == expected_pairings[index].second);
        CHECK(matchup.result.games == 100);
        CHECK(matchup.result.decks[0].wins +
                  matchup.result.decks[1].wins +
                  matchup.result.draws ==
              100);
    }
    for (const auto& deck : result.decks) {
        CHECK(deck.games == 400);
        CHECK(deck.wins + deck.losses + deck.draws == 400);
    }
    CHECK(result.decks[static_cast<std::size_t>(
              old_school::DeckId::Blue)]
              .total_spells_countered >
          0);
    CHECK(result.decks[static_cast<std::size_t>(
              old_school::DeckId::White)]
              .total_cards_milled >
          0);
    CHECK(result.life_total_finishes + result.empty_library_finishes +
              result.turn_limit_draws ==
          1000);
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

TEST(mixed_tournament_rotates_all_five_bot_kinds) {
    const old_school::TournamentConfig bots = {
        .bot_field = old_school::BotField::Mixed,
        .monte_carlo_rollouts = 1,
        .deep_monte_carlo_rollouts = 4,
        .learned_training_games = 20,
    };
    const auto result =
        old_school::run_tournament(25, 0xDEC1DEULL, {}, bots);
    const auto& random =
        result.bots[static_cast<std::size_t>(old_school::BotKind::Random)];
    const auto& monte_carlo =
        result.bots[static_cast<std::size_t>(
            old_school::BotKind::MonteCarlo)];
    const auto& deep_monte_carlo =
        result.bots[static_cast<std::size_t>(
            old_school::BotKind::DeepMonteCarlo)];
    const auto& handcrafted =
        result.bots[static_cast<std::size_t>(
            old_school::BotKind::Handcrafted)];
    const auto& learned =
        result.bots[static_cast<std::size_t>(
            old_school::BotKind::Learned)];

    CHECK(result.total_games == 250);
    CHECK(random.games == 100);
    CHECK(monte_carlo.games == 100);
    CHECK(deep_monte_carlo.games == 100);
    CHECK(handcrafted.games == 100);
    CHECK(learned.games == 100);
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
        CHECK(matchup.games == 20);
        CHECK(matchup.first_wins + matchup.second_wins +
                  matchup.draws ==
              matchup.games);
    }
    for (std::size_t deck = 0; deck < result.decks.size(); ++deck) {
        std::size_t deck_bot_games = 0;
        for (const auto& deck_bot : result.deck_bots[deck]) {
            CHECK(deck_bot.games == 20);
            CHECK(deck_bot.wins + deck_bot.losses + deck_bot.draws ==
                  deck_bot.games);
            deck_bot_games += deck_bot.games;
        }
        CHECK(deck_bot_games == result.decks[deck].games);
    }

    const auto repeated =
        old_school::run_tournament(25, 0xDEC1DEULL, {}, bots);
    CHECK(repeated.bots[static_cast<std::size_t>(
              old_school::BotKind::Random)]
              .wins == random.wins);
    CHECK(repeated.bots[static_cast<std::size_t>(
              old_school::BotKind::MonteCarlo)]
              .wins == monte_carlo.wins);
    CHECK(repeated.bots[static_cast<std::size_t>(
              old_school::BotKind::MonteCarlo)]
              .total_rollouts == monte_carlo.total_rollouts);
    CHECK(repeated.bots[static_cast<std::size_t>(
              old_school::BotKind::DeepMonteCarlo)]
              .total_rollouts ==
          deep_monte_carlo.total_rollouts);
    CHECK(repeated.bots[static_cast<std::size_t>(
              old_school::BotKind::Handcrafted)]
              .wins == handcrafted.wins);
    CHECK(repeated.bots[static_cast<std::size_t>(
              old_school::BotKind::Learned)]
              .wins == learned.wins);
    CHECK(repeated.bot_matchups.back().second_wins ==
          result.bot_matchups.back().second_wins);
}

TEST(tournament_threads_and_validates_learned_rollout_budget) {
    old_school::GameConfig game;
    game.learned_model = small_value_model();
    old_school::TournamentConfig tournament = {
        .bot_field = old_school::BotField::Learned,
        .learned_rollouts = 3,
        .learned_training_games = 1,
    };
    const auto result =
        old_school::run_tournament(
            1, 0x1EA4E0ULL, game, tournament);
    const auto& learned =
        result.bots[static_cast<std::size_t>(
            old_school::BotKind::Learned)];
    CHECK(learned.games == 20);
    CHECK(learned.total_decisions > 0);
    CHECK(learned.total_rollouts >=
          learned.total_decisions * tournament.learned_rollouts);

    tournament.learned_rollouts = 0;
    bool rejected_zero = false;
    try {
        static_cast<void>(
            old_school::run_tournament(
                1, 0x1EA4E0ULL, game, tournament));
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    CHECK(rejected_zero);
}

TEST(learned_deck_lift_gate_requires_every_policy_and_allows_ties) {
    old_school::TournamentSummary summary;
    const auto set_record =
        [&](old_school::DeckId deck, old_school::BotKind bot,
            std::size_t wins) {
            auto& stats =
                summary.deck_bots[static_cast<std::size_t>(deck)]
                                  [static_cast<std::size_t>(bot)];
            stats.games = 100;
            stats.wins = wins;
            stats.losses = 100 - wins;
        };

    for (std::size_t deck = 0;
         deck < summary.deck_bots.size(); ++deck) {
        const auto id = static_cast<old_school::DeckId>(deck);
        set_record(id, old_school::BotKind::Random, 20);
        set_record(id, old_school::BotKind::MonteCarlo, 40);
        set_record(id, old_school::BotKind::DeepMonteCarlo, 50);
        set_record(id, old_school::BotKind::Handcrafted, 60);
        set_record(id, old_school::BotKind::Learned, 61);
    }
    set_record(old_school::DeckId::Red, old_school::BotKind::Learned, 60);

    const auto passing =
        old_school::compare_learned_deck_lifts(summary);
    CHECK(passing.decks.size() == 5);
    CHECK(passing.complete());
    CHECK(passing.learned_is_best_on_every_deck());
    CHECK(passing.decks[static_cast<std::size_t>(
              old_school::DeckId::RUAggro)]
              .available);
    const auto& red =
        passing.decks[static_cast<std::size_t>(old_school::DeckId::Red)];
    CHECK(red.learned_lift == red.best_other_lift);
    CHECK(red.learned_is_best);
    CHECK(red.best_other == old_school::BotKind::Handcrafted);

    set_record(old_school::DeckId::Blue, old_school::BotKind::Learned, 59);
    const auto losing =
        old_school::compare_learned_deck_lifts(summary);
    CHECK(losing.complete());
    CHECK(!losing.learned_is_best_on_every_deck());
    CHECK(!losing.decks[static_cast<std::size_t>(
               old_school::DeckId::Blue)]
               .learned_is_best);

    auto& missing =
        summary.deck_bots[static_cast<std::size_t>(
                              old_school::DeckId::RUAggro)]
                         [static_cast<std::size_t>(
                              old_school::BotKind::MonteCarlo)];
    missing = {};
    const auto incomplete =
        old_school::compare_learned_deck_lifts(summary);
    CHECK(!incomplete.complete());
    CHECK(!incomplete.learned_is_best_on_every_deck());
    CHECK(!incomplete.decks[static_cast<std::size_t>(
                   old_school::DeckId::RUAggro)]
                   .available);
}

TEST(bot_benchmark_balances_decks_seats_and_play_draw) {
    const old_school::BotConfig challenger = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::Random,
        .rollouts_per_action = 1,
    };
    const auto result = old_school::run_bot_benchmark(
        2, 0xB07B07ULL, challenger, baseline);

    CHECK(result.total_games == 120);
    CHECK(result.challenger_stats.games == 120);
    CHECK(result.baseline_stats.games == 120);
    CHECK(result.challenger_stats.wins +
              result.challenger_stats.losses +
              result.challenger_stats.draws ==
          120);
    for (std::size_t deck = 0;
         deck < result.challenger_decks.size(); ++deck) {
        CHECK(result.challenger_decks[deck].games == 24);
        CHECK(result.baseline_decks[deck].games == 24);
        CHECK(result.challenger_decks[deck].on_play_games == 12);
        CHECK(result.challenger_decks[deck].on_draw_games == 12);
        CHECK(result.baseline_decks[deck].on_play_games == 12);
        CHECK(result.baseline_decks[deck].on_draw_games == 12);
    }
    CHECK(result.confidence_low_95() >= 0.0);
    CHECK(result.confidence_high_95() <= 100.0);
    CHECK(result.confidence_low_95() <=
          result.challenger_win_rate());
    CHECK(result.confidence_high_95() >=
          result.challenger_win_rate());
}

TEST(benchmark_training_seed_is_independent_and_model_is_reusable) {
    constexpr std::uint64_t kTrainingSeed = 707;
    const old_school::BotConfig learned = {
        .kind = old_school::BotKind::Learned,
        .rollouts_per_action = 0,
        .training_games = 1,
    };
    const old_school::BotConfig random = {
        .kind = old_school::BotKind::Random,
        .rollouts_per_action = 1,
    };
    old_school::GameConfig shared_config;
    CHECK(shared_config.learned_training_seed ==
          old_school::kDefaultLearnedTrainingSeed);
    shared_config.learned_training_seed = kTrainingSeed;
    shared_config.learned_model =
        old_school::train_learned_model(1, kTrainingSeed);

    const auto first = old_school::run_bot_benchmark(
        1, 101, learned, random, shared_config);
    const auto repeated = old_school::run_bot_benchmark(
        1, 101, learned, random, shared_config);
    const auto other_evaluation_seed =
        old_school::run_bot_benchmark(
            1, 424242, learned, random, shared_config);

    CHECK(first.learned_training_seed == kTrainingSeed);
    CHECK(repeated.learned_training_seed == kTrainingSeed);
    CHECK(other_evaluation_seed.learned_training_seed ==
          kTrainingSeed);
    CHECK(first.challenger_stats.wins ==
          repeated.challenger_stats.wins);
    CHECK(first.challenger_stats.losses ==
          repeated.challenger_stats.losses);
    CHECK(first.challenger_stats.draws ==
          repeated.challenger_stats.draws);
}

TEST(benchmark_policy_identity_includes_value_continuation_epsilon) {
    const auto model = small_value_model();
    const old_school::BotConfig greedy = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 0,
        .training_games = 1,
        .learned_model = model,
    };
    old_school::BotConfig exploratory = greedy;
    exploratory.value_continuation_epsilon = 1.0;

    const auto result = old_school::run_bot_benchmark(
        1, 0xE05110AULL, exploratory, greedy);
    CHECK(result.total_games == 60);
    CHECK(result.challenger.value_continuation_epsilon == 1.0);
    CHECK(result.baseline.value_continuation_epsilon == 0.0);

    bool rejected_identical = false;
    try {
        static_cast<void>(old_school::run_bot_benchmark(
            1, 0xE05110AULL, greedy, greedy));
    } catch (const std::invalid_argument&) {
        rejected_identical = true;
    }
    CHECK(rejected_identical);
}

TEST(actor_and_value_champion_use_distinct_frozen_models_in_benchmark) {
    constexpr std::uint64_t kTrainingSeed = 424242;
    const auto actor_model =
        old_school::train_learned_actor_model(1, kTrainingSeed);

    const old_school::BotConfig actor = {
        .kind = old_school::BotKind::Learned,
        .learned_variant = old_school::LearnedVariant::UnifiedActor,
        .rollouts_per_action = 0,
        .training_games = 1,
        .learned_model = actor_model,
    };
    const old_school::BotConfig champion = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .training_games = 1,
    };
    old_school::GameConfig config;
    config.learned_training_seed = kTrainingSeed;
    // A global Actor fallback must not be silently reused for the Champion.
    config.learned_model = actor_model;
    const auto result = old_school::run_bot_benchmark(
        1, 0xAC70C4A6ULL, actor, champion, config);

    CHECK(result.total_games == 60);
    CHECK(result.challenger_stats.games == 60);
    CHECK(result.baseline_stats.games == 60);
    CHECK(result.challenger.learned_model == actor_model);
    CHECK(result.baseline.learned_model);
    CHECK(result.baseline.learned_model != actor_model);
    CHECK(result.challenger_stats.total_rollouts == 0);
    CHECK(result.baseline_stats.total_rollouts > 0);
    for (std::size_t deck = 0;
         deck < result.challenger_decks.size(); ++deck) {
        CHECK(result.challenger_decks[deck].games == 12);
        CHECK(result.baseline_decks[deck].games == 12);
    }

    const auto next_actor_model =
        old_school::train_learned_actor_model(
            1, kTrainingSeed + 1);
    old_school::BotConfig next_actor = actor;
    next_actor.learned_model = next_actor_model;
    const auto generations = old_school::run_bot_benchmark(
        1, 0x6E6E5EEDULL, next_actor, actor);
    CHECK(generations.total_games == 60);
    CHECK(generations.challenger.learned_model ==
          next_actor_model);
    CHECK(generations.baseline.learned_model == actor_model);
}

TEST(handcrafted_bot_beats_monte_carlo_in_seeded_benchmark) {
    const old_school::BotConfig challenger = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::MonteCarlo,
        .rollouts_per_action = 2,
    };
    const auto result = old_school::run_bot_benchmark(
        5, 424242, challenger, baseline);

    CHECK(result.total_games == 300);
    CHECK(result.challenger_win_rate() > 60.0);
    CHECK(result.challenger_is_better_95());
    CHECK(result.challenger_stats.total_rollouts == 0);
    CHECK(result.baseline_stats.total_rollouts > 0);
}

TEST(handcrafted_bot_beats_deep_monte_carlo_in_seeded_benchmark) {
    const old_school::BotConfig challenger = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::DeepMonteCarlo,
        .rollouts_per_action = 8,
    };
    const auto result = old_school::run_bot_benchmark(
        2, 424242, challenger, baseline);

    CHECK(result.total_games == 120);
    CHECK(result.challenger_is_better_95());
    CHECK(result.challenger_stats.total_rollouts == 0);
    CHECK(result.baseline_stats.average_rollouts() > 500.0);
}

TEST(learned_policy_bot_beats_monte_carlo_without_rollouts_or_handcrafted_values) {
    const auto actor_model =
        old_school::train_learned_actor_model(200, 424242);
    const old_school::BotConfig challenger = {
        .kind = old_school::BotKind::Learned,
        .learned_variant = old_school::LearnedVariant::UnifiedActor,
        .rollouts_per_action = 0,
        .training_games = 200,
        .learned_model = actor_model,
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::MonteCarlo,
        .rollouts_per_action = 2,
    };
    const auto result = old_school::run_bot_benchmark(
        5, 424242, challenger, baseline);

    CHECK(result.total_games == 300);
    CHECK(result.challenger_win_rate() > 70.0);
    CHECK(result.challenger_is_better_95());
    CHECK(result.challenger_stats.total_rollouts == 0);
    CHECK(result.baseline_stats.total_rollouts > 0);
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
        old_school::CardId::WaterElemental,
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
        const std::size_t learned =
            static_cast<std::size_t>(matchup.learned_deck);
        CHECK(human < old_school::kDeckCount);
        CHECK(learned < old_school::kDeckCount);
        CHECK(human != learned);
        seen[human][learned] = true;
    }
    for (std::size_t human = 0;
         human < old_school::kDeckCount; ++human) {
        for (std::size_t learned = 0;
             learned < old_school::kDeckCount; ++learned) {
            CHECK(seen[human][learned] ==
                  (human != learned));
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

TEST(all_ten_pairings_complete_a_seeded_smoke_run) {
    const auto result = old_school::run_tournament(10, 424242);
    CHECK(result.total_games == 100);
    CHECK(result.matchups.size() == 10);
    for (const auto& matchup : result.matchups) {
        CHECK(matchup.result.games == 10);
        CHECK(matchup.result.decks[0].wins +
                  matchup.result.decks[1].wins +
                  matchup.result.draws ==
              10);
    }
    for (const auto& deck : result.decks) {
        CHECK(deck.games == 40);
    }
}

TEST(five_deck_random_balance_stays_within_the_declared_band) {
    const auto result = old_school::run_tournament(30'000, 303);
    CHECK(result.total_games == 300'000);
    for (const auto& matchup : result.matchups) {
        CHECK(matchup.result.draws == 0);
        const double first_rate =
            matchup.result.decks[0].win_rate();
        const double second_rate =
            matchup.result.decks[1].win_rate();
        CHECK(first_rate >= 30.0);
        CHECK(first_rate <= 70.0);
        CHECK(second_rate >= 30.0);
        CHECK(second_rate <= 70.0);
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
