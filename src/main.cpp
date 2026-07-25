#include "old_school/game.hpp"
#include "old_school/interactive.hpp"
#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t random_seed() {
    std::random_device entropy;
    const auto clock_value = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return (static_cast<std::uint64_t>(entropy()) << 32U) ^
           static_cast<std::uint64_t>(entropy()) ^ clock_value;
}

std::uint64_t parse_number(std::string_view text, std::string_view option) {
    std::uint64_t value = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid value for " +
                                    std::string(option));
    }
    return value;
}

double parse_positive_real(std::string_view text,
                           std::string_view option) {
    const auto is_digit = [](char character) {
        return character >= '0' && character <= '9';
    };
    std::size_t cursor = 0;
    if (cursor < text.size() &&
        (text[cursor] == '+' || text[cursor] == '-')) {
        ++cursor;
    }
    const std::size_t integer_start = cursor;
    while (cursor < text.size() && is_digit(text[cursor])) {
        ++cursor;
    }
    const bool has_integer_digits = cursor != integer_start;
    bool has_fraction_digits = false;
    if (cursor < text.size() && text[cursor] == '.') {
        ++cursor;
        const std::size_t fraction_start = cursor;
        while (cursor < text.size() && is_digit(text[cursor])) {
            ++cursor;
        }
        has_fraction_digits = cursor != fraction_start;
    }
    if (!has_integer_digits && !has_fraction_digits) {
        cursor = text.size() + 1;
    }
    if (cursor < text.size() &&
        (text[cursor] == 'e' || text[cursor] == 'E')) {
        ++cursor;
        if (cursor < text.size() &&
            (text[cursor] == '+' || text[cursor] == '-')) {
            ++cursor;
        }
        const std::size_t exponent_start = cursor;
        while (cursor < text.size() && is_digit(text[cursor])) {
            ++cursor;
        }
        if (cursor == exponent_start) {
            cursor = text.size() + 1;
        }
    }
    const std::string owned(text);
    char* parsed_end = nullptr;
    const double value = cursor == text.size()
                             ? std::strtod(owned.c_str(), &parsed_end)
                             : 0.0;
    if (cursor != text.size() ||
        parsed_end != owned.c_str() + owned.size() ||
        !std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(
            std::string(option) +
            " must be a positive finite number");
    }
    return value;
}

std::string format_real(double value) {
    for (int precision = 1;
         precision <=
         std::numeric_limits<double>::max_digits10;
         ++precision) {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(precision) << value;
        const std::string candidate = output.str();
        char* parsed_end = nullptr;
        const double reparsed =
            std::strtod(candidate.c_str(), &parsed_end);
        if (parsed_end ==
                candidate.c_str() + candidate.size() &&
            reparsed == value) {
            return candidate;
        }
    }
    throw std::runtime_error("failed to format real number");
}

void print_help(std::string_view executable) {
    std::cout
        << "Usage: " << executable
        << " [--games N] [--seed N] [--bots MODE] [--rollouts N]"
           " [--deep-rollouts N] [--train-games N] [--train-seed N]\n"
        << "       " << executable
        << " --interactive [--seed N] [--train-games N]"
           " [--train-seed N]\n"
        << "       " << executable
        << " --benchmark [--games N] [--challenger BOT]"
           " [--baseline BOT] [--actor-policy-epochs N]"
           " [--actor-policy-rate X]"
           " [--refresh-value-g8-cache]"
           " [--refresh-value-mix50-cache]\n"
        << "       " << executable
        << " --stability [--stability-runs N] [--games N]\n\n"
        << "       " << executable
        << " --diagnose-white-plan [--seed N] [--train-games N]\n"
        << "       " << executable
        << " --variance-study [--games N] [--train-games N]\n"
        << "       " << executable
        << " --score-probes [--probe-worlds N] [--probe-horizon N]"
           " [--actor-generation 0|1] [--value-generation 0|8]"
           " [--value-recipe canonical|mix50]"
           " [--probe-cache PATH]"
           " [--refresh-probe-cache]"
           " [--refresh-value-g8-cache]"
           " [--refresh-value-mix50-cache]"
           " [--actor-policy-epochs N] [--actor-policy-rate X]\n"
        << "       " << executable
        << " --evolve-deck [--generations N] [--population N] "
           "[--games N]\n\n"
        << "Simulates an Old School Magic round robin with legal bot play.\n"
        << "  Green: 18 Forest, 9 Grizzly Bears, 8 Ironroot Treefolk, "
           "4 Giant Growth, 1 Tsunami\n"
        << "  Red: 18 Mountain, 10 Lightning Bolt, 12 Fire Elemental\n"
        << "  Blue: 18 Island, 14 Counterspell, 8 Water Elemental\n"
        << "  White: 22 Plains, 3 Millstone, 15 Moat\n"
        << "  RU Aggro: 13 Mountain, 4 Island, 3 Flying Men, "
           "5 Ironclaw Orcs, 2 Gray Ogre, 8 Hill Giant, "
           "3 Lightning Bolt, 2 Disintegrate\n\n"
        << "Options:\n"
        << "  --games N       Games per matchup (default: 100)\n"
        << "  --seed N        Reproducible random seed (default: random)\n"
        << "  --bots MODE     mixed, random, monte-carlo, "
           "deep-monte-carlo, handcrafted, learned-value, or "
           "learned-actor (default: mixed)\n"
        << "  --rollouts N    Monte Carlo continuations per legal action "
           "(default: 2)\n"
        << "  --deep-rollouts N  Deep Monte Carlo continuations per "
           "legal action (default: 8)\n"
        << "  --train-games N  Training games for the selected learned "
           "model "
           "(default: 800)\n"
        << "  --train-seed N   Learned model seed, independent of --seed "
           "(default: 424242)\n"
        << "  --interactive   Play one RU Aggro mirror against frozen "
           "Learned Value\n"
        << "  --benchmark     Run the paired bot-strength harness\n"
        << "  --challenger BOT  Benchmark challenger "
           "(default: handcrafted; learned generations: "
           "learned-value-g0..g8, learned-value-mix50-g8, "
           "learned-actor-g0/g1)\n"
        << "  --baseline BOT    Benchmark baseline "
           "(default: monte-carlo)\n"
        << "  --stability     Validate Learned against all policies across "
           "seed panels\n"
        << "  --stability-runs N  Number of independent runs "
           "(default: 8)\n"
        << "  --diagnose-white-plan  Compare K=2 and K=64 Learned root "
           "rankings on a held-out White lock state\n"
        << "  --variance-study  Run fixed 3x3 training/evaluation seed "
           "study (default: 5 games)\n"
        << "  --score-probes   Label/score the held-out 20-position "
           "probe-dev-v3 development corpus\n"
        << "  --probe-worlds N  Common worlds per reference candidate "
           "(default: 128; minimum: 2)\n"
        << "  --probe-horizon N  Actor-mirror reference horizon in turns "
           "(default: 12)\n"
        << "  --actor-generation N  Score frozen Actor G0 or G1 "
           "(default: 0; requires --score-probes)\n"
        << "  --value-generation N  Score frozen Value G0 or G8 "
           "(default: 0; requires --score-probes)\n"
        << "  --value-recipe NAME  Select canonical or mix50 Value G8 "
           "checkpoints (default: canonical; requires --score-probes)\n"
        << "  --actor-policy-epochs N  G1 policy-fit epochs "
           "(default: 2; requires a selected Actor G1)\n"
        << "  --actor-policy-rate X  G1 policy-fit learning rate "
           "(default: 0.001; requires a selected Actor G1)\n"
        << "  --probe-cache PATH  Deterministic label cache "
           "(default: data/old-school-probe-dev-v3.labels.tsv)\n"
        << "  --refresh-probe-cache  Regenerate matching probe labels "
           "atomically\n"
        << "  --refresh-value-g8-cache  Retrain and atomically replace "
           "the selected canonical Value G8 artifact\n"
        << "  --refresh-value-mix50-cache  Retrain and atomically replace "
           "the selected Value G8 Late-Mix50 artifact\n"
        << "  --evolve-deck   Evolve a 40-card deck against the current "
           "metagame\n"
        << "  --generations N  Evolution generations (default: 10)\n"
        << "  --population N   Candidate decks per generation "
           "(default: 16)\n"
        << "  With --evolve-deck, --games is paired repetitions per "
           "metagame deck (default: 4).\n"
        << "  --help          Show this help\n";
}

struct BotSelection {
    old_school::BotKind kind = old_school::BotKind::Random;
    old_school::LearnedVariant learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    old_school::LearnedValueG8Recipe value_recipe =
        old_school::LearnedValueG8Recipe::CanonicalAllSearchLate;
    std::size_t value_generation = 0;
    std::size_t actor_generation = 0;
};

BotSelection parse_bot(std::string_view value) {
    if (value == "random") {
        return {.kind = old_school::BotKind::Random};
    }
    if (value == "monte-carlo" || value == "mc") {
        return {.kind = old_school::BotKind::MonteCarlo};
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return {.kind = old_school::BotKind::DeepMonteCarlo};
    }
    if (value == "handcrafted" || value == "handcoded" ||
        value == "strategic") {
        return {.kind = old_school::BotKind::Handcrafted};
    }
    if (value == "learned" || value == "learned-value" ||
        value == "learned-value-g0") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_generation = 0,
        };
    }
    constexpr std::string_view value_generation_prefix =
        "learned-value-g";
    if (value.starts_with(value_generation_prefix) &&
        value.size() == value_generation_prefix.size() + 1 &&
        value.back() >= '0' && value.back() <= '8') {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_generation =
                static_cast<std::size_t>(value.back() - '0'),
        };
    }
    if (value == "learned-value-mix50-g8") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
            .value_recipe =
                old_school::LearnedValueG8Recipe::LateMix50,
            .value_generation = 8,
        };
    }
    if (value == "learned-actor" || value == "actor" ||
        value == "learned-actor-g0") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .actor_generation = 0,
        };
    }
    if (value == "learned-actor-g1") {
        return {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
            .actor_generation = 1,
        };
    }
    throw std::invalid_argument("invalid bot name: " +
                                std::string(value));
}

struct BotFieldSelection {
    old_school::BotField field = old_school::BotField::Mixed;
    old_school::LearnedVariant learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
};

