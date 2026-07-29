#include "old_school/action_q_nested_actor_early_stop.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq =
    old_school::action_q_nested_actor_early_stop;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

aq::g1::Metrics metrics(
    std::size_t roots, std::size_t options,
    double agreement, double regret) {
    aq::g1::Metrics result;
    result.roots = roots;
    result.options = options;
    result.equal_deck_top_one_expected_agreement =
        agreement;
    result.equal_deck_mean_regret = regret;
    return result;
}

aq::g1::FitReport frozen_control_fit() {
    aq::g1::FitReport fit;
    fit.candidate_fingerprint =
        std::string(aq::kExpectedControlFingerprint);
    fit.optimizer =
        aq::optimizer_for_epochs(aq::kControlEpochs);
    fit.fit_examples = aq::kExpectedFitRoots;
    fit.fit_options = aq::kExpectedFitOptions;
    fit.parent_fit = metrics(
        aq::kExpectedFitRoots,
        aq::kExpectedFitOptions,
        aq::kExpectedParentFitAgreement,
        aq::kExpectedParentFitRegret);
    fit.candidate_fit = metrics(
        aq::kExpectedFitRoots,
        aq::kExpectedFitOptions,
        aq::kExpectedControlFitAgreement,
        aq::kExpectedControlFitRegret);
    fit.parent_check = metrics(
        aq::kExpectedDevRoots,
        aq::kExpectedDevOptions,
        aq::kExpectedParentDevAgreement,
        aq::kExpectedParentDevRegret);
    fit.candidate_check = metrics(
        aq::kExpectedDevRoots,
        aq::kExpectedDevOptions,
        aq::kExpectedControlDevAgreement,
        aq::kExpectedControlDevRegret);
    return fit;
}

old_school::action_q_offline_gate::AncestralGate
passing_ancestral() {
    using old_school::CardId;
    using old_school::PriorityAction;
    using old_school::Target;
    old_school::action_q_offline_gate::AncestralGate gate;
    gate.self_score = 0.9;
    gate.opponent_score = 0.1;
    gate.legal_actions = {
        PriorityAction::pass(),
        PriorityAction::play_land(CardId::Island),
        PriorityAction::cast_artifact(CardId::SolRing),
        PriorityAction::cast_ancestral_recall(
            Target::player_target(0)),
        PriorityAction::cast_ancestral_recall(
            Target::player_target(1)),
    };
    gate.selected_support = {
        PriorityAction::cast_ancestral_recall(
            Target::player_target(0)),
    };
    gate.information_action_fingerprint =
        std::string(
            old_school::action_q_offline_gate::
                kAncestralInformationActionFingerprint);
    gate.complete_legal_actions_exact = true;
    gate.information_action_fingerprint_exact = true;
    gate.hidden_repartition_bit_identical = true;
    gate.self_strictly_above_opponent = true;
    gate.opponent_absent_from_support = true;
    expect(
        gate.gate_passed(),
        "AQ4-G2 test Ancestral fixture is invalid");
    return gate;
}

aq::g1::OfflineReport passing_offline() {
    aq::g1::OfflineReport report;
    report.parent_immutable = true;
    report.repeated_fit_bit_identical = true;
    report.only_priority_component_changed = true;
    report.fit_regret_strictly_improved = true;
    report.check_regret_strictly_improved = true;
    report.check_deck_regret_guard.fill(true);
    report.descriptor_order_identity = true;
    report.redundant_counter_pass = true;
    report.braingeyser_x_zero_excluded = true;
    report.sick_bear_growth_pass = true;
    report.live_force_spike = true;
    report.ancestral = passing_ancestral();
    expect(
        report.gate_passed(),
        "AQ4-G2 test offline fixture is invalid");
    return report;
}

aq::PrefixReport prefix(
    std::size_t epochs, double dev_regret) {
    aq::PrefixReport report;
    report.epochs = epochs;
    report.fit.optimizer =
        aq::optimizer_for_epochs(epochs);
    report.fit.parent_immutable = true;
    report.fit.repeated_fit_bit_identical = true;
    report.fit.only_priority_component_changed = true;
    report.fit.candidate_check
        .equal_deck_mean_regret = dev_regret;
    report.offline = passing_offline();
    report.offline.candidate_check =
        report.fit.candidate_check;
    report.ancestral_eligible =
        report.offline.ancestral.gate_passed();
    return report;
}

