#include "old_school/action_q_multiscale_explore.hpp"

#include "old_school/action_q_bellman_teacher.hpp"
#include "old_school/action_q_multiscale_teacher.hpp"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq = old_school::action_q_multiscale_explore;
namespace bellman = old_school::action_q_bellman_teacher;
namespace multiscale =
    old_school::action_q_multiscale_teacher;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
static_assert(
    aq::kBaseHorizonTurns ==
    old_school::kLearnedValueSearchHorizonTurns);
static_assert(
    aq::kBaseRolloutsPerWorld ==
    old_school::kLearnedValueSearchRolloutsPerWorld);
bool selector_seed_opened = false;

std::shared_ptr<const old_school::LearnedModel>
load_parent() {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            800, 424242, 16);
    const auto parent = artifact.model();
    if (old_school::learned_model_fingerprint(parent) !=
        aq::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ2 loaded parent fingerprint drifted");
    }
    return parent;
}

aq::PreflightReport require_preflight(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent) {
    aq::PreflightReport report =
        aq::run_preflight(parent);
    aq::print_preflight_report(std::cout, report);
    if (!report.gate_passed()) {
        throw std::runtime_error(
            "AQ2 multiscale D0 preflight failed before corpus");
    }
    return report;
}

void print_metrics(
    const aq::Metrics& metrics,
    std::string_view split,
    std::string_view policy) {
    std::cout
        << std::setprecision(17)
        << "metrics split=" << split
        << " policy=" << policy
        << " roots=" << metrics.roots
        << " options=" << metrics.options
        << " top1_expected_agreement="
        << metrics.equal_deck_top_one_expected_agreement
        << " mean_regret="
        << metrics.equal_deck_mean_regret << '\n';
    for (const auto& deck : metrics.decks) {
        std::cout
            << "metrics_deck split=" << split
            << " policy=" << policy
            << " deck="
            << old_school::deck_name(deck.deck)
            << " roots=" << deck.roots
            << " options=" << deck.options
            << " top1_expected_agreement="
            << deck.top_one_expected_agreement
            << " mean_regret="
            << deck.mean_regret << '\n';
    }
}

void print_run(
    const aq::Corpus& corpus,
    const aq::FitReport& fit,
    const aq::OfflineReport& report,
    double collection_seconds,
    double fit_seconds,
    double gate_seconds) {
    std::cout
        << "schema=old-school-action-q-aq2-ms0-run-v1\n"
        << "mode=run\n"
        << "parent_artifact=" << kParentArtifactPath << '\n'
        << "parent_fingerprint="
        << report.parent_fingerprint << '\n'
        << "candidate_fingerprint="
        << report.candidate_fingerprint << '\n'
        << "root_seed=" << corpus.root_seed << '\n'
        << "fit_seed=" << aq::fit_seed() << '\n'
        << "reserved_selector_seed="
        << aq::kReservedSelectorSeed << '\n'
        << "collection_seconds="
        << collection_seconds
        << " fit_seconds=" << fit_seconds
        << " offline_gate_seconds="
        << gate_seconds << '\n'
        << "fit_examples=" << fit.fit_examples
        << " fit_options=" << fit.fit_options
        << " batch_size=" << fit.optimizer.batch_size
        << " epochs=" << fit.optimizer.epochs
        << " learning_rate="
        << fit.optimizer.learning_rate
        << " residual_weight="
        << fit.optimizer.residual_weight << '\n';
    aq::print_census(std::cout, corpus);
    print_metrics(fit.parent_fit, "FIT", "parent");
    print_metrics(fit.candidate_fit, "FIT", "candidate");
    print_metrics(report.check.parent, "CHECK", "parent");
    print_metrics(
        report.check.candidate,
        "CHECK", "candidate");
    aq::print_model_gate_report(std::cout, report.model);
    std::cout
        << "gates isolation="
        << report.isolation.gate_passed()
        << " check=" << report.check.gate_passed()
        << " model=" << report.model.gate_passed()
        << " blue_regret_no_worse="
        << report.frozen_dev_blue_regret_no_worse
        << " accounting="
        << report.corpus_accounting_complete
        << " descriptor_order="
        << report.model.descriptor_order.gate_passed()
        << " hidden_repartition="
        << report.model.descriptor_order
               .hidden_action_keyed_scores_bit_identical
        << " intervening_counter="
        << report.model.behavior
               .intervening_counter_selects_opposing_counter
        << " sick_bear_growth_pass="
        << report.model.behavior
               .sick_bear_growth_selects_pass
        << " braingeyser_x0_excluded="
        << report.model.behavior
               .braingeyser_x_zero_excluded
        << '\n';
}

