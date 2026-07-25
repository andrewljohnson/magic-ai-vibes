#include "alpha/game.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
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

void print_help(std::string_view executable) {
    std::cout
        << "Usage: " << executable
        << " [--games N] [--seed N] [--bots MODE] [--rollouts N]"
           " [--deep-rollouts N] [--train-games N] [--train-seed N]\n"
        << "       " << executable
        << " --benchmark [--games N] [--challenger BOT]"
           " [--baseline BOT]\n"
        << "       " << executable
        << " --stability [--stability-runs N] [--games N]\n\n"
        << "       " << executable
        << " --diagnose-white-plan [--seed N] [--train-games N]\n"
        << "       " << executable
        << " --variance-study [--games N] [--train-games N]\n"
        << "       " << executable
        << " --evolve-deck [--generations N] [--population N] "
           "[--games N]\n\n"
        << "Simulates an early-Magic round robin with legal bot play.\n"
        << "  Green: 18 Forest, 9 Grizzly Bears, 12 Ironroot Treefolk, "
           "1 Tsunami\n"
        << "  Red: 18 Mountain, 10 Lightning Bolt, 12 Fire Elemental\n"
        << "  Blue: 18 Island, 14 Counterspell, 8 Water Elemental\n"
        << "  White: 22 Plains, 3 Millstone, 15 Moat\n\n"
        << "Options:\n"
        << "  --games N       Games per matchup (default: 100)\n"
        << "  --seed N        Reproducible random seed (default: random)\n"
        << "  --bots MODE     mixed, random, monte-carlo, "
           "deep-monte-carlo, handcrafted, or learned (default: mixed)\n"
        << "  --rollouts N    Monte Carlo continuations per legal action "
           "(default: 2)\n"
        << "  --deep-rollouts N  Deep Monte Carlo continuations per "
           "legal action (default: 8)\n"
        << "  --train-games N  Random-play games for Learned Value "
           "(default: 800)\n"
        << "  --train-seed N   Learned model seed, independent of --seed "
           "(default: 424242)\n"
        << "  --benchmark     Run the paired bot-strength harness\n"
        << "  --challenger BOT  Benchmark challenger "
           "(default: handcrafted)\n"
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
        << "  --evolve-deck   Evolve a 40-card deck against the current "
           "metagame\n"
        << "  --generations N  Evolution generations (default: 10)\n"
        << "  --population N   Candidate decks per generation "
           "(default: 16)\n"
        << "  With --evolve-deck, --games is paired repetitions per "
           "metagame deck (default: 4).\n"
        << "  --help          Show this help\n";
}

alpha::BotKind parse_bot_kind(std::string_view value) {
    if (value == "random") {
        return alpha::BotKind::Random;
    }
    if (value == "monte-carlo" || value == "mc") {
        return alpha::BotKind::MonteCarlo;
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return alpha::BotKind::DeepMonteCarlo;
    }
    if (value == "handcrafted" || value == "handcoded" ||
        value == "strategic") {
        return alpha::BotKind::Handcrafted;
    }
    if (value == "learned" || value == "learned-value") {
        return alpha::BotKind::Learned;
    }
    throw std::invalid_argument("invalid bot name: " +
                                std::string(value));
}

alpha::BotField parse_bot_field(std::string_view value) {
    if (value == "mixed") {
        return alpha::BotField::Mixed;
    }
    if (value == "random") {
        return alpha::BotField::Random;
    }
    if (value == "monte-carlo" || value == "mc") {
        return alpha::BotField::MonteCarlo;
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return alpha::BotField::DeepMonteCarlo;
    }
    if (value == "handcrafted" || value == "handcoded" ||
        value == "strategic") {
        return alpha::BotField::Handcrafted;
    }
    if (value == "learned" || value == "learned-value") {
        return alpha::BotField::Learned;
    }
    throw std::invalid_argument(
        "invalid value for --bots (use mixed, random, monte-carlo, "
        "deep-monte-carlo, handcrafted, or learned)");
}

