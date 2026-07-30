#include "old_school/selfplay_zero.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace old_school;
using namespace old_school::selfplay_zero;

struct TestCase {
    std::string_view name;
    std::function<void()> run;
};

std::vector<TestCase>& tests() {
    static std::vector<TestCase> registered;
    return registered;
}

struct RegisterTest {
    RegisterTest(std::string_view name, std::function<void()> run) {
        tests().push_back({name, std::move(run)});
    }
};

#define SPZ_TEST(name)                                            \
    void name();                                                  \
    const RegisterTest register_##name{#name, name};              \
    void name()

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::array<std::vector<CardId>, 2> mirror_decks(std::size_t deck_index) {
    return {spz_decks()[deck_index], spz_decks()[deck_index]};
}

SPZ_TEST(feature_count_is_stable) {
    expect(spz_feature_count() > 0, "feature count positive");
}

SPZ_TEST(features_are_bounded_and_deterministic) {
    for (std::size_t deck = 0; deck < kSpzDeckCount; ++deck) {
        GameState state;
        const auto& decks = spz_decks();
        std::array<std::vector<CardId>, 2> game_decks = {
            decks[deck], decks[(deck + 1) % kSpzDeckCount]};
        for (std::size_t player = 0; player < 2; ++player) {
            state.players[player].library = game_decks[player];
            for (std::size_t draw = 0; draw < 7; ++draw) {
                state.players[player].hand.push_back(
                    state.players[player].library.back());
                state.players[player].library.pop_back();
            }
        }
        const auto observation = observe_game_state(state, 0);
        const auto features =
            spz_features(observation, game_decks, TurnPhase::FirstMain);
        expect(features.size() == spz_feature_count(),
               "feature vector length matches schema");
        for (const float value : features) {
            expect(std::isfinite(value), "feature is finite");
            expect(value >= -0.01f && value <= 30.0f,
                   "feature is in a sane range");
        }
        const auto again =
            spz_features(observation, game_decks, TurnPhase::FirstMain);
        expect(features == again, "feature extraction is deterministic");
    }
}

SPZ_TEST(reconstruction_supports_determinization) {
    // A reconstructed observation must satisfy sample_determinization's
    // physical card conservation for every deck, including mid-game zones.
    const auto& decks = spz_decks();
    for (std::size_t deck = 0; deck < kSpzDeckCount; ++deck) {
        std::array<std::vector<CardId>, 2> game_decks = {
            decks[deck], decks[(deck + 2) % kSpzDeckCount]};
        GameConfig config;
        config.max_turns = 12;
        Game game(game_decks[0], game_decks[1], 12345 + deck, config);
        const GameResult result = game.run();
        (void)result;
        const GameState& final_state = game.state();
        for (std::size_t observer = 0; observer < 2; ++observer) {
            const auto observation =
                observe_game_state(final_state, observer);
            const GameState reconstructed =
                reconstruct_observed_state(observation);
            const GameState sampled = sample_determinization(
                reconstructed, game_decks, observer, 99);
            expect(sampled.players[observer].hand ==
                       final_state.players[observer].hand,
                   "observer hand preserved through determinization");
            expect(sampled.players[1 - observer].hand.size() ==
                       final_state.players[1 - observer].hand.size(),
                   "opponent hand size preserved");
            expect(sampled.players[0].library.size() ==
                       final_state.players[0].library.size(),
                   "library sizes preserved");
        }
    }
}