BotFieldSelection parse_bot_field(std::string_view value) {
    if (value == "mixed") {
        return {.field = old_school::BotField::Mixed};
    }
    if (value == "random") {
        return {.field = old_school::BotField::Random};
    }
    if (value == "monte-carlo" || value == "mc") {
        return {.field = old_school::BotField::MonteCarlo};
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return {.field = old_school::BotField::DeepMonteCarlo};
    }
    if (value == "handcrafted" || value == "handcoded" ||
        value == "strategic") {
        return {.field = old_school::BotField::Handcrafted};
    }
    if (value == "learned" || value == "learned-value") {
        return {
            .field = old_school::BotField::Learned,
            .learned_variant =
                old_school::LearnedVariant::ValueSearchChampion,
        };
    }
    if (value == "learned-actor" || value == "actor") {
        return {
            .field = old_school::BotField::Learned,
            .learned_variant =
                old_school::LearnedVariant::UnifiedActor,
        };
    }
    throw std::invalid_argument(
        "invalid value for --bots (use mixed, random, monte-carlo, "
        "deep-monte-carlo, handcrafted, learned-value, or "
        "learned-actor)");
}

std::string_view bot_field_name(
    old_school::BotField field,
    old_school::LearnedVariant learned_variant) {
    switch (field) {
    case old_school::BotField::Random:
        return "random only";
    case old_school::BotField::MonteCarlo:
        return "Monte Carlo only";
    case old_school::BotField::DeepMonteCarlo:
        return "Deep Monte Carlo only";
    case old_school::BotField::Handcrafted:
        return "Handcrafted Policy only";
    case old_school::BotField::Learned:
        return learned_variant ==
                       old_school::LearnedVariant::UnifiedActor
                   ? "Learned Unified Actor only"
                   : "Learned Value Search Champion only";
    case old_school::BotField::Mixed:
        return "mixed Random, Monte Carlo, Deep Monte Carlo, "
               "Handcrafted Policy, and Learned Value Search Champion";
    }
    return "unknown";
}

void print_deck_stats(std::string_view label,
                      const old_school::DeckSimulationStats& stats) {
    std::cout << label << '\n'
              << "  Record: " << stats.wins << '-' << stats.losses << '-'
              << stats.draws << " (" << stats.win_rate() << "% wins)\n"
              << "  On the play: " << stats.on_play_wins << '/'
              << stats.on_play_games << " wins ("
              << stats.on_play_win_rate() << "%)\n"
              << "  On the draw: " << stats.on_draw_wins << '/'
              << stats.on_draw_games << " wins ("
              << stats.on_draw_win_rate() << "%)\n"
              << "  Per game: " << stats.average_ending_life()
              << " ending life, " << stats.average_cards_drawn()
              << " cards drawn, " << stats.average_lands_played()
              << " lands played, " << stats.average_spells_cast()
              << " spells cast, " << stats.average_spells_countered()
              << " spells countered, "
              << stats.average_damage_to_opponent()
              << " damage to opponent, " << stats.average_cards_milled()
              << " cards milled\n";
}

void print_bot_stats(std::string_view label,
                     const old_school::BotSimulationStats& stats) {
    std::cout << label << '\n'
              << "  Record: " << stats.wins << '-' << stats.losses << '-'
              << stats.draws << " across " << stats.games
              << " seat-games (" << stats.win_rate() << "% wins)\n"
              << "  Per seat-game: " << stats.average_decisions()
              << " nontrivial priority decisions, "
              << stats.average_rollouts()
              << " rollout continuations\n"
              << "  Rollouts per decision: "
              << stats.average_rollouts_per_decision() << '\n';
}

void print_delta(double delta) {
    if (delta >= 0.0) {
        std::cout << '+';
    }
    std::cout << delta << " pp";
}

void print_deck_bot_benefit(const old_school::TournamentSummary& result) {
    struct Benefit {
        old_school::DeckId deck;
        double ranking_delta;
    };

    const auto random_index =
        static_cast<std::size_t>(old_school::BotKind::Random);
    const auto monte_carlo_index =
        static_cast<std::size_t>(old_school::BotKind::MonteCarlo);
    const auto deep_index =
        static_cast<std::size_t>(old_school::BotKind::DeepMonteCarlo);
    const auto handcrafted_index =
        static_cast<std::size_t>(old_school::BotKind::Handcrafted);
    const auto learned_index =
        static_cast<std::size_t>(old_school::BotKind::Learned);
    const bool has_deep_comparison =
        result.bots[random_index].games > 0 &&
        result.bots[deep_index].games > 0;
    const bool has_learned_comparison =
        result.bots[random_index].games > 0 &&
        result.bots[learned_index].games > 0;
    const bool has_monte_carlo_comparison =
        result.bots[random_index].games > 0 &&
        result.bots[monte_carlo_index].games > 0;
    const auto learned_lift =
        old_school::compare_learned_deck_lifts(result);

    std::cout << "\nBot benefit by deck\n";
    if (!has_learned_comparison && !has_deep_comparison &&
        !has_monte_carlo_comparison) {
        std::cout << "  Not available; use --bots mixed to compare "
                     "policies.\n";
        return;
    }

    std::vector<Benefit> benefits;
    for (std::size_t deck = 0; deck < result.deck_bots.size(); ++deck) {
        const auto& random = result.deck_bots[deck][random_index];
        const auto& comparison =
            has_learned_comparison
                ? result.deck_bots[deck][learned_index]
                : (has_deep_comparison
                       ? result.deck_bots[deck][deep_index]
                       : result.deck_bots[deck][monte_carlo_index]);
        if (random.games == 0 || comparison.games == 0) {
            continue;
        }
        benefits.push_back({
            .deck = static_cast<old_school::DeckId>(deck),
            .ranking_delta =
                comparison.win_rate() - random.win_rate(),
        });
    }
    std::sort(benefits.begin(), benefits.end(),
              [](const Benefit& left, const Benefit& right) {
                  return left.ranking_delta > right.ranking_delta;
              });

    std::cout << "  Ranked by "
              << (has_learned_comparison
                      ? "Learned Value"
                      : (has_deep_comparison ? "Deep Monte Carlo"
                                             : "Monte Carlo"))
              << " win-rate lift over Random:\n";
    for (std::size_t rank = 0; rank < benefits.size(); ++rank) {
        const auto deck_index =
            static_cast<std::size_t>(benefits[rank].deck);
        const auto& random = result.deck_bots[deck_index][random_index];
        std::cout << "  " << rank + 1 << ". "
                  << old_school::deck_name(benefits[rank].deck)
                  << ": Random " << random.win_rate() << "% ("
                  << random.games << " games)";

        const auto& monte_carlo =
            result.deck_bots[deck_index][monte_carlo_index];
        if (monte_carlo.games > 0) {
            std::cout << ", Monte Carlo " << monte_carlo.win_rate()
                      << "% (";
            print_delta(monte_carlo.win_rate() - random.win_rate());
            std::cout << ", " << monte_carlo.games << " games)";
        }

        const auto& deep = result.deck_bots[deck_index][deep_index];
        if (deep.games > 0) {
            std::cout << ", Deep " << deep.win_rate() << "% (";
            print_delta(deep.win_rate() - random.win_rate());
            std::cout << ", " << deep.games << " games)";
        }

        const auto& handcrafted =
            result.deck_bots[deck_index][handcrafted_index];
        if (handcrafted.games > 0) {
            std::cout << ", Handcrafted " << handcrafted.win_rate()
                      << "% (";
            print_delta(handcrafted.win_rate() - random.win_rate());
            std::cout << ", " << handcrafted.games << " games)";
        }

        const auto& learned =
            result.deck_bots[deck_index][learned_index];
        if (learned.games > 0) {
            std::cout << ", Learned " << learned.win_rate() << "% (";
            print_delta(learned.win_rate() - random.win_rate());
            std::cout << ", " << learned.games << " games)";
        }
        const auto& lift = learned_lift.decks[deck_index];
        if (lift.available) {
            std::cout << " [Learned lift "
                      << (lift.learned_is_best ? "PASS" : "FAIL")
                      << ']';
        }
        std::cout << '\n';
    }
    if (has_learned_comparison) {
        std::cout << "  Learned per-deck lift gate (all five decks): "
                  << (learned_lift.learned_is_best_on_every_deck()
                          ? "PASS"
                          : "FAIL");
        if (!learned_lift.complete()) {
            std::cout << " (all five policies need samples)";
        }
        std::cout << '\n';
    }
}

old_school::BotConfig bot_config(BotSelection selection,
                            std::size_t rollouts,
                            std::size_t deep_rollouts,
                            std::size_t training_games) {
    old_school::BotConfig config;
    config.kind = selection.kind;
    config.learned_variant = selection.learned_variant;
    config.training_games = training_games;
    switch (selection.kind) {
    case old_school::BotKind::Random:
    case old_school::BotKind::Handcrafted:
        config.rollouts_per_action = 1;
        return config;
    case old_school::BotKind::Learned:
        config.rollouts_per_action = 2;
        return config;
    case old_school::BotKind::MonteCarlo:
        config.rollouts_per_action = rollouts;
        return config;
    case old_school::BotKind::DeepMonteCarlo:
        config.rollouts_per_action = deep_rollouts;
        return config;
    }
    throw std::invalid_argument("unknown bot kind");
}

old_school::BotConfig bot_config(old_school::BotKind kind,
                            std::size_t rollouts,
                            std::size_t deep_rollouts,
                            std::size_t training_games) {
    return bot_config(
        {.kind = kind,
         .learned_variant =
             old_school::LearnedVariant::ValueSearchChampion},
        rollouts, deep_rollouts, training_games);
}

std::shared_ptr<const old_school::LearnedModel>
train_frozen_learned_model(old_school::LearnedVariant variant,
                           std::size_t training_games,
                           std::uint64_t training_seed) {
    if (variant == old_school::LearnedVariant::UnifiedActor) {
        return old_school::train_learned_actor_model(
            training_games, training_seed);
    }
    return old_school::train_learned_value_champion(
        training_games, training_seed);
}