std::string_view bot_field_name(alpha::BotField field) {
    switch (field) {
    case alpha::BotField::Random:
        return "random only";
    case alpha::BotField::MonteCarlo:
        return "Monte Carlo only";
    case alpha::BotField::DeepMonteCarlo:
        return "Deep Monte Carlo only";
    case alpha::BotField::Handcrafted:
        return "Handcrafted Policy only";
    case alpha::BotField::Learned:
        return "Learned Value only";
    case alpha::BotField::Mixed:
        return "mixed Random, Monte Carlo, Deep Monte Carlo, "
               "Handcrafted Policy, and Learned Value";
    }
    return "unknown";
}

void print_deck_stats(std::string_view label,
                      const alpha::DeckSimulationStats& stats) {
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
                     const alpha::BotSimulationStats& stats) {
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

void print_deck_bot_benefit(const alpha::TournamentSummary& result) {
    struct Benefit {
        alpha::DeckId deck;
        double ranking_delta;
    };

    const auto random_index =
        static_cast<std::size_t>(alpha::BotKind::Random);
    const auto monte_carlo_index =
        static_cast<std::size_t>(alpha::BotKind::MonteCarlo);
    const auto deep_index =
        static_cast<std::size_t>(alpha::BotKind::DeepMonteCarlo);
    const auto handcrafted_index =
        static_cast<std::size_t>(alpha::BotKind::Handcrafted);
    const auto learned_index =
        static_cast<std::size_t>(alpha::BotKind::Learned);
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
        alpha::compare_learned_deck_lifts(result);

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
            .deck = static_cast<alpha::DeckId>(deck),
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
                  << alpha::deck_name(benefits[rank].deck)
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
        std::cout << "  Learned per-deck lift gate: "
                  << (learned_lift.learned_is_best_on_every_deck()
                          ? "PASS"
                          : "FAIL");
        if (!learned_lift.complete()) {
            std::cout << " (all five policies need samples)";
        }
        std::cout << '\n';
    }
}

alpha::BotConfig bot_config(alpha::BotKind kind, std::size_t rollouts,
                            std::size_t deep_rollouts,
                            std::size_t training_games) {
    switch (kind) {
    case alpha::BotKind::Random:
    case alpha::BotKind::Handcrafted:
        return {
            .kind = kind,
            .rollouts_per_action = 1,
        };
    case alpha::BotKind::Learned:
        return {
            .kind = kind,
            .rollouts_per_action = 2,
            .training_games = training_games,
        };
    case alpha::BotKind::MonteCarlo:
        return {
            .kind = kind,
            .rollouts_per_action = rollouts,
        };
    case alpha::BotKind::DeepMonteCarlo:
        return {
            .kind = kind,
            .rollouts_per_action = deep_rollouts,
        };
    }
    throw std::invalid_argument("unknown bot kind");
}

std::string white_plan_action_name(
    const alpha::PriorityAction& action) {
    switch (action.kind) {
    case alpha::PriorityActionKind::Pass:
        return "Pass";
    case alpha::PriorityActionKind::PlayLand:
        return "Play " +
               std::string(alpha::card_definition(action.card).name);
    case alpha::PriorityActionKind::CastCreature:
    case alpha::PriorityActionKind::CastSorcery:
    case alpha::PriorityActionKind::CastArtifact:
    case alpha::PriorityActionKind::CastEnchantment:
        return "Cast " +
               std::string(alpha::card_definition(action.card).name);
    case alpha::PriorityActionKind::CastLightningBolt:
        return "Cast Lightning Bolt";
    case alpha::PriorityActionKind::CastCounterspell:
        return "Cast Counterspell";
    case alpha::PriorityActionKind::ActivateMillstone:
        if (action.target.has_value() &&
            action.target->player == 1) {
            return "Activate Millstone targeting opponent";
        }
        return "Activate Millstone targeting self";
    }
    throw std::logic_error("unknown diagnostic priority action");
}

