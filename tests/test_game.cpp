#include "alpha/game.hpp"
#include "alpha/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

void remove_fixture_card(std::vector<alpha::CardId>& cards,
                         alpha::CardId card) {
    const auto position = std::find(cards.begin(), cards.end(), card);
    if (position == cards.end()) {
        throw std::runtime_error("fixture card is missing from deck");
    }
    cards.erase(position);
}

struct DeterminizationFixture {
    alpha::GameState state;
    std::array<std::vector<alpha::CardId>, 2> decks;
};

DeterminizationFixture determinization_fixture() {
    DeterminizationFixture fixture{
        .state = {},
        .decks = {
            alpha::white_control_deck(),
            alpha::blue_alpha_deck(),
        },
    };
    auto& state = fixture.state;
    state.active_player = 1;
    state.turn_number = 9;

    state.players[0].hand = {
        alpha::CardId::Plains,
        alpha::CardId::Moat,
        alpha::CardId::Plains,
    };
    state.players[0].graveyard = {alpha::CardId::Moat};
    state.players[0].lands = {
        {.card = alpha::CardId::Plains, .tapped = true},
    };
    state.players[0].artifacts = {
        {.id = 41, .card = alpha::CardId::Millstone, .tapped = true},
    };

    state.players[1].graveyard = {
        alpha::CardId::Counterspell,
        alpha::CardId::Island,
    };
    state.players[1].lands = {
        {.card = alpha::CardId::Island, .tapped = true},
    };
    state.players[1].creatures = {
        creature(51, alpha::CardId::WaterElemental),
    };

    state.stack = {
        {
            .kind = alpha::StackObjectKind::Spell,
            .id = 70,
            .card = alpha::CardId::WaterElemental,
            .controller = 1,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        },
        {
            .kind = alpha::StackObjectKind::ActivatedAbility,
            .id = 71,
            .card = alpha::CardId::Millstone,
            .controller = 0,
            .target = alpha::Target::player_target(1),
            .spell_target = std::nullopt,
        },
    };

    auto observer_library = fixture.decks[0];
    for (const alpha::CardId card : state.players[0].hand) {
        remove_fixture_card(observer_library, card);
    }
    remove_fixture_card(observer_library, alpha::CardId::Moat);
    remove_fixture_card(observer_library, alpha::CardId::Plains);
    remove_fixture_card(observer_library, alpha::CardId::Millstone);
    state.players[0].library = std::move(observer_library);

    auto opponent_hidden = fixture.decks[1];
    remove_fixture_card(opponent_hidden, alpha::CardId::Counterspell);
    remove_fixture_card(opponent_hidden, alpha::CardId::Island);
    remove_fixture_card(opponent_hidden, alpha::CardId::Island);
    remove_fixture_card(opponent_hidden, alpha::CardId::WaterElemental);
    // A spell stack object is another physical Water Elemental.
    remove_fixture_card(opponent_hidden, alpha::CardId::WaterElemental);
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

std::vector<alpha::CardId>
physical_cards(const alpha::GameState& state, std::size_t player) {
    std::vector<alpha::CardId> cards;
    const auto& player_state = state.players[player];
    cards.insert(cards.end(), player_state.library.begin(),
                 player_state.library.end());
    cards.insert(cards.end(), player_state.hand.begin(),
                 player_state.hand.end());
    cards.insert(cards.end(), player_state.graveyard.begin(),
                 player_state.graveyard.end());
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
            object.kind == alpha::StackObjectKind::Spell) {
            cards.push_back(object.card);
        }
    }
    std::sort(cards.begin(), cards.end());
    return cards;
}

std::shared_ptr<const alpha::LearnedModel> small_actor_model() {
    static const auto model =
        alpha::train_learned_actor_model(1, 0xAC70E7A1ULL);
    return model;
}

std::shared_ptr<const alpha::LearnedModel> small_value_model() {
    static const auto model =
        alpha::train_learned_value_champion(1, 0xC4A6E7A1ULL);
    return model;
}

