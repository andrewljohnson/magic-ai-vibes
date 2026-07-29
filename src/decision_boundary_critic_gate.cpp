#include "old_school/decision_boundary_critic_gate.hpp"

#include "old_school/action_q_nested_actor_distill.hpp"
#include "old_school/information_set_puct.hpp"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace old_school::decision_boundary_critic_gate {
namespace {

namespace diagnostic =
    information_set_puct_budget_diagnostic;
namespace g1 = action_q_nested_actor_distill;
namespace puct = information_set_puct;

bool fitted_candidate_authenticated(
    const std::shared_ptr<const LearnedModel>& parent,
    const dbc::FitReport& fit) {
    if (!parent || !fit.candidate ||
        learned_model_fingerprint(parent) !=
            dbc::kRequiredParentFingerprint ||
        fit.parent_fingerprint_before !=
            dbc::kRequiredParentFingerprint ||
        fit.parent_fingerprint_after !=
            dbc::kRequiredParentFingerprint ||
        fit.candidate_fingerprint !=
            learned_model_fingerprint(fit.candidate) ||
        !fit.parent_immutable ||
        !fit.repeated_fit_bit_identical ||
        !fit.parameter_replay_bit_identical ||
        !fit.only_output_layer_changed ||
        fit.authorized_output_parameters !=
            dbc::kOutputParameterCount ||
        fit.changed_output_parameters == 0 ||
        fit.changed_output_parameters >
            dbc::kOutputParameterCount ||
        learned_model_component_fingerprints(parent) !=
            fit.parent_components ||
        learned_critic_tensor_fingerprints(parent) !=
            fit.parent_critic_tensors) {
        return false;
    }
    const auto parameters =
        learned_output_calibration_parameters(
            fit.candidate);
    const auto replay =
        with_learned_output_calibration_parameters(
            parent, parameters);
    return learned_model_fingerprint(replay) ==
               fit.candidate_fingerprint &&
           learned_output_calibration_parameters(replay) ==
               parameters &&
           learned_model_component_fingerprints(
               fit.candidate) ==
               fit.candidate_components &&
           learned_critic_tensor_fingerprints(
               fit.candidate) ==
               fit.candidate_critic_tensors;
}

template <typename Stats>
bool accounted(const Stats& stats) {
    return stats.games ==
        stats.wins + stats.losses + stats.draws;
}

bool selector_recipe_exact(
    const BotBenchmarkSummary& summary,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    std::uint64_t selector_seed) {
    const auto bot_exact =
        [](const BotConfig& bot,
           const std::shared_ptr<const LearnedModel>& model) {
            return bot.kind == BotKind::Learned &&
                bot.learned_variant ==
                    LearnedVariant::
                        ValueSearchChampion &&
                bot.rollouts_per_action ==
                    g1::kBaseWorlds &&
                bot.exploration_rate == 0.0 &&
                bot.value_continuation_epsilon == 0.0 &&
                bot.value_priority_residual_weight == 0.0 &&
                !bot.value_pass_dominance &&
                bot.value_resolved_shallow_prior_weight ==
                    0.0 &&
                !bot.value_adversarial_blocks &&
                !bot.value_actor_local_search &&
                !bot.value_recursive_policy_improvement &&
                bot.value_continuation_controller ==
                    LearnedContinuationController::Legacy &&
                bot.training_games == 800 &&
                bot.learned_model == model;
        };
    return summary.evaluation_seed == selector_seed &&
        summary.learned_training_seed == 424242 &&
        summary.repetitions_per_deck_pairing ==
            g1::kSelectorRepetitions &&
        summary.total_games ==
            g1::kExpectedSelectorGames &&
        bot_exact(summary.challenger, candidate) &&
        bot_exact(summary.baseline, parent) &&
        summary.challenger_model_fingerprint ==
            learned_model_fingerprint(candidate) &&
        summary.baseline_model_fingerprint ==
            learned_model_fingerprint(parent);
}

bool generic_candidate_authenticated(
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    bool derivation_authenticated) {
    return derivation_authenticated &&
        parent && candidate &&
        learned_model_fingerprint(parent) ==
            dbc::kRequiredParentFingerprint &&
        learned_critic_schema(parent) ==
            LearnedCriticSchema::LegacyStateOnly &&
        learned_critic_schema(candidate) ==
            LearnedCriticSchema::LegacyStateOnly &&
        learned_model_fingerprint(candidate) !=
            learned_model_fingerprint(parent);
}

bool selector_balance_exact(
    const BotBenchmarkSummary& summary) {
    if (!accounted(summary.challenger_stats) ||
        !accounted(summary.baseline_stats) ||
        summary.challenger_stats.games !=
            g1::kExpectedSelectorGames ||
        summary.baseline_stats.games !=
            g1::kExpectedSelectorGames ||
        summary.challenger_stats.wins !=
            summary.baseline_stats.losses ||
        summary.challenger_stats.losses !=
            summary.baseline_stats.wins ||
        summary.challenger_stats.draws !=
            summary.baseline_stats.draws ||
        summary.life_total_finishes +
                summary.empty_library_finishes +
                summary.turn_limit_draws !=
            g1::kExpectedSelectorGames ||
        summary.challenger_quartet_cr1.clusters != 15 ||
        summary.challenger_quartet_cr1.records !=
            g1::kExpectedSelectorGames) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& challenger =
            summary.challenger_decks[deck];
        const auto& baseline =
            summary.baseline_decks[deck];
        if (!accounted(challenger) ||
            !accounted(baseline) ||
            challenger.games !=
                g1::kExpectedSelectorGamesPerDeck ||
            baseline.games !=
                g1::kExpectedSelectorGamesPerDeck ||
            challenger.on_play_games != 6 ||
            challenger.on_draw_games != 6 ||
            baseline.on_play_games != 6 ||
            baseline.on_draw_games != 6) {
            return false;
        }
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                if (summary.challenger_outcome_quadrants
                            [deck][seat][play_draw]
                                .games != 3 ||
                    summary.baseline_outcome_quadrants
                            [deck][seat][play_draw]
                                .games != 3) {
                    return false;
                }
            }
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const std::size_t expected =
                deck == opponent ? 4 : 2;
            if (summary.challenger_deck_matchups
                        [deck][opponent]
                            .games != expected) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool MechanismReport::mechanism_supported() const {
    return candidate_derivation_authenticated &&
        exact_configuration &&
        common_seed_contract &&
        exact_nine_root_census &&
        all_invariants_green &&
        all_controls_green &&
        no_parent_correct_repair_regression &&
        repairs_correct >= 3;
}

bool MechanismReport::selector_licensed() const {
    return mechanism_supported() &&
        repairs_correct == 4;
}

MechanismReport run_mechanism_gate(
    std::shared_ptr<const LearnedModel> parent,
    const dbc::FitReport& fit) {
    return run_candidate_mechanism_gate(
        parent, fit.candidate,
        fitted_candidate_authenticated(parent, fit));
}

MechanismReport run_candidate_mechanism_gate(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    bool derivation_authenticated) {
    MechanismReport report{
        .parent_fingerprint =
            learned_model_fingerprint(parent),
        .candidate_fingerprint =
            learned_model_fingerprint(candidate),
        .candidate_derivation_authenticated =
            generic_candidate_authenticated(
                parent, candidate,
                derivation_authenticated),
        .exact_configuration =
            kMechanismSimulations ==
                diagnostic::kSmallBudget &&
            puct::kMaximumDecisionPlies == 8 &&
            puct::kMaximumNodeCount == 513 &&
            puct::kMaximumExpandedEdgeCount == 512 &&
            puct::kExplorationConstant == 1.0,
    };
    if (!report.candidate_derivation_authenticated) {
        throw std::invalid_argument(
            "AQ10-DBC1 mechanism gate received an "
            "unauthenticated candidate");
    }

    const auto roots = diagnostic::diagnostic_roots();
    report.roots.reserve(roots.size());
    std::set<std::string> seen;
    report.common_seed_contract = true;
    report.no_parent_correct_repair_regression = true;
    for (const auto& root : roots) {
        MechanismRootReport row{
            .stable_id = root.stable_id,
            .repair =
                diagnostic::terminal_scale_repair_root(
                    root.stable_id),
            .parent =
                isp0::run_root_evidence(
                    root, parent, kMechanismSeed,
                    kMechanismSimulations,
                    LearnedTerminalUtilityMode::
                        C16DiscountedAbsoluteTurn),
            .candidate =
                isp0::
                    run_output_calibrated_candidate_root_evidence(
                        root, candidate,
                        kMechanismSeed,
                        kMechanismSimulations,
                        LearnedTerminalUtilityMode::
                            C16DiscountedAbsoluteTurn),
        };
        row.parent_correct =
            diagnostic::semantic_direction_passed(
                row.stable_id, row.parent);
        row.candidate_correct =
            diagnostic::semantic_direction_passed(
                row.stable_id, row.candidate);
        row.parent_invariants =
            row.parent.evidence_gate_passed();
        row.candidate_invariants =
            row.candidate.evidence_gate_passed();
        report.common_seed_contract =
            report.common_seed_contract &&
            row.parent.search_seed != 0 &&
            row.parent.tie_seed != 0 &&
            row.parent.search_seed ==
                row.candidate.search_seed &&
            row.parent.tie_seed ==
                row.candidate.tie_seed &&
            row.parent.requested_simulations ==
                kMechanismSimulations &&
            row.candidate.requested_simulations ==
                kMechanismSimulations &&
            row.parent.terminal_utility_mode ==
                LearnedTerminalUtilityMode::
                    C16DiscountedAbsoluteTurn &&
            row.candidate.terminal_utility_mode ==
                LearnedTerminalUtilityMode::
                    C16DiscountedAbsoluteTurn;
        if (row.repair) {
            report.repairs_correct +=
                row.candidate_correct ? 1U : 0U;
            report.no_parent_correct_repair_regression =
                report
                    .no_parent_correct_repair_regression &&
                (!row.parent_correct ||
                 row.candidate_correct);
        } else {
            report.controls_correct +=
                row.candidate_correct ? 1U : 0U;
        }
        seen.insert(row.stable_id);
        report.roots.push_back(std::move(row));
    }
    report.parent_opponent =
        isp0::run_opponent_noninterference_evidence(
            roots, parent, kMechanismSeed,
            LearnedTerminalUtilityMode::
                C16DiscountedAbsoluteTurn);
    report.candidate_opponent =
        isp0::
            run_output_calibrated_candidate_opponent_noninterference_evidence(
                roots, candidate, kMechanismSeed,
                LearnedTerminalUtilityMode::
                    C16DiscountedAbsoluteTurn);
    report.exact_nine_root_census =
        report.roots.size() == diagnostic::kRootCount &&
        seen.size() == diagnostic::kRootCount;
    report.all_invariants_green =
        report.parent_opponent.gate_passed() &&
        report.candidate_opponent.gate_passed() &&
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [](const MechanismRootReport& row) {
                return row.parent_invariants &&
                    row.candidate_invariants;
            });
    report.all_controls_green =
        report.controls_correct == 5;
    return report;
}

