#include "old_school/joint_c17_orchestration.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

namespace orchestration =
    old_school::joint_c17_orchestration;
namespace eval = old_school::joint_c17_eval;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void test_exact_preregistered_stage_plan() {
    const auto& plan = orchestration::canonical_stage_plan();
    expect(
        plan.holdout_seed ==
            old_school::kLearnedJointC17HoldoutSeed,
        "holdout seed");
    expect(
        plan.holdout_generation == 18,
        "holdout generation");
    expect(
        plan.holdout_balanced_blocks == 5,
        "holdout blocks");
    expect(
        plan.maximum_game_turns == 500,
        "maximum turns");
    expect(
        plan.training_games == 800,
        "training games");

    expect(
        plan.direct[0] ==
            orchestration::DirectStagePlan{
                .role =
                    eval::DirectPanelRole::
                        TreatmentVsControl,
                .seed =
                    old_school::
                        kLearnedJointC17MatchedControlGameplaySeed,
                .repetitions = 34,
            },
        "treatment/control stage");
    expect(
        plan.direct[1] ==
            orchestration::DirectStagePlan{
                .role =
                    eval::DirectPanelRole::
                        TreatmentVsParent,
                .seed =
                    old_school::
                        kLearnedJointC17FrozenC16GameplaySeed,
                .repetitions = 34,
            },
        "treatment/parent stage");
    expect(
        plan.direct[2] ==
            orchestration::DirectStagePlan{
                .role =
                    eval::DirectPanelRole::
                        TreatmentVsHandcodedPrimary,
                .seed =
                    old_school::
                        kLearnedJointC17HandcodedGameplaySeed,
                .repetitions = 34,
            },
        "treatment/Handcoded stage");
    expect(
        plan.fixed_seed_panel_seeds ==
            eval::kFixedSeedPanelSeeds,
        "fixed seed order");
    expect(
        plan.fixed_seed_panel_repetitions == 5,
        "fixed seed repetitions");
    expect(
        plan.mixed_field_games_per_matchup == 100,
        "mixed-field games");
}

void test_stage_plan_keeps_training_and_evaluation_disjoint() {
    const auto& plan = orchestration::canonical_stage_plan();
    expect(
        plan.holdout_seed !=
            old_school::kLearnedJointC17ShardSeed,
        "holdout reused shard seed");
    for (const auto& stage : plan.direct) {
        expect(
            stage.seed !=
                    old_school::kLearnedJointC17ShardSeed &&
                stage.seed != plan.holdout_seed,
            "direct stage reused training or holdout seed");
    }
    for (const std::uint64_t seed :
         plan.fixed_seed_panel_seeds) {
        expect(
            seed !=
                    old_school::kLearnedJointC17ShardSeed &&
                seed != plan.holdout_seed,
            "fixed panel reused training or holdout seed");
        for (const auto& stage : plan.direct) {
            expect(
                seed != stage.seed,
                "fixed panel reused primary gameplay seed");
        }
    }
}

void test_public_stage_api_requires_canonical_context() {
    using Context =
        old_school::joint_c17_execution::
            CanonicalJointC17Context;
    using RunFunction =
        orchestration::ProductionEvaluationOutcome (*)(
            std::ostream&);

    static_assert(
        std::is_same_v<
            decltype(
                &orchestration::
                    run_production_evaluation),
            RunFunction>);
    static_assert(!std::is_default_constructible_v<Context>);
    static_assert(
        !std::is_constructible_v<
            Context,
            std::shared_ptr<const old_school::LearnedModel>>);
}

void test_exit_dispositions_are_distinct() {
    expect(
        orchestration::evaluation_exit_code(
            orchestration::EvaluationDisposition::Accepted) ==
            0,
        "accepted exit code");
    expect(
        orchestration::evaluation_exit_code(
            orchestration::EvaluationDisposition::
                ScientificRejection) == 1,
        "scientific rejection exit code");
    expect(
        orchestration::evaluation_exit_code(
            orchestration::EvaluationDisposition::
                InfrastructureFailure) == 2,
        "infrastructure failure exit code");
}