std::shared_ptr<const old_school::LearnedModel>
train_value_g0_with_progress(std::size_t training_games,
                             std::uint64_t training_seed) {
    std::cout << "Training frozen Value G0 (seed " << training_seed
              << ", " << training_games << " games)..."
              << std::flush;
    const auto started = std::chrono::steady_clock::now();
    auto model = old_school::train_learned_value_champion(
        training_games, training_seed);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Value G0 fingerprint: "
              << old_school::learned_model_fingerprint(model) << '\n';
    return model;
}

old_school::LearnedValueG8Result
train_value_g8_with_progress(std::size_t training_games,
                             std::uint64_t training_seed,
                             bool refresh_cache) {
    const std::string cache_path =
        old_school::learned_value_g8_cache_path(
            training_games, training_seed);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect Value G8 artifact cache '" +
            cache_path + "': " + exists_error.message());
    }

    old_school::LearnedValueG8Result result;
    const auto started = std::chrono::steady_clock::now();
    if (cache_exists && !refresh_cache) {
        std::cout << "Loading immutable Value G8 artifact (seed "
                  << training_seed << ", " << training_games
                  << " initial games) from " << cache_path
                  << "..." << std::flush;
        try {
            result = old_school::load_learned_value_g8_bundle(
                cache_path, training_games, training_seed);
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Value G8 artifact cache '" + cache_path +
                "' is invalid: " + error.what() +
                "; rerun this G8 route with "
                "--refresh-value-g8-cache to regenerate it");
        }
    } else {
        std::cout << "Training immutable Value G8 (seed "
                  << training_seed << ", " << training_games
                  << " initial games, 8 generations)..."
                  << std::flush;
        result = old_school::train_learned_value_g8(
            training_games, training_seed);
        old_school::write_learned_value_g8_bundle_atomic(
            cache_path, result);
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Value G8 artifact cache: "
              << (cache_exists && !refresh_cache
                      ? "loaded "
                      : "generated ")
              << cache_path << '\n';
    const auto& report = result.report;
    std::cout << "  G8 base: " << report.base_examples
              << " anchor examples; fingerprint "
              << report.base_fingerprint << '\n';
    for (const auto& generation : report.generations) {
        std::cout
            << "  G8 generation " << generation.generation << ": "
            << generation.self_play_games << " games, "
            << generation.generation_examples << " new + "
            << generation.anchor_examples << " anchor, replay "
            << generation.replay_generations << " shards/"
            << generation.replay_examples << " examples, "
            << (generation.search_enabled ? "search K=" : "raw Value");
        if (generation.search_enabled) {
            std::cout << generation.search_worlds << "/H="
                      << generation.search_horizon_turns;
        }
        std::cout << ", " << generation.rollout_evaluations
                  << " rollout evaluations, exploration "
                  << format_real(generation.exploration_rate)
                  << ", fingerprints "
                  << generation.parent_fingerprint << " -> "
                  << generation.candidate_fingerprint << '\n';
    }
    std::cout << "  Value G8 final fingerprint: "
              << report.final_fingerprint << '\n';
    return result;
}

old_school::LearnedValueG8Result
train_value_g8_mix50_with_progress(std::size_t training_games,
                                   std::uint64_t training_seed,
                                   bool refresh_cache) {
    const std::string cache_path =
        old_school::learned_value_g8_mix50_cache_path(
            training_games, training_seed);
    std::error_code exists_error;
    const bool cache_exists =
        std::filesystem::exists(cache_path, exists_error);
    if (exists_error) {
        throw std::runtime_error(
            "cannot inspect Value G8 Late-Mix50 artifact cache '" +
            cache_path + "': " + exists_error.message());
    }

    old_school::LearnedValueG8Result result;
    const auto started = std::chrono::steady_clock::now();
    if (cache_exists && !refresh_cache) {
        std::cout
            << "Loading immutable Value G8 Late-Mix50 artifact (seed "
            << training_seed << ", " << training_games
            << " initial games) from " << cache_path
            << "..." << std::flush;
        try {
            result = old_school::load_learned_value_g8_mix50_bundle(
                cache_path, training_games, training_seed);
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "Value G8 Late-Mix50 artifact cache '" +
                cache_path + "' is invalid: " + error.what() +
                "; rerun this Mix50 route with "
                "--refresh-value-mix50-cache to regenerate it");
        }
    } else {
        std::cout
            << "Training immutable Value G8 Late-Mix50 (seed "
            << training_seed << ", " << training_games
            << " initial games, 8 generations)..."
            << std::flush;
        result = old_school::train_learned_value_g8_mix50(
            training_games, training_seed);
        old_school::write_learned_value_g8_mix50_bundle_atomic(
            cache_path, result);
    }
    if (result.report.recipe !=
        old_school::LearnedValueG8Recipe::LateMix50) {
        throw std::runtime_error(
            "Value G8 Late-Mix50 route returned a canonical recipe");
    }
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout
        << " done (" << std::fixed << std::setprecision(2)
        << elapsed.count() << "s)\n"
        << "  Value G8 Late-Mix50 artifact cache: "
        << (cache_exists && !refresh_cache
                ? "loaded "
                : "generated ")
        << cache_path << '\n';
    const auto& report = result.report;
    std::cout << "  Mix50 G8 base: " << report.base_examples
              << " anchor examples; fingerprint "
              << report.base_fingerprint << '\n';
    for (const auto& generation : report.generations) {
        std::cout
            << "  Mix50 G8 generation "
            << generation.generation << ": "
            << generation.self_play_games << " games, "
            << generation.generation_examples << " new + "
            << generation.anchor_examples << " anchor, replay "
            << generation.replay_generations << " shards/"
            << generation.replay_examples << " examples, collection "
            << generation.raw_collection_games << " raw games/"
            << generation.raw_collection_examples
            << " raw examples + "
            << generation.search_collection_games
            << " search games/"
            << generation.search_collection_examples
            << " search examples, "
            << (generation.search_enabled
                    ? "search present K="
                    : "raw Value only");
        if (generation.search_enabled) {
            std::cout << generation.search_worlds << "/H="
                      << generation.search_horizon_turns;
        }
        std::cout << ", " << generation.rollout_evaluations
                  << " rollout evaluations, exploration "
                  << format_real(generation.exploration_rate)
                  << ", fingerprints "
                  << generation.parent_fingerprint << " -> "
                  << generation.candidate_fingerprint << '\n';
    }
    std::cout << "  Value G8 Late-Mix50 final fingerprint: "
              << report.final_fingerprint << '\n';
    return result;
}

std::shared_ptr<const old_school::LearnedModel>
train_actor_g0_with_progress(std::size_t training_games,
                             std::uint64_t training_seed) {
    std::cout << "Training frozen Actor G0 (seed " << training_seed
              << ", " << training_games << " games)..."
              << std::flush;
    const auto started = std::chrono::steady_clock::now();
    auto model = old_school::train_learned_actor_model(
        training_games, training_seed);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  G0 fingerprint: "
              << old_school::learned_model_fingerprint(model) << '\n';
    return model;
}

old_school::LearnedActorGenerationResult
train_actor_g1_with_progress(
    std::shared_ptr<const old_school::LearnedModel> actor_g0,
    std::uint64_t training_seed,
    old_school::LearnedActorGenerationConfig config) {
    std::cout << "Training Actor G1 from frozen G0 "
                 "(24 balanced games, K="
              << config.search_worlds << "/H="
              << config.horizon_turns << ", cap="
              << config.max_roots_per_seat_kind
              << "/seat/kind, policy epochs="
              << config.policy_epochs << ", rate="
              << format_real(config.policy_learning_rate) << ")..."
              << std::flush;
    const auto started = std::chrono::steady_clock::now();
    auto result = old_school::train_learned_actor_generation(
        std::move(actor_g0), training_seed, config);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    const auto& report = result.report;
    const auto print_policy_fit =
        [](std::string_view label,
           const old_school::LearnedPolicyFitDiagnostics& fit) {
            std::cout
                << "  " << label << " fit: "
                << fit.example_count << " examples, weight "
                << std::setprecision(2) << fit.total_weight
                << ", teacher top-1 "
                << 100.0 *
                       fit.parent_expected_top_one_agreement
                << "% -> "
                << 100.0 *
                       fit.candidate_expected_top_one_agreement
                << "%, entropy "
                << std::setprecision(6)
                << fit.weighted_teacher_entropy
                << ", cross-entropy "
                << std::setprecision(6)
                << fit.parent_weighted_cross_entropy << " -> "
                << fit.candidate_weighted_cross_entropy
                << " (excess "
                << fit.parent_excess_cross_entropy << " -> "
                << fit.candidate_excess_cross_entropy << ')'
                << ", changed argmax "
                << fit.changed_argmax_examples << " (weight "
                << fit.changed_argmax_weight << ", "
                << 100.0 *
                       fit.changed_argmax_weight_fraction
                << "%)\n";
        };
    std::cout << " done (" << std::fixed << std::setprecision(2)
              << elapsed.count() << "s)\n"
              << "  Search roots: Priority " << report.priority_roots
              << ", Attack " << report.attack_roots
              << "; evaluations: Priority "
              << report.priority_rollout_evaluations << ", Attack "
              << report.attack_rollout_evaluations << '\n'
              << "  Training examples: critic "
              << report.critic_examples << ", Priority "
              << report.priority_policy_examples << ", Attack "
              << report.attack_policy_examples << "; replay generations "
              << report.replay_generations << '\n';
    print_policy_fit("Priority", report.fit.priority);
    print_policy_fit("Attack", report.fit.attack);
    std::cout
        << "  Critic TD fit: " << report.fit.critic.example_count
        << " examples, target mean/variance "
        << std::setprecision(6)
        << report.fit.critic.target_mean << '/'
        << report.fit.critic.target_variance
        << ", MSE "
        << report.fit.critic.parent_mean_squared_error << " -> "
        << report.fit.critic.candidate_mean_squared_error
        << ", BCE "
        << report.fit.critic.parent_binary_cross_entropy << " -> "
        << report.fit.critic.candidate_binary_cross_entropy << '\n'
        << "  G1 fingerprint: "
        << report.candidate_fingerprint << '\n';
    return result;
}