aq::g1::Corpus digest_corpus() {
    aq::g1::ManifestRoot manifest;
    manifest.coordinate.schedule_index = 3;
    manifest.coordinate.actor = 1;
    manifest.coordinate.nontrivial_ordinal = 7;
    manifest.coordinate.search_seed = 99;
    manifest.actions = {
        old_school::PriorityAction::pass(),
    };
    manifest.action_descriptors = {"pass"};
    manifest.options = {{0.25, -0.5}};

    aq::g1::RootExample example;
    example.manifest = manifest;
    example.base_scores = {0.2};
    example.teacher_scores = {0.8};
    example.target_probabilities = {1.0};
    example.accounting = {
        .base_sampled_worlds = 8,
        .base_rollout_evaluations = 8,
        .base_terminal_evaluations = 1,
        .base_bootstrapped_evaluations = 7,
        .teacher_sampled_worlds = 8,
        .teacher_rollout_evaluations = 8,
        .teacher_terminal_evaluations = 2,
        .teacher_bootstrapped_evaluations = 6,
        .teacher_inner_rollout_evaluations = 24,
        .teacher_inner_search_invocations = 12,
        .teacher_inner_search_max_depth = 1,
    };
    example.weight = 0.5;

    aq::g1::Corpus corpus;
    corpus.census.manifest_hash = "owner-safe-manifest";
    corpus.census.roots = {manifest};
    corpus.fit = {example};
    return corpus;
}

void test_cli_is_sealed_to_run() {
    const std::vector<std::string_view> run{"--run"};
    const std::vector<std::string_view> empty;
    const std::vector<std::string_view> census{"--census"};
    const std::vector<std::string_view> extra{
        "--run",
        "--again",
    };
    expect(
        aq::parse_command(run) == aq::Command::Run &&
            !aq::parse_command(empty).has_value() &&
            !aq::parse_command(census).has_value() &&
            !aq::parse_command(extra).has_value(),
        "AQ4-G2 CLI accepted an undeclared shape");
    std::ostringstream usage;
    aq::print_usage(usage);
    expect(
        usage.str() ==
            "Usage: old-school-action-q-nested-actor-early-stop "
            "--run\n",
        "AQ4-G2 usage drifted");
}

void test_epoch_ladder_is_exact() {
    constexpr std::array<std::size_t, 5> kAllEpochs{
        4, 8, 16, 32, 64,
    };
    expect(
        aq::kPrefixEpochs ==
                std::array<std::size_t, 4>{4, 8, 16, 32} &&
            aq::kControlEpochs == 64,
        "AQ4-G2 epoch ladder constants drifted");
    for (const std::size_t epochs : kAllEpochs) {
        auto expected = aq::g1::optimizer_config();
        expected.epochs = epochs;
        expect(
            aq::is_declared_epoch(epochs) &&
                aq::optimizer_for_epochs(epochs) ==
                    expected,
            "AQ4-G2 declared epoch changed optimizer geometry");
    }
    expect(
        !aq::is_declared_epoch(0) &&
            !aq::is_declared_epoch(1) &&
            !aq::is_declared_epoch(48),
        "AQ4-G2 accepted an undeclared epoch");
    expect_rejected(
        [] {
            static_cast<void>(
                aq::optimizer_for_epochs(48));
        },
        "AQ4-G2 built an optimizer for epoch 48");
}

void test_control_constants_are_bit_exact() {
    const aq::g1::FitReport fit = frozen_control_fit();
    expect(
        aq::kExpectedControlFingerprint.size() == 64 &&
            aq::kExpectedFitRoots == 320 &&
            aq::kExpectedFitOptions == 1023 &&
            aq::kExpectedDevRoots == 319 &&
            aq::kExpectedDevOptions == 1018 &&
            aq::control_matches_frozen_result(fit),
        "AQ4-G2 rejected the exact epoch-64 control");

    auto changed = fit;
    changed.candidate_fingerprint[0] =
        changed.candidate_fingerprint[0] == '0'
            ? '1'
            : '0';
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G2 control omitted candidate identity");
    changed = fit;
    changed.candidate_check.equal_deck_mean_regret =
        std::nextafter(
            changed.candidate_check
                .equal_deck_mean_regret,
            1.0);
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G2 control accepted a one-bit metric drift");
    changed = fit;
    --changed.fit_options;
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G2 control omitted corpus counts");
    changed = fit;
    changed.optimizer =
        aq::optimizer_for_epochs(32);
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G2 control omitted the 64-epoch optimizer");
    changed = fit;
    changed.optimizer.learning_rate =
        std::nextafter(
            changed.optimizer.learning_rate, 1.0);
    expect(
        !aq::control_matches_frozen_result(changed),
        "AQ4-G2 control omitted optimizer geometry");

    const aq::ControlReport forged{
        .fit = fit,
        .fingerprint_exact = true,
        .aggregate_metrics_bit_exact = true,
        .corpus_counts_exact = true,
    };
    expect(
        !forged.gate_passed(),
        "AQ4-G2 control trusted report bits without a "
        "bound candidate");
}

