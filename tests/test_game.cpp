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

DeterminizationFixture ancestral_target_fixture() {
    DeterminizationFixture fixture{
        .state = {},
        .decks = {
            old_school::blue_deck(),
            old_school::red_deck(),
        },
    };
    auto& state = fixture.state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 6;
    state.next_stack_object_id = 1;
    state.players[0].hand = {
        old_school::CardId::AncestralRecall,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
    };
    state.players[0].library = fixture.decks[0];
    remove_fixture_card(
        state.players[0].library,
        old_school::CardId::AncestralRecall);
    remove_fixture_card(
        state.players[0].library,
        old_school::CardId::Island);

    state.players[1].hand = {
        old_school::CardId::LightningBolt,
        old_school::CardId::Mountain,
    };
    state.players[1].library = fixture.decks[1];
    remove_fixture_card(
        state.players[1].library,
        old_school::CardId::LightningBolt);
    remove_fixture_card(
        state.players[1].library,
        old_school::CardId::Mountain);
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

std::shared_ptr<const old_school::LearnedModel>
context_sensitive_value_model() {
    static const auto model = [] {
        const auto contextual =
            old_school::with_learned_decision_context(
                small_value_model());
        const auto state =
            old_school::white_lock_plan_diagnostic_state();
        const auto state_features =
            old_school::learned_observation(state, 0);
        std::vector<
            old_school::LearnedContextualCriticTrainingExample>
            examples;
        for (std::size_t phase = 0; phase < 7; ++phase) {
            for (std::size_t decision_player = 0;
                 decision_player < 2; ++decision_player) {
                for (int passes = 0; passes < 2; ++passes) {
                    for (std::size_t sorcery = 0;
                         sorcery < 2; ++sorcery) {
                        const old_school::LearnedDecisionContext
                            context = {
                                .valid = true,
                                .phase =
                                    static_cast<
                                        old_school::TurnPhase>(
                                        phase),
                                .decision_player =
                                    decision_player,
                                .consecutive_passes = passes,
                                .sorcery_actions =
                                    sorcery != 0,
                            };
                        const double target =
                            0.1 +
                            0.05 *
                                static_cast<double>(phase) +
                            0.15 *
                                static_cast<double>(
                                    decision_player) +
                            0.2 *
                                static_cast<double>(passes) +
                            0.1 *
                                static_cast<double>(sorcery);
                        for (std::size_t repeat = 0;
                             repeat < 8; ++repeat) {
                            examples.push_back({
                                .features = state_features,
                                .context_features =
                                    old_school::
                                        learned_decision_context_features(
                                            context, 0),
                                .target = target,
                            });
                        }
                    }
                }
            }
        }
        return old_school::
            update_learned_contextual_value_model(
                contextual, examples,
                {
                    .epochs = 14,
                    .learning_rate = 0.012,
                    .root_seed = 0xC07E57A11ULL,
                    .member_training_tag = 0x1A7E6A7EULL,
                });
    }();
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

const old_school::LearnedTerminalWeightC17Artifact&
small_terminal_weight_family() {
    static const auto artifact = [] {
        old_school::LearnedTerminalWeightC17Config config;
        config.training_games = 1;
        config.parent_training_seed = 424242;
        config.parent_generations = 2;
        config.shard_seed = 0x7A17C17ULL;
        config.balanced_blocks = 1;
        config.max_game_turns = 8;
        config.required_parent_fingerprint =
            "8b9696870ca43087cddb3987a3d80759ac0528b552f1ead5447091d526cf2e06";
        return old_school::
            train_learned_terminal_weight_c17_family(
                std::move(config));
    }();
    return artifact;
}

const old_school::LearnedJointC17Artifact&
small_joint_c17_family() {
    static const auto artifact = [] {
        old_school::LearnedJointC17Config config;
        config.training_games = 1;
        config.parent_training_seed = 424242;
        config.parent_generations = 2;
        config.shard_seed = 0x7A17C18ULL;
        config.balanced_blocks = 1;
        config.max_game_turns = 12;
        config.required_parent_fingerprint =
            "8b9696870ca43087cddb3987a3d80759ac0528b552f1ead5447091d526cf2e06";
        return old_school::train_learned_joint_c17_family(
            std::move(config));
    }();
    return artifact;
}

const old_school::LearnedValueContextChallengerArtifact&
small_value_context_challenger_c1_artifact() {
    static const auto artifact =
        old_school::
            train_learned_value_context_challenger_artifact(
                1, 0xC07E6751ULL, 1);
    return artifact;
}

const old_school::LearnedValueDenseContextChallengerArtifact&
small_value_dense_context_d0_c1_artifact() {
    static const auto artifact =
        old_school::
            train_learned_value_dense_context_challenger_artifact(
                1, 0xD305E001ULL, 1,
                old_school::LearnedValueDenseContextTreatment::
                    ContextMasked);
    return artifact;
}

const old_school::LearnedValueDenseContextChallengerArtifact&
small_value_dense_context_d1_c1_artifact() {
    static const auto artifact =
        old_school::
            train_learned_value_dense_context_challenger_artifact(
                1, 0xD305E001ULL, 1,
                old_school::LearnedValueDenseContextTreatment::
                    ContextLive);
    return artifact;
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

DeterminizationFixture block_evaluation_fixture() {
    DeterminizationFixture fixture{
        .state = {},
        .decks = {
            old_school::blue_deck(),
            old_school::blue_deck(),
        },
    };
    auto& state = fixture.state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 11;
    state.players[0].land_played_this_turn = true;
    state.players[0].lands.assign(
        5, old_school::LandPermanent{
               .card = old_school::CardId::Island,
               .tapped = false,
           });
    state.players[0].creatures = {
        creature(1, old_school::CardId::AirElemental),
    };
    state.players[0].creatures[0].tapped = true;
    state.players[1].lands.assign(
        5, old_school::LandPermanent{
               .card = old_school::CardId::Island,
               .tapped = false,
           });
    state.players[1].creatures = {
        creature(2, old_school::CardId::FlyingMen),
    };

    for (std::size_t player = 0; player < fixture.decks.size();
         ++player) {
        std::vector<old_school::CardId> hidden =
            fixture.decks[player];
        for (const auto& land : state.players[player].lands) {
            remove_fixture_card(hidden, land.card);
        }
        for (const auto& permanent :
             state.players[player].creatures) {
            remove_fixture_card(hidden, permanent.card);
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

TEST(learned_search_seed_domains_are_stable) {
    struct SeedAnchor {
        std::uint64_t root;
        std::size_t world;
        std::uint64_t world_seed;
        std::uint64_t continuation_zero;
        std::uint64_t continuation_three;
    };
    constexpr std::array<SeedAnchor, 4> anchors{{
        {
            0,
            0,
            0x5cf7ccc2fc86ea16ULL,
            0xf62870961178f3beULL,
            0x23b2b09d8535c736ULL,
        },
        {
            0,
            7,
            0xf51aebf2552c966bULL,
            0x0180cd04745f0b4eULL,
            0xea037ebedbe02897ULL,
        },
        {
            1,
            1,
            0x64189955455f43b0ULL,
            0xe742d5eb9d1136a1ULL,
            0x084d1cd8e86ff007ULL,
        },
        {
            0x0123456789abcdefULL,
            7,
            0x3e1e735800096f8cULL,
            0x76f51d23816d7291ULL,
            0x06d5687c8766f53dULL,
        },
    }};
    for (const SeedAnchor& anchor : anchors) {
        CHECK(old_school::learned_search_world_seed(
                  anchor.root, anchor.world) ==
              anchor.world_seed);
        CHECK(old_school::learned_search_continuation_seed(
                  anchor.root, anchor.world, 0) ==
              anchor.continuation_zero);
        CHECK(old_school::learned_search_continuation_seed(
                  anchor.root, anchor.world, 3) ==
              anchor.continuation_three);
        CHECK(anchor.world_seed != anchor.continuation_zero);
    }
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

    auto mana_changed = state;
    mana_changed.players[0].mana_pool.blue = 1;
    CHECK(old_school::learned_observation(mana_changed, 0) !=
          baseline_observation);
    CHECK(old_school::learned_priority_policy_features(
              mana_changed, 0, old_school::PriorityAction::pass(), true,
              old_school::TurnPhase::FirstMain, 0) != baseline_policy);

    auto extra_turn_changed = state;
    extra_turn_changed.extra_turns_pending[0] = 1;
    CHECK(old_school::learned_observation(extra_turn_changed, 0) !=
          baseline_observation);
    CHECK(old_school::learned_priority_policy_features(
              extra_turn_changed, 0,
              old_school::PriorityAction::pass(), true,
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

TEST(value_context_alias_audit_proves_missing_markov_context) {
    const auto diagnostic =
        old_school::diagnose_value_context_aliases();

    CHECK(diagnostic.root_player == 0);
    CHECK(diagnostic.perspective == 0);
    CHECK(diagnostic.stack_action_count == 2);
    CHECK(diagnostic.stack_actions_identical);
    CHECK(diagnostic.stack_critic_features_bit_identical);
    CHECK(diagnostic.stack_policy_features_different);

    CHECK(diagnostic.zero_pass_result ==
          old_school::PriorityPassResult::Passed);
    CHECK(diagnostic.zero_pass_next_player == 1);
    CHECK(diagnostic.zero_pass_next_count == 1);
    CHECK(diagnostic.zero_pass_stack_size == 1);
    CHECK(diagnostic.zero_pass_life == 3);

    CHECK(diagnostic.one_pass_result ==
          old_school::PriorityPassResult::StackObjectResolved);
    CHECK(diagnostic.one_pass_next_player == 0);
    CHECK(diagnostic.one_pass_next_count == 0);
    CHECK(diagnostic.one_pass_stack_size == 0);
    CHECK(diagnostic.one_pass_life == 0);

    CHECK(diagnostic.main_action_count == 2);
    CHECK(diagnostic.main_actions_identical);
    CHECK(diagnostic.main_critic_features_bit_identical);
    CHECK(diagnostic.main_policy_features_different);
    CHECK(diagnostic.hidden_information_bit_identical);
    CHECK(diagnostic.demonstrated());
}

TEST(decision_context_encoding_is_exact_and_hidden_safe) {
    static_assert(
        old_school::kLearnedDecisionContextFeatureCount == 13);
    const old_school::LearnedDecisionContext pass_zero = {
        .valid = true,
        .phase = old_school::TurnPhase::BeginCombat,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = false,
    };
    auto pass_one = pass_zero;
    pass_one.consecutive_passes = 1;
    const old_school::LearnedDecisionContext first_main = {
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    auto second_main = first_main;
    second_main.phase = old_school::TurnPhase::SecondMain;
    auto response_in_first_main = first_main;
    response_in_first_main.sorcery_actions = false;

    const auto zero_features =
        old_school::learned_decision_context_features(
            pass_zero, 0);
    const auto one_features =
        old_school::learned_decision_context_features(
            pass_one, 0);
    const auto opponent_features =
        old_school::learned_decision_context_features(
            pass_zero, 1);
    CHECK(zero_features[0] == 1.0);
    CHECK(zero_features[1 +
                        static_cast<std::size_t>(
                            old_school::TurnPhase::BeginCombat)] ==
          1.0);
    CHECK(zero_features[8] == 1.0);
    CHECK(zero_features[9] == 0.0);
    CHECK(opponent_features[8] == 0.0);
    CHECK(opponent_features[9] == 1.0);
    CHECK(zero_features[10] == 1.0);
    CHECK(zero_features[11] == 0.0);
    CHECK(one_features[10] == 0.0);
    CHECK(one_features[11] == 1.0);
    CHECK(zero_features[12] == 0.0);
    CHECK(zero_features != one_features);
    CHECK(old_school::learned_decision_context_features(
              first_main, 0) !=
          old_school::learned_decision_context_features(
              second_main, 0));
    CHECK(old_school::learned_decision_context_features(
              first_main, 0) !=
          old_school::learned_decision_context_features(
              response_in_first_main, 0));

    old_school::LearnedDecisionContext masked;
    masked.phase = old_school::TurnPhase::DamageOrder;
    masked.decision_player = 999;
    masked.consecutive_passes = 999;
    masked.sorcery_actions = true;
    const auto masked_features =
        old_school::learned_decision_context_features(masked, 0);
    CHECK(std::all_of(
        masked_features.begin(), masked_features.end(),
        [](double value) { return value == 0.0; }));

    const auto fixture = determinization_fixture();
    const auto hidden =
        hidden_repartition(fixture.state, 0);
    const auto base =
        old_school::learned_observation(fixture.state, 0);
    const auto contextual =
        old_school::learned_contextual_observation(
            fixture.state, 0, pass_one);
    CHECK(contextual.size() ==
          base.size() +
              old_school::kLearnedDecisionContextFeatureCount);
    CHECK(old_school::learned_contextual_observation(
              hidden, 0, pass_one) == contextual);
    CHECK(old_school::learned_contextual_observation(
              fixture.state, 0, pass_zero) != contextual);
}

TEST(contextual_critic_zero_columns_preserve_legacy_exactly) {
    const auto legacy = small_value_model();
    const std::string legacy_fingerprint =
        old_school::learned_model_fingerprint(legacy);
    const auto contextual =
        old_school::with_learned_decision_context(legacy);
    const auto repeated =
        old_school::with_learned_decision_context(legacy);
    CHECK(old_school::learned_critic_schema(legacy) ==
          old_school::LearnedCriticSchema::LegacyStateOnly);
    CHECK(old_school::learned_critic_schema(contextual) ==
          old_school::LearnedCriticSchema::DecisionContextV1);
    CHECK(old_school::learned_model_fingerprint(legacy) ==
          legacy_fingerprint);
    CHECK(old_school::learned_model_fingerprint(contextual) ==
          old_school::learned_model_fingerprint(repeated));
    CHECK(old_school::learned_model_fingerprint(contextual) !=
          legacy_fingerprint);

    const auto fixture = determinization_fixture();
    const old_school::LearnedDecisionContext pass_zero = {
        .valid = true,
        .phase = old_school::TurnPhase::SecondMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    auto pass_one = pass_zero;
    pass_one.consecutive_passes = 1;
    for (std::size_t perspective = 0; perspective < 2;
         ++perspective) {
        const double legacy_value =
            old_school::learned_critic_value(
                fixture.state, perspective, legacy);
        CHECK(old_school::learned_contextual_critic_value(
                  fixture.state, perspective, pass_zero,
                  legacy) == legacy_value);
        CHECK(old_school::learned_contextual_critic_value(
                  fixture.state, perspective, pass_zero,
                  contextual) == legacy_value);
        CHECK(old_school::learned_contextual_critic_value(
                  fixture.state, perspective, pass_one,
                  contextual) == legacy_value);
    }
}

TEST(contextual_critic_training_can_fit_distinct_pass_contexts) {
    const auto legacy = small_value_model();
    const auto contextual =
        old_school::with_learned_decision_context(legacy);
    const auto fixture = determinization_fixture();
    const old_school::LearnedDecisionContext pass_zero = {
        .valid = true,
        .phase = old_school::TurnPhase::BeginCombat,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = false,
    };
    auto pass_one = pass_zero;
    pass_one.consecutive_passes = 1;
    const auto state_features =
        old_school::learned_observation(fixture.state, 0);
    const auto pass_zero_features =
        old_school::learned_decision_context_features(
            pass_zero, 0);
    const auto pass_one_features =
        old_school::learned_decision_context_features(
            pass_one, 0);
    std::vector<old_school::LearnedContextualCriticTrainingExample>
        examples;
    for (std::size_t repeat_index = 0; repeat_index < 32;
         ++repeat_index) {
        examples.push_back({
            .features = state_features,
            .context_features = pass_zero_features,
            .target = 0.0,
        });
        examples.push_back({
            .features = state_features,
            .context_features = pass_one_features,
            .target = 1.0,
        });
    }
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(contextual);
    const auto trained =
        old_school::update_learned_contextual_value_model(
            contextual, examples,
            {
                .epochs = 12,
                .learning_rate = 0.02,
                .root_seed = 0xC07E570ULL,
                .member_training_tag = 0x7A61ULL,
            });
    const double pass_zero_value =
        old_school::learned_contextual_critic_value(
            fixture.state, 0, pass_zero, trained);
    const double pass_one_value =
        old_school::learned_contextual_critic_value(
            fixture.state, 0, pass_one, trained);
    CHECK(pass_one_value > pass_zero_value);
    CHECK(pass_one_value - pass_zero_value > 0.01);
    CHECK(old_school::learned_model_fingerprint(trained) !=
          parent_fingerprint);
    CHECK(old_school::learned_model_fingerprint(contextual) ==
          parent_fingerprint);
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
    CHECK(learned.value_priority_residual_weight == 0.0);
    CHECK(!learned.value_adversarial_blocks);
    CHECK(learned.value_continuation_controller ==
          old_school::LearnedContinuationController::Legacy);
    CHECK(old_school::LearnedSearchConfig{}
              .value_priority_residual_weight == 0.0);
    CHECK(!old_school::LearnedSearchConfig{}
               .value_pass_dominance);
    CHECK(old_school::LearnedSearchConfig{}
              .value_continuation_controller ==
          old_school::LearnedContinuationController::Legacy);
    CHECK(old_school::bot_config_name(learned) == "Learned Value");

    const old_school::BotConfig actor = {
        .kind = old_school::BotKind::Learned,
        .learned_variant = old_school::LearnedVariant::UnifiedActor,
    };
    CHECK(old_school::bot_config_name(actor) == "Learned Actor");
}

TEST(value_continuation_controller_rejects_every_non_value_policy) {
    const std::array<old_school::BotConfig, 5> invalid = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Random,
            .rollouts_per_action = 1,
            .value_continuation_controller =
                old_school::LearnedContinuationController::
                    PublicStackPassV1,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::MonteCarlo,
            .rollouts_per_action = 1,
            .value_continuation_controller =
                old_school::LearnedContinuationController::
                    PublicStackPassV1,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::DeepMonteCarlo,
            .rollouts_per_action = 1,
            .value_continuation_controller =
                old_school::LearnedContinuationController::
                    PublicStackPassV1,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Handcrafted,
            .rollouts_per_action = 1,
            .value_continuation_controller =
                old_school::LearnedContinuationController::
                    PublicStackPassV1,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .value_continuation_controller =
                old_school::LearnedContinuationController::
                    PublicStackPassV1,
            .learned_model = small_actor_model(),
        },
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    for (const auto& challenger : invalid) {
        CHECK(throws_with_text(
            [&] {
                static_cast<void>(
                    old_school::run_bot_benchmark(
                        1, 0xBAD0C017ULL,
                        challenger, baseline));
            },
            "requires Learned Value"));
    }

    old_school::GameState state;
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::green_deck(),
        old_school::red_deck(),
    };
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_priority_action_samples(
                    state, decks, 0, true,
                    old_school::TurnPhase::FirstMain, 0,
                    {old_school::PriorityAction::pass()},
                    small_actor_model(),
                    {
                        .seed = 1,
                        .continuation_variant =
                            old_school::LearnedVariant::
                                UnifiedActor,
                        .value_continuation_controller =
                            old_school::
                                LearnedContinuationController::
                                    PublicStackPassV1,
                    }));
        },
        "requires a Value-mirror search"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_priority_action_samples(
                    state, decks, 0, true,
                    old_school::TurnPhase::FirstMain, 0,
                    {old_school::PriorityAction::pass()},
                    small_actor_model(),
                    {
                        .seed = 2,
                        .continuation_variant =
                            old_school::LearnedVariant::
                                UnifiedActor,
                        .value_pass_dominance = true,
                    }));
        },
        "requires a Value-mirror search"));
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

TEST(value_priority_residual_p0_and_uniform_head_are_exact_identity) {
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto model = small_value_model();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() > 1);

    const auto zero_residual =
        old_school::diagnose_learned_value_priority_residual(
            state, 0, true, old_school::TurnPhase::FirstMain, 1,
            actions, model, 0.0);
    const auto uniform_residual =
        old_school::diagnose_learned_value_priority_residual(
            state, 0, true, old_school::TurnPhase::FirstMain, 1,
            actions, model, 0.10);
    CHECK(zero_residual.policy_logits ==
          uniform_residual.policy_logits);
    CHECK(std::all_of(
        uniform_residual.policy_logits.begin(),
        uniform_residual.policy_logits.end(),
        [](double value) { return value == 0.0; }));
    CHECK(uniform_residual.mean_legal_logit == 0.0);
    CHECK(std::all_of(
        uniform_residual.centered_policy_logits.begin(),
        uniform_residual.centered_policy_logits.end(),
        [](double value) { return value == 0.0; }));
    CHECK(std::all_of(
        zero_residual.residuals.begin(),
        zero_residual.residuals.end(),
        [](double value) { return value == 0.0; }));
    CHECK(zero_residual.residuals ==
          uniform_residual.residuals);

    constexpr std::uint64_t kSeed = 0xA11CE050ULL;
    const auto p0 =
        old_school::diagnose_learned_value_priority(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, 0,
            kSeed);
    const auto explicit_zero =
        old_school::diagnose_learned_value_priority(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, 0,
            kSeed, 0.0, 0.0);
    const auto uniform_weighted =
        old_school::diagnose_learned_value_priority(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, 0,
            kSeed, 0.0, 0.10);
    CHECK(p0.actions == explicit_zero.actions);
    CHECK(p0.actions == uniform_weighted.actions);
    CHECK(p0.base_scores == explicit_zero.base_scores);
    CHECK(p0.base_scores == uniform_weighted.base_scores);
    CHECK(p0.scores == explicit_zero.scores);
    CHECK(p0.scores == uniform_weighted.scores);
    CHECK(p0.policy_logits == uniform_weighted.policy_logits);
    CHECK(p0.centered_policy_logits ==
          uniform_weighted.centered_policy_logits);
    CHECK(p0.priority_residuals ==
          uniform_weighted.priority_residuals);

    const auto hidden = hidden_repartition(state, 0);
    const auto hidden_residual =
        old_school::diagnose_learned_value_priority_residual(
            hidden, 0, true, old_school::TurnPhase::FirstMain, 1,
            actions, model, 0.10);
    CHECK(hidden_residual.policy_logits ==
          uniform_residual.policy_logits);
    CHECK(hidden_residual.centered_policy_logits ==
          uniform_residual.centered_policy_logits);
    CHECK(hidden_residual.residuals ==
          uniform_residual.residuals);

    old_school::LearnedSearchConfig search = {
        .seed = kSeed,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = true,
    };
    const auto p0_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, actions, model,
            search);
    search.value_priority_residual_weight = 0.10;
    const auto uniform_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, actions, model,
            search);
    CHECK(p0_samples.q_samples == uniform_samples.q_samples);
    CHECK(p0_samples.rollout_evaluations ==
          uniform_samples.rollout_evaluations);

    old_school::GameConfig game_config;
    game_config.max_turns = 5;
    game_config.starting_player = 0;
    game_config.learned_model = model;
    game_config.learned_search_depth = 0;
    game_config.bots = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .rollouts_per_action = 0,
            .learned_model = model,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .rollouts_per_action = 0,
            .learned_model = model,
        },
    };
    old_school::Game p0_game(
        old_school::green_deck(), old_school::red_deck(),
        kSeed, game_config);
    const auto p0_record = p0_game.run();
    for (auto& bot : game_config.bots) {
        bot.value_priority_residual_weight = 0.10;
    }
    old_school::Game uniform_game(
        old_school::green_deck(), old_school::red_deck(),
        kSeed, game_config);
    CHECK(uniform_game.run() == p0_record);
}

TEST(value_priority_residual_rejects_invalid_or_non_value_use) {
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
        .rollouts_per_action = 0,
        .value_priority_residual_weight =
            std::numeric_limits<double>::quiet_NaN(),
        .learned_model = small_value_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 0,
        .value_priority_residual_weight = -0.01,
        .learned_model = small_value_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 0,
        .value_priority_residual_weight = 1.01,
        .learned_model = small_value_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::UnifiedActor,
        .rollouts_per_action = 0,
        .value_priority_residual_weight = 0.10,
        .learned_model = small_actor_model(),
    }));
    CHECK(rejects_game_config({
        .kind = old_school::BotKind::Random,
        .rollouts_per_action = 1,
        .value_priority_residual_weight = 0.10,
    }));

    const auto state =
        old_school::white_lock_plan_diagnostic_state();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    diagnose_learned_value_priority_residual(
                        state, 0, true,
                        old_school::TurnPhase::FirstMain, 1,
                        actions, small_value_model(), -0.01));
        },
        "residual weight"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    diagnose_learned_value_priority_residual(
                        state, 0, true,
                        old_school::TurnPhase::FirstMain, 1,
                        actions, small_actor_model(), 0.10));
        },
        "variant"));

    old_school::LearnedSearchConfig actor_search = {
        .seed = 1,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .value_priority_residual_weight = 0.10,
    };
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_priority_action_samples(
                    state,
                    {
                        old_school::white_control_deck(),
                        old_school::red_deck(),
                    },
                    0, true, old_school::TurnPhase::FirstMain, 1,
                    actions, small_actor_model(), actor_search));
        },
        "Value-mirror"));
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
    CHECK(baseline.exact_priority_aggregate_scores.size() ==
          actions.size());
    for (const auto& samples : baseline.q_samples) {
        CHECK(samples.size() == 4);
        CHECK(std::all_of(
            samples.begin(), samples.end(),
            [](double value) {
                return std::isfinite(value) &&
                       value >= 0.0 && value <= 1.0;
            }));
    }
    CHECK(std::all_of(
        baseline.exact_priority_aggregate_scores.begin(),
        baseline.exact_priority_aggregate_scores.end(),
        [](double value) {
            return std::isfinite(value) &&
                   value >= 0.0 && value <= 1.0;
        }));

    const old_school::GameState hidden =
        hidden_repartition(state, 0);
    const auto repeated =
        old_school::learned_priority_action_samples(
            hidden, decks, 0, true, old_school::TurnPhase::FirstMain,
            0, actions, model, config);
    CHECK(repeated.q_samples == baseline.q_samples);
    CHECK(repeated.exact_priority_aggregate_scores ==
          baseline.exact_priority_aggregate_scores);
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

    old_school::LearnedSearchConfig value_greedy = {
        .seed = 0xE05110AULL,
        .worlds = 2,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = false,
    };
    const auto greedy_value_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            small_value_model(), value_greedy);
    old_school::LearnedSearchConfig value_exploratory =
        value_greedy;
    value_exploratory.value_continuation_epsilon = 1.0;
    const auto exploratory_value_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            small_value_model(), value_exploratory);
    const auto exploratory_value_repeated =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            small_value_model(), value_exploratory);
    const auto exploratory_value_hidden =
        old_school::learned_priority_action_samples(
            hidden, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            small_value_model(), value_exploratory);
    CHECK(exploratory_value_samples.q_samples ==
          exploratory_value_repeated.q_samples);
    CHECK(exploratory_value_samples.q_samples ==
          exploratory_value_hidden.q_samples);
    CHECK(exploratory_value_samples.sampled_worlds ==
          greedy_value_samples.sampled_worlds);
    CHECK(exploratory_value_samples.rollout_evaluations ==
          greedy_value_samples.rollout_evaluations);
    CHECK(exploratory_value_samples.q_samples !=
          greedy_value_samples.q_samples);

    auto reordered_actions = actions;
    std::reverse(reordered_actions.begin(), reordered_actions.end());
    const auto reordered =
        old_school::learned_priority_action_samples(
            state, decks, 0, true, old_school::TurnPhase::FirstMain,
            0, reordered_actions, model, config);
    for (std::size_t index = 0; index < actions.size(); ++index) {
        CHECK(reordered.q_samples[index] ==
              baseline.q_samples[actions.size() - index - 1]);
        CHECK(
            std::bit_cast<std::uint64_t>(
                reordered.exact_priority_aggregate_scores[index]) ==
            std::bit_cast<std::uint64_t>(
                baseline.exact_priority_aggregate_scores[
                    actions.size() - index - 1]));
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

TEST(priority_sample_trace_reconstructs_deployed_arithmetic_bit_exactly) {
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    const old_school::LearnedSearchConfig config = {
        .seed = 0x4651345452414345ULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = true,
        .evaluation_threads = 4,
    };
    const auto samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            small_value_model(), config);
    const std::size_t samples_per_action =
        config.worlds * config.rollouts_per_world;
    CHECK(samples.q_samples.size() == actions.size());
    CHECK(samples.priority_shallow_prior_samples.size() ==
          actions.size());
    CHECK(samples.priority_continuation_samples.size() ==
          actions.size());
    CHECK(samples.exact_priority_aggregate_scores.size() ==
          actions.size());
    for (std::size_t action = 0; action < actions.size();
         ++action) {
        CHECK(samples.q_samples[action].size() ==
              samples_per_action);
        CHECK(samples.priority_shallow_prior_samples[action]
                  .size() == samples_per_action);
        CHECK(samples.priority_continuation_samples[action]
                  .size() == samples_per_action);
        double reconstructed_aggregate = 0.0;
        for (std::size_t sample = 0;
             sample < samples_per_action; ++sample) {
            const double shallow =
                samples.priority_shallow_prior_samples[action]
                                                       [sample];
            const double continuation =
                samples.priority_continuation_samples[action]
                                                    [sample];
            const double continuation_weight =
                static_cast<double>(samples_per_action);
            const double reconstructed_q =
                (shallow +
                 continuation_weight * continuation) /
                (continuation_weight + 1.0);
            CHECK(std::bit_cast<std::uint64_t>(
                      reconstructed_q) ==
                  std::bit_cast<std::uint64_t>(
                      samples.q_samples[action][sample]));
            reconstructed_aggregate += shallow;
        }
        reconstructed_aggregate /=
            static_cast<double>(samples_per_action);
        for (const double continuation :
             samples.priority_continuation_samples[action]) {
            reconstructed_aggregate += continuation;
        }
        reconstructed_aggregate /=
            static_cast<double>(samples_per_action + 1);
        CHECK(std::bit_cast<std::uint64_t>(
                  reconstructed_aggregate) ==
              std::bit_cast<std::uint64_t>(
                  samples.exact_priority_aggregate_scores[
                      action]));
    }

    auto serial_config = config;
    serial_config.evaluation_threads = 1;
    const auto serial =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            small_value_model(), serial_config);
    CHECK(serial.q_samples == samples.q_samples);
    CHECK(serial.priority_shallow_prior_samples ==
          samples.priority_shallow_prior_samples);
    CHECK(serial.priority_continuation_samples ==
          samples.priority_continuation_samples);
    CHECK(serial.exact_priority_aggregate_scores ==
          samples.exact_priority_aggregate_scores);
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

    const auto components =
        old_school::learned_model_component_fingerprints(actor);
    CHECK(components ==
          old_school::learned_model_component_fingerprints(repeated));
    CHECK(components.critic.size() == 64);
    CHECK(components.priority.size() == 64);
    CHECK(components.attack.size() == 64);
    CHECK(components.block.size() == 64);
    CHECK(components.damage_order.size() == 64);
    CHECK(old_school::learned_model_component_fingerprints(changed) !=
          components);

    bool rejected_null = false;
    try {
        static_cast<void>(
            old_school::learned_model_fingerprint(nullptr));
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    CHECK(rejected_null);
    rejected_null = false;
    try {
        static_cast<void>(
            old_school::learned_model_component_fingerprints(
                nullptr));
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    CHECK(rejected_null);
}

TEST(learned_priority_head_parameters_round_trip_bit_exactly_and_isolate) {
    const auto parent = small_value_model();
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const auto parent_components =
        old_school::learned_model_component_fingerprints(parent);
    const auto parameters =
        old_school::learned_priority_head_parameters(parent);

    CHECK(!parameters.input_hidden.empty());
    CHECK(parameters.input_hidden.size() ==
          parameters.hidden_bias.size());
    CHECK(parameters.input_hidden.size() ==
          parameters.hidden_output.size());
    CHECK(!parameters.direct.empty());
    CHECK(std::all_of(
        parameters.input_hidden.begin(),
        parameters.input_hidden.end(),
        [&](const auto& row) {
            return row.size() == parameters.direct.size();
        }));

    const auto round_trip =
        old_school::with_learned_priority_head_parameters(
            parent, parameters);
    CHECK(round_trip.get() != parent.get());
    CHECK(old_school::learned_model_fingerprint(round_trip) ==
          parent_fingerprint);
    CHECK(old_school::learned_model_component_fingerprints(
              round_trip) == parent_components);
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);

    auto changed_parameters = parameters;
    changed_parameters.input_hidden[0][0] =
        std::bit_cast<double>(UINT64_C(0x3fd123456789abcd));
    changed_parameters.hidden_bias[0] =
        std::bit_cast<double>(UINT64_C(0xbfc23456789abcde));
    changed_parameters.hidden_output[0] =
        std::bit_cast<double>(UINT64_C(0x0010000000000001));
    changed_parameters.direct[0] =
        std::bit_cast<double>(UINT64_C(0x8000000000000000));
    changed_parameters.output_bias =
        std::bit_cast<double>(UINT64_C(0x3fe3456789abcdef));

    const auto changed =
        old_school::with_learned_priority_head_parameters(
            parent, changed_parameters);
    const auto exported_changed =
        old_school::learned_priority_head_parameters(changed);
    const auto same_bits = [](double left, double right) {
        return std::bit_cast<std::uint64_t>(left) ==
               std::bit_cast<std::uint64_t>(right);
    };
    CHECK(same_bits(
        exported_changed.input_hidden[0][0],
        changed_parameters.input_hidden[0][0]));
    CHECK(same_bits(
        exported_changed.hidden_bias[0],
        changed_parameters.hidden_bias[0]));
    CHECK(same_bits(
        exported_changed.hidden_output[0],
        changed_parameters.hidden_output[0]));
    CHECK(same_bits(
        exported_changed.direct[0],
        changed_parameters.direct[0]));
    CHECK(same_bits(
        exported_changed.output_bias,
        changed_parameters.output_bias));

    const auto changed_components =
        old_school::learned_model_component_fingerprints(changed);
    CHECK(old_school::learned_model_fingerprint(changed) !=
          parent_fingerprint);
    CHECK(changed_components.priority !=
          parent_components.priority);
    CHECK(changed_components.critic ==
          parent_components.critic);
    CHECK(changed_components.attack ==
          parent_components.attack);
    CHECK(changed_components.block ==
          parent_components.block);
    CHECK(changed_components.damage_order ==
          parent_components.damage_order);
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(old_school::learned_model_component_fingerprints(parent) ==
          parent_components);
}