std::string white_plan_action_name(
    const old_school::PriorityAction& action) {
    switch (action.kind) {
    case old_school::PriorityActionKind::Pass:
        return "Pass";
    case old_school::PriorityActionKind::PlayLand:
        return "Play " +
               std::string(old_school::card_definition(action.card).name);
    case old_school::PriorityActionKind::CastCreature:
    case old_school::PriorityActionKind::CastSorcery:
    case old_school::PriorityActionKind::CastArtifact:
    case old_school::PriorityActionKind::CastEnchantment:
        return "Cast " +
               std::string(old_school::card_definition(action.card).name);
    case old_school::PriorityActionKind::CastLightningBolt:
        return "Cast Lightning Bolt";
    case old_school::PriorityActionKind::CastDisintegrate:
        return "Cast Disintegrate for X=" +
               std::to_string(action.x_value);
    case old_school::PriorityActionKind::CastGiantGrowth:
        return "Cast Giant Growth";
    case old_school::PriorityActionKind::CastCounterspell:
        return "Cast Counterspell";
    case old_school::PriorityActionKind::ActivateMillstone:
        if (action.target.has_value() &&
            action.target->player == 1) {
            return "Activate Millstone targeting opponent";
        }
        return "Activate Millstone targeting self";
    }
    throw std::logic_error("unknown diagnostic priority action");
}

void print_white_plan_diagnostic(
    const old_school::WhitePlanTeacherDiagnostic& result,
    std::uint64_t evaluation_seed, std::uint64_t training_seed,
    std::size_t training_games) {
    const auto annotated_name =
        [&](std::size_t index) {
            std::string name =
                white_plan_action_name(result.actions.at(index).action);
            if (result.opponent_millstone_action == index) {
                name += " [opponent-mill plan]";
            }
            if (result.redundant_moat_action == index) {
                name += " [redundant-Moat plan]";
            }
            return name;
        };

    std::cout << std::fixed << std::setprecision(6)
              << "White Lock-Plan Teacher Diagnostic\n"
              << "Evaluation seed: " << evaluation_seed << '\n'
              << "Training seed: " << training_seed << '\n'
              << "Model: "
              << old_school::learned_variant_name(
                     old_school::LearnedVariant::UnifiedActor)
              << '\n'
              << "Learned training games: " << training_games << '\n'
              << "Fixture: White first main, four untapped Plains, "
                 "land played, one Moat, one untapped Millstone, "
                 "redundant Moat in hand\n"
              << "Reference worlds: " << result.reference_worlds
              << "\nTwo-world trials: " << result.two_world_trials
              << "\n\nReference scores\n";
    for (std::size_t index = 0; index < result.actions.size();
         ++index) {
        const auto& action = result.actions[index];
        const double first_place_rate =
            result.two_world_trials == 0
                ? 0.0
                : 100.0 *
                      static_cast<double>(
                          action.two_world_first_place_count) /
                      static_cast<double>(
                          result.two_world_trials);
        std::cout << "  " << annotated_name(index) << ": "
                  << action.reference_score << "; K=2 first "
                  << action.two_world_first_place_count << '/'
                  << result.two_world_trials << " ("
                  << std::setprecision(1) << first_place_rate
                  << "%)\n"
                  << std::setprecision(6);
    }

    std::cout << "\nReference best: "
              << annotated_name(result.reference_best_action)
              << "\nK=2 reference agreement: "
              << result.two_world_reference_agreements << '/'
              << result.two_world_trials << " ("
              << std::setprecision(1)
              << result.two_world_reference_agreement_rate()
              << "%)\n";

    const std::size_t mill =
        result.opponent_millstone_action.value();
    const std::size_t moat = result.redundant_moat_action.value();
    const double mill_score =
        result.actions[mill].reference_score;
    const double moat_score =
        result.actions[moat].reference_score;
    std::cout << std::setprecision(6)
              << "\nPlan comparison\n"
              << "  Opponent Millstone activation: "
              << mill_score << '\n'
              << "  Redundant Moat cast: " << moat_score << '\n'
              << "  K=64 preference: ";
    if (mill_score > moat_score) {
        std::cout << "opponent Millstone activation\n";
    } else if (moat_score > mill_score) {
        std::cout << "redundant Moat\n";
    } else {
        std::cout << "tie\n";
    }
    std::cout << "  K=2 plan rankings: Millstone "
              << result.two_world_millstone_preferences
              << ", Moat " << result.two_world_moat_preferences
              << ", ties " << result.two_world_plan_ties << '\n'
              << "  K=2 plan-order agreement with K=64: "
              << result.two_world_plan_order_agreements << '/'
              << result.two_world_trials << " ("
              << std::setprecision(1)
              << result.two_world_plan_order_agreement_rate()
              << "%)\n"
              << std::setprecision(6);
    std::cout << "  K=2 reference-best agreement below 80%: "
              << (result.two_world_reference_agreement_rate() < 80.0
                      ? "yes"
                      : "no")
              << '\n';
}

void print_benchmark(const old_school::BotBenchmarkSummary& result,
                     std::uint64_t evaluation_seed,
                     std::string challenger_name = {},
                     std::string baseline_name = {}) {
    if (challenger_name.empty()) {
        challenger_name =
            old_school::bot_config_name(result.challenger);
    }
    if (baseline_name.empty()) {
        baseline_name =
            old_school::bot_config_name(result.baseline);
    }
    std::cout << std::fixed << std::setprecision(1)
              << "Old School Magic Bot Benchmark\n"
              << "Evaluation seed: " << evaluation_seed << '\n'
              << "Training seed: "
              << result.learned_training_seed << '\n'
              << "Challenger: "
              << challenger_name << '\n'
              << "Baseline: "
              << baseline_name
              << '\n';
    if (result.challenger.kind == old_school::BotKind::Learned) {
        std::cout << "Challenger frozen model: "
                  << challenger_name
                  << ", seed " << result.learned_training_seed
                  << ", " << result.challenger.training_games
                  << " training games\n";
    }
    if (result.baseline.kind == old_school::BotKind::Learned) {
        std::cout << "Baseline frozen model: "
                  << baseline_name
                  << ", seed " << result.learned_training_seed
                  << ", " << result.baseline.training_games
                  << " training games\n";
    }
    std::cout
              << "Repetitions per unordered deck pairing: "
              << result.repetitions_per_deck_pairing << '\n'
              << "Total paired games: " << result.total_games
              << "\n\nOverall\n"
              << "  Challenger record: "
              << result.challenger_stats.wins << '-'
              << result.challenger_stats.losses << '-'
              << result.challenger_stats.draws << " ("
              << result.challenger_win_rate() << "% wins)\n"
              << "  Approximate 95% confidence interval: "
              << result.confidence_low_95() << "% to "
              << result.confidence_high_95() << "%\n"
              << "  Verdict: ";
    if (result.challenger_is_better_95()) {
        std::cout
            << "PASS — aggregate lower bound exceeds 50% and "
               "challenger wins on all five decks\n";
    } else if (result.confidence_low_95() > 50.0) {
        std::cout
            << "FAIL — aggregate confidence passes, but challenger "
               "does not win on every deck\n";
    } else if (result.confidence_high_95() < 50.0) {
        std::cout << "FAIL — baseline is better at 95% confidence\n";
    } else {
        std::cout << "INCONCLUSIVE — run more repetitions\n";
    }

    std::cout << "\nEfficiency\n"
              << "  Challenger: "
              << result.challenger_stats.average_decisions()
              << " decisions/game, "
              << result.challenger_stats.average_rollouts()
              << " rollouts/game\n"
              << "  Baseline: "
              << result.baseline_stats.average_decisions()
              << " decisions/game, "
              << result.baseline_stats.average_rollouts()
              << " rollouts/game\n"
              << "\nBy challenger deck\n";
    for (std::size_t deck = 0;
         deck < result.challenger_decks.size(); ++deck) {
        const auto id = static_cast<old_school::DeckId>(deck);
        const auto& challenger = result.challenger_decks[deck];
        const auto& baseline = result.baseline_decks[deck];
        std::cout << "  " << old_school::deck_name(id) << ": challenger "
                  << challenger.win_rate() << "% (" << challenger.wins
                  << '-' << challenger.losses << '-' << challenger.draws
                  << "), baseline " << baseline.win_rate() << "% ("
                  << baseline.wins << '-' << baseline.losses << '-'
                  << baseline.draws << ")\n"
                  << "    Average spells/damage/milled/life: challenger "
                  << challenger.average_spells_cast() << '/'
                  << challenger.average_damage_to_opponent() << '/'
                  << challenger.average_cards_milled() << '/'
                  << challenger.average_ending_life() << ", baseline "
                  << baseline.average_spells_cast() << '/'
                  << baseline.average_damage_to_opponent() << '/'
                  << baseline.average_cards_milled() << '/'
                  << baseline.average_ending_life() << '\n';
    }
}

