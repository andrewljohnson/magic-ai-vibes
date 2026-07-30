#include "old_school/selfplay_zero.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace old_school;
using namespace old_school::selfplay_zero;

[[noreturn]] void usage() {
    std::cerr
        << "usage:\n"
        << "  selfplay-zero train --out PATH [--iterations N] [--games N]\n"
        << "      [--hidden N] [--seed N] [--threads N] [--max-turns N]\n"
        << "      [--worlds N] [--lr X] [--eps-start X] [--eps-final X]\n"
        << "  selfplay-zero benchmark --model PATH [--reps N] [--seed N]\n"
        << "      [--threads N] [--worlds N] [--max-turns N] [--rollout]\n"
        << "      [--cycles N] [--top-k N] [--ismcts] [--sims N]\n"
        << "      [--versus-champion]\n"
        << "      [--baseline handcrafted|random|montecarlo]\n";
    std::exit(2);
}

struct Arguments {
    std::string command;
    std::string out;
    std::string init;
    std::string model;
    std::string baseline = "handcrafted";
    std::size_t iterations = 60;
    std::size_t games = 128;
    std::size_t hidden = 64;
    std::uint64_t seed = 20260729;
    std::size_t threads = 1;
    std::size_t max_turns = 0;  // 0 selects the per-command default.
    std::size_t worlds = 0;     // 0 selects the per-command default.
    std::size_t reps = 20;
    std::size_t cycles = 1;
    std::size_t top_k = 5;
    std::size_t sims = 160;
    bool rollout = false;
    bool ismcts = false;
    bool versus_champion = false;
    double learning_rate = 0.01;
    double epsilon_start = 0.25;
    double epsilon_final = 0.03;
};

Arguments parse_arguments(int argc, char** argv) {
    if (argc < 2) {
        usage();
    }
    Arguments arguments;
    arguments.command = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view flag = argv[index];
        const auto next = [&]() -> std::string {
            index += 1;
            if (index >= argc) {
                usage();
            }
            return argv[index];
        };
        if (flag == "--out") {
            arguments.out = next();
        } else if (flag == "--init") {
            arguments.init = next();
        } else if (flag == "--model") {
            arguments.model = next();
        } else if (flag == "--baseline") {
            arguments.baseline = next();
        } else if (flag == "--iterations") {
            arguments.iterations = std::stoull(next());
        } else if (flag == "--games") {
            arguments.games = std::stoull(next());
        } else if (flag == "--hidden") {
            arguments.hidden = std::stoull(next());
        } else if (flag == "--seed") {
            arguments.seed = std::stoull(next());
        } else if (flag == "--threads") {
            arguments.threads = std::stoull(next());
        } else if (flag == "--max-turns") {
            arguments.max_turns = std::stoull(next());
        } else if (flag == "--worlds") {
            arguments.worlds = std::stoull(next());
        } else if (flag == "--reps") {
            arguments.reps = std::stoull(next());
        } else if (flag == "--rollout") {
            arguments.rollout = true;
        } else if (flag == "--cycles") {
            arguments.cycles = std::stoull(next());
        } else if (flag == "--top-k") {
            arguments.top_k = std::stoull(next());
        } else if (flag == "--ismcts") {
            arguments.ismcts = true;
        } else if (flag == "--sims") {
            arguments.sims = std::stoull(next());
        } else if (flag == "--versus-champion") {
            arguments.versus_champion = true;
        } else if (flag == "--lr") {
            arguments.learning_rate = std::stod(next());
        } else if (flag == "--eps-start") {
            arguments.epsilon_start = std::stod(next());
        } else if (flag == "--eps-final") {
            arguments.epsilon_final = std::stod(next());
        } else {
            std::cerr << "unknown flag: " << flag << '\n';
            usage();
        }
    }
    return arguments;
}