TEST(learned_priority_head_parameters_reject_malformed_or_nonfinite_input) {
    const auto parent = small_value_model();
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const auto valid =
        old_school::learned_priority_head_parameters(parent);
    const auto rejected =
        [&](old_school::LearnedPriorityHeadParameters parameters) {
            try {
                static_cast<void>(
                    old_school::
                        with_learned_priority_head_parameters(
                            parent, parameters));
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };

    auto malformed = valid;
    malformed.input_hidden.pop_back();
    CHECK(rejected(malformed));
    malformed = valid;
    malformed.input_hidden[0].pop_back();
    CHECK(rejected(malformed));
    malformed = valid;
    malformed.hidden_bias.push_back(0.0);
    CHECK(rejected(malformed));
    malformed = valid;
    malformed.hidden_output.pop_back();
    CHECK(rejected(malformed));
    malformed = valid;
    malformed.direct.push_back(0.0);
    CHECK(rejected(malformed));

    auto nonfinite = valid;
    nonfinite.input_hidden[0][0] =
        std::numeric_limits<double>::quiet_NaN();
    CHECK(rejected(nonfinite));
    nonfinite = valid;
    nonfinite.hidden_bias[0] =
        std::numeric_limits<double>::infinity();
    CHECK(rejected(nonfinite));
    nonfinite = valid;
    nonfinite.hidden_output[0] =
        -std::numeric_limits<double>::infinity();
    CHECK(rejected(nonfinite));
    nonfinite = valid;
    nonfinite.direct[0] =
        std::numeric_limits<double>::quiet_NaN();
    CHECK(rejected(nonfinite));
    nonfinite = valid;
    nonfinite.output_bias =
        std::numeric_limits<double>::infinity();
    CHECK(rejected(nonfinite));

    bool rejected_null = false;
    try {
        static_cast<void>(
            old_school::learned_priority_head_parameters(
                nullptr));
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    CHECK(rejected_null);
    rejected_null = false;
    try {
        static_cast<void>(
            old_school::with_learned_priority_head_parameters(
                nullptr, valid));
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    CHECK(rejected_null);
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
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
          "2dff8887e931364973c02bfc6fc6b196908ad834232b4dd923012d44036f65af");
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
    // Captured from the inline distance-four production target path before
    // it was routed through n_state_bootstrap_targets.
    CHECK(old_school::learned_model_fingerprint(generation_two) ==
          "8b9696870ca43087cddb3987a3d80759ac0528b552f1ead5447091d526cf2e06");
    const old_school::LearnedModelComponentFingerprints
        expected_components = {
            .critic =
                "0f571aab9098cb25613fd1d9e06496cad9248d4520d035a3cd1540237d50fd24",
            .priority =
                "32dc6688a5c970e3eda4325bea5ee419077027e160697899e3b00c963fa1bb22",
            .attack =
                "dfd3aaa16755bee5d0c2c40956851b94ef5676a271a602eb23a57719f7358b01",
            .block =
                "d64e40796bd1587958b7386996e6a1e5660778d40ec7b40b0ee6324b8e39adbb",
            .damage_order =
                "f0a84ed549bbf95197dd00c13ab04c0a4f6b1771f14bdb30a7dca937d2d79c76",
        };
    CHECK(old_school::learned_model_component_fingerprints(
              generation_two) == expected_components);

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
          "8b9696870ca43087cddb3987a3d80759ac0528b552f1ead5447091d526cf2e06");
    CHECK(old_school::learned_model_fingerprint(loaded) ==
          fingerprint);
    CHECK(loaded_artifact.training_games() == 1);
    CHECK(loaded_artifact.seed() == 424242);
    CHECK(loaded_artifact.self_play_generations() == 2);
    CHECK(old_school::learned_value_challenger_cache_path(
              800, 424242, 16) ==
          "build/model-cache/"
          "old-school-value-challenger-v3-c16-t800-s424242.bin");

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

TEST(terminal_weight_c17_uses_one_balanced_shard_and_changes_only_critic) {
    old_school::GameResult decisive;
    decisive.winner = 0;
    decisive.turns = 10;
    const double discount =
        0.5 * std::pow(0.985, 10.0);
    CHECK(old_school::learned_discounted_terminal_target(
              decisive, 0) ==
          0.5 + discount);
    CHECK(old_school::learned_discounted_terminal_target(
              decisive, 1) ==
          0.5 - discount);
    decisive.winner = -1;
    CHECK(old_school::learned_discounted_terminal_target(
              decisive, 0) == 0.5);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_discounted_terminal_target(
                    decisive, 2));
        },
        "perspective"));

    const auto& artifact = small_terminal_weight_family();
    const auto& report = artifact.report();
    CHECK(report.training_games == 1);
    CHECK(report.parent_training_seed == 424242);
    CHECK(report.parent_generations == 2);
    CHECK(report.shard_seed == 0x7A17C17ULL);
    CHECK(report.balanced_blocks == 1);
    CHECK(report.scheduled_games == 40);
    CHECK(report.bootstrap_distance == 4);
    CHECK(report.collection_search_worlds == 1);
    CHECK(report.collection_horizon_turns ==
          old_school::kLearnedValueSearchHorizonTurns);
    CHECK(report.collection_max_game_turns == 8);
    CHECK(report.collection_exploration_rate == 0.05);
    CHECK(report.control_terminal_weight == 0.50);
    CHECK(report.treatment_terminal_weight == 0.75);
    CHECK(report.fit_epochs == 3);
    CHECK(report.fit_learning_rate == 0.006);
    CHECK(report.parent_fingerprint ==
          "8b9696870ca43087cddb3987a3d80759ac0528b552f1ead5447091d526cf2e06");
    CHECK(report.control_fingerprint ==
          old_school::learned_model_fingerprint(
              artifact.control_model()));
    CHECK(report.treatment_fingerprint ==
          old_school::learned_model_fingerprint(
              artifact.treatment_model()));
    CHECK(report.control_fingerprint !=
          report.treatment_fingerprint);
    CHECK(report.control_target_hash !=
          report.treatment_target_hash);
    CHECK(report.historical_replay_examples ==
          report.anchor_examples +
              report.penultimate_generation_examples +
              report.last_generation_examples);
    CHECK(report.fit_examples ==
          report.historical_replay_examples +
              report.shard_examples);
    CHECK(report.bootstrapped_examples +
              report.terminal_tail_examples ==
          report.shard_examples);
    CHECK(report.maximum_target_delta_error <=
          8.0 * std::numeric_limits<double>::epsilon());
    CHECK(report.schedule_hash.size() == 64);
    CHECK(report.raw_shard_hash.size() == 64);
    CHECK(report.fit_feature_order_hash.size() == 64);
    CHECK(report.outcome_hash.size() == 64);

    std::size_t deck_examples = 0;
    std::size_t deck_bootstrapped = 0;
    std::size_t deck_tail = 0;
    for (const auto& deck : report.decks) {
        CHECK(deck.games == 16);
        CHECK(deck.examples > 0);
        CHECK(deck.bootstrapped_examples +
                  deck.terminal_tail_examples ==
              deck.examples);
        deck_examples += deck.examples;
        deck_bootstrapped += deck.bootstrapped_examples;
        deck_tail += deck.terminal_tail_examples;
    }
    CHECK(deck_examples == report.shard_examples);
    CHECK(deck_bootstrapped ==
          report.bootstrapped_examples);
    CHECK(deck_tail == report.terminal_tail_examples);

    const auto& parent_components = report.parent_components;
    for (const auto& candidate :
         {report.control_components,
          report.treatment_components}) {
        CHECK(candidate.critic != parent_components.critic);
        CHECK(candidate.priority == parent_components.priority);
        CHECK(candidate.attack == parent_components.attack);
        CHECK(candidate.block == parent_components.block);
        CHECK(candidate.damage_order ==
              parent_components.damage_order);
    }
}

TEST(terminal_weight_c17_artifact_is_distinct_atomic_and_fail_closed) {
    const auto& original = small_terminal_weight_family();
    const std::filesystem::path directory =
        "build/test-model-cache";
    const std::filesystem::path good =
        directory / "terminal-weight-family-good.bin";
    const std::filesystem::path corrupt =
        directory / "terminal-weight-family-corrupt.bin";
    const std::filesystem::path challenger =
        directory / "terminal-weight-cross-family.bin";
    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(challenger);

    old_school::
        write_learned_terminal_weight_c17_artifact_atomic(
            good.string(), original);
    const auto loaded =
        old_school::load_learned_terminal_weight_c17_artifact(
            good.string(), 1, 424242, 0x7A17C17ULL);
    CHECK(loaded.report() == original.report());
    CHECK(old_school::learned_model_fingerprint(
              loaded.control_model()) ==
          original.report().control_fingerprint);
    CHECK(old_school::learned_model_fingerprint(
              loaded.treatment_model()) ==
          original.report().treatment_fingerprint);
    CHECK(old_school::learned_terminal_weight_c17_cache_path(
              800, 424242) ==
          "build/model-cache/"
          "old-school-value-terminal-weight-c17-v1-t800-p424242-"
          "r202607260311.bin");

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_terminal_weight_c17_artifact(
                        good.string(), 2, 424242,
                        0x7A17C17ULL));
        },
        "training_games mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_terminal_weight_c17_artifact(
                        good.string(), 1, 424243,
                        0x7A17C17ULL));
        },
        "parent training seed mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_terminal_weight_c17_artifact(
                        good.string(), 1, 424242,
                        0x7A17C18ULL));
        },
        "shard seed mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_challenger_artifact(
                        good.string(), 1, 424242, 2));
        },
        "wrong magic"));

    old_school::write_learned_value_challenger_artifact_atomic(
        challenger.string(),
        small_value_challenger_c2_artifact());
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_terminal_weight_c17_artifact(
                        challenger.string(), 1, 424242,
                        0x7A17C17ULL));
        },
        "wrong magic"));

    auto changed = read_binary_file(good);
    CHECK(changed.size() > 64);
    changed.back() ^= 0x01U;
    write_binary_file(corrupt, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_terminal_weight_c17_artifact(
                        corrupt.string(), 1, 424242,
                        0x7A17C17ULL));
        },
        "checksum"));

    old_school::LearnedTerminalWeightC17Config canonical;
    canonical.required_parent_fingerprint.clear();
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    train_learned_terminal_weight_c17_family(
                        canonical));
        },
        "exact frozen C16 fingerprint"));
    CHECK(throws_with_text(
        [] {
            static_cast<void>(
                old_school::
                    learned_terminal_weight_c17_cache_path(
                        0, 424242));
        },
        "positive"));

    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(challenger);
}

TEST(joint_c17_family_is_deterministic_shared_and_critic_only) {
    const auto& artifact = small_joint_c17_family();
    const auto& report = artifact.report();
    CHECK(report.training_games == 1);
    CHECK(report.parent_training_seed == 424242);
    CHECK(report.parent_generations == 2);
    CHECK(report.shard_seed == 0x7A17C18ULL);
    CHECK(report.shard_generation == 3);
    CHECK(report.balanced_blocks == 1);
    CHECK(report.scheduled_games == 40);
    CHECK(report.actor_perspectives == 80);
    CHECK(report.control_record_bootstrap_distance == 4);
    CHECK(report.treatment_turn_bootstrap_advances == 8);
    CHECK(report.collection_search_worlds == 1);
    CHECK(report.collection_horizon_turns ==
          old_school::kLearnedValueSearchHorizonTurns);
    CHECK(report.collection_max_game_turns == 12);
    CHECK(report.collection_exploration_rate == 0.05);
    CHECK(report.collection_value_continuation_epsilon == 0.0);
    CHECK(report.collection_value_priority_residual_weight == 0.0);
    CHECK(report.collection_blend_shallow_prior);
    CHECK(!report.collection_value_pass_dominance);
    CHECK(report.collection_continuation_controller ==
          old_school::LearnedContinuationController::Legacy);
    CHECK(report.control_terminal_weight == 0.5);
    CHECK(report.treatment_terminal_weight == 0.5);
    CHECK(report.fit_epochs == 3);
    CHECK(report.fit_learning_rate == 0.006);
    CHECK(report.fit_example_weight == 1.0);
    CHECK(report.fit_root_seed == 424242);
    CHECK(report.fit_member_training_tag ==
          0x53454C4600000200ULL);
    CHECK(report.parent_fingerprint ==
          "8b9696870ca43087cddb3987a3d80759ac0528b552f1ead5447091d526cf2e06");
    CHECK(report.control_fingerprint ==
          old_school::learned_model_fingerprint(
              artifact.control_model()));
    CHECK(report.treatment_fingerprint ==
          old_school::learned_model_fingerprint(
              artifact.treatment_model()));
    CHECK(artifact.control_model().get() !=
          artifact.treatment_model().get());
    CHECK(report.control_fingerprint !=
          report.treatment_fingerprint);
    CHECK(report.control_historical_replay_hash ==
          report.treatment_historical_replay_hash);
    CHECK(report.control_fit_feature_order_hash ==
          report.treatment_fit_feature_order_hash);
    CHECK(report.control_fit_order_hash ==
          report.treatment_fit_order_hash);
    CHECK(report.control_raw_shard_hash ==
          report.treatment_raw_shard_hash);
    CHECK(report.control_future_index_hash !=
          report.treatment_future_index_hash);
    CHECK(report.control_target_hash !=
          report.treatment_target_hash);
    CHECK(report.control_fit_target_hash !=
          report.treatment_fit_target_hash);
    CHECK(report.historical_replay_examples ==
          report.anchor_examples +
              report.penultimate_generation_examples +
              report.last_generation_examples);
    CHECK(report.fit_examples ==
          report.historical_replay_examples +
              report.shard_examples);
    CHECK(report.control_bootstrapped_examples +
              report.control_terminal_tail_examples ==
          report.shard_examples);
    CHECK(report.treatment_bootstrapped_examples +
              report.treatment_terminal_tail_examples ==
          report.shard_examples);
    CHECK(report.treatment_bootstrapped_examples > 0);
    CHECK(report.treatment_terminal_tail_examples > 0);
    CHECK(report.maximum_control_target_error == 0.0);
    CHECK(report.maximum_treatment_target_error == 0.0);

    std::size_t perspectives = 0;
    std::size_t examples = 0;
    std::size_t control_bootstrapped = 0;
    std::size_t control_tail = 0;
    std::size_t treatment_bootstrapped = 0;
    std::size_t treatment_tail = 0;
    for (const auto& deck : report.decks) {
        CHECK(deck.perspectives == 16);
        CHECK(deck.examples > 0);
        CHECK(deck.control_bootstrapped_examples +
                  deck.control_terminal_tail_examples ==
              deck.examples);
        CHECK(deck.treatment_bootstrapped_examples +
                  deck.treatment_terminal_tail_examples ==
              deck.examples);
        perspectives += deck.perspectives;
        examples += deck.examples;
        control_bootstrapped +=
            deck.control_bootstrapped_examples;
        control_tail +=
            deck.control_terminal_tail_examples;
        treatment_bootstrapped +=
            deck.treatment_bootstrapped_examples;
        treatment_tail +=
            deck.treatment_terminal_tail_examples;
    }
    CHECK(perspectives == report.actor_perspectives);
    CHECK(examples == report.shard_examples);
    CHECK(control_bootstrapped ==
          report.control_bootstrapped_examples);
    CHECK(control_tail ==
          report.control_terminal_tail_examples);
    CHECK(treatment_bootstrapped ==
          report.treatment_bootstrapped_examples);
    CHECK(treatment_tail ==
          report.treatment_terminal_tail_examples);

    for (const auto& candidate :
         {report.control_components,
          report.treatment_components}) {
        CHECK(candidate.critic !=
              report.parent_components.critic);
        CHECK(candidate.priority ==
              report.parent_components.priority);
        CHECK(candidate.attack ==
              report.parent_components.attack);
        CHECK(candidate.block ==
              report.parent_components.block);
        CHECK(candidate.damage_order ==
              report.parent_components.damage_order);
    }
    CHECK(report.control_components.critic !=
          report.treatment_components.critic);

    const auto control =
        artifact.control_deployment(0xC017ULL);
    CHECK(control.arm ==
          old_school::LearnedJointC17Arm::Control);
    CHECK(control.policy_token ==
          old_school::kLearnedJointC17ControlPolicyToken);
    CHECK(control.model == artifact.control_model());
    CHECK(control.bot.kind == old_school::BotKind::Learned);
    CHECK(control.bot.rollouts_per_action == 8);
    CHECK(control.bot.exploration_rate == 0.0);
    CHECK(!control.bot.value_pass_dominance);
    CHECK(control.bot.value_continuation_controller ==
          old_school::LearnedContinuationController::Legacy);
    CHECK(control.search.seed == 0xC017ULL);
    CHECK(control.search.worlds == 8);
    CHECK(control.search.rollouts_per_world == 1);
    CHECK(control.search.horizon_turns == 4);
    CHECK(control.search.blend_shallow_prior);
    CHECK(!control.search.value_pass_dominance);
    CHECK(control.search.value_continuation_controller ==
          old_school::LearnedContinuationController::Legacy);

    const auto treatment =
        artifact.treatment_deployment(0xC018ULL);
    CHECK(treatment.arm ==
          old_school::LearnedJointC17Arm::Treatment);
    CHECK(treatment.policy_token ==
          old_school::kLearnedJointC17TreatmentPolicyToken);
    CHECK(treatment.model == artifact.treatment_model());
    CHECK(treatment.bot.rollouts_per_action == 8);
    CHECK(treatment.bot.value_pass_dominance);
    CHECK(treatment.bot.value_continuation_controller ==
          old_school::LearnedContinuationController::
              PublicStackPassV1);
    CHECK(treatment.search.seed == 0xC018ULL);
    CHECK(treatment.search.worlds == 8);
    CHECK(treatment.search.horizon_turns == 4);
    CHECK(treatment.search.blend_shallow_prior);
    CHECK(treatment.search.value_pass_dominance);
    CHECK(treatment.search.value_continuation_controller ==
          old_school::LearnedContinuationController::
              PublicStackPassV1);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(artifact.deployment(
                static_cast<
                    old_school::LearnedJointC17Arm>(0xFFU)));
        },
        "arm is invalid"));

    old_school::LearnedJointC17Config repeated_config;
    repeated_config.training_games = 1;
    repeated_config.parent_training_seed = 424242;
    repeated_config.parent_generations = 2;
    repeated_config.shard_seed = 0x7A17C18ULL;
    repeated_config.balanced_blocks = 1;
    repeated_config.max_game_turns = 12;
    repeated_config.required_parent_fingerprint =
        report.parent_fingerprint;
    const auto repeated =
        old_school::train_learned_joint_c17_family(
            std::move(repeated_config));
    CHECK(repeated.report() == report);
    CHECK(old_school::learned_model_fingerprint(
              repeated.control_model()) ==
          report.control_fingerprint);
    CHECK(old_school::learned_model_fingerprint(
              repeated.treatment_model()) ==
          report.treatment_fingerprint);
}

TEST(joint_c17_artifact_is_no_replace_and_cross_family_closed) {
    const auto& original = small_joint_c17_family();
    const std::filesystem::path directory =
        "build/test-model-cache";
    const std::filesystem::path good =
        directory / "joint-c17-family-good.bin";
    const std::filesystem::path corrupt =
        directory / "joint-c17-family-corrupt.bin";
    const std::filesystem::path trailing =
        directory / "joint-c17-family-trailing.bin";
    const std::filesystem::path existing =
        directory / "joint-c17-family-existing.bin";
    const std::filesystem::path existing_directory =
        directory / "joint-c17-family-existing-directory";
    const std::filesystem::path sentinel =
        directory / "joint-c17-family-sentinel.bin";
    const std::filesystem::path symlink =
        directory / "joint-c17-family-symlink.bin";
    const std::filesystem::path terminal_weight =
        directory / "joint-c17-family-terminal-weight.bin";
    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(trailing);
    std::filesystem::remove(existing);
    std::filesystem::remove(existing_directory);
    std::filesystem::remove(sentinel);
    std::filesystem::remove(symlink);
    std::filesystem::remove(terminal_weight);

    old_school::write_learned_joint_c17_artifact_atomic(
        good.string(), original);
    const auto first_bytes = read_binary_file(good);
    const auto loaded =
        old_school::load_learned_joint_c17_artifact(
            good.string(), 1, 424242, 0x7A17C18ULL);
    CHECK(loaded.report() == original.report());
    CHECK(old_school::learned_model_fingerprint(
              loaded.control_model()) ==
          original.report().control_fingerprint);
    CHECK(old_school::learned_model_fingerprint(
              loaded.treatment_model()) ==
          original.report().treatment_fingerprint);
    CHECK(old_school::learned_joint_c17_cache_path(
              800, 424242) ==
          "build/model-cache/"
          "old-school-value-joint-c17-v1-t800-p424242-"
          "r202607261145.bin");
    const std::filesystem::path canonical_destination =
        std::filesystem::absolute(
            std::filesystem::path(".") /
            old_school::learned_joint_c17_cache_path(
                800, 424242))
            .lexically_normal();
    CHECK(throws_with_text(
        [&] {
            old_school::
                write_learned_joint_c17_artifact_atomic(
                    canonical_destination.string(), original);
        },
        "canonical joint C17 cache path"));

    CHECK(throws_with_text(
        [&] {
            old_school::
                write_learned_joint_c17_artifact_atomic(
                    good.string(), original);
        },
        "already exists"));
    CHECK(read_binary_file(good) == first_bytes);

    const std::vector<std::uint8_t> sentinel_bytes = {
        0x11U, 0x22U, 0x33U, 0x44U,
    };
    write_binary_file(existing, sentinel_bytes);
    CHECK(throws_with_text(
        [&] {
            old_school::
                write_learned_joint_c17_artifact_atomic(
                    existing.string(), original);
        },
        "already exists"));
    CHECK(read_binary_file(existing) == sentinel_bytes);

    std::filesystem::create_directory(existing_directory);
    CHECK(throws_with_text(
        [&] {
            old_school::
                write_learned_joint_c17_artifact_atomic(
                    existing_directory.string(), original);
        },
        "already exists"));
    CHECK(std::filesystem::is_directory(existing_directory));

    write_binary_file(sentinel, sentinel_bytes);
    std::filesystem::create_symlink(
        std::filesystem::absolute(sentinel), symlink);
    CHECK(throws_with_text(
        [&] {
            old_school::
                write_learned_joint_c17_artifact_atomic(
                    symlink.string(), original);
        },
        "already exists"));
    CHECK(std::filesystem::is_symlink(
        std::filesystem::symlink_status(symlink)));
    CHECK(read_binary_file(sentinel) == sentinel_bytes);

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_joint_c17_artifact(
                    good.string(), 2, 424242,
                    0x7A17C18ULL));
        },
        "training_games mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_joint_c17_artifact(
                    good.string(), 1, 424243,
                    0x7A17C18ULL));
        },
        "parent training seed mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_joint_c17_artifact(
                    good.string(), 1, 424242,
                    0x7A17C19ULL));
        },
        "shard seed mismatch"));

    auto changed = first_bytes;
    CHECK(changed.size() > 64);
    changed.back() ^= 0x01U;
    write_binary_file(corrupt, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_joint_c17_artifact(
                    corrupt.string(), 1, 424242,
                    0x7A17C18ULL));
        },
        "checksum"));
    changed = first_bytes;
    changed.push_back(0x00U);
    write_binary_file(trailing, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_joint_c17_artifact(
                    trailing.string(), 1, 424242,
                    0x7A17C18ULL));
        },
        "invalid payload length"));

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_terminal_weight_c17_artifact(
                        good.string(), 1, 424242,
                        0x7A17C18ULL));
        },
        "wrong magic"));
    old_school::
        write_learned_terminal_weight_c17_artifact_atomic(
            terminal_weight.string(),
            small_terminal_weight_family());
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_joint_c17_artifact(
                    terminal_weight.string(), 1, 424242,
                    0x7A17C17ULL));
        },
        "wrong magic"));

    old_school::LearnedJointC17Config canonical;
    canonical.required_parent_fingerprint.clear();
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_joint_c17_family(
                    canonical));
        },
        "exact frozen C16 fingerprint"));
    canonical.required_parent_fingerprint =
        old_school::kLearnedJointC17ParentFingerprint;
    canonical.parent_generations = 15;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_joint_c17_family(
                    canonical));
        },
        "canonical joint C17 coordinates"));
    canonical.parent_generations = 16;
    canonical.balanced_blocks = 4;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_joint_c17_family(
                    canonical));
        },
        "canonical joint C17 coordinates"));
    canonical.balanced_blocks = 5;
    canonical.max_game_turns = 499;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_joint_c17_family(
                    canonical));
        },
        "canonical joint C17 coordinates"));
    CHECK(throws_with_text(
        [] {
            static_cast<void>(
                old_school::learned_joint_c17_cache_path(
                    0, 424242));
        },
        "positive"));

    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(trailing);
    std::filesystem::remove(existing);
    std::filesystem::remove(existing_directory);
    std::filesystem::remove(symlink);
    std::filesystem::remove(sentinel);
    std::filesystem::remove(terminal_weight);
}

TEST(value_context_challenger_s1_is_deterministic_and_reports_roots) {
    const auto& original =
        small_value_context_challenger_c1_artifact();
    const auto repeated =
        old_school::
            train_learned_value_context_challenger_artifact(
                1, 0xC07E6751ULL, 1);
    const auto changed_seed =
        old_school::
            train_learned_value_context_challenger_artifact(
                1, 0xC07E6752ULL, 1);

    const std::string fingerprint =
        old_school::learned_model_fingerprint(
            original.model());
    // Captured from the inline contextual distance-four target path before
    // it was routed through n_state_bootstrap_targets.
    CHECK(fingerprint ==
          "79fd1e93b5a6103fc6b27ff779aeb63f3ba21d9c1aff80b552a7ae78e4cf3b67");
    const old_school::LearnedModelComponentFingerprints
        expected_components = {
            .critic =
                "b1ff81db63126f3ce68580e1e2ba6f0151b7ab1943bb95f45dbeb455d2bec563",
            .priority =
                "82e63faa3724d9909e9215289e38feec7af6cc405e90c0c95ad39e1709efc104",
            .attack =
                "f152ae09d7abeab9f467c5876a45884b85b101c29d3c28f74ac4a8665368335e",
            .block =
                "1e91c76d1906ee90fec3f461b109c57ed5d7d80bb1618e490e186a12eaef526d",
            .damage_order =
                "fdf0e08528639c9f20df779623f54023cc4a652d6d7696add5252a7d4d5f5a37",
        };
    CHECK(old_school::learned_model_component_fingerprints(
              original.model()) == expected_components);
    CHECK(old_school::learned_model_fingerprint(
              repeated.model()) == fingerprint);
    CHECK(old_school::learned_model_fingerprint(
              changed_seed.model()) != fingerprint);
    CHECK(old_school::learned_critic_schema(
              original.model()) ==
          old_school::LearnedCriticSchema::
              DecisionContextV1);
    CHECK(original.critic_schema() ==
          old_school::LearnedCriticSchema::
              DecisionContextV1);
    CHECK(original.trace_mode() ==
          old_school::LearnedDecisionTraceMode::Sparse);
    CHECK(original.trace_limit() == 0);
    CHECK(original.training_games() == 1);
    CHECK(original.seed() == 0xC07E6751ULL);
    CHECK(original.self_play_generations() == 1);

    const auto& coverage = original.root_coverage();
    CHECK(coverage == repeated.root_coverage());
    CHECK(coverage.anchor_roots > 0);
    CHECK(coverage.self_play_roots > 0);
    const std::size_t total = coverage.total_roots();
    CHECK(total ==
          coverage.anchor_roots +
              coverage.self_play_roots);
    CHECK(std::accumulate(
              coverage.decision_player_decks.begin(),
              coverage.decision_player_decks.end(),
              std::size_t{0}) == total);
    CHECK(std::accumulate(
              coverage.phases.begin(),
              coverage.phases.end(),
              std::size_t{0}) == total);
    CHECK(std::accumulate(
              coverage.pass_counts.begin(),
              coverage.pass_counts.end(),
              std::size_t{0}) == total);
    CHECK(std::accumulate(
              coverage.stack_status.begin(),
              coverage.stack_status.end(),
              std::size_t{0}) == total);
}

