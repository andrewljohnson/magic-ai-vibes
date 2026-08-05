#include "old_school/selfplay_zero.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
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
    std::string policy;
    std::string policy_out;
    std::string policy_init;
    std::string advantage;
    std::string advantage_out;
    std::string advantage_init;
    bool no_value_training = false;
    bool no_guardrails = false;

    std::string baseline_model;
    std::string telemetry;
    std::string spar_model;
    double spar_rules = 0.0;
    double tie_band = 0.0;
    std::size_t focus_deck = 0;
    double focus_weight = 0.0;
    std::size_t probe_every = 0;
    std::size_t probe_reps = 2;
    std::string baseline = "handcrafted";
    std::size_t iterations = 60;
    std::size_t games = 128;
    std::size_t hidden = 64;
    std::size_t hidden2 = 0;
    std::size_t schema = 0;
    bool hard_targets = false;
    double gamma = 1.0;
    double td_lambda = 1.0;
    std::uint64_t seed = 20260729;
    std::size_t threads = 1;
    std::size_t max_turns = 0;  // 0 selects the per-command default.
    std::size_t worlds = 0;     // 0 selects the per-command default.
    std::size_t reps = 20;
    std::size_t cycles = 1;
    std::size_t top_k = 5;
    std::size_t sims = 160;
    bool rollout = false;
    bool payment_branching = false;
    bool adaptive_depth = false;
    bool ismcts = false;
    bool versus_champion = false;
    double learning_rate = 0.01;
    double epsilon_start = 0.25;
    double epsilon_final = 0.03;
    // Metagame restriction for train/benchmark: spz_decks() indices.
    std::vector<std::size_t> decks;
};