old_school::BotConfig selector_bot(
    std::shared_ptr<const old_school::LearnedModel> model,
    double residual_weight) {
    return {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = aq::kBaseWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = residual_weight,
        .value_pass_dominance = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_adversarial_blocks = false,
        .value_continuation_controller =
            old_school::LearnedContinuationController::Legacy,
        .training_games = 800,
        .learned_model = std::move(model),
    };
}

void require_frozen_selector(
    const old_school::BotBenchmarkSummary& result,
    const std::shared_ptr<const old_school::LearnedModel>& parent,
    const std::shared_ptr<const old_school::LearnedModel>&
        candidate) {
    if (result.evaluation_seed !=
            aq::kReservedSelectorSeed ||
        result.learned_training_seed != 424242 ||
        result.repetitions_per_deck_pairing !=
            aq::kSelectorRepetitions ||
        result.total_games != aq::kExpectedSelectorGames ||
        result.challenger_stats.games !=
            aq::kExpectedSelectorGames ||
        result.baseline_stats.games !=
            aq::kExpectedSelectorGames ||
        result.challenger_model_fingerprint !=
            old_school::learned_model_fingerprint(candidate) ||
        result.baseline_model_fingerprint !=
            old_school::learned_model_fingerprint(parent) ||
        result.challenger.learned_model != candidate ||
        result.baseline.learned_model != parent ||
        result.challenger.kind !=
            old_school::BotKind::Learned ||
        result.baseline.kind !=
            old_school::BotKind::Learned ||
        result.challenger.learned_variant !=
            old_school::LearnedVariant::ValueSearchChampion ||
        result.baseline.learned_variant !=
            old_school::LearnedVariant::ValueSearchChampion ||
        result.challenger.rollouts_per_action !=
            aq::kBaseWorlds ||
        result.baseline.rollouts_per_action !=
            aq::kBaseWorlds ||
        result.challenger.exploration_rate != 0.0 ||
        result.baseline.exploration_rate != 0.0 ||
        result.challenger.value_continuation_epsilon != 0.0 ||
        result.baseline.value_continuation_epsilon != 0.0 ||
        result.challenger.value_priority_residual_weight !=
            aq::kCandidateResidualWeight ||
        result.baseline.value_priority_residual_weight != 0.0 ||
        result.challenger.value_pass_dominance ||
        result.baseline.value_pass_dominance ||
        result.challenger
                .value_resolved_shallow_prior_weight != 0.0 ||
        result.baseline
                .value_resolved_shallow_prior_weight != 0.0 ||
        result.challenger.value_adversarial_blocks ||
        result.baseline.value_adversarial_blocks ||
        result.challenger.value_continuation_controller !=
            old_school::LearnedContinuationController::Legacy ||
        result.baseline.value_continuation_controller !=
            old_school::LearnedContinuationController::Legacy ||
        result.challenger.training_games != 800 ||
        result.baseline.training_games != 800) {
        throw std::runtime_error(
            "AQ2 selector drifted from frozen recipe");
    }
    const auto accounted =
        [](const old_school::BotSimulationStats& stats) {
            return stats.games ==
                stats.wins + stats.losses + stats.draws;
        };
    if (!accounted(result.challenger_stats) ||
        !accounted(result.baseline_stats) ||
        result.challenger_stats.wins !=
            result.baseline_stats.losses ||
        result.challenger_stats.losses !=
            result.baseline_stats.wins ||
        result.challenger_stats.draws !=
            result.baseline_stats.draws ||
        result.life_total_finishes +
                result.empty_library_finishes +
                result.turn_limit_draws !=
            aq::kExpectedSelectorGames ||
        result.challenger_quartet_cr1.clusters != 15 ||
        result.challenger_quartet_cr1.records !=
            aq::kExpectedSelectorGames) {
        throw std::runtime_error(
            "AQ2 selector aggregate accounting drifted");
    }
    std::size_t challenger_deck_games = 0;
    std::size_t baseline_deck_games = 0;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto& challenger_deck =
            result.challenger_decks[deck];
        const auto& baseline_deck =
            result.baseline_decks[deck];
        challenger_deck_games += challenger_deck.games;
        baseline_deck_games += baseline_deck.games;
        if (challenger_deck.games !=
                aq::kExpectedSelectorGamesPerDeck ||
            baseline_deck.games !=
                aq::kExpectedSelectorGamesPerDeck ||
            challenger_deck.wins + challenger_deck.losses +
                    challenger_deck.draws !=
                challenger_deck.games ||
            baseline_deck.wins + baseline_deck.losses +
                    baseline_deck.draws !=
                baseline_deck.games ||
            challenger_deck.on_play_games != 6 ||
            challenger_deck.on_draw_games != 6 ||
            baseline_deck.on_play_games != 6 ||
            baseline_deck.on_draw_games != 6) {
            throw std::runtime_error(
                "AQ2 selector deck balance drifted");
        }
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                if (result.challenger_outcome_quadrants
                        [deck][seat][play_draw]
                            .games != 3 ||
                    result.baseline_outcome_quadrants
                        [deck][seat][play_draw]
                            .games != 3) {
                    throw std::runtime_error(
                        "AQ2 selector quadrant balance drifted");
                }
            }
        }
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount;
             ++opponent) {
            const std::size_t expected =
                deck == opponent ? 4 : 2;
            if (result.challenger_deck_matchups
                    [deck][opponent]
                        .games != expected) {
                throw std::runtime_error(
                    "AQ2 selector matchup balance drifted");
            }
        }
    }
    if (challenger_deck_games !=
            aq::kExpectedSelectorGames ||
        baseline_deck_games !=
            aq::kExpectedSelectorGames) {
        throw std::runtime_error(
            "AQ2 selector deck accounting drifted");
    }
}