TEST(value_context_challenger_artifact_roundtrips_and_fails_closed) {
    const auto& original_artifact =
        small_value_context_challenger_c1_artifact();
    const auto original = original_artifact.model();
    const std::filesystem::path directory =
        "build/test-model-cache";
    const std::filesystem::path good =
        directory / "value-context-c1-good.bin";
    const std::filesystem::path corrupt =
        directory / "value-context-c1-corrupt.bin";
    const std::filesystem::path trailing =
        directory / "value-context-c1-trailing.bin";
    const std::filesystem::path truncated =
        directory / "value-context-c1-truncated.bin";
    const std::filesystem::path legacy =
        directory / "value-challenger-c2-context-cross.bin";
    const std::filesystem::path canonical =
        directory / "value-g8-context-cross.bin";
    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(trailing);
    std::filesystem::remove(truncated);
    std::filesystem::remove(legacy);
    std::filesystem::remove(canonical);

    old_school::
        write_learned_value_context_challenger_artifact_atomic(
            good.string(), original_artifact);
    const auto bytes = read_binary_file(good);
    const std::array<std::uint8_t, 8> expected_magic = {
        'O', 'S', 'M', 'V', 'C', 'T', 'X', '1',
    };
    CHECK(bytes.size() > expected_magic.size());
    CHECK(std::equal(
        expected_magic.begin(), expected_magic.end(),
        bytes.begin()));

    const auto loaded_artifact =
        old_school::
            load_learned_value_context_challenger_artifact(
                good.string(), 1, 0xC07E6751ULL, 1);
    const auto loaded = loaded_artifact.model();
    const std::string fingerprint =
        old_school::learned_model_fingerprint(original);
    CHECK(old_school::learned_model_fingerprint(loaded) ==
          fingerprint);
    CHECK(loaded_artifact.root_coverage() ==
          original_artifact.root_coverage());
    CHECK(loaded_artifact.critic_schema() ==
          old_school::LearnedCriticSchema::
              DecisionContextV1);
    CHECK(loaded_artifact.trace_mode() ==
          old_school::LearnedDecisionTraceMode::Sparse);
    CHECK(loaded_artifact.trace_limit() == 0);
    CHECK(old_school::
              learned_value_context_challenger_cache_path(
                  800, 424242, 16) ==
          "build/model-cache/"
          "old-school-value-context-s1-v3-c16-t800-s424242.bin");

    const old_school::GameState state =
        determinization_fixture().state;
    const std::array<old_school::LearnedDecisionContext, 2>
        contexts = {
            old_school::LearnedDecisionContext{
                .valid = true,
                .phase =
                    old_school::TurnPhase::SecondMain,
                .decision_player = 0,
                .consecutive_passes = 0,
                .sorcery_actions = false,
            },
            old_school::LearnedDecisionContext{
                .valid = true,
                .phase =
                    old_school::TurnPhase::SecondMain,
                .decision_player = 1,
                .consecutive_passes = 1,
                .sorcery_actions = false,
            },
        };
    for (const auto& context : contexts) {
        for (std::size_t perspective = 0;
             perspective < 2; ++perspective) {
            const double before =
                old_school::learned_contextual_critic_value(
                    state, perspective, context, original);
            const double after =
                old_school::learned_contextual_critic_value(
                    state, perspective, context, loaded);
            CHECK(std::bit_cast<std::uint64_t>(after) ==
                  std::bit_cast<std::uint64_t>(before));
        }
    }

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        good.string(), 2,
                        0xC07E6751ULL, 1));
        },
        "training_games mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        good.string(), 1,
                        0xC07E6752ULL, 1));
        },
        "training seed mismatch"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        good.string(), 1,
                        0xC07E6751ULL, 2));
        },
        "generation mismatch"));

    old_school::write_learned_value_challenger_artifact_atomic(
        legacy.string(),
        small_value_challenger_c2_artifact());
    old_school::write_learned_value_g8_bundle_atomic(
        canonical.string(), small_value_g8());
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_challenger_artifact(
                        good.string(), 1,
                        0xC07E6751ULL, 1));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::load_learned_value_g8_bundle(
                    good.string(), 1, 0xC07E6751ULL));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        legacy.string(), 1, 424242, 2));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        canonical.string(), 1,
                        0x68A11EADULL, 1));
        },
        "wrong magic"));

    auto changed = bytes;
    changed.back() ^= 0x01U;
    write_binary_file(corrupt, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        corrupt.string(), 1,
                        0xC07E6751ULL, 1));
        },
        "checksum"));

    changed = bytes;
    changed.push_back(0x00U);
    write_binary_file(trailing, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        trailing.string(), 1,
                        0xC07E6751ULL, 1));
        },
        "payload length"));

    changed = bytes;
    changed.pop_back();
    write_binary_file(truncated, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        truncated.string(), 1,
                        0xC07E6751ULL, 1));
        },
        "payload length"));

    CHECK(throws_with_text(
        [] {
            static_cast<void>(
                old_school::
                    learned_value_context_challenger_cache_path(
                        1, 424242, 0));
        },
        "generations"));
    std::filesystem::remove(good);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(trailing);
    std::filesystem::remove(truncated);
    std::filesystem::remove(legacy);
    std::filesystem::remove(canonical);
}

TEST(value_dense_context_cells_are_deterministic_bounded_and_isolated) {
    using Treatment =
        old_school::LearnedValueDenseContextTreatment;
    const auto& d0 =
        small_value_dense_context_d0_c1_artifact();
    const auto& d1 =
        small_value_dense_context_d1_c1_artifact();
    const auto repeated_d0 =
        old_school::
            train_learned_value_dense_context_challenger_artifact(
                1, 0xD305E001ULL, 1,
                Treatment::ContextMasked);
    const auto repeated_d1 =
        old_school::
            train_learned_value_dense_context_challenger_artifact(
                1, 0xD305E001ULL, 1,
                Treatment::ContextLive);

    const std::string d0_fingerprint =
        old_school::learned_model_fingerprint(d0.model());
    const std::string d1_fingerprint =
        old_school::learned_model_fingerprint(d1.model());
    CHECK(old_school::learned_model_fingerprint(
              repeated_d0.model()) == d0_fingerprint);
    CHECK(old_school::learned_model_fingerprint(
              repeated_d1.model()) == d1_fingerprint);
    CHECK(d0_fingerprint != d1_fingerprint);
    CHECK(d0.root_coverage() ==
          repeated_d0.root_coverage());
    CHECK(d1.root_coverage() ==
          repeated_d1.root_coverage());

    for (const auto* artifact : {&d0, &d1}) {
        CHECK(artifact->critic_schema() ==
              old_school::LearnedCriticSchema::
                  DecisionContextV1);
        CHECK(artifact->trace_mode() ==
              old_school::LearnedDecisionTraceMode::Dense);
        CHECK(artifact->trace_limit() ==
              old_school::kLearnedDenseDecisionTraceLimit);
        CHECK(artifact->training_games() == 1);
        CHECK(artifact->seed() == 0xD305E001ULL);
        CHECK(artifact->self_play_generations() == 1);

        const auto& coverage = artifact->root_coverage();
        CHECK(coverage.anchor_roots > 0);
        CHECK(coverage.self_play_roots > 0);
        CHECK(coverage.anchor_roots <=
              old_school::kLearnedDenseDecisionTraceLimit);
        CHECK(coverage.self_play_roots <=
              old_school::kLearnedDenseDecisionTraceLimit);
        const std::size_t total = coverage.total_roots();
        CHECK(std::accumulate(
                  coverage.decision_player_decks.begin(),
                  coverage.decision_player_decks.end(),
                  std::size_t{0}) == total);
        CHECK(std::accumulate(
                  coverage.phases.begin(),
                  coverage.phases.end(),
                  std::size_t{0}) == total);
        CHECK(std::accumulate(
                  coverage.pass_counts.begin(),
                  coverage.pass_counts.end(),
                  std::size_t{0}) == total);
        CHECK(std::accumulate(
                  coverage.stack_status.begin(),
                  coverage.stack_status.end(),
                  std::size_t{0}) == total);
        CHECK(coverage.pass_counts[1] > 0);
        CHECK(coverage.stack_status[1] > 0);
    }
    CHECK(d0.treatment() == Treatment::ContextMasked);
    CHECK(d0.context_masked());
    CHECK(d1.treatment() == Treatment::ContextLive);
    CHECK(!d1.context_masked());

    const old_school::GameState state =
        determinization_fixture().state;
    const old_school::LearnedDecisionContext pass_zero{
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    const old_school::LearnedDecisionContext pass_one{
        .valid = true,
        .phase = old_school::TurnPhase::SecondMain,
        .decision_player = 1,
        .consecutive_passes = 1,
        .sorcery_actions = true,
    };
    for (std::size_t perspective = 0;
         perspective < 2; ++perspective) {
        const double d0_zero =
            old_school::learned_contextual_critic_value(
                state, perspective, pass_zero, d0.model());
        const double d0_one =
            old_school::learned_contextual_critic_value(
                state, perspective, pass_one, d0.model());
        CHECK(std::bit_cast<std::uint64_t>(d0_zero) ==
              std::bit_cast<std::uint64_t>(d0_one));
    }
    CHECK(old_school::learned_contextual_critic_value(
              state, 0, pass_zero, d1.model()) !=
          old_school::learned_contextual_critic_value(
              state, 0, pass_one, d1.model()));
}

TEST(value_dense_context_artifacts_roundtrip_and_cross_load_fail) {
    using Treatment =
        old_school::LearnedValueDenseContextTreatment;
    const auto& d0 =
        small_value_dense_context_d0_c1_artifact();
    const auto& d1 =
        small_value_dense_context_d1_c1_artifact();
    const std::filesystem::path directory =
        "build/test-model-cache";
    const std::filesystem::path d0_path =
        directory / "value-context-d0-c1.bin";
    const std::filesystem::path d1_path =
        directory / "value-context-d1-c1.bin";
    const std::filesystem::path corrupt =
        directory / "value-context-d0-c1-corrupt.bin";
    const std::filesystem::path s1_path =
        directory / "value-context-s1-cross.bin";
    std::filesystem::remove(d0_path);
    std::filesystem::remove(d1_path);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(s1_path);

    old_school::
        write_learned_value_dense_context_challenger_artifact_atomic(
            d0_path.string(), d0);
    old_school::
        write_learned_value_dense_context_challenger_artifact_atomic(
            d1_path.string(), d1);
    old_school::
        write_learned_value_context_challenger_artifact_atomic(
            s1_path.string(),
            small_value_context_challenger_c1_artifact());

    const auto d0_bytes = read_binary_file(d0_path);
    const auto d1_bytes = read_binary_file(d1_path);
    const std::array<std::uint8_t, 8> d0_magic = {
        'O', 'S', 'M', 'V', 'D', '0', '0', '1',
    };
    const std::array<std::uint8_t, 8> d1_magic = {
        'O', 'S', 'M', 'V', 'D', '1', '0', '1',
    };
    CHECK(std::equal(
        d0_magic.begin(), d0_magic.end(), d0_bytes.begin()));
    CHECK(std::equal(
        d1_magic.begin(), d1_magic.end(), d1_bytes.begin()));
    CHECK(d0_bytes != d1_bytes);

    const auto loaded_d0 =
        old_school::
            load_learned_value_dense_context_challenger_artifact(
                d0_path.string(), 1, 0xD305E001ULL, 1,
                Treatment::ContextMasked);
    const auto loaded_d1 =
        old_school::
            load_learned_value_dense_context_challenger_artifact(
                d1_path.string(), 1, 0xD305E001ULL, 1,
                Treatment::ContextLive);
    CHECK(old_school::learned_model_fingerprint(
              loaded_d0.model()) ==
          old_school::learned_model_fingerprint(d0.model()));
    CHECK(old_school::learned_model_fingerprint(
              loaded_d1.model()) ==
          old_school::learned_model_fingerprint(d1.model()));
    CHECK(loaded_d0.root_coverage() == d0.root_coverage());
    CHECK(loaded_d1.root_coverage() == d1.root_coverage());
    CHECK(loaded_d0.context_masked());
    CHECK(!loaded_d1.context_masked());

    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_dense_context_challenger_artifact(
                        d0_path.string(), 1,
                        0xD305E001ULL, 1,
                        Treatment::ContextLive));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_dense_context_challenger_artifact(
                        d1_path.string(), 1,
                        0xD305E001ULL, 1,
                        Treatment::ContextMasked));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_dense_context_challenger_artifact(
                        s1_path.string(), 1,
                        0xC07E6751ULL, 1,
                        Treatment::ContextLive));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_context_challenger_artifact(
                        d1_path.string(), 1,
                        0xD305E001ULL, 1));
        },
        "wrong magic"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_dense_context_challenger_artifact(
                        d0_path.string(), 2,
                        0xD305E001ULL, 1,
                        Treatment::ContextMasked));
        },
        "training_games mismatch"));

    auto changed = d0_bytes;
    changed.back() ^= 0x01U;
    write_binary_file(corrupt, changed);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    load_learned_value_dense_context_challenger_artifact(
                        corrupt.string(), 1,
                        0xD305E001ULL, 1,
                        Treatment::ContextMasked));
        },
        "checksum"));

    CHECK(old_school::
              learned_value_dense_context_challenger_cache_path(
                  800, 424242, 16,
                  Treatment::ContextMasked) ==
          "build/model-cache/"
          "old-school-value-context-d0-v3-c16-t800-s424242.bin");
    CHECK(old_school::
              learned_value_dense_context_challenger_cache_path(
                  800, 424242, 16,
                  Treatment::ContextLive) ==
          "build/model-cache/"
          "old-school-value-context-d1-v3-c16-t800-s424242.bin");
    CHECK(throws_with_text(
        [] {
            static_cast<void>(
                old_school::
                    learned_value_dense_context_challenger_cache_path(
                        1, 424242, 0,
                        Treatment::ContextMasked));
        },
        "generations"));
    CHECK(throws_with_text(
        [] {
            static_cast<void>(
                old_school::
                    learned_value_dense_context_challenger_cache_path(
                        1, 424242, 1,
                        static_cast<Treatment>(255)));
        },
        "unknown"));

    std::filesystem::remove(d0_path);
    std::filesystem::remove(d1_path);
    std::filesystem::remove(corrupt);
    std::filesystem::remove(s1_path);
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

TEST(learned_output_calibration_is_deterministic_and_isolated) {
    const auto parent = small_value_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto features =
        old_school::learned_observation(state, 0);
    const double parent_value =
        old_school::learned_critic_value(state, 0, parent);
    const auto parent_leaf_values =
        old_school::learned_critic_leaf_values(
            state, 0, parent);
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const auto parent_components =
        old_school::learned_model_component_fingerprints(parent);
    const auto parent_tensors =
        old_school::learned_critic_tensor_fingerprints(parent);

    const auto empty =
        old_school::calibrate_learned_value_output_layer(
            parent, {}, {});
    CHECK(empty.model.get() != parent.get());
    CHECK(old_school::learned_model_fingerprint(empty.model) ==
          parent_fingerprint);
    CHECK(empty.diagnostics.example_count == 0);
    CHECK(empty.diagnostics.leaf_count > 0);
    CHECK(empty.diagnostics.iterations == 0);
    CHECK(empty.diagnostics.converged);
    CHECK(empty.diagnostics.total_weight == 0.0);
    CHECK(empty.diagnostics.before_weighted_bce == 0.0);
    CHECK(empty.diagnostics.after_weighted_bce == 0.0);
    CHECK(empty.diagnostics.max_parameter_delta == 0.0);

    const double target = parent_value < 0.5 ? 1.0 : 0.0;
    const std::vector<
        old_school::LearnedWeightedCriticTrainingExample>
        examples = {{
            .features = features,
            .target = target,
            .weight = 3.0,
        }};
    const auto first =
        old_school::calibrate_learned_value_output_layer(
            parent, examples, {});
    const auto second =
        old_school::calibrate_learned_value_output_layer(
            parent, examples, {});

    CHECK(first.diagnostics == second.diagnostics);
    CHECK(first.diagnostics.example_count == 1);
    CHECK(first.diagnostics.leaf_count ==
          empty.diagnostics.leaf_count);
    CHECK(first.diagnostics.iterations > 0);
    CHECK(first.diagnostics.iterations <= 32);
    CHECK(first.diagnostics.total_weight == 3.0);
    CHECK(first.diagnostics.after_weighted_bce <
          first.diagnostics.before_weighted_bce);
    CHECK(first.diagnostics.max_parameter_delta > 0.0);
    CHECK(old_school::learned_model_fingerprint(first.model) ==
          old_school::learned_model_fingerprint(second.model));
    const auto candidate_leaf_values =
        old_school::learned_critic_leaf_values(
            state, 0, first.model);
    for (std::size_t leaf = 0;
         leaf < candidate_leaf_values.size(); ++leaf) {
        if (target > parent_leaf_values[leaf]) {
            CHECK(candidate_leaf_values[leaf] >
                  parent_leaf_values[leaf]);
        } else {
            CHECK(candidate_leaf_values[leaf] <
                  parent_leaf_values[leaf]);
        }
    }

    const auto candidate_components =
        old_school::learned_model_component_fingerprints(
            first.model);
    const auto candidate_tensors =
        old_school::learned_critic_tensor_fingerprints(
            first.model);
    CHECK(candidate_components.critic !=
          parent_components.critic);
    CHECK(candidate_components.priority ==
          parent_components.priority);
    CHECK(candidate_components.attack ==
          parent_components.attack);
    CHECK(candidate_components.block ==
          parent_components.block);
    CHECK(candidate_components.damage_order ==
          parent_components.damage_order);
    CHECK(candidate_tensors.input_hidden ==
          parent_tensors.input_hidden);
    CHECK(candidate_tensors.output_layer !=
          parent_tensors.output_layer);
    CHECK(candidate_tensors.direct_paths ==
          parent_tensors.direct_paths);

    const auto parent_parameters =
        old_school::learned_output_calibration_parameters(
            parent);
    const auto candidate_parameters =
        old_school::learned_output_calibration_parameters(
            first.model);
    CHECK(candidate_parameters != parent_parameters);
    const auto restored_candidate =
        old_school::with_learned_output_calibration_parameters(
            parent, candidate_parameters);
    CHECK(old_school::learned_model_fingerprint(
              restored_candidate) ==
          old_school::learned_model_fingerprint(first.model));
    CHECK(old_school::learned_model_component_fingerprints(
              restored_candidate) == candidate_components);
    CHECK(old_school::learned_critic_tensor_fingerprints(
              restored_candidate) == candidate_tensors);
    const auto restored_parent =
        old_school::with_learned_output_calibration_parameters(
            parent, parent_parameters);
    CHECK(old_school::learned_model_fingerprint(
              restored_parent) == parent_fingerprint);

    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(old_school::learned_model_component_fingerprints(
              parent) == parent_components);
    CHECK(old_school::learned_critic_tensor_fingerprints(
              parent) == parent_tensors);
    CHECK(old_school::learned_critic_value(
              state, 0, parent) == parent_value);
}

TEST(learned_critic_leaf_values_are_exact_and_topology_checked) {
    const auto parent = small_value_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto leaves =
        old_school::learned_critic_leaf_values(
            state, 0, parent);
    CHECK((leaves[0] + leaves[1]) / 2.0 ==
          old_school::learned_critic_value(
              state, 0, parent));

    old_school::GameState repartitioned =
        hidden_repartition(state, 0);
    CHECK(
        repartitioned.players[1].hand !=
            state.players[1].hand ||
        repartitioned.players[1].library !=
            state.players[1].library);
    CHECK(old_school::learned_critic_leaf_values(
              repartitioned, 0, parent) == leaves);

    const auto contextual =
        old_school::with_learned_decision_context(parent);
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_critic_leaf_values(
                    state, 0, contextual));
        },
        "two-leaf legacy"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_critic_leaf_values(
                    state, 2, parent));
        },
        "perspective"));
}

TEST(learned_output_calibration_honors_example_weights) {
    const auto parent = small_value_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto features =
        old_school::learned_observation(state, 0);
    const auto positive =
        old_school::calibrate_learned_value_output_layer(
            parent,
            {
                {
                    .features = features,
                    .target = 1.0,
                    .weight = 9.0,
                },
                {
                    .features = features,
                    .target = 0.0,
                    .weight = 1.0,
                },
            },
            {});
    const auto negative =
        old_school::calibrate_learned_value_output_layer(
            parent,
            {
                {
                    .features = features,
                    .target = 1.0,
                    .weight = 1.0,
                },
                {
                    .features = features,
                    .target = 0.0,
                    .weight = 9.0,
                },
            },
            {});
    const auto positive_scaled =
        old_school::calibrate_learned_value_output_layer(
            parent,
            {
                {
                    .features = features,
                    .target = 1.0,
                    .weight = 90.0,
                },
                {
                    .features = features,
                    .target = 0.0,
                    .weight = 10.0,
                },
            },
            {});
    CHECK(positive.diagnostics.after_weighted_bce <
          positive.diagnostics.before_weighted_bce);
    CHECK(negative.diagnostics.after_weighted_bce <
          negative.diagnostics.before_weighted_bce);
    CHECK(old_school::learned_critic_value(
              state, 0, positive.model) >
          old_school::learned_critic_value(
              state, 0, negative.model));
    CHECK(old_school::learned_model_fingerprint(
              positive.model) ==
          old_school::learned_model_fingerprint(
              positive_scaled.model));
    CHECK(positive.diagnostics.before_weighted_bce ==
          positive_scaled.diagnostics.before_weighted_bce);
    CHECK(positive.diagnostics.after_weighted_bce ==
          positive_scaled.diagnostics.after_weighted_bce);
}

TEST(learned_output_calibration_rejects_malformed_inputs) {
    const auto parent = small_value_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto features =
        old_school::learned_observation(state, 0);
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
            old_school::calibrate_learned_value_output_layer(
                nullptr, {}, {}));
    }));
    CHECK(rejects_invalid([] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                small_actor_model(), {}, {}));
    }));

    auto short_features = features;
    short_features.pop_back();
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent,
                {{
                    .features = short_features,
                    .target = 0.5,
                    .weight = 1.0,
                }},
                {}));
    }));

    auto nonfinite_features = features;
    nonfinite_features.front() =
        std::numeric_limits<double>::quiet_NaN();
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent,
                {{
                    .features = nonfinite_features,
                    .target = 0.5,
                    .weight = 1.0,
                }},
                {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent,
                {{
                    .features = features,
                    .target = 1.01,
                    .weight = 1.0,
                }},
                {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent,
                {{
                    .features = features,
                    .target = 0.5,
                    .weight = 0.0,
                }},
                {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent,
                {{
                    .features = features,
                    .target = 0.5,
                    .weight =
                        std::numeric_limits<double>::
                            infinity(),
                }},
                {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent, {},
                {
                    .max_iterations = 0,
                }));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent, {},
                {
                    .max_iterations = 32,
                    .l2_tether = 0.0,
                }));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent, {},
                {
                    .max_iterations = 32,
                    .l2_tether = 0.01,
                    .gradient_tolerance =
                        std::numeric_limits<double>::
                            quiet_NaN(),
                }));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent, {},
                {
                    .max_iterations = 33,
                }));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent, {},
                {
                    .max_iterations = 32,
                    .l2_tether = 0.02,
                }));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent, {},
                {
                    .max_iterations = 32,
                    .l2_tether = 0.01,
                    .gradient_tolerance = 1e-9,
                }));
    }));
    CHECK(rejects_invalid([] {
        static_cast<void>(
            old_school::learned_critic_tensor_fingerprints(
                nullptr));
    }));
    CHECK(rejects_invalid([] {
        static_cast<void>(
            old_school::learned_output_calibration_parameters(
                nullptr));
    }));
    CHECK(rejects_invalid([] {
        static_cast<void>(
            old_school::learned_output_calibration_parameters(
                small_actor_model()));
    }));
    auto parameters =
        old_school::learned_output_calibration_parameters(
            parent);
    parameters.leaves[0][0] =
        std::numeric_limits<double>::quiet_NaN();
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::with_learned_output_calibration_parameters(
                parent, parameters));
    }));
}

TEST(learned_output_calibration_fails_closed_on_topology_and_nonconvergence) {
    const auto parent = small_value_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto features =
        old_school::learned_observation(state, 0);
    const double value =
        old_school::learned_critic_value(state, 0, parent);
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const auto contextual =
        old_school::with_learned_decision_context(parent);

    bool topology_invalid = false;
    try {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                contextual, {}, {}));
    } catch (const std::invalid_argument&) {
        topology_invalid = true;
    }
    CHECK(topology_invalid);

    bool infrastructure_failure = false;
    try {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent,
                {{
                    .features = features,
                    .target = value < 0.5 ? 1.0 : 0.0,
                    .weight = 1.0,
                }},
                {
                    .max_iterations = 1,
                    .l2_tether = 0.01,
                    .gradient_tolerance = 1e-10,
                }));
    } catch (const std::runtime_error& error) {
        infrastructure_failure =
            std::string_view(error.what()).find(
                "infrastructure error") !=
                std::string_view::npos &&
            std::string_view(error.what()).find(
                "did not converge") !=
                std::string_view::npos;
    }
    CHECK(infrastructure_failure);
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);

    bool cap_invalid = false;
    try {
        static_cast<void>(
            old_school::calibrate_learned_value_output_layer(
                parent, {},
                {
                    .max_iterations = 33,
                    .l2_tether = 0.01,
                    .gradient_tolerance = 1e-10,
                }));
    } catch (const std::invalid_argument&) {
        cap_invalid = true;
    }
    CHECK(cap_invalid);
}

