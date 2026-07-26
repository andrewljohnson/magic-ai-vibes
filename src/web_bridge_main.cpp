#include "old_school/web_bridge.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::uint64_t parse_u64(std::string_view option,
                        std::string_view value) {
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} ||
        result.ptr != value.data() + value.size()) {
        throw std::invalid_argument(
            std::string(option) + " requires an unsigned integer");
    }
    return parsed;
}

std::size_t parse_positive_size(std::string_view option,
                                std::string_view value) {
    const std::uint64_t parsed = parse_u64(option, value);
    if (parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(
            std::string(option) + " requires a positive integer");
    }
    return static_cast<std::size_t>(parsed);
}

void print_help(std::ostream& output) {
    output
        << "Usage: old-school-web-bridge [options]\n"
        << "  --human-deck green|red|blue|white|ru-aggro\n"
        << "  --opponent-deck green|red|blue|white|ru-aggro\n"
        << "  --opponent-policy random|monte-carlo|deep-monte-carlo|"
           "handcrafted|learned-value|learned-actor\n"
        << "  --seed N --train-games N --train-seed N\n"
        << "  --rollouts N --deep-rollouts N --learned-rollouts N\n"
        << "  --debug-reveal\n"
        << "  --bluff-mode\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        old_school::web::BridgeConfig config;
        for (int argument = 1; argument < argc; ++argument) {
            const std::string_view option(argv[argument]);
            if (option == "--help") {
                print_help(std::cout);
                return 0;
            }
            if (option == "--debug-reveal") {
                config.reveal_opponent_hand = true;
                continue;
            }
            if (option == "--bluff-mode") {
                config.bluff_mode = true;
                continue;
            }
            if (argument + 1 >= argc) {
                throw std::invalid_argument(
                    std::string(option) + " requires a value");
            }
            const std::string_view value(argv[++argument]);
            if (option == "--human-deck") {
                config.human_deck =
                    old_school::web::parse_deck_id(value);
            } else if (option == "--opponent-deck") {
                config.opponent_deck =
                    old_school::web::parse_deck_id(value);
            } else if (option == "--opponent-policy") {
                config.opponent_bot =
                    old_school::web::parse_opponent_bot(
                        value, config.learned_variant);
            } else if (option == "--seed") {
                config.game_seed = parse_u64(option, value);
            } else if (option == "--train-games") {
                config.training_games =
                    parse_positive_size(option, value);
            } else if (option == "--train-seed") {
                config.training_seed = parse_u64(option, value);
            } else if (option == "--rollouts") {
                config.monte_carlo_rollouts =
                    parse_positive_size(option, value);
            } else if (option == "--deep-rollouts") {
                config.deep_monte_carlo_rollouts =
                    parse_positive_size(option, value);
            } else if (option == "--learned-rollouts") {
                config.learned_rollouts =
                    parse_positive_size(option, value);
            } else {
                throw std::invalid_argument(
                    "unknown option: " + std::string(option));
            }
        }
        return old_school::web::run_bridge_session(
            std::cin, std::cout, config);
    } catch (const std::exception& error) {
        std::cerr << "old-school-web-bridge: "
                  << error.what() << '\n';
        return 1;
    }
}
