#include "old_school/fq4_d1_field_gate.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace gate = old_school::fq4_d1_field_gate;

int worker() {
    try {
        const auto parent =
            old_school::
                load_learned_value_challenger_artifact(
                    std::string(gate::kParentArtifactPath),
                    gate::kParentTrainingGames,
                    gate::kParentTrainingSeed,
                    gate::kParentGenerations)
                .model();
        const gate::CensusReport report =
            gate::run_parent_census(parent);
        const gate::ExitClassification classification =
            gate::classify_exit(report);
        if (!report.repeated_construction_bit_identical) {
            std::cerr
                << "FQ4-D1-P0 infrastructure failure: "
                   "repeat identity failed; scientific stdout "
                   "withheld\n";
            std::cerr.flush();
            return static_cast<int>(
                gate::ExitClassification::
                    InfrastructureFailure);
        }

        std::cout
            << std::setprecision(
                   std::numeric_limits<double>::
                       max_digits10)
            << "FQ4-D1-P0 held-out C16 parent census\n"
            << "parent_fingerprint="
            << report.parent_fingerprint << '\n'
            << "parent_components critic="
            << report.parent_components.critic
            << " priority="
            << report.parent_components.priority
            << " attack="
            << report.parent_components.attack
            << " block="
            << report.parent_components.block
            << " damage_order="
            << report.parent_components.damage_order
            << '\n'
            << "schedule_sha256="
            << report.schedule_sha256
            << " trajectory_sha256="
            << report.trajectory_sha256 << '\n'
            << "retained_corpus_sha256="
            << report.retained_corpus_sha256
            << " dominance_corpus_sha256="
            << report.dominance_corpus_sha256
            << " scored_corpus_sha256="
            << report.scored_corpus_sha256
            << " audit_scores_sha256="
            << report.audit_scores_sha256 << '\n'
            << "physical_games="
            << report.schedule_balance.physical_games
            << " owner_perspectives="
            << report.schedule_balance.owner_perspectives
            << " schedule_exact="
            << (report.schedule_balance.exact ? 1 : 0)
            << '\n';

        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            const auto deck_id =
                static_cast<old_school::DeckId>(deck);
            const auto& balance =
                report.schedule_balance;
            const auto& counts = report.decks[deck];
            const auto& games =
                report.deck_game_coverage[deck];
            std::cout
                << "deck="
                << old_school::deck_name(deck_id)
                << " perspectives="
                << balance.perspectives_by_deck[deck]
                << " seat0="
                << balance.seat_zero_by_deck[deck]
                << " on_play="
                << balance.on_play_by_deck[deck]
                << " raw=" << counts.raw
                << " nontrivial=" << counts.nontrivial
                << " malformed=" << counts.malformed
                << " trivial=" << counts.trivial
                << " over_cap=" << counts.over_cap
                << " eligible=" << counts.eligible
                << " duplicate=" << counts.duplicate
                << " unique=" << counts.unique
                << " retained=" << counts.retained
                << " cap_dropped=" << counts.cap_dropped
                << " dominance_positive="
                << counts.dominance_positive
                << " safe=" << counts.parent_classes[0]
                << " class1=" << counts.parent_classes[1]
                << " class2=" << counts.parent_classes[2]
                << " class3=" << counts.parent_classes[3]
                << " raw_base_dominated_support="
                << report
                       .raw_base_dominated_support_by_deck[
                           deck]
                << " raw_base_mixed_tie_support="
                << report
                       .raw_base_mixed_tie_support_by_deck[
                           deck]
                << " owner_games=" << games.owner_games
                << " games_with_raw="
                << games.games_with_raw
                << " games_with_retained="
                << games.games_with_retained
                << " games_with_dominance="
                << games
                       .games_with_dominance_positive
                << '\n';
        }
        for (const gate::GameCensus& game :
             report.games) {
            for (std::size_t owner = 0;
                 owner < 2; ++owner) {
                const auto& counts =
                    game.owners[owner];
                std::cout
                    << "owner_game source_seed_base="
                    << game.source.source_seed_base
                    << " schedule_index="
                    << game.source.schedule_index
                    << " game_seed="
                    << game.source.game_seed
                    << " owner=" << owner
                    << " deck="
                    << old_school::deck_name(
                           game.source
                               .seat_decks[owner])
                    << " raw=" << counts.raw
                    << " nontrivial="
                    << counts.nontrivial
                    << " malformed="
                    << counts.malformed
                    << " trivial=" << counts.trivial
                    << " over_cap=" << counts.over_cap
                    << " eligible=" << counts.eligible
                    << " duplicate="
                    << counts.duplicate
                    << " unique=" << counts.unique
                    << " retained=" << counts.retained
                    << " cap_dropped="
                    << counts.cap_dropped
                    << " dominance_positive="
                    << counts.dominance_positive
                    << " safe="
                    << counts.parent_classes[0]
                    << " class1="
                    << counts.parent_classes[1]
                    << " class2="
                    << counts.parent_classes[2]
                    << " class3="
                    << counts.parent_classes[3]
                    << '\n';
            }
        }
        const auto& pooled = report.pooled;
        std::size_t pooled_raw_base_support = 0;
        std::size_t pooled_raw_base_ties = 0;
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            pooled_raw_base_support +=
                report
                    .raw_base_dominated_support_by_deck[
                        deck];
            pooled_raw_base_ties +=
                report
                    .raw_base_mixed_tie_support_by_deck[
                        deck];
        }
        std::cout
            << "pooled raw=" << pooled.raw
            << " nontrivial=" << pooled.nontrivial
            << " malformed=" << pooled.malformed
            << " trivial=" << pooled.trivial
            << " over_cap=" << pooled.over_cap
            << " eligible=" << pooled.eligible
            << " duplicate=" << pooled.duplicate
            << " unique=" << pooled.unique
            << " retained=" << pooled.retained
            << " cap_dropped=" << pooled.cap_dropped
            << " dominance_positive="
            << pooled.dominance_positive
            << " safe=" << pooled.parent_classes[0]
            << " class1=" << pooled.parent_classes[1]
            << " class2=" << pooled.parent_classes[2]
            << " class3=" << pooled.parent_classes[3]
            << " class2_sigma_mass="
            << report.class2_sigma_mass
            << " raw_base_dominated_support="
            << pooled_raw_base_support
            << " raw_base_mixed_tie_support="
            << pooled_raw_base_ties << '\n'
            << "support high_confidence_roots="
            << pooled.parent_classes[1] +
                   pooled.parent_classes[2]
            << " distinct_games="
            << report.distinct_high_confidence_games
            << " distinct_decks="
            << report.distinct_high_confidence_decks
            << '\n'
            << "hidden_replays="
            << report.hidden_replay_roots
            << " distinct_clones="
            << report.distinct_hidden_clones
            << " vacuous_clones="
            << report.vacuous_hidden_clones << '\n';
        const auto print_accounting =
            [](std::string_view label,
               const gate::ProductionAccounting& accounting) {
                std::cout
                    << "accounting bucket=" << label
                    << " calls=" << accounting.score_calls
                    << " actions="
                    << accounting.scored_actions
                    << " worlds="
                    << accounting.sampled_worlds
                    << " evaluations="
                    << accounting.rollout_evaluations
                    << " terminal="
                    << accounting.terminal_evaluations
                    << " bootstrap="
                    << accounting.bootstrapped_evaluations
                    << " dominance_transitions="
                    << accounting.dominance_transitions
                    << '\n';
            };
        print_accounting(
            "primary", report.primary_accounting);
        print_accounting(
            "hidden-audit",
            report.hidden_control_accounting);
        print_accounting(
            "reverse-audit",
            report.reverse_control_accounting);
        print_accounting("total", report.accounting);
        print_accounting(
            "repeat-primary",
            report.repeat_primary_accounting);
        print_accounting(
            "repeat-hidden-audit",
            report.repeat_hidden_control_accounting);
        print_accounting(
            "repeat-reverse-audit",
            report.repeat_reverse_control_accounting);
        print_accounting(
            "repeat-total", report.repeat_accounting);
        std::cout
            << "controls replay="
            << (report.all_replays_exact ? 1 : 0)
            << " hidden_feature_bits="
            << (report
                        .all_hidden_feature_bits_identical
                    ? 1
                    : 0)
            << " scorer_hidden_reverse="
            << (report
                        .first_deck_controls_bit_identical
                    ? 1
                    : 0)
            << " accounting="
            << (report.recipe_and_accounting_exact ? 1 : 0)
            << " cross_sums="
            << (report.count_cross_sums_exact ? 1 : 0)
            << " repeat="
            << (report.repeated_construction_bit_identical
                    ? 1
                    : 0)
            << " runtime_seconds="
            << report.runtime_seconds << '\n';
        for (const std::string& reason :
             report.underpowered_reasons) {
            std::cout
                << "underpowered_reason="
                << reason << '\n';
        }
        for (const std::string& failure :
             report.infrastructure_failures) {
            std::cout
                << "infrastructure_failure="
                << failure << '\n';
        }
        std::cout
            << "verdict="
            << (classification ==
                        gate::ExitClassification::Pass
                    ? "PASS"
                    : classification ==
                              gate::ExitClassification::
                                  Underpowered
                          ? "UNDERPOWERED"
                          : "INFRASTRUCTURE_FAILURE")
            << '\n';
        std::cout.flush();
        return static_cast<int>(classification);
    } catch (const std::exception& error) {
        std::cerr
            << "FQ4-D1-P0 infrastructure failure: "
            << error.what() << '\n';
        std::cerr.flush();
        return static_cast<int>(
            gate::ExitClassification::
                InfrastructureFailure);
    }
}