std::vector<std::size_t> parse_deck_list(const std::string& value) {
    static constexpr std::array<std::string_view, kSpzDeckCount> kIds = {
        "green",  "red",    "blue",         "white",
        "ru-aggro", "lotus-combo", "burn", "uwr",
        "robots", "white-weenie", "br-midrange",
        "rg-berserk", "atog",
    };
    std::vector<std::size_t> result;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            continue;
        }
        std::size_t index = kSpzDeckCount;
        for (std::size_t deck = 0; deck < kSpzDeckCount; ++deck) {
            if (token == kIds[deck]) {
                index = deck;
                break;
            }
        }
        if (index == kSpzDeckCount) {
            index = std::stoull(token);
        }
        if (index >= kSpzDeckCount) {
            throw std::invalid_argument("unknown deck: " + token);
        }
        result.push_back(index);
    }
    if (result.empty()) {
        throw std::invalid_argument("--decks parsed to an empty list");
    }
    return result;
}

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
        } else if (flag == "--baseline-model") {
            arguments.baseline_model = next();
        } else if (flag == "--policy") {
            arguments.policy = next();
        } else if (flag == "--policy-out") {
            arguments.policy_out = next();
        } else if (flag == "--policy-init") {
            arguments.policy_init = next();
        } else if (flag == "--advantage") {
            arguments.advantage = next();
        } else if (flag == "--advantage-out") {
            arguments.advantage_out = next();
        } else if (flag == "--advantage-init") {
            arguments.advantage_init = next();
        } else if (flag == "--no-value-training") {
            arguments.no_value_training = true;
        } else if (flag == "--no-guardrails") {
            arguments.no_guardrails = true;
        } else if (flag == "--telemetry") {
            arguments.telemetry = next();
        } else if (flag == "--spar-model") {
            arguments.spar_model = next();
        } else if (flag == "--tie-band") {
            arguments.tie_band = std::stod(next());
        } else if (flag == "--focus-deck") {
            arguments.focus_deck = std::stoull(next());
        } else if (flag == "--focus-weight") {
            arguments.focus_weight = std::stod(next());
        } else if (flag == "--spar-rules") {
            arguments.spar_rules = std::stod(next());
        } else if (flag == "--probe-every") {
            arguments.probe_every = std::stoull(next());
        } else if (flag == "--probe-reps") {
            arguments.probe_reps = std::stoull(next());
        } else if (flag == "--baseline") {
            arguments.baseline = next();
        } else if (flag == "--iterations") {
            arguments.iterations = std::stoull(next());
        } else if (flag == "--games") {
            arguments.games = std::stoull(next());
        } else if (flag == "--schema-v2") {
            arguments.schema = 2;
        } else if (flag == "--schema-v3") {
            arguments.schema = 3;
        } else if (flag == "--schema-colors") {
            arguments.schema = 4;
        } else if (flag == "--hard-targets") {
            arguments.hard_targets = true;
        } else if (flag == "--td-lambda") {
            arguments.td_lambda = std::stod(next());
        } else if (flag == "--gamma") {
            arguments.gamma = std::stod(next());
        } else if (flag == "--hidden") {
            arguments.hidden = std::stoull(next());
        } else if (flag == "--hidden2") {
            arguments.hidden2 = std::stoull(next());
        } else if (flag == "--decks") {
            arguments.decks = parse_deck_list(next());
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
        } else if (flag == "--payment-branching") {
            arguments.payment_branching = true;
        } else if (flag == "--adaptive-depth") {
            arguments.adaptive_depth = true;
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

int run_train(const Arguments& raw_arguments) {
    Arguments arguments = raw_arguments;
    if (arguments.out.empty()) {
        usage();
    }
    // Every training run reports to the live monitor by default.
    if (arguments.telemetry.empty()) {
        arguments.telemetry = "build/telemetry/telemetry.jsonl";
    }
    if (arguments.probe_every == 0) {
        arguments.probe_every = 10;
    }
    SpzTrainConfig config;
    config.iterations = arguments.iterations;
    config.games_per_iteration = arguments.games;
    config.hidden = arguments.hidden;
    config.hidden2 = arguments.hidden2;
    config.schema = arguments.schema;
    config.discounted_targets = !arguments.hard_targets;
    config.gamma = arguments.gamma;
    config.td_lambda = arguments.td_lambda;
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
    config.ismcts = arguments.ismcts;
    config.ismcts_iterations = arguments.sims;
    config.training_decks = arguments.decks;
    if (!arguments.advantage_out.empty()) {
        config.train_advantage = true;
        if (!arguments.advantage_init.empty()) {
            config.initial_advantage =
                std::make_shared<const SpzAdvantageNet>(
                    load_spz_advantage_net(arguments.advantage_init));
        }
    }
    config.train_value = !arguments.no_value_training;
    if (!arguments.policy_out.empty()) {
        config.train_policy = true;
        if (!arguments.policy_init.empty()) {
            config.initial_policy =
                std::make_shared<const SpzPolicyNet>(
                    load_spz_policy_net(arguments.policy_init));
        }
    }
    if (!arguments.init.empty()) {
        config.initial_net = std::make_shared<const SpzNet>(
            load_spz_net(arguments.init));
    }
    config.rules_spar_probability = arguments.spar_rules;
    config.focus_probability = arguments.focus_weight;
    config.focus_deck = arguments.focus_deck;
    if (!arguments.spar_model.empty()) {
        config.spar_net = std::make_shared<const SpzNet>(
            load_spz_net(arguments.spar_model));
    }
    config.telemetry_path = arguments.telemetry;
    config.probe_interval = arguments.probe_every;
    config.probe_reps = arguments.probe_reps;
    config.checkpoint_prefix = arguments.out + ".ckpt-";
    config.log = [](const std::string& line) {
        std::cout << line << std::endl;
    };
    const auto output = train_spz(config);
    save_spz_net(*output.value, arguments.out);
    std::cout << "saved " << arguments.out << std::endl;
    if (output.policy != nullptr && !arguments.policy_out.empty()) {
        save_spz_policy_net(*output.policy, arguments.policy_out);
        std::cout << "saved " << arguments.policy_out << std::endl;
    }
    if (output.advantage != nullptr &&
        !arguments.advantage_out.empty()) {
        save_spz_advantage_net(*output.advantage,
                               arguments.advantage_out);
        std::cout << "saved " << arguments.advantage_out << std::endl;
    }
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
    policy.payment_branching = arguments.payment_branching;
    policy.adaptive_depth = arguments.adaptive_depth;
    policy.rollout_turn_cycles = arguments.cycles;
    policy.rollout_top_k = arguments.top_k;
    policy.gamma_per_turn = arguments.gamma;
    policy.benchmark_decks = arguments.decks;
    if (arguments.tie_band > 0.0) {
        policy.advantage_tie_band = arguments.tie_band;
    }
    policy.pass_dominance_prune = !arguments.no_guardrails;
    if (arguments.ismcts) {
        policy.search = SpzPolicyConfig::Search::Ismcts;
        policy.ismcts_iterations = arguments.sims;
    }
    SpzPolicyConfig champion_policy;
    champion_policy.worlds = 4;
    champion_policy.block_prediction_worlds = 4;
    champion_policy.rollout = true;
    champion_policy.gamma_per_turn = 0.98;  // deployed arena config
    const std::size_t max_turns =
        arguments.max_turns == 0 ? 200 : arguments.max_turns;
    const auto result = run_spz_benchmark(
        net, baseline, arguments.reps, arguments.seed, policy, max_turns,
        arguments.threads,
        [](const std::string& line) { std::cout << line << std::endl; },
        arguments.versus_champion ? &champion_policy : nullptr,
        arguments.baseline_model.empty()
            ? nullptr
            : std::make_shared<const SpzNet>(
                  load_spz_net(arguments.baseline_model)),
        arguments.policy.empty()
            ? nullptr
            : std::make_shared<const SpzPolicyNet>(
                  load_spz_policy_net(arguments.policy)),
        nullptr,
        arguments.advantage.empty()
            ? nullptr
            : std::make_shared<const SpzAdvantageNet>(
                  load_spz_advantage_net(arguments.advantage)),
        nullptr);
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
    if (arguments.command == "behavior-probe") {
        const auto net = std::make_shared<const SpzNet>(
            load_spz_net(arguments.model));
        const auto rates =
            run_behavior_probes(net, arguments.seed);
        std::printf(
            "land-drop %.2f no-self-bolt %.2f growth-save %.2f "
            "spike-good %.2f spike-restraint %.2f race-removal %.2f "
            "counter-respect %.2f no-self-swords %.2f\n",
            rates.land_drop, rates.no_self_bolt, rates.growth_save,
            rates.spike_good, rates.spike_restraint,
            rates.race_removal, rates.counter_respect,
            rates.no_self_swords);
        return 0;
    }
    if (arguments.command == "benchmark") {
        return run_benchmark(arguments);
    }
    usage();
}