void print_selector(
    const old_school::BotBenchmarkSummary& result,
    double seconds) {
    std::cout
        << std::setprecision(17)
        << "selector seed=" << result.evaluation_seed
        << " order=treatment-first"
        << " repetitions="
        << result.repetitions_per_deck_pairing
        << " games=" << result.total_games
        << " challenger_wins="
        << result.challenger_stats.wins
        << " challenger_losses="
        << result.challenger_stats.losses
        << " draws=" << result.challenger_stats.draws
        << " win_rate=" << result.challenger_win_rate()
        << " seconds=" << seconds
        << " challenger_decisions="
        << result.challenger_stats.total_decisions
        << " challenger_rollouts="
        << result.challenger_stats.total_rollouts
        << " baseline_decisions="
        << result.baseline_stats.total_decisions
        << " baseline_rollouts="
        << result.baseline_stats.total_rollouts
        << " total_turns=" << result.total_turns
        << " life_finishes="
        << result.life_total_finishes
        << " library_finishes="
        << result.empty_library_finishes
        << " turn_limit_draws="
        << result.turn_limit_draws
        << " schedule_accounting=PASS\n";
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto id =
            static_cast<old_school::DeckId>(deck);
        const auto& challenger =
            result.challenger_decks[deck];
        const auto& baseline =
            result.baseline_decks[deck];
        std::cout
            << "selector_deck deck="
            << old_school::deck_name(id)
            << " games=" << challenger.games
            << " challenger="
            << challenger.wins << '-'
            << challenger.losses << '-'
            << challenger.draws
            << " challenger_win_rate="
            << challenger.win_rate()
            << " on_play="
            << challenger.on_play_wins << '/'
            << challenger.on_play_games
            << " on_draw="
            << challenger.on_draw_wins << '/'
            << challenger.on_draw_games
            << " baseline="
            << baseline.wins << '-'
            << baseline.losses << '-'
            << baseline.draws << '\n';
    }
}