bool run_stability_panel(std::size_t runs,
                         std::size_t repetitions_per_deck_pairing,
                         std::uint64_t base_seed,
                         std::uint64_t training_seed,
                         std::size_t rollouts,
                         std::size_t deep_rollouts,
                         std::size_t training_games) {
    constexpr std::array<old_school::BotKind, 4> baseline_kinds = {
        old_school::BotKind::Random,
        old_school::BotKind::MonteCarlo,
        old_school::BotKind::DeepMonteCarlo,
        old_school::BotKind::Handcrafted,
    };
    old_school::BotConfig learned_config =
        bot_config(old_school::BotKind::Learned, rollouts,
                   deep_rollouts, training_games);
    std::array<old_school::BotBenchmarkSummary, baseline_kinds.size()>
        pooled;
    std::array<std::size_t, baseline_kinds.size()> seed_wins{};
    for (std::size_t baseline = 0; baseline < pooled.size();
         ++baseline) {
        pooled[baseline].challenger = learned_config;
        pooled[baseline].baseline =
            bot_config(baseline_kinds[baseline], rollouts,
                       deep_rollouts, training_games);
        pooled[baseline].learned_training_seed =
            training_seed;
    }
    const auto merge_bot = [](old_school::BotSimulationStats& destination,
                              const old_school::BotSimulationStats& source) {
        destination.games += source.games;
        destination.wins += source.wins;
        destination.losses += source.losses;
        destination.draws += source.draws;
        destination.total_decisions += source.total_decisions;
        destination.total_rollouts += source.total_rollouts;
    };
    const auto merge_deck = [](old_school::DeckSimulationStats& destination,
                               const old_school::DeckSimulationStats& source) {
        destination.games += source.games;
        destination.wins += source.wins;
        destination.losses += source.losses;
        destination.draws += source.draws;
        destination.on_play_games += source.on_play_games;
        destination.on_play_wins += source.on_play_wins;
        destination.on_draw_games += source.on_draw_games;
        destination.on_draw_wins += source.on_draw_wins;
    };
    constexpr std::size_t mixed_policy_matrix_games =
        old_school::kBotKindCount * old_school::kBotKindCount;
    const std::size_t mixed_target_games =
        20 * repetitions_per_deck_pairing;
    const std::size_t mixed_games_per_matchup =
        ((mixed_target_games + mixed_policy_matrix_games - 1) /
         mixed_policy_matrix_games) *
        mixed_policy_matrix_games;
    old_school::TournamentConfig mixed_config;
    mixed_config.bot_field = old_school::BotField::Mixed;
    mixed_config.monte_carlo_rollouts = rollouts;
    mixed_config.deep_monte_carlo_rollouts = deep_rollouts;
    mixed_config.learned_training_games = training_games;
    mixed_config.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    old_school::TournamentSummary pooled_mixed;
    std::size_t mixed_seed_lift_passes = 0;
    std::size_t all_policy_seed_wins = 0;

    std::cout << std::fixed << std::setprecision(1)
              << "Learned Value All-Policy Stability Panel\n"
              << "Runs: " << runs << '\n'
              << "Evaluation base seed: " << base_seed << '\n'
              << "Training seed: " << training_seed << '\n'
              << "Repetitions per unordered deck pairing per run: "
              << repetitions_per_deck_pairing
              << '\n'
              << "Mixed-field games per deck pairing per run: "
              << mixed_games_per_matchup << '\n'
              << "Training games for fixed model: "
              << training_games
              << "\nTraining fixed model..." << std::flush;

    old_school::GameConfig shared_config;
    shared_config.learned_training_seed = training_seed;
    shared_config.learned_model =
        old_school::train_learned_value_champion(
            training_games, training_seed);
    learned_config.learned_model = shared_config.learned_model;
    for (auto& result : pooled) {
        result.challenger.learned_model =
            shared_config.learned_model;
    }
    std::cout << " done\n\n";

    for (std::size_t run = 0; run < runs; ++run) {
        const std::uint64_t evaluation_seed =
            base_seed + 101ULL * static_cast<std::uint64_t>(run + 1);
        bool seed_pass = true;
        std::cout << "  Evaluation seed " << evaluation_seed
                  << ":\n";
        for (std::size_t baseline = 0;
             baseline < baseline_kinds.size(); ++baseline) {
            const auto result = old_school::run_bot_benchmark(
                repetitions_per_deck_pairing, evaluation_seed,
                learned_config, pooled[baseline].baseline,
                shared_config);
            const bool learned_won =
                result.challenger_stats.wins >
                result.baseline_stats.wins;
            seed_wins[baseline] += learned_won ? 1 : 0;
            seed_pass = seed_pass && learned_won;
            pooled[baseline].total_games += result.total_games;
            merge_bot(pooled[baseline].challenger_stats,
                      result.challenger_stats);
            merge_bot(pooled[baseline].baseline_stats,
                      result.baseline_stats);
            for (std::size_t deck = 0;
                 deck < result.challenger_decks.size(); ++deck) {
                merge_deck(
                    pooled[baseline].challenger_decks[deck],
                    result.challenger_decks[deck]);
                merge_deck(pooled[baseline].baseline_decks[deck],
                           result.baseline_decks[deck]);
            }
            std::cout << "    vs "
                      << old_school::bot_name(baseline_kinds[baseline])
                      << ": " << result.challenger_stats.wins << '-'
                      << result.baseline_stats.wins << '-'
                      << result.challenger_stats.draws << " ("
                      << result.challenger_win_rate() << "%)"
                      << (learned_won ? " PASS\n" : " FAIL\n");
        }
        const auto mixed = old_school::run_tournament(
            mixed_games_per_matchup, evaluation_seed,
            shared_config,
            mixed_config);
        const auto seed_lift =
            old_school::compare_learned_deck_lifts(mixed);
        const bool seed_lift_pass =
            seed_lift.learned_is_best_on_every_deck();
        mixed_seed_lift_passes += seed_lift_pass ? 1 : 0;
        std::cout << "    mixed-field lift:";
        for (const auto& deck : seed_lift.decks) {
            std::cout << ' ' << old_school::deck_name(deck.deck) << '='
                      << (deck.available
                              ? (deck.learned_is_best ? "PASS"
                                                     : "FAIL")
                              : "N/A");
        }
        std::cout << " => "
                  << (seed_lift_pass ? "PASS\n" : "FAIL\n");
        for (std::size_t deck = 0;
             deck < mixed.deck_bots.size(); ++deck) {
            for (std::size_t bot = 0;
                 bot < mixed.deck_bots[deck].size(); ++bot) {
                merge_deck(pooled_mixed.deck_bots[deck][bot],
                           mixed.deck_bots[deck][bot]);
            }
        }
        all_policy_seed_wins += seed_pass ? 1 : 0;
    }

    bool every_policy_passed = true;
    std::cout << "\nPooled results\n";
    for (std::size_t baseline = 0;
         baseline < baseline_kinds.size(); ++baseline) {
        const auto& result = pooled[baseline];
        const bool confidence_pass =
            result.challenger_is_better_95();
        bool every_deck_won = true;
        std::cout << "  vs "
                  << old_school::bot_name(baseline_kinds[baseline])
                  << ": " << result.challenger_stats.wins << '-'
                  << result.baseline_stats.wins << '-'
                  << result.challenger_stats.draws << " ("
                  << result.challenger_win_rate()
                  << "%, 95% interval "
                  << result.confidence_low_95() << "% to "
                  << result.confidence_high_95() << "%)\n";
        for (std::size_t deck = 0;
             deck < result.challenger_decks.size(); ++deck) {
            const auto id = static_cast<old_school::DeckId>(deck);
            const auto& learned =
                result.challenger_decks[deck];
            const auto& other = result.baseline_decks[deck];
            const bool deck_won = learned.wins > other.wins;
            every_deck_won = every_deck_won && deck_won;
            std::cout << "    " << old_school::deck_name(id) << ": "
                      << learned.wins << " vs " << other.wins
                      << (deck_won ? " PASS\n" : " FAIL\n");
        }
        const bool policy_pass =
            seed_wins[baseline] == runs && confidence_pass &&
            every_deck_won;
        every_policy_passed =
            every_policy_passed && policy_pass;
        std::cout << "    Seeds " << seed_wins[baseline] << '/'
                  << runs << ", confidence "
                  << (confidence_pass ? "PASS" : "FAIL")
                  << ", decks "
                  << (every_deck_won ? "PASS" : "FAIL")
                  << " => " << (policy_pass ? "PASS" : "FAIL")
                  << '\n';
    }
    const auto pooled_lift =
        old_school::compare_learned_deck_lifts(pooled_mixed);
    const bool mixed_lift_pass =
        pooled_lift.learned_is_best_on_every_deck();
    std::cout << "\nPooled mixed-field lift over Random\n";
    for (const auto& deck : pooled_lift.decks) {
        std::cout << "  " << old_school::deck_name(deck.deck) << ": ";
        if (!deck.available) {
            std::cout << "N/A FAIL\n";
            continue;
        }
        std::cout << "Learned ";
        print_delta(deck.learned_lift);
        std::cout << ", best other "
                  << old_school::bot_name(deck.best_other) << ' ';
        print_delta(deck.best_other_lift);
        std::cout << ' '
                  << (deck.learned_is_best ? "PASS\n" : "FAIL\n");
    }
    std::cout << "  Mixed-field seeds: "
              << mixed_seed_lift_passes << '/' << runs
              << "\n  Per-deck pooled lift gate: "
              << (mixed_lift_pass ? "PASS" : "FAIL") << '\n';
    const bool passed =
        all_policy_seed_wins == runs && every_policy_passed &&
        mixed_lift_pass;
    std::cout << "\nAll-policy seed verdict: "
              << all_policy_seed_wins << '/' << runs
              << "\nOverall: " << (passed ? "PASS" : "FAIL")
              << '\n';
    return passed;
}

