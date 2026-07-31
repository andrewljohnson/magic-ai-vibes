#include "old_school/web_bridge.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
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
        << "Usage: old-school-web-bridge [session options]\n"
        << "  --human-deck green|red|blue|white|ru-aggro|lotus-combo|burn\n"
        << "  --human-deck-cards ID,ID,... (exactly 40)\n"
        << "  --opponent-deck green|red|blue|white|ru-aggro|lotus-combo|burn\n"
        << "  --opponent-deck-cards ID,ID,... (exactly 40)\n"
        << "  --opponent-policy random|monte-carlo|deep-monte-carlo|"
           "handcrafted|spz\n"
        << "  --spz-artifact PATH (Self-Play Zero net; default "
           "data/spz-champion-v7.txt beside the repo)\n"
        << "  --seed N\n"
        << "  --rollouts N --deep-rollouts N\n"
        << "  --debug-reveal\n"
        << "  --bluff-mode\n"
        << "\n"
        << "       old-school-web-bridge --evolve-json "
           "[evolution options]\n"
        << "  --evolve-pilot random|monte-carlo|deep-monte-carlo|"
           "handcrafted|spz\n"
        << "  --generations N --population N --games N --seed N\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path executable =
            std::filesystem::absolute(argv[0]);
        const std::string spz_artifact_path =
            (executable.parent_path().parent_path() / "data" /
             "spz-champion-v7.txt")
                .string();
        if (argc == 2 &&
            std::string_view(argv[1]) == "--help") {
            print_help(std::cout);
            return 0;
        }

        bool evolve_json = false;
        for (int argument = 1; argument < argc; ++argument) {
            if (std::string_view(argv[argument]) ==
                "--evolve-json") {
                evolve_json = true;
            }
        }
        if (evolve_json) {
            old_school::web::EvolutionJsonConfig config;
            config.spz_artifact_path = spz_artifact_path;
            std::set<std::string> seen_options;
            for (int argument = 1; argument < argc; ++argument) {
                const std::string_view option(argv[argument]);
                if (!seen_options.insert(
                         std::string(option))
                         .second) {
                    throw std::invalid_argument(
                        "duplicate option: " +
                        std::string(option));
                }
                if (option == "--evolve-json") {
                    continue;
                }
                if (option != "--evolve-pilot" &&
                    option != "--generations" &&
                    option != "--population" &&
                    option != "--games" &&
                    option != "--spz-artifact" &&
                    option != "--seed") {
                    throw std::invalid_argument(
                        "unknown evolution option: " +
                        std::string(option));
                }
                if (argument + 1 >= argc) {
                    throw std::invalid_argument(
                        std::string(option) +
                        " requires a value");
                }
                const std::string_view value(argv[++argument]);
                if (option == "--evolve-pilot") {
                    config.pilot =
                        old_school::web::parse_evolution_pilot(
                            value);
                } else if (option == "--spz-artifact") {
                    config.spz_artifact_path =
                        std::string(value);
                } else if (option == "--generations") {
                    config.generations =
                        parse_positive_size(option, value);
                } else if (option == "--population") {
                    config.population =
                        parse_positive_size(option, value);
                } else if (option == "--games") {
                    config.games_per_opponent =
                        parse_positive_size(option, value);
                } else {
                    config.seed = parse_u64(option, value);
                }
            }
            return old_school::web::run_evolution_json(
                std::cout, config);
        }

        old_school::web::BridgeConfig config;
        config.spz_artifact_path = spz_artifact_path;
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
            } else if (option == "--human-deck-cards") {
                config.human_deck_cards =
                    old_school::web::parse_exact_deck_cards(
                        value);
            } else if (option == "--opponent-deck") {
                config.opponent_deck =
                    old_school::web::parse_deck_id(value);
            } else if (option == "--opponent-deck-cards") {
                config.opponent_deck_cards =
                    old_school::web::parse_exact_deck_cards(
                        value);
            } else if (option == "--opponent-policy") {
                if (value == "spz" || value == "self-play-zero" ||
                    value == "selfplay-zero") {
                    config.opponent_spz = true;
                    config.opponent_bot =
                        old_school::BotKind::Random;
                } else {
                    config.opponent_spz = false;
                    config.opponent_bot =
                        old_school::web::parse_opponent_bot(
                            value);
                }
            } else if (option == "--spz-artifact") {
                config.spz_artifact_path = std::string(value);
            } else if (option == "--seed") {
                config.game_seed = parse_u64(option, value);
            } else if (option == "--rollouts") {
                config.monte_carlo_rollouts =
                    parse_positive_size(option, value);
            } else if (option == "--deep-rollouts") {
                config.deep_monte_carlo_rollouts =
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