int run_census() {
    const auto parent = load_parent();
    static_cast<void>(require_preflight(parent));
    const aq::Corpus corpus = aq::collect_corpus(parent);
    std::cout
        << "schema=old-school-action-q-aq2-ms0-census-v1\n"
        << "mode=census\n"
        << "parent_artifact=" << kParentArtifactPath << '\n'
        << "parent_fingerprint="
        << corpus.parent_fingerprint << '\n'
        << "root_seed=" << corpus.root_seed << '\n'
        << "schedule_generation="
        << aq::kScheduleGeneration << '\n'
        << "fit_seed=" << aq::fit_seed() << '\n'
        << "base_worlds=" << aq::kBaseWorlds
        << " base_horizon=" << aq::kBaseHorizonTurns
        << " base_rollouts_per_world="
        << aq::kBaseRolloutsPerWorld
        << " base_evaluation_threads="
        << aq::kBaseEvaluationThreads
        << " bellman_root_worlds="
        << bellman::kRootWorlds
        << " bellman_successor_banks=2"
        << " bellman_successor_worlds_per_bank="
        << bellman::kSuccessorWorlds
        << " bellman_weight="
        << multiscale::kBellmanWeight
        << " resolved_weight="
        << multiscale::kResolvedWeight
        << " root_cap="
        << aq::kMaximumRootsPerActorGame
        << " source_turn_cap="
        << aq::kSourceTurnCap << '\n'
        << "preflight_directions=4"
        << " descriptor_order_bit_identical=1"
        << " hidden_repartition_bit_identical=1"
        << " hidden_repartition_nonvacuous=1\n";
    aq::print_census(std::cout, corpus);
    std::cout
        << "result=PASS disposition=CENSUS_ONLY"
        << " model_created=0 gameplay_seed_opened=0"
        << " artifact_published=0\n";
    return 0;
}

