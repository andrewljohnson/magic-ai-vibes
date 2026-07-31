// Matchup-matrix tool: for each bot, play every unordered deck pairing with
// that bot piloting both seats and report deck-vs-deck win rates, plus a
// single bots-by-decks summary (each deck's mean win rate across the field
// under that pilot). Output is JSON for the training-monitor site.

#include "old_school/game.hpp"
#include "old_school/selfplay_zero.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
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
    // Installs this bot as the pilot of one seat (BotConfig or an SPZ
    // controller), so mirror and cross-pilot games share one codepath.
    std::function<void(GameConfig&, std::size_t,
                       const std::array<std::vector<CardId>, 2>&,
                       std::uint64_t)>
        pilot_seat;
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
    std::string spz_model = "data/spz-champion-v9.txt";
    std::string spz_advantage = "data/spz-advantage-v5.txt";
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

    std::vector<DeckEntry> decks = {
        {"green", green_deck()},
        {"red", red_deck()},
        {"blue", blue_deck()},
        {"white", white_control_deck()},
        {"ru-aggro", ru_aggro_deck()},
        {"lotus-combo", lotus_combo_deck()},
        {"burn", burn_deck()},
    };
    // Persisted evolved decks join the matrix: data/*.deck holds forty
    // whitespace-separated card ids; the filename stem is the deck name.
    if (std::filesystem::is_directory("data")) {
        std::vector<std::filesystem::path> deck_files;
        for (const auto& entry :
             std::filesystem::directory_iterator("data")) {
            if (entry.path().extension() == ".deck") {
                deck_files.push_back(entry.path());
            }
        }
        std::sort(deck_files.begin(), deck_files.end());
        for (const auto& path : deck_files) {
            std::ifstream in(path);
            std::vector<CardId> cards;
            unsigned int id = 0;
            while (in >> id && cards.size() < 40) {
                if (id >= kCardCount) {
                    break;
                }
                cards.push_back(static_cast<CardId>(id));
            }
            if (cards.size() == 40) {
                decks.push_back({path.stem().string(), cards});
            } else {
                std::cout << "skipping " << path
                          << ": not 40 valid card ids\n";
            }
        }
    }

    const auto plain_bot = [](BotKind kind, std::size_t rollouts) {
        return [kind, rollouts](
                   GameConfig& config, std::size_t seat,
                   const std::array<std::vector<CardId>, 2>&,
                   std::uint64_t) {
            config.bots[seat] =
                BotConfig{.kind = kind, .rollouts_per_action = rollouts};
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
            spz_net = std::make_shared<const spz::SpzNet>(
                spz::load_spz_net(spz_model));
            std::ifstream advantage_probe(spz_advantage);
            if (advantage_probe) {
                spz_advantage_net =
                    std::make_shared<const spz::SpzAdvantageNet>(
                        spz::load_spz_advantage_net(spz_advantage));
            }
        } else {
            std::cout << "spz skipped: " << spz_model
                      << " not found\n";
        }
    }
    // The previous champion keeps a row for old-vs-new comparison when
    // its artifacts are preserved on promotion.
    const auto add_spz_bot = [&bots](
                                 const std::string& name,
                                 std::shared_ptr<const spz::SpzNet> bot_net,
                                 std::shared_ptr<const spz::SpzAdvantageNet>
                                     bot_advantage) {
        bots.push_back(
            {name,
             [bot_net, bot_advantage](
                 GameConfig& config, std::size_t seat,
                 const std::array<std::vector<CardId>, 2>& game_decks,
                 std::uint64_t game_seed) {
                 spz::SpzPolicyConfig policy;
                 policy.worlds = 4;
                 policy.block_prediction_worlds = 4;
                 policy.rollout = true;
                 policy.gamma_per_turn = 0.98;
                 policy.seed = game_seed + seat;
                 config.human_controllers[seat] =
                     spz::make_spz_controller(bot_net, game_decks, seat,
                                              policy, nullptr, nullptr,
                                              bot_advantage);
             }});
    };
    if (spz_net) {
        bots.push_back(
            {"spz",
             [spz_net, spz_advantage_net](
                 GameConfig& config, std::size_t seat,
                 const std::array<std::vector<CardId>, 2>& game_decks,
                 std::uint64_t game_seed) {
                 spz::SpzPolicyConfig policy;
                 policy.worlds = 4;
                 policy.block_prediction_worlds = 4;
                 policy.rollout = true;
                 policy.gamma_per_turn = 0.98;
                 policy.seed = game_seed + seat;
                 if (spz_advantage_net) {
                     policy.pass_dominance_prune = false;
                 }
                 config.human_controllers[seat] =
                     spz::make_spz_controller(spz_net, game_decks, seat,
                                              policy, nullptr, nullptr,
                                              spz_advantage_net);
             }});
    }
    {
        std::ifstream prev_probe("data/spz-champion-prev.txt");
        if (prev_probe) {
            auto prev_net = std::make_shared<const spz::SpzNet>(
                spz::load_spz_net("data/spz-champion-prev.txt"));
            std::shared_ptr<const spz::SpzAdvantageNet> prev_advantage;
            std::ifstream prev_adv_probe("data/spz-advantage-prev.txt");
            if (prev_adv_probe) {
                prev_advantage =
                    std::make_shared<const spz::SpzAdvantageNet>(
                        spz::load_spz_advantage_net(
                            "data/spz-advantage-prev.txt"));
            }
            add_spz_bot("spz-prev", std::move(prev_net),
                        std::move(prev_advantage));
        }
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

    {
        // Deck catalog for the telemetry site's decks page.
        std::ofstream deck_out("build/telemetry/decks.json");
        if (deck_out) {
            deck_out << "{\"decks\":[";
            for (std::size_t deck = 0; deck < decks.size(); ++deck) {
                if (deck != 0) {
                    deck_out << ',';
                }
                deck_out << "{\"name\":\"" << decks[deck].name
                         << "\",\"cards\":[";
                std::array<std::size_t, kCardCount> counts{};
                for (const CardId card : decks[deck].cards) {
                    ++counts[static_cast<std::size_t>(card)];
                }
                bool wrote = false;
                for (std::size_t card = 0; card < counts.size();
                     ++card) {
                    if (counts[card] == 0) {
                        continue;
                    }
                    const auto& definition =
                        card_definition(static_cast<CardId>(card));
                    if (wrote) {
                        deck_out << ',';
                    }
                    wrote = true;
                    const auto& cost = definition.cost;
                    deck_out << "{\"name\":\"" << definition.name
                             << "\",\"count\":" << counts[card]
                             << ",\"type\":"
                             << static_cast<int>(definition.type)
                             << ",\"cost\":"
                             << cost.generic + cost.green + cost.red +
                                    cost.blue + cost.white
                             << ",\"power\":" << definition.power
                             << ",\"toughness\":"
                             << definition.toughness << '}';
                }
                deck_out << "]}";
            }
            deck_out << "]}\n";
        }
    }
    const std::size_t deck_count = decks.size();
    std::vector<std::pair<std::size_t, std::size_t>> pairings;
    for (std::size_t low = 0; low < deck_count; ++low) {
        for (std::size_t high = low + 1; high < deck_count; ++high) {
            pairings.emplace_back(low, high);
        }
    }

    // One paired game: `pilots[seat]` flies decks[deck_ids[seat]]. Returns
    // seat-zero points (win 1, draw 0.5).
    const auto play_game = [&](const BotEntry& zero, const BotEntry& one,
                               std::size_t deck_zero, std::size_t deck_one,
                               std::uint64_t game_seed,
                               std::size_t starting_player) {
        const std::array<std::vector<CardId>, 2> game_decks = {
            decks[deck_zero].cards, decks[deck_one].cards};
        GameConfig config;
        config.starting_player = starting_player;
        zero.pilot_seat(config, 0, game_decks, game_seed);
        one.pilot_seat(config, 1, game_decks, game_seed);
        Game match(game_decks[0], game_decks[1], game_seed,
                   std::move(config));
        const GameResult result = match.run();
        if (result.winner == 0) {
            return 1.0;
        }
        return result.winner == 1 ? 0.0 : 0.5;
    };

    const BotEntry& reference =
        *std::find_if(bots.begin(), bots.end(), [](const BotEntry& bot) {
            return bot.name == "handcrafted";
        });

    std::string header;
    {
        std::ostringstream head;
        head << "{\n  \"games_per_pairing\": " << games_per_pairing
             << ",\n  \"seed\": " << seed
             << ",\n  \"reference_pilot\": \"" << reference.name
             << "\",\n  \"decks\": [";
        for (std::size_t deck = 0; deck < deck_count; ++deck) {
            head << (deck ? ", " : "") << '"' << decks[deck].name
                 << '"';
        }
        head << "],\n  \"bots\": [\n";
        header = head.str();
    }
    const std::string progress_path = [&output_path]() {
        const auto slash = output_path.find_last_of('/');
        const std::string dir =
            slash == std::string::npos
                ? std::string()
                : output_path.substr(0, slash + 1);
        return dir + "matchup-progress.json";
    }();
    std::atomic<std::size_t> stage_done{0};
    const auto write_progress = [&](const std::string& status,
                                    const std::string& bot_name,
                                    std::size_t bot_index,
                                    std::size_t bot_count,
                                    const std::string& stage,
                                    std::size_t done,
                                    std::size_t total) {
        std::ofstream out(progress_path);
        if (!out) {
            return;
        }
        out << "{\"status\":\"" << status << "\",\"bot\":\""
            << bot_name << "\",\"botIndex\":" << bot_index
            << ",\"botCount\":" << bot_count << ",\"stage\":\""
            << stage << "\",\"done\":" << done << ",\"total\":"
            << total << "}\n";
    };
    std::vector<std::string> finished_bots;
    const auto write_snapshot = [&]() {
        std::ofstream out(output_path);
        if (!out) {
            std::cerr << "cannot write " << output_path << "\n";
            std::exit(1);
        }
        out << header;
        for (std::size_t index = 0; index < finished_bots.size();
             ++index) {
            out << finished_bots[index]
                << (index + 1 < finished_bots.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
    };

    std::mutex points_mutex;
    for (std::size_t bot = 0; bot < bots.size(); ++bot) {
        const BotEntry& entry = bots[bot];

        // Mirror grid: this bot pilots every seat, deck against deck.
        std::vector<std::vector<double>> matrix(
            deck_count, std::vector<double>(deck_count, 50.0));
        std::vector<double> pairing_points(pairings.size(), 0.0);
        const std::size_t mirror_total =
            pairings.size() * games_per_pairing;
        stage_done.store(0);
        write_progress("running", entry.name, bot, bots.size(),
                       "mirror grid", 0, mirror_total);
        run_jobs(
            mirror_total, threads,
            [&](std::size_t job) {
                const std::size_t finished =
                    stage_done.fetch_add(1) + 1;
                if (finished % 100 == 0) {
                    write_progress("running", entry.name, bot,
                                   bots.size(), "mirror grid",
                                   finished, mirror_total);
                }
                const std::size_t pairing = job / games_per_pairing;
                const std::size_t game = job % games_per_pairing;
                const auto [low, high] = pairings[pairing];
                // Balance seats and starting player; pairs of games share
                // a seed with swapped seats.
                const bool low_first = (game % 2) == 0;
                const std::uint64_t game_seed =
                    seed + 7919 * (bot * 1000 + pairing) + game / 2;
                const double zero_points = play_game(
                    entry, entry, low_first ? low : high,
                    low_first ? high : low, game_seed, (game / 2) % 2);
                const double low_points =
                    low_first ? zero_points : 1.0 - zero_points;
                std::lock_guard<std::mutex> lock(points_mutex);
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

        // Field row: this bot pilots each deck against the other decks,
        // all flown by the reference pilot. Comparable across bots: a
        // stronger pilot scores higher with the same deck.
        const std::size_t field_games =
            std::max<std::size_t>(2, games_per_pairing / 2) & ~1ULL;
        std::vector<double> field_points(deck_count, 0.0);
        std::vector<std::size_t> field_totals(deck_count, 0);
        const std::size_t field_total =
            deck_count * (deck_count - 1) * field_games;
        stage_done.store(0);
        write_progress("running", entry.name, bot, bots.size(),
                       "vs field", 0, field_total);
        run_jobs(
            field_total, threads,
            [&](std::size_t job) {
                const std::size_t finished =
                    stage_done.fetch_add(1) + 1;
                if (finished % 100 == 0) {
                    write_progress("running", entry.name, bot,
                                   bots.size(), "vs field", finished,
                                   field_total);
                }
                const std::size_t game = job % field_games;
                const std::size_t pair = job / field_games;
                const std::size_t mine = pair / (deck_count - 1);
                std::size_t theirs = pair % (deck_count - 1);
                theirs += theirs >= mine ? 1 : 0;
                const bool mine_first = (game % 2) == 0;
                const std::uint64_t game_seed =
                    seed + 6007 * (bot * 1000 + pair) + game / 2;
                const double zero_points =
                    mine_first ? play_game(entry, reference, mine,
                                           theirs, game_seed,
                                           (game / 2) % 2)
                               : play_game(reference, entry, theirs,
                                           mine, game_seed,
                                           (game / 2) % 2);
                const double my_points =
                    mine_first ? zero_points : 1.0 - zero_points;
                std::lock_guard<std::mutex> lock(points_mutex);
                field_points[mine] += my_points;
                field_totals[mine] += 1;
            });

        std::ostringstream json;
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
        json << "], \"vs_field\": [";
        for (std::size_t deck = 0; deck < deck_count; ++deck) {
            json << (deck ? ", " : "")
                 << (100.0 * field_points[deck] /
                     static_cast<double>(field_totals[deck]));
        }
        json << "]}";
        finished_bots.push_back(json.str());
        write_snapshot();
        std::cout << entry.name << " done\n";
    }
    write_progress("complete", "", bots.size(), bots.size(), "done",
                   0, 0);
    std::cout << "wrote " << output_path << "\n";
    return 0;
}