TEST(value_priority_head_adam_is_deterministic_isolated_and_bounded) {
    const auto parent = small_value_model();
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() > 1);
    std::vector<std::vector<double>> options;
    options.reserve(actions.size());
    for (const auto& action : actions) {
        options.push_back(
            old_school::learned_priority_policy_features(
                state, 0, action, true,
                old_school::TurnPhase::FirstMain, 1));
    }

    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const auto parent_components =
        old_school::learned_model_component_fingerprints(parent);
    const auto pure_clone =
        old_school::update_learned_value_priority_head(
            parent, {}, {});
    CHECK(pure_clone.get() != parent.get());
    CHECK(old_school::learned_model_fingerprint(pure_clone) ==
          parent_fingerprint);
    CHECK(old_school::learned_model_component_fingerprints(
              pure_clone) == parent_components);

    std::vector<double> base_scores(options.size(), 0.5);
    if (base_scores.size() > 2) {
        base_scores[0] = 0.55;
        base_scores[1] = 0.45;
    }
    std::vector<double> targets(options.size(), 0.0);
    targets.back() = 1.0;
    const old_school::LearnedValuePriorityTrainingExample example = {
        .options = options,
        .base_scores = base_scores,
        .target_probabilities = targets,
        .weight = 1.0,
    };
    const old_school::LearnedValuePriorityHeadUpdateConfig config = {
        .batch_size = 64,
        .epochs = 8,
        .learning_rate = 0.001,
        .beta1 = 0.9,
        .beta2 = 0.999,
        .epsilon = 1.0e-8,
        .global_gradient_norm_clip = 5.0,
        .seed = 0xADAA050ULL,
        .residual_weight = 0.10,
        .policy_temperature = 0.10,
    };

    const auto parent_priority =
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Priority,
            parent);
    std::array<std::vector<double>, 3> parent_combat_heads = {
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Attack,
            parent),
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Block,
            parent),
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::DamageOrder,
            parent),
    };
    std::array<std::uint64_t, 2> parent_critic_bits{};
    for (std::size_t perspective = 0; perspective < 2;
         ++perspective) {
        parent_critic_bits[perspective] =
            std::bit_cast<std::uint64_t>(
                old_school::learned_critic_value(
                    state, perspective, parent));
    }

    const auto candidate =
        old_school::update_learned_value_priority_head(
            parent, {example}, config);
    const auto repeated =
        old_school::update_learned_value_priority_head(
            parent, {example}, config);
    CHECK(old_school::learned_model_fingerprint(candidate) ==
          old_school::learned_model_fingerprint(repeated));
    CHECK(old_school::learned_model_fingerprint(candidate) !=
          parent_fingerprint);
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);

    const auto candidate_components =
        old_school::learned_model_component_fingerprints(candidate);
    CHECK(candidate_components ==
          old_school::learned_model_component_fingerprints(
              repeated));
    CHECK(candidate_components.critic == parent_components.critic);
    CHECK(candidate_components.priority !=
          parent_components.priority);
    CHECK(candidate_components.attack == parent_components.attack);
    CHECK(candidate_components.block == parent_components.block);
    CHECK(candidate_components.damage_order ==
          parent_components.damage_order);

    const auto candidate_priority =
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Priority,
            candidate);
    CHECK(candidate_priority != parent_priority);
    CHECK(candidate_priority ==
          old_school::learned_policy_head_logits(
              options,
              old_school::LearnedPolicyDecisionKind::Priority,
              repeated));
    const std::array<old_school::LearnedPolicyDecisionKind, 3>
        combat_kinds = {
            old_school::LearnedPolicyDecisionKind::Attack,
            old_school::LearnedPolicyDecisionKind::Block,
            old_school::LearnedPolicyDecisionKind::DamageOrder,
        };
    for (std::size_t index = 0; index < combat_kinds.size();
         ++index) {
        CHECK(old_school::learned_policy_head_logits(
                  options, combat_kinds[index], candidate) ==
              parent_combat_heads[index]);
    }
    for (std::size_t perspective = 0; perspective < 2;
         ++perspective) {
        CHECK(std::bit_cast<std::uint64_t>(
                  old_school::learned_critic_value(
                      state, perspective, candidate)) ==
              parent_critic_bits[perspective]);
    }

    const auto residual =
        old_school::diagnose_learned_value_priority_residual(
            state, 0, true, old_school::TurnPhase::FirstMain, 1,
            actions, candidate, 0.10);
    CHECK(residual.policy_logits == candidate_priority);
    CHECK(std::abs(std::accumulate(
              residual.centered_policy_logits.begin(),
              residual.centered_policy_logits.end(), 0.0)) <
          1.0e-12);
    CHECK(std::any_of(
        residual.residuals.begin(), residual.residuals.end(),
        [](double value) { return value != 0.0; }));
    for (std::size_t index = 0;
         index < residual.residuals.size(); ++index) {
        CHECK(std::abs(residual.residuals[index]) <= 0.10);
        CHECK(residual.residuals[index] ==
              0.10 *
                  std::tanh(
                      residual.centered_policy_logits[index]));
    }

    auto reversed_actions = actions;
    std::reverse(
        reversed_actions.begin(), reversed_actions.end());
    const auto reversed_residual =
        old_school::diagnose_learned_value_priority_residual(
            state, 0, true, old_school::TurnPhase::FirstMain, 1,
            reversed_actions, candidate, 0.10);
    double caller_order_total = 0.0;
    double reversed_caller_order_total = 0.0;
    for (const double logit : residual.policy_logits) {
        caller_order_total += logit;
    }
    for (const double logit :
         reversed_residual.policy_logits) {
        reversed_caller_order_total += logit;
    }
    CHECK(std::bit_cast<std::uint64_t>(
              caller_order_total) !=
          std::bit_cast<std::uint64_t>(
              reversed_caller_order_total));
    CHECK(std::bit_cast<std::uint64_t>(
              residual.mean_legal_logit) ==
          std::bit_cast<std::uint64_t>(
              reversed_residual.mean_legal_logit));
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        const std::size_t reversed_index =
            actions.size() - index - 1;
        CHECK(actions[index] ==
              reversed_actions[reversed_index]);
        CHECK(std::bit_cast<std::uint64_t>(
                  residual.policy_logits[index]) ==
              std::bit_cast<std::uint64_t>(
                  reversed_residual
                      .policy_logits[reversed_index]));
        CHECK(std::bit_cast<std::uint64_t>(
                  residual
                      .centered_policy_logits[index]) ==
              std::bit_cast<std::uint64_t>(
                  reversed_residual
                      .centered_policy_logits[
                          reversed_index]));
        CHECK(std::bit_cast<std::uint64_t>(
                  residual.residuals[index]) ==
              std::bit_cast<std::uint64_t>(
                  reversed_residual
                      .residuals[reversed_index]));
    }

    const auto hidden = hidden_repartition(state, 0);
    const auto hidden_residual =
        old_school::diagnose_learned_value_priority_residual(
            hidden, 0, true, old_school::TurnPhase::FirstMain, 1,
            actions, candidate, 0.10);
    CHECK(hidden_residual.policy_logits ==
          residual.policy_logits);
    CHECK(hidden_residual.centered_policy_logits ==
          residual.centered_policy_logits);
    CHECK(hidden_residual.residuals == residual.residuals);

    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto scored =
        old_school::diagnose_learned_value_priority(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, candidate, 0,
            0xADAA050ULL, 0.0, 0.10);
    CHECK(scored.policy_logits == residual.policy_logits);
    CHECK(scored.centered_policy_logits ==
          residual.centered_policy_logits);
    CHECK(scored.priority_residuals == residual.residuals);
    CHECK(scored.scores.size() == scored.base_scores.size());
    for (std::size_t index = 0;
         index < scored.scores.size(); ++index) {
        CHECK(scored.scores[index] ==
              scored.base_scores[index] +
                  scored.priority_residuals[index]);
    }

    const auto behavior_cross_entropy =
        [&](const std::shared_ptr<const old_school::LearnedModel>&
                model) {
            const auto logits =
                old_school::learned_policy_head_logits(
                    options,
                    old_school::LearnedPolicyDecisionKind::Priority,
                    model);
            const double mean =
                std::accumulate(
                    logits.begin(), logits.end(), 0.0) /
                static_cast<double>(logits.size());
            std::vector<double> softmax(logits.size());
            double maximum =
                -std::numeric_limits<double>::infinity();
            for (std::size_t index = 0;
                 index < logits.size(); ++index) {
                softmax[index] =
                    (base_scores[index] +
                     0.10 * std::tanh(logits[index] - mean)) /
                    0.10;
                maximum = std::max(maximum, softmax[index]);
            }
            double total = 0.0;
            for (double& value : softmax) {
                value = std::exp(value - maximum);
                total += value;
            }
            double loss = 0.0;
            for (std::size_t index = 0;
                 index < softmax.size(); ++index) {
                const double behavior =
                    0.90 * softmax[index] / total +
                    0.10 /
                        static_cast<double>(softmax.size());
                if (targets[index] != 0.0) {
                    loss -= targets[index] *
                            std::log(behavior);
                }
            }
            return loss;
        };
    CHECK(behavior_cross_entropy(candidate) <
          behavior_cross_entropy(parent));
}

TEST(value_priority_head_ce_uses_behavior_mixture_and_rejects_bad_targets) {
    const auto parent = small_value_model();
    const auto state =
        old_school::white_lock_plan_diagnostic_state();
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() >= 2);
    std::vector<std::vector<double>> options;
    for (std::size_t index = 0; index < 2; ++index) {
        options.push_back(
            old_school::learned_priority_policy_features(
                state, 0, actions[index], true,
                old_school::TurnPhase::FirstMain, 1));
    }

    const auto parent_logits =
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Priority,
            parent);
    const double parent_logit_mean =
        std::accumulate(
            parent_logits.begin(), parent_logits.end(), 0.0) /
        static_cast<double>(parent_logits.size());
    std::vector<double> moderate_scores = {0.55, 0.45};
    for (std::size_t index = 0;
         index < moderate_scores.size(); ++index) {
        moderate_scores[index] +=
            0.10 *
            std::tanh(
                parent_logits[index] - parent_logit_mean);
    }
    const old_school::LearnedValuePriorityTrainingExample
        moderate_stationary = {
            .options = options,
            .base_scores = {0.55, 0.45},
            .target_probabilities =
                old_school::learned_iteration::
                    p16_exploration_distribution(
                        moderate_scores),
            .weight = 1.0,
        };
    const auto moderate_unchanged =
        old_school::update_learned_value_priority_head(
            parent, {moderate_stationary}, {});
    CHECK(old_school::learned_model_fingerprint(
              moderate_unchanged) ==
          old_school::learned_model_fingerprint(parent));

    // exp((-100 - 100) / .10) underflows to exactly zero, so the
    // parent behavior is exactly {.9 * 1 + .1 / 2, .1 / 2}. A bare
    // softmax CE would update this target; CE against the deployed
    // 90/10 behavior mixture must leave the model bit-identical.
    const double uniform = 0.10 / 2.0;
    const old_school::LearnedValuePriorityTrainingExample stationary = {
        .options = options,
        .base_scores = {100.0, -100.0},
        .target_probabilities = {
            0.90 * 1.0 + uniform,
            0.90 * 0.0 + uniform,
        },
        .weight = 1.0,
    };
    const auto unchanged =
        old_school::update_learned_value_priority_head(
            parent, {stationary}, {});
    CHECK(old_school::learned_model_fingerprint(unchanged) ==
          old_school::learned_model_fingerprint(parent));

    const auto rejects_invalid =
        [](const std::function<void()>& operation) {
            try {
                operation();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_priority_head(
                nullptr, {stationary}, {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_priority_head(
                small_actor_model(), {stationary}, {}));
    }));

    auto malformed = stationary;
    malformed.base_scores.pop_back();
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_priority_head(
                parent, {malformed}, {}));
    }));
    malformed = stationary;
    malformed.options[0].pop_back();
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_priority_head(
                parent, {malformed}, {}));
    }));
    malformed = stationary;
    malformed.target_probabilities = {0.4, 0.4};
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_priority_head(
                parent, {malformed}, {}));
    }));
    malformed = stationary;
    malformed.target_probabilities[0] =
        std::numeric_limits<double>::quiet_NaN();
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_priority_head(
                parent, {malformed}, {}));
    }));
    CHECK(rejects_invalid([&] {
        static_cast<void>(
            old_school::update_learned_value_priority_head(
                parent, {stationary},
                {
                    .batch_size = 64,
                    .epochs = 0,
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
          "old-school-value-g8-mix50-v3-t800-s424242.bin");

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
          "build/model-cache/old-school-value-g8-v3-t800-s424242.bin");
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

TEST(learned_value_policy_family_balances_retains_and_isolates_p1) {
    const old_school::LearnedValuePolicyFamilyConfig defaults;
    CHECK(defaults.generations == 1);
    CHECK(defaults.search_worlds == 8);
    CHECK(defaults.max_roots_per_actor_game == 32);
    CHECK(defaults.max_game_turns == 500);
    CHECK(defaults.collection_threads == 4);
    CHECK(defaults.residual_weight == 0.10);
    CHECK(defaults.td_lambda == 0.90);
    CHECK(defaults.optimizer.batch_size == 64);
    CHECK(defaults.optimizer.epochs == 8);
    CHECK(defaults.optimizer.learning_rate == 0.001);
    CHECK(defaults.optimizer.residual_weight == 0.10);
    CHECK(defaults.optimizer.policy_temperature == 0.10);
    CHECK(defaults.capacity_diagnostic_optimizers.empty());
    CHECK(!defaults.compute_rootwise_oracle);

    const auto parent = small_value_model();
    const std::string parent_fingerprint =
        old_school::learned_model_fingerprint(parent);
    const auto parent_components =
        old_school::learned_model_component_fingerprints(parent);
    constexpr std::uint64_t kRootSeed = 0x5011C1EULL;
    old_school::LearnedValuePolicyFamilyConfig fast;
    fast.search_worlds = 1;
    fast.max_roots_per_actor_game = 1;
    fast.max_game_turns = 8;
    fast.collection_threads = 1;
    fast.optimizer.epochs = 1;

    const auto family =
        old_school::train_learned_value_policy_family(
            parent, kRootSeed, fast);
    CHECK(family.checkpoints.size() == 2);
    CHECK(family.checkpoints.front().get() == parent.get());
    CHECK(family.reports.size() == 1);
    const auto& report = family.reports.front();
    const auto& candidate = family.checkpoints.back();
    const auto expected =
        old_school::learned_iteration::balanced_schedule(
            kRootSeed, 1);

    CHECK(!report.canonical_recipe);
    CHECK(report.search_worlds == 1);
    CHECK(report.rollouts_per_world == 1);
    CHECK(report.search_horizon_turns == 4);
    CHECK(report.max_roots_per_actor_game == 1);
    CHECK(report.max_game_turns == 8);
    CHECK(report.collection_threads == 1);
    CHECK(report.residual_weight == 0.10);
    CHECK(report.td_lambda == 0.90);
    CHECK(report.optimizer.epochs == 1);
    CHECK(report.optimizer.seed ==
          old_school::learned_iteration::derive_seed(
              kRootSeed,
              old_school::learned_iteration::
                  SeedDomain::PolicyFit,
              1, 0));
    CHECK(report.games.size() == expected.size());
    CHECK(report.games.size() == 40);
    CHECK(report.rootless_actor_games == 0);
    CHECK(report.parent_fingerprint == parent_fingerprint);
    CHECK(report.candidate_fingerprint ==
          old_school::learned_model_fingerprint(candidate));
    CHECK(report.parent_components == parent_components);
    CHECK(report.candidate_components.critic ==
          parent_components.critic);
    CHECK(report.candidate_components.priority !=
          parent_components.priority);
    CHECK(report.candidate_components.attack ==
          parent_components.attack);
    CHECK(report.candidate_components.block ==
          parent_components.block);
    CHECK(report.candidate_components.damage_order ==
          parent_components.damage_order);
    CHECK(report.replay_generations == 1);
    CHECK(report.replay_examples ==
          report.retained_priority_roots);
    CHECK(report.retained_priority_roots == 80);
    CHECK(std::abs(report.policy_weight - 80.0) < 1.0e-12);
    CHECK(std::abs(report.minimum_target_sum - 1.0) <
          1.0e-12);
    CHECK(std::abs(report.maximum_target_sum - 1.0) <
          1.0e-12);
    CHECK(report.rollout_evaluations ==
          report.raw_legal_options);

    std::size_t raw_roots = 0;
    std::size_t raw_options = 0;
    std::size_t retained_roots = 0;
    std::size_t rollout_evaluations = 0;
    double policy_weight = 0.0;
    for (std::size_t index = 0;
         index < report.games.size(); ++index) {
        const auto& actual = report.games[index];
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
            CHECK(actual.raw_priority_roots[player] > 0);
            CHECK(actual.raw_legal_options[player] >=
                  2 * actual.raw_priority_roots[player]);
            CHECK(actual.rollout_evaluations[player] ==
                  actual.raw_legal_options[player]);
            const auto retained =
                old_school::learned_iteration::
                    evenly_spaced_retained_indices(
                        actual.raw_priority_roots[player], 1);
            CHECK(actual.retained_ordinals[player] ==
                  retained);
            CHECK(actual.retained_priority_roots[player] ==
                  1);
            CHECK(actual.policy_weight_sums[player] == 1.0);
            raw_roots += actual.raw_priority_roots[player];
            raw_options += actual.raw_legal_options[player];
            retained_roots +=
                actual.retained_priority_roots[player];
            rollout_evaluations +=
                actual.rollout_evaluations[player];
            policy_weight +=
                actual.policy_weight_sums[player];
        }
    }
    CHECK(raw_roots == report.raw_priority_roots);
    CHECK(raw_options == report.raw_legal_options);
    CHECK(retained_roots == report.retained_priority_roots);
    CHECK(rollout_evaluations ==
          report.rollout_evaluations);
    CHECK(policy_weight == report.policy_weight);

    for (const auto& deck : report.decks) {
        CHECK(deck.seat_games == 16);
        CHECK(deck.seat_zero_games == 8);
        CHECK(deck.starting_games == 8);
        CHECK(deck.rootless_actor_games == 0);
        CHECK(deck.retained_priority_roots == 16);
        CHECK(deck.rollout_evaluations ==
              deck.raw_legal_options);
        CHECK(deck.policy_weight == 16.0);
    }
    CHECK(report.mechanism.observation_count == 80);
    CHECK(report.mechanism.total_weight == 80.0);
    CHECK(report.mechanism.residual_option_weight == 80.0);
    CHECK(report.mechanism.positive_advantage_weight +
              report.mechanism.negative_advantage_weight +
              report.mechanism.zero_advantage_weight ==
          80.0);
    CHECK(report.mechanism.correct_signed_movement_weight <=
          report.mechanism
              .eligible_signed_movement_weight);
    CHECK(report.mechanism.changed_argmax_weight <= 80.0);
    CHECK(report.mechanism.saturated_residual_weight <=
          report.mechanism.residual_option_weight);
    CHECK(report.capacity_diagnostics.empty());
    CHECK(!report.rootwise_oracle.has_value());
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(old_school::learned_model_component_fingerprints(parent) ==
          parent_components);

    auto parallel_fast = fast;
    parallel_fast.collection_threads = 4;
    auto harder_optimizer = fast.optimizer;
    harder_optimizer.epochs = 4;
    parallel_fast.capacity_diagnostic_optimizers = {
        fast.optimizer,
        harder_optimizer,
        harder_optimizer,
    };
    parallel_fast.compute_rootwise_oracle = true;
    const auto repeated =
        old_school::train_learned_value_policy_family(
            parent, kRootSeed, parallel_fast);
    CHECK(repeated.reports.front().collection_threads == 4);
    const auto& capacity =
        repeated.reports.front().capacity_diagnostics;
    CHECK(capacity.size() == 3);
    CHECK(capacity[0].optimizer.epochs == 1);
    CHECK(capacity[0].optimizer.learning_rate == 0.001);
    CHECK(capacity[0].optimizer.seed == report.optimizer.seed);
    CHECK(capacity[0].parent_fingerprint ==
          report.parent_fingerprint);
    CHECK(capacity[0].candidate_fingerprint ==
          report.candidate_fingerprint);
    CHECK(capacity[0].parent_components ==
          report.parent_components);
    CHECK(capacity[0].candidate_components ==
          report.candidate_components);
    CHECK(capacity[0].mechanism == report.mechanism);
    CHECK(capacity[1] == capacity[2]);
    CHECK(capacity[1].optimizer.epochs == 4);
    CHECK(capacity[1].parent_fingerprint ==
          report.parent_fingerprint);
    CHECK(capacity[1].parent_components ==
          report.parent_components);
    CHECK(capacity[1].candidate_components.critic ==
          report.parent_components.critic);
    CHECK(capacity[1].candidate_components.attack ==
          report.parent_components.attack);
    CHECK(capacity[1].candidate_components.block ==
          report.parent_components.block);
    CHECK(capacity[1].candidate_components.damage_order ==
          report.parent_components.damage_order);
    CHECK(capacity[1].candidate_components.priority !=
          report.parent_components.priority);
    CHECK(capacity[1].candidate_fingerprint !=
          report.parent_fingerprint);
    CHECK(capacity[1].mechanism.observation_count == 80);
    CHECK(capacity[1].mechanism.total_weight == 80.0);
    for (const auto& deck_mechanism :
         capacity[1].mechanisms_by_deck) {
        CHECK(deck_mechanism.observation_count == 16);
        CHECK(deck_mechanism.total_weight == 16.0);
        CHECK(deck_mechanism.correct_signed_movement_weight <=
              deck_mechanism
                  .eligible_signed_movement_weight);
        CHECK(deck_mechanism.saturated_residual_weight <=
                  deck_mechanism.residual_option_weight);
    }
    CHECK(repeated.reports.front().rootwise_oracle.has_value());
    const auto& oracle =
        *repeated.reports.front().rootwise_oracle;
    CHECK(oracle.observation_count == 80);
    CHECK(oracle.total_weight == 80.0);
    CHECK(std::abs(
              oracle.parent_kl -
              repeated.reports.front().mechanism.parent_kl) <
          1.0e-12);
    CHECK(oracle.reduction_defined);
    CHECK(oracle.full_range.numerical_best_kl <=
          oracle.parent_kl);
    CHECK(oracle.zero_saturation.numerical_best_kl <=
          oracle.parent_kl);
    CHECK(oracle.full_range.achievable_reduction_fraction <=
          oracle.full_range.certified_reduction_upper_bound);
    CHECK(
        oracle.zero_saturation.achievable_reduction_fraction <=
        oracle.zero_saturation.certified_reduction_upper_bound);
    CHECK(oracle.zero_saturation
              .maximum_abs_squashed_residual <
          old_school::learned_iteration::
              kP16ResidualSaturationThreshold);
    auto serial_reports = family.reports;
    auto parallel_reports = repeated.reports;
    for (auto& generation_report : serial_reports) {
        generation_report.collection_threads = 0;
    }
    for (auto& generation_report : parallel_reports) {
        generation_report.collection_threads = 0;
        generation_report.capacity_diagnostics.clear();
        generation_report.rootwise_oracle.reset();
    }
    CHECK(parallel_reports == serial_reports);
    CHECK(repeated.checkpoints.size() ==
          family.checkpoints.size());
    CHECK(repeated.checkpoints.back().get() !=
          candidate.get());
    CHECK(old_school::learned_model_fingerprint(
              repeated.checkpoints.back()) ==
          report.candidate_fingerprint);
    CHECK(old_school::learned_model_fingerprint(parent) ==
          parent_fingerprint);

    for (const std::size_t invalid_threads :
         std::array<std::size_t, 2>{0, 41}) {
        auto invalid = fast;
        invalid.collection_threads = invalid_threads;
        CHECK(throws_with_text(
            [&] {
                static_cast<void>(
                    old_school::train_learned_value_policy_family(
                        parent, kRootSeed, invalid));
            },
            "configuration"));
    }

    auto mismatched = fast;
    mismatched.optimizer.residual_weight = 0.20;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_value_policy_family(
                    parent, kRootSeed, mismatched));
        },
        "configuration"));
    auto invalid_diagnostic = fast;
    auto invalid_diagnostic_optimizer = fast.optimizer;
    invalid_diagnostic_optimizer.epochs = 0;
    invalid_diagnostic.capacity_diagnostic_optimizers = {
        invalid_diagnostic_optimizer,
    };
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_value_policy_family(
                    parent, kRootSeed, invalid_diagnostic));
        },
        "configuration"));
    auto invalid_oracle = fast;
    invalid_oracle.generations = 2;
    invalid_oracle.compute_rootwise_oracle = true;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_value_policy_family(
                    parent, kRootSeed, invalid_oracle));
        },
        "configuration"));
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::train_learned_value_policy_family(
                    small_actor_model(), kRootSeed, fast));
        },
        "Value parent"));
}

TEST(learned_value_policy_priority_sampling_is_real_and_hidden_safe) {
    auto fixture = determinization_fixture();
    auto& player = fixture.state.players[0];
    const auto plains =
        std::find(
            player.hand.begin(), player.hand.end(),
            old_school::CardId::Plains);
    CHECK(plains != player.hand.end());
    player.hand.erase(plains);
    player.lands.push_back({
        .card = old_school::CardId::Plains,
        .tapped = false,
    });
    player.lands.front().tapped = false;
    player.artifacts.front().tapped = false;

    constexpr std::size_t kWorlds = 1;
    constexpr std::uint64_t kGeneration = 1;
    constexpr std::size_t kScheduleIndex = 3;
    std::optional<
        old_school::LearnedValuePolicyPriorityDiagnostic>
        sampled_non_argmax;
    std::uint64_t selected_root_seed = 0;
    for (std::uint64_t root_seed = 1;
         root_seed <= 32; ++root_seed) {
        auto diagnostic =
            old_school::diagnose_learned_value_policy_priority(
                fixture.state, fixture.decks, 0, false,
                old_school::TurnPhase::SecondMain, 0,
                small_value_model(), root_seed, kGeneration,
                kScheduleIndex, kWorlds);
        const double best =
            *std::max_element(
                diagnostic.combined_scores.begin(),
                diagnostic.combined_scores.end());
        if (diagnostic.combined_scores[diagnostic.chosen] !=
            best) {
            selected_root_seed = root_seed;
            sampled_non_argmax = std::move(diagnostic);
            break;
        }
    }
    CHECK(sampled_non_argmax.has_value());
    const auto& original = *sampled_non_argmax;
    CHECK(original.actions.size() >= 2);
    CHECK(original.actions.size() ==
          original.base_scores.size());
    CHECK(original.actions.size() ==
          original.policy_logits.size());
    CHECK(original.actions.size() ==
          original.centered_policy_logits.size());
    CHECK(original.actions.size() ==
          original.residuals.size());
    CHECK(original.actions.size() ==
          original.combined_scores.size());
    CHECK(original.actions.size() ==
          original.behavior_probabilities.size());
    CHECK(original.selected_action ==
          original.actions[original.chosen]);
    CHECK(original.transition_applied);
    CHECK(original.rollout_evaluations ==
          original.actions.size() * kWorlds);
    CHECK(original.search_seed != original.choice_seed);
    CHECK(std::abs(
              std::accumulate(
                  original.behavior_probabilities.begin(),
                  original.behavior_probabilities.end(), 0.0) -
              1.0) <
          1.0e-12);

    std::mt19937_64 choice_random(original.choice_seed);
    std::discrete_distribution<std::size_t> sample(
        original.behavior_probabilities.begin(),
        original.behavior_probabilities.end());
    CHECK(sample(choice_random) == original.chosen);

    const auto hidden =
        old_school::diagnose_learned_value_policy_priority(
            hidden_repartition(fixture.state, 0),
            fixture.decks, 0, false,
            old_school::TurnPhase::SecondMain, 0,
            small_value_model(), selected_root_seed,
            kGeneration, kScheduleIndex, kWorlds);
    CHECK(hidden.actions == original.actions);
    CHECK(hidden.base_scores == original.base_scores);
    CHECK(hidden.policy_logits == original.policy_logits);
    CHECK(hidden.centered_policy_logits ==
          original.centered_policy_logits);
    CHECK(hidden.residuals == original.residuals);
    CHECK(hidden.combined_scores ==
          original.combined_scores);
    CHECK(hidden.behavior_probabilities ==
          original.behavior_probabilities);
    CHECK(hidden.search_seed == original.search_seed);
    CHECK(hidden.choice_seed == original.choice_seed);
    CHECK(hidden.chosen == original.chosen);
    CHECK(hidden.selected_action == original.selected_action);
    CHECK(hidden.transition_applied ==
          original.transition_applied);
    CHECK(hidden.pass_result == original.pass_result);
    CHECK(hidden.terminal_result == original.terminal_result);
    CHECK(old_school::observe_game_state(
              hidden.final_state, 0) ==
          old_school::observe_game_state(
              original.final_state, 0));
    CHECK(old_school::learned_iteration::p16_all_action_target(
              hidden.behavior_probabilities, hidden.chosen, 0.25) ==
          old_school::learned_iteration::p16_all_action_target(
              original.behavior_probabilities,
              original.chosen, 0.25));
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
    CHECK(actor_config.terminal_utility_mode ==
          old_school::LearnedTerminalUtilityMode::
              ExactOutcome);
    const auto actor_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1, actions,
            small_actor_model(), actor_config);
    CHECK(actor_samples.rollout_evaluations == 4);
    CHECK(actor_samples.terminal_evaluations == 4);
    CHECK(actor_samples.bootstrapped_evaluations == 0);
    CHECK(actor_samples.terminal_evaluations +
              actor_samples.bootstrapped_evaluations ==
          actor_samples.rollout_evaluations);
    CHECK(actor_samples.q_samples.size() == 1);
    CHECK(actor_samples.q_samples[0] ==
          std::vector<double>({0.0, 0.0, 0.0, 0.0}));

    old_school::LearnedSearchConfig aligned_config =
        actor_config;
    aligned_config.worlds = 1;
    aligned_config.rollouts_per_world = 1;
    aligned_config.terminal_utility_mode =
        old_school::LearnedTerminalUtilityMode::
            C16DiscountedAbsoluteTurn;
    const auto aligned_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1, actions,
            small_actor_model(), aligned_config);
    old_school::GameResult aligned_loss;
    aligned_loss.winner = 1;
    aligned_loss.turns = state.turn_number;
    const double aligned_loss_target =
        old_school::learned_discounted_terminal_target(
            aligned_loss, 0);
    CHECK(std::bit_cast<std::uint64_t>(
              aligned_samples.q_samples[0][0]) ==
          std::bit_cast<std::uint64_t>(
              aligned_loss_target));
    CHECK(aligned_samples.q_samples[0][0] > 0.0);
    CHECK(aligned_samples.q_samples[0][0] < 0.5);
    CHECK(aligned_samples.terminal_evaluations == 1);
    CHECK(aligned_samples.bootstrapped_evaluations == 0);

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
    CHECK(value_samples.terminal_evaluations == 1);
    CHECK(value_samples.bootstrapped_evaluations == 0);
    CHECK(value_samples.terminal_evaluations +
              value_samples.bootstrapped_evaluations ==
          value_samples.rollout_evaluations);
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
    const auto horizon_zero_samples =
        old_school::learned_priority_action_samples(
            boundary, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_zero);
    const double h0 = horizon_zero_samples.q_samples[0][0];
    CHECK(h0 > 0.0 && h0 < 1.0);
    CHECK(horizon_zero_samples.terminal_evaluations == 0);
    CHECK(horizon_zero_samples.bootstrapped_evaluations == 1);
    CHECK(horizon_zero_samples.terminal_evaluations +
              horizon_zero_samples.bootstrapped_evaluations ==
          horizon_zero_samples.rollout_evaluations);

    old_school::LearnedSearchConfig horizon_one = horizon_zero;
    horizon_one.horizon_turns = 1;
    const auto horizon_one_samples =
        old_school::learned_priority_action_samples(
            boundary, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_one);
    const double h1 = horizon_one_samples.q_samples[0][0];
    // H1 bootstraps after turn two cleanup. Preparing turn three here would
    // make player zero draw from its empty library and return exactly zero.
    CHECK(h1 > 0.0 && h1 < 1.0);
    CHECK(horizon_one_samples.terminal_evaluations == 0);
    CHECK(horizon_one_samples.bootstrapped_evaluations == 1);

    old_school::LearnedSearchConfig horizon_two = horizon_zero;
    horizon_two.horizon_turns = 2;
    const auto horizon_two_samples =
        old_school::learned_priority_action_samples(
            boundary, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_two);
    CHECK(horizon_two_samples.q_samples[0][0] == 0.0);
    CHECK(horizon_two_samples.terminal_evaluations == 1);
    CHECK(horizon_two_samples.bootstrapped_evaluations == 0);
    CHECK(horizon_two_samples.terminal_evaluations +
              horizon_two_samples.bootstrapped_evaluations ==
          horizon_two_samples.rollout_evaluations);

    auto empty_next_library = boundary;
    empty_next_library.players[1].graveyard.push_back(
        empty_next_library.players[1].library.back());
    empty_next_library.players[1].library.clear();
    const auto deck_out_samples =
        old_school::learned_priority_action_samples(
            empty_next_library, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_zero);
    const double deck_out = deck_out_samples.q_samples[0][0];
    CHECK(deck_out == 1.0);
    CHECK(deck_out_samples.terminal_evaluations == 1);
    CHECK(deck_out_samples.bootstrapped_evaluations == 0);
}