SPZ_TEST(features_and_rollout_policy_ignore_opponent_hidden_partition) {
    const std::array<std::vector<CardId>, 2> game_decks = {
        spz_decks()[1], spz_decks()[2]};
    GameState first;
    first.active_player = 0;
    first.starting_player = 0;
    first.turn_number = 1;
    first.next_permanent_id = 1;
    first.next_stack_object_id = 1;
    first.players[0].library = game_decks[0];
    first.players[1].library = game_decks[1];

    const auto move_to_hand = [](PlayerState& player, CardId card) {
        const auto position =
            std::find(player.library.begin(), player.library.end(), card);
        expect(position != player.library.end(),
               "hidden-safety fixture card is in its deck");
        player.hand.push_back(*position);
        player.library.erase(position);
    };
    move_to_hand(first.players[0], CardId::Mountain);
    for (std::size_t draw = 1; draw < 7; ++draw) {
        first.players[0].hand.push_back(first.players[0].library.back());
        first.players[0].library.pop_back();
    }
    for (std::size_t draw = 0; draw < 7; ++draw) {
        move_to_hand(first.players[1], CardId::Island);
    }

    GameState repartitioned = first;
    const auto hidden_counterspell = std::find(
        repartitioned.players[1].library.begin(),
        repartitioned.players[1].library.end(), CardId::Counterspell);
    expect(hidden_counterspell != repartitioned.players[1].library.end(),
           "hidden-safety fixture has an unseen nonland");
    std::swap(repartitioned.players[1].hand.front(),
              *hidden_counterspell);

    const PlayerObservation first_observation =
        observe_game_state(first, 0);
    const PlayerObservation repartitioned_observation =
        observe_game_state(repartitioned, 0);
    expect(first_observation == repartitioned_observation,
           "opponent hand/library repartition is observation-invisible");
    expect(
        spz_features(first_observation, game_decks, TurnPhase::FirstMain) ==
            spz_features(repartitioned_observation, game_decks,
                         TurnPhase::FirstMain),
        "features ignore opponent hidden identities");

    const auto actions = legal_priority_actions(first, 0, true);
    expect(actions.size() > 1,
           "hidden-safety fixture has a real priority choice");
    const auto net = std::make_shared<const SpzNet>(
        spz_feature_count(), 8, 20260731);
    SpzPolicyConfig policy;
    policy.worlds = 2;
    policy.block_prediction_worlds = 2;
    policy.rollout = true;
    policy.rollout_top_k = 2;
    policy.seed = 0x5AFE;
    const HumanController first_controller =
        make_spz_controller(net, game_decks, 0, policy);
    const HumanController repartitioned_controller =
        make_spz_controller(net, game_decks, 0, policy);
    const std::size_t first_choice =
        first_controller.choose_priority_action(
            first_observation, TurnPhase::FirstMain, actions);
    const std::size_t repartitioned_choice =
        repartitioned_controller.choose_priority_action(
            repartitioned_observation, TurnPhase::FirstMain, actions);
    expect(first_choice == repartitioned_choice,
           "rollout action is invariant to opponent hidden partition");

    PlayerObservation revealed_first = first_observation;
    PlayerObservation revealed_second = first_observation;
    revealed_first.revealed_opponent_hand = first.players[1].hand;
    revealed_second.revealed_opponent_hand =
        repartitioned.players[1].hand;
    const HumanController first_reveal_controller =
        make_spz_controller(net, game_decks, 0, policy);
    const HumanController second_reveal_controller =
        make_spz_controller(net, game_decks, 0, policy);
    expect(
        first_reveal_controller.choose_priority_action(
            revealed_first, TurnPhase::FirstMain, actions) ==
            second_reveal_controller.choose_priority_action(
                revealed_second, TurnPhase::FirstMain, actions),
        "debug-only revealed hand cannot affect rollout action");
}

SPZ_TEST(net_learns_a_simple_separation) {
    SpzNet net(4, 8, 7);
    std::vector<float> positive = {1.0f, 0.0f, 0.5f, 0.0f};
    std::vector<float> negative = {0.0f, 1.0f, 0.0f, 0.5f};
    std::vector<const std::vector<float>*> batch = {&positive, &negative};
    std::vector<float> targets = {1.0f, 0.0f};
    for (int step = 0; step < 400; ++step) {
        net.train_batch(batch, targets, 0.05);
    }
    expect(net.value(positive) > 0.9, "positive example learned");
    expect(net.value(negative) < 0.1, "negative example learned");
}

SPZ_TEST(net_save_load_roundtrip_preserves_values) {
    SpzNet net(spz_feature_count(), 16, 424242);
    std::stringstream buffer;
    net.save(buffer);
    const SpzNet loaded = SpzNet::load(buffer);
    std::vector<float> probe(spz_feature_count(), 0.0f);
    for (std::size_t index = 0; index < probe.size(); ++index) {
        probe[index] = static_cast<float>((index % 5)) / 5.0f;
    }
    expect(std::abs(net.value(probe) - loaded.value(probe)) < 1e-12,
           "save/load preserves outputs exactly");
}