int supervise_worker() {
    const pid_t child = ::fork();
    if (child < 0) {
        std::cerr
            << "FQ4-D1-P0 infrastructure failure: "
               "could not create watchdog worker\n";
        return static_cast<int>(
            gate::ExitClassification::
                InfrastructureFailure);
    }
    if (child == 0) {
        const int status = worker();
        ::_exit(status);
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(gate::kWatchdogSeconds);
    while (true) {
        int status = 0;
        const pid_t waited =
            ::waitpid(child, &status, WNOHANG);
        if (waited == child) {
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            std::cerr
                << "FQ4-D1-P0 infrastructure failure: "
                   "worker terminated by signal\n";
            return static_cast<int>(
                gate::ExitClassification::
                    InfrastructureFailure);
        }
        if (waited < 0) {
            static_cast<void>(::kill(child, SIGKILL));
            static_cast<void>(
                ::waitpid(child, nullptr, 0));
            std::cerr
                << "FQ4-D1-P0 infrastructure failure: "
                   "watchdog wait failed\n";
            return static_cast<int>(
                gate::ExitClassification::
                    InfrastructureFailure);
        }
        if (std::chrono::steady_clock::now() >=
            deadline) {
            static_cast<void>(::kill(child, SIGKILL));
            static_cast<void>(
                ::waitpid(child, nullptr, 0));
            std::cerr
                << "FQ4-D1-P0 infrastructure failure: "
                   "hard 180-second watchdog expired\n";
            return static_cast<int>(
                gate::ExitClassification::
                    InfrastructureFailure);
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 1) {
        const std::string program =
            argc > 0 && argv[0] != nullptr
                ? argv[0]
                : "old-school-fq4-priority-fit-d1-census";
        std::cerr << "Usage: " << program << '\n';
        return static_cast<int>(
            gate::ExitClassification::
                InfrastructureFailure);
    }
    return supervise_worker();
}