TEST(priority_h0_boundary_capture_is_bit_inert_ordered_and_thread_exact) {
    const old_school::GameState state =
        old_school::white_lock_plan_diagnostic_state();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto actions =
        old_school::legal_priority_actions(state, 0, true);
    CHECK(actions.size() == 4);

    old_school::LearnedSearchConfig default_off = {
        .seed = 0xAC1B0A0D4ULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
        .evaluation_threads = 4,
    };
    CHECK(!default_off.capture_priority_h0_boundaries);
    const auto model = small_actor_model();
    const auto before =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, default_off);

    auto capture_parallel = default_off;
    capture_parallel.capture_priority_h0_boundaries = true;
    const auto captured =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, capture_parallel);
    const auto subsequent_default =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, default_off);

    const auto check_scoring_bits =
        [](const old_school::LearnedActionSamples& left,
           const old_school::LearnedActionSamples& right) {
            CHECK(left.q_samples.size() ==
                  right.q_samples.size());
            for (std::size_t action = 0;
                 action < left.q_samples.size(); ++action) {
                CHECK(left.q_samples[action].size() ==
                      right.q_samples[action].size());
                for (std::size_t sample = 0;
                     sample < left.q_samples[action].size();
                     ++sample) {
                    CHECK(std::bit_cast<std::uint64_t>(
                              left.q_samples[action][sample]) ==
                          std::bit_cast<std::uint64_t>(
                              right.q_samples[action][sample]));
                }
            }
            CHECK(left.exact_priority_aggregate_scores.size() ==
                  right.exact_priority_aggregate_scores.size());
            for (std::size_t action = 0;
                 action <
                 left.exact_priority_aggregate_scores.size();
                 ++action) {
                CHECK(std::bit_cast<std::uint64_t>(
                          left.exact_priority_aggregate_scores[
                              action]) ==
                      std::bit_cast<std::uint64_t>(
                          right.exact_priority_aggregate_scores[
                              action]));
            }
            CHECK(left.sampled_worlds ==
                  right.sampled_worlds);
            CHECK(left.rollout_evaluations ==
                  right.rollout_evaluations);
            CHECK(left.terminal_evaluations ==
                  right.terminal_evaluations);
            CHECK(left.bootstrapped_evaluations ==
                  right.bootstrapped_evaluations);
        };
    auto explicit_exact_config = default_off;
    explicit_exact_config.terminal_utility_mode =
        old_school::LearnedTerminalUtilityMode::
            ExactOutcome;
    const auto explicit_exact =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, explicit_exact_config);
    check_scoring_bits(before, captured);
    check_scoring_bits(before, subsequent_default);
    check_scoring_bits(before, explicit_exact);
    CHECK(explicit_exact.priority_h0_boundaries.empty());
    CHECK(before.priority_h0_boundaries.empty());
    CHECK(subsequent_default.priority_h0_boundaries.empty());
    CHECK(captured.priority_h0_boundaries.size() ==
          actions.size());
    CHECK(captured.rollout_evaluations ==
          actions.size() * 4);
    CHECK(captured.terminal_evaluations +
              captured.bootstrapped_evaluations ==
          captured.rollout_evaluations);
    for (std::size_t action = 0; action < actions.size();
         ++action) {
        CHECK(captured.priority_h0_boundaries[action].size() ==
              4);
        for (std::size_t sample = 0; sample < 4; ++sample) {
            CHECK(std::bit_cast<std::uint64_t>(
                      captured.priority_h0_boundaries[action]
                                                     [sample]
                                                         .continuation_score) ==
                  std::bit_cast<std::uint64_t>(
                      captured.q_samples[action][sample]));
        }
    }

    auto capture_serial = capture_parallel;
    capture_serial.evaluation_threads = 1;
    const auto serial =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, capture_serial);
    check_scoring_bits(serial, captured);
    CHECK(serial.priority_h0_boundaries ==
          captured.priority_h0_boundaries);

    auto reversed_actions = actions;
    std::reverse(
        reversed_actions.begin(), reversed_actions.end());
    const auto reversed =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0,
            reversed_actions, model, capture_parallel);
    CHECK(reversed.priority_h0_boundaries.size() ==
          captured.priority_h0_boundaries.size());
    for (std::size_t action = 0; action < actions.size();
         ++action) {
        const std::size_t original =
            actions.size() - action - 1;
        CHECK(reversed.priority_h0_boundaries[action] ==
              captured.priority_h0_boundaries[original]);
        CHECK(reversed.q_samples[action] ==
              captured.q_samples[original]);
        CHECK(std::bit_cast<std::uint64_t>(
                  reversed.exact_priority_aggregate_scores[
                      action]) ==
              std::bit_cast<std::uint64_t>(
                  captured.exact_priority_aggregate_scores[
                      original]));
    }
}

TEST(priority_h0_boundary_prepares_exact_next_turn_without_acting) {
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::Forest,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
        },
    };
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 4;
    state.players[0].life = 19;
    state.players[0].graveyard = decks[0];
    state.players[1].life = 7;
    state.players[1].library = decks[1];

    const auto model = context_sensitive_value_model();
    const old_school::LearnedSearchConfig capture = {
        .seed = 0xAC1B0A0D5ULL,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = false,
        .evaluation_threads = 1,
        .capture_priority_h0_boundaries = true,
    };
    const auto samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            {old_school::PriorityAction::pass()},
            model, capture);
    CHECK(samples.priority_h0_boundaries.size() == 1);
    CHECK(samples.priority_h0_boundaries[0].size() == 1);
    const auto& boundary =
        samples.priority_h0_boundaries[0][0];
    CHECK(!boundary.terminal);
    CHECK(boundary.state.turn_number == 5);
    CHECK(boundary.state.active_player == 1);
    CHECK(boundary.state.active_player != 0);
    CHECK(boundary.state.players[1].library.empty());
    CHECK(boundary.state.players[1].hand ==
          std::vector<old_school::CardId>{
              old_school::CardId::Mountain});
    CHECK(boundary.state.stats[1].cards_drawn == 1);
    CHECK(boundary.state.players[1].lands.empty());
    CHECK(!boundary.state.players[1].land_played_this_turn);
    CHECK(boundary.state.stats[1].decisions == 0);
    const old_school::LearnedDecisionContext expected_context = {
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 1,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    CHECK(boundary.context == expected_context);

    const double root_perspective =
        old_school::learned_contextual_critic_value(
            boundary.state, 0, boundary.context, model);
    const double active_perspective =
        old_school::learned_contextual_critic_value(
            boundary.state, 1, boundary.context, model);
    CHECK(std::bit_cast<std::uint64_t>(
              boundary.continuation_score) ==
          std::bit_cast<std::uint64_t>(root_perspective));
    CHECK(std::bit_cast<std::uint64_t>(
              boundary.continuation_score) !=
          std::bit_cast<std::uint64_t>(active_perspective));
    CHECK(std::bit_cast<std::uint64_t>(
              samples.q_samples[0][0]) ==
          std::bit_cast<std::uint64_t>(
              boundary.continuation_score));
    CHECK(samples.terminal_evaluations == 0);
    CHECK(samples.bootstrapped_evaluations == 1);

    auto first_turn_skip = old_school::GameState{};
    first_turn_skip.active_player = 1;
    first_turn_skip.starting_player = 0;
    first_turn_skip.turn_number = 0;
    first_turn_skip.players[0].library = decks[0];
    first_turn_skip.players[1].graveyard = decks[1];
    const auto skipped =
        old_school::learned_priority_action_samples(
            first_turn_skip, decks, 1, true,
            old_school::TurnPhase::SecondMain, 1,
            {old_school::PriorityAction::pass()},
            model, capture);
    const auto& skipped_boundary =
        skipped.priority_h0_boundaries[0][0];
    CHECK(!skipped_boundary.terminal);
    CHECK(skipped_boundary.state.turn_number == 1);
    CHECK(skipped_boundary.state.active_player == 0);
    CHECK(skipped_boundary.state.players[0].hand.empty());
    CHECK(skipped_boundary.state.players[0].library ==
          decks[0]);
    CHECK(skipped_boundary.state.stats[0].cards_drawn == 0);

    auto extra_turn = old_school::GameState{};
    extra_turn.active_player = 0;
    extra_turn.starting_player = 0;
    extra_turn.turn_number = 4;
    extra_turn.extra_turns_pending[0] = 1;
    extra_turn.players[0].library = decks[0];
    extra_turn.players[1].graveyard = decks[1];
    const auto extra =
        old_school::learned_priority_action_samples(
            extra_turn, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            {old_school::PriorityAction::pass()},
            model, capture);
    const auto& extra_boundary =
        extra.priority_h0_boundaries[0][0];
    CHECK(!extra_boundary.terminal);
    CHECK(extra_boundary.state.turn_number == 5);
    CHECK(extra_boundary.state.active_player == 0);
    CHECK(extra_boundary.state.extra_turns_pending[0] == 0);
    CHECK(extra_boundary.state.players[0].library.empty());
    CHECK(extra_boundary.state.players[0].hand ==
          std::vector<old_school::CardId>{
              old_school::CardId::Forest});
    CHECK(extra_boundary.state.stats[0].cards_drawn == 1);
    CHECK(extra_boundary.state.players[0].lands.empty());
    CHECK(extra_boundary.state.stats[0].decisions == 0);

    auto horizon_eight = capture;
    horizon_eight.horizon_turns = 8;
    const auto long_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            {old_school::PriorityAction::pass()},
            model, horizon_eight);
    CHECK(long_samples.q_samples.size() == 1);
    CHECK(long_samples.q_samples[0].size() == 1);
    CHECK(long_samples.priority_h0_boundaries.size() == 1);
    CHECK(long_samples.priority_h0_boundaries[0].size() == 1);
    const auto& long_boundary =
        long_samples.priority_h0_boundaries[0][0];
    CHECK(!long_boundary.terminal);
    CHECK(long_boundary.state == boundary.state);
    CHECK(long_boundary.context == boundary.context);
    CHECK(std::bit_cast<std::uint64_t>(
              long_boundary.continuation_score) ==
          std::bit_cast<std::uint64_t>(
              boundary.continuation_score));
    CHECK(long_samples.q_samples[0][0] == 0.0);
    CHECK(std::bit_cast<std::uint64_t>(
              long_boundary.continuation_score) !=
          std::bit_cast<std::uint64_t>(
              long_samples.q_samples[0][0]));
    CHECK(long_samples.terminal_evaluations == 1);
    CHECK(long_samples.bootstrapped_evaluations == 0);

    const auto repeated_long_samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            {old_school::PriorityAction::pass()},
            model, horizon_eight);
    CHECK(repeated_long_samples.q_samples ==
          long_samples.q_samples);
    CHECK(repeated_long_samples.terminal_evaluation_flags ==
          long_samples.terminal_evaluation_flags);
    CHECK(repeated_long_samples.priority_h0_boundaries ==
          long_samples.priority_h0_boundaries);
    CHECK(repeated_long_samples.rollout_evaluations ==
          long_samples.rollout_evaluations);
    CHECK(repeated_long_samples.terminal_evaluations ==
          long_samples.terminal_evaluations);
    CHECK(repeated_long_samples.bootstrapped_evaluations ==
          long_samples.bootstrapped_evaluations);
}

TEST(priority_h0_boundary_preserves_early_and_next_draw_terminals) {
    const auto model = small_value_model();
    const old_school::LearnedSearchConfig capture = {
        .seed = 0xAC1B0A0D6ULL,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = false,
        .evaluation_threads = 1,
        .capture_priority_h0_boundaries = true,
    };

    const std::array<std::vector<old_school::CardId>, 2>
        lethal_decks = {
            std::vector<old_school::CardId>{
                old_school::CardId::Forest,
            },
            std::vector<old_school::CardId>{
                old_school::CardId::Mountain,
                old_school::CardId::LightningBolt,
            },
        };
    old_school::GameState lethal;
    lethal.active_player = 1;
    lethal.starting_player = 1;
    lethal.turn_number = 10;
    lethal.next_stack_object_id = 2;
    lethal.players[0].life = 3;
    lethal.players[0].graveyard = lethal_decks[0];
    lethal.players[1].lands = {
        {
            .card = old_school::CardId::Mountain,
            .tapped = true,
        },
    };
    lethal.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::LightningBolt,
            .controller = 1,
            .target =
                old_school::Target::player_target(0),
            .spell_target = std::nullopt,
        },
    };
    const auto lethal_samples =
        old_school::learned_priority_action_samples(
            lethal, lethal_decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1,
            {old_school::PriorityAction::pass()},
            model, capture);
    const auto& lethal_boundary =
        lethal_samples.priority_h0_boundaries[0][0];
    CHECK(lethal_boundary.terminal);
    CHECK(!lethal_boundary.context.valid);
    CHECK(lethal_boundary.state.turn_number == 10);
    CHECK(lethal_boundary.state.active_player == 1);
    CHECK(lethal_boundary.state.players[0].life == 0);
    CHECK(lethal_boundary.continuation_score == 0.0);
    CHECK(lethal_samples.q_samples[0][0] == 0.0);
    CHECK(lethal_samples.terminal_evaluations == 1);
    CHECK(lethal_samples.bootstrapped_evaluations == 0);

    const std::array<std::vector<old_school::CardId>, 2>
        draw_decks = {
            std::vector<old_school::CardId>{
                old_school::CardId::Forest,
            },
            std::vector<old_school::CardId>{
                old_school::CardId::Mountain,
            },
        };
    old_school::GameState empty_draw;
    empty_draw.active_player = 0;
    empty_draw.starting_player = 0;
    empty_draw.turn_number = 4;
    empty_draw.players[0].graveyard = draw_decks[0];
    empty_draw.players[1].graveyard = draw_decks[1];
    const auto draw_samples =
        old_school::learned_priority_action_samples(
            empty_draw, draw_decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            {old_school::PriorityAction::pass()},
            model, capture);
    const auto& draw_boundary =
        draw_samples.priority_h0_boundaries[0][0];
    CHECK(draw_boundary.terminal);
    CHECK(!draw_boundary.context.valid);
    CHECK(draw_boundary.state.turn_number == 5);
    CHECK(draw_boundary.state.active_player == 1);
    CHECK(draw_boundary.state.players[1].library.empty());
    CHECK(draw_boundary.state.players[1].hand.empty());
    CHECK(draw_boundary.state.stats[1].cards_drawn == 0);
    CHECK(draw_boundary.continuation_score == 1.0);
    CHECK(draw_samples.q_samples[0][0] == 1.0);
    CHECK(draw_samples.terminal_evaluations == 1);
    CHECK(draw_samples.bootstrapped_evaluations == 0);
}

TEST(priority_h0_boundary_capture_rejects_non_priority_samplers) {
    const auto attack =
        attack_evaluation_fixture(
            old_school::CardId::GrizzlyBears);
    const old_school::LearnedSearchConfig attack_capture = {
        .seed = 0xAC1B0A0D7ULL,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
        .evaluation_threads = 1,
        .capture_priority_h0_boundaries = true,
    };
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_binary_attack_samples(
                    attack.state, attack.decks, 0, {}, 1, {},
                    small_actor_model(), attack_capture));
        },
        "unavailable for Attack"));

    const auto block = block_evaluation_fixture();
    const old_school::LearnedSearchConfig block_capture = {
        .seed = 0xAC1B0A0D8ULL,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = false,
        .evaluation_threads = 1,
        .capture_priority_h0_boundaries = true,
    };
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_binary_block_samples(
                    block.state, block.decks, 1, 1, 2,
                    small_value_model(), block_capture));
        },
        "unavailable for Block"));
}

TEST(priority_macro_budget_accepts_exact_limits_and_rejects_next) {
    old_school::LearnedPriorityMacroBudget actions;
    CHECK(actions.try_apply_actions(
        old_school::kLearnedPriorityMacroActionBound - 1));
    CHECK(actions.actions_applied ==
          old_school::kLearnedPriorityMacroActionBound);
    CHECK(!actions.try_apply_actions(1));
    CHECK(actions.actions_applied ==
          old_school::kLearnedPriorityMacroActionBound);

    old_school::LearnedPriorityMacroBudget phases;
    for (std::size_t transition = 0;
         transition <
         old_school::kLearnedPriorityMacroPhaseTransitionBound;
         ++transition) {
        CHECK(phases.try_advance_phase());
    }
    CHECK(phases.phase_transitions ==
          old_school::kLearnedPriorityMacroPhaseTransitionBound);
    CHECK(!phases.try_advance_phase());
    CHECK(phases.phase_transitions ==
          old_school::kLearnedPriorityMacroPhaseTransitionBound);

    old_school::LearnedPriorityMacroBudget turns;
    for (std::size_t advance = 0;
         advance <
         old_school::kLearnedPriorityMacroTurnAdvanceBound;
         ++advance) {
        CHECK(turns.try_advance_turn());
    }
    CHECK(turns.turn_advances ==
          old_school::kLearnedPriorityMacroTurnAdvanceBound);
    CHECK(!turns.try_advance_turn());
    CHECK(turns.turn_advances ==
          old_school::kLearnedPriorityMacroTurnAdvanceBound);
}

TEST(priority_macro_transition_rejects_invalid_phase) {
    old_school::GameState state;
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::Forest,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
        },
    };
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    advance_learned_priority_macro_transition(
                        state, decks, 0, false,
                        static_cast<old_school::TurnPhase>(
                            std::numeric_limits<std::uint8_t>::max()),
                        0, old_school::PriorityAction::pass(),
                        small_value_model(), 0xF000BAD0ULL));
        },
        "priority evaluation phase is invalid"));
}

TEST(priority_macro_transition_cast_retains_priority_before_policy) {
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 4;
    state.next_permanent_id = 2;
    state.players[0].hand = {
        old_school::CardId::GrizzlyBears,
        old_school::CardId::GiantGrowth,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
        {.card = old_school::CardId::Forest},
    };
    state.players[0].creatures = {bear(1)};
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::GrizzlyBears,
            old_school::CardId::GiantGrowth,
            old_school::CardId::Forest,
            old_school::CardId::Forest,
            old_school::CardId::Forest,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
        },
    };

    const auto result =
        old_school::advance_learned_priority_macro_transition(
            state, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0,
            old_school::PriorityAction::cast_creature(
                old_school::CardId::GrizzlyBears),
            small_value_model(), 0xF000CA57ULL);

    CHECK(result.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(result.exhausted_limit ==
          old_school::LearnedPriorityMacroLimit::None);
    CHECK(!result.terminal_result.has_value());
    CHECK(result.context.valid);
    CHECK(result.context.phase ==
          old_school::TurnPhase::FirstMain);
    CHECK(result.context.decision_player == 0);
    CHECK(result.context.consecutive_passes == 0);
    CHECK(result.context.sorcery_actions);
    CHECK(result.actions_applied == 1);
    CHECK(result.priority_actions_applied == 1);
    CHECK(result.phase_transitions == 0);
    CHECK(result.turn_advances == 0);
    CHECK(result.state.stack.size() == 1);
    CHECK(result.state.stack.back().card ==
          old_school::CardId::GrizzlyBears);
    CHECK(result.state.stack.back().controller == 0);
    CHECK(result.state.stats[0].spells_cast == 1);
    CHECK(result.state.stats[0].decisions == 0);
    CHECK(result.legal_actions ==
          old_school::legal_priority_actions(
              result.state, 0, true));
    CHECK(has_action(
        result.legal_actions,
        old_school::PriorityAction::cast_giant_growth(
            old_school::Target::creature_target(0, 1))));
}

TEST(priority_macro_transition_preserves_first_and_second_pass_semantics) {
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
            old_school::CardId::LightningBolt,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
            old_school::CardId::LightningBolt,
            old_school::CardId::GrizzlyBears,
        },
    };
    old_school::GameState first_pass;
    first_pass.active_player = 0;
    first_pass.starting_player = 0;
    first_pass.turn_number = 4;
    first_pass.players[1].hand = {
        old_school::CardId::LightningBolt,
    };
    first_pass.players[1].lands = {
        {.card = old_school::CardId::Mountain},
    };

    const auto transferred =
        old_school::advance_learned_priority_macro_transition(
            first_pass, decks, 0, false,
            old_school::TurnPhase::BeginCombat, 0,
            old_school::PriorityAction::pass(),
            small_value_model(), 0xF000A551ULL);
    CHECK(transferred.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(transferred.context.decision_player == 1);
    CHECK(transferred.context.consecutive_passes == 1);
    CHECK(transferred.context.phase ==
          old_school::TurnPhase::BeginCombat);
    CHECK(!transferred.context.sorcery_actions);
    CHECK(transferred.priority_actions_applied == 1);
    CHECK(transferred.state.stack.empty());
    CHECK(transferred.state.stats[1].decisions == 0);

    old_school::GameState second_pass;
    second_pass.active_player = 0;
    second_pass.starting_player = 0;
    second_pass.turn_number = 4;
    second_pass.next_permanent_id = 1;
    second_pass.next_stack_object_id = 2;
    second_pass.players[0].hand = {
        old_school::CardId::LightningBolt,
    };
    second_pass.players[0].lands = {
        {.card = old_school::CardId::Mountain},
    };
    second_pass.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::GrizzlyBears,
            .controller = 1,
        },
    };

    const auto resolved =
        old_school::advance_learned_priority_macro_transition(
            second_pass, decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1,
            old_school::PriorityAction::pass(),
            small_value_model(), 0xF000A552ULL);
    CHECK(resolved.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(resolved.context.decision_player == 0);
    CHECK(resolved.context.consecutive_passes == 0);
    CHECK(resolved.context.phase ==
          old_school::TurnPhase::BeginCombat);
    CHECK(resolved.state.stack.empty());
    CHECK(resolved.state.players[1].creatures.size() == 1);
    CHECK(resolved.state.players[1].creatures.front().card ==
          old_school::CardId::GrizzlyBears);
    CHECK(resolved.state.stats[0].decisions == 0);
    CHECK(resolved.priority_actions_applied == 1);
}

TEST(priority_macro_transition_uses_every_empty_stack_phase_exit) {
    struct PhaseCase {
        old_school::TurnPhase root;
        old_school::TurnPhase successor;
        std::size_t successor_player = 0;
        bool successor_sorcery = false;
        std::size_t phase_transitions = 0;
        std::size_t turn_advances = 0;
    };
    const std::array<PhaseCase, 4> cases = {{
        {
            .root = old_school::TurnPhase::FirstMain,
            .successor = old_school::TurnPhase::BeginCombat,
            .successor_player = 0,
            .successor_sorcery = false,
            .phase_transitions = 1,
        },
        {
            .root = old_school::TurnPhase::BeginCombat,
            .successor = old_school::TurnPhase::EndCombat,
            .successor_player = 0,
            .successor_sorcery = false,
            .phase_transitions = 2,
        },
        {
            .root = old_school::TurnPhase::EndCombat,
            .successor = old_school::TurnPhase::SecondMain,
            .successor_player = 0,
            .successor_sorcery = true,
            .phase_transitions = 1,
        },
        {
            .root = old_school::TurnPhase::SecondMain,
            .successor = old_school::TurnPhase::FirstMain,
            .successor_player = 1,
            .successor_sorcery = true,
            .phase_transitions = 2,
            .turn_advances = 1,
        },
    }};
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
            old_school::CardId::LightningBolt,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
            old_school::CardId::LightningBolt,
            old_school::CardId::Mountain,
        },
    };

    for (std::size_t index = 0; index < cases.size(); ++index) {
        old_school::GameState state;
        state.active_player = 0;
        state.starting_player = 0;
        state.turn_number = 4;
        for (std::size_t player = 0; player < 2; ++player) {
            state.players[player].hand = {
                old_school::CardId::LightningBolt,
            };
            state.players[player].lands = {
                {.card = old_school::CardId::Mountain},
            };
        }
        state.players[1].library = {
            old_school::CardId::Mountain,
        };

        const bool root_sorcery =
            cases[index].root ==
                old_school::TurnPhase::FirstMain ||
            cases[index].root ==
                old_school::TurnPhase::SecondMain;
        const auto result =
            old_school::advance_learned_priority_macro_transition(
                state, decks, 0, root_sorcery,
                cases[index].root, 1,
                old_school::PriorityAction::pass(),
                small_value_model(),
                0xF000FA5EULL + index);
        CHECK(result.disposition ==
              old_school::LearnedPriorityMacroDisposition::
                  PriorityBoundary);
        CHECK(result.context.phase ==
              cases[index].successor);
        CHECK(result.context.decision_player ==
              cases[index].successor_player);
        CHECK(result.context.consecutive_passes == 0);
        CHECK(result.context.sorcery_actions ==
              cases[index].successor_sorcery);
        CHECK(result.phase_transitions ==
              cases[index].phase_transitions);
        CHECK(result.turn_advances ==
              cases[index].turn_advances);
        CHECK(result.state.stats[
                  cases[index].successor_player]
                  .decisions == 0);
    }
}

TEST(priority_macro_transition_runs_attack_block_and_damage_order) {
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 4;
    state.next_permanent_id = 4;
    state.players[0].hand = {
        old_school::CardId::LightningBolt,
    };
    state.players[0].lands = {
        {.card = old_school::CardId::Mountain},
    };
    state.players[0].creatures = {
        creature(1, old_school::CardId::HillGiant),
    };
    state.players[1].life = 1;
    state.players[1].creatures = {
        bear(2),
        bear(3),
    };
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
            old_school::CardId::LightningBolt,
            old_school::CardId::HillGiant,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::GrizzlyBears,
            old_school::CardId::GrizzlyBears,
        },
    };

    const auto result =
        old_school::advance_learned_priority_macro_transition(
            state, decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1,
            old_school::PriorityAction::pass(),
            small_value_model(), 0xF000C0B4ULL);

    CHECK(result.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(result.context.phase ==
          old_school::TurnPhase::EndCombat);
    CHECK(result.context.decision_player == 0);
    CHECK(result.context.consecutive_passes == 0);
    CHECK(!result.context.sorcery_actions);
    CHECK(result.phase_transitions == 4);
    CHECK(result.turn_advances == 0);
    CHECK(result.actions_applied == 4);
    CHECK(result.priority_actions_applied == 1);
    CHECK(result.state.players[0].creatures.empty());
    CHECK(result.state.players[0].graveyard ==
          std::vector<old_school::CardId>{
              old_school::CardId::HillGiant});
    CHECK(result.state.players[1].creatures.size() == 1);
    CHECK(result.state.players[1].graveyard ==
          std::vector<old_school::CardId>{
              old_school::CardId::GrizzlyBears});
    CHECK(result.state.players[1].life == 1);
    CHECK(result.state.stats[0].decisions == 0);
    CHECK(result.state.stats[1].decisions == 0);
    CHECK(result.legal_actions ==
          old_school::legal_priority_actions(
              result.state, 0, false));
}

TEST(priority_macro_transition_runs_cleanup_extra_turn_draw_and_deckout) {
    const auto model = small_value_model();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::Forest,
            old_school::CardId::Forest,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
            old_school::CardId::Mountain,
        },
    };

    old_school::GameState cleanup;
    cleanup.active_player = 0;
    cleanup.starting_player = 0;
    cleanup.turn_number = 4;
    cleanup.players[0].hand.assign(
        8, old_school::CardId::Forest);
    cleanup.players[1].hand = {
        old_school::CardId::Mountain,
    };
    cleanup.players[1].library = {
        old_school::CardId::Mountain,
    };
    const auto cleaned =
        old_school::advance_learned_priority_macro_transition(
            cleanup, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            old_school::PriorityAction::pass(), model,
            0xF000C1EAULL);
    CHECK(cleaned.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(cleaned.state.players[0].hand.size() == 7);
    CHECK(cleaned.state.players[0].graveyard ==
          std::vector<old_school::CardId>{
              old_school::CardId::Forest});
    CHECK(cleaned.state.active_player == 1);
    CHECK(cleaned.state.turn_number == 5);
    CHECK(cleaned.state.stats[1].cards_drawn == 1);
    CHECK(cleaned.context.phase ==
          old_school::TurnPhase::FirstMain);

    old_school::GameState extra_turn;
    extra_turn.active_player = 0;
    extra_turn.starting_player = 0;
    extra_turn.turn_number = 4;
    extra_turn.extra_turns_pending[0] = 1;
    extra_turn.players[0].hand = {
        old_school::CardId::Forest,
    };
    extra_turn.players[0].library = {
        old_school::CardId::Forest,
    };
    const auto extra =
        old_school::advance_learned_priority_macro_transition(
            extra_turn, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            old_school::PriorityAction::pass(), model,
            0xF000E471ULL);
    CHECK(extra.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(extra.state.active_player == 0);
    CHECK(extra.state.turn_number == 5);
    CHECK(extra.state.extra_turns_pending[0] == 0);
    CHECK(extra.state.stats[0].cards_drawn == 1);
    CHECK(extra.context.decision_player == 0);

    old_school::GameState draw_skip;
    draw_skip.active_player = 1;
    draw_skip.starting_player = 0;
    draw_skip.turn_number = 0;
    draw_skip.players[0].hand = {
        old_school::CardId::Forest,
    };
    draw_skip.players[0].library = {
        old_school::CardId::Forest,
    };
    const auto skipped =
        old_school::advance_learned_priority_macro_transition(
            draw_skip, decks, 1, true,
            old_school::TurnPhase::SecondMain, 1,
            old_school::PriorityAction::pass(), model,
            0xF000D2A5ULL);
    CHECK(skipped.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(skipped.state.turn_number == 1);
    CHECK(skipped.state.active_player == 0);
    CHECK(skipped.state.stats[0].cards_drawn == 0);
    CHECK(skipped.state.players[0].library ==
          std::vector<old_school::CardId>{
              old_school::CardId::Forest});

    old_school::GameState deckout;
    deckout.active_player = 0;
    deckout.starting_player = 0;
    deckout.turn_number = 4;
    const auto empty =
        old_school::advance_learned_priority_macro_transition(
            deckout, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            old_school::PriorityAction::pass(), model,
            0xF000DEC0ULL);
    CHECK(empty.disposition ==
          old_school::LearnedPriorityMacroDisposition::Terminal);
    CHECK(empty.exhausted_limit ==
          old_school::LearnedPriorityMacroLimit::None);
    CHECK(empty.terminal_result.has_value());
    CHECK(empty.terminal_result->winner == 0);
    CHECK(empty.terminal_result->reason ==
          old_school::EndReason::EmptyLibrary);
    CHECK(empty.state.turn_number == 5);
    CHECK(empty.state.active_player == 1);
    CHECK(empty.turn_advances == 1);
}

TEST(priority_macro_transition_forces_singletons_and_bounds_turns) {
    const auto model = small_value_model();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        std::vector<old_school::CardId>{
            old_school::CardId::GrizzlyBears,
        },
        std::vector<old_school::CardId>{
            old_school::CardId::Mountain,
        },
    };
    old_school::GameState chain;
    chain.active_player = 0;
    chain.starting_player = 0;
    chain.turn_number = 4;
    chain.next_permanent_id = 1;
    chain.next_stack_object_id = 2;
    chain.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::GrizzlyBears,
            .controller = 0,
        },
    };
    chain.players[1].library = {
        old_school::CardId::Mountain,
    };
    const auto stopped =
        old_school::advance_learned_priority_macro_transition(
            chain, decks, 0, true,
            old_school::TurnPhase::FirstMain, 0,
            old_school::PriorityAction::pass(), model,
            0xF00051A6ULL);
    CHECK(stopped.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);
    CHECK(stopped.priority_actions_applied > 1);
    CHECK(stopped.context.phase ==
          old_school::TurnPhase::FirstMain);
    CHECK(stopped.context.decision_player == 1);
    CHECK(stopped.state.stack.empty());
    CHECK(stopped.state.players[0].creatures.size() == 1);
    CHECK(stopped.state.players[0].creatures.front().card ==
          old_school::CardId::GrizzlyBears);

    old_school::GameState action_bounded;
    action_bounded.active_player = 0;
    action_bounded.starting_player = 0;
    action_bounded.turn_number = 4;
    action_bounded.players[0].hand.assign(
        old_school::kMaximumHandSize +
            old_school::kLearnedPriorityMacroActionBound,
        old_school::CardId::FireElemental);
    const std::array<std::vector<old_school::CardId>, 2>
        action_bounded_decks = {
            action_bounded.players[0].hand,
            std::vector<old_school::CardId>{
                old_school::CardId::FireElemental,
            },
        };
    const auto action_incomplete =
        old_school::advance_learned_priority_macro_transition(
            action_bounded, action_bounded_decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            old_school::PriorityAction::pass(), model,
            0xF000AC71ULL);
    CHECK(action_incomplete.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              Incomplete);
    CHECK(action_incomplete.exhausted_limit ==
          old_school::LearnedPriorityMacroLimit::Action);
    CHECK(action_incomplete.actions_applied == 1);
    CHECK(action_incomplete.priority_actions_applied == 1);
    CHECK(action_incomplete.phase_transitions == 1);
    CHECK(action_incomplete.turn_advances == 0);

    old_school::GameState bounded;
    bounded.active_player = 0;
    bounded.starting_player = 0;
    // Deliberately straddles GameConfig's ordinary turn-500 ceiling. The
    // macro-specific 65th attempted advance must own termination.
    bounded.turn_number = 499;
    bounded.players[0].library.assign(
        100, old_school::CardId::FireElemental);
    bounded.players[1].library.assign(
        100, old_school::CardId::FireElemental);
    const std::array<std::vector<old_school::CardId>, 2>
        bounded_decks = {
            bounded.players[0].library,
            bounded.players[1].library,
        };
    const auto incomplete =
        old_school::advance_learned_priority_macro_transition(
            bounded, bounded_decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            old_school::PriorityAction::pass(), model,
            0xF000B00DULL);
    CHECK(incomplete.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              Incomplete);
    CHECK(incomplete.exhausted_limit ==
          old_school::LearnedPriorityMacroLimit::TurnAdvance);
    CHECK(!incomplete.terminal_result.has_value());
    CHECK(!incomplete.context.valid);
    CHECK(incomplete.legal_actions.empty());
    CHECK(incomplete.turn_advances ==
          old_school::kLearnedPriorityMacroTurnAdvanceBound);
    CHECK(incomplete.state.turn_number ==
          bounded.turn_number +
              old_school::kLearnedPriorityMacroTurnAdvanceBound);
    CHECK(incomplete.actions_applied <=
          old_school::kLearnedPriorityMacroActionBound);
    CHECK(incomplete.priority_actions_applied <=
          incomplete.actions_applied);
    CHECK(incomplete.phase_transitions <=
          old_school::kLearnedPriorityMacroPhaseTransitionBound);
}