void test_infrastructure_report_is_machine_parseable() {
    const orchestration::ProductionEvaluationOutcome outcome{
        .disposition =
            orchestration::EvaluationDisposition::
                InfrastructureFailure,
        .result = std::nullopt,
        .infrastructure_error =
            "missing\tbundle\nidentity",
    };
    std::ostringstream human;
    orchestration::write_human_report(outcome, human);
    expect(
        human.str().find("INFRASTRUCTURE_FAILURE") !=
                std::string::npos &&
            human.str().find("missing\tbundle") !=
                std::string::npos,
        "human infrastructure report");

    std::ostringstream tsv;
    orchestration::write_tsv_report(outcome, tsv);
    expect(
        tsv.str().starts_with("C17_J1_TSV_BEGIN\n") &&
            tsv.str().ends_with("C17_J1_TSV_END\n") &&
            tsv.str().find(
                "kind\tscope\tsubject\tseed\tgames\tvalue_1"
                "\tvalue_2\tvalue_3\tvalue_4\tlabel\tstatus\n") !=
                std::string::npos &&
            tsv.str().find("missing bundle identity") !=
                std::string::npos &&
            tsv.str().find("missing\tbundle") ==
                std::string::npos,
        "TSV infrastructure report");
}

std::size_t count_lines_with_prefix(
    std::string_view text, std::string_view prefix) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t end = text.find('\n', offset);
        const std::string_view line = text.substr(
            offset,
            end == std::string_view::npos
                ? text.size() - offset
                : end - offset);
        if (line.starts_with(prefix)) {
            ++count;
        }
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
    }
    return count;
}

void expect_every_tsv_data_row_has_eleven_columns(
    std::string_view text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t end = text.find('\n', offset);
        const std::string_view line = text.substr(
            offset,
            end == std::string_view::npos
                ? text.size() - offset
                : end - offset);
        if (!line.empty() &&
            line != "C17_J1_TSV_BEGIN" &&
            line != "C17_J1_TSV_END") {
            const auto tabs = static_cast<std::size_t>(
                std::count(line.begin(), line.end(), '\t'));
            expect(
                tabs == 10,
                std::string("TSV row does not have 11 columns: ") +
                    std::string(line));
        }
        if (end == std::string_view::npos) {
            break;
        }
        offset = end + 1;
    }
}

old_school::terminal_weight_eval::ClusteredEstimate
estimate(double mean) {
    return {
        .records = 40,
        .clusters = 20,
        .mean = mean,
        .standard_error = 0.01,
        .confidence_lower_95 = mean - 0.02,
        .confidence_upper_95 = mean + 0.02,
    };
}

old_school::terminal_weight_eval::HoldoutScopeMetrics
heldout_scope(std::size_t deck) {
    old_school::terminal_weight_eval::HoldoutScopeMetrics scope;
    scope.records = 100 + deck;
    scope.perspectives = 80;
    scope.physical_games = 40;
    scope.target_mean = 0.5;
    scope.target_variance = 0.2;
    scope.bootstrapped_records = 60;
    scope.terminal_tail_records = 40;
    scope.mean_trace_turn_distance = 8.0;
    scope.minimum_trace_turn_distance = 8;
    scope.maximum_trace_turn_distance = 8;
    for (std::size_t model = 0;
         model <
         old_school::terminal_weight_eval::kCriticModelCount;
         ++model) {
        auto& critic = scope.models[model];
        critic.brier = estimate(0.2 + 0.01 * model);
        critic.soft_log_loss =
            estimate(0.4 + 0.01 * model);
        critic.signed_bias =
            estimate(-0.02 + 0.01 * model);
        critic.prediction_mean = 0.45 + 0.01 * model;
        critic.saturated_predictions = model;
        critic.saturation_fraction = 0.01 * model;
    }
    for (std::size_t comparison = 0;
         comparison < scope.treatment_comparisons.size();
         ++comparison) {
        scope.treatment_comparisons[comparison]
            .brier_delta =
            estimate(-0.01 - 0.001 * comparison);
        scope.treatment_comparisons[comparison]
            .soft_log_loss_delta =
            estimate(-0.02 - 0.001 * comparison);
    }
    return scope;
}