void test_prefix_selection_and_tie_break_are_exact() {
    std::vector<aq::PrefixReport> reports{
        prefix(4, 0.02),
        prefix(8, 0.01),
        prefix(16, 0.01),
        prefix(32, 0.03),
    };
    expect(
        aq::prefix_family_is_exact(reports) &&
            reports[0].eligible() &&
            aq::select_prefix(reports) ==
                std::optional<std::size_t>(1),
        "AQ4-G2 did not break a DEV tie toward fewer epochs");
    reports[1].ancestral_eligible = false;
    expect(
        aq::select_prefix(reports) ==
            std::optional<std::size_t>(2),
        "AQ4-G2 selected an Ancestral-ineligible prefix");
    for (auto& report : reports) {
        report.ancestral_eligible = false;
    }
    expect(
        !aq::select_prefix(reports).has_value(),
        "AQ4-G2 selected from an ineligible family");

    aq::PrefixReport mismatch = prefix(4, 0.01);
    mismatch.fit.optimizer =
        aq::optimizer_for_epochs(8);
    expect(
        !mismatch.eligible(),
        "AQ4-G2 ignored an epoch/optimizer mismatch");
    mismatch = prefix(4, 0.01);
    mismatch.fit.optimizer.learning_rate =
        std::nextafter(
            mismatch.fit.optimizer.learning_rate,
            1.0);
    expect(
        !mismatch.eligible(),
        "AQ4-G2 ignored prefix optimizer geometry");

    auto missing = reports;
    missing.pop_back();
    expect(
        !aq::prefix_family_is_exact(missing),
        "AQ4-G2 accepted a missing prefix");
    auto duplicate = reports;
    duplicate[2] = prefix(8, 0.02);
    expect(
        !aq::prefix_family_is_exact(duplicate),
        "AQ4-G2 accepted a duplicate prefix");
    auto undeclared = reports;
    undeclared[2].epochs = 48;
    expect(
        !aq::prefix_family_is_exact(undeclared),
        "AQ4-G2 accepted an undeclared prefix");
    auto optimizer_tamper = reports;
    optimizer_tamper[2].fit.optimizer.learning_rate =
        std::nextafter(
            optimizer_tamper[2]
                .fit.optimizer.learning_rate,
            1.0);
    expect(
        !aq::prefix_family_is_exact(optimizer_tamper),
        "AQ4-G2 family omitted optimizer identity");

    aq::OfflineRunReport missing_report;
    missing_report.prefixes = missing;
    expect(
        !missing_report.selection_ready(),
        "AQ4-G2 selection_ready accepted missing epochs");
    aq::OfflineRunReport duplicate_report;
    duplicate_report.prefixes = duplicate;
    expect(
        !duplicate_report.selection_ready(),
        "AQ4-G2 selection_ready accepted duplicate epochs");
    aq::OfflineRunReport undeclared_report;
    undeclared_report.prefixes = undeclared;
    expect(
        !undeclared_report.selection_ready(),
        "AQ4-G2 selection_ready accepted undeclared epochs");
}

void test_ancestral_gate_is_conjunctive() {
    aq::PrefixReport report = prefix(4, 0.01);
    expect(
        report.eligible(),
        "AQ4-G2 rejected a fully eligible prefix");
    report.ancestral_eligible = false;
    expect(
        !report.eligible(),
        "AQ4-G2 omitted explicit Ancestral eligibility");
    report = prefix(4, 0.01);
    report.offline.ancestral
        .hidden_repartition_bit_identical = false;
    expect(
        !report.offline.ancestral.gate_passed() &&
            !report.eligible(),
        "AQ4-G2 did not bind the complete Ancestral gate");
}

