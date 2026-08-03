// Simulation benchmark + behavioral fingerprint.
//
// Two jobs, one binary:
//   --fingerprint  Play a fixed battery of seeded games (Random and
//                  Handcrafted pilots plus SPZ decisions on a fixed
//                  random-weight net) and print one hash of every
//                  public outcome. Pure optimizations must keep this
//                  hash bit-identical.
//   --speed        Time the same three workloads and report throughput,
//                  so each optimization piece proves its win.
#include "old_school/game.hpp"
#include "old_school/selfplay_zero.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

using namespace old_school;
namespace spz = old_school::selfplay_zero;

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

std::uint64_t hash_result(std::uint64_t hash, const GameResult& result) {
    hash = mix(hash, static_cast<std::uint64_t>(result.winner + 1));
    hash = mix(hash, result.turns);
    hash = mix(hash, static_cast<std::uint64_t>(result.ending_life[0] + 100));
    hash = mix(hash, static_cast<std::uint64_t>(result.ending_life[1] + 100));
    for (std::size_t seat = 0; seat < 2; ++seat) {
        const auto& stats = result.player_stats[seat];
        hash = mix(hash, stats.cards_drawn);
        hash = mix(hash, stats.lands_played);
        hash = mix(hash, stats.spells_cast);
        hash = mix(hash, stats.spells_countered);
        hash = mix(hash, stats.damage_to_opponent);
        hash = mix(hash, stats.mulligans);
    }
    return hash;
}

std::vector<CardId> deck_for(std::size_t index) {
    switch (index % 11) {
    case 0: return green_deck();
    case 1: return red_deck();
    case 2: return blue_deck();
    case 3: return white_control_deck();
    case 4: return ru_aggro_deck();
    case 5: return lotus_combo_deck();
    case 6: return burn_deck();
    case 7: return robots_deck();
    case 8: return white_weenie_deck();
    case 9: return br_midrange_deck();
    default: return uwr_deck();
    }
}

GameResult play_bot_game(BotKind kind, std::size_t pairing,
                         unsigned seed) {
    GameConfig config;
    config.bots[0] = {.kind = kind, .rollouts_per_action = 1};
    config.bots[1] = {.kind = kind, .rollouts_per_action = 1};
    config.starting_player = seed % 2;
    config.max_turns = 200;
    Game game(deck_for(pairing), deck_for(pairing + 3),
              seed * 2654435761u + 17, config);
    return game.run();
}

GameResult play_spz_game(const std::shared_ptr<const spz::SpzNet>& net,
                         std::size_t pairing, unsigned seed) {
    const std::array<std::vector<CardId>, 2> decks = {
        deck_for(pairing), deck_for(pairing + 5)};
    GameConfig config;
    config.bots[1] = {.kind = BotKind::Handcrafted,
                      .rollouts_per_action = 1};
    config.starting_player = seed % 2;
    config.max_turns = 200;
    spz::SpzPolicyConfig policy;
    policy.worlds = 4;
    policy.block_prediction_worlds = 4;
    policy.rollout = true;
    policy.gamma_per_turn = 0.98;
    policy.seed = 4242 + seed;
    config.human_controllers[0] = spz::make_spz_controller(
        net, decks, 0, policy, nullptr, nullptr, nullptr);
    Game game(decks[0], decks[1], seed * 40503u + 3, config);
    return game.run();
}

double seconds_since(
    const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration_cast<std::chrono::duration<double>>(
               std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    bool fingerprint = false;
    bool speed = false;
    for (int argument = 1; argument < argc; ++argument) {
        if (std::strcmp(argv[argument], "--fingerprint") == 0) {
            fingerprint = true;
        } else if (std::strcmp(argv[argument], "--speed") == 0) {
            speed = true;
        }
    }
    if (!fingerprint && !speed) {
        fingerprint = true;
        speed = true;
    }

    const auto net = std::make_shared<const spz::SpzNet>(
        spz::SpzNet(spz::spz_feature_count_colors(), 64, 20260801));

    if (fingerprint) {
        std::uint64_t hash = 0x00debeefULL;
        for (unsigned seed = 0; seed < 120; ++seed) {
            hash = hash_result(
                hash, play_bot_game(BotKind::Random, seed % 11, seed));
        }
        for (unsigned seed = 0; seed < 60; ++seed) {
            hash = hash_result(
                hash,
                play_bot_game(BotKind::Handcrafted, seed % 11, seed));
        }
        for (unsigned seed = 0; seed < 8; ++seed) {
            hash = hash_result(hash, play_spz_game(net, seed, seed));
        }
        std::printf("fingerprint %016llx\n",
                    static_cast<unsigned long long>(hash));
    }

    if (speed) {
        {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned seed = 0; seed < 400; ++seed) {
                play_bot_game(BotKind::Random, seed % 11, seed);
            }
            const double elapsed = seconds_since(start);
            std::printf("random   %7.1f games/s (%.2fs)\n",
                        400.0 / elapsed, elapsed);
        }
        {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned seed = 0; seed < 200; ++seed) {
                play_bot_game(BotKind::Handcrafted, seed % 11, seed);
            }
            const double elapsed = seconds_since(start);
            std::printf("handcraf %7.1f games/s (%.2fs)\n",
                        200.0 / elapsed, elapsed);
        }
        {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned seed = 0; seed < 10; ++seed) {
                play_spz_game(net, seed, seed);
            }
            const double elapsed = seconds_since(start);
            std::printf("spz      %7.2f games/s (%.2fs)\n",
                        10.0 / elapsed, elapsed);
        }
    }
    return 0;
}