int run_train(const Arguments& arguments) {
    if (arguments.out.empty()) {
        usage();
    }
    SpzTrainConfig config;
    config.iterations = arguments.iterations;
    config.games_per_iteration = arguments.games;
    config.hidden = arguments.hidden;
    config.seed = arguments.seed;
    config.threads = arguments.threads;
    config.learning_rate = arguments.learning_rate;
    config.epsilon_start = arguments.epsilon_start;
    config.epsilon_final = arguments.epsilon_final;
    if (arguments.max_turns != 0) {
        config.max_turns = arguments.max_turns;
    }
    if (arguments.worlds != 0) {
        config.training_worlds = arguments.worlds;
    }
    config.rollout = arguments.rollout;
    if (!arguments.init.empty()) {
        config.initial_net = std::make_shared<const SpzNet>(
            load_spz_net(arguments.init));
    }
    config.checkpoint_prefix = arguments.out + ".ckpt-";
    config.log = [](const std::string& line) {
        std::cout << line << std::endl;
    };
    const auto net = train_spz(config);
    save_spz_net(*net, arguments.out);
    std::cout << "saved " << arguments.out << std::endl;
    return 0;
}

int run_benchmark(const Arguments& arguments) {
    if (arguments.model.empty()) {
        usage();
    }
    BotKind baseline = BotKind::Handcrafted;
    if (arguments.baseline == "handcrafted") {
        baseline = BotKind::Handcrafted;
    } else if (arguments.baseline == "random") {
        baseline = BotKind::Random;
    } else if (arguments.baseline == "montecarlo") {
        baseline = BotKind::MonteCarlo;
    } else {
        std::cerr << "unknown baseline: " << arguments.baseline << '\n';
        usage();
    }
    const auto net =
        std::make_shared<const SpzNet>(load_spz_net(arguments.model));
    SpzPolicyConfig policy;
    policy.worlds = arguments.worlds == 0 ? 4 : arguments.worlds;
    policy.block_prediction_worlds = policy.worlds;
    policy.rollout = arguments.rollout;
    policy.rollout_turn_cycles = arguments.cycles;
    policy.rollout_top_k = arguments.top_k;
    if (arguments.ismcts) {
        policy.search = SpzPolicyConfig::Search::Ismcts;
        policy.ismcts_iterations = arguments.sims;
    }
    SpzPolicyConfig champion_policy;
    champion_policy.worlds = 4;
    champion_policy.block_prediction_worlds = 4;
    champion_policy.rollout = true;
    const std::size_t max_turns =
        arguments.max_turns == 0 ? 200 : arguments.max_turns;
    const auto result = run_spz_benchmark(
        net, baseline, arguments.reps, arguments.seed, policy, max_turns,
        arguments.threads,
        [](const std::string& line) { std::cout << line << std::endl; },
        arguments.versus_champion ? &champion_policy : nullptr);
    std::cout << std::fixed << std::setprecision(4);
    for (std::size_t deck = 0; deck < kSpzDeckCount; ++deck) {
        const SpzDeckStats& stats = result.per_deck[deck];
        std::cout << "deck " << spz_deck_name(deck) << ": games "
                  << stats.games << " wins " << stats.wins << " losses "
                  << stats.losses << " draws " << stats.draws
                  << " win-rate " << stats.win_rate()
                  << " baseline-pilots-it " << std::setprecision(4)
                  << result.baseline_deck_win_rate(deck) << '\n';
    }
    std::cout << "aggregate: games " << result.aggregate.games << " wins "
              << result.aggregate.wins << " losses "
              << result.aggregate.losses << " draws "
              << result.aggregate.draws << " win-rate "
              << result.aggregate.win_rate() << " wilson-lcb95 "
              << result.wilson_lower_bound_95 << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Arguments arguments = parse_arguments(argc, argv);
    if (arguments.command == "train") {
        return run_train(arguments);
    }
    if (arguments.command == "benchmark") {
        return run_benchmark(arguments);
    }
    usage();
}