void print_white_plan_diagnostic(
    const alpha::WhitePlanTeacherDiagnostic& result,
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

void print_benchmark(const alpha::BotBenchmarkSummary& result,
                     std::uint64_t evaluation_seed) {
    std::cout << std::fixed << std::setprecision(1)
              << "Early Magic Bot Benchmark\n"
              << "Evaluation seed: " << evaluation_seed << '\n'
              << "Training seed: "
              << result.learned_training_seed << '\n'
              << "Challenger: "
              << alpha::bot_name(result.challenger.kind) << '\n'
              << "Baseline: " << alpha::bot_name(result.baseline.kind)
              << '\n';
    if (result.challenger.kind == alpha::BotKind::Learned ||
        result.baseline.kind == alpha::BotKind::Learned) {
        const std::size_t training_games =
            result.challenger.kind == alpha::BotKind::Learned
                ? result.challenger.training_games
                : result.baseline.training_games;
        std::cout << "Learned self-play training games: "
                  << training_games << '\n';
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
        std::cout << "PASS — challenger is better at 95% confidence\n";
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
        const auto id = static_cast<alpha::DeckId>(deck);
        const auto& challenger = result.challenger_decks[deck];
        const auto& baseline = result.baseline_decks[deck];
        std::cout << "  " << alpha::deck_name(id) << ": challenger "
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
    constexpr std::array<alpha::BotKind, 4> baseline_kinds = {
        alpha::BotKind::Random,
        alpha::BotKind::MonteCarlo,
        alpha::BotKind::DeepMonteCarlo,
        alpha::BotKind::Handcrafted,
    };
    const alpha::BotConfig learned_config =
        bot_config(alpha::BotKind::Learned, rollouts,
                   deep_rollouts, training_games);
    std::array<alpha::BotBenchmarkSummary, baseline_kinds.size()>
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
    const auto merge_bot = [](alpha::BotSimulationStats& destination,
                              const alpha::BotSimulationStats& source) {
        destination.games += source.games;
        destination.wins += source.wins;
        destination.losses += source.losses;
        destination.draws += source.draws;
        destination.total_decisions += source.total_decisions;
        destination.total_rollouts += source.total_rollouts;
    };
    const auto merge_deck = [](alpha::DeckSimulationStats& destination,
                               const alpha::DeckSimulationStats& source) {
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
        alpha::kBotKindCount * alpha::kBotKindCount;
    const std::size_t mixed_target_games =
        20 * repetitions_per_deck_pairing;
    const std::size_t mixed_games_per_matchup =
        ((mixed_target_games + mixed_policy_matrix_games - 1) /
         mixed_policy_matrix_games) *
        mixed_policy_matrix_games;
    const alpha::TournamentConfig mixed_config = {
        .bot_field = alpha::BotField::Mixed,
        .monte_carlo_rollouts = rollouts,
        .deep_monte_carlo_rollouts = deep_rollouts,
        .learned_training_games = training_games,
    };
    alpha::TournamentSummary pooled_mixed;
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

    alpha::GameConfig shared_config;
    shared_config.learned_training_seed = training_seed;
    shared_config.learned_model = alpha::train_learned_model(
        training_games, training_seed);
    std::cout << " done\n\n";

    for (std::size_t run = 0; run < runs; ++run) {
        const std::uint64_t evaluation_seed =
            base_seed + 101ULL * static_cast<std::uint64_t>(run + 1);
        bool seed_pass = true;
        std::cout << "  Evaluation seed " << evaluation_seed
                  << ":\n";
        for (std::size_t baseline = 0;
             baseline < baseline_kinds.size(); ++baseline) {
            const auto result = alpha::run_bot_benchmark(
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
                      << alpha::bot_name(baseline_kinds[baseline])
                      << ": " << result.challenger_stats.wins << '-'
                      << result.baseline_stats.wins << '-'
                      << result.challenger_stats.draws << " ("
                      << result.challenger_win_rate() << "%)"
                      << (learned_won ? " PASS\n" : " FAIL\n");
        }
        const auto mixed = alpha::run_tournament(
            mixed_games_per_matchup, evaluation_seed,
            shared_config,
            mixed_config);
        const auto seed_lift =
            alpha::compare_learned_deck_lifts(mixed);
        const bool seed_lift_pass =
            seed_lift.learned_is_best_on_every_deck();
        mixed_seed_lift_passes += seed_lift_pass ? 1 : 0;
        std::cout << "    mixed-field lift:";
        for (const auto& deck : seed_lift.decks) {
            std::cout << ' ' << alpha::deck_name(deck.deck) << '='
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
                  << alpha::bot_name(baseline_kinds[baseline])
                  << ": " << result.challenger_stats.wins << '-'
                  << result.baseline_stats.wins << '-'
                  << result.challenger_stats.draws << " ("
                  << result.challenger_win_rate()
                  << "%, 95% interval "
                  << result.confidence_low_95() << "% to "
                  << result.confidence_high_95() << "%)\n";
        for (std::size_t deck = 0;
             deck < result.challenger_decks.size(); ++deck) {
            const auto id = static_cast<alpha::DeckId>(deck);
            const auto& learned =
                result.challenger_decks[deck];
            const auto& other = result.baseline_decks[deck];
            const bool deck_won = learned.wins > other.wins;
            every_deck_won = every_deck_won && deck_won;
            std::cout << "    " << alpha::deck_name(id) << ": "
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
        alpha::compare_learned_deck_lifts(pooled_mixed);
    const bool mixed_lift_pass =
        pooled_lift.learned_is_best_on_every_deck();
    std::cout << "\nPooled mixed-field lift over Random\n";
    for (const auto& deck : pooled_lift.decks) {
        std::cout << "  " << alpha::deck_name(deck.deck) << ": ";
        if (!deck.available) {
            std::cout << "N/A FAIL\n";
            continue;
        }
        std::cout << "Learned ";
        print_delta(deck.learned_lift);
        std::cout << ", best other "
                  << alpha::bot_name(deck.best_other) << ' ';
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
    const auto learned = bot_config(
        alpha::BotKind::Learned, rollouts,
        deep_rollouts, training_games);
    const auto handcrafted = bot_config(
        alpha::BotKind::Handcrafted, rollouts,
        deep_rollouts, training_games);
    std::array<std::array<double, seeds.size()>, seeds.size()>
        win_rates{};

    std::cout << std::fixed << std::setprecision(1)
              << "Learned Training/Evaluation Seed Variance Study\n"
              << "Challenger: "
              << alpha::bot_name(alpha::BotKind::Learned) << '\n'
              << "Baseline: "
              << alpha::bot_name(alpha::BotKind::Handcrafted)
              << '\n'
              << "Training seeds: 424242, 101, 707\n"
              << "Evaluation seeds: 424242, 101, 707\n"
              << "Training games per model: " << training_games
              << '\n'
              << "Paired repetitions per cell: "
              << repetitions_per_deck_pairing << "\n\n";

    for (std::size_t training = 0; training < seeds.size();
         ++training) {
        alpha::GameConfig shared_config;
        shared_config.learned_training_seed = seeds[training];
        std::cout << "Training seed " << seeds[training]
                  << "..." << std::flush;
        shared_config.learned_model = alpha::train_learned_model(
            training_games, seeds[training]);
        std::cout << " done\n";
        for (std::size_t evaluation = 0;
             evaluation < seeds.size(); ++evaluation) {
            const auto result = alpha::run_bot_benchmark(
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

void print_evolution(const alpha::DeckEvolutionSummary& result,
                     std::uint64_t seed) {
    std::cout << std::fixed << std::setprecision(1)
              << "Early Magic Deck Evolution\n"
              << "Seed: " << seed << "\n\nGeneration best fitness\n";
    for (std::size_t generation = 0;
         generation < result.generation_best_win_rates.size();
         ++generation) {
        std::cout << "  " << generation + 1 << ": "
                  << result.generation_best_win_rates[generation]
                  << "%\n";
    }

    constexpr std::size_t card_count =
        static_cast<std::size_t>(alpha::CardId::Moat) + 1;
    std::array<std::size_t, card_count> counts{};
    for (const alpha::CardId card : result.best.cards) {
        ++counts[static_cast<std::size_t>(card)];
    }
    std::cout << "\nBest 40-card deck\n";
    for (std::size_t card = 0; card < counts.size(); ++card) {
        if (counts[card] == 0) {
            continue;
        }
        const auto id = static_cast<alpha::CardId>(card);
        std::cout << "  " << counts[card] << " "
                  << alpha::card_definition(id).name << '\n';
    }
    std::cout << "\nFitness: " << result.best.total.win_rate()
              << "% (" << result.best.total.wins << '-'
              << result.best.total.losses << '-'
              << result.best.total.draws << ")\n"
              << "By metagame opponent\n";
    for (std::size_t opponent = 0;
         opponent < result.best.by_opponent.size(); ++opponent) {
        const auto id = static_cast<alpha::DeckId>(opponent);
        const auto& stats = result.best.by_opponent[opponent];
        std::cout << "  " << alpha::deck_name(id) << ": "
                  << stats.win_rate() << "% (" << stats.wins << '-'
                  << stats.losses << '-' << stats.draws << ")\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::size_t games = 100;
        std::uint64_t seed = random_seed();
        alpha::BotField bot_field = alpha::BotField::Mixed;
        std::size_t rollouts = 2;
        std::size_t deep_rollouts = 8;
        std::size_t training_games = 800;
        std::uint64_t training_seed =
            alpha::kDefaultLearnedTrainingSeed;
        bool games_were_set = false;
        bool benchmark = false;
        bool stability = false;
        bool evolve = false;
        bool diagnose_white_plan = false;
        bool variance_study = false;
        std::size_t stability_runs = 8;
        std::size_t generations = 10;
        std::size_t population = 16;
        alpha::BotKind challenger = alpha::BotKind::Handcrafted;
        alpha::BotKind baseline = alpha::BotKind::MonteCarlo;

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
            if (option != "--games" && option != "--seed" &&
                option != "--train-seed" &&
                option != "--bots" && option != "--rollouts" &&
                option != "--deep-rollouts" &&
                option != "--train-games" &&
                option != "--stability-runs" &&
                option != "--generations" &&
                option != "--population" &&
                option != "--challenger" &&
                option != "--baseline") {
                throw std::invalid_argument("unknown option: " +
                                            std::string(option));
            }
            if (++argument >= argc) {
                throw std::invalid_argument("missing value for " +
                                            std::string(option));
            }
            if (option == "--bots") {
                bot_field = parse_bot_field(argv[argument]);
                continue;
            }
            if (option == "--challenger" ||
                option == "--baseline") {
                const auto kind = parse_bot_kind(argv[argument]);
                if (option == "--challenger") {
                    challenger = kind;
                } else {
                    baseline = kind;
                }
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
                if (value < 4) {
                    throw std::invalid_argument(
                        "--population must be at least four");
                }
                population = static_cast<std::size_t>(value);
            } else {
                if (value == 0) {
                    throw std::invalid_argument(
                        "--train-games must be greater than zero");
                }
                training_games = static_cast<std::size_t>(value);
            }
        }

        if (static_cast<int>(benchmark) + static_cast<int>(stability) +
                static_cast<int>(evolve) +
                static_cast<int>(diagnose_white_plan) +
                static_cast<int>(variance_study) >
            1) {
            throw std::invalid_argument(
                "--benchmark, --stability, --evolve-deck, and "
                "--diagnose-white-plan, and --variance-study cannot "
                "be combined");
        }
        if (diagnose_white_plan) {
            const auto model = alpha::train_learned_model(
                training_games, training_seed);
            const auto result =
                alpha::diagnose_white_lock_plan_teacher(model, seed);
            print_white_plan_diagnostic(
                result, seed, training_seed, training_games);
            return 0;
        }
        if (benchmark) {
            const auto challenger_config =
                bot_config(challenger, rollouts, deep_rollouts,
                           training_games);
            const auto baseline_config =
                bot_config(baseline, rollouts, deep_rollouts,
                           training_games);
            alpha::GameConfig shared_config;
            shared_config.learned_training_seed = training_seed;
            if (challenger == alpha::BotKind::Learned ||
                baseline == alpha::BotKind::Learned) {
                shared_config.learned_model =
                    alpha::train_learned_model(
                        training_games, training_seed);
            }
            const auto result = alpha::run_bot_benchmark(
                games, seed, challenger_config,
                baseline_config, shared_config);
            print_benchmark(result, seed);
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
            alpha::GameConfig evolution_game_config;
            evolution_game_config.learned_training_seed =
                training_seed;
            const auto result = alpha::evolve_deck(
                {
                    .generations = generations,
                    .population = population,
                    .repetitions_per_opponent = repetitions,
                    .pilot =
                        {
                            .kind = alpha::BotKind::Handcrafted,
                            .rollouts_per_action = 1,
                        },
                },
                seed, evolution_game_config);
            print_evolution(result, seed);
            return 0;
        }

        alpha::GameConfig tournament_game_config;
        tournament_game_config.learned_training_seed =
            training_seed;
        if (bot_field == alpha::BotField::Mixed ||
            bot_field == alpha::BotField::Learned) {
            tournament_game_config.learned_model =
                alpha::train_learned_model(
                    training_games, training_seed);
        }
        const alpha::TournamentSummary result =
            alpha::run_tournament(
                games, seed, tournament_game_config,
                {.bot_field = bot_field,
                 .monte_carlo_rollouts = rollouts,
                 .deep_monte_carlo_rollouts = deep_rollouts,
                 .learned_training_games = training_games});

        std::cout << std::fixed << std::setprecision(1)
                  << "Early Magic Bot Simulator\n"
                  << "Evaluation seed: " << seed << '\n'
                  << "Training seed: " << training_seed << '\n'
                  << "Bot field: " << bot_field_name(bot_field) << '\n';
        if (bot_field == alpha::BotField::Mixed ||
            bot_field == alpha::BotField::MonteCarlo) {
            std::cout << "Monte Carlo rollouts per legal action: "
                      << rollouts << '\n';
        }
        if (bot_field == alpha::BotField::Mixed ||
            bot_field == alpha::BotField::DeepMonteCarlo) {
            std::cout
                << "Deep Monte Carlo rollouts per legal action: "
                << deep_rollouts << '\n';
        }
        if (bot_field == alpha::BotField::Mixed ||
            bot_field == alpha::BotField::Learned) {
            std::cout << "Learned Value self-play training games: "
                      << training_games << '\n';
        }
        std::cout
                  << "Games per matchup: " << result.games_per_matchup
                  << '\n'
                  << "Total games: " << result.total_games
                  << "\n\nMatchups\n";

        for (const auto& matchup : result.matchups) {
            const auto& first = matchup.result.decks[0];
            const auto& second = matchup.result.decks[1];
            std::cout << "  " << alpha::deck_name(matchup.first_deck)
                      << " vs "
                      << alpha::deck_name(matchup.second_deck) << ": "
                      << first.wins << '-' << second.wins << '-'
                      << matchup.result.draws << " ("
                      << first.win_rate() << "% / "
                      << second.win_rate() << "%)\n";
        }

        std::cout << "\nDeck statistics\n";
        for (std::size_t deck = 0; deck < result.decks.size(); ++deck) {
            const auto id = static_cast<alpha::DeckId>(deck);
            const std::string label =
                std::string(alpha::deck_name(id)) + " — " +
                std::string(alpha::deck_list(id));
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
            const auto kind = static_cast<alpha::BotKind>(bot);
            print_bot_stats(alpha::bot_name(kind), result.bots[bot]);
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
            std::cout << "  " << alpha::bot_name(matchup.first_bot)
                      << " vs " << alpha::bot_name(matchup.second_bot)
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
