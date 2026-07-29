#include "old_school/action_q_bellman_explore.hpp"

#include "old_school/action_q_bellman_teacher.hpp"
#include "old_school/action_q_field_gate.hpp"

#include <algorithm>
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

namespace aq = old_school::action_q_bellman_explore;
namespace teacher = old_school::action_q_bellman_teacher;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::uint64_t kReservedGameplaySeed =
    202607281831ULL;

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
            "AQ1 loaded parent fingerprint drifted");
    }
    return parent;
}

void run_teacher_preflight(
    const std::shared_ptr<const old_school::LearnedModel>&
        parent) {
    const auto root =
        old_school::action_q_field_gate::
            make_ancestral_field_root();
    const auto hidden =
        old_school::action_q_field_gate::
            hidden_repartition_clone(root);
    if (root.state == hidden.state ||
        !old_school::action_q_field_gate::
            has_required_action_identities(root) ||
        !old_school::action_q_field_gate::
            has_required_action_identities(hidden)) {
        throw std::runtime_error(
            "AQ1 hidden-repartition witness is vacuous");
    }
    const std::uint64_t seed =
        aq::root_search_seed(
            aq::kFitBlock, 0, root.actor, 0);
    const auto direct =
        teacher::score_priority_root(
            root.state, root.original_decks,
            root.context, root.legal_actions,
            parent, seed);
    auto reversed_actions = root.legal_actions;
    std::reverse(
        reversed_actions.begin(),
        reversed_actions.end());
    const auto reversed =
        teacher::score_priority_root(
            root.state, root.original_decks,
            root.context, reversed_actions,
            parent, seed);
    const auto hidden_direct =
        teacher::score_priority_root(
            hidden.state, hidden.original_decks,
            hidden.context, hidden.legal_actions,
            parent, seed);
    if (direct != reversed ||
        direct != hidden_direct) {
        throw std::runtime_error(
            "AQ1 teacher changed under descriptor order or "
            "hidden repartition");
    }
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
        << "schema=old-school-action-q-aq1-bl0-run-v1\n"
        << "mode=run\n"
        << "parent_artifact=" << kParentArtifactPath << '\n'
        << "parent_fingerprint="
        << report.parent_fingerprint << '\n'
        << "candidate_fingerprint="
        << report.candidate_fingerprint << '\n'
        << "root_seed=" << corpus.root_seed << '\n'
        << "fit_seed=" << aq::fit_seed() << '\n'
        << "reserved_gameplay_seed="
        << kReservedGameplaySeed << '\n'
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
        << " sick_bear_growth_pass="
        << report.model.behavior
               .sick_bear_growth_selects_pass
        << '\n';
}

int run_census() {
    const auto parent = load_parent();
    run_teacher_preflight(parent);
    const aq::Corpus corpus = aq::collect_corpus(parent);
    std::cout
        << "schema=old-school-action-q-aq1-bl0-census-v1\n"
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
        << " root_worlds="
        << teacher::kRootWorlds
        << " successor_banks=2"
        << " successor_worlds_per_bank="
        << teacher::kSuccessorWorlds
        << " root_cap="
        << aq::kMaximumRootsPerActorGame
        << " source_turn_cap="
        << aq::kSourceTurnCap << '\n'
        << "preflight descriptor_order_bit_identical=1"
        << " hidden_repartition_bit_identical=1"
        << " hidden_repartition_nonvacuous=1\n";
    aq::print_census(std::cout, corpus);
    std::cout
        << "result=PASS disposition=CENSUS_ONLY"
        << " model_created=0 gameplay_seed_opened=0\n";
    return 0;
}

int run_experiment() {
    using Clock = std::chrono::steady_clock;
    const auto parent = load_parent();
    run_teacher_preflight(parent);
    const auto collection_start = Clock::now();
    const aq::Corpus corpus = aq::collect_corpus(parent);
    const auto collection_end = Clock::now();
    aq::require_frozen_census(corpus);
    const auto fit_start = Clock::now();
    const aq::FitReport fit = aq::fit(corpus, parent);
    const auto fit_end = Clock::now();
    if (!fit.candidate) {
        throw std::runtime_error(
            "AQ1 fit produced a null candidate");
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
            << " gameplay_seed_opened=0"
            << " artifact_published=0\n";
        return 1;
    }
    // Deliberately stop at the preregistered human checkpoint.  The fresh
    // selector seed remains unopened until this output is independently
    // reviewed and the root agent explicitly authorizes the selector.
    std::cout
        << "result=PASS stage=OFFLINE"
        << " disposition=AWAITING_SELECTOR_AUTHORIZATION"
        << " gameplay_seed_opened=0"
        << " artifact_published=0\n";
    return 0;
}

int run_teacher_diagnostic() {
    const auto parent = load_parent();
    const aq::TeacherDiagnosticReport report =
        aq::diagnose_teacher(parent);
    aq::print_teacher_diagnostic_report(
        std::cout, report);
    return report.gate_passed() ? 0 : 1;
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
        if (*command == aq::Command::Run) {
            return run_experiment();
        }
        return run_teacher_diagnostic();
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR"
            << " reason=action_q_bellman_explore_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