TEST(priority_macro_hooks_are_default_off_for_fixed_seed_games) {
    const std::vector<old_school::CardId> forests(
        8, old_school::CardId::Forest);
    const std::vector<old_school::CardId> mountains(
        8, old_school::CardId::Mountain);
    old_school::GameConfig config;
    config.max_turns = 8;
    config.starting_player = 0;
    const auto run_default_game = [&] {
        old_school::Game game(
            forests, mountains, 0xF0000FF0ULL, config);
        const auto result = game.run();
        return std::pair{result, game.state()};
    };

    const auto before = run_default_game();

    old_school::GameState macro_state;
    macro_state.active_player = 0;
    macro_state.starting_player = 0;
    macro_state.turn_number = 4;
    macro_state.players[1].hand = {
        old_school::CardId::LightningBolt,
    };
    macro_state.players[1].lands = {
        {.card = old_school::CardId::Mountain},
    };
    const std::array<std::vector<old_school::CardId>, 2>
        macro_decks = {
            std::vector<old_school::CardId>{
                old_school::CardId::Forest,
            },
            std::vector<old_school::CardId>{
                old_school::CardId::Mountain,
                old_school::CardId::LightningBolt,
            },
        };
    const auto boundary =
        old_school::advance_learned_priority_macro_transition(
            macro_state, macro_decks, 0, false,
            old_school::TurnPhase::BeginCombat, 0,
            old_school::PriorityAction::pass(),
            small_value_model(), 0xF0000FF1ULL);
    CHECK(boundary.disposition ==
          old_school::LearnedPriorityMacroDisposition::
              PriorityBoundary);

    const auto after = run_default_game();
    CHECK(after == before);
    CHECK(before.first.winner == 0);
    CHECK(before.first.reason ==
          old_school::EndReason::EmptyLibrary);
    CHECK(before.first.turns == 4);
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
    CHECK(baseline.terminal_evaluations == 0);
    CHECK(baseline.bootstrapped_evaluations == 8);
    CHECK(baseline.terminal_evaluations +
              baseline.bootstrapped_evaluations ==
          baseline.rollout_evaluations);
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
    CHECK(value_samples.terminal_evaluations == 0);
    CHECK(value_samples.bootstrapped_evaluations == 2);
    CHECK(value_samples.terminal_evaluations +
              value_samples.bootstrapped_evaluations ==
          value_samples.rollout_evaluations);

    auto depleted = fixture;
    for (std::size_t player = 0;
         player < depleted.state.players.size(); ++player) {
        auto& player_state = depleted.state.players[player];
        if (player == 1) {
            const old_school::CardId final_draw =
                player_state.library.back();
            player_state.library.pop_back();
            player_state.graveyard.insert(
                player_state.graveyard.end(),
                player_state.library.begin(),
                player_state.library.end());
            player_state.library = {final_draw};
        } else {
            player_state.graveyard.insert(
                player_state.graveyard.end(),
                player_state.library.begin(),
                player_state.library.end());
            player_state.library.clear();
        }
    }
    old_school::LearnedSearchConfig terminal_config = actor_config;
    terminal_config.worlds = 1;
    terminal_config.rollouts_per_world = 1;
    terminal_config.horizon_turns = 2;
    const auto natural_terminals =
        old_school::learned_binary_attack_samples(
            depleted.state, depleted.decks, 0, {}, 1, {},
            small_actor_model(), terminal_config);
    CHECK(natural_terminals.rollout_evaluations == 2);
    CHECK(natural_terminals.terminal_evaluations == 2);
    CHECK(natural_terminals.bootstrapped_evaluations == 0);
    CHECK(natural_terminals.terminal_evaluations +
              natural_terminals.bootstrapped_evaluations ==
          natural_terminals.rollout_evaluations);

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

TEST(generic_binary_block_samples_are_paired_legal_and_hidden_safe) {
    const DeterminizationFixture fixture =
        block_evaluation_fixture();
    const old_school::GameState original_state =
        fixture.state;
    const old_school::LearnedSearchConfig config = {
        .seed = 0xB10C5A6DULL,
        .worlds = 3,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .blend_shallow_prior = false,
    };
    const auto baseline =
        old_school::learned_binary_block_samples(
            fixture.state, fixture.decks, 1, 1, 2,
            small_value_model(), config);
    CHECK(fixture.state == original_state);
    CHECK(baseline.sampled_worlds == 3);
    CHECK(baseline.q_samples.size() == 2);
    CHECK(baseline.exact_priority_aggregate_scores.empty());
    CHECK(baseline.rollout_evaluations == 12);
    CHECK(baseline.terminal_evaluations +
              baseline.bootstrapped_evaluations ==
          baseline.rollout_evaluations);
    for (const auto& row : baseline.q_samples) {
        CHECK(row.size() == 6);
        CHECK(std::all_of(
            row.begin(), row.end(),
            [](double score) {
                return std::isfinite(score) &&
                       score >= 0.0 && score <= 1.0;
            }));
    }

    const auto repeated =
        old_school::learned_binary_block_samples(
            fixture.state, fixture.decks, 1, 1, 2,
            small_value_model(), config);
    CHECK(repeated.q_samples == baseline.q_samples);
    CHECK(repeated.sampled_worlds ==
          baseline.sampled_worlds);
    CHECK(repeated.rollout_evaluations ==
          baseline.rollout_evaluations);
    CHECK(repeated.terminal_evaluations ==
          baseline.terminal_evaluations);
    CHECK(repeated.bootstrapped_evaluations ==
          baseline.bootstrapped_evaluations);

    const auto hidden =
        hidden_repartition(fixture.state, 1);
    const auto hidden_samples =
        old_school::learned_binary_block_samples(
            hidden, fixture.decks, 1, 1, 2,
            small_value_model(), config);
    CHECK(hidden_samples.q_samples == baseline.q_samples);
    CHECK(hidden_samples.rollout_evaluations ==
          baseline.rollout_evaluations);
    CHECK(hidden_samples.terminal_evaluations ==
          baseline.terminal_evaluations);
    CHECK(hidden_samples.bootstrapped_evaluations ==
          baseline.bootstrapped_evaluations);

    auto no_block = fixture.state;
    no_block.players[0].creatures[0].tapped = false;
    CHECK(old_school::resolve_combat(
        no_block, 0, {1}, {}));
    CHECK(no_block.players[1].life == 16);
    CHECK(no_block.players[0].creatures.size() == 1);
    CHECK(no_block.players[1].creatures.size() == 1);
    CHECK(no_block.players[0].creatures[0].tapped);

    auto block = fixture.state;
    block.players[0].creatures[0].tapped = false;
    CHECK(old_school::resolve_combat(
        block, 0, {1}, {{1, 2}}));
    CHECK(block.players[1].life == 20);
    CHECK(block.players[0].creatures.size() == 1);
    CHECK(block.players[0].creatures[0].tapped);
    CHECK(block.players[0].creatures[0].damage == 1);
    CHECK(block.players[1].creatures.empty());
    CHECK(block.players[1].graveyard.back() ==
          old_school::CardId::FlyingMen);

    const auto immediate =
        old_school::learned_value_binary_block_scores(
            fixture.state, 1, 1, 2,
            small_value_model());
    std::array<double, 2> expected_immediate{};
    for (std::size_t choice = 0; choice < 2; ++choice) {
        auto successor = fixture.state;
        successor.players[0].creatures[0].tapped = false;
        const std::vector<std::pair<
            old_school::PermanentId,
            old_school::PermanentId>>
            blocks =
                choice == 0
                    ? std::vector<std::pair<
                          old_school::PermanentId,
                          old_school::PermanentId>>{}
                    : std::vector<std::pair<
                          old_school::PermanentId,
                          old_school::PermanentId>>{
                          {1, 2},
                      };
        CHECK(old_school::resolve_combat(
            successor, 0, {1}, blocks));
        expected_immediate[choice] =
            old_school::learned_contextual_critic_value(
                successor, 1,
                {
                    .valid = true,
                    .phase =
                        old_school::TurnPhase::EndCombat,
                    .decision_player =
                        successor.active_player,
                    .consecutive_passes = 0,
                    .sorcery_actions = false,
                },
                small_value_model());
    }
    CHECK(immediate.scores == expected_immediate);
    CHECK(immediate.selected_candidate ==
          (immediate.scores[1] >
                   immediate.scores[0]
               ? 1U
               : 0U));
    CHECK(old_school::learned_value_binary_block_scores(
              hidden, 1, 1, 2, small_value_model())
              .scores == immediate.scores);

    old_school::LearnedSearchConfig blended_config =
        config;
    blended_config.blend_shallow_prior = true;
    const auto blended =
        old_school::learned_binary_block_samples(
            fixture.state, fixture.decks, 1, 1, 2,
            small_value_model(), blended_config);
    const double sample_count = 6.0;
    for (std::size_t row = 0; row < 2; ++row) {
        for (std::size_t sample = 0; sample < 6;
             ++sample) {
            CHECK(blended.q_samples[row][sample] ==
                  (immediate.scores[row] +
                   sample_count *
                       baseline.q_samples[row][sample]) /
                      (sample_count + 1.0));
        }
    }

    auto lethal = fixture.state;
    lethal.players[1].life = 4;
    old_school::LearnedSearchConfig one_sample =
        config;
    one_sample.worlds = 1;
    one_sample.rollouts_per_world = 1;
    const auto lethal_samples =
        old_school::learned_binary_block_samples(
            lethal, fixture.decks, 1, 1, 2,
            small_value_model(), one_sample);
    CHECK(lethal_samples.q_samples[0][0] == 0.0);
    CHECK(lethal_samples.q_samples[1][0] > 0.0);
    CHECK(lethal_samples.q_samples[1][0] < 1.0);
    CHECK(lethal_samples.terminal_evaluations == 1);
    CHECK(lethal_samples.bootstrapped_evaluations == 1);

    const auto rejects =
        [&](const old_school::GameState& state,
            std::size_t defender,
            old_school::PermanentId attacker,
            old_school::PermanentId blocker,
            std::string_view text) {
            CHECK(throws_with_text(
                [&] {
                    static_cast<void>(
                        old_school::
                            learned_binary_block_samples(
                                state, fixture.decks,
                                defender, attacker,
                                blocker,
                                small_value_model(),
                                config));
                },
                text));
        };
    rejects(fixture.state, 2, 1, 2, "defending player");
    rejects(fixture.state, 0, 1, 2, "active opponent");
    rejects(fixture.state, 1, 2, 1, "attacker");
    rejects(fixture.state, 1, 999, 2, "attacker");
    rejects(fixture.state, 1, 1, 999, "blocker");

    auto untapped_attacker = fixture.state;
    untapped_attacker.players[0].creatures[0].tapped =
        false;
    rejects(
        untapped_attacker, 1, 1, 2,
        "existing tapped creature");
    auto tapped_blocker = fixture.state;
    tapped_blocker.players[1].creatures[0].tapped =
        true;
    rejects(
        tapped_blocker, 1, 1, 2,
        "existing untapped creature");
    auto occupied_stack = fixture.state;
    occupied_stack.stack.push_back({
        .kind = old_school::StackObjectKind::Spell,
        .id = 1,
        .card = old_school::CardId::Counterspell,
        .controller = 0,
    });
    rejects(
        occupied_stack, 1, 1, 2, "empty stack");
    auto ground_blocker = fixture.state;
    ground_blocker.players[1].creatures[0].card =
        old_school::CardId::GrizzlyBears;
    rejects(
        ground_blocker, 1, 1, 2,
        "block branch is illegal");

    const auto rejects_config =
        [&](old_school::LearnedSearchConfig bad,
            std::string_view text) {
            CHECK(throws_with_text(
                [&] {
                    static_cast<void>(
                        old_school::
                            learned_binary_block_samples(
                                fixture.state,
                                fixture.decks, 1, 1, 2,
                                small_value_model(), bad));
                },
                text));
        };
    auto bad = config;
    bad.worlds = 0;
    rejects_config(bad, "worlds");
    bad = config;
    bad.rollouts_per_world = 0;
    rejects_config(bad, "rollouts per world");
    bad = config;
    bad.horizon_turns = 129;
    rejects_config(bad, "horizon");
    bad = config;
    bad.evaluation_threads = 0;
    rejects_config(bad, "threads");
    rejects_config(
        {
            .seed = config.seed,
            .worlds = 1,
            .rollouts_per_world = 1,
            .horizon_turns = 0,
            .continuation_variant =
                old_school::LearnedVariant::UnifiedActor,
        },
        "does not match");
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
        const auto explicit_default =
            old_school::learned_value_attack_set_scores(
                fixture.state, 0, candidates,
                small_value_model(), seed, false);
        const auto shared_samples =
            old_school::learned_value_attack_block_samples(
                fixture.state, 0, candidates,
                small_value_model(), seed);
        CHECK(
            shared_samples ==
            old_school::learned_value_attack_block_samples(
                fixture.state, 0, candidates,
                small_value_model(), seed));
        const auto mean_from_shared_samples =
            old_school::aggregate_learned_value_attack_block_scores(
                shared_samples, false);
        const auto minimum_from_shared_samples =
            old_school::aggregate_learned_value_attack_block_scores(
                shared_samples, true);
        const auto explicit_minimum =
            old_school::learned_value_attack_set_scores(
                fixture.state, 0, candidates,
                small_value_model(), seed, true);
        CHECK(explicit_default.scores == scored.scores);
        CHECK(explicit_default.selected_candidate ==
              scored.selected_candidate);
        CHECK(explicit_default.scores ==
              mean_from_shared_samples.scores);
        CHECK(explicit_default.selected_candidate ==
              mean_from_shared_samples.selected_candidate);
        CHECK(explicit_minimum.scores ==
              minimum_from_shared_samples.scores);
        CHECK(explicit_minimum.selected_candidate ==
              minimum_from_shared_samples.selected_candidate);
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

TEST(adversarial_block_aggregation_uses_defender_best_response) {
    const std::vector<std::vector<double>> block_scores = {
        {1.0, 1.0, 1.0, 0.0},
        {0.5, 0.5, 0.5, 0.5},
    };
    const auto historical =
        old_school::aggregate_learned_value_attack_block_scores(
            block_scores, false);
    const auto adversarial =
        old_school::aggregate_learned_value_attack_block_scores(
            block_scores, true);
    CHECK(historical.scores ==
          std::vector<double>({0.75, 0.5}));
    CHECK(historical.selected_candidate == 0);
    CHECK(adversarial.scores ==
          std::vector<double>({0.0, 0.5}));
    CHECK(adversarial.selected_candidate == 1);

    const auto tied =
        old_school::aggregate_learned_value_attack_block_scores(
            {{0.25}, {0.25}}, true);
    CHECK(tied.selected_candidate == 0);
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

TEST(five_deck_tournament_runs_all_ten_pairings) {
    const auto result = old_school::run_tournament(100, 0xC0FFEEULL);
    CHECK(result.games_per_matchup == 100);
    CHECK(result.total_games == 1000);
    CHECK(result.evaluation_seed == 0xC0FFEEULL);
    CHECK(!result.effective_learned_bot.has_value());
    CHECK(result.effective_learned_model_fingerprint.empty());
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
            CHECK(deck_bot.on_play_games == 10);
            CHECK(deck_bot.on_draw_games == 10);
            deck_bot_games += deck_bot.games;
        }
        CHECK(deck_bot_games == result.decks[deck].games);
    }
    CHECK(result.evaluation_seed == 0xDEC1DEULL);
    CHECK(result.effective_learned_bot.has_value());
    CHECK(result.effective_learned_bot->kind ==
          old_school::BotKind::Learned);
    CHECK(result.effective_learned_bot->learned_variant ==
          old_school::LearnedVariant::ValueSearchChampion);
    CHECK(result.effective_learned_bot->rollouts_per_action ==
          bots.learned_rollouts);
    CHECK(result.effective_learned_bot->learned_model);
    CHECK(
        result.effective_learned_model_fingerprint ==
        old_school::learned_model_fingerprint(
            result.effective_learned_bot->learned_model));

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

TEST(tournament_frozen_learned_override_propagates_exact_treatment) {
    const auto model = small_value_model();
    const old_school::BotConfig treatment = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 8,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = true,
        .value_continuation_controller =
            old_school::LearnedContinuationController::
                PublicStackPassV1,
        .training_games = 17,
        .learned_model = model,
    };
    const old_school::TournamentConfig tournament = {
        .bot_field = old_school::BotField::Mixed,
        .learned_variant =
            old_school::LearnedVariant::UnifiedActor,
        .monte_carlo_rollouts = 1,
        .deep_monte_carlo_rollouts = 2,
        // These legacy scalar values are deliberately different. The exact
        // frozen BotConfig is authoritative when the override is present.
        .learned_rollouts = 1,
        .value_continuation_epsilon = 0.75,
        .learned_training_games = 0,
        .frozen_learned_bot = treatment,
    };
    old_school::GameConfig bounded;
    bounded.max_turns = 1;
    constexpr std::uint64_t kEvaluationSeed =
        0xF202E4C7ULL;
    const auto result = old_school::run_tournament(
        1, kEvaluationSeed, bounded, tournament);

    CHECK(result.evaluation_seed == kEvaluationSeed);
    CHECK(result.effective_learned_bot.has_value());
    const auto& effective =
        *result.effective_learned_bot;
    CHECK(effective.kind == treatment.kind);
    CHECK(effective.learned_variant ==
          treatment.learned_variant);
    CHECK(effective.rollouts_per_action ==
          treatment.rollouts_per_action);
    CHECK(effective.exploration_rate ==
          treatment.exploration_rate);
    CHECK(effective.value_continuation_epsilon ==
          treatment.value_continuation_epsilon);
    CHECK(effective.value_priority_residual_weight ==
          treatment.value_priority_residual_weight);
    CHECK(effective.value_pass_dominance ==
          treatment.value_pass_dominance);
    CHECK(effective.value_continuation_controller ==
          treatment.value_continuation_controller);
    CHECK(effective.training_games ==
          treatment.training_games);
    CHECK(effective.learned_model == model);
    CHECK(
        result.effective_learned_model_fingerprint ==
        old_school::learned_model_fingerprint(model));
}

TEST(tournament_frozen_override_matches_legacy_rng_when_equivalent) {
    const auto model = small_value_model();
    old_school::GameConfig bounded;
    bounded.max_turns = 2;
    bounded.learned_model = model;
    const old_school::TournamentConfig legacy = {
        .bot_field = old_school::BotField::Learned,
        .learned_rollouts = 1,
        .learned_training_games = 1,
    };
    old_school::TournamentConfig frozen = legacy;
    frozen.frozen_learned_bot = old_school::BotConfig{
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .training_games = 1,
        .learned_model = model,
    };
    constexpr std::uint64_t kEvaluationSeed =
        0xAB5E470FFULL;
    const auto implicit = old_school::run_tournament(
        2, kEvaluationSeed, bounded, legacy);
    const auto explicit_frozen =
        old_school::run_tournament(
            2, kEvaluationSeed, bounded, frozen);

    CHECK(implicit.total_games ==
          explicit_frozen.total_games);
    CHECK(implicit.draws == explicit_frozen.draws);
    CHECK(implicit.life_total_finishes ==
          explicit_frozen.life_total_finishes);
    CHECK(implicit.empty_library_finishes ==
          explicit_frozen.empty_library_finishes);
    CHECK(implicit.turn_limit_draws ==
          explicit_frozen.turn_limit_draws);
    CHECK(implicit.total_turns ==
          explicit_frozen.total_turns);
    for (std::size_t deck = 0; deck < old_school::kDeckCount;
         ++deck) {
        CHECK(implicit.decks[deck].games ==
              explicit_frozen.decks[deck].games);
        CHECK(implicit.decks[deck].wins ==
              explicit_frozen.decks[deck].wins);
        CHECK(implicit.decks[deck].losses ==
              explicit_frozen.decks[deck].losses);
        CHECK(implicit.decks[deck].draws ==
              explicit_frozen.decks[deck].draws);
    }
    const std::size_t learned =
        static_cast<std::size_t>(
            old_school::BotKind::Learned);
    CHECK(implicit.bots[learned].wins ==
          explicit_frozen.bots[learned].wins);
    CHECK(implicit.bots[learned].losses ==
          explicit_frozen.bots[learned].losses);
    CHECK(implicit.bots[learned].draws ==
          explicit_frozen.bots[learned].draws);
    CHECK(implicit.bots[learned].total_decisions ==
          explicit_frozen.bots[learned].total_decisions);
    CHECK(implicit.bots[learned].total_rollouts ==
          explicit_frozen.bots[learned].total_rollouts);
    CHECK(
        implicit.effective_learned_model_fingerprint ==
        explicit_frozen
            .effective_learned_model_fingerprint);
}

TEST(tournament_rejects_malformed_frozen_learned_overrides) {
    old_school::GameConfig bounded;
    bounded.max_turns = 1;
    const auto rejects =
        [&](old_school::BotConfig frozen,
            old_school::BotField field) {
            old_school::TournamentConfig tournament = {
                .bot_field = field,
                .monte_carlo_rollouts = 1,
                .deep_monte_carlo_rollouts = 2,
                .learned_rollouts = 1,
                .learned_training_games = 0,
                .frozen_learned_bot = std::move(frozen),
            };
            bool rejected = false;
            try {
                static_cast<void>(
                    old_school::run_tournament(
                        1, 0xBADF202EULL, bounded,
                        tournament));
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            CHECK(rejected);
        };

    rejects(
        {
            .kind = old_school::BotKind::Random,
            .rollouts_per_action = 1,
            .learned_model = small_value_model(),
        },
        old_school::BotField::Mixed);
    rejects(
        {
            .kind = old_school::BotKind::Learned,
            .rollouts_per_action = 1,
        },
        old_school::BotField::Learned);
    rejects(
        {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 1,
            .learned_model = small_actor_model(),
        },
        old_school::BotField::Learned);
    rejects(
        {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .rollouts_per_action = 1,
            .learned_model = small_actor_model(),
        },
        old_school::BotField::Mixed);
    rejects(
        {
            .kind = old_school::BotKind::Learned,
            .rollouts_per_action = 1,
            .learned_model = small_value_model(),
        },
        old_school::BotField::Random);
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

    CHECK(result.evaluation_seed == 0xB07B07ULL);
    CHECK(result.total_games == 120);
    CHECK(result.challenger_model_fingerprint.empty());
    CHECK(result.baseline_model_fingerprint.empty());
    CHECK(result.challenger_stats.games == 120);
    CHECK(result.baseline_stats.games == 120);
    CHECK(result.challenger_stats.wins +
              result.challenger_stats.losses +
              result.challenger_stats.draws ==
          120);
    CHECK(result.challenger_stats.total_decisions > 0);
    CHECK(result.baseline_stats.total_decisions > 0);
    CHECK(result.total_turns >= result.total_games);
    CHECK(result.life_total_finishes +
              result.empty_library_finishes +
              result.turn_limit_draws ==
          result.total_games);
    for (std::size_t deck = 0;
         deck < result.challenger_decks.size(); ++deck) {
        CHECK(result.challenger_decks[deck].games == 24);
        CHECK(result.baseline_decks[deck].games == 24);
        CHECK(result.challenger_decks[deck].on_play_games == 12);
        CHECK(result.challenger_decks[deck].on_draw_games == 12);
        CHECK(result.baseline_decks[deck].on_play_games == 12);
        CHECK(result.baseline_decks[deck].on_draw_games == 12);
        for (std::size_t policy_seat = 0;
             policy_seat < 2; ++policy_seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                const auto& challenger_quadrant =
                    result.challenger_outcome_quadrants
                        [deck][policy_seat][play_draw];
                const auto& baseline_quadrant =
                    result.baseline_outcome_quadrants
                        [deck][policy_seat][play_draw];
                CHECK(challenger_quadrant.games == 6);
                CHECK(
                    challenger_quadrant.wins +
                        challenger_quadrant.losses +
                        challenger_quadrant.draws ==
                    challenger_quadrant.games);
                CHECK(baseline_quadrant.games == 6);
                CHECK(
                    baseline_quadrant.wins +
                        baseline_quadrant.losses +
                        baseline_quadrant.draws ==
                    baseline_quadrant.games);
            }
        }
    }
    std::size_t matrix_games = 0;
    std::size_t matrix_wins = 0;
    std::size_t matrix_losses = 0;
    std::size_t matrix_draws = 0;
    for (std::size_t challenger_deck = 0;
         challenger_deck <
         result.challenger_deck_matchups.size();
         ++challenger_deck) {
        old_school::DeckSimulationStats row;
        for (std::size_t baseline_deck = 0;
             baseline_deck <
             result.challenger_deck_matchups[challenger_deck].size();
             ++baseline_deck) {
            const auto& cell =
                result.challenger_deck_matchups[challenger_deck]
                                                [baseline_deck];
            CHECK(
                cell.games ==
                (challenger_deck == baseline_deck ? 8 : 4));
            CHECK(cell.wins + cell.losses + cell.draws == cell.games);
            row.games += cell.games;
            row.wins += cell.wins;
            row.losses += cell.losses;
            row.draws += cell.draws;
            matrix_games += cell.games;
            matrix_wins += cell.wins;
            matrix_losses += cell.losses;
            matrix_draws += cell.draws;
        }
        CHECK(row.games == result.challenger_decks[challenger_deck].games);
        CHECK(row.wins == result.challenger_decks[challenger_deck].wins);
        CHECK(row.losses == result.challenger_decks[challenger_deck].losses);
        CHECK(row.draws == result.challenger_decks[challenger_deck].draws);
    }
    for (std::size_t baseline_deck = 0;
         baseline_deck < old_school::kDeckCount;
         ++baseline_deck) {
        old_school::DeckSimulationStats reciprocal_column;
        for (std::size_t challenger_deck = 0;
             challenger_deck < old_school::kDeckCount;
             ++challenger_deck) {
            const auto& cell =
                result.challenger_deck_matchups[challenger_deck]
                                                [baseline_deck];
            reciprocal_column.games += cell.games;
            reciprocal_column.wins += cell.losses;
            reciprocal_column.losses += cell.wins;
            reciprocal_column.draws += cell.draws;
        }
        CHECK(
            reciprocal_column.games ==
            result.baseline_decks[baseline_deck].games);
        CHECK(
            reciprocal_column.wins ==
            result.baseline_decks[baseline_deck].wins);
        CHECK(
            reciprocal_column.losses ==
            result.baseline_decks[baseline_deck].losses);
        CHECK(
            reciprocal_column.draws ==
            result.baseline_decks[baseline_deck].draws);
    }
    CHECK(matrix_games == result.total_games);
    CHECK(matrix_wins == result.challenger_stats.wins);
    CHECK(matrix_losses == result.challenger_stats.losses);
    CHECK(matrix_draws == result.challenger_stats.draws);
    CHECK(result.confidence_low_95() >= 0.0);
    CHECK(result.confidence_high_95() <= 100.0);
    CHECK(result.confidence_low_95() <=
          result.challenger_win_rate());
    CHECK(result.confidence_high_95() >=
          result.challenger_win_rate());
    CHECK(result.challenger_quartet_cr1.clusters == 30);
    CHECK(result.challenger_quartet_cr1.records == 120);
    CHECK(
        result.challenger_quartet_cr1.mean ==
        (static_cast<double>(result.challenger_stats.wins) +
         0.5 *
             static_cast<double>(
                 result.challenger_stats.draws)) /
            120.0);
    CHECK(std::isfinite(
        result.challenger_quartet_cr1.standard_error));
    CHECK(
        result.challenger_quartet_cr1.confidence_low_95 <=
        result.challenger_quartet_cr1.mean);
    CHECK(
        result.challenger_quartet_cr1.confidence_high_95 >=
        result.challenger_quartet_cr1.mean);
}

TEST(bot_benchmark_exact_cyclic_quadrants_at_gate_repetitions) {
    const old_school::BotConfig random = {
        .kind = old_school::BotKind::Random,
        .rollouts_per_action = 1,
    };
    old_school::GameConfig bounded;
    bounded.max_turns = 1;
    const std::array<std::size_t, 2> repetitions = {
        5,
        34,
    };
    for (const std::size_t repetition : repetitions) {
        const std::uint64_t evaluation_seed =
            0xC1C11C5EEDULL + repetition;
        const auto first = old_school::run_bot_benchmark(
            repetition, evaluation_seed, random, random,
            bounded, true);
        const auto repeated =
            old_school::run_bot_benchmark(
                repetition, evaluation_seed, random, random,
                bounded, true);
        const std::size_t expected_quadrant =
            3 * repetition;
        const std::size_t expected_clusters =
            15 * repetition;
        const std::size_t expected_records =
            60 * repetition;
        CHECK(first.evaluation_seed == evaluation_seed);
        CHECK(first.total_games == expected_records);
        CHECK(first.challenger_quartet_cr1.clusters ==
              expected_clusters);
        CHECK(first.challenger_quartet_cr1.records ==
              expected_records);
        CHECK(first.challenger_quartet_cr1.mean == 0.5);
        CHECK(
            first.challenger_quartet_cr1.standard_error ==
            0.0);
        CHECK(
            first.challenger_quartet_cr1 ==
            repeated.challenger_quartet_cr1);
        CHECK(
            first.challenger_outcome_quadrants ==
            repeated.challenger_outcome_quadrants);
        CHECK(
            first.baseline_outcome_quadrants ==
            repeated.baseline_outcome_quadrants);
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            for (std::size_t policy_seat = 0;
                 policy_seat < 2; ++policy_seat) {
                for (std::size_t play_draw = 0;
                     play_draw < 2; ++play_draw) {
                    const auto& challenger_quadrant =
                        first.challenger_outcome_quadrants
                            [deck][policy_seat][play_draw];
                    const auto& baseline_quadrant =
                        first.baseline_outcome_quadrants
                            [deck][policy_seat][play_draw];
                    CHECK(challenger_quadrant.games ==
                          expected_quadrant);
                    CHECK(baseline_quadrant.games ==
                          expected_quadrant);
                    CHECK(challenger_quadrant.draws ==
                          expected_quadrant);
                    CHECK(baseline_quadrant.draws ==
                          expected_quadrant);
                }
            }
        }
    }
}