old_school::probe_eval::ProbeMetricSummary
probe_metrics() {
    old_school::probe_eval::ProbeMetricSummary metrics;
    metrics.probe_count = 20;
    metrics.stable_pair_count = 10;
    metrics.top1_expected_agreement = 0.7;
    metrics.stable_pair_agreement = 0.8;
    metrics.mean_regret = 0.1;
    metrics.critic_brier = 0.2;
    metrics.critic_mse = 0.2;
    metrics.critic_log_loss = 0.4;
    metrics.critic_bias = -0.02;
    metrics.critic_ece = 0.03;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& row = metrics.by_deck[deck];
        row.root_deck =
            static_cast<old_school::DeckId>(deck);
        row.probe_count = 4;
        row.stable_pair_count = 2;
        row.top1_expected_agreement = 0.6 + 0.01 * deck;
        row.stable_pair_agreement = 0.7;
        row.mean_regret = 0.1 + 0.01 * deck;
        row.critic_brier = 0.2 + 0.01 * deck;
        row.critic_mse = row.critic_brier;
        row.critic_log_loss = 0.4 + 0.01 * deck;
        row.critic_bias = -0.02 + 0.01 * deck;
        row.critic_ece = 0.03 + 0.01 * deck;
    }
    return metrics;
}

orchestration::DirectGameplayStageResult direct_stage(
    std::uint64_t seed, std::size_t wins) {
    orchestration::DirectGameplayStageResult stage;
    auto& summary = stage.evidence.summary;
    summary.evaluation_seed = seed;
    summary.challenger_stats.games = 100;
    summary.challenger_stats.wins = wins;
    summary.challenger_stats.losses = 100 - wins;
    summary.baseline_stats.games = 100;
    summary.baseline_stats.wins = 100 - wins;
    summary.baseline_stats.losses = wins;
    summary.challenger_quartet_cr1 = {
        .clusters = 25,
        .records = 100,
        .mean = static_cast<double>(wins) / 100.0,
        .standard_error = 0.02,
        .confidence_low_95 = 0.5,
        .confidence_high_95 = 0.58,
    };
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& stats = summary.challenger_decks[deck];
        stats.games = 20;
        stats.wins = 11 + (deck % 2);
        stats.losses = 20 - stats.wins;
        stage.gate.challenger_deck_strict_wins[deck] = true;
    }
    stage.gate.identity_exact = true;
    stage.gate.accounting_exact = true;
    stage.gate.clustered_estimate_valid = true;
    stage.gate.rates_finite = true;
    stage.gate.aggregate_strict_win = true;
    stage.gate.wilson_lower_above_half = true;
    stage.gate.every_challenger_deck_strict_win = true;
    stage.gate.challenger_win_rate_percent =
        static_cast<double>(wins);
    stage.gate.wilson_lower_95_percent = 51.0;
    stage.gate.passed = true;
    return stage;
}