void print_mechanism_report(
    std::ostream& output,
    const MechanismReport& report) {
    output
        << "mechanism seed=" << kMechanismSeed
        << " simulations=" << kMechanismSimulations
        << " parent=" << report.parent_fingerprint
        << " candidate=" << report.candidate_fingerprint
        << '\n';
    for (const auto& row : report.roots) {
        output
            << "mechanism_root id=" << row.stable_id
            << " role=" << (row.repair ? "repair" : "control")
            << " parent_selected=" << row.parent.selected_key
            << " candidate_selected="
            << row.candidate.selected_key
            << " parent_correct=" << row.parent_correct
            << " candidate_correct=" << row.candidate_correct
            << " invariants="
            << (row.parent_invariants &&
                row.candidate_invariants)
            << '\n';
    }
    output
        << "mechanism_gate repairs="
        << report.repairs_correct << "/4 controls="
        << report.controls_correct << "/5 invariants="
        << report.all_invariants_green
        << " no_parent_correct_repair_regression="
        << report.no_parent_correct_repair_regression
        << " mechanism_supported="
        << report.mechanism_supported()
        << " selector_licensed="
        << report.selector_licensed() << '\n';
}

SelectorReport run_selector(
    std::shared_ptr<const LearnedModel> parent,
    const dbc::FitReport& fit) {
    return run_candidate_selector(
        parent, fit.candidate, kSelectorSeed,
        fitted_candidate_authenticated(parent, fit));
}