void run_variance_study(
    std::size_t repetitions_per_deck_pairing,
    std::size_t training_games, std::size_t rollouts,
    std::size_t deep_rollouts) {
    constexpr std::array<std::uint64_t, 3> seeds = {
        424242,
        101,
        707,
    };
    auto learned = bot_config(
        old_school::BotKind::Learned, rollouts,
        deep_rollouts, training_games);
    const auto handcrafted = bot_config(
        old_school::BotKind::Handcrafted, rollouts,
        deep_rollouts, training_games);
    std::array<std::array<double, seeds.size()>, seeds.size()>
        win_rates{};

    std::cout << std::fixed << std::setprecision(1)
              << "Learned Training/Evaluation Seed Variance Study\n"
              << "Challenger: "
              << old_school::bot_config_name(learned) << '\n'
              << "Baseline: "
              << old_school::bot_name(old_school::BotKind::Handcrafted)
              << '\n'
              << "Training seeds: 424242, 101, 707\n"
              << "Evaluation seeds: 424242, 101, 707\n"
              << "Training games per model: " << training_games
              << '\n'
              << "Paired repetitions per cell: "
              << repetitions_per_deck_pairing << "\n\n";

    for (std::size_t training = 0; training < seeds.size();
         ++training) {
        old_school::GameConfig shared_config;
        shared_config.learned_training_seed = seeds[training];
        std::cout << "Training seed " << seeds[training]
                  << "..." << std::flush;
        shared_config.learned_model =
            old_school::train_learned_value_champion(
                training_games, seeds[training]);
        learned.learned_model = shared_config.learned_model;
        std::cout << " done\n";
        for (std::size_t evaluation = 0;
             evaluation < seeds.size(); ++evaluation) {
            const auto result = old_school::run_bot_benchmark(
                repetitions_per_deck_pairing,
                seeds[evaluation], learned, handcrafted,
                shared_config);
            win_rates[training][evaluation] =
                result.challenger_win_rate();
            std::cout << "  Evaluation seed "
                      << seeds[evaluation] << ": "
                      << win_rates[training][evaluation]
                      << "%\n"
                      << std::flush;
        }
    }

    std::cout << "\nWin-rate matrix (%)\n"
              << "  train/eval";
    for (const auto evaluation_seed : seeds) {
        std::cout << std::setw(12) << evaluation_seed;
    }
    std::cout << '\n';
    std::array<double, seeds.size()> row_spans{};
    std::array<double, seeds.size()> column_spans{};
    for (std::size_t training = 0; training < seeds.size();
         ++training) {
        std::cout << "  " << std::setw(12) << seeds[training];
        for (const double rate : win_rates[training]) {
            std::cout << std::setw(12) << rate;
        }
        const auto [minimum, maximum] = std::minmax_element(
            win_rates[training].begin(),
            win_rates[training].end());
        row_spans[training] = *maximum - *minimum;
        std::cout << '\n';
    }

    std::cout << "\nRow ranges (evaluation variance at fixed model)\n";
    for (std::size_t training = 0; training < seeds.size();
         ++training) {
        const auto [minimum, maximum] = std::minmax_element(
            win_rates[training].begin(),
            win_rates[training].end());
        std::cout << "  Training seed " << seeds[training]
                  << ": " << *minimum << "% to " << *maximum
                  << "% (span " << row_spans[training]
                  << " pp)\n";
    }

    std::cout
        << "\nColumn ranges (training variance at fixed evaluation)\n";
    for (std::size_t evaluation = 0;
         evaluation < seeds.size(); ++evaluation) {
        std::array<double, seeds.size()> column{};
        for (std::size_t training = 0;
             training < seeds.size(); ++training) {
            column[training] =
                win_rates[training][evaluation];
        }
        const auto [minimum, maximum] =
            std::minmax_element(column.begin(), column.end());
        column_spans[evaluation] = *maximum - *minimum;
        std::cout << "  Evaluation seed " << seeds[evaluation]
                  << ": " << *minimum << "% to " << *maximum
                  << "% (span " << column_spans[evaluation]
                  << " pp)\n";
    }

    const auto mean =
        [](const std::array<double, 3>& values) {
            double total = 0.0;
            for (const double value : values) {
                total += value;
            }
            return total /
                   static_cast<double>(values.size());
        };
    const double mean_row_span = mean(row_spans);
    const double mean_column_span = mean(column_spans);
    std::cout << "\nMean row span: " << mean_row_span
              << " pp\nMean column span: "
              << mean_column_span
              << " pp\nTraining variance hypothesis: "
              << (mean_column_span > mean_row_span
                      ? "SUPPORTED"
                      : "NOT SUPPORTED")
              << '\n';
}