orchestration::ProductionEvaluationOutcome
complete_reporting_fixture() {
    orchestration::CanonicalEvaluationResult result;
    result.sealed.disposition =
        old_school::joint_c17_runner::RunnerDisposition::
            Rejected;
    result.sealed.deployments.computed_control_fingerprint =
        "control-fingerprint";
    result.sealed.deployments.computed_treatment_fingerprint =
        "treatment-fingerprint";
    result.sealed.stages.push_back({
        .stage =
            old_school::joint_c17_runner::RunnerStage::Heldout,
        .disposition =
            old_school::joint_c17_runner::RunnerDisposition::
                Rejected,
        .failures = {"synthetic stage failure"},
    });

    orchestration::HeldoutStageResult heldout;
    heldout.evidence.pooled = heldout_scope(0);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        heldout.evidence.by_deck[deck] =
            heldout_scope(deck);
    }
    heldout.gate.accounting_exact = true;
    heldout.gate.inputs_finite = true;
    heldout.gate.pooled_losses_improved = true;
    heldout.gate.every_deck_loss_guard = true;
    heldout.gate.green_bias_strictly_shrank = true;
    heldout.gate.blue_bias_strictly_shrank = true;
    heldout.gate.ru_bias_guard = true;
    heldout.gate.no_new_material_bias = true;
    heldout.gate.passed = true;
    result.evidence.heldout = std::move(heldout);

    orchestration::DeepReferenceStageResult deep;
    deep.evidence.control.metrics = probe_metrics();
    deep.evidence.treatment.metrics = probe_metrics();
    deep.evidence.hidden_repartition = {
        .passed = true,
        .policy_count = 2,
        .probe_count = 20,
    };
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto deck_id =
            static_cast<old_school::DeckId>(deck);
        const std::string id =
            std::string("probe-") + std::to_string(deck);
        old_school::probe_runner::ValueProbeDecisionDetail
            decision{
                .stable_id = id,
                .root_deck = deck_id,
                .selected_keys = {"best"},
                .deterministic_selection = true,
                .reference_best_set = {"best"},
                .regret = 0.01,
                .critic_prediction = 0.6,
                .selected_action_reference_q = 0.7,
                .critic_error = -0.1,
            };
        deep.evidence.control.decisions.push_back(decision);
        deep.evidence.treatment.decisions.push_back(decision);
        auto& stable = deep.gate.by_deck[deck];
        stable.root_deck = deck_id;
        stable.eligible_probes = 4;
        stable.control_agreements = 4;
        stable.treatment_agreements = 4;
        stable.passed = true;
        auto& common = deep.gate.common_state_critics
                           .by_deck[deck];
        for (auto& model : common.models) {
            model.probe_count = 4;
            model.brier = 0.2;
            model.soft_log_loss = 0.4;
            model.signed_bias = -0.01;
            model.ece = 0.02;
        }
    }
    deep.gate.common_state_critics.accounting_exact = true;
    deep.gate.common_state_critics.predictions_valid = true;
    deep.gate.common_state_critics.metrics_finite = true;
    for (auto& model :
         deep.gate.common_state_critics.pooled.models) {
        model.probe_count = 20;
        model.brier = 0.2;
        model.soft_log_loss = 0.4;
        model.signed_bias = -0.01;
        model.ece = 0.02;
    }
    deep.force_spike.policy_name = "treatment";
    deep.force_spike.model_fingerprint = "treatment-fingerprint";
    deep.force_spike.worlds = 8;
    deep.force_spike.horizon_turns = 4;
    deep.force_spike.live = {
        .stable_id = "force-live",
        .pass_score = 0.4,
        .force_spike_score = 0.6,
        .selected_keys = {"force-spike-gray-ogre"},
    };
    deep.force_spike.payable = {
        .stable_id = "force-payable",
        .pass_score = 0.7,
        .force_spike_score = 0.3,
        .selected_keys = {"pass"},
    };
    deep.gate.force_spike.identities_exact = true;
    deep.gate.force_spike
        .live_uniquely_selects_force_spike = true;
    deep.gate.force_spike.payable_uniquely_selects_pass = true;
    deep.gate.force_spike.hidden_repartition_passed = true;
    deep.gate.force_spike.passed = true;
    deep.gate.accounting_exact = true;
    deep.gate.metrics_finite = true;
    deep.gate.pooled_regret_no_worse = true;
    deep.gate.pooled_top_one_no_lower = true;
    deep.gate.every_deck_regret_guard = true;
    deep.gate.stable_best_set_loss_guard = true;
    deep.gate.required_blue_probes_exact = true;
    deep.gate.required_blue_selections_passed = true;
    deep.gate.hidden_repartition_passed = true;
    deep.gate.passed = true;
    result.evidence.deep_reference = std::move(deep);

    orchestration::FieldRegressionStageResult field;
    old_school::probe_runner::FieldRegressionDecisionReport
        field_decision;
    field_decision.stable_id = "field-fixture";
    field_decision.root_deck = old_school::DeckId::RUAggro;
    field_decision.candidate_descriptors = {
        "No Block", "Block"};
    field_decision.forced_consequences.push_back({
        .descriptor = "No Block",
        .public_state_fingerprint = "public-fingerprint",
    });
    for (auto* policy : {
             &field_decision.parent,
             &field_decision.control,
             &field_decision.treatment}) {
        policy->name = "policy";
        policy->deployment_worlds = 8;
        policy->deployment_horizon_turns = 4;
        policy->scores = {{"No Block", 0.6}};
        policy->selected_keys = {"No Block"};
    }
    field.evidence.decisions.push_back(
        std::move(field_decision));
    field.gate.metadata_exact = true;
    field.gate.fixture_count_exact = true;
    field.gate.every_fixture_valid = true;
    field.gate.stable_fixture_count = 1;
    field.gate.control_agreements = 1;
    field.gate.treatment_agreements = 1;
    field.gate.fixtures.push_back({
        .stable_id = "field-fixture",
        .identity_exact = true,
        .reference_valid = true,
        .deployment_valid = true,
        .stable_reference_best_set = true,
        .control_agrees = true,
        .treatment_agrees = true,
    });
    field.gate.passed = true;
    result.evidence.field_regression = std::move(field);

    result.evidence.treatment_vs_control =
        direct_stage(111, 56);
    result.evidence.treatment_vs_parent =
        direct_stage(222, 57);
    result.evidence.treatment_vs_handcoded =
        direct_stage(333, 58);

    orchestration::FixedSeedPanelsStageResult fixed;
    fixed.evidence.push_back(
        direct_stage(101, 54).evidence);
    old_school::joint_c17_eval::FixedSeedPanelGateReport
        fixed_gate;
    fixed_gate.identity_exact = true;
    fixed_gate.accounting_exact = true;
    fixed_gate.clustered_estimate_valid = true;
    fixed_gate.aggregate_non_losing = true;
    fixed_gate.passed = true;
    fixed.gate.panels.push_back(fixed_gate);
    fixed.gate.panel_count_exact = true;
    fixed.gate.seeds_exact = true;
    fixed.gate.every_panel_passed = true;
    fixed.gate.passed = true;
    result.evidence.fixed_seed_panels = std::move(fixed);

    old_school::joint_c17_eval::FinalDirectGateReport final;
    final.primary.passed = true;
    final.fixed_seed_panels.passed = true;
    final.merge_succeeded = true;
    final.pooled.accounting_exact = true;
    final.pooled.rates_finite = true;
    final.pooled.aggregate_strict_win = true;
    final.pooled.wilson_lower_above_half = true;
    final.pooled.every_challenger_deck_strict_win = true;
    final.pooled.challenger_win_rate_percent = 56.0;
    final.pooled.wilson_lower_95_percent = 52.0;
    final.pooled.challenger_deck_strict_wins.fill(true);
    final.pooled.passed = true;
    final.passed = true;
    result.evidence.final_direct_pool = std::move(final);

    orchestration::MixedFieldStageResult mixed;
    old_school::joint_c17_eval::MixedFieldSeedPanelEvidence
        mixed_panel;
    mixed_panel.summary.evaluation_seed = 101;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t bot = 0;
             bot < old_school::kBotKindCount; ++bot) {
            auto& stats =
                mixed_panel.summary.deck_bots[deck][bot];
            stats.games = 80;
            stats.wins = 30 + bot;
            stats.losses = 50 - bot;
        }
        auto& gate = mixed.gate.by_deck[deck];
        gate.deck = static_cast<old_school::DeckId>(deck);
        gate.random_win_rate_percent = 37.5;
        gate.learned_win_rate_percent = 42.5;
        gate.learned_lift_percentage_points = 5.0;
        gate.best_other_lift_percentage_points = 4.0;
        gate.best_other = old_school::BotKind::Handcrafted;
        gate.rates_finite = true;
        gate.learned_lift_is_best = true;
    }
    mixed.evidence.push_back(std::move(mixed_panel));
    mixed.gate.panel_count_exact = true;
    mixed.gate.seeds_exact = true;
    mixed.gate.policy_identity_exact = true;
    mixed.gate.accounting_exact = true;
    mixed.gate.rates_finite = true;
    mixed.gate.learned_lift_best_on_every_deck = true;
    mixed.gate.passed = true;
    result.evidence.mixed_field = std::move(mixed);

    return {
        .disposition =
            orchestration::EvaluationDisposition::
                ScientificRejection,
        .result = std::move(result),
    };
}