SPZ_TEST(controller_plays_complete_legal_games) {
    // Every deck pairing finishes a mirror self-play game without the engine
    // rejecting a decision, and identical seeds replay identically.
    const auto net = std::make_shared<const SpzNet>(
        spz_feature_count(), 8, 20260729);
    for (std::size_t deck = 0; deck < kSpzDeckCount; ++deck) {
        const std::array<std::vector<CardId>, 2> game_decks = {
            spz_decks()[deck],
            spz_decks()[(deck + 1) % kSpzDeckCount]};
        std::vector<GameResult> results;
        for (int repeat = 0; repeat < 2; ++repeat) {
            GameConfig config;
            config.max_turns = 40;
            for (std::size_t seat = 0; seat < 2; ++seat) {
                SpzPolicyConfig policy;
                policy.worlds = 1;
                policy.block_prediction_worlds = 1;
                policy.seed = 5 + seat;
                config.human_controllers[seat] = make_spz_controller(
                    net, game_decks, seat, policy);
            }
            Game game(game_decks[0], game_decks[1], 777 + deck, config);
            results.push_back(game.run());
        }
        expect(results[0] == results[1],
               "identical seeds and policies replay identically");
    }
}

SPZ_TEST(rollout_controller_plays_complete_legal_games) {
    const auto net = std::make_shared<const SpzNet>(
        spz_feature_count(), 8, 20260730);
    for (std::size_t deck = 0; deck < kSpzDeckCount; ++deck) {
        const std::array<std::vector<CardId>, 2> game_decks = {
            spz_decks()[deck],
            spz_decks()[(deck + 2) % kSpzDeckCount]};
        std::vector<GameResult> results;
        for (int repeat = 0; repeat < 2; ++repeat) {
            GameConfig config;
            config.max_turns = 25;
            for (std::size_t seat = 0; seat < 2; ++seat) {
                SpzPolicyConfig policy;
                policy.worlds = 1;
                policy.block_prediction_worlds = 1;
                policy.rollout = true;
                policy.rollout_top_k = 3;
                policy.seed = 21 + seat;
                config.human_controllers[seat] = make_spz_controller(
                    net, game_decks, seat, policy);
            }
            Game game(game_decks[0], game_decks[1], 4243 + deck, config);
            results.push_back(game.run());
        }
        expect(results[0] == results[1],
               "rollout policy replays identically for identical seeds");
    }
}

SPZ_TEST(pass_dominance_prune_blocks_zero_effect_casts) {
    // A state with Braingeyser and four untapped Islands: the X=0 casts are
    // strictly dominated by Pass and must never be selected, regardless of
    // how an arbitrary value net happens to score them.
    const auto deck = blue_deck();
    GameState state;
    state.turn_number = 6;
    state.active_player = 0;
    state.starting_player = 0;
    const auto remove_one = [](std::vector<CardId>& pool, CardId card) {
        const auto found = std::find(pool.begin(), pool.end(), card);
        expect(found != pool.end(), "probe deck contains the card");
        pool.erase(found);
    };
    for (std::size_t player = 0; player < 2; ++player) {
        auto pool = deck;
        auto& player_state = state.players[player];
        for (int land = 0; land < 4; ++land) {
            remove_one(pool, CardId::Island);
            player_state.lands.push_back({CardId::Island, false});
        }
        const std::vector<CardId> hand =
            player == 0 ? std::vector<CardId>{CardId::Braingeyser,
                                              CardId::Island,
                                              CardId::AirElemental}
                        : std::vector<CardId>{CardId::Island,
                                              CardId::Counterspell,
                                              CardId::AirElemental};
        for (const CardId card : hand) {
            remove_one(pool, card);
            player_state.hand.push_back(card);
        }
        player_state.library = pool;
        player_state.life = 15;
    }
    const auto observation = observe_game_state(state, 0);
    const auto actions = legal_priority_actions(state, 0, true);
    bool saw_zero_geyser = false;
    for (const auto& action : actions) {
        saw_zero_geyser |=
            action.kind == PriorityActionKind::CastBraingeyser &&
            action.x_value == 0;
    }
    expect(saw_zero_geyser, "state offers an X=0 Braingeyser");
    const std::array<std::vector<CardId>, 2> game_decks = {deck, deck};
    for (std::uint64_t net_seed = 1; net_seed <= 30; ++net_seed) {
        const auto net = std::make_shared<const SpzNet>(
            spz_feature_count(), 8, net_seed);
        SpzPolicyConfig policy;
        policy.worlds = 1;
        policy.block_prediction_worlds = 1;
        policy.seed = net_seed;
        const auto controller =
            make_spz_controller(net, game_decks, 0, policy);
        const std::size_t chosen = controller.choose_priority_action(
            observation, TurnPhase::FirstMain, actions);
        const PriorityAction& action = actions[chosen];
        expect(!(action.kind == PriorityActionKind::CastBraingeyser &&
                 action.x_value == 0),
               "pruned policy never selects an X=0 Braingeyser");
    }
}