SelectorReport run_candidate_selector(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    std::uint64_t selector_seed,
    bool derivation_authenticated) {
    SelectorReport report{
        .candidate_derivation_authenticated =
            generic_candidate_authenticated(
                parent, candidate,
                derivation_authenticated),
    };
    if (!report.candidate_derivation_authenticated) {
        throw std::invalid_argument(
            "AQ10-DBC1 selector received an "
            "unauthenticated candidate");
    }
    GameConfig game;
    game.max_turns = 500;
    game.learned_training_seed = 424242;
    game.learned_search_depth = 1;
    report.summary =
        run_bot_benchmark(
            g1::kSelectorRepetitions,
            selector_seed,
            g1::selector_bot_config(
                candidate, 0.0),
            g1::selector_bot_config(parent, 0.0),
            game, false);
    report.exact_recipe =
        selector_recipe_exact(
            report.summary, parent, candidate,
            selector_seed);
    report.exact_balance =
        selector_balance_exact(report.summary);
    report.every_deck_floor = true;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        report.deck_wins[deck] =
            report.summary.challenger_decks[deck].wins;
        report.every_deck_floor =
            report.every_deck_floor &&
            report.deck_wins[deck] >= 3;
    }
    report.pilot_licensed =
        report.candidate_derivation_authenticated &&
        report.exact_recipe &&
        report.exact_balance &&
        report.summary.challenger_stats.wins > 30 &&
        report.every_deck_floor;
    report.fast_go =
        report.pilot_licensed &&
        report.summary.challenger_stats.wins >= 37;
    return report;
}

void print_selector_report(
    std::ostream& output,
    const SelectorReport& report) {
    output
        << "selector seed="
        << report.summary.evaluation_seed
        << " wins=" << report.summary.challenger_stats.wins
        << " losses="
        << report.summary.challenger_stats.losses
        << " draws=" << report.summary.challenger_stats.draws
        << " recipe=" << report.exact_recipe
        << " balance=" << report.exact_balance
        << '\n';
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& stats =
            report.summary.challenger_decks[deck];
        output
            << "selector_deck deck="
            << deck_name(static_cast<DeckId>(deck))
            << " wins=" << stats.wins
            << " losses=" << stats.losses
            << " draws=" << stats.draws << '\n';
    }
    output
        << "selector_gate every_deck_floor="
        << report.every_deck_floor
        << " pilot_licensed=" << report.pilot_licensed
        << " fast_go=" << report.fast_go << '\n';
}

} // namespace old_school::decision_boundary_critic_gate