void test_complete_report_has_all_preregistered_rows() {
    const auto outcome = complete_reporting_fixture();
    std::ostringstream human;
    orchestration::write_human_report(outcome, human);
    expect(
        human.str().find("Force Spike live") !=
                std::string::npos &&
            human.str().find("quartet CR1") !=
                std::string::npos &&
            human.str().find(
                "Mixed-field pooled policy rates") !=
                std::string::npos,
        "human report omitted decision or uncertainty evidence");

    std::ostringstream output;
    orchestration::write_tsv_report(outcome, output);
    const std::string text = output.str();
    expect_every_tsv_data_row_has_eleven_columns(text);
    expect(
        count_lines_with_prefix(
            text, "heldout_accounting\t") == 12,
        "heldout pooled/five-deck accounting rows");
    expect(
        count_lines_with_prefix(
            text, "heldout_critic\t") == 54,
        "heldout pooled/five-deck critic rows");
    expect(
        count_lines_with_prefix(
            text, "heldout_delta\t") == 24,
        "heldout pooled/five-deck comparison rows");
    expect(
        count_lines_with_prefix(text, "deep_policy\t") == 12 &&
            count_lines_with_prefix(
                text, "deep_selected_critic\t") == 12 &&
            count_lines_with_prefix(
                text, "deep_common_critic\t") == 12 &&
            count_lines_with_prefix(
                text, "deep_stable_best_set\t") == 5,
        "deep-reference five-deck policy/calibration rows");
    expect(
        count_lines_with_prefix(text, "force_spike\t") == 2 &&
            count_lines_with_prefix(
                text, "force_spike_meta\t") == 2 &&
            count_lines_with_prefix(
                text, "gate\tforce_spike\t") == 2,
        "Force Spike decisions and gates");
    expect(
        count_lines_with_prefix(
            text, "field_fixture\t") == 1 &&
            count_lines_with_prefix(
                text, "field_policy\t") == 3,
        "field fixture rows");
    expect(
        count_lines_with_prefix(
            text, "direct_deck\t") == 15 &&
            count_lines_with_prefix(
                text, "direct_clustered\t") == 3,
        "direct all-five and clustered rows");
    expect(
        count_lines_with_prefix(
            text, "fixed_seed_deck\t") == 5 &&
            count_lines_with_prefix(
                text, "final_pool_deck\t") == 5,
        "fixed/final all-five rows");
    expect(
        count_lines_with_prefix(
            text, "mixed_field_seed\t") == 25 &&
            count_lines_with_prefix(
                text, "mixed_field_policy\t") == 25,
        "mixed-field per-seed and pooled policy rows");
}