SPZ_TEST(recorder_collects_outcome_labeled_rows) {
    const auto net = std::make_shared<const SpzNet>(
        spz_feature_count(), 8, 99);
    const auto game_decks = mirror_decks(1);
    SpzRecorder recorders[2];
    GameConfig config;
    config.max_turns = 40;
    for (std::size_t seat = 0; seat < 2; ++seat) {
        SpzPolicyConfig policy;
        policy.worlds = 1;
        policy.block_prediction_worlds = 1;
        policy.epsilon = 0.1;
        policy.seed = 11 + seat;
        config.human_controllers[seat] = make_spz_controller(
            net, game_decks, seat, policy, &recorders[seat]);
    }
    Game game(game_decks[0], game_decks[1], 4242, config);
    const GameResult result = game.run();
    (void)result;
    expect(!recorders[0].feature_rows.empty(),
           "seat zero recorded decisions");
    expect(!recorders[1].feature_rows.empty(),
           "seat one recorded decisions");
    for (const auto& row : recorders[0].feature_rows) {
        expect(row.size() == spz_feature_count(),
               "recorded rows match the feature schema");
    }
}

SPZ_TEST(training_smoke_produces_a_working_net) {
    SpzTrainConfig config;
    config.iterations = 2;
    config.games_per_iteration = 4;
    config.hidden = 8;
    config.seed = 31337;
    config.max_turns = 30;
    config.training_worlds = 1;
    config.threads = 2;
    const auto net = train_spz(config);
    expect(net != nullptr, "training returns a net");
    std::vector<float> probe(spz_feature_count(), 0.1f);
    const double value = net->value(probe);
    expect(value > 0.0 && value < 1.0, "trained net emits probabilities");
}

SPZ_TEST(benchmark_counts_are_consistent) {
    const auto net = std::make_shared<const SpzNet>(
        spz_feature_count(), 8, 5150);
    SpzPolicyConfig policy;
    policy.worlds = 1;
    policy.block_prediction_worlds = 1;
    const auto result = run_spz_benchmark(net, BotKind::Random, 1, 606,
                                          policy, 60, 2);
    expect(result.aggregate.games ==
               2 * kSpzDeckCount * kSpzDeckCount,
           "one repetition plays two seat-swapped games per pairing");
    std::size_t per_deck_total = 0;
    for (const auto& deck_stats : result.per_deck) {
        per_deck_total += deck_stats.games;
        expect(deck_stats.wins + deck_stats.losses + deck_stats.draws ==
                   deck_stats.games,
               "deck results partition its games");
    }
    expect(per_deck_total == result.aggregate.games,
           "per-deck games sum to the aggregate");
    expect(result.wilson_lower_bound_95 >= 0.0 &&
               result.wilson_lower_bound_95 <= 1.0,
           "wilson bound is a probability");
}

}  // namespace

int main() {
    for (const TestCase& test : tests()) {
        std::cout << "RUN " << test.name << '\n';
        test.run();
    }
    std::cout << "OK " << tests().size() << " tests\n";
    return 0;
}