void test_corpus_digest_is_tamper_sensitive() {
    const aq::g1::Corpus corpus = digest_corpus();
    const std::string digest =
        aq::canonical_corpus_digest(corpus);
    expect(
        digest.size() == 64 &&
            digest ==
                aq::canonical_corpus_digest(corpus),
        "AQ4-G2 corpus digest is not deterministic");

    auto score_tamper = corpus;
    score_tamper.fit.front().teacher_scores.front() =
        std::nextafter(
            score_tamper.fit.front()
                .teacher_scores.front(),
            1.0);
    expect(
        aq::canonical_corpus_digest(score_tamper) !=
            digest,
        "AQ4-G2 corpus digest omitted a teacher scalar");

    auto manifest_tamper = corpus;
    ++manifest_tamper.census.roots.front()
          .coordinate.trace_ordinal;
    expect(
        aq::canonical_corpus_digest(manifest_tamper) !=
            digest,
        "AQ4-G2 corpus digest omitted its census manifest");

    auto feature_tamper = corpus;
    feature_tamper.fit.front()
        .manifest.options.front().front() =
        std::nextafter(
            feature_tamper.fit.front()
                .manifest.options.front().front(),
            1.0);
    expect(
        aq::canonical_corpus_digest(feature_tamper) !=
            digest,
        "AQ4-G2 corpus digest omitted an owner-safe feature");

    auto base_tamper = corpus;
    base_tamper.fit.front().base_scores.front() =
        std::nextafter(
            base_tamper.fit.front().base_scores.front(),
            1.0);
    expect(
        aq::canonical_corpus_digest(base_tamper) !=
            digest,
        "AQ4-G2 corpus digest omitted a base scalar");

    auto target_tamper = corpus;
    target_tamper.fit.front()
        .target_probabilities.front() =
        std::nextafter(
            target_tamper.fit.front()
                .target_probabilities.front(),
            0.0);
    expect(
        aq::canonical_corpus_digest(target_tamper) !=
            digest,
        "AQ4-G2 corpus digest omitted a target scalar");

    auto weight_tamper = corpus;
    weight_tamper.fit.front().weight =
        std::nextafter(
            weight_tamper.fit.front().weight, 1.0);
    expect(
        aq::canonical_corpus_digest(weight_tamper) !=
            digest,
        "AQ4-G2 corpus digest omitted example weight");

    auto accounting_tamper = corpus;
    ++accounting_tamper.fit.front()
          .accounting.teacher_inner_search_invocations;
    expect(
        aq::canonical_corpus_digest(
            accounting_tamper) != digest,
        "AQ4-G2 corpus digest omitted search accounting");
}

void test_selector_is_fresh_and_authorized() {
    expect(
        aq::kSelectorSeed == 202607282131ULL &&
            aq::kSelectorSeed != aq::g1::kSelectorSeed,
        "AQ4-G2 selector seed is not fresh");
    const aq::OfflineRunReport report;
    expect(
        !report.selection_ready() &&
            report.selected() == nullptr,
        "AQ4-G2 default report authorized selection");
    expect_rejected(
        [&] {
            static_cast<void>(
                aq::run_selector(
                    std::shared_ptr<
                        const old_school::LearnedModel>{},
                    report));
        },
        "AQ4-G2 selector opened without an authorized report");
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&passed](std::string_view name, auto test) {
            try {
                test();
                ++passed;
            } catch (const std::exception& error) {
                std::cerr
                    << "FAIL " << name << ": "
                    << error.what() << '\n';
                throw;
            }
        };
    run("cli_is_sealed_to_run", test_cli_is_sealed_to_run);
    run("epoch_ladder_is_exact", test_epoch_ladder_is_exact);
    run(
        "control_constants_are_bit_exact",
        test_control_constants_are_bit_exact);
    run(
        "prefix_selection_and_tie_break_are_exact",
        test_prefix_selection_and_tie_break_are_exact);
    run(
        "ancestral_gate_is_conjunctive",
        test_ancestral_gate_is_conjunctive);
    run(
        "corpus_digest_is_tamper_sensitive",
        test_corpus_digest_is_tamper_sensitive);
    run(
        "selector_is_fresh_and_authorized",
        test_selector_is_fresh_and_authorized);
    std::cout
        << "action-Q nested-actor early-stop tests: "
        << passed << "/7 passed\n";
    return 0;
}