void test_unpinned_production_evaluation_fails_before_load() {
    if (orchestration::canonical_bundle_identity_is_pinned()) {
        // The post-publication identity-only patch intentionally changes
        // this compile-time state. Unit tests must never launch the reserved
        // evaluation merely because that patch has landed.
        return;
    }
    std::ostringstream progress;
    const auto outcome =
        orchestration::run_production_evaluation(progress);
    expect(
        outcome.disposition ==
                orchestration::EvaluationDisposition::
                    InfrastructureFailure &&
            !outcome.result.has_value() &&
            outcome.infrastructure_error.find(
                "identity is not compiled") !=
                std::string::npos &&
            progress.str().empty(),
        "unpinned evaluation did not fail before artifact load");
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&passed](std::string_view name, auto&& test) {
            try {
                test();
                ++passed;
            } catch (const std::exception& error) {
                std::cerr << "FAIL " << name << ": "
                          << error.what() << '\n';
                throw;
            }
        };
    run(
        "exact preregistered stage plan",
        test_exact_preregistered_stage_plan);
    run(
        "seed domains remain disjoint",
        test_stage_plan_keeps_training_and_evaluation_disjoint);
    run(
        "public API is context-only",
        test_public_stage_api_requires_canonical_context);
    run(
        "exit dispositions are distinct",
        test_exit_dispositions_are_distinct);
    run(
        "infrastructure report is parseable",
        test_infrastructure_report_is_machine_parseable);
    run(
        "complete report covers every preregistered metric",
        test_complete_report_has_all_preregistered_rows);
    run(
        "unpinned production evaluation is blocked",
        test_unpinned_production_evaluation_fails_before_load);
    std::cout << passed
              << " joint-C17 orchestration tests passed\n";
    return 0;
}