TEST(benchmark_strength_gate_is_challenger_deck_perspective) {
    old_school::BotBenchmarkSummary summary;
    summary.challenger_stats = {
        .games = 1'000,
        .wins = 600,
        .losses = 400,
    };
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        summary.challenger_decks[deck] = {
            .games = 200,
            .wins = 120,
            .losses = 80,
        };
        // Baseline deck buckets cover a different set of ordered
        // matchups. Their win counts are accounting evidence, not the
        // strength comparator for challenger deck `deck`.
        summary.baseline_decks[deck] = {
            .games = 200,
            .wins = 150,
            .losses = 50,
        };
    }
    CHECK(summary.confidence_low_95() > 50.0);
    CHECK(summary.challenger_is_better_95());

    summary.challenger_decks[
        static_cast<std::size_t>(
            old_school::DeckId::RUAggro)] = {
        .games = 200,
        .wins = 100,
        .losses = 100,
    };
    CHECK(!summary.challenger_is_better_95());
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
    CHECK(first.evaluation_seed == 101);
    CHECK(repeated.evaluation_seed == 101);
    CHECK(other_evaluation_seed.evaluation_seed == 424242);
    CHECK(
        first.challenger_model_fingerprint ==
        old_school::learned_model_fingerprint(
            shared_config.learned_model));
    CHECK(first.baseline_model_fingerprint.empty());
    CHECK(first.challenger_stats.wins ==
          repeated.challenger_stats.wins);
    CHECK(first.challenger_stats.losses ==
          repeated.challenger_stats.losses);
    CHECK(first.challenger_stats.draws ==
          repeated.challenger_stats.draws);
    CHECK(first.challenger_quartet_cr1 ==
          repeated.challenger_quartet_cr1);
    CHECK(first.challenger_outcome_quadrants ==
          repeated.challenger_outcome_quadrants);
    CHECK(first.baseline_outcome_quadrants ==
          repeated.baseline_outcome_quadrants);
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

TEST(benchmark_identical_policy_control_requires_explicit_permission) {
    const old_school::BotConfig random = {
        .kind = old_school::BotKind::Random,
        .rollouts_per_action = 1,
    };
    old_school::GameConfig bounded;
    bounded.max_turns = 1;

    bool rejected_by_default = false;
    try {
        static_cast<void>(old_school::run_bot_benchmark(
            1, 0x1D3A71CA1ULL, random, random, bounded));
    } catch (const std::invalid_argument&) {
        rejected_by_default = true;
    }
    CHECK(rejected_by_default);

    const auto control = old_school::run_bot_benchmark(
        1, 0x1D3A71CA1ULL, random, random, bounded, true);
    CHECK(control.total_games == 60);
    CHECK(control.challenger_stats.games == 60);
    CHECK(control.baseline_stats.games == 60);
    CHECK(control.challenger_stats.draws == 60);
    CHECK(control.baseline_stats.draws == 60);
}

TEST(benchmark_policy_identity_includes_value_pass_dominance) {
    const auto model = small_value_model();
    const old_school::BotConfig control = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 0,
        .training_games = 1,
        .learned_model = model,
    };
    old_school::BotConfig treatment = control;
    treatment.value_pass_dominance = true;
    old_school::GameConfig bounded;
    bounded.max_turns = 1;
    bounded.learned_model = model;

    const auto result = old_school::run_bot_benchmark(
        1, 0x5044301D3A71ULL, treatment, control, bounded);
    CHECK(result.total_games == 60);
    CHECK(result.challenger.value_pass_dominance);
    CHECK(!result.baseline.value_pass_dominance);
}

TEST(benchmark_policy_identity_includes_resolved_shallow_prior_weight) {
    const auto model = small_value_model();
    const old_school::BotConfig control = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 0,
        .training_games = 1,
        .learned_model = model,
    };
    CHECK(control.value_resolved_shallow_prior_weight == 0.0);
    old_school::BotConfig treatment = control;
    treatment.value_resolved_shallow_prior_weight = 0.5;
    old_school::GameConfig bounded;
    bounded.max_turns = 1;
    bounded.learned_model = model;

    const auto result = old_school::run_bot_benchmark(
        1, 0x5E771ED501ULL, treatment, control, bounded);
    CHECK(result.total_games == 60);
    CHECK(result.challenger.value_resolved_shallow_prior_weight ==
          0.5);
    CHECK(result.baseline.value_resolved_shallow_prior_weight ==
          0.0);
}

TEST(resolved_shallow_prior_default_off_is_rng_identity) {
    const auto model = small_value_model();
    old_school::GameConfig implicit;
    implicit.max_turns = 4;
    implicit.starting_player = 0;
    implicit.learned_model = model;
    implicit.bots = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 1,
            .learned_model = model,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 1,
            .learned_model = model,
        },
    };
    old_school::GameConfig explicit_off = implicit;
    for (auto& bot : explicit_off.bots) {
        bot.value_resolved_shallow_prior_weight = 0.0;
    }

    old_school::Game implicit_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0x5E771ED0FFULL, implicit);
    old_school::Game explicit_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0x5E771ED0FFULL, explicit_off);
    CHECK(implicit_game.run() == explicit_game.run());
    CHECK(implicit_game.state() == explicit_game.state());
}

TEST(value_actor_local_search_recipe_is_exact_and_default_off) {
    CHECK(!old_school::BotConfig{}.value_actor_local_search);
    constexpr std::uint64_t kSeed = 0xA04F0E51ULL;
    const auto recipe =
        old_school::learned_value_actor_local_search_config(
            kSeed);
    CHECK(recipe.seed == kSeed);
    CHECK(recipe.worlds ==
          old_school::kLearnedValueActorLocalSearchWorlds);
    CHECK(recipe.worlds == 8);
    CHECK(recipe.rollouts_per_world == 1);
    CHECK(recipe.horizon_turns == 8);
    CHECK(recipe.continuation_variant ==
          old_school::LearnedVariant::ValueSearchChampion);
    CHECK(recipe.value_continuation_epsilon == 0.0);
    CHECK(!recipe.blend_shallow_prior);
    CHECK(recipe.value_resolved_shallow_prior_weight == 0.0);
    CHECK(recipe.value_priority_residual_weight == 0.0);
    CHECK(!recipe.value_pass_dominance);
    CHECK(recipe.value_continuation_controller ==
          old_school::LearnedContinuationController::Legacy);
    CHECK(recipe.evaluation_threads == 4);
    CHECK(!recipe.capture_priority_h0_boundaries);
    CHECK(recipe.value_continuation_search_worlds == 2);
}

TEST(value_actor_local_priority_samples_ignore_real_attack_aggregation) {
    const auto model = small_value_model();
    old_school::BotConfig actor_local = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action =
            old_school::kLearnedValueActorLocalSearchWorlds,
        .value_actor_local_search = true,
        .learned_model = model,
    };
    auto combined = actor_local;
    combined.value_adversarial_blocks = true;
    constexpr std::uint64_t kSeed = 0xA015C0B1EDULL;
    const auto actor_local_search =
        old_school::learned_value_actor_local_priority_search_config(
            actor_local, kSeed);
    const auto combined_search =
        old_school::learned_value_actor_local_priority_search_config(
            combined, kSeed);
    CHECK(actor_local_search == combined_search);

    const auto fixture = ancestral_target_fixture();
    const auto actions = old_school::legal_priority_actions(
        fixture.state, 0, true);
    CHECK(actions.size() > 1);
    const auto actor_local_samples =
        old_school::learned_priority_action_samples(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0,
            actions, model, actor_local_search);
    const auto combined_samples =
        old_school::learned_priority_action_samples(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0,
            actions, model, combined_search);
    CHECK(actor_local_samples == combined_samples);

    auto invalid = combined;
    invalid.value_pass_dominance = true;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::
                    learned_value_actor_local_priority_search_config(
                        invalid, kSeed));
        },
        "exact AQ4-P1/AQ15"));
}

TEST(value_actor_local_search_fails_closed_and_off_is_rng_identity) {
    const auto model = small_value_model();
    old_school::BotConfig treatment = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action =
            old_school::kLearnedValueActorLocalSearchWorlds,
        .value_actor_local_search = true,
        .learned_model = model,
    };
    const auto construct =
        [&](const old_school::BotConfig& bot,
            std::size_t search_depth = 1) {
            old_school::GameConfig config;
            config.max_turns = 1;
            config.learned_search_depth = search_depth;
            config.bots[0] = bot;
            config.learned_model = model;
            old_school::Game game(
                old_school::blue_deck(),
                old_school::red_deck(), 1, config);
            static_cast<void>(game);
        };
    CHECK(throws_with_text(
        [&] { construct(treatment); },
        "exact frozen C16"));

    auto wrong_k = treatment;
    wrong_k.rollouts_per_action = 7;
    CHECK(throws_with_text(
        [&] { construct(wrong_k); },
        "exact AQ4-P1"));
    auto combined = treatment;
    combined.value_priority_residual_weight = 0.10;
    CHECK(throws_with_text(
        [&] { construct(combined); },
        "exact AQ4-P1"));
    auto actor = treatment;
    actor.learned_variant =
        old_school::LearnedVariant::UnifiedActor;
    actor.learned_model = small_actor_model();
    CHECK(throws_with_text(
        [&] { construct(actor); },
        "exact AQ4-P1"));

    old_school::GameConfig implicit;
    implicit.max_turns = 3;
    implicit.starting_player = 0;
    implicit.learned_model = model;
    implicit.bots = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 1,
            .learned_model = model,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 1,
            .learned_model = model,
        },
    };
    auto explicit_off = implicit;
    for (auto& bot : explicit_off.bots) {
        bot.value_actor_local_search = false;
    }
    old_school::Game implicit_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0xA04F0FFULL, implicit);
    old_school::Game explicit_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0xA04F0FFULL, explicit_off);
    CHECK(implicit_game.run() == explicit_game.run());
    CHECK(implicit_game.state() == explicit_game.state());
}

TEST(benchmark_policy_identity_includes_value_adversarial_blocks) {
    const auto model = small_value_model();
    const old_school::BotConfig control = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 0,
        .training_games = 1,
        .learned_model = model,
    };
    old_school::BotConfig treatment = control;
    treatment.value_adversarial_blocks = true;
    old_school::GameConfig bounded;
    bounded.max_turns = 1;
    bounded.learned_model = model;

    const auto result = old_school::run_bot_benchmark(
        1, 0xADB10C5ULL, treatment, control, bounded);
    CHECK(result.total_games == 60);
    CHECK(result.challenger.value_adversarial_blocks);
    CHECK(!result.baseline.value_adversarial_blocks);
}

TEST(benchmark_policy_identity_includes_value_continuation_controller) {
    const auto model = small_value_model();
    const old_school::BotConfig control = {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 0,
        .training_games = 1,
        .learned_model = model,
    };
    old_school::BotConfig treatment = control;
    treatment.value_continuation_controller =
        old_school::LearnedContinuationController::
            PublicStackPassV1;
    old_school::GameConfig bounded;
    bounded.max_turns = 1;
    bounded.learned_model = model;

    const auto result = old_school::run_bot_benchmark(
        1, 0xC0171D3A71ULL, treatment, control, bounded);
    CHECK(result.total_games == 60);
    CHECK(result.challenger.value_continuation_controller ==
          old_school::LearnedContinuationController::
              PublicStackPassV1);
    CHECK(result.baseline.value_continuation_controller ==
          old_school::LearnedContinuationController::Legacy);
}

TEST(value_continuation_controller_legacy_is_default_off_rng_identity) {
    const auto model = small_value_model();
    old_school::GameConfig implicit;
    implicit.max_turns = 3;
    implicit.starting_player = 0;
    implicit.learned_model = model;
    implicit.bots = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 1,
            .learned_model = model,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 1,
            .learned_model = model,
        },
    };
    auto explicit_legacy = implicit;
    for (auto& bot : explicit_legacy.bots) {
        bot.value_continuation_controller =
            old_school::LearnedContinuationController::Legacy;
    }

    old_school::Game implicit_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0xC0171DE4717EULL, implicit);
    old_school::Game explicit_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0xC0171DE4717EULL, explicit_legacy);
    CHECK(implicit_game.run() == explicit_game.run());
    CHECK(implicit_game.state() == explicit_game.state());
}

TEST(public_stack_controller_does_not_change_real_root_policy) {
    const auto model = small_value_model();
    old_school::GameConfig legacy;
    legacy.max_turns = 8;
    legacy.starting_player = 0;
    legacy.learned_model = model;
    legacy.learned_search_depth = 0;
    legacy.bots = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 0,
            .learned_model = model,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 0,
            .learned_model = model,
        },
    };
    auto treatment = legacy;
    for (auto& bot : treatment.bots) {
        bot.value_continuation_controller =
            old_school::LearnedContinuationController::
                PublicStackPassV1;
    }

    old_school::Game legacy_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0xC017A007ULL, legacy);
    old_school::Game treatment_game(
        old_school::blue_deck(),
        old_school::ru_aggro_deck(),
        0xC017A007ULL, treatment);
    CHECK(legacy_game.run() == treatment_game.run());
    CHECK(legacy_game.state() == treatment_game.state());
}

TEST(value_pass_dominance_rejects_non_value_bots) {
    const std::array<old_school::BotConfig, 2> invalid = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Random,
            .rollouts_per_action = 1,
            .value_pass_dominance = true,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .value_pass_dominance = true,
        },
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    for (const auto& challenger : invalid) {
        bool rejected = false;
        try {
            static_cast<void>(old_school::run_bot_benchmark(
                1, 0xBAD504430ULL, challenger, baseline));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }
}

TEST(value_adversarial_blocks_rejects_non_value_bots) {
    const std::array<old_school::BotConfig, 2> invalid = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Random,
            .rollouts_per_action = 1,
            .value_adversarial_blocks = true,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .value_adversarial_blocks = true,
        },
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    for (const auto& challenger : invalid) {
        bool rejected = false;
        try {
            static_cast<void>(old_school::run_bot_benchmark(
                1, 0xBADADB10CULL, challenger, baseline));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        CHECK(rejected);
    }
}

TEST(resolved_shallow_prior_rejects_non_value_bots) {
    const std::array<old_school::BotConfig, 2> invalid = {
        old_school::BotConfig{
            .kind = old_school::BotKind::Random,
            .rollouts_per_action = 1,
            .value_resolved_shallow_prior_weight = 0.5,
        },
        old_school::BotConfig{
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .rollouts_per_action = 0,
            .value_resolved_shallow_prior_weight = 0.5,
        },
    };
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    for (const auto& challenger : invalid) {
        CHECK(throws_with_text(
            [&] {
                static_cast<void>(
                    old_school::run_bot_benchmark(
                        1, 0xBAD5E771EDULL,
                        challenger, baseline));
            },
            "resolved shallow prior requires Learned Value"));
    }
}

TEST(resolved_shallow_prior_weight_rejects_invalid_numbers) {
    const std::array<double, 4> invalid_weights = {
        -0.01,
        1.01,
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),
    };
    const auto model = small_value_model();
    const old_school::BotConfig baseline = {
        .kind = old_school::BotKind::Handcrafted,
        .rollouts_per_action = 1,
    };
    for (const double weight : invalid_weights) {
        const old_school::BotConfig challenger = {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 0,
            .value_resolved_shallow_prior_weight = weight,
            .training_games = 1,
            .learned_model = model,
        };
        CHECK(throws_with_text(
            [&] {
                static_cast<void>(
                    old_school::run_bot_benchmark(
                        1, 0xBAD5E771ED2ULL,
                        challenger, baseline));
            },
            "resolved shallow prior weight must be finite and in [0, 1]"));
    }

    const auto fixture = ancestral_target_fixture();
    const auto actions =
        old_school::legal_priority_actions(
            fixture.state, 0, true);
    for (const double weight : invalid_weights) {
        const old_school::LearnedSearchConfig search = {
            .seed = 0xBAD5E771ED3ULL,
            .worlds = 1,
            .rollouts_per_world = 1,
            .horizon_turns = 0,
            .continuation_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .blend_shallow_prior = true,
            .value_resolved_shallow_prior_weight = weight,
        };
        CHECK(throws_with_text(
            [&] {
                static_cast<void>(
                    old_school::learned_priority_action_samples(
                        fixture.state, fixture.decks, 0, true,
                        old_school::TurnPhase::FirstMain, 0,
                        actions, model, search));
            },
            "resolved shallow prior weight must be finite and in [0, 1]"));
    }
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

    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::blue_deck(),
        old_school::red_deck(),
    };
    constexpr std::uint64_t kSeed = 0x504430425241494EULL;
    const auto default_off =
        old_school::diagnose_learned_value_priority(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 0,
            small_value_model(), 2, kSeed);
    const auto explicit_off =
        old_school::diagnose_learned_value_priority(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 0,
            small_value_model(), 2, kSeed, 0.0, 0.0, false);
    CHECK(default_off == explicit_off);
    CHECK(default_off.legal_actions == default_off.actions);
    CHECK(default_off.pass_dominated_actions.empty());

    const auto treatment =
        old_school::diagnose_learned_value_priority(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 0,
            small_value_model(), 2, kSeed, 0.0, 0.0, true);
    CHECK(treatment.legal_actions == default_off.legal_actions);
    CHECK(treatment.actions.size() + 2 ==
          treatment.legal_actions.size());
    CHECK(treatment.pass_dominated_actions.size() == 2);
    CHECK(std::find(
              treatment.pass_dominated_actions.begin(),
              treatment.pass_dominated_actions.end(),
              treatment.selected_action) ==
          treatment.pass_dominated_actions.end());
    CHECK(old_school::diagnose_learned_value_priority(
              hidden, decks, 0, true,
              old_school::TurnPhase::SecondMain, 0,
              small_value_model(), 2, kSeed, 0.0, 0.0, true) ==
          treatment);
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

old_school::GameState continuation_counter_war_state(
    std::size_t chooser) {
    const std::size_t opponent = 1 - chooser;
    old_school::GameState state;
    state.active_player = opponent;
    state.starting_player = opponent;
    state.turn_number = 7;
    state.next_stack_object_id = 3;
    state.players[chooser].hand = {
        old_school::CardId::Counterspell,
    };
    state.players[chooser].library = {
        old_school::CardId::Island,
        old_school::CardId::FlyingMen,
    };
    state.players[opponent].hand = {
        old_school::CardId::Mountain,
    };
    state.players[opponent].library = {
        old_school::CardId::LightningBolt,
        old_school::CardId::Mountain,
    };
    state.players[chooser].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 1,
            .card = old_school::CardId::FlyingMen,
            .controller = chooser,
        },
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 2,
            .card = old_school::CardId::Counterspell,
            .controller = opponent,
            .spell_target = 1,
        },
    };
    return state;
}

std::size_t spell_target_action_index(
    const std::vector<old_school::PriorityAction>& actions,
    old_school::StackObjectId target) {
    const auto action = std::find_if(
        actions.begin(), actions.end(),
        [target](const old_school::PriorityAction& candidate) {
            return candidate.spell_target == target;
        });
    if (action == actions.end()) {
        throw std::runtime_error(
            "fixture stack-target action is unavailable");
    }
    return static_cast<std::size_t>(
        std::distance(actions.begin(), action));
}

TEST(public_stack_controller_prunes_only_own_targets_for_both_seats) {
    constexpr auto controller =
        old_school::LearnedContinuationController::
            PublicStackPassV1;
    constexpr std::uint64_t seed = 0xC017C017ULL;
    for (const std::size_t chooser : {
             std::size_t{0}, std::size_t{1}}) {
        const auto state =
            continuation_counter_war_state(chooser);
        const auto actions =
            old_school::legal_priority_actions(
                state, chooser, false);
        const std::size_t own_spell =
            spell_target_action_index(actions, 1);
        const std::size_t opposing_counter =
            spell_target_action_index(actions, 2);
        std::vector<double> scores(actions.size(), -1.0);
        scores[priority_action_index(
            actions,
            old_school::PriorityActionKind::Pass)] = 0.0;
        scores[own_spell] = 10.0;
        scores[opposing_counter] = 5.0;

        const auto treatment =
            old_school::
                diagnose_learned_value_continuation_controller(
                    state, chooser, false,
                    old_school::TurnPhase::FirstMain, 0,
                    scores, controller, 0.0, seed);
        CHECK(treatment.legal_actions == actions);
        CHECK(treatment.controller_pruned_actions.size() == 1);
        CHECK(treatment.controller_pruned_actions.front() ==
              actions[own_spell]);
        CHECK(has_action(
            treatment.retained_actions,
            actions[opposing_counter]));
        CHECK(treatment.selected_action ==
              actions[opposing_counter]);

        const auto hidden =
            old_school::
                diagnose_learned_value_continuation_controller(
                    hidden_repartition(state, chooser),
                    chooser, false,
                    old_school::TurnPhase::FirstMain, 0,
                    scores, controller, 0.0, seed);
        CHECK(hidden == treatment);

        const auto legacy =
            old_school::
                diagnose_learned_value_continuation_controller(
                    state, chooser, false,
                    old_school::TurnPhase::FirstMain, 0,
                    scores,
                    old_school::
                        LearnedContinuationController::Legacy,
                    0.0, seed);
        CHECK(legacy.controller_pruned_actions.empty());
        CHECK(legacy.retained_actions == legacy.legal_actions);
        CHECK(legacy.selected_action == actions[own_spell]);
    }
}