int run_experiment() {
    using Clock = std::chrono::steady_clock;
    const auto parent = load_parent();
    static_cast<void>(require_preflight(parent));
    const auto collection_start = Clock::now();
    const aq::Corpus corpus = aq::collect_corpus(parent);
    const auto collection_end = Clock::now();
    aq::require_frozen_census(corpus);
    const auto fit_start = Clock::now();
    const aq::FitReport fit = aq::fit(corpus, parent);
    const auto fit_end = Clock::now();
    if (!fit.candidate) {
        throw std::runtime_error(
            "AQ2 fit produced a null candidate");
    }
    const auto gate_start = Clock::now();
    const aq::OfflineReport report =
        aq::evaluate_offline(
            corpus, fit, parent, fit.candidate);
    const auto gate_end = Clock::now();
    const auto seconds =
        [](Clock::time_point begin,
           Clock::time_point end) {
            return std::chrono::duration<double>(
                       end - begin)
                .count();
        };
    print_run(
        corpus, fit, report,
        seconds(collection_start, collection_end),
        seconds(fit_start, fit_end),
        seconds(gate_start, gate_end));
    if (!report.gate_passed()) {
        for (const std::string& failure :
             report.failures()) {
            std::cout
                << "offline_failure="
                << failure << '\n';
        }
        std::cout
            << "result=REJECT stage=OFFLINE"
            << " selector_seed_opened=0"
            << " gameplay_seed_opened=0"
            << " artifact_published=0\n";
        return 1;
    }

    old_school::GameConfig game;
    game.max_turns = 500;
    game.learned_training_seed = 424242;
    game.learned_search_depth = 1;
    const old_school::BotConfig challenger =
        selector_bot(
            fit.candidate,
            aq::kCandidateResidualWeight);
    const old_school::BotConfig baseline =
        selector_bot(parent, 0.0);
    if (parent == fit.candidate ||
        game.learned_model ||
        old_school::learned_model_fingerprint(parent) !=
            aq::kRequiredParentFingerprint ||
        old_school::learned_model_fingerprint(
            fit.candidate) !=
            report.candidate_fingerprint ||
        report.candidate_fingerprint ==
            report.parent_fingerprint) {
        throw std::runtime_error(
            "AQ2 selector boundary identity check failed");
    }
    const std::string parent_before =
        old_school::learned_model_fingerprint(parent);
    const std::string candidate_before =
        old_school::learned_model_fingerprint(
            fit.candidate);
    const auto parent_components_before =
        old_school::learned_model_component_fingerprints(
            parent);
    const auto candidate_components_before =
        old_school::learned_model_component_fingerprints(
            fit.candidate);
    std::cout
        << "selector_recipe seed="
        << aq::kReservedSelectorSeed
        << " order=treatment-first"
        << " repetitions=" << aq::kSelectorRepetitions
        << " strict_wins_required="
        << aq::kSelectorWinsRequired
        << " worlds=" << aq::kBaseWorlds
        << " horizon=" << aq::kBaseHorizonTurns
        << " rollouts_per_world="
        << aq::kBaseRolloutsPerWorld
        << " candidate_residual="
        << aq::kCandidateResidualWeight
        << " baseline_residual=0"
        << " exploration=0 continuation_epsilon=0"
        << " pass_dominance=0 resolved_alpha=0"
        << " adversarial_blocks=0 continuation=Legacy"
        << " max_turns=" << game.max_turns << '\n';
    const auto selector_start = Clock::now();
    selector_seed_opened = true;
    const old_school::BotBenchmarkSummary selector =
        old_school::run_bot_benchmark(
            aq::kSelectorRepetitions,
            aq::kReservedSelectorSeed,
            challenger, baseline, game, false);
    const auto selector_end = Clock::now();
    require_frozen_selector(
        selector, parent, fit.candidate);
    if (old_school::learned_model_fingerprint(parent) !=
            parent_before ||
        old_school::learned_model_fingerprint(
            fit.candidate) != candidate_before ||
        old_school::learned_model_component_fingerprints(
            parent) != parent_components_before ||
        old_school::learned_model_component_fingerprints(
            fit.candidate) !=
            candidate_components_before) {
        throw std::runtime_error(
            "AQ2 selector mutated a frozen model");
    }
    print_selector(
        selector,
        seconds(selector_start, selector_end));
    const bool advances =
        aq::selector_wins_advance(
            selector.challenger_stats.wins);
    std::cout
        << "result=" << (advances ? "PASS" : "REJECT")
        << " stage=SELECTOR disposition="
        << (advances
                ? "MANUAL_PILOT_ELIGIBLE"
                : "AQ2_CLOSED")
        << " selector_seed_opened=1"
        << " gameplay_seed_opened=1"
        << " artifact_published=0\n";
    return advances ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const auto command = aq::parse_command(arguments);
    if (!command.has_value()) {
        aq::print_usage(std::cerr);
        return 2;
    }
    try {
        if (*command == aq::Command::Census) {
            return run_census();
        }
        return run_experiment();
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_multiscale_explore_failed"
            << " message=" << error.what()
            << " selector_seed_opened="
            << selector_seed_opened
            << " gameplay_seed_opened="
            << selector_seed_opened
            << " artifact_published=0\n";
        return 1;
    }
}
