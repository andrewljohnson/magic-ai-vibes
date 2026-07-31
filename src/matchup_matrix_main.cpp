// Matchup-matrix tool: for each bot, play every unordered deck pairing with
// that bot piloting both seats and report deck-vs-deck win rates, plus a
// single bots-by-decks summary (each deck's mean win rate across the field
// under that pilot). Output is JSON for the training-monitor site.

#include "old_school/game.hpp"
#include "old_school/selfplay_zero.hpp"

#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace old_school;
namespace spz = old_school::selfplay_zero;

namespace {

struct DeckEntry {
    std::string name;
    std::vector<CardId> cards;
};

struct BotEntry {
    std::string name;
    // Builds one game's config (bot seats + optional SPZ controllers).
    std::function<GameConfig(const std::array<std::vector<CardId>, 2>&,
                             std::uint64_t)>
        configure;
};

void run_jobs(std::size_t job_count, std::size_t threads,
              const std::function<void(std::size_t)>& job) {
    const std::size_t workers =
        std::max<std::size_t>(1, std::min(threads, job_count));
    if (workers <= 1) {
        for (std::size_t index = 0; index < job_count; ++index) {
            job(index);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        pool.emplace_back([&]() {
            while (true) {
                const std::size_t index =
                    next.fetch_add(1, std::memory_order_relaxed);
                if (index >= job_count) {
                    return;
                }
                job(index);
            }
        });
    }
    for (std::thread& worker : pool) {
        worker.join();
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t games_per_pairing = 100;
    std::uint64_t seed = 20260730;
    std::string output_path = "build/telemetry/matchup-matrix.json";
    std::string spz_model = "data/spz-champion-v6.txt";
    std::string spz_advantage = "data/spz-advantage-v1.txt";
    std::size_t threads = std::thread::hardware_concurrency();
    std::optional<std::string> only_bot;
    for (int arg = 1; arg < argc; ++arg) {
        const std::string flag = argv[arg];
        const auto value = [&]() -> std::string {
            if (arg + 1 >= argc) {
                std::cerr << "missing value for " << flag << "\n";
                std::exit(1);
            }
            return argv[++arg];
        };
        if (flag == "--games") {
            games_per_pairing = std::stoul(value());
        } else if (flag == "--seed") {
            seed = std::stoull(value());
        } else if (flag == "--out") {
            output_path = value();
        } else if (flag == "--spz-model") {
            spz_model = value();
        } else if (flag == "--spz-advantage") {
            spz_advantage = value();
        } else if (flag == "--threads") {
            threads = std::stoul(value());
        } else if (flag == "--bot") {
            only_bot = value();
        } else {
            std::cerr << "unknown flag " << flag << "\n";
            return 1;
        }
    }

    const std::vector<DeckEntry> decks = {
        {"green", green_deck()},
        {"red", red_deck()},
        {"blue", blue_deck()},
        {"white", white_control_deck()},
        {"ru-aggro", ru_aggro_deck()},
        {"lotus-combo", lotus_combo_deck()},
        {"burn", burn_deck()},
    };

    const auto plain_bot = [](BotKind kind, std::size_t rollouts) {
        return [kind, rollouts](
                   const std::array<std::vector<CardId>, 2>&,
                   std::uint64_t) {
            GameConfig config;
            config.bots = {
                BotConfig{.kind = kind, .rollouts_per_action = rollouts},
                BotConfig{.kind = kind, .rollouts_per_action = rollouts},
            };
            return config;
        };
    };

    std::vector<BotEntry> bots = {
        {"random", plain_bot(BotKind::Random, 1)},
        {"monte-carlo", plain_bot(BotKind::MonteCarlo, 2)},
        {"deep-monte-carlo", plain_bot(BotKind::DeepMonteCarlo, 8)},
        {"handcrafted", plain_bot(BotKind::Handcrafted, 1)},
    };

    // SPZ joins only when its artifact matches the current card pool; the
    // deployed champion policy (rollout, four worlds, gamma 0.98, optional
    // advantage head at 0.6) pilots both seats.
    std::shared_ptr<const spz::SpzNet> spz_net;
    std::shared_ptr<const spz::SpzAdvantageNet> spz_advantage_net;
    {
        std::ifstream probe(spz_model);
        if (probe) {
            auto net = std::make_shared<const spz::SpzNet>(
                spz::load_spz_net(spz_model));
            const std::size_t inputs = net->input_count();
            if (inputs == spz::spz_feature_count() ||
                inputs == spz::spz_feature_count_v2() ||
                inputs == spz::spz_feature_count_v3()) {
                spz_net = std::move(net);
                std::ifstream advantage_probe(spz_advantage);
                if (advantage_probe) {
                    spz_advantage_net = std::make_shared<
                        const spz::SpzAdvantageNet>(
                        spz::load_spz_advantage_net(spz_advantage));
                }
            } else {
                std::cout << "spz skipped: " << spz_model
                          << " predates the current card pool\n";
            }
        } else {
            std::cout << "spz skipped: " << spz_model
                      << " not found\n";
        }
    }
    if (spz_net) {
        bots.push_back(
            {"spz",
             [spz_net, spz_advantage_net](
                 const std::array<std::vector<CardId>, 2>& game_decks,
                 std::uint64_t game_seed) {
                 GameConfig config;
                 for (std::size_t seat = 0; seat < 2; ++seat) {
                     spz::SpzPolicyConfig policy;
                     policy.worlds = 4;
                     policy.block_prediction_worlds = 4;
                     policy.rollout = true;
                     policy.gamma_per_turn = 0.98;
                     policy.seed = game_seed + seat;
                     if (spz_advantage_net) {
                         policy.pass_dominance_prune = false;
                         policy.advantage_scale = 0.6;
                     }
                     config.human_controllers[seat] =
                         spz::make_spz_controller(
                             spz_net, game_decks, seat, policy, nullptr,
                             nullptr, spz_advantage_net);
                 }
                 return config;
             }});
    }
    if (only_bot) {
        std::erase_if(bots, [&](const BotEntry& bot) {
            return bot.name != *only_bot;
        });
        if (bots.empty()) {
            std::cerr << "unknown bot " << *only_bot << "\n";
            return 1;
        }
    }

    const std::size_t deck_count = decks.size();
    std::vector<std::pair<std::size_t, std::size_t>> pairings;
    for (std::size_t low = 0; low < deck_count; ++low) {
        for (std::size_t high = low + 1; high < deck_count; ++high) {
            pairings.emplace_back(low, high);
        }
    }

    std::ostringstream json;
    json << "{\n  \"games_per_pairing\": " << games_per_pairing
         << ",\n  \"seed\": " << seed << ",\n  \"decks\": [";
    for (std::size_t deck = 0; deck < deck_count; ++deck) {
        json << (deck ? ", " : "") << '"' << decks[deck].name << '"';
    }
    json << "],\n  \"bots\": [\n";

    std::mutex progress_mutex;
    for (std::size_t bot = 0; bot < bots.size(); ++bot) {
        const BotEntry& entry = bots[bot];
        std::vector<std::vector<double>> matrix(
            deck_count, std::vector<double>(deck_count, 50.0));
        std::vector<double> pairing_points(pairings.size(), 0.0);

        run_jobs(
            pairings.size() * games_per_pairing, threads,
            [&](std::size_t job) {
                const std::size_t pairing = job / games_per_pairing;
                const std::size_t game = job % games_per_pairing;
                const auto [low, high] = pairings[pairing];
                // Balance seats and starting player; pairs of games share
                // a seed with swapped seats.
                const bool low_first = (game % 2) == 0;
                const std::size_t deck_zero = low_first ? low : high;
                const std::size_t deck_one = low_first ? high : low;
                const std::uint64_t game_seed =
                    seed + 7919 * (bot * 1000 + pairing) + game / 2;
                const std::array<std::vector<CardId>, 2> game_decks = {
                    decks[deck_zero].cards, decks[deck_one].cards};
                GameConfig config =
                    entry.configure(game_decks, game_seed);
                config.starting_player = (game / 2) % 2;
                Game match(game_decks[0], game_decks[1], game_seed,
                           std::move(config));
                const GameResult result = match.run();
                double low_points = 0.5;
                if (result.winner == 0) {
                    low_points = low_first ? 1.0 : 0.0;
                } else if (result.winner == 1) {
                    low_points = low_first ? 0.0 : 1.0;
                }
                std::lock_guard<std::mutex> lock(progress_mutex);
                pairing_points[pairing] += low_points;
            });

        for (std::size_t pairing = 0; pairing < pairings.size();
             ++pairing) {
            const auto [low, high] = pairings[pairing];
            const double rate = 100.0 * pairing_points[pairing] /
                                static_cast<double>(games_per_pairing);
            matrix[low][high] = rate;
            matrix[high][low] = 100.0 - rate;
        }

        json << "    {\"name\": \"" << entry.name
             << "\", \"matrix\": [";
        for (std::size_t row = 0; row < deck_count; ++row) {
            json << (row ? ", " : "") << "[";
            for (std::size_t col = 0; col < deck_count; ++col) {
                json << (col ? ", " : "")
                     << (row == col ? std::string("null")
                                    : std::to_string(matrix[row][col]));
            }
            json << "]";
        }
        json << "], \"deck_means\": [";
        for (std::size_t row = 0; row < deck_count; ++row) {
            double total = 0.0;
            for (std::size_t col = 0; col < deck_count; ++col) {
                if (col != row) {
                    total += matrix[row][col];
                }
            }
            json << (row ? ", " : "")
                 << (total / static_cast<double>(deck_count - 1));
        }
        json << "]}" << (bot + 1 < bots.size() ? "," : "") << "\n";

        std::cout << entry.name << " done ("
                  << pairings.size() * games_per_pairing << " games)\n";
    }
    json << "  ]\n}\n";

    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "cannot write " << output_path << "\n";
        return 1;
    }
    out << json.str();
    std::cout << "wrote " << output_path << "\n";
    return 0;
}