void print_evolution(const old_school::DeckEvolutionSummary& result,
                     std::uint64_t seed) {
    std::cout << std::fixed << std::setprecision(1)
              << "Old School Magic Deck Evolution\n"
              << "Seed: " << seed << "\n\nGeneration best fitness\n";
    for (std::size_t generation = 0;
         generation < result.generation_best_win_rates.size();
         ++generation) {
        std::cout << "  " << generation + 1 << ": "
                  << result.generation_best_win_rates[generation]
                  << "%\n";
    }

    std::array<std::size_t, old_school::kCardCount> counts{};
    for (const old_school::CardId card : result.best.cards) {
        ++counts[static_cast<std::size_t>(card)];
    }
    std::cout << "\nBest 40-card deck\n";
    for (std::size_t card = 0; card < counts.size(); ++card) {
        if (counts[card] == 0) {
            continue;
        }
        const auto id = static_cast<old_school::CardId>(card);
        std::cout << "  " << counts[card] << " "
                  << old_school::card_definition(id).name << '\n';
    }
    std::cout << "\nFitness: " << result.best.total.win_rate()
              << "% (" << result.best.total.wins << '-'
              << result.best.total.losses << '-'
              << result.best.total.draws << ")\n"
              << "By metagame opponent\n";
    for (std::size_t opponent = 0;
         opponent < result.best.by_opponent.size(); ++opponent) {
        const auto id = static_cast<old_school::DeckId>(opponent);
        const auto& stats = result.best.by_opponent[opponent];
        std::cout << "  " << old_school::deck_name(id) << ": "
                  << stats.win_rate() << "% (" << stats.wins << '-'
                  << stats.losses << '-' << stats.draws << ")\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::size_t games = 100;
        std::uint64_t seed = random_seed();
        old_school::BotField bot_field = old_school::BotField::Mixed;
        old_school::LearnedVariant bot_field_learned_variant =
            old_school::LearnedVariant::ValueSearchChampion;
        std::size_t rollouts = 2;
        std::size_t deep_rollouts = 8;
        std::size_t training_games = 800;
        std::uint64_t training_seed =
            old_school::kDefaultLearnedTrainingSeed;
        bool games_were_set = false;
        bool interactive = false;
        bool interactive_unsupported_option_used = false;
        bool benchmark = false;
        bool stability = false;
        bool evolve = false;
        bool diagnose_white_plan = false;
        bool variance_study = false;
        bool score_probes = false;
        bool refresh_probe_cache = false;
        bool refresh_value_g8_cache = false;
        bool refresh_value_mix50_cache = false;
        bool probe_option_used = false;
        bool actor_policy_option_used = false;
        std::size_t probe_worlds = 128;
        std::size_t probe_horizon = 12;
        std::size_t actor_generation = 0;
        std::size_t value_generation = 0;
        old_school::LearnedValueG8Recipe value_recipe =
            old_school::LearnedValueG8Recipe::CanonicalAllSearchLate;
        old_school::LearnedActorGenerationConfig
            actor_generation_config;
        std::string probe_cache =
            "data/old-school-probe-dev-v3.labels.tsv";
        std::size_t stability_runs = 8;
        std::size_t generations = 10;
        std::size_t population = 16;
        BotSelection challenger = {
            .kind = old_school::BotKind::Handcrafted,
        };
        BotSelection baseline = {
            .kind = old_school::BotKind::MonteCarlo,
        };

        for (int argument = 1; argument < argc; ++argument) {
            const std::string_view option = argv[argument];
            if (option == "--help" || option == "-h") {
                print_help(argv[0]);
                return 0;
            }
            if (option == "--benchmark") {
                benchmark = true;
                continue;
            }
            if (option == "--interactive") {
                interactive = true;
                continue;
            }
            if (option == "--stability") {
                stability = true;
                continue;
            }
            if (option == "--evolve-deck") {
                evolve = true;
                continue;
            }
            if (option == "--diagnose-white-plan") {
                diagnose_white_plan = true;
                continue;
            }
            if (option == "--variance-study") {
                variance_study = true;
                continue;
            }
            if (option == "--score-probes") {
                score_probes = true;
                continue;
            }
            if (option == "--refresh-probe-cache") {
                refresh_probe_cache = true;
                probe_option_used = true;
                continue;
            }
            if (option == "--refresh-value-g8-cache") {
                refresh_value_g8_cache = true;
                continue;
            }
            if (option == "--refresh-value-mix50-cache") {
                refresh_value_mix50_cache = true;
                continue;
            }
            if (option != "--games" && option != "--seed" &&
                option != "--train-seed" &&
                option != "--bots" && option != "--rollouts" &&
                option != "--deep-rollouts" &&
                option != "--train-games" &&
                option != "--stability-runs" &&
                option != "--generations" &&
                option != "--population" &&
                option != "--challenger" &&
                option != "--baseline" &&
                option != "--probe-worlds" &&
                option != "--probe-horizon" &&
                option != "--actor-generation" &&
                option != "--value-generation" &&
                option != "--value-recipe" &&
                option != "--actor-policy-epochs" &&
                option != "--actor-policy-rate" &&
                option != "--probe-cache") {
                throw std::invalid_argument("unknown option: " +
                                            std::string(option));
            }
            if (++argument >= argc) {
                throw std::invalid_argument("missing value for " +
                                            std::string(option));
            }
            if (option != "--seed" &&
                option != "--train-seed" &&
                option != "--train-games") {
                interactive_unsupported_option_used = true;
            }
            if (option == "--bots") {
                const auto selection =
                    parse_bot_field(argv[argument]);
                bot_field = selection.field;
                bot_field_learned_variant =
                    selection.learned_variant;
                continue;
            }
            if (option == "--challenger" ||
                option == "--baseline") {
                const auto selection =
                    parse_bot(argv[argument]);
                if (option == "--challenger") {
                    challenger = selection;
                } else {
                    baseline = selection;
                }
                continue;
            }
            if (option == "--probe-cache") {
                probe_cache = argv[argument];
                if (probe_cache.empty()) {
                    throw std::invalid_argument(
                        "--probe-cache must not be empty");
                }
                probe_option_used = true;
                continue;
            }
            if (option == "--value-recipe") {
                const std::string_view recipe = argv[argument];
                if (recipe == "canonical") {
                    value_recipe =
                        old_school::LearnedValueG8Recipe::
                            CanonicalAllSearchLate;
                } else if (recipe == "mix50") {
                    value_recipe =
                        old_school::LearnedValueG8Recipe::LateMix50;
                } else {
                    throw std::invalid_argument(
                        "--value-recipe must be canonical or mix50");
                }
                probe_option_used = true;
                continue;
            }
            if (option == "--actor-policy-rate") {
                actor_generation_config.policy_learning_rate =
                    parse_positive_real(argv[argument], option);
                actor_policy_option_used = true;
                continue;
            }

            const std::uint64_t value = parse_number(argv[argument], option);
            if (option == "--games") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--games must be greater than zero");
                }
                games = static_cast<std::size_t>(value);
                games_were_set = true;
            } else if (option == "--seed") {
                seed = value;
            } else if (option == "--train-seed") {
                training_seed = value;
            } else if (option == "--rollouts") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--rollouts must be greater than zero");
                }
                rollouts = static_cast<std::size_t>(value);
            } else if (option == "--deep-rollouts") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--deep-rollouts must be greater than zero");
                }
                deep_rollouts = static_cast<std::size_t>(value);
            } else if (option == "--stability-runs") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--stability-runs must be greater than zero");
                }
                stability_runs = static_cast<std::size_t>(value);
            } else if (option == "--generations") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--generations must be greater than zero");
                }
                generations = static_cast<std::size_t>(value);
            } else if (option == "--population") {
                if (value < static_cast<long long>(
                                old_school::kDeckCount)) {
                    throw std::invalid_argument(
                        "--population must be at least five");
                }
                population = static_cast<std::size_t>(value);
            } else if (option == "--probe-worlds") {
                if (value < 2) {
                    throw std::invalid_argument(
                        "--probe-worlds must be at least two");
                }
                probe_worlds = static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--probe-horizon") {
                probe_horizon = static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--actor-generation") {
                if (value > 1) {
                    throw std::invalid_argument(
                        "--actor-generation must be zero or one");
                }
                actor_generation =
                    static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--value-generation") {
                if (value != 0 && value != 8) {
                    throw std::invalid_argument(
                        "--value-generation must be zero or eight");
                }
                value_generation =
                    static_cast<std::size_t>(value);
                probe_option_used = true;
            } else if (option == "--actor-policy-epochs") {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--actor-policy-epochs must be greater "
                        "than zero");
                }
                actor_generation_config.policy_epochs =
                    static_cast<std::size_t>(value);
                actor_policy_option_used = true;
            } else {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--train-games must be greater than zero");
                }
                training_games = static_cast<std::size_t>(value);
            }
        }

        if (static_cast<int>(interactive) +
                static_cast<int>(benchmark) +
                static_cast<int>(stability) +
                static_cast<int>(evolve) +
                static_cast<int>(diagnose_white_plan) +
                static_cast<int>(variance_study) +
                static_cast<int>(score_probes) >
            1) {
            throw std::invalid_argument(
                "--interactive, --benchmark, --stability, "
                "--evolve-deck, and "
                "--diagnose-white-plan, --variance-study, and "
                "--score-probes cannot be combined");
        }
        if (interactive &&
            interactive_unsupported_option_used) {
            throw std::invalid_argument(
                "--interactive only accepts --seed, --train-seed, "
                "and --train-games");
        }
        if (probe_option_used && !score_probes) {
            throw std::invalid_argument(
                "--probe-worlds, --probe-horizon, --actor-generation, "
                "--value-generation, --value-recipe, --probe-cache, and "
                "--refresh-probe-cache require --score-probes");
        }
        if (score_probes &&
            value_recipe ==
                old_school::LearnedValueG8Recipe::LateMix50 &&
            value_generation != 8) {
            throw std::invalid_argument(
                "--value-recipe mix50 requires "
                "--value-generation 8");
        }
        const auto selects_actor_g1 =
            [](const BotSelection& selection) {
                return selection.kind == old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::UnifiedActor &&
                       selection.actor_generation == 1;
            };
        const bool actor_g1_will_be_trained =
            (score_probes && actor_generation == 1) ||
            (benchmark &&
             (selects_actor_g1(challenger) ||
              selects_actor_g1(baseline)));
        if (actor_policy_option_used &&
            !actor_g1_will_be_trained) {
            throw std::invalid_argument(
                "--actor-policy-epochs and --actor-policy-rate "
                "require a selected Actor G1");
        }
        const auto selects_canonical_value_bundle_checkpoint =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_recipe ==
                           old_school::LearnedValueG8Recipe::
                               CanonicalAllSearchLate &&
                       selection.value_generation > 0;
            };
        const auto selects_mix50_value_bundle =
            [](const BotSelection& selection) {
                return selection.kind ==
                           old_school::BotKind::Learned &&
                       selection.learned_variant ==
                           old_school::LearnedVariant::
                               ValueSearchChampion &&
                       selection.value_recipe ==
                           old_school::LearnedValueG8Recipe::
                               LateMix50 &&
                       selection.value_generation == 8;
            };
        const bool value_g8_will_be_used =
            (score_probes && value_generation == 8 &&
             value_recipe ==
                 old_school::LearnedValueG8Recipe::
                     CanonicalAllSearchLate) ||
            (benchmark &&
             (selects_canonical_value_bundle_checkpoint(
                  challenger) ||
              selects_canonical_value_bundle_checkpoint(
                  baseline)));
        const bool value_mix50_will_be_used =
            (score_probes && value_generation == 8 &&
             value_recipe ==
                 old_school::LearnedValueG8Recipe::LateMix50) ||
            (benchmark &&
             (selects_mix50_value_bundle(challenger) ||
              selects_mix50_value_bundle(baseline)));
        if (refresh_value_g8_cache &&
            !value_g8_will_be_used) {
            throw std::invalid_argument(
                "--refresh-value-g8-cache requires a benchmark "
                "or probe route that selects canonical Value G8");
        }
        if (refresh_value_mix50_cache &&
            !value_mix50_will_be_used) {
            throw std::invalid_argument(
                "--refresh-value-mix50-cache requires a benchmark "
                "or probe route that selects Value G8 Late-Mix50");
        }
        if (interactive) {
            std::cout
                << "Old School Magic Interactive\n"
                << "Match: Human RU Aggro vs Learned Value RU Aggro\n"
                << "Game seed: " << seed << '\n'
                << "Training seed: " << training_seed << '\n'
                << "Type q at any prompt to abandon the game.\n"
                << "MVP timing note: there is no priority window after "
                   "attackers or blockers are declared.\n";
            const auto learned_model =
                train_value_g0_with_progress(
                    training_games, training_seed);
            old_school::run_interactive_match(
                std::cin, std::cout, seed, learned_model);
            return 0;
        }
        if (score_probes) {
            const old_school::probe_runner::ProbeScoreConfig config{
                .training_games = training_games,
                .training_seed = training_seed,
                .reference_worlds = probe_worlds,
                .reference_horizon_turns = probe_horizon,
                .reference_rollouts_per_world = 1,
                .cache_path = probe_cache,
                .refresh_cache = refresh_probe_cache,
            };
            const auto actor_g0 =
                train_actor_g0_with_progress(
                    training_games, training_seed);
            auto scoring_actor = actor_g0;
            if (actor_generation == 1) {
                scoring_actor =
                    train_actor_g1_with_progress(
                        actor_g0, training_seed,
                        actor_generation_config)
                        .model;
            }
            const auto value_g0 =
                train_value_g0_with_progress(
                    training_games, training_seed);
            std::vector<
                old_school::probe_runner::NamedValueScoringModel>
                scoring_value_models;
            if (value_generation == 8) {
                const bool mix50 =
                    value_recipe ==
                    old_school::LearnedValueG8Recipe::LateMix50;
                const auto value_g8 =
                    mix50
                        ? train_value_g8_mix50_with_progress(
                              training_games, training_seed,
                              refresh_value_mix50_cache)
                        : train_value_g8_with_progress(
                              training_games, training_seed,
                              refresh_value_g8_cache);
                if (value_g8.checkpoints.size() !=
                    old_school::kLearnedValueG8Generations + 1) {
                    throw std::runtime_error(
                        std::string(
                            mix50 ? "Value G8 Late-Mix50"
                                  : "Value G8") +
                        " trainer did not retain base-G8");
                }
                scoring_value_models.reserve(9);
                scoring_value_models.push_back(
                    {
                        .name =
                            mix50 ? "Value Mix50 base"
                                  : "Value G8 base",
                        .model = value_g8.checkpoints.front(),
                    });
                for (std::size_t generation = 1;
                     generation < value_g8.checkpoints.size();
                     ++generation) {
                    scoring_value_models.push_back(
                        {
                            .name =
                                std::string(
                                    mix50 ? "Value Mix50 G"
                                          : "Value G") +
                                std::to_string(generation),
                            .model =
                                value_g8.checkpoints[generation],
                        });
                }
            }
            const auto report =
                old_school::probe_runner::
                    score_probe_dev_with_candidates(
                        config, std::cout,
                        {
                            .reference_actor_model = actor_g0,
                            .scoring_actor_model = scoring_actor,
                            .scoring_actor_name =
                                actor_generation == 0
                                    ? "Actor G0"
                                    : "Actor G1",
                            .reference_value_model = value_g0,
                            .reference_value_name = "Value G0",
                            .scoring_value_models =
                                std::move(scoring_value_models),
                        });
            std::cout
                << old_school::probe_runner::format_probe_score_report(
                       report);
            return 0;
        }
        if (diagnose_white_plan) {
            const auto model =
                old_school::train_learned_actor_model(
                    training_games, training_seed);
            const auto result =
                old_school::diagnose_white_lock_plan_teacher(model, seed);
            print_white_plan_diagnostic(
                result, seed, training_seed, training_games);
            return 0;
        }
        if (benchmark) {
            auto challenger_config =
                bot_config(challenger, rollouts, deep_rollouts,
                           training_games);
            auto baseline_config =
                bot_config(baseline, rollouts, deep_rollouts,
                           training_games);
            old_school::GameConfig shared_config;
            shared_config.learned_training_seed = training_seed;
            std::shared_ptr<const old_school::LearnedModel>
                frozen_value_g0;
            old_school::LearnedValueG8Result frozen_value_bundle;
            old_school::LearnedValueG8Result
                frozen_value_mix50_bundle;
            std::shared_ptr<const old_school::LearnedModel>
                frozen_actor_g0;
            std::shared_ptr<const old_school::LearnedModel>
                frozen_actor_g1;
            const auto resolve_frozen_model =
                [&](const BotSelection& selection)
                -> std::shared_ptr<const old_school::LearnedModel> {
                if (selection.learned_variant ==
                    old_school::LearnedVariant::
                        ValueSearchChampion) {
                    if (selection.value_recipe ==
                        old_school::LearnedValueG8Recipe::
                            LateMix50) {
                        if (selection.value_generation != 8) {
                            throw std::logic_error(
                                "Value G8 Late-Mix50 supports only "
                                "generation eight");
                        }
                        if (frozen_value_mix50_bundle
                                .checkpoints.empty()) {
                            frozen_value_mix50_bundle =
                                train_value_g8_mix50_with_progress(
                                    training_games,
                                    training_seed,
                                    refresh_value_mix50_cache);
                        }
                        const auto checkpoint =
                            old_school::
                                learned_value_g8_generation_checkpoint(
                                    frozen_value_mix50_bundle,
                                    selection.value_generation);
                        std::cout
                            << "  Selected Value Mix50 G8 "
                               "fingerprint: "
                            << old_school::learned_model_fingerprint(
                                   checkpoint)
                            << '\n';
                        return checkpoint;
                    }
                    if (selection.value_generation == 0) {
                        if (!frozen_value_g0) {
                            frozen_value_g0 =
                                train_value_g0_with_progress(
                                    training_games, training_seed);
                        }
                        return frozen_value_g0;
                    }
                    if (selection.value_generation >
                        old_school::kLearnedValueG8Generations) {
                        throw std::logic_error(
                            "unsupported Value generation");
                    }
                    if (frozen_value_bundle.checkpoints.empty()) {
                        frozen_value_bundle =
                            train_value_g8_with_progress(
                                training_games, training_seed,
                                refresh_value_g8_cache);
                    }
                    const auto checkpoint =
                        old_school::
                            learned_value_g8_generation_checkpoint(
                                frozen_value_bundle,
                                selection.value_generation);
                    std::cout
                        << "  Selected Value G"
                        << selection.value_generation
                        << " fingerprint: "
                        << old_school::learned_model_fingerprint(
                               checkpoint)
                        << '\n';
                    return checkpoint;
                }
                if (!frozen_actor_g0) {
                    frozen_actor_g0 =
                        train_actor_g0_with_progress(
                            training_games, training_seed);
                }
                if (selection.actor_generation == 0) {
                    return frozen_actor_g0;
                }
                if (!frozen_actor_g1) {
                    frozen_actor_g1 =
                        train_actor_g1_with_progress(
                            frozen_actor_g0, training_seed,
                            actor_generation_config)
                            .model;
                }
                return frozen_actor_g1;
            };
            const auto selections_share_model =
                [](const BotSelection& left,
                   const BotSelection& right) {
                    if (left.learned_variant !=
                        right.learned_variant) {
                        return false;
                    }
                    if (left.learned_variant ==
                        old_school::LearnedVariant::UnifiedActor) {
                        return left.actor_generation ==
                               right.actor_generation;
                    }
                    return left.value_recipe ==
                               right.value_recipe &&
                           left.value_generation ==
                           right.value_generation;
                };
            if (challenger_config.kind ==
                old_school::BotKind::Learned) {
                challenger_config.learned_model =
                    resolve_frozen_model(challenger);
                shared_config.learned_model =
                    challenger_config.learned_model;
            }
            if (baseline_config.kind ==
                old_school::BotKind::Learned) {
                if (challenger_config.kind ==
                        old_school::BotKind::Learned &&
                    selections_share_model(challenger, baseline)) {
                    baseline_config.learned_model =
                        challenger_config.learned_model;
                } else {
                    baseline_config.learned_model =
                        resolve_frozen_model(baseline);
                }
                if (!shared_config.learned_model) {
                    shared_config.learned_model =
                        baseline_config.learned_model;
                }
            }
            const auto result = old_school::run_bot_benchmark(
                games, seed, challenger_config,
                baseline_config, shared_config);
            const auto benchmark_name =
                [](const BotSelection& selection,
                   const old_school::BotConfig& config) {
                    if (selection.kind ==
                            old_school::BotKind::Learned) {
                        if (selection.learned_variant ==
                            old_school::LearnedVariant::
                                UnifiedActor) {
                            return std::string(
                                       "Learned Actor G") +
                                   std::to_string(
                                       selection.actor_generation);
                        }
                        if (selection.value_recipe ==
                            old_school::LearnedValueG8Recipe::
                                LateMix50) {
                            return std::string(
                                "Learned Value Mix50 G8");
                        }
                        return std::string("Learned Value G") +
                               std::to_string(
                                   selection.value_generation);
                    }
                    return old_school::bot_config_name(config);
                };
            print_benchmark(
                result, seed,
                benchmark_name(challenger, challenger_config),
                benchmark_name(baseline, baseline_config));
            return result.challenger_is_better_95() ? 0 : 1;
        }
        if (stability) {
            return run_stability_panel(
                       stability_runs, games, seed,
                       training_seed, rollouts, deep_rollouts,
                       training_games)
                       ? 0
                       : 1;
        }
        if (variance_study) {
            run_variance_study(
                games_were_set ? games : 5, training_games,
                rollouts, deep_rollouts);
            return 0;
        }
        if (evolve) {
            const std::size_t repetitions =
                games_were_set ? games : 4;
            old_school::GameConfig evolution_game_config;
            evolution_game_config.learned_training_seed =
                training_seed;
            const auto result = old_school::evolve_deck(
                {
                    .generations = generations,
                    .population = population,
                    .repetitions_per_opponent = repetitions,
                    .pilot =
                        {
                            .kind = old_school::BotKind::Handcrafted,
                            .rollouts_per_action = 1,
                        },
                },
                seed, evolution_game_config);
            print_evolution(result, seed);
            return 0;
        }

        old_school::GameConfig tournament_game_config;
        tournament_game_config.learned_training_seed =
            training_seed;
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::Learned) {
            const auto model_variant =
                bot_field == old_school::BotField::Mixed
                    ? old_school::LearnedVariant::
                          ValueSearchChampion
                    : bot_field_learned_variant;
            tournament_game_config.learned_model =
                train_frozen_learned_model(
                    model_variant, training_games,
                    training_seed);
        }
        old_school::TournamentConfig tournament_config;
        tournament_config.bot_field = bot_field;
        tournament_config.monte_carlo_rollouts = rollouts;
        tournament_config.deep_monte_carlo_rollouts =
            deep_rollouts;
        tournament_config.learned_training_games =
            training_games;
        tournament_config.learned_variant =
            bot_field == old_school::BotField::Mixed
                ? old_school::LearnedVariant::ValueSearchChampion
                : bot_field_learned_variant;
        const old_school::TournamentSummary result =
            old_school::run_tournament(
                games, seed, tournament_game_config,
                tournament_config);

        std::cout << std::fixed << std::setprecision(1)
                  << "Old School Magic Bot Simulator\n"
                  << "Evaluation seed: " << seed << '\n'
                  << "Training seed: " << training_seed << '\n'
                  << "Bot field: "
                  << bot_field_name(
                         bot_field,
                         bot_field_learned_variant)
                  << '\n';
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::MonteCarlo) {
            std::cout << "Monte Carlo rollouts per legal action: "
                      << rollouts << '\n';
        }
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::DeepMonteCarlo) {
            std::cout
                << "Deep Monte Carlo rollouts per legal action: "
                << deep_rollouts << '\n';
        }
        if (bot_field == old_school::BotField::Mixed ||
            bot_field == old_school::BotField::Learned) {
            const auto model_variant =
                bot_field == old_school::BotField::Mixed
                    ? old_school::LearnedVariant::
                          ValueSearchChampion
                    : bot_field_learned_variant;
            std::cout << "Frozen learned model: "
                      << old_school::learned_variant_name(
                             model_variant)
                      << ", seed " << training_seed << ", "
                      << training_games << " training games\n";
        }
        std::cout
                  << "Games per matchup: " << result.games_per_matchup
                  << '\n'
                  << "Total games: " << result.total_games
                  << "\n\nMatchups\n";

        for (const auto& matchup : result.matchups) {
            const auto& first = matchup.result.decks[0];
            const auto& second = matchup.result.decks[1];
            std::cout << "  " << old_school::deck_name(matchup.first_deck)
                      << " vs "
                      << old_school::deck_name(matchup.second_deck) << ": "
                      << first.wins << '-' << second.wins << '-'
                      << matchup.result.draws << " ("
                      << first.win_rate() << "% / "
                      << second.win_rate() << "%)\n";
        }

        std::cout << "\nDeck statistics\n";
        for (std::size_t deck = 0; deck < result.decks.size(); ++deck) {
            const auto id = static_cast<old_school::DeckId>(deck);
            const std::string label =
                std::string(old_school::deck_name(id)) + " — " +
                std::string(old_school::deck_list(id));
            print_deck_stats(label, result.decks[deck]);
            if (deck + 1 != result.decks.size()) {
                std::cout << '\n';
            }
        }

        print_deck_bot_benefit(result);

        std::cout << "\nBot statistics\n";
        bool printed_bot = false;
        for (std::size_t bot = 0; bot < result.bots.size(); ++bot) {
            if (result.bots[bot].games == 0) {
                continue;
            }
            if (printed_bot) {
                std::cout << '\n';
            }
            const auto kind = static_cast<old_school::BotKind>(bot);
            if (kind == old_school::BotKind::Learned) {
                const auto learned_display = bot_config(
                    {.kind = old_school::BotKind::Learned,
                     .learned_variant =
                         bot_field == old_school::BotField::Mixed
                             ? old_school::LearnedVariant::
                                   ValueSearchChampion
                             : bot_field_learned_variant},
                    rollouts, deep_rollouts, training_games);
                print_bot_stats(
                    old_school::bot_config_name(learned_display),
                    result.bots[bot]);
            } else {
                print_bot_stats(old_school::bot_name(kind),
                                result.bots[bot]);
            }
            printed_bot = true;
        }
        bool printed_matchup = false;
        for (const auto& matchup : result.bot_matchups) {
            if (matchup.games == 0) {
                continue;
            }
            if (!printed_matchup) {
                std::cout << "\nDirect bot matchups\n";
                printed_matchup = true;
            }
            std::cout << "  " << old_school::bot_name(matchup.first_bot)
                      << " vs " << old_school::bot_name(matchup.second_bot)
                      << ": " << matchup.first_wins << '-'
                      << matchup.second_wins << '-' << matchup.draws
                      << " across " << matchup.games << " games ("
                      << matchup.first_win_rate() << "% / "
                      << matchup.second_win_rate() << "%)\n";
        }

        std::cout << "\nOverall\n"
                  << "  Draws: " << result.draws << '\n'
                  << "  Average individual turns: "
                  << result.average_turns() << '\n'
                  << "  Finishes by life total: "
                  << result.life_total_finishes << '\n'
                  << "  Finishes by empty library: "
                  << result.empty_library_finishes << '\n'
                  << "  Turn-limit draws: " << result.turn_limit_draws
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