alpha::GameState hidden_repartition(
    const alpha::GameState& state, std::size_t observer) {
    alpha::GameState changed = state;
    std::reverse(changed.players[observer].library.begin(),
                 changed.players[observer].library.end());
    const std::size_t opponent = 1 - observer;
    auto& hand = changed.players[opponent].hand;
    auto& library = changed.players[opponent].library;
    if (!hand.empty() && !library.empty()) {
        const auto different = std::find_if(
            library.begin(), library.end(),
            [&](alpha::CardId card) {
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
    alpha::CardId blocker_card) {
    const bool red_blocker =
        blocker_card == alpha::CardId::FireElemental;
    DeterminizationFixture fixture{
        .state = {},
        .decks = {
            alpha::green_alpha_deck(),
            red_blocker ? alpha::red_alpha_deck()
                        : alpha::green_alpha_deck(),
        },
    };
    auto& state = fixture.state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 11;
    state.players[0].land_played_this_turn = true;
    state.players[0].lands.assign(
        5, alpha::LandPermanent{
               .card = alpha::CardId::Forest,
               .tapped = false,
           });
    state.players[0].creatures = {
        creature(1, alpha::CardId::IronrootTreefolk),
    };
    state.players[1].lands.assign(
        5, alpha::LandPermanent{
               .card = red_blocker ? alpha::CardId::Mountain
                                   : alpha::CardId::Forest,
               .tapped = false,
           });
    state.players[1].creatures = {
        creature(2, blocker_card),
    };

    for (std::size_t player = 0; player < fixture.decks.size();
         ++player) {
        std::vector<alpha::CardId> hidden = fixture.decks[player];
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

TEST(determinization_is_reproducible_and_preserves_observer_hand) {
    const auto fixture = determinization_fixture();
    constexpr std::uint64_t kSeed = 0xD37E2A11ULL;
    const auto first = alpha::sample_determinization(
        fixture.state, fixture.decks, 0, kSeed);
    const auto repeated = alpha::sample_determinization(
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
    CHECK(first.stack.size() == fixture.state.stack.size());
    CHECK(first.stack[0].id == fixture.state.stack[0].id);
    CHECK(first.stack[1].kind ==
          alpha::StackObjectKind::ActivatedAbility);

    const auto other_observer = alpha::sample_determinization(
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
        [&](alpha::CardId card) {
            return card != altered.players[1].hand.front();
        });
    CHECK(different_card != altered.players[1].library.end());
    std::iter_swap(altered.players[1].hand.begin(), different_card);
    std::reverse(altered.players[1].hand.begin(),
                 altered.players[1].hand.end());
    std::reverse(altered.players[1].library.begin(),
                 altered.players[1].library.end());

    constexpr std::uint64_t kSeed = 0x1AF05E7ULL;
    const auto original_sample = alpha::sample_determinization(
        fixture.state, fixture.decks, 0, kSeed);
    const auto altered_sample = alpha::sample_determinization(
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
    const auto sampled = alpha::sample_determinization(
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
                     alpha::CardId::Millstone) ==
          count_card(fixture.decks[0], alpha::CardId::Millstone));
    CHECK(count_card(physical_cards(sampled, 1),
                     alpha::CardId::WaterElemental) ==
          count_card(fixture.decks[1],
                     alpha::CardId::WaterElemental));
}

TEST(determinization_varies_by_seed_and_rejects_invalid_public_state) {
    const auto fixture = determinization_fixture();
    const auto baseline = alpha::sample_determinization(
        fixture.state, fixture.decks, 0, 0x5EEDULL);
    bool found_variation = false;
    for (std::uint64_t seed = 1; seed <= 16; ++seed) {
        const auto candidate = alpha::sample_determinization(
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
    invalid.players[1].graveyard.push_back(alpha::CardId::Forest);
    bool rejected = false;
    try {
        static_cast<void>(alpha::sample_determinization(
            invalid, fixture.decks, 0, 0xBADULL));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

TEST(learned_observation_excludes_opponent_hidden_card_identities) {
    const auto fixture = determinization_fixture();
    const auto baseline =
        alpha::learned_observation(fixture.state, 0);

    auto changed_hidden = fixture.state;
    std::fill(changed_hidden.players[1].hand.begin(),
              changed_hidden.players[1].hand.end(),
              alpha::CardId::LightningBolt);
    std::fill(changed_hidden.players[1].library.begin(),
              changed_hidden.players[1].library.end(),
              alpha::CardId::Forest);
    CHECK(alpha::learned_observation(changed_hidden, 0) == baseline);

    auto changed_own_hand = fixture.state;
    changed_own_hand.players[0].hand[0] =
        alpha::CardId::LightningBolt;
    CHECK(alpha::learned_observation(changed_own_hand, 0) != baseline);

    auto changed_public_zone = fixture.state;
    changed_public_zone.players[1].graveyard[0] =
        alpha::CardId::LightningBolt;
    CHECK(alpha::learned_observation(changed_public_zone, 0) !=
          baseline);
}

TEST(learned_priority_policy_encodes_phase_and_pass_context) {
    const auto fixture = determinization_fixture();
    const auto pass = alpha::PriorityAction::pass();
    const auto beginning_of_combat =
        alpha::learned_priority_policy_features(
            fixture.state, 0, pass, false,
            alpha::TurnPhase::BeginCombat, 0);
    const auto first_main =
        alpha::learned_priority_policy_features(
            fixture.state, 0, pass, true,
            alpha::TurnPhase::FirstMain, 0);
    const auto second_main =
        alpha::learned_priority_policy_features(
            fixture.state, 0, pass, true,
            alpha::TurnPhase::SecondMain, 0);
    const auto resolving_pass =
        alpha::learned_priority_policy_features(
            fixture.state, 0, pass, false,
            alpha::TurnPhase::BeginCombat, 1);
    CHECK(beginning_of_combat != first_main);
    CHECK(first_main != second_main);
    CHECK(beginning_of_combat != resolving_pass);

    auto changed_hidden = fixture.state;
    std::fill(changed_hidden.players[1].hand.begin(),
              changed_hidden.players[1].hand.end(),
              alpha::CardId::LightningBolt);
    std::fill(changed_hidden.players[1].library.begin(),
              changed_hidden.players[1].library.end(),
              alpha::CardId::Forest);
    CHECK(alpha::learned_priority_policy_features(
              changed_hidden, 0, pass, false,
              alpha::TurnPhase::BeginCombat, 1) ==
          resolving_pass);
}

TEST(learned_soft_priority_target_is_smoothed_and_ordered) {
    const auto targets =
        alpha::learned_soft_priority_target({0.2, 0.4, 0.4});
    CHECK(targets.size() == 3);
    CHECK(std::abs(targets[0] + targets[1] + targets[2] - 1.0) <
          1.0e-12);
    CHECK(targets[0] >= 0.1 / 3.0);
    CHECK(targets[1] > targets[0]);
    CHECK(std::abs(targets[1] - targets[2]) < 1.0e-12);

    const auto uniform =
        alpha::learned_soft_priority_target({0.5, 0.5, 0.5});
    CHECK(uniform.size() == 3);
    for (const double target : uniform) {
        CHECK(std::abs(target - 1.0 / 3.0) < 1.0e-12);
    }
    CHECK(alpha::learned_soft_priority_target({}).empty());
}

TEST(learned_defaults_to_value_search_champion) {
    const alpha::BotConfig learned = {
        .kind = alpha::BotKind::Learned,
    };
    CHECK(learned.learned_variant ==
          alpha::LearnedVariant::ValueSearchChampion);
    CHECK(alpha::bot_config_name(learned) == "Learned Value");

    const alpha::BotConfig actor = {
        .kind = alpha::BotKind::Learned,
        .learned_variant = alpha::LearnedVariant::UnifiedActor,
    };
    CHECK(alpha::bot_config_name(actor) == "Learned Actor");
}

TEST(learned_value_search_is_hidden_invariant_phase_aware_and_bounded) {
    auto fixture = determinization_fixture();
    fixture.state.active_player = 0;
    fixture.state.starting_player = 0;
    fixture.state.players[0].artifacts[0].tapped = false;
    fixture.state.players[0].lands[0].tapped = false;
    fixture.state.players[0].lands.push_back(
        {.card = alpha::CardId::Plains, .tapped = false});
    remove_fixture_card(
        fixture.state.players[0].library,
        alpha::CardId::Plains);
    // Make phase continuation observably different: the First Main path has
    // a guaranteed lethal combat, while Second Main has already passed it.
    remove_fixture_card(
        fixture.decks[0], alpha::CardId::Moat);
    fixture.decks[0].push_back(alpha::CardId::GrizzlyBears);
    remove_fixture_card(
        fixture.state.players[0].library,
        alpha::CardId::Moat);
    fixture.state.players[0].creatures.push_back(bear(91));
    fixture.state.players[1].creatures[0].tapped = true;
    fixture.state.players[1].life = 2;
    const auto model =
        alpha::train_learned_value_champion(1, 0xC4A6A10ULL);

    constexpr std::size_t kRollouts = 2;
    constexpr std::uint64_t kEvaluationSeed = 0x1F05AFEULL;
    const auto first_main =
        alpha::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, true,
            alpha::TurnPhase::FirstMain, 1, model, kRollouts,
            kEvaluationSeed);

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
        alpha::diagnose_learned_value_priority(
            hidden_variant, fixture.decks, 0, true,
            alpha::TurnPhase::FirstMain, 1, model, kRollouts,
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

    const auto second_main =
        alpha::diagnose_learned_value_priority(
            fixture.state, fixture.decks, 0, true,
            alpha::TurnPhase::SecondMain, 1, model, kRollouts,
            kEvaluationSeed);
    CHECK(second_main.actions == first_main.actions);
    CHECK(second_main.scores != first_main.scores);
}

TEST(generic_priority_samples_use_common_worlds_and_hide_repartition) {
    const alpha::GameState state =
        alpha::white_lock_plan_diagnostic_state();
    const std::array<std::vector<alpha::CardId>, 2> decks = {
        alpha::white_control_deck(),
        alpha::red_alpha_deck(),
    };
    const auto actions =
        alpha::legal_priority_actions(state, 0, true);
    CHECK(actions.size() == 4);
    const alpha::LearnedSearchConfig config = {
        .seed = 0xC0110A5EULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            alpha::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto model = small_actor_model();
    const auto baseline =
        alpha::learned_priority_action_samples(
            state, decks, 0, true, alpha::TurnPhase::FirstMain,
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

    const alpha::GameState hidden =
        hidden_repartition(state, 0);
    const auto repeated =
        alpha::learned_priority_action_samples(
            hidden, decks, 0, true, alpha::TurnPhase::FirstMain,
            0, actions, model, config);
    CHECK(repeated.q_samples == baseline.q_samples);
    const auto logits =
        alpha::learned_actor_priority_logits(
            state, 0, true, alpha::TurnPhase::FirstMain, 0,
            actions, model);
    CHECK(logits.size() == actions.size());
    CHECK(std::all_of(
        logits.begin(), logits.end(),
        [](double value) { return std::isfinite(value); }));
    CHECK(alpha::learned_actor_priority_logits(
              hidden, 0, true, alpha::TurnPhase::FirstMain, 0,
              actions, model) == logits);
    const double critic =
        alpha::learned_critic_value(state, 0, model);
    CHECK(critic > 0.0 && critic < 1.0);
    CHECK(alpha::learned_critic_value(hidden, 0, model) == critic);

    auto reordered_actions = actions;
    std::reverse(reordered_actions.begin(), reordered_actions.end());
    const auto reordered =
        alpha::learned_priority_action_samples(
            state, decks, 0, true, alpha::TurnPhase::FirstMain,
            0, reordered_actions, model, config);
    for (std::size_t index = 0; index < actions.size(); ++index) {
        CHECK(reordered.q_samples[index] ==
              baseline.q_samples[actions.size() - index - 1]);
    }

    bool rejected_illegal = false;
    try {
        static_cast<void>(
            alpha::learned_priority_action_samples(
                state, decks, 0, true,
                alpha::TurnPhase::FirstMain, 0,
                {alpha::PriorityAction::cast_sorcery(
                    alpha::CardId::Tsunami)},
                model, config));
    } catch (const std::invalid_argument&) {
        rejected_illegal = true;
    }
    CHECK(rejected_illegal);
}

TEST(learned_model_fingerprint_binds_exact_frozen_weights) {
    const auto actor = small_actor_model();
    const auto repeated =
        alpha::train_learned_actor_model(1, 0xAC70E7A1ULL);
    const auto changed =
        alpha::train_learned_actor_model(1, 0xAC70E7A2ULL);
    const std::string fingerprint =
        alpha::learned_model_fingerprint(actor);
    CHECK(fingerprint.size() == 64);
    CHECK(std::all_of(
        fingerprint.begin(), fingerprint.end(),
        [](char character) {
            return (character >= '0' && character <= '9') ||
                   (character >= 'a' && character <= 'f');
        }));
    CHECK(alpha::learned_model_fingerprint(repeated) == fingerprint);
    CHECK(alpha::learned_model_fingerprint(changed) != fingerprint);
    CHECK(alpha::learned_model_fingerprint(small_value_model()) !=
          fingerprint);

    bool rejected_null = false;
    try {
        static_cast<void>(
            alpha::learned_model_fingerprint(nullptr));
    } catch (const std::invalid_argument&) {
        rejected_null = true;
    }
    CHECK(rejected_null);
}

TEST(learned_actor_updates_deep_clone_critic_and_policy_without_mutating_parent) {
    const auto parent = small_actor_model();
    const alpha::GameState state =
        alpha::white_lock_plan_diagnostic_state();
    const auto actions =
        alpha::legal_priority_actions(state, 0, true);
    CHECK(actions.size() >= 2);

    const std::string parent_fingerprint =
        alpha::learned_model_fingerprint(parent);
    const double parent_critic =
        alpha::learned_critic_value(state, 0, parent);
    const auto parent_logits =
        alpha::learned_actor_priority_logits(
            state, 0, true, alpha::TurnPhase::FirstMain, 0,
            actions, parent);

    std::vector<std::vector<double>> policy_options;
    policy_options.reserve(actions.size());
    for (const auto& action : actions) {
        policy_options.push_back(
            alpha::learned_priority_policy_features(
                state, 0, action, true,
                alpha::TurnPhase::FirstMain, 0));
    }

    // Even a no-op update is a recursive clone, but its serialized content
    // and both prediction paths are initially bit-identical.
    const auto frozen_clone =
        alpha::update_learned_actor_model(parent, {}, {}, {});
    CHECK(frozen_clone.get() != parent.get());
    CHECK(alpha::learned_model_fingerprint(frozen_clone) ==
          parent_fingerprint);
    CHECK(alpha::learned_critic_value(state, 0, frozen_clone) ==
          parent_critic);
    CHECK(alpha::learned_actor_priority_logits(
              state, 0, true, alpha::TurnPhase::FirstMain, 0,
              actions, frozen_clone) == parent_logits);

    const double critic_target =
        parent_critic < 0.5 ? 1.0 : 0.0;
    const auto critic_candidate =
        alpha::update_learned_actor_model(
            frozen_clone,
            {{
                .features = alpha::learned_observation(state, 0),
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
    CHECK(alpha::learned_model_fingerprint(critic_candidate) !=
          parent_fingerprint);
    CHECK(alpha::learned_critic_value(
              state, 0, critic_candidate) != parent_critic);
    CHECK(alpha::learned_actor_priority_logits(
              state, 0, true, alpha::TurnPhase::FirstMain, 0,
              actions, critic_candidate) == parent_logits);

    std::vector<double> soft_target(actions.size(), 0.0);
    const std::size_t target_option =
        static_cast<std::size_t>(std::min_element(
            parent_logits.begin(), parent_logits.end()) -
                                 parent_logits.begin());
    soft_target[target_option] = 1.0;
    const auto policy_candidate =
        alpha::update_learned_actor_model(
            frozen_clone, {},
            {{
                .options = policy_options,
                .target_probabilities = soft_target,
                .decision_kind =
                    alpha::LearnedPolicyDecisionKind::Priority,
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
    CHECK(alpha::learned_model_fingerprint(policy_candidate) !=
          parent_fingerprint);
    CHECK(alpha::learned_critic_value(
              state, 0, policy_candidate) == parent_critic);
    CHECK(alpha::learned_actor_priority_logits(
              state, 0, true, alpha::TurnPhase::FirstMain, 0,
              actions, policy_candidate) != parent_logits);

    // Neither the original publication nor the no-op cloned publication was
    // aliased by either mutable training candidate.
    CHECK(alpha::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(alpha::learned_model_fingerprint(frozen_clone) ==
          parent_fingerprint);
    CHECK(alpha::learned_critic_value(state, 0, parent) ==
          parent_critic);
    CHECK(alpha::learned_actor_priority_logits(
              state, 0, true, alpha::TurnPhase::FirstMain, 0,
          actions, parent) == parent_logits);
}

TEST(learned_actor_generation_is_balanced_bounded_immutable_and_deterministic) {
    const alpha::LearnedActorGenerationConfig defaults;
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
    const alpha::GameState state =
        alpha::white_lock_plan_diagnostic_state();
    const auto actions =
        alpha::legal_priority_actions(state, 0, true);
    const std::string parent_fingerprint =
        alpha::learned_model_fingerprint(parent);
    const double parent_critic =
        alpha::learned_critic_value(state, 0, parent);
    const auto parent_logits =
        alpha::learned_actor_priority_logits(
            state, 0, true, alpha::TurnPhase::FirstMain, 0,
            actions, parent);

    constexpr std::uint64_t kRootSeed = 0x617E2A710ULL;
    const alpha::LearnedActorGenerationConfig fast = {
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
        alpha::train_learned_actor_generation(
            parent, kRootSeed, fast);
    const auto expected =
        alpha::learned_iteration::balanced_schedule(
            kRootSeed, fast.generation);

    CHECK(first.model);
    CHECK(first.report.games.size() == expected.size());
    CHECK(first.report.games.size() == 24);
    CHECK(first.report.parent_fingerprint ==
          parent_fingerprint);
    CHECK(first.report.candidate_fingerprint ==
          alpha::learned_model_fingerprint(first.model));
    CHECK(first.report.candidate_fingerprint !=
          parent_fingerprint);
    CHECK(first.report.replay_generations == 1);
    CHECK(first.report.priority_policy_examples ==
          first.report.priority_roots);
    CHECK(first.report.attack_policy_examples ==
          first.report.attack_roots);
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
        }
    }
    CHECK(priority_roots == first.report.priority_roots);
    CHECK(attack_roots == first.report.attack_roots);
    CHECK(attack_includes <= attack_roots);

    CHECK(alpha::learned_model_fingerprint(parent) ==
          parent_fingerprint);
    CHECK(alpha::learned_critic_value(state, 0, parent) ==
          parent_critic);
    CHECK(alpha::learned_actor_priority_logits(
              state, 0, true, alpha::TurnPhase::FirstMain, 0,
              actions, parent) == parent_logits);

    const auto repeated =
        alpha::train_learned_actor_generation(
            parent, kRootSeed, fast);
    CHECK(repeated.model.get() != first.model.get());
    CHECK(repeated.report == first.report);
    CHECK(alpha::learned_model_fingerprint(repeated.model) ==
          alpha::learned_model_fingerprint(first.model));
    CHECK(alpha::learned_model_fingerprint(parent) ==
          parent_fingerprint);
}

TEST(learned_actor_generation_attack_search_controls_real_lethal_combat) {
    auto fixture =
        attack_evaluation_fixture(alpha::CardId::GrizzlyBears);
    fixture.state.players[1].library.push_back(
        alpha::CardId::GrizzlyBears);
    fixture.state.players[1].creatures.clear();
    fixture.state.players[1].life = 3;

    const auto diagnostic =
        alpha::diagnose_learned_actor_generation_attack(
            fixture.state, fixture.decks, small_actor_model(),
            {
                .seed = 0xA77AC6ULL,
                .worlds = 2,
                .rollouts_per_world = 1,
                .horizon_turns = 0,
                .continuation_variant =
                    alpha::LearnedVariant::UnifiedActor,
                .blend_shallow_prior = false,
            });
    CHECK(diagnostic.searched_roots == 1);
    CHECK(diagnostic.rollout_evaluations == 4);
    CHECK(diagnostic.included_attackers == 1);
    CHECK(diagnostic.terminal_result.has_value());
    CHECK(diagnostic.terminal_result->winner == 0);
    CHECK(diagnostic.terminal_result->reason ==
          alpha::EndReason::LifeTotal);
    CHECK(diagnostic.final_state.players[1].life == 0);
    CHECK(diagnostic.final_state.players[0].creatures[0].tapped);
}

TEST(learned_actor_generation_priority_search_applies_real_counterspell) {
    const std::array<std::vector<alpha::CardId>, 2> decks = {
        alpha::blue_alpha_deck(),
        alpha::red_alpha_deck(),
    };
    alpha::GameState state;
    state.active_player = 1;
    state.starting_player = 1;
    state.turn_number = 8;
    state.next_stack_object_id = 2;
    state.players[0].life = 3;
    state.players[0].hand = {
        alpha::CardId::Counterspell,
    };
    state.players[0].lands = {
        {.card = alpha::CardId::Island, .tapped = false},
        {.card = alpha::CardId::Island, .tapped = false},
    };
    state.players[0].library = decks[0];
    remove_fixture_card(
        state.players[0].library, alpha::CardId::Counterspell);
    remove_fixture_card(
        state.players[0].library, alpha::CardId::Island);
    remove_fixture_card(
        state.players[0].library, alpha::CardId::Island);

    state.players[1].lands = {
        {.card = alpha::CardId::Mountain, .tapped = true},
    };
    state.players[1].library = decks[1];
    remove_fixture_card(
        state.players[1].library, alpha::CardId::Mountain);
    remove_fixture_card(
        state.players[1].library, alpha::CardId::LightningBolt);
    state.stack = {
        {
            .kind = alpha::StackObjectKind::Spell,
            .id = 1,
            .card = alpha::CardId::LightningBolt,
            .controller = 1,
            .target = alpha::Target::player_target(0),
            .spell_target = std::nullopt,
        },
    };

    const auto actions =
        alpha::legal_priority_actions(state, 0, true);
    CHECK(actions.size() == 2);
    CHECK(std::find(
              actions.begin(), actions.end(),
              alpha::PriorityAction::cast_counterspell(1)) !=
          actions.end());

    const auto diagnostic =
        alpha::diagnose_learned_actor_generation_priority(
            state, decks, 0, true,
            alpha::TurnPhase::SecondMain, 1,
            small_actor_model(),
            {
                .seed = 0xC0A17E5EEDULL,
                .worlds = 2,
                .rollouts_per_world = 1,
                .horizon_turns = 0,
                .continuation_variant =
                    alpha::LearnedVariant::UnifiedActor,
                .blend_shallow_prior = false,
            });

    CHECK(diagnostic.searched_roots == 1);
    CHECK(diagnostic.rollout_evaluations == 4);
    CHECK(diagnostic.selected_action ==
          alpha::PriorityAction::cast_counterspell(1));
    CHECK(diagnostic.transition_applied);
    CHECK(!diagnostic.pass_result.has_value());
    CHECK(!diagnostic.terminal_result.has_value());
    CHECK(diagnostic.final_state.players[0].life == 3);
    CHECK(diagnostic.final_state.players[0].hand.empty());
    CHECK(std::all_of(
        diagnostic.final_state.players[0].lands.begin(),
        diagnostic.final_state.players[0].lands.end(),
        [](const alpha::LandPermanent& land) {
            return land.tapped;
        }));
    CHECK(diagnostic.final_state.stack.size() == 2);
    CHECK(diagnostic.final_state.stack[0].card ==
          alpha::CardId::LightningBolt);
    CHECK(diagnostic.final_state.stack[1].card ==
          alpha::CardId::Counterspell);
    CHECK(diagnostic.final_state.stack[1].controller == 0);
    CHECK(diagnostic.final_state.stack[1].spell_target ==
          std::optional<alpha::StackObjectId>{1});
    CHECK(diagnostic.final_state.next_stack_object_id == 3);
    CHECK(diagnostic.final_state.stats[0].spells_cast == 1);
}

TEST(generic_priority_samples_resolve_stack_and_bound_horizon) {
    const std::array<std::vector<alpha::CardId>, 2> decks = {
        alpha::red_alpha_deck(),
        alpha::red_alpha_deck(),
    };
    alpha::GameState state;
    state.active_player = 1;
    state.starting_player = 1;
    state.turn_number = 10;
    state.players[0].life = 3;
    state.players[0].library = decks[0];
    state.players[1].lands = {
        {.card = alpha::CardId::Mountain, .tapped = true},
    };
    state.stack = {
        {
            .kind = alpha::StackObjectKind::Spell,
            .id = 1,
            .card = alpha::CardId::LightningBolt,
            .controller = 1,
            .target = alpha::Target::player_target(0),
            .spell_target = std::nullopt,
        },
    };
    state.next_stack_object_id = 2;
    state.players[1].library = decks[1];
    remove_fixture_card(
        state.players[1].library, alpha::CardId::Mountain);
    remove_fixture_card(
        state.players[1].library, alpha::CardId::LightningBolt);
    const std::vector<alpha::PriorityAction> actions = {
        alpha::PriorityAction::pass(),
    };
    const alpha::LearnedSearchConfig actor_config = {
        .seed = 0x57ACCA55ULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            alpha::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto actor_samples =
        alpha::learned_priority_action_samples(
            state, decks, 0, false,
            alpha::TurnPhase::BeginCombat, 1, actions,
            small_actor_model(), actor_config);
    CHECK(actor_samples.rollout_evaluations == 4);
    CHECK(actor_samples.q_samples.size() == 1);
    CHECK(actor_samples.q_samples[0] ==
          std::vector<double>({0.0, 0.0, 0.0, 0.0}));

    alpha::LearnedSearchConfig value_config = actor_config;
    value_config.worlds = 1;
    value_config.rollouts_per_world = 1;
    value_config.continuation_variant =
        alpha::LearnedVariant::ValueSearchChampion;
    const auto value_samples =
        alpha::learned_priority_action_samples(
            state, decks, 0, false,
            alpha::TurnPhase::BeginCombat, 1, actions,
            small_value_model(), value_config);
    CHECK(value_samples.q_samples ==
          std::vector<std::vector<double>>({{0.0}}));

    bool rejected_mismatch = false;
    try {
        static_cast<void>(
            alpha::learned_priority_action_samples(
                state, decks, 0, false,
                alpha::TurnPhase::BeginCombat, 1, actions,
                small_actor_model(), value_config));
    } catch (const std::invalid_argument&) {
        rejected_mismatch = true;
    }
    CHECK(rejected_mismatch);

    bool rejected_unbounded = false;
    alpha::LearnedSearchConfig unbounded = actor_config;
    unbounded.horizon_turns = 129;
    try {
        static_cast<void>(
            alpha::learned_priority_action_samples(
                state, decks, 0, false,
                alpha::TurnPhase::BeginCombat, 1, actions,
                small_actor_model(), unbounded));
    } catch (const std::invalid_argument&) {
        rejected_unbounded = true;
    }
    CHECK(rejected_unbounded);

    alpha::GameState boundary;
    boundary.active_player = 0;
    boundary.starting_player = 0;
    boundary.turn_number = 1;
    boundary.players[0].graveyard = decks[0];
    boundary.players[1].library = {
        alpha::CardId::Mountain,
    };
    boundary.players[1].graveyard = decks[1];
    remove_fixture_card(
        boundary.players[1].graveyard,
        alpha::CardId::Mountain);
    alpha::LearnedSearchConfig horizon_zero = actor_config;
    horizon_zero.worlds = 1;
    horizon_zero.rollouts_per_world = 1;
    const double h0 =
        alpha::learned_priority_action_samples(
            boundary, decks, 0, true,
            alpha::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_zero)
            .q_samples[0][0];
    CHECK(h0 > 0.0 && h0 < 1.0);

    alpha::LearnedSearchConfig horizon_one = horizon_zero;
    horizon_one.horizon_turns = 1;
    const double h1 =
        alpha::learned_priority_action_samples(
            boundary, decks, 0, true,
            alpha::TurnPhase::SecondMain, 1, actions,
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
        alpha::learned_priority_action_samples(
            empty_next_library, decks, 0, true,
            alpha::TurnPhase::SecondMain, 1, actions,
            small_actor_model(), horizon_zero)
            .q_samples[0][0];
    CHECK(deck_out == 1.0);
}

TEST(generic_binary_attack_samples_use_deployed_combat_and_obey_moat) {
    const DeterminizationFixture fixture =
        attack_evaluation_fixture(alpha::CardId::GrizzlyBears);
    const alpha::LearnedSearchConfig actor_config = {
        .seed = 0xA77AC5EEDULL,
        .worlds = 2,
        .rollouts_per_world = 2,
        .horizon_turns = 0,
        .continuation_variant =
            alpha::LearnedVariant::UnifiedActor,
        .blend_shallow_prior = false,
    };
    const auto baseline =
        alpha::learned_binary_attack_samples(
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
    CHECK(alpha::learned_binary_attack_samples(
              hidden, fixture.decks, 0, {}, 1, {},
              small_actor_model(), actor_config)
              .q_samples == baseline.q_samples);
    const auto logits =
        alpha::learned_actor_binary_attack_logits(
            fixture.state, 0, {}, 1, {}, small_actor_model());
    CHECK(std::isfinite(logits[0]));
    CHECK(std::isfinite(logits[1]));

    alpha::LearnedSearchConfig value_config = actor_config;
    value_config.worlds = 1;
    value_config.rollouts_per_world = 1;
    value_config.continuation_variant =
        alpha::LearnedVariant::ValueSearchChampion;
    const auto value_samples =
        alpha::learned_binary_attack_samples(
            fixture.state, fixture.decks, 0, {}, 1, {},
            small_value_model(), value_config);
    CHECK(value_samples.q_samples.size() == 2);
    CHECK(value_samples.q_samples[0].size() == 1);
    CHECK(value_samples.q_samples[1].size() == 1);

    auto moated = fixture.state;
    moated.players[1].enchantments.push_back(alpha::CardId::Moat);
    bool rejected_moat = false;
    try {
        static_cast<void>(
            alpha::learned_binary_attack_samples(
                moated, fixture.decks, 0, {}, 1, {},
                small_actor_model(), actor_config));
    } catch (const std::invalid_argument&) {
        rejected_moat = true;
    }
    CHECK(rejected_moat);
}

TEST(learned_value_attack_set_scores_match_deployed_argmax_and_hide_cards) {
    const std::vector<std::vector<alpha::PermanentId>> candidates = {
        {},
        {1},
    };
    constexpr std::uint64_t seed = 0xB10C5C0EULL;
    for (const alpha::CardId blocker : {
             alpha::CardId::GrizzlyBears,
             alpha::CardId::FireElemental,
         }) {
        const auto fixture = attack_evaluation_fixture(blocker);
        const auto scored =
            alpha::learned_value_attack_set_scores(
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
            alpha::learned_value_attack_set_scores(
                hidden_repartition(fixture.state, 0), 0,
                candidates, small_value_model(), seed);
        CHECK(hidden.scores == scored.scores);
        CHECK(hidden.selected_candidate ==
              scored.selected_candidate);
    }

    bool rejected_actor = false;
    try {
        static_cast<void>(
            alpha::learned_value_attack_set_scores(
                attack_evaluation_fixture(
                    alpha::CardId::GrizzlyBears)
                    .state,
                0, candidates, small_actor_model(), seed));
    } catch (const std::invalid_argument&) {
        rejected_actor = true;
    }
    CHECK(rejected_actor);
}

TEST(handcrafted_diagnostic_scores_match_deployed_preferences) {
    alpha::GameState priority_state;
    priority_state.active_player = 0;
    priority_state.players[0].hand = {
        alpha::CardId::LightningBolt,
    };
    priority_state.players[0].lands = {
        {.card = alpha::CardId::Mountain, .tapped = false},
    };
    priority_state.players[1].life = 3;
    const auto actions =
        alpha::legal_priority_actions(priority_state, 0, true);
    const auto scores =
        alpha::handcrafted_priority_scores(
            priority_state, 0, actions);
    const auto lethal =
        std::find(
            actions.begin(), actions.end(),
            alpha::PriorityAction::cast_lightning_bolt(
                alpha::Target::player_target(1)));
    CHECK(lethal != actions.end());
    const std::size_t lethal_index =
        static_cast<std::size_t>(
            std::distance(actions.begin(), lethal));
    CHECK(scores[lethal_index] ==
          *std::max_element(scores.begin(), scores.end()));

    const auto favorable =
        attack_evaluation_fixture(alpha::CardId::GrizzlyBears);
    CHECK((alpha::handcrafted_binary_attack_scores(
               favorable.state, 0, {}, 1, {}) ==
           std::array<double, 2>({0.0, 1.0})));
    const auto unfavorable =
        attack_evaluation_fixture(alpha::CardId::FireElemental);
    CHECK((alpha::handcrafted_binary_attack_scores(
               unfavorable.state, 0, {}, 1, {}) ==
           std::array<double, 2>({1.0, 0.0})));
}

TEST(white_lock_plan_diagnostic_fixture_is_valid_and_locked) {
    const auto state = alpha::white_lock_plan_diagnostic_state();
    CHECK(state.active_player == 0);
    CHECK(state.stack.empty());
    CHECK(state.players[0].land_played_this_turn);
    CHECK(state.players[0].lands.size() == 4);
    CHECK(std::all_of(
        state.players[0].lands.begin(),
        state.players[0].lands.end(),
        [](const alpha::LandPermanent& land) {
            return land.card == alpha::CardId::Plains &&
                   !land.tapped;
        }));
    CHECK(state.players[0].artifacts.size() == 1);
    CHECK(state.players[0].artifacts[0].card ==
          alpha::CardId::Millstone);
    CHECK(!state.players[0].artifacts[0].tapped);
    CHECK(count_card(
              state.players[0].enchantments,
              alpha::CardId::Moat) == 1);
    CHECK(count_card(
              state.players[0].hand,
              alpha::CardId::Moat) == 7);

    CHECK(state.players[1].creatures.size() == 1);
    const auto& attacker = state.players[1].creatures[0];
    CHECK(attacker.card == alpha::CardId::FireElemental);
    CHECK(!attacker.tapped);
    CHECK(!attacker.summoning_sick);
    CHECK(!alpha::card_definition(attacker.card).flying);

    const auto actions =
        alpha::legal_priority_actions(state, 0, true);
    CHECK(actions.size() == 4);
    CHECK(has_action(
        actions,
        alpha::PriorityAction::cast_enchantment(
            alpha::CardId::Moat)));
    CHECK(has_action(
        actions,
        alpha::PriorityAction::activate_millstone(
            state.players[0].artifacts[0].id,
            alpha::Target::player_target(1))));

    const std::array<std::vector<alpha::CardId>, 2> decks = {
        alpha::white_control_deck(),
        alpha::red_alpha_deck(),
    };
    const auto sampled =
        alpha::sample_determinization(state, decks, 0, 0x10C4ULL);
    for (std::size_t player = 0; player < decks.size(); ++player) {
        auto expected = decks[player];
        std::sort(expected.begin(), expected.end());
        CHECK(physical_cards(sampled, player) == expected);
    }

    auto attempted_combat = state;
    CHECK(!alpha::resolve_combat(
        attempted_combat, 1, {attacker.id}, {}));
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

TEST(attacking_player_controls_multi_block_damage_order) {
    const auto combat_state = [] {
        alpha::GameState state;
        state.players[0].creatures = {
            creature(1, alpha::CardId::FireElemental)};
        state.players[1].creatures = {
            bear(2),
            creature(3, alpha::CardId::IronrootTreefolk),
        };
        return state;
    };

    auto bears_first = combat_state();
    CHECK(alpha::resolve_combat(
        bears_first, 0, {1}, {{1, 2}, {1, 3}}));
    CHECK(bears_first.players[0].creatures.empty());
    CHECK(bears_first.players[1].creatures.size() == 1);
    CHECK(bears_first.players[1].creatures[0].card ==
          alpha::CardId::IronrootTreefolk);
    CHECK(count_card(bears_first.players[1].graveyard,
                     alpha::CardId::GrizzlyBears) == 1);

    auto treefolk_first = combat_state();
    CHECK(alpha::resolve_combat(
        treefolk_first, 0, {1}, {{1, 3}, {1, 2}}));
    CHECK(treefolk_first.players[0].creatures.empty());
    CHECK(treefolk_first.players[1].creatures.size() == 1);
    CHECK(treefolk_first.players[1].creatures[0].card ==
          alpha::CardId::GrizzlyBears);
    CHECK(count_card(treefolk_first.players[1].graveyard,
                     alpha::CardId::IronrootTreefolk) == 1);
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

TEST(learned_deck_lift_gate_requires_every_policy_and_allows_ties) {
    alpha::TournamentSummary summary;
    const auto set_record =
        [&](alpha::DeckId deck, alpha::BotKind bot,
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
        const auto id = static_cast<alpha::DeckId>(deck);
        set_record(id, alpha::BotKind::Random, 20);
        set_record(id, alpha::BotKind::MonteCarlo, 40);
        set_record(id, alpha::BotKind::DeepMonteCarlo, 50);
        set_record(id, alpha::BotKind::Handcrafted, 60);
        set_record(id, alpha::BotKind::Learned, 61);
    }
    set_record(alpha::DeckId::Red, alpha::BotKind::Learned, 60);

    const auto passing =
        alpha::compare_learned_deck_lifts(summary);
    CHECK(passing.complete());
    CHECK(passing.learned_is_best_on_every_deck());
    const auto& red =
        passing.decks[static_cast<std::size_t>(alpha::DeckId::Red)];
    CHECK(red.learned_lift == red.best_other_lift);
    CHECK(red.learned_is_best);
    CHECK(red.best_other == alpha::BotKind::Handcrafted);

    set_record(alpha::DeckId::Blue, alpha::BotKind::Learned, 59);
    const auto losing =
        alpha::compare_learned_deck_lifts(summary);
    CHECK(losing.complete());
    CHECK(!losing.learned_is_best_on_every_deck());
    CHECK(!losing.decks[static_cast<std::size_t>(
               alpha::DeckId::Blue)]
               .learned_is_best);

    auto& missing =
        summary.deck_bots[static_cast<std::size_t>(
                              alpha::DeckId::White)]
                         [static_cast<std::size_t>(
                              alpha::BotKind::MonteCarlo)];
    missing = {};
    const auto incomplete =
        alpha::compare_learned_deck_lifts(summary);
    CHECK(!incomplete.complete());
    CHECK(!incomplete.learned_is_best_on_every_deck());
    CHECK(!incomplete.decks[static_cast<std::size_t>(
                   alpha::DeckId::White)]
                   .available);
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

TEST(benchmark_training_seed_is_independent_and_model_is_reusable) {
    constexpr std::uint64_t kTrainingSeed = 707;
    const alpha::BotConfig learned = {
        .kind = alpha::BotKind::Learned,
        .rollouts_per_action = 0,
        .training_games = 1,
    };
    const alpha::BotConfig random = {
        .kind = alpha::BotKind::Random,
        .rollouts_per_action = 1,
    };
    alpha::GameConfig shared_config;
    CHECK(shared_config.learned_training_seed ==
          alpha::kDefaultLearnedTrainingSeed);
    shared_config.learned_training_seed = kTrainingSeed;
    shared_config.learned_model =
        alpha::train_learned_model(1, kTrainingSeed);

    const auto first = alpha::run_bot_benchmark(
        1, 101, learned, random, shared_config);
    const auto repeated = alpha::run_bot_benchmark(
        1, 101, learned, random, shared_config);
    const auto other_evaluation_seed =
        alpha::run_bot_benchmark(
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

TEST(actor_and_value_champion_use_distinct_frozen_models_in_benchmark) {
    constexpr std::uint64_t kTrainingSeed = 424242;
    const auto actor_model =
        alpha::train_learned_actor_model(1, kTrainingSeed);

    const alpha::BotConfig actor = {
        .kind = alpha::BotKind::Learned,
        .learned_variant = alpha::LearnedVariant::UnifiedActor,
        .rollouts_per_action = 0,
        .training_games = 1,
        .learned_model = actor_model,
    };
    const alpha::BotConfig champion = {
        .kind = alpha::BotKind::Learned,
        .learned_variant =
            alpha::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .training_games = 1,
    };
    alpha::GameConfig config;
    config.learned_training_seed = kTrainingSeed;
    // A global Actor fallback must not be silently reused for the Champion.
    config.learned_model = actor_model;
    const auto result = alpha::run_bot_benchmark(
        1, 0xAC70C4A6ULL, actor, champion, config);

    CHECK(result.total_games == 40);
    CHECK(result.challenger_stats.games == 40);
    CHECK(result.baseline_stats.games == 40);
    CHECK(result.challenger.learned_model == actor_model);
    CHECK(result.baseline.learned_model);
    CHECK(result.baseline.learned_model != actor_model);
    CHECK(result.challenger_stats.total_rollouts == 0);
    CHECK(result.baseline_stats.total_rollouts > 0);
    for (std::size_t deck = 0;
         deck < result.challenger_decks.size(); ++deck) {
        CHECK(result.challenger_decks[deck].games == 10);
        CHECK(result.baseline_decks[deck].games == 10);
    }

    const auto next_actor_model =
        alpha::train_learned_actor_model(
            1, kTrainingSeed + 1);
    alpha::BotConfig next_actor = actor;
    next_actor.learned_model = next_actor_model;
    const auto generations = alpha::run_bot_benchmark(
        1, 0x6E6E5EEDULL, next_actor, actor);
    CHECK(generations.total_games == 40);
    CHECK(generations.challenger.learned_model ==
          next_actor_model);
    CHECK(generations.baseline.learned_model == actor_model);
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

TEST(learned_policy_bot_beats_monte_carlo_without_rollouts_or_handcrafted_values) {
    const auto actor_model =
        alpha::train_learned_actor_model(200, 424242);
    const alpha::BotConfig challenger = {
        .kind = alpha::BotKind::Learned,
        .learned_variant = alpha::LearnedVariant::UnifiedActor,
        .rollouts_per_action = 0,
        .training_games = 200,
        .learned_model = actor_model,
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
    CHECK(result.challenger_stats.total_rollouts == 0);
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