TEST(public_stack_controller_applies_pd0_before_stack_pruning) {
    old_school::GameState state;
    state.active_player = 1;
    state.starting_player = 1;
    state.turn_number = 7;
    state.next_stack_object_id = 3;
    state.players[0].hand = {
        old_school::CardId::Counterspell,
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
    const auto actions =
        old_school::legal_priority_actions(state, 0, false);
    std::vector<double> scores(actions.size(), 0.0);
    const auto diagnostic =
        old_school::
            diagnose_learned_value_continuation_controller(
                state, 0, false,
                old_school::TurnPhase::FirstMain, 0,
                scores,
                old_school::LearnedContinuationController::
                    PublicStackPassV1,
                0.0, 0x504430C017ULL, true);
    CHECK(diagnostic.pass_dominated_actions.size() == 1);
    CHECK(diagnostic.pass_dominated_actions.front().spell_target ==
          1);
    CHECK(diagnostic.controller_pruned_actions.size() == 1);
    CHECK(diagnostic.controller_pruned_actions.front().spell_target ==
          2);
    CHECK(diagnostic.retained_actions.size() == 1);
    CHECK(diagnostic.retained_actions.front().kind ==
          old_school::PriorityActionKind::Pass);
    CHECK(diagnostic.selected_action.kind ==
          old_school::PriorityActionKind::Pass);
}

TEST(public_stack_controller_prefers_exact_pass_argmax_and_bounds_exploration) {
    const auto state = continuation_counter_war_state(0);
    const auto actions =
        old_school::legal_priority_actions(state, 0, false);
    const std::size_t pass = priority_action_index(
        actions, old_school::PriorityActionKind::Pass);
    const std::size_t own_spell =
        spell_target_action_index(actions, 1);
    const std::size_t opposing_counter =
        spell_target_action_index(actions, 2);
    constexpr auto controller =
        old_school::LearnedContinuationController::
            PublicStackPassV1;

    std::vector<double> tied(actions.size(), -1.0);
    tied[pass] = 4.0;
    tied[own_spell] = 100.0;
    tied[opposing_counter] = 4.0;
    const auto exact_tie =
        old_school::
            diagnose_learned_value_continuation_controller(
                state, 0, false,
                old_school::TurnPhase::FirstMain, 0,
                tied, controller, 0.0, 0x71EULL);
    CHECK(exact_tie.selected_action.kind ==
          old_school::PriorityActionKind::Pass);
    CHECK(!exact_tie.explored);
    CHECK(!exact_tie.tie_break_path_used);

    auto unique_non_pass = tied;
    unique_non_pass[opposing_counter] = 5.0;
    const auto unique =
        old_school::
            diagnose_learned_value_continuation_controller(
                state, 0, false,
                old_school::TurnPhase::FirstMain, 0,
                unique_non_pass, controller, 0.0, 0x71EULL);
    CHECK(unique.selected_action == actions[opposing_counter]);
    CHECK(!unique.explored);
    CHECK(unique.tie_break_path_used);

    bool saw_pass = false;
    bool saw_opposing_counter = false;
    for (std::uint64_t seed = 1; seed <= 128; ++seed) {
        const auto exploratory =
            old_school::
                diagnose_learned_value_continuation_controller(
                    state, 0, false,
                    old_school::TurnPhase::FirstMain, 0,
                    tied, controller, 1.0, seed);
        CHECK(exploratory.explored);
        CHECK(exploratory.selected_legal_index != own_spell);
        saw_pass =
            saw_pass ||
            exploratory.selected_legal_index == pass;
        saw_opposing_counter =
            saw_opposing_counter ||
            exploratory.selected_legal_index ==
                opposing_counter;
    }
    CHECK(saw_pass);
    CHECK(saw_opposing_counter);
}

TEST(public_stack_controller_retains_live_and_payable_force_spike) {
    for (const bool payable : {false, true}) {
        old_school::GameState state;
        state.active_player = 1;
        state.turn_number = 6;
        state.next_stack_object_id = 2;
        state.players[0].hand = {
            old_school::CardId::ForceSpike,
        };
        state.players[0].lands = {
            {.card = old_school::CardId::Island,
             .tapped = false},
        };
        state.players[1].lands = {
            {.card = old_school::CardId::Mountain,
             .tapped = !payable},
        };
        state.stack = {
            {
                .kind = old_school::StackObjectKind::Spell,
                .id = 1,
                .card = old_school::CardId::GrayOgre,
                .controller = 1,
            },
        };
        const auto actions =
            old_school::legal_priority_actions(
                state, 0, false);
        const std::size_t force_spike =
            spell_target_action_index(actions, 1);
        std::vector<double> scores(actions.size(), 0.0);
        scores[force_spike] = 1.0;
        const auto diagnostic =
            old_school::
                diagnose_learned_value_continuation_controller(
                    state, 0, false,
                    old_school::TurnPhase::FirstMain, 0,
                    scores,
                    old_school::
                        LearnedContinuationController::
                            PublicStackPassV1,
                    0.0, 0xF05CEULL, true);
        CHECK(diagnostic.controller_pruned_actions.empty());
        CHECK(has_action(
            diagnostic.retained_actions,
            actions[force_spike]));
        CHECK(diagnostic.selected_action ==
              actions[force_spike]);

        auto resolved = state;
        CHECK(old_school::apply_priority_action(
            resolved, 0, actions[force_spike], false));
        old_school::PriorityState priority = {
            .player = 0,
            .consecutive_passes = 0,
        };
        CHECK(old_school::pass_priority(resolved, priority) ==
              old_school::PriorityPassResult::Passed);
        CHECK(old_school::pass_priority(resolved, priority) ==
              old_school::PriorityPassResult::
                  StackObjectResolved);
        if (payable) {
            CHECK(resolved.stack.size() == 1);
            CHECK(resolved.stack.back().id == 1);
            CHECK(resolved.players[1].lands.front().tapped);
        } else {
            CHECK(resolved.stack.empty());
            CHECK(count_card(
                resolved.players[1].graveyard,
                old_school::CardId::GrayOgre) == 1);
        }
    }
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

old_school::HumanController delayed_burn_human_controller(
    std::size_t player, std::size_t first_burn_turn) {
    auto controller = developing_human_controller();
    controller.choose_priority_action =
        [player, first_burn_turn](
            const old_school::PlayerObservation& observation,
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
            if (observation.turn_number >= first_burn_turn) {
                const auto bolt = std::find_if(
                    actions.begin(), actions.end(),
                    [player](
                        const old_school::PriorityAction& action) {
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
            }
            return priority_action_index(
                actions, old_school::PriorityActionKind::Pass);
        };
    return controller;
}

TEST(sparse_contextual_trace_has_exact_legacy_state_pushes) {
    old_school::GameConfig config;
    config.max_turns = 12;
    config.starting_player = 0;
    constexpr std::uint64_t kSeed = 0x5A2A5EULL;

    std::vector<old_school::GameState> legacy_trace;
    old_school::Game legacy_game(
        old_school::blue_deck(),
        old_school::white_control_deck(), kSeed, config);
    const auto legacy_result =
        legacy_game.run_with_trace(legacy_trace);

    std::vector<old_school::LearnedDecisionTracePoint>
        contextual_trace;
    old_school::Game contextual_game(
        old_school::blue_deck(),
        old_school::white_control_deck(), kSeed, config);
    const auto contextual_result =
        contextual_game.run_with_learned_decision_trace(
            contextual_trace,
            old_school::LearnedDecisionTraceMode::Sparse);

    CHECK(contextual_result == legacy_result);
    CHECK(!legacy_trace.empty());
    CHECK(contextual_trace.size() == legacy_trace.size());
    for (std::size_t index = 0; index < legacy_trace.size();
         ++index) {
        CHECK(contextual_trace[index].state ==
              legacy_trace[index]);
        const auto& context = contextual_trace[index].context;
        CHECK(context.valid);
        CHECK(context.decision_player < 2);
        CHECK(context.consecutive_passes >= 0);
        CHECK(context.consecutive_passes <= 1);
        const auto encoded =
            old_school::learned_decision_context_features(
                context, context.decision_player);
        CHECK(std::any_of(
            encoded.begin(), encoded.end(),
            [](double value) { return value != 0.0; }));
    }
}

TEST(dense_contextual_trace_is_a_chronological_sparse_superset) {
    const auto deck = two_card_deck(
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt);
    old_school::GameConfig config;
    config.max_turns = 4;
    config.starting_player = 0;
    config.human_controllers[0] =
        delayed_burn_human_controller(0, 3);
    config.human_controllers[1] =
        delayed_burn_human_controller(1, 3);
    constexpr std::uint64_t kSeed = 0xD345EULL;

    std::vector<old_school::LearnedDecisionTracePoint> sparse;
    old_school::Game sparse_game(deck, deck, kSeed, config);
    const auto sparse_result =
        sparse_game.run_with_learned_decision_trace(
            sparse, old_school::LearnedDecisionTraceMode::Sparse);

    std::vector<old_school::LearnedDecisionTracePoint> dense;
    old_school::Game dense_game(deck, deck, kSeed, config);
    const auto dense_result =
        dense_game.run_with_learned_decision_trace(
            dense, old_school::LearnedDecisionTraceMode::Dense);

    CHECK(dense_result == sparse_result);
    CHECK(!sparse.empty());
    CHECK(dense.size() >= sparse.size());
    CHECK(dense.size() <
          old_school::kLearnedDenseDecisionTraceLimit);
    CHECK(std::is_sorted(
        dense.begin(), dense.end(),
        [](const old_school::LearnedDecisionTracePoint& left,
           const old_school::LearnedDecisionTracePoint& right) {
            return left.state.turn_number <
                   right.state.turn_number;
        }));

    std::size_t dense_index = 0;
    for (const auto& sparse_point : sparse) {
        while (dense_index < dense.size() &&
               !(dense[dense_index].state ==
                     sparse_point.state &&
                 dense[dense_index].context ==
                     sparse_point.context)) {
            ++dense_index;
        }
        CHECK(dense_index < dense.size());
        ++dense_index;
    }
}

TEST(dense_contextual_trace_cap_is_deterministic_and_retains_late_stack_stratum) {
    const auto deck = two_card_deck(
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt);
    old_school::GameConfig config;
    config.max_turns = 20;
    config.starting_player = 0;
    config.human_controllers[0] =
        delayed_burn_human_controller(0, 15);
    config.human_controllers[1] =
        delayed_burn_human_controller(1, 15);
    constexpr std::uint64_t kSeed = 0x64B0A7DULL;

    const auto run = [&] {
        std::vector<old_school::LearnedDecisionTracePoint> trace;
        old_school::Game game(deck, deck, kSeed, config);
        const auto result =
            game.run_with_learned_decision_trace(
                trace,
                old_school::LearnedDecisionTraceMode::Dense);
        return std::pair{result, std::move(trace)};
    };
    const auto [result, trace] = run();
    const auto [repeated_result, repeated_trace] = run();

    CHECK(result == repeated_result);
    CHECK(trace == repeated_trace);
    CHECK(trace.size() ==
          old_school::kLearnedDenseDecisionTraceLimit);
    CHECK(std::is_sorted(
        trace.begin(), trace.end(),
        [](const old_school::LearnedDecisionTracePoint& left,
           const old_school::LearnedDecisionTracePoint& right) {
            return left.state.turn_number <
                   right.state.turn_number;
        }));

    const auto pass_one_stack = std::find_if(
        trace.begin(), trace.end(),
        [](const old_school::LearnedDecisionTracePoint& point) {
            return point.context.valid &&
                   point.context.consecutive_passes == 1 &&
                   !point.state.stack.empty();
        });
    CHECK(pass_one_stack != trace.end());
    CHECK(pass_one_stack->state.turn_number >= 15);
    CHECK(pass_one_stack->context.decision_player !=
          pass_one_stack->state.stack.back().controller);

    const std::size_t perspective =
        pass_one_stack->context.decision_player;
    const auto repartitioned =
        hidden_repartition(pass_one_stack->state, perspective);
    CHECK(old_school::learned_contextual_observation(
              pass_one_stack->state, perspective,
              pass_one_stack->context) ==
          old_school::learned_contextual_observation(
              repartitioned, perspective,
              pass_one_stack->context));
}

TEST(dense_contextual_trace_smokes_all_five_decks) {
    const std::array<std::vector<old_school::CardId>, 5> decks = {
        old_school::green_deck(),
        old_school::red_deck(),
        old_school::blue_deck(),
        old_school::white_control_deck(),
        old_school::ru_aggro_deck(),
    };
    for (std::size_t deck = 0; deck < decks.size(); ++deck) {
        old_school::GameConfig config;
        config.max_turns = 2;
        config.starting_player = deck % 2;
        old_school::Game game(
            decks[deck], decks[(deck + 1) % decks.size()],
            0xF1DEDEC0ULL + deck, config);
        std::vector<old_school::LearnedDecisionTracePoint> trace;
        static_cast<void>(
            game.run_with_learned_decision_trace(
                trace,
                old_school::LearnedDecisionTraceMode::Dense));
        CHECK(!trace.empty());
        CHECK(trace.size() <=
              old_school::kLearnedDenseDecisionTraceLimit);
        CHECK(std::all_of(
            trace.begin(), trace.end(),
            [](const old_school::LearnedDecisionTracePoint& point) {
                return point.context.valid &&
                       point.context.decision_player < 2 &&
                       point.context.consecutive_passes >= 0 &&
                       point.context.consecutive_passes <= 1;
            }));
    }
}

TEST(priority_root_trace_records_the_selected_legal_action_without_changing_play) {
    old_school::GameConfig config;
    config.max_turns = 12;
    config.starting_player = 1;
    constexpr std::uint64_t kSeed = 0xB5A0AC710ULL;

    old_school::Game untraced(
        old_school::blue_deck(), old_school::red_deck(),
        kSeed, config);
    const auto untraced_result = untraced.run();
    const old_school::GameState untraced_state =
        untraced.state();

    old_school::Game traced(
        old_school::blue_deck(), old_school::red_deck(),
        kSeed, config);
    std::vector<old_school::LearnedDecisionTracePoint> roots;
    const auto traced_result =
        traced.run_with_priority_root_trace(roots);

    CHECK(traced_result == untraced_result);
    CHECK(traced.state() == untraced_state);
    CHECK(!roots.empty());
    for (const auto& root : roots) {
        CHECK(root.context.valid);
        CHECK(root.context.decision_player < 2);
        CHECK(root.selected_priority_action.has_value());
        const auto legal = old_school::legal_priority_actions(
            root.state, root.context.decision_player,
            root.context.sorcery_actions);
        CHECK(std::count(
                  legal.begin(), legal.end(),
                  *root.selected_priority_action) == 1);
    }
}

TEST(resolved_priority_consequence_observes_rules_effects_without_responses) {
    old_school::GameState land_state;
    land_state.active_player = 0;
    land_state.players[0].hand = {
        old_school::CardId::Forest,
    };
    const auto land =
        old_school::resolve_priority_action_consequence(
            land_state, 0, true, 0,
            old_school::PriorityAction::play_land(
                old_school::CardId::Forest));
    CHECK(land.has_value());
    CHECK(land->state.stack.empty());
    CHECK(land->state.players[0].hand.empty());
    CHECK(land->state.players[0].lands.size() == 1);
    CHECK(land->state.players[0].lands.front().card ==
          old_school::CardId::Forest);
    CHECK((land->priority ==
           old_school::PriorityState{
               .player = 0,
               .consecutive_passes = 0,
           }));
    CHECK(land->priority_passes == 0);
    CHECK(land->stack_resolutions == 0);
    CHECK(!land->window_ended);
    CHECK(!land->terminal);

    old_school::GameState draw_state;
    draw_state.active_player = 0;
    draw_state.next_stack_object_id = 9;
    draw_state.players[0].hand = {
        old_school::CardId::AncestralRecall,
    };
    draw_state.players[0].library = {
        old_school::CardId::FlyingMen,
        old_school::CardId::Counterspell,
        old_school::CardId::Island,
    };
    draw_state.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
    };
    const auto draw =
        old_school::resolve_priority_action_consequence(
            draw_state, 0, false, 0,
            old_school::PriorityAction::cast_ancestral_recall(
                old_school::Target::player_target(0)));
    CHECK(draw.has_value());
    CHECK(draw->state.stack.empty());
    CHECK(draw->state.players[0].library.empty());
    CHECK(draw->state.players[0].hand.size() == 3);
    CHECK(draw->state.players[0].graveyard ==
          std::vector<old_school::CardId>{
              old_school::CardId::AncestralRecall});
    CHECK((draw->priority ==
           old_school::PriorityState{
               .player = 0,
               .consecutive_passes = 0,
           }));
    CHECK(draw->priority_passes == 2);
    CHECK(draw->stack_resolutions == 1);
    CHECK(!draw->terminal);

    old_school::GameState lethal_state;
    lethal_state.active_player = 0;
    lethal_state.players[1].life = 3;
    lethal_state.stack = {
        {
            .kind = old_school::StackObjectKind::Spell,
            .id = 17,
            .card = old_school::CardId::LightningBolt,
            .controller = 0,
            .target =
                old_school::Target::player_target(1),
        },
    };
    const auto lethal =
        old_school::resolve_priority_action_consequence(
            lethal_state, 1, false, 1,
            old_school::PriorityAction::pass());
    CHECK(lethal.has_value());
    CHECK(lethal->state.stack.empty());
    CHECK(lethal->state.players[1].life == 0);
    CHECK(lethal->priority_passes == 1);
    CHECK(lethal->stack_resolutions == 1);
    CHECK(lethal->terminal);
    CHECK(lethal->winner == 0);

    old_school::GameState precedence_state;
    // Engine terminal checks give failed draws precedence over life totals.
    precedence_state.active_player = 0;
    precedence_state.failed_draw[0] = true;
    precedence_state.players[1].life = 0;
    const auto precedence =
        old_school::resolve_priority_action_consequence(
            precedence_state, 0, true, 0,
            old_school::PriorityAction::pass());
    CHECK(precedence.has_value());
    CHECK(precedence->terminal);
    CHECK(precedence->winner == 1);

    old_school::GameState counter_war;
    counter_war.active_player = 1;
    counter_war.next_stack_object_id = 3;
    counter_war.players[0].hand = {
        old_school::CardId::Counterspell,
    };
    counter_war.players[0].lands = {
        {.card = old_school::CardId::Island, .tapped = false},
        {.card = old_school::CardId::Island, .tapped = false},
    };
    counter_war.stack = {
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
    const auto counter =
        old_school::resolve_priority_action_consequence(
            counter_war, 0, false, 0,
            old_school::PriorityAction::cast_counterspell(2));
    CHECK(counter.has_value());
    CHECK(counter->state.stack.size() == 1);
    CHECK(counter->state.stack.front().id == 1);
    CHECK(counter->state.stack.front().card ==
          old_school::CardId::FlyingMen);
    CHECK(counter->state.players[0].graveyard ==
          std::vector<old_school::CardId>{
              old_school::CardId::Counterspell});
    CHECK(counter->state.players[1].graveyard ==
          std::vector<old_school::CardId>{
              old_school::CardId::Counterspell});
    CHECK(counter->state.stats[0].spells_countered == 1);
    CHECK(counter->priority_passes == 2);
    CHECK(counter->stack_resolutions == 1);

    const std::array<std::vector<old_school::CardId>, 2>
        counter_war_decks = {
            std::vector<old_school::CardId>{
                old_school::CardId::Counterspell,
                old_school::CardId::Island,
                old_school::CardId::Island,
                old_school::CardId::FlyingMen,
            },
            std::vector<old_school::CardId>{
                old_school::CardId::Counterspell,
            },
        };
    const auto counter_war_actions =
        old_school::legal_priority_actions(
            counter_war, 0, false);
    const auto counter_war_diagnostic =
        old_school::diagnose_learned_value_priority(
            counter_war, counter_war_decks, 0, false,
            old_school::TurnPhase::FirstMain, 0,
            small_value_model(), 0, 0xC0A17E2ULL,
            0.0, 0.0, false,
            old_school::LearnedContinuationController::Legacy,
            1.0);
    CHECK(counter_war_diagnostic
              .value_resolved_shallow_prior_weight == 1.0);
    CHECK(counter_war_diagnostic.legal_actions ==
          counter_war_actions);
    CHECK(counter_war_diagnostic.actions ==
          counter_war_actions);
    CHECK(counter_war_diagnostic.pass_dominated_actions.empty());

    old_school::GameState empty_pass;
    empty_pass.active_player = 0;
    const auto passed =
        old_school::resolve_priority_action_consequence(
            empty_pass, 0, true, 0,
            old_school::PriorityAction::pass());
    CHECK(passed.has_value());
    CHECK((passed->priority ==
           old_school::PriorityState{
               .player = 1,
               .consecutive_passes = 1,
           }));
    CHECK(passed->priority_passes == 1);
    CHECK(passed->stack_resolutions == 0);
    CHECK(!passed->window_ended);
}

TEST(resolved_shallow_prior_changes_only_the_paired_root_observation) {
    const auto fixture = ancestral_target_fixture();
    const auto model = small_value_model();
    const auto actions =
        old_school::legal_priority_actions(
            fixture.state, 0, true);
    const auto self_draw_position =
        std::find(
            actions.begin(), actions.end(),
            old_school::PriorityAction::cast_ancestral_recall(
                old_school::Target::player_target(0)));
    CHECK(self_draw_position != actions.end());
    const std::size_t self_draw =
        static_cast<std::size_t>(
            std::distance(
                actions.begin(), self_draw_position));
    const auto opponent_draw_position =
        std::find(
            actions.begin(), actions.end(),
            old_school::PriorityAction::cast_ancestral_recall(
                old_school::Target::player_target(1)));
    CHECK(opponent_draw_position != actions.end());
    const std::size_t opponent_draw =
        static_cast<std::size_t>(
            std::distance(
                actions.begin(), opponent_draw_position));

    old_school::LearnedSearchConfig control{
        .seed = 0xA11CE5701ULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .blend_shallow_prior = true,
    };
    auto explicit_zero = control;
    explicit_zero.value_resolved_shallow_prior_weight = 0.0;
    auto treatment = control;
    treatment.value_resolved_shallow_prior_weight = 1.0;
    auto midpoint = control;
    midpoint.value_resolved_shallow_prior_weight = 0.5;
    const auto before =
        old_school::learned_priority_action_samples(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, control);
    const auto zero =
        old_school::learned_priority_action_samples(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, explicit_zero);
    const auto after =
        old_school::learned_priority_action_samples(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, treatment);
    const auto half =
        old_school::learned_priority_action_samples(
            fixture.state, fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, midpoint);

    const auto check_same_samples =
        [](const old_school::LearnedActionSamples& left,
           const old_school::LearnedActionSamples& right) {
            CHECK(left.q_samples == right.q_samples);
            CHECK(left.priority_shallow_prior_samples ==
                  right.priority_shallow_prior_samples);
            CHECK(left.priority_continuation_samples ==
                  right.priority_continuation_samples);
            CHECK(left.exact_priority_aggregate_scores ==
                  right.exact_priority_aggregate_scores);
            CHECK(left.sampled_worlds == right.sampled_worlds);
            CHECK(left.rollout_evaluations ==
                  right.rollout_evaluations);
            CHECK(left.terminal_evaluations ==
                  right.terminal_evaluations);
            CHECK(left.bootstrapped_evaluations ==
                  right.bootstrapped_evaluations);
            CHECK(left.priority_h0_boundaries ==
                  right.priority_h0_boundaries);
        };
    check_same_samples(zero, before);
    CHECK(before.sampled_worlds == after.sampled_worlds);
    CHECK(before.rollout_evaluations ==
          after.rollout_evaluations);
    CHECK(before.terminal_evaluations ==
          after.terminal_evaluations);
    CHECK(before.bootstrapped_evaluations ==
          after.bootstrapped_evaluations);
    CHECK(before.priority_continuation_samples ==
          after.priority_continuation_samples);
    CHECK(before.priority_continuation_samples ==
          half.priority_continuation_samples);
    CHECK(before.priority_shallow_prior_samples !=
          after.priority_shallow_prior_samples);
    CHECK(half.priority_shallow_prior_samples.size() ==
          before.priority_shallow_prior_samples.size());
    for (std::size_t action_index = 0;
         action_index <
         half.priority_shallow_prior_samples.size();
         ++action_index) {
        CHECK(
            half.priority_shallow_prior_samples[action_index].size() ==
            before.priority_shallow_prior_samples[action_index].size());
        for (std::size_t sample = 0;
             sample <
             half.priority_shallow_prior_samples[action_index].size();
             ++sample) {
            CHECK(
                half.priority_shallow_prior_samples[action_index][sample] ==
                std::lerp(
                    before.priority_shallow_prior_samples[action_index]
                                                         [sample],
                    after.priority_shallow_prior_samples[action_index]
                                                        [sample],
                    0.5));
        }
    }
    CHECK(after.priority_shallow_prior_samples[self_draw] !=
          after.priority_shallow_prior_samples[opponent_draw]);
    CHECK(after.priority_shallow_prior_samples[self_draw].front() >
          after.priority_shallow_prior_samples[opponent_draw]
                                              .front());
    CHECK(half.priority_shallow_prior_samples[self_draw].front() >
          half.priority_shallow_prior_samples[opponent_draw]
                                             .front());

    const auto hidden =
        old_school::learned_priority_action_samples(
            hidden_repartition(fixture.state, 0),
            fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, treatment);
    CHECK(hidden.q_samples == after.q_samples);
    CHECK(hidden.priority_shallow_prior_samples ==
          after.priority_shallow_prior_samples);
    CHECK(hidden.priority_continuation_samples ==
          after.priority_continuation_samples);
    CHECK(hidden.exact_priority_aggregate_scores ==
          after.exact_priority_aggregate_scores);
    CHECK(hidden.rollout_evaluations ==
          after.rollout_evaluations);

    const auto hidden_half =
        old_school::learned_priority_action_samples(
            hidden_repartition(fixture.state, 0),
            fixture.decks, 0, true,
            old_school::TurnPhase::FirstMain, 0, actions,
            model, midpoint);
    check_same_samples(hidden_half, half);

    auto unblended = treatment;
    unblended.blend_shallow_prior = false;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_priority_action_samples(
                    fixture.state, fixture.decks, 0, true,
                    old_school::TurnPhase::FirstMain, 0,
                    actions, model, unblended));
        },
        "requires shallow-prior blending"));

    auto actor_search = treatment;
    actor_search.continuation_variant =
        old_school::LearnedVariant::UnifiedActor;
    CHECK(throws_with_text(
        [&] {
            static_cast<void>(
                old_school::learned_priority_action_samples(
                    fixture.state, fixture.decks, 0, true,
                    old_school::TurnPhase::FirstMain, 0,
                    actions, small_actor_model(),
                    actor_search));
        },
        "requires a Value-mirror search"));
}

TEST(contextual_shallow_value_uses_each_live_priority_successor) {
    const auto model = context_sensitive_value_model();
    auto fixture = determinization_fixture();
    constexpr std::uint64_t kStackSeed = 0xC07E57ACULL;
    const auto sampled_world =
        [](const old_school::GameState& state,
           const std::array<std::vector<old_school::CardId>, 2>&
               decks,
           std::size_t player, std::uint64_t seed) {
            std::mt19937_64 random(seed);
            return old_school::sample_determinization(
                state, decks, player, random());
        };
    const auto pass_score =
        [](const old_school::LearnedValuePriorityDiagnostic&
               diagnostic) {
            return diagnostic.scores[priority_action_index(
                diagnostic.actions,
                old_school::PriorityActionKind::Pass)];
        };

    const auto pass_zero =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, false,
            old_school::TurnPhase::BeginCombat, 0, model, 0,
            kStackSeed);
    auto pass_zero_successor =
        sampled_world(
            fixture.state, fixture.decks, 0, kStackSeed);
    old_school::PriorityState pass_zero_priority = {
        .player = 0,
        .consecutive_passes = 0,
    };
    CHECK(old_school::pass_priority(
              pass_zero_successor, pass_zero_priority) ==
          old_school::PriorityPassResult::Passed);
    const old_school::LearnedDecisionContext
        pass_zero_context = {
            .valid = true,
            .phase = old_school::TurnPhase::BeginCombat,
            .decision_player = 1,
            .consecutive_passes = 1,
            .sorcery_actions = false,
        };
    const double expected_pass_zero =
        old_school::learned_contextual_critic_value(
            pass_zero_successor, 0, pass_zero_context, model);
    CHECK(pass_score(pass_zero) == expected_pass_zero);
    CHECK(std::abs(
              expected_pass_zero -
              old_school::learned_critic_value(
                  pass_zero_successor, 0, model)) >
          1.0e-6);

    const auto pass_one =
        old_school::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, false,
            old_school::TurnPhase::BeginCombat, 1, model, 0,
            kStackSeed);
    auto pass_one_successor =
        sampled_world(
            fixture.state, fixture.decks, 0, kStackSeed);
    old_school::PriorityState pass_one_priority = {
        .player = 0,
        .consecutive_passes = 1,
    };
    CHECK(old_school::pass_priority(
              pass_one_successor, pass_one_priority) ==
          old_school::PriorityPassResult::
              StackObjectResolved);
    CHECK(pass_one_priority.player ==
          pass_one_successor.active_player);
    CHECK(pass_one_priority.consecutive_passes == 0);
    const old_school::LearnedDecisionContext pass_one_context = {
        .valid = true,
        .phase = old_school::TurnPhase::BeginCombat,
        .decision_player = pass_one_successor.active_player,
        .consecutive_passes = 0,
        .sorcery_actions = false,
    };
    CHECK(pass_score(pass_one) ==
          old_school::learned_contextual_critic_value(
              pass_one_successor, 0, pass_one_context, model));

    const auto white_state =
        old_school::white_lock_plan_diagnostic_state();
    const std::array<std::vector<old_school::CardId>, 2>
        white_decks = {
            old_school::white_control_deck(),
            old_school::red_deck(),
        };
    constexpr std::uint64_t kWhiteSeed = 0xCA57C07EULL;
    const auto cast =
        old_school::diagnose_learned_value_priority(
            white_state, white_decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, 0,
            kWhiteSeed);
    const std::size_t cast_index = priority_action_index(
        cast.actions,
        old_school::PriorityActionKind::CastEnchantment);
    auto cast_successor =
        sampled_world(
            white_state, white_decks, 0, kWhiteSeed);
    CHECK(old_school::apply_priority_action(
        cast_successor, 0, cast.actions[cast_index], true));
    const old_school::LearnedDecisionContext cast_context = {
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    CHECK(cast.scores[cast_index] ==
          old_school::learned_contextual_critic_value(
              cast_successor, 0, cast_context, model));

    const auto ended =
        old_school::diagnose_learned_value_priority(
            white_state, white_decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, model, 0,
            kWhiteSeed);
    CHECK(pass_score(ended) == 0.5);

    const auto legacy = small_value_model();
    const auto legacy_ended =
        old_school::diagnose_learned_value_priority(
            white_state, white_decks, 0, true,
            old_school::TurnPhase::FirstMain, 1, legacy, 0,
            kWhiteSeed);
    auto legacy_successor =
        sampled_world(
            white_state, white_decks, 0, kWhiteSeed);
    old_school::PriorityState legacy_priority = {
        .player = 0,
        .consecutive_passes = 1,
    };
    CHECK(old_school::pass_priority(
              legacy_successor, legacy_priority) ==
          old_school::PriorityPassResult::WindowEnded);
    CHECK(pass_score(legacy_ended) ==
          old_school::learned_critic_value(
              legacy_successor, 0, legacy));
}

TEST(contextual_combat_value_uses_end_combat_and_hides_opponent_cards) {
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 6;
    state.next_permanent_id = 2;
    state.players[0].library = {
        old_school::CardId::Forest,
        old_school::CardId::GiantGrowth,
    };
    state.players[0].hand = {
        old_school::CardId::Forest,
    };
    state.players[0].creatures = {
        creature(
            1, old_school::CardId::GrizzlyBears, false),
    };
    state.players[1].hand = {
        old_school::CardId::Mountain,
        old_school::CardId::LightningBolt,
    };
    state.players[1].library = {
        old_school::CardId::LightningBolt,
        old_school::CardId::Mountain,
    };
    const std::vector<std::vector<old_school::PermanentId>>
        candidates = {{1}};
    const auto model = context_sensitive_value_model();
    const auto scores =
        old_school::learned_value_attack_set_scores(
            state, 0, candidates, model, 0xE0DC0B47ULL);
    CHECK(scores.scores.size() == 1);

    auto successor = state;
    CHECK(old_school::resolve_combat(
        successor, 0, candidates.front(), {}));
    const old_school::LearnedDecisionContext context = {
        .valid = true,
        .phase = old_school::TurnPhase::EndCombat,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = false,
    };
    const double expected =
        old_school::learned_contextual_critic_value(
            successor, 0, context, model);
    CHECK(scores.scores.front() == expected);
    CHECK(std::abs(
              expected -
              old_school::learned_critic_value(
                  successor, 0, model)) >
          1.0e-6);

    const auto hidden = hidden_repartition(state, 0);
    CHECK(old_school::learned_value_attack_set_scores(
              hidden, 0, candidates, model,
              0xE0DC0B47ULL)
              .scores == scores.scores);
}

TEST(contextual_horizon_bootstraps_at_next_first_main_and_window_end_is_neutral) {
    const auto state =
        old_school::white_lock_plan_diagnostic_state();
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::white_control_deck(),
        old_school::red_deck(),
    };
    const auto model = context_sensitive_value_model();
    const std::vector<old_school::PriorityAction> candidates = {
        old_school::PriorityAction::pass(),
    };
    old_school::LearnedSearchConfig search{
        .seed = 0xF1257A11ULL,
        .worlds = 1,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .blend_shallow_prior = false,
    };
    const auto continuation =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, candidates,
            model, search);
    CHECK(continuation.q_samples.size() == 1);
    CHECK(continuation.q_samples.front().size() == 1);

    auto next_turn = state;
    old_school::PriorityState priority = {
        .player = 0,
        .consecutive_passes = 1,
    };
    CHECK(old_school::pass_priority(
              next_turn, priority) ==
          old_school::PriorityPassResult::WindowEnded);
    old_school::cleanup_turn(
        next_turn, next_turn.active_player, {});
    ++next_turn.turn_number;
    next_turn.active_player = 1;
    old_school::begin_turn(next_turn, 1);
    CHECK(!next_turn.players[1].library.empty());
    next_turn.players[1].hand.push_back(
        next_turn.players[1].library.back());
    next_turn.players[1].library.pop_back();
    ++next_turn.stats[1].cards_drawn;
    const old_school::LearnedDecisionContext next_context = {
        .valid = true,
        .phase = old_school::TurnPhase::FirstMain,
        .decision_player = 1,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    const double expected =
        old_school::learned_contextual_critic_value(
            next_turn, 0, next_context, model);
    CHECK(continuation.q_samples.front().front() == expected);

    search.blend_shallow_prior = true;
    const auto blended =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1, candidates,
            model, search);
    CHECK(blended.q_samples.size() == 1);
    CHECK(blended.q_samples.front().size() == 1);
    CHECK(blended.q_samples.front().front() ==
          (expected + 0.5) / 2.0);
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
        small_value_model(),
        {
            .human_deck = old_school::DeckId::Blue,
            .learned_deck = old_school::DeckId::Green,
        },
        1);
    const std::string transcript = output.str();
    CHECK(result.abandoned || result.game.has_value());
    CHECK(transcript.find("CLEANUP | DISCARD 1 MORE") !=
          std::string::npos);
    CHECK(transcript.find("[DISCARD] You discard:") !=
          std::string::npos);
}

TEST(all_bot_kinds_complete_cleanup_deterministically) {
    struct Policy {
        old_school::BotKind kind =
            old_school::BotKind::Random;
        old_school::LearnedVariant variant =
            old_school::LearnedVariant::
                ValueSearchChampion;
    };
    for (const auto policy : std::array{
             Policy{.kind = old_school::BotKind::Random},
             Policy{.kind = old_school::BotKind::MonteCarlo},
             Policy{
                 .kind =
                     old_school::BotKind::DeepMonteCarlo},
             Policy{.kind = old_school::BotKind::Handcrafted},
             Policy{
                 .kind = old_school::BotKind::Learned,
                 .variant =
                     old_school::LearnedVariant::
                         ValueSearchChampion,
             },
             Policy{
                 .kind = old_school::BotKind::Learned,
                 .variant =
                     old_school::LearnedVariant::UnifiedActor,
             },
         }) {
        old_school::GameConfig config;
        config.max_turns = 4;
        config.starting_player = 0;
        const auto model =
            policy.kind != old_school::BotKind::Learned
                ? std::shared_ptr<
                      const old_school::LearnedModel>{}
                : policy.variant ==
                          old_school::LearnedVariant::
                              UnifiedActor
                      ? small_actor_model()
                      : small_value_model();
        for (auto& bot : config.bots) {
            bot = {
                .kind = policy.kind,
                .learned_variant = policy.variant,
                .rollouts_per_action = 1,
                .training_games = 1,
                .learned_model = model,
            };
        }
        config.learned_model = model;
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

TEST(learned_continuation_performs_overfull_cleanup) {
    const std::array<std::vector<old_school::CardId>, 2> decks = {
        old_school::blue_deck(),
        old_school::green_deck(),
    };
    old_school::GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 5;
    state.players[0].hand.assign(
        decks[0].begin(), decks[0].begin() + 8);
    state.players[0].library.assign(
        decks[0].begin() + 8, decks[0].end());
    state.players[1].hand.assign(
        decks[1].begin(), decks[1].begin() + 7);
    state.players[1].library.assign(
        decks[1].begin() + 7, decks[1].end());
    const std::vector<old_school::PriorityAction> candidates = {
        old_school::PriorityAction::pass(),
    };
    const old_school::LearnedSearchConfig search = {
        .seed = 0xC1EA4C017ULL,
        .worlds = 2,
        .rollouts_per_world = 1,
        .horizon_turns = 0,
        .continuation_variant =
            old_school::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto samples =
        old_school::learned_priority_action_samples(
            state, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            candidates, small_actor_model(), search);
    CHECK(samples.rollout_evaluations == 2);
    CHECK(samples.q_samples.size() == 1);
    CHECK(samples.q_samples[0].size() == 2);
    CHECK(std::all_of(
        samples.q_samples[0].begin(),
        samples.q_samples[0].end(),
        [](double value) {
            return std::isfinite(value);
        }));

    const auto hidden = hidden_repartition(state, 0);
    const auto repeated =
        old_school::learned_priority_action_samples(
            hidden, decks, 0, true,
            old_school::TurnPhase::SecondMain, 1,
            candidates, small_actor_model(), search);
    CHECK(repeated.q_samples == samples.q_samples);
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

TEST(five_deck_random_matrix_tracks_the_exact_current_lists) {
    const auto result = old_school::run_tournament(30'000, 303);
    CHECK(result.total_games == 300'000);
    const std::array<double, 10> expected_first_deck_rates = {
        61.7, 56.4, 54.7, 67.6, 48.9,
        54.9, 42.0, 90.1, 48.8, 38.6,
    };
    CHECK(result.matchups.size() ==
          expected_first_deck_rates.size());
    for (std::size_t index = 0;
         index < result.matchups.size(); ++index) {
        const auto& matchup = result.matchups[index];
        CHECK(matchup.result.draws == 0);
        const double first_rate =
            matchup.result.decks[0].win_rate();
        CHECK(std::abs(
                  first_rate -
                  expected_first_deck_rates[index]) <= 1.0);
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
