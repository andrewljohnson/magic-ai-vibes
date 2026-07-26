#include "old_school/joint_c17_orchestration.hpp"

#include "old_school/audit_common.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace old_school::joint_c17_orchestration {
namespace {

using joint_c17_eval::DirectPanelEvidence;
using joint_c17_eval::DirectPanelRole;
using joint_c17_execution::CanonicalJointC17Context;
using probe_runner::NamedValueScoringModel;

const CanonicalStagePlan kPlan{
    .holdout_seed = kLearnedJointC17HoldoutSeed,
    .holdout_generation = kCanonicalHoldoutGeneration,
    .holdout_balanced_blocks =
        kCanonicalHoldoutBalancedBlocks,
    .maximum_game_turns = kCanonicalMaximumGameTurns,
    .training_games =
        joint_c17_runner::kCanonicalTrainingGames,
    .direct =
        {{
            {
                .role = DirectPanelRole::TreatmentVsControl,
                .seed =
                    kLearnedJointC17MatchedControlGameplaySeed,
                .repetitions =
                    joint_c17_eval::kDirectPanelRepetitions,
            },
            {
                .role = DirectPanelRole::TreatmentVsParent,
                .seed =
                    kLearnedJointC17FrozenC16GameplaySeed,
                .repetitions =
                    joint_c17_eval::kDirectPanelRepetitions,
            },
            {
                .role =
                    DirectPanelRole::
                        TreatmentVsHandcodedPrimary,
                .seed =
                    kLearnedJointC17HandcodedGameplaySeed,
                .repetitions =
                    joint_c17_eval::kDirectPanelRepetitions,
            },
        }},
    .fixed_seed_panel_seeds =
        joint_c17_eval::kFixedSeedPanelSeeds,
    .fixed_seed_panel_repetitions =
        joint_c17_eval::kFixedSeedPanelRepetitions,
    .mixed_field_games_per_matchup =
        joint_c17_eval::kMixedFieldGamesPerMatchup,
};

void require_context(const CanonicalJointC17Context& context) {
    const auto& provenance = context.provenance();
    if (!canonical_bundle_identity_is_pinned() ||
        provenance.bundle.before.byte_size !=
            kCanonicalBundleByteSize ||
        provenance.bundle.before.sha256 !=
            kCanonicalBundleSha256 ||
        !provenance.all_bindings_verified ||
        !provenance.five_deck_label_balance_exact ||
        !provenance.model_fingerprint_gate.passed ||
        !provenance.deployment_gate.passed ||
        provenance.bundle.before != provenance.bundle.after ||
        provenance.parent.before != provenance.parent.after ||
        provenance.label_cache.before !=
            provenance.label_cache.after ||
        !context.control_deployment().model ||
        !context.treatment_deployment().model ||
        !context.parent_model()) {
        throw std::runtime_error(
            "canonical C17-J1 orchestration received an "
            "unverified context");
    }
}

joint_c17_eval::PolicyRecipeEvidence deployment_recipe(
    const LearnedJointC17Deployment& deployment) {
    return {
        .policy_token = deployment.policy_token,
        .horizon_turns = deployment.search.horizon_turns,
        .blend_shallow_prior =
            deployment.search.blend_shallow_prior,
    };
}

joint_c17_eval::PolicyRecipeEvidence parent_recipe(
    const CanonicalJointC17Context& context) {
    return {
        .policy_token = std::string(
            joint_c17_eval::kFrozenC16EvidencePolicyToken),
        .horizon_turns =
            context.control_deployment().search.horizon_turns,
        .blend_shallow_prior =
            context.control_deployment()
                .search.blend_shallow_prior,
    };
}

joint_c17_eval::PolicyRecipeEvidence handcoded_recipe() {
    return {
        .policy_token = std::string(
            joint_c17_eval::kHandcodedEvidencePolicyToken),
        .horizon_turns = 0,
        .blend_shallow_prior = false,
    };
}

NamedValueScoringModel named_deployment(
    const LearnedJointC17Deployment& deployment) {
    return {
        .name = deployment.policy_token,
        .model = deployment.model,
        .value_priority_residual_weight =
            deployment.bot.value_priority_residual_weight,
        .value_pass_dominance =
            deployment.bot.value_pass_dominance,
        .value_continuation_controller =
            deployment.bot.value_continuation_controller,
    };
}

NamedValueScoringModel named_parent(
    const CanonicalJointC17Context& context) {
    return {
        .name = std::string(
            joint_c17_eval::kFrozenC16EvidencePolicyToken),
        .model = context.parent_model(),
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
    };
}

BotConfig parent_bot(const CanonicalJointC17Context& context) {
    BotConfig bot = context.control_deployment().bot;
    bot.learned_model = context.parent_model();
    return bot;
}

BotConfig handcoded_bot() {
    return {
        .kind = BotKind::Handcrafted,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 1,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games =
            joint_c17_runner::kCanonicalTrainingGames,
        .learned_model = {},
    };
}

GameConfig canonical_game_config() {
    GameConfig config;
    config.max_turns = kPlan.maximum_game_turns;
    config.learned_training_seed =
        kDefaultLearnedTrainingSeed;
    return config;
}

HeldoutStageResult run_heldout_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);
DeepReferenceStageResult run_deep_reference_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);
FieldRegressionStageResult run_field_regression_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);
DirectGameplayStageResult run_treatment_vs_control_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);
DirectGameplayStageResult run_treatment_vs_parent_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);
DirectGameplayStageResult run_treatment_vs_handcoded_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);
FixedSeedPanelsStageResult run_fixed_seed_panels_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);
MixedFieldStageResult run_mixed_field_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress);

DirectGameplayStageResult run_direct_stage(
    const CanonicalJointC17Context& context,
    const DirectStagePlan& plan, BotConfig baseline,
    joint_c17_eval::PolicyRecipeEvidence baseline_recipe,
    std::ostream& progress) {
    require_context(context);
    progress << "Running canonical C17-J1 "
             << plan.repetitions
             << "-repetition direct panel at seed "
             << plan.seed << "..." << std::flush;
    DirectPanelEvidence evidence{
        .role = plan.role,
        .challenger_policy =
            deployment_recipe(
                context.treatment_deployment()),
        .baseline_policy = std::move(baseline_recipe),
        .summary = run_bot_benchmark(
            plan.repetitions, plan.seed,
            context.treatment_deployment().bot,
            std::move(baseline),
            canonical_game_config()),
    };
    progress << " done\n";
    auto gate =
        joint_c17_eval::evaluate_direct_gameplay_panel(
            evidence,
            context.provenance().model_fingerprints);
    return {
        .evidence = std::move(evidence),
        .gate = std::move(gate),
    };
}

struct EvidenceProducer {
    explicit EvidenceProducer(
        const CanonicalJointC17Context& context,
        std::ostream& progress)
        : context(context), progress(progress) {}

    joint_c17_eval::HeldoutGateReport heldout() {
        evidence.heldout =
            run_heldout_stage(context, progress);
        return evidence.heldout->gate;
    }

    joint_c17_eval::DeepReferenceGateReport
    deep_reference() {
        evidence.deep_reference =
            run_deep_reference_stage(context, progress);
        return evidence.deep_reference->gate;
    }

    joint_c17_eval::FieldRegressionGateReport
    field_regression() {
        evidence.field_regression =
            run_field_regression_stage(context, progress);
        return evidence.field_regression->gate;
    }

    joint_c17_eval::DirectGameplayGateReport
    treatment_vs_control() {
        evidence.treatment_vs_control =
            run_treatment_vs_control_stage(
                context, progress);
        return evidence.treatment_vs_control->gate;
    }

    joint_c17_eval::DirectGameplayGateReport
    treatment_vs_parent() {
        evidence.treatment_vs_parent =
            run_treatment_vs_parent_stage(
                context, progress);
        return evidence.treatment_vs_parent->gate;
    }

    joint_c17_eval::DirectGameplayGateReport
    treatment_vs_handcoded() {
        evidence.treatment_vs_handcoded =
            run_treatment_vs_handcoded_stage(
                context, progress);
        return evidence.treatment_vs_handcoded->gate;
    }

    joint_c17_eval::FixedSeedPanelSetGateReport
    fixed_seed_panels() {
        evidence.fixed_seed_panels =
            run_fixed_seed_panels_stage(
                context, progress);
        return evidence.fixed_seed_panels->gate;
    }

    joint_c17_eval::FinalDirectGateReport
    final_direct_pool() {
        if (!evidence.treatment_vs_handcoded.has_value() ||
            !evidence.fixed_seed_panels.has_value()) {
            throw std::logic_error(
                "canonical final direct pool requires the "
                "completed primary and fixed-seed evidence");
        }
        evidence.final_direct_pool =
            joint_c17_eval::evaluate_final_direct_gate(
                evidence.treatment_vs_handcoded->evidence,
                evidence.fixed_seed_panels->evidence,
                context.provenance().model_fingerprints);
        return *evidence.final_direct_pool;
    }

    joint_c17_eval::MixedFieldGateReport mixed_field() {
        evidence.mixed_field =
            run_mixed_field_stage(context, progress);
        return evidence.mixed_field->gate;
    }

    const CanonicalJointC17Context& context;
    std::ostream& progress;
    CanonicalEvaluationEvidence evidence;
};

joint_c17_runner::SealedRunRequest make_request(
    const CanonicalJointC17Context& context,
    const std::shared_ptr<EvidenceProducer>& producer) {
    require_context(context);
    const auto& provenance = context.provenance();
    joint_c17_runner::SealedRunRequest request{
        .artifact_integrity =
            joint_c17_runner::
                make_evaluation_artifact_integrity_request(
                    kCanonicalBundleByteSize,
                    std::string(
                        kCanonicalBundleSha256)),
        .model_fingerprints =
            provenance.model_fingerprints,
        .deployments =
            {
                .control = context.control_deployment(),
                .treatment =
                    context.treatment_deployment(),
                .artifact_control_fingerprint =
                    context.joint_report()
                        .control_fingerprint,
                .artifact_treatment_fingerprint =
                    context.joint_report()
                        .treatment_fingerprint,
            },
    };
    request.stages.heldout =
        [producer] { return producer->heldout(); };
    request.stages.deep_reference =
        [producer] {
            return producer->deep_reference();
        };
    request.stages.field_regression =
        [producer] {
            return producer->field_regression();
        };
    request.stages.treatment_vs_control =
        [producer] {
            return producer->treatment_vs_control();
        };
    request.stages.treatment_vs_parent =
        [producer] {
            return producer->treatment_vs_parent();
        };
    request.stages.treatment_vs_handcoded =
        [producer] {
            return producer->treatment_vs_handcoded();
        };
    request.stages.fixed_seed_panels =
        [producer] {
            return producer->fixed_seed_panels();
        };
    request.stages.final_direct_pool =
        [producer] {
            return producer->final_direct_pool();
        };
    request.stages.mixed_field =
        [producer] {
            return producer->mixed_field();
        };
    return request;
}

EvaluationDisposition map_disposition(
    joint_c17_runner::RunnerDisposition disposition) {
    switch (disposition) {
    case joint_c17_runner::RunnerDisposition::Passed:
        return EvaluationDisposition::Accepted;
    case joint_c17_runner::RunnerDisposition::Rejected:
        return EvaluationDisposition::ScientificRejection;
    case joint_c17_runner::RunnerDisposition::
        InfrastructureFailure:
        return EvaluationDisposition::InfrastructureFailure;
    }
    return EvaluationDisposition::InfrastructureFailure;
}

std::string_view runner_disposition_name(
    joint_c17_runner::RunnerDisposition disposition) {
    switch (disposition) {
    case joint_c17_runner::RunnerDisposition::Passed:
        return "PASS";
    case joint_c17_runner::RunnerDisposition::Rejected:
        return "REJECT";
    case joint_c17_runner::RunnerDisposition::
        InfrastructureFailure:
        return "INFRASTRUCTURE_FAILURE";
    }
    return "INFRASTRUCTURE_FAILURE";
}

std::string_view pass_fail(bool passed) {
    return passed ? "PASS" : "REJECT";
}

std::string joined_keys(
    const std::vector<std::string>& keys) {
    std::string joined;
    for (const auto& key : keys) {
        if (!joined.empty()) {
            joined += ',';
        }
        joined += key;
    }
    return joined;
}

std::string_view critic_model_name(std::size_t model) {
    switch (
        static_cast<terminal_weight_eval::CriticModel>(model)) {
    case terminal_weight_eval::CriticModel::ParentC16:
        return "parent_c16";
    case terminal_weight_eval::CriticModel::TW50:
        return "control";
    case terminal_weight_eval::CriticModel::TW75:
        return "treatment";
    }
    return "unknown";
}

std::string_view decision_kind_name(
    probes::DecisionKind kind) {
    switch (kind) {
    case probes::DecisionKind::Priority:
        return "priority";
    case probes::DecisionKind::Attack:
        return "attack";
    case probes::DecisionKind::Block:
        return "block";
    }
    return "unknown";
}

std::string_view field_score_kind_name(
    probe_runner::FieldRegressionScoreKind kind) {
    switch (kind) {
    case probe_runner::FieldRegressionScoreKind::
        DeployedPrioritySearch:
        return "deployed_priority_search";
    case probe_runner::FieldRegressionScoreKind::
        ImmediateCombat:
        return "immediate_combat";
    }
    return "unknown";
}

std::string_view continuation_controller_name(
    LearnedContinuationController controller) {
    switch (controller) {
    case LearnedContinuationController::Legacy:
        return "Legacy";
    case LearnedContinuationController::PublicStackPassV1:
        return "PublicStackPassV1";
    }
    return "unknown";
}

struct OutcomeTotals {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
};

template <typename Stats>
void add_outcomes(
    const Stats& source, OutcomeTotals& destination) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
}

struct FinalDirectTotals {
    OutcomeTotals pooled;
    std::array<OutcomeTotals, kDeckCount> by_deck{};
};

FinalDirectTotals final_direct_totals(
    const CanonicalEvaluationEvidence& evidence) {
    FinalDirectTotals totals;
    const auto add_panel =
        [&totals](
            const joint_c17_eval::DirectPanelEvidence& panel) {
            add_outcomes(
                panel.summary.challenger_stats,
                totals.pooled);
            for (std::size_t deck = 0;
                 deck < kDeckCount; ++deck) {
                add_outcomes(
                    panel.summary.challenger_decks[deck],
                    totals.by_deck[deck]);
            }
        };
    if (evidence.treatment_vs_handcoded.has_value()) {
        add_panel(
            evidence.treatment_vs_handcoded->evidence);
    }
    if (evidence.fixed_seed_panels.has_value()) {
        for (const auto& panel :
             evidence.fixed_seed_panels->evidence) {
            add_panel(panel);
        }
    }
    return totals;
}

using MixedFieldTotals = std::array<
    std::array<OutcomeTotals, kBotKindCount>, kDeckCount>;

MixedFieldTotals mixed_field_totals(
    const std::vector<
        joint_c17_eval::MixedFieldSeedPanelEvidence>& panels) {
    MixedFieldTotals totals{};
    for (const auto& panel : panels) {
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            for (std::size_t bot = 0;
                 bot < kBotKindCount; ++bot) {
                add_outcomes(
                    panel.summary.deck_bots[deck][bot],
                    totals[deck][bot]);
            }
        }
    }
    return totals;
}

double win_rate_percent(const OutcomeTotals& totals) {
    if (totals.games == 0) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(totals.wins) /
           static_cast<double>(totals.games);
}

void write_heldout_scope_human(
    std::string_view scope,
    const terminal_weight_eval::HoldoutScopeMetrics& metrics,
    std::ostream& output) {
    output << "    " << scope << ": " << metrics.records
           << " records / " << metrics.physical_games
           << " games, target mean "
           << audit_common::format_real(metrics.target_mean)
           << '\n';
    for (std::size_t model = 0;
         model < terminal_weight_eval::kCriticModelCount;
         ++model) {
        const auto& critic = metrics.models[model];
        output
            << "      " << critic_model_name(model)
            << ": Brier "
            << audit_common::format_real(critic.brier.mean)
            << ", soft log "
            << audit_common::format_real(
                   critic.soft_log_loss.mean)
            << ", bias "
            << audit_common::format_real(
                   critic.signed_bias.mean)
            << ", prediction "
            << audit_common::format_real(
                   critic.prediction_mean)
            << ", saturation "
            << critic.saturated_predictions << '/'
            << metrics.records << '\n';
    }
    const auto& delta = metrics.treatment_comparisons[0];
    output
        << "      treatment-control: Brier "
        << audit_common::format_real(delta.brier_delta.mean)
        << " ["
        << audit_common::format_real(
               delta.brier_delta.confidence_lower_95)
        << ", "
        << audit_common::format_real(
               delta.brier_delta.confidence_upper_95)
        << "], soft log "
        << audit_common::format_real(
               delta.soft_log_loss_delta.mean)
        << " ["
        << audit_common::format_real(
               delta.soft_log_loss_delta
                   .confidence_lower_95)
        << ", "
        << audit_common::format_real(
               delta.soft_log_loss_delta
                   .confidence_upper_95)
        << "]\n";
}

void write_probe_scope_human(
    std::string_view scope,
    const probe_eval::DeckProbeMetrics& control,
    const probe_eval::DeckProbeMetrics& treatment,
    const joint_c17_eval::CommonStateCriticScope& common,
    const joint_c17_eval::StableBestSetDeckReport* stable,
    std::ostream& output) {
    output
        << "    " << scope << ": top-one "
        << audit_common::format_real(
               control.top1_expected_agreement)
        << " / "
        << audit_common::format_real(
               treatment.top1_expected_agreement)
        << ", regret "
        << audit_common::format_real(control.mean_regret)
        << " / "
        << audit_common::format_real(treatment.mean_regret)
        << ", selected-Q Brier "
        << audit_common::format_real(control.critic_brier)
        << " / "
        << audit_common::format_real(
               treatment.critic_brier)
        << ", common-state Brier "
        << audit_common::format_real(
               common.models[
                   joint_c17_eval::
                       kCommonStateControlIndex]
                   .brier)
        << " / "
        << audit_common::format_real(
               common.models[
                   joint_c17_eval::
                       kCommonStateTreatmentIndex]
                   .brier);
    if (stable != nullptr) {
        output << ", stable losses "
               << stable->lost_agreements << '/'
               << stable->eligible_probes;
    }
    output << '\n';
}

void write_direct_human(
    std::string_view label,
    const DirectGameplayStageResult& stage,
    std::ostream& output) {
    const auto& summary = stage.evidence.summary;
    output << "  " << label << ": "
           << summary.challenger_stats.wins << '-'
           << summary.baseline_stats.wins << '-'
           << summary.challenger_stats.draws << " ("
           << audit_common::format_real(
                  stage.gate.challenger_win_rate_percent)
           << "%, Wilson lower "
           << audit_common::format_real(
                  stage.gate.wilson_lower_95_percent)
           << "%; quartet CR1 "
           << audit_common::format_real(
                  100.0 *
                  summary.challenger_quartet_cr1.mean)
           << "% ["
           << audit_common::format_real(
                  100.0 *
                  summary.challenger_quartet_cr1
                      .confidence_low_95)
           << ", "
           << audit_common::format_real(
                  100.0 *
                  summary.challenger_quartet_cr1
                      .confidence_high_95)
           << "]%) "
           << (stage.gate.passed ? "PASS" : "REJECT")
           << '\n';
    output << "    by challenger deck:";
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        const auto& stats =
            summary.challenger_decks[deck];
        output << ' '
               << deck_name(static_cast<DeckId>(deck))
               << '=' << stats.wins << '-' << stats.losses
               << '-' << stats.draws;
    }
    output << '\n';
}

template <typename... Values>
void write_tsv_row(std::ostream& output, Values&&... values) {
    bool first = true;
    const auto field = [&](const auto& value) {
        if (!first) {
            output << '\t';
        }
        first = false;
        output << audit_common::sanitize_tsv(value);
    };
    (field(std::forward<Values>(values)), ...);
    output << '\n';
}

void write_gate_check_tsv(
    std::ostream& output, std::string_view scope,
    std::string_view check, bool passed) {
    write_tsv_row(
        output, "gate", scope, check, "", "", "", "", "",
        "", "", pass_fail(passed));
}

void write_gate_failures_tsv(
    std::ostream& output, std::string_view scope,
    const std::vector<std::string>& failures) {
    for (const auto& failure : failures) {
        write_tsv_row(
            output, "gate_failure", scope, failure, "", "",
            "", "", "", "", "", "REJECT");
    }
}

void write_clustered_estimate_tsv(
    std::ostream& output, std::string_view kind,
    std::string_view scope, std::string_view subject,
    const terminal_weight_eval::ClusteredEstimate& estimate,
    std::string_view status) {
    write_tsv_row(
        output, kind, scope, subject, "",
        std::to_string(estimate.records),
        std::to_string(estimate.clusters),
        audit_common::format_real(estimate.mean),
        audit_common::format_real(estimate.standard_error),
        audit_common::format_real(
            estimate.confidence_lower_95),
        audit_common::format_real(
            estimate.confidence_upper_95),
        status);
}

void write_heldout_scope_tsv(
    std::ostream& output, std::string_view scope,
    const terminal_weight_eval::HoldoutScopeMetrics& metrics,
    std::string_view status) {
    write_tsv_row(
        output, "heldout_accounting", scope, "records",
        std::to_string(kPlan.holdout_seed),
        std::to_string(metrics.physical_games),
        std::to_string(metrics.records),
        std::to_string(metrics.perspectives),
        std::to_string(metrics.bootstrapped_records),
        std::to_string(metrics.terminal_tail_records), "",
        status);
    write_tsv_row(
        output, "heldout_accounting", scope, "targets", "",
        std::to_string(metrics.records),
        audit_common::format_real(metrics.target_mean),
        audit_common::format_real(metrics.target_variance),
        audit_common::format_real(
            metrics.mean_trace_turn_distance),
        std::to_string(metrics.minimum_trace_turn_distance),
        std::to_string(metrics.maximum_trace_turn_distance),
        status);
    for (std::size_t model = 0;
         model < terminal_weight_eval::kCriticModelCount;
         ++model) {
        const auto& critic = metrics.models[model];
        const std::string model_name(critic_model_name(model));
        write_clustered_estimate_tsv(
            output, "heldout_critic", scope,
            model_name + ".brier", critic.brier, status);
        write_clustered_estimate_tsv(
            output, "heldout_critic", scope,
            model_name + ".soft_log_loss",
            critic.soft_log_loss, status);
        write_clustered_estimate_tsv(
            output, "heldout_critic", scope,
            model_name + ".signed_bias",
            critic.signed_bias, status);
        write_tsv_row(
            output, "heldout_critic_meta", scope,
            model_name, "", std::to_string(metrics.records),
            audit_common::format_real(
                critic.prediction_mean),
            std::to_string(critic.saturated_predictions),
            audit_common::format_real(
                critic.saturation_fraction),
            "", "", status);
    }
    constexpr std::array<std::string_view, 2>
        comparison_names = {
            "treatment_minus_control",
            "treatment_minus_parent",
        };
    for (std::size_t comparison = 0;
         comparison < comparison_names.size();
         ++comparison) {
        const auto& delta =
            metrics.treatment_comparisons[comparison];
        write_clustered_estimate_tsv(
            output, "heldout_delta", scope,
            std::string(comparison_names[comparison]) +
                ".brier",
            delta.brier_delta, status);
        write_clustered_estimate_tsv(
            output, "heldout_delta", scope,
            std::string(comparison_names[comparison]) +
                ".soft_log_loss",
            delta.soft_log_loss_delta, status);
    }
}

template <typename Metrics>
void write_probe_metric_scope_tsv(
    std::ostream& output, std::string_view scope,
    std::string_view policy, const Metrics& metrics,
    std::string_view status) {
    write_tsv_row(
        output, "deep_policy", scope, policy, "",
        std::to_string(metrics.probe_count),
        std::to_string(metrics.stable_pair_count),
        audit_common::format_real(
            metrics.top1_expected_agreement),
        audit_common::format_real(
            metrics.stable_pair_agreement),
        audit_common::format_real(metrics.mean_regret), "",
        status);
    write_tsv_row(
        output, "deep_selected_critic", scope, policy, "",
        std::to_string(metrics.probe_count),
        audit_common::format_real(metrics.critic_brier),
        audit_common::format_real(metrics.critic_log_loss),
        audit_common::format_real(metrics.critic_bias),
        audit_common::format_real(metrics.critic_ece), "",
        status);
}

void write_common_critic_scope_tsv(
    std::ostream& output, std::string_view scope,
    const joint_c17_eval::CommonStateCriticScope& metrics,
    std::string_view status) {
    constexpr std::array<std::string_view, 2> names = {
        "control", "treatment"};
    for (std::size_t model = 0;
         model < metrics.models.size(); ++model) {
        const auto& critic = metrics.models[model];
        write_tsv_row(
            output, "deep_common_critic", scope,
            names[model], "",
            std::to_string(critic.probe_count),
            audit_common::format_real(critic.brier),
            audit_common::format_real(
                critic.soft_log_loss),
            audit_common::format_real(critic.signed_bias),
            audit_common::format_real(critic.ece), "",
            status);
    }
}

void write_probe_decisions_tsv(
    std::ostream& output, std::string_view policy,
    const std::vector<
        probe_runner::ValueProbeDecisionDetail>& decisions,
    std::string_view status) {
    for (const auto& decision : decisions) {
        write_tsv_row(
            output, "deep_decision",
            deck_name(decision.root_deck),
            std::string(policy) + ":" + decision.stable_id,
            "", "",
            audit_common::format_real(decision.regret),
            audit_common::format_real(
                decision.critic_prediction),
            audit_common::format_real(
                decision.selected_action_reference_q),
            audit_common::format_real(
                decision.critic_error),
            "selected=" + joined_keys(decision.selected_keys) +
                ";reference_best=" +
                joined_keys(decision.reference_best_set) +
                ";deterministic=" +
                (decision.deterministic_selection
                     ? "true"
                     : "false"),
            status);
    }
}

void write_field_policy_tsv(
    std::ostream& output,
    const probe_runner::FieldRegressionDecisionReport& decision,
    std::string_view policy,
    const probe_runner::FieldRegressionPolicyDecision& evidence,
    std::string_view status) {
    write_tsv_row(
        output, "field_policy", decision.stable_id, policy,
        "", "", std::to_string(evidence.deployment_worlds),
        std::to_string(evidence.deployment_horizon_turns),
        audit_common::format_real(
            evidence.value_priority_residual_weight),
        field_score_kind_name(evidence.score_kind),
        joined_keys(evidence.selected_keys), status);
    write_tsv_row(
        output, "field_policy_meta", decision.stable_id,
        policy, "", "",
        evidence.blend_shallow_prior ? "blend_on" : "blend_off",
        audit_common::format_real(
            evidence.value_continuation_epsilon),
        evidence.value_pass_dominance ? "pd0_on" : "pd0_off",
        continuation_controller_name(
            evidence.value_continuation_controller),
        "fingerprint=" + evidence.fingerprint +
            ";scores_adjusted=" +
            (evidence.policy_scores_adjusted_for_deployment
                 ? "true"
                 : "false"),
        status);
    for (const auto& score : evidence.scores) {
        write_tsv_row(
            output, "field_score", decision.stable_id,
            std::string(policy) + ":" + score.key, "", "",
            audit_common::format_real(score.score), "", "",
            "", "", status);
    }
}

void write_direct_gate_checks_tsv(
    std::ostream& output, std::string_view scope,
    const joint_c17_eval::DirectGameplayGateReport& gate) {
    write_gate_check_tsv(
        output, scope, "identity_exact", gate.identity_exact);
    write_gate_check_tsv(
        output, scope, "accounting_exact",
        gate.accounting_exact);
    write_gate_check_tsv(
        output, scope, "clustered_estimate_valid",
        gate.clustered_estimate_valid);
    write_gate_check_tsv(
        output, scope, "rates_finite", gate.rates_finite);
    write_gate_check_tsv(
        output, scope, "aggregate_strict_win",
        gate.aggregate_strict_win);
    write_gate_check_tsv(
        output, scope, "wilson_lower_above_half",
        gate.wilson_lower_above_half);
    write_gate_check_tsv(
        output, scope, "every_challenger_deck_strict_win",
        gate.every_challenger_deck_strict_win);
    write_gate_failures_tsv(output, scope, gate.failures);
}

void write_direct_tsv(
    std::string_view label,
    const DirectGameplayStageResult& stage,
    std::ostream& output) {
    const auto& summary = stage.evidence.summary;
    write_tsv_row(
        output, "direct", label, "pooled",
        std::to_string(summary.evaluation_seed),
        std::to_string(summary.challenger_stats.games),
        std::to_string(summary.challenger_stats.wins),
        std::to_string(summary.challenger_stats.losses),
        std::to_string(summary.challenger_stats.draws),
        audit_common::format_real(
            stage.gate.challenger_win_rate_percent),
        audit_common::format_real(
            stage.gate.wilson_lower_95_percent),
        stage.gate.passed ? "PASS" : "REJECT");
    write_tsv_row(
        output, "direct_clustered", label, "quartet_cr1",
        std::to_string(summary.evaluation_seed),
        std::to_string(
            summary.challenger_quartet_cr1.records),
        std::to_string(
            summary.challenger_quartet_cr1.clusters),
        audit_common::format_real(
            100.0 *
            summary.challenger_quartet_cr1.mean),
        audit_common::format_real(
            100.0 *
            summary.challenger_quartet_cr1.standard_error),
        audit_common::format_real(
            100.0 *
            summary.challenger_quartet_cr1
                .confidence_low_95),
        audit_common::format_real(
            100.0 *
            summary.challenger_quartet_cr1
                .confidence_high_95),
        stage.gate.clustered_estimate_valid
            ? "PASS"
            : "REJECT");
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        const auto& stats =
            summary.challenger_decks[deck];
        write_tsv_row(
            output, "direct_deck", label,
            deck_name(static_cast<DeckId>(deck)),
            std::to_string(summary.evaluation_seed),
            std::to_string(stats.games),
            std::to_string(stats.wins),
            std::to_string(stats.losses),
            std::to_string(stats.draws), "", "",
            stage.gate.challenger_deck_strict_wins[deck]
                ? "PASS"
                : "REJECT");
    }
    write_direct_gate_checks_tsv(
        output, label, stage.gate);
}

} // namespace

const CanonicalStagePlan& canonical_stage_plan() {
    return kPlan;
}

bool canonical_bundle_identity_is_pinned() {
    return kCanonicalBundleByteSize > 0 &&
           kCanonicalBundleSha256.size() == 64 &&
           std::all_of(
               kCanonicalBundleSha256.begin(),
               kCanonicalBundleSha256.end(),
               [](char character) {
                   return
                       (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
               });
}

namespace {

HeldoutStageResult run_heldout_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    require_context(context);
    progress
        << "Collecting canonical C17-J1 held-out critic "
           "evidence..."
        << std::flush;
    const auto records =
        terminal_weight_eval::collect_holdout_records(
            context.parent_model(),
            context.control_deployment().model,
            context.treatment_deployment().model,
            {
                .seed = kPlan.holdout_seed,
                .generation =
                    kPlan.holdout_generation,
                .balanced_blocks =
                    kPlan.holdout_balanced_blocks,
                .max_game_turns =
                    kPlan.maximum_game_turns,
                .pilot_training_games =
                    kPlan.training_games,
            },
            progress);
    auto report =
        terminal_weight_eval::score_holdout_records(
            records);
    progress << "Held-out critic evidence scored.\n";
    auto gate =
        joint_c17_eval::evaluate_heldout_gate(report);
    return {
        .evidence = std::move(report),
        .gate = std::move(gate),
    };
}

DeepReferenceStageResult run_deep_reference_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    require_context(context);
    progress
        << "Scoring canonical C17-J1 frozen Dev-v3 "
           "deep-reference evidence..."
        << std::flush;
    const std::vector<probe_eval::ProbeLabel> labels(
        context.labels().begin(), context.labels().end());
    const NamedValueScoringModel control =
        named_deployment(context.control_deployment());
    const NamedValueScoringModel treatment =
        named_deployment(context.treatment_deployment());
    auto evidence =
        probe_runner::score_value_probe_pair_against_labels(
            probe_runner::ProbeCorpusKind::DevV3,
            labels, control, treatment,
            context.treatment_deployment().search.worlds,
            context.treatment_deployment()
                .search.value_continuation_epsilon);
    auto force_spike =
        probe_runner::score_value_force_spike_policy_controls(
            context.treatment_deployment().model,
            context.treatment_deployment().policy_token,
            context.treatment_deployment().search.worlds,
            context.treatment_deployment()
                .search.value_continuation_epsilon,
            context.treatment_deployment()
                .search.value_priority_residual_weight,
            context.treatment_deployment()
                .search.value_pass_dominance,
            context.treatment_deployment()
                .search.value_continuation_controller);
    progress << " done\n";
    auto gate =
        joint_c17_eval::evaluate_deep_reference_gate(
            evidence.control.metrics,
            evidence.treatment.metrics, labels,
            evidence.control.decisions,
            evidence.treatment.decisions,
            force_spike,
            evidence.hidden_repartition.passed);
    return {
        .evidence = std::move(evidence),
        .force_spike = std::move(force_spike),
        .gate = std::move(gate),
    };
}

FieldRegressionStageResult run_field_regression_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    require_context(context);
    progress
        << "Scoring canonical C17-J1 six-fixture field "
           "regression..."
        << std::flush;
    auto evidence =
        probe_runner::score_field_regressions_v1(
            named_parent(context),
            named_deployment(context.control_deployment()),
            named_deployment(
                context.treatment_deployment()));
    progress << " done\n";
    auto gate =
        joint_c17_eval::evaluate_field_regression_gate(
            evidence,
            context.provenance().model_fingerprints);
    return {
        .evidence = std::move(evidence),
        .gate = std::move(gate),
    };
}

DirectGameplayStageResult run_treatment_vs_control_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    return run_direct_stage(
        context, kPlan.direct[0],
        context.control_deployment().bot,
        deployment_recipe(context.control_deployment()),
        progress);
}

DirectGameplayStageResult run_treatment_vs_parent_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    return run_direct_stage(
        context, kPlan.direct[1], parent_bot(context),
        parent_recipe(context), progress);
}

DirectGameplayStageResult run_treatment_vs_handcoded_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    return run_direct_stage(
        context, kPlan.direct[2], handcoded_bot(),
        handcoded_recipe(), progress);
}

FixedSeedPanelsStageResult run_fixed_seed_panels_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    require_context(context);
    std::vector<DirectPanelEvidence> panels;
    panels.reserve(
        kPlan.fixed_seed_panel_seeds.size());
    for (const std::uint64_t seed :
         kPlan.fixed_seed_panel_seeds) {
        progress
            << "Running canonical C17-J1 fixed Handcoded "
               "panel at seed "
            << seed << "..." << std::flush;
        panels.push_back({
            .role =
                DirectPanelRole::
                    TreatmentVsHandcodedFixedSeed,
            .challenger_policy =
                deployment_recipe(
                    context.treatment_deployment()),
            .baseline_policy = handcoded_recipe(),
            .summary = run_bot_benchmark(
                kPlan.fixed_seed_panel_repetitions,
                seed,
                context.treatment_deployment().bot,
                handcoded_bot(),
                canonical_game_config()),
        });
        progress << " done\n";
    }
    auto gate =
        joint_c17_eval::evaluate_fixed_seed_panel_set(
            panels,
            context.provenance().model_fingerprints);
    return {
        .evidence = std::move(panels),
        .gate = std::move(gate),
    };
}

MixedFieldStageResult run_mixed_field_stage(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    require_context(context);
    TournamentConfig tournament;
    tournament.bot_field = BotField::Mixed;
    tournament.learned_variant =
        LearnedVariant::ValueSearchChampion;
    tournament.monte_carlo_rollouts = 2;
    tournament.deep_monte_carlo_rollouts = 8;
    tournament.learned_rollouts =
        context.treatment_deployment()
            .bot.rollouts_per_action;
    tournament.value_continuation_epsilon = 0.0;
    tournament.learned_training_games =
        kPlan.training_games;
    tournament.frozen_learned_bot =
        context.treatment_deployment().bot;

    std::vector<joint_c17_eval::MixedFieldSeedPanelEvidence>
        panels;
    panels.reserve(
        kPlan.fixed_seed_panel_seeds.size());
    for (const std::uint64_t seed :
         kPlan.fixed_seed_panel_seeds) {
        progress
            << "Running canonical C17-J1 mixed-field panel "
               "at seed "
            << seed << "..." << std::flush;
        panels.push_back({
            .learned_policy =
                deployment_recipe(
                    context.treatment_deployment()),
            .summary = run_tournament(
                kPlan.mixed_field_games_per_matchup,
                seed, canonical_game_config(),
                tournament),
        });
        progress << " done\n";
    }
    auto gate =
        joint_c17_eval::evaluate_mixed_field_pool(
            panels,
            context.provenance().model_fingerprints);
    return {
        .evidence = std::move(panels),
        .gate = std::move(gate),
    };
}

CanonicalEvaluationResult run_canonical_evaluation_with_context(
    const CanonicalJointC17Context& context,
    std::ostream& progress) {
    require_context(context);
    const auto producer =
        std::make_shared<EvidenceProducer>(
            context, progress);
    auto sealed =
        joint_c17_runner::run_sealed_evaluation(
            make_request(context, producer));
    return {
        .sealed = std::move(sealed),
        .evidence = std::move(producer->evidence),
    };
}

} // namespace

std::string_view evaluation_disposition_name(
    EvaluationDisposition disposition) {
    switch (disposition) {
    case EvaluationDisposition::Accepted:
        return "ACCEPTED";
    case EvaluationDisposition::ScientificRejection:
        return "SCIENTIFIC_REJECTION";
    case EvaluationDisposition::InfrastructureFailure:
        return "INFRASTRUCTURE_FAILURE";
    }
    return "INFRASTRUCTURE_FAILURE";
}

int evaluation_exit_code(EvaluationDisposition disposition) {
    switch (disposition) {
    case EvaluationDisposition::Accepted:
        return 0;
    case EvaluationDisposition::ScientificRejection:
        return 1;
    case EvaluationDisposition::InfrastructureFailure:
        return 2;
    }
    return 2;
}

ProductionEvaluationOutcome run_production_evaluation(
    std::ostream& progress) {
    if (!canonical_bundle_identity_is_pinned()) {
        return {
            .disposition =
                EvaluationDisposition::
                    InfrastructureFailure,
            .result = std::nullopt,
            .infrastructure_error =
                "canonical C17-J1 bundle identity is not "
                "compiled; apply the post-publication "
                "identity-only source patch before evaluation",
        };
    }
    try {
        const auto bundle =
            artifact_integrity::snapshot_regular_file(
                joint_c17_runner::kCanonicalArtifactPath);
        if (bundle.byte_size !=
                kCanonicalBundleByteSize ||
            bundle.sha256 != kCanonicalBundleSha256) {
            return {
                .disposition =
                    EvaluationDisposition::
                        InfrastructureFailure,
                .result = std::nullopt,
                .infrastructure_error =
                    "canonical C17-J1 bundle does not match "
                    "the compiled publication identity",
            };
        }
        auto context =
            joint_c17_execution::
                load_canonical_joint_c17_context(progress);
        auto result =
            run_canonical_evaluation_with_context(
                context, progress);
        const EvaluationDisposition disposition =
            map_disposition(result.sealed.disposition);
        return {
            .disposition = disposition,
            .result = std::move(result),
        };
    } catch (const std::exception& error) {
        return {
            .disposition =
                EvaluationDisposition::
                    InfrastructureFailure,
            .result = std::nullopt,
            .infrastructure_error = error.what(),
        };
    } catch (...) {
        return {
            .disposition =
                EvaluationDisposition::
                    InfrastructureFailure,
            .result = std::nullopt,
            .infrastructure_error =
                "non-standard exception",
        };
    }
}

void write_human_report(
    const ProductionEvaluationOutcome& outcome,
    std::ostream& output) {
    output << "\nC17-J1 sealed evaluation\n"
           << "  Disposition: "
           << evaluation_disposition_name(
                  outcome.disposition)
           << '\n';
    if (!outcome.result.has_value()) {
        output << "  Infrastructure error: "
               << (outcome.infrastructure_error.empty()
                       ? "unspecified"
                       : outcome.infrastructure_error)
               << '\n';
        return;
    }

    const auto& result = *outcome.result;
    output << "  Terminal stage: "
           << joint_c17_runner::runner_stage_name(
                  result.sealed.terminal_stage)
           << "\n  Control fingerprint: "
           << result.sealed.deployments
                  .computed_control_fingerprint
           << "\n  Treatment fingerprint: "
           << result.sealed.deployments
                  .computed_treatment_fingerprint
           << "\n  Stage ledger:\n";
    for (const auto& stage : result.sealed.stages) {
        output << "    "
               << joint_c17_runner::runner_stage_name(
                      stage.stage)
               << ": "
               << runner_disposition_name(
                      stage.disposition);
        if (!stage.failures.empty()) {
            output << " — " << stage.failures.front();
        }
        output << '\n';
    }

    if (result.evidence.heldout.has_value()) {
        const auto& stage = *result.evidence.heldout;
        output << "  Held-out critic gate: "
               << pass_fail(stage.gate.passed) << '\n';
        write_heldout_scope_human(
            "pooled", stage.evidence.pooled, output);
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            write_heldout_scope_human(
                deck_name(static_cast<DeckId>(deck)),
                stage.evidence.by_deck[deck], output);
        }
    }
    if (result.evidence.deep_reference.has_value()) {
        const auto& stage =
            *result.evidence.deep_reference;
        const auto& control =
            stage.evidence.control.metrics;
        const auto& treatment =
            stage.evidence.treatment.metrics;
        output << "  Dev-v3 deep-reference gate: "
               << pass_fail(stage.gate.passed) << '\n';
        probe_eval::DeckProbeMetrics pooled_control{
            .probe_count = control.probe_count,
            .stable_pair_count = control.stable_pair_count,
            .top1_expected_agreement =
                control.top1_expected_agreement,
            .stable_pair_agreement =
                control.stable_pair_agreement,
            .mean_regret = control.mean_regret,
            .critic_brier = control.critic_brier,
            .critic_mse = control.critic_mse,
            .critic_log_loss = control.critic_log_loss,
            .critic_bias = control.critic_bias,
            .critic_ece = control.critic_ece,
        };
        probe_eval::DeckProbeMetrics pooled_treatment{
            .probe_count = treatment.probe_count,
            .stable_pair_count =
                treatment.stable_pair_count,
            .top1_expected_agreement =
                treatment.top1_expected_agreement,
            .stable_pair_agreement =
                treatment.stable_pair_agreement,
            .mean_regret = treatment.mean_regret,
            .critic_brier = treatment.critic_brier,
            .critic_mse = treatment.critic_mse,
            .critic_log_loss = treatment.critic_log_loss,
            .critic_bias = treatment.critic_bias,
            .critic_ece = treatment.critic_ece,
        };
        write_probe_scope_human(
            "pooled", pooled_control, pooled_treatment,
            stage.gate.common_state_critics.pooled, nullptr,
            output);
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            write_probe_scope_human(
                deck_name(static_cast<DeckId>(deck)),
                control.by_deck[deck],
                treatment.by_deck[deck],
                stage.gate.common_state_critics.by_deck[deck],
                &stage.gate.by_deck[deck], output);
        }
        output
            << "    Force Spike live: Pass "
            << audit_common::format_real(
                   stage.force_spike.live.pass_score)
            << ", Spike "
            << audit_common::format_real(
                   stage.force_spike.live.force_spike_score)
            << ", selected "
            << joined_keys(
                   stage.force_spike.live.selected_keys)
            << "; payable: Pass "
            << audit_common::format_real(
                   stage.force_spike.payable.pass_score)
            << ", Spike "
            << audit_common::format_real(
                   stage.force_spike.payable
                       .force_spike_score)
            << ", selected "
            << joined_keys(
                   stage.force_spike.payable.selected_keys)
            << ' ' << pass_fail(
                   stage.gate.force_spike.passed)
            << '\n';
    }
    if (result.evidence.field_regression.has_value()) {
        const auto& stage =
            *result.evidence.field_regression;
        const auto& gate = stage.gate;
        output << "  Field regressions: "
               << gate.stable_fixture_count
               << " stable, " << gate.treatment_losses
               << " lost control agreements, "
               << pass_fail(gate.passed)
               << '\n';
        for (const auto& fixture : gate.fixtures) {
            output
                << "    " << fixture.stable_id
                << ": stable="
                << (fixture.stable_reference_best_set
                        ? "yes"
                        : "no")
                << ", control="
                << (fixture.control_agrees ? "agree" : "miss")
                << ", treatment="
                << (fixture.treatment_agrees
                        ? "agree"
                        : "miss")
                << ", lost="
                << (fixture
                            .treatment_lost_control_agreement
                        ? "yes"
                        : "no")
                << '\n';
        }
    }
    if (result.evidence.treatment_vs_control.has_value()) {
        write_direct_human(
            "treatment vs control",
            *result.evidence.treatment_vs_control, output);
    }
    if (result.evidence.treatment_vs_parent.has_value()) {
        write_direct_human(
            "treatment vs frozen C16",
            *result.evidence.treatment_vs_parent, output);
    }
    if (result.evidence.treatment_vs_handcoded.has_value()) {
        write_direct_human(
            "treatment vs Handcoded",
            *result.evidence.treatment_vs_handcoded,
            output);
    }
    if (result.evidence.fixed_seed_panels.has_value()) {
        const auto& stage =
            *result.evidence.fixed_seed_panels;
        output << "  Fixed-seed panels:\n";
        const auto& panels = stage.evidence;
        const auto& gates = stage.gate.panels;
        for (std::size_t index = 0;
             index < panels.size(); ++index) {
            const auto& summary = panels[index].summary;
            output << "    " << summary.evaluation_seed
                   << ": " << summary.challenger_stats.wins
                   << '-' << summary.challenger_stats.losses
                   << '-' << summary.challenger_stats.draws
                   << ", CR1 "
                   << audit_common::format_real(
                          100.0 *
                          summary.challenger_quartet_cr1.mean)
                   << "% "
                   << (index < gates.size()
                           ? pass_fail(gates[index].passed)
                           : "REJECT")
                   << "\n      by challenger deck:";
            for (std::size_t deck = 0;
                 deck < kDeckCount; ++deck) {
                const auto& stats =
                    summary.challenger_decks[deck];
                output
                    << ' '
                    << deck_name(static_cast<DeckId>(deck))
                    << '=' << stats.wins << '-'
                    << stats.losses << '-' << stats.draws;
            }
            output << '\n';
        }
    }
    if (result.evidence.final_direct_pool.has_value()) {
        const auto& gate =
            *result.evidence.final_direct_pool;
        const auto totals =
            final_direct_totals(result.evidence);
        output
            << "  Final 4,440-game pool: "
            << totals.pooled.wins << '-'
            << totals.pooled.losses << '-'
            << totals.pooled.draws << ", "
            << audit_common::format_real(
                   gate.pooled.challenger_win_rate_percent)
            << "%, Wilson lower "
            << audit_common::format_real(
                   gate.pooled.wilson_lower_95_percent)
            << "% "
            << pass_fail(gate.passed)
            << "\n    by challenger deck:";
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const auto& stats = totals.by_deck[deck];
            output << ' '
                   << deck_name(static_cast<DeckId>(deck))
                   << '=' << stats.wins << '-'
                   << stats.losses << '-' << stats.draws;
        }
        output << '\n';
    }
    if (result.evidence.mixed_field.has_value()) {
        const auto& stage = *result.evidence.mixed_field;
        const auto totals =
            mixed_field_totals(stage.evidence);
        output << "  Mixed-field pooled policy rates:\n";
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const double random_rate =
                win_rate_percent(
                    totals[deck][static_cast<std::size_t>(
                        BotKind::Random)]);
            output << "    "
                   << deck_name(static_cast<DeckId>(deck))
                   << ':';
            for (std::size_t bot = 0;
                 bot < kBotKindCount; ++bot) {
                const double rate =
                    win_rate_percent(totals[deck][bot]);
                output << ' '
                       << bot_name(static_cast<BotKind>(bot))
                       << '='
                       << audit_common::format_real(rate)
                       << "%("
                       << audit_common::format_real(
                              rate - random_rate)
                       << "pp)";
            }
            output << ' '
                   << pass_fail(
                          stage.gate.by_deck[deck]
                              .learned_lift_is_best)
                   << '\n';
        }
    }
}

void write_tsv_report(
    const ProductionEvaluationOutcome& outcome,
    std::ostream& output) {
    output
        << "C17_J1_TSV_BEGIN\n"
        << "kind\tscope\tsubject\tseed\tgames\tvalue_1"
           "\tvalue_2\tvalue_3\tvalue_4\tlabel\tstatus\n";
    write_tsv_row(
        output, "summary", "evaluation", "disposition",
        "", "", "", "", "", "", "",
        evaluation_disposition_name(outcome.disposition));
    if (!outcome.result.has_value()) {
        write_tsv_row(
            output, "error", "evaluation",
            outcome.infrastructure_error.empty()
                ? std::string_view("unspecified")
                : std::string_view(
                      outcome.infrastructure_error),
            "", "", "", "", "", "", "",
            "INFRASTRUCTURE_FAILURE");
        output << "C17_J1_TSV_END\n";
        return;
    }

    const auto& result = *outcome.result;
    write_tsv_row(
        output, "meta", "model", "control", "", "", "",
        "", "", "", "",
        result.sealed.deployments
            .computed_control_fingerprint);
    write_tsv_row(
        output, "meta", "model", "treatment", "", "", "",
        "", "", "", "",
        result.sealed.deployments
            .computed_treatment_fingerprint);
    for (const auto& stage : result.sealed.stages) {
        write_tsv_row(
            output, "stage",
            joint_c17_runner::runner_stage_name(stage.stage),
            "disposition", "", "", "", "", "", "", "",
            runner_disposition_name(stage.disposition));
        for (const auto& failure : stage.failures) {
            write_tsv_row(
                output, "stage_failure",
                joint_c17_runner::runner_stage_name(
                    stage.stage),
                failure, "", "", "", "", "", "", "",
                runner_disposition_name(stage.disposition));
        }
    }

    if (result.evidence.heldout.has_value()) {
        const auto& stage = *result.evidence.heldout;
        const std::string_view status =
            pass_fail(stage.gate.passed);
        write_heldout_scope_tsv(
            output, "pooled", stage.evidence.pooled, status);
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            write_heldout_scope_tsv(
                output,
                deck_name(static_cast<DeckId>(deck)),
                stage.evidence.by_deck[deck], status);
        }
        write_gate_check_tsv(
            output, "heldout", "accounting_exact",
            stage.gate.accounting_exact);
        write_gate_check_tsv(
            output, "heldout", "inputs_finite",
            stage.gate.inputs_finite);
        write_gate_check_tsv(
            output, "heldout", "pooled_losses_improved",
            stage.gate.pooled_losses_improved);
        write_gate_check_tsv(
            output, "heldout", "every_deck_loss_guard",
            stage.gate.every_deck_loss_guard);
        write_gate_check_tsv(
            output, "heldout",
            "green_bias_strictly_shrank",
            stage.gate.green_bias_strictly_shrank);
        write_gate_check_tsv(
            output, "heldout",
            "blue_bias_strictly_shrank",
            stage.gate.blue_bias_strictly_shrank);
        write_gate_check_tsv(
            output, "heldout", "ru_bias_guard",
            stage.gate.ru_bias_guard);
        write_gate_check_tsv(
            output, "heldout", "no_new_material_bias",
            stage.gate.no_new_material_bias);
        write_gate_failures_tsv(
            output, "heldout", stage.gate.failures);
    }
    if (result.evidence.deep_reference.has_value()) {
        const auto& stage =
            *result.evidence.deep_reference;
        const std::string_view status =
            pass_fail(stage.gate.passed);
        const auto& control =
            stage.evidence.control.metrics;
        const auto& treatment =
            stage.evidence.treatment.metrics;
        write_probe_metric_scope_tsv(
            output, "pooled", "control", control, status);
        write_probe_metric_scope_tsv(
            output, "pooled", "treatment", treatment, status);
        const auto write_policy_meta =
            [&](std::string_view policy,
                const probe_runner::ValueCheckpointProbeReport&
                    report) {
                write_tsv_row(
                    output, "deep_policy_meta", policy,
                    report.name, "", "",
                    audit_common::format_real(
                        report.value_priority_residual_weight),
                    report.value_pass_dominance
                        ? "pd0_on"
                        : "pd0_off",
                    continuation_controller_name(
                        report
                            .value_continuation_controller),
                    report
                            .policy_scores_adjusted_for_deployment
                        ? "scores_adjusted"
                        : "scores_raw",
                    report.fingerprint, status);
            };
        write_policy_meta(
            "control", stage.evidence.control);
        write_policy_meta(
            "treatment", stage.evidence.treatment);
        write_common_critic_scope_tsv(
            output, "pooled",
            stage.gate.common_state_critics.pooled, status);
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const auto scope =
                deck_name(static_cast<DeckId>(deck));
            write_probe_metric_scope_tsv(
                output, scope, "control",
                control.by_deck[deck], status);
            write_probe_metric_scope_tsv(
                output, scope, "treatment",
                treatment.by_deck[deck], status);
            write_common_critic_scope_tsv(
                output, scope,
                stage.gate.common_state_critics
                    .by_deck[deck],
                status);
            const auto& stable = stage.gate.by_deck[deck];
            write_tsv_row(
                output, "deep_stable_best_set", scope,
                "control_vs_treatment", "",
                std::to_string(stable.eligible_probes),
                std::to_string(stable.control_agreements),
                std::to_string(
                    stable.treatment_agreements),
                std::to_string(stable.lost_agreements),
                "", "", pass_fail(stable.passed));
        }
        write_probe_decisions_tsv(
            output, "control",
            stage.evidence.control.decisions, status);
        write_probe_decisions_tsv(
            output, "treatment",
            stage.evidence.treatment.decisions, status);
        write_tsv_row(
            output, "deep_hidden_repartition", "dev_v3",
            "control_and_treatment", "", "",
            std::to_string(
                stage.evidence.hidden_repartition.policy_count),
            std::to_string(
                stage.evidence.hidden_repartition.probe_count),
            "", "", "",
            pass_fail(
                stage.evidence.hidden_repartition.passed));
        write_tsv_row(
            output, "force_spike_meta", "treatment",
            stage.force_spike.policy_name, "", "",
            std::to_string(stage.force_spike.worlds),
            std::to_string(stage.force_spike.horizon_turns),
            audit_common::format_real(
                stage.force_spike
                    .value_priority_residual_weight),
            stage.force_spike.value_pass_dominance
                ? "pd0_on"
                : "pd0_off",
            stage.force_spike.model_fingerprint,
            pass_fail(stage.gate.force_spike.passed));
        write_tsv_row(
            output, "force_spike_meta", "treatment",
            "deployment", "", "",
            continuation_controller_name(
                stage.force_spike
                    .value_continuation_controller),
            stage.force_spike
                    .policy_scores_adjusted_for_deployment
                ? "scores_adjusted"
                : "scores_raw",
            stage.force_spike.hidden_repartition_passed
                ? "hidden_invariant"
                : "hidden_mismatch",
            "", stage.force_spike.policy_name,
            pass_fail(stage.gate.force_spike.passed));
        const auto write_force_decision =
            [&](std::string_view scope,
                const probe_runner::ForceSpikeControlDecision&
                    decision,
                bool passed) {
                write_tsv_row(
                    output, "force_spike", scope,
                    decision.stable_id, "", "",
                    audit_common::format_real(
                        decision.pass_score),
                    audit_common::format_real(
                        decision.force_spike_score),
                    "", "", joined_keys(
                        decision.selected_keys),
                    pass_fail(passed));
            };
        write_force_decision(
            "live", stage.force_spike.live,
            stage.gate.force_spike
                .live_uniquely_selects_force_spike);
        write_force_decision(
            "payable", stage.force_spike.payable,
            stage.gate.force_spike
                .payable_uniquely_selects_pass);
        write_gate_check_tsv(
            output, "force_spike", "identities_exact",
            stage.gate.force_spike.identities_exact);
        write_gate_check_tsv(
            output, "force_spike",
            "hidden_repartition_passed",
            stage.gate.force_spike
                .hidden_repartition_passed);
        write_gate_failures_tsv(
            output, "force_spike",
            stage.gate.force_spike.failures);
        write_gate_check_tsv(
            output, "deep_reference", "accounting_exact",
            stage.gate.accounting_exact);
        write_gate_check_tsv(
            output, "deep_reference", "metrics_finite",
            stage.gate.metrics_finite);
        write_gate_check_tsv(
            output, "deep_reference",
            "pooled_regret_no_worse",
            stage.gate.pooled_regret_no_worse);
        write_gate_check_tsv(
            output, "deep_reference",
            "pooled_top_one_no_lower",
            stage.gate.pooled_top_one_no_lower);
        write_gate_check_tsv(
            output, "deep_reference",
            "every_deck_regret_guard",
            stage.gate.every_deck_regret_guard);
        write_gate_check_tsv(
            output, "deep_reference",
            "stable_best_set_loss_guard",
            stage.gate.stable_best_set_loss_guard);
        write_gate_check_tsv(
            output, "deep_reference",
            "required_blue_probes_exact",
            stage.gate.required_blue_probes_exact);
        write_gate_check_tsv(
            output, "deep_reference",
            "required_blue_selections_passed",
            stage.gate.required_blue_selections_passed);
        write_gate_check_tsv(
            output, "deep_reference",
            "hidden_repartition_passed",
            stage.gate.hidden_repartition_passed);
        write_gate_check_tsv(
            output, "deep_common_critic",
            "accounting_exact",
            stage.gate.common_state_critics.accounting_exact);
        write_gate_check_tsv(
            output, "deep_common_critic",
            "predictions_valid",
            stage.gate.common_state_critics
                .predictions_valid);
        write_gate_check_tsv(
            output, "deep_common_critic", "metrics_finite",
            stage.gate.common_state_critics.metrics_finite);
        write_gate_failures_tsv(
            output, "deep_reference", stage.gate.failures);
    }
    if (result.evidence.field_regression.has_value()) {
        const auto& stage =
            *result.evidence.field_regression;
        const auto& gate = stage.gate;
        write_tsv_row(
            output, "field_regression", "six_fixtures",
            "stable_loss", "", "",
            std::to_string(gate.stable_fixture_count),
            std::to_string(gate.control_agreements),
            std::to_string(gate.treatment_agreements),
            std::to_string(gate.treatment_losses),
            std::to_string(gate.treatment_gains),
            pass_fail(gate.passed));
        for (std::size_t index = 0;
             index < gate.fixtures.size(); ++index) {
            const auto& fixture = gate.fixtures[index];
            write_tsv_row(
                output, "field_fixture", fixture.stable_id,
                index < stage.evidence.decisions.size()
                    ? decision_kind_name(
                          stage.evidence.decisions[index]
                              .decision_kind)
                    : std::string_view("missing"),
                "", "",
                fixture.identity_exact ? "1" : "0",
                fixture.reference_valid ? "1" : "0",
                fixture.deployment_valid ? "1" : "0",
                fixture.stable_reference_best_set
                    ? "1"
                    : "0",
                std::string("control=") +
                    (fixture.control_agrees ? "agree" : "miss") +
                    ";treatment=" +
                    (fixture.treatment_agrees
                         ? "agree"
                         : "miss") +
                    ";lost=" +
                    (fixture
                             .treatment_lost_control_agreement
                         ? "true"
                         : "false"),
                fixture
                        .treatment_lost_control_agreement
                    ? "REJECT"
                    : "PASS");
            if (index >= stage.evidence.decisions.size()) {
                continue;
            }
            const auto& decision =
                stage.evidence.decisions[index];
            write_field_policy_tsv(
                output, decision, "parent",
                decision.parent, pass_fail(gate.passed));
            write_field_policy_tsv(
                output, decision, "control",
                decision.control, pass_fail(gate.passed));
            write_field_policy_tsv(
                output, decision, "treatment",
                decision.treatment, pass_fail(gate.passed));
            for (const auto& consequence :
                 decision.forced_consequences) {
                write_tsv_row(
                    output, "field_consequence",
                    decision.stable_id,
                    consequence.descriptor, "", "", "", "",
                    "", "",
                    consequence.public_state_fingerprint,
                    pass_fail(
                        fixture.reference_valid));
            }
        }
        write_gate_check_tsv(
            output, "field_regression", "metadata_exact",
            gate.metadata_exact);
        write_gate_check_tsv(
            output, "field_regression",
            "fixture_count_exact",
            gate.fixture_count_exact);
        write_gate_check_tsv(
            output, "field_regression",
            "every_fixture_valid",
            gate.every_fixture_valid);
        write_gate_check_tsv(
            output, "field_regression",
            "zero_treatment_losses",
            gate.treatment_losses == 0);
        write_gate_failures_tsv(
            output, "field_regression", gate.failures);
    }
    if (result.evidence.treatment_vs_control.has_value()) {
        write_direct_tsv(
            "treatment_vs_control",
            *result.evidence.treatment_vs_control, output);
    }
    if (result.evidence.treatment_vs_parent.has_value()) {
        write_direct_tsv(
            "treatment_vs_parent",
            *result.evidence.treatment_vs_parent, output);
    }
    if (result.evidence.treatment_vs_handcoded.has_value()) {
        write_direct_tsv(
            "treatment_vs_handcoded",
            *result.evidence.treatment_vs_handcoded,
            output);
    }
    if (result.evidence.fixed_seed_panels.has_value()) {
        const auto& stage =
            *result.evidence.fixed_seed_panels;
        for (std::size_t index = 0;
             index < stage.evidence.size(); ++index) {
            const auto& panel = stage.evidence[index].summary;
            const bool gate_present =
                index < stage.gate.panels.size();
            const std::string_view status =
                gate_present
                    ? pass_fail(
                          stage.gate.panels[index].passed)
                    : std::string_view("REJECT");
            const std::string gate_scope =
                "fixed_seed_" +
                std::to_string(panel.evaluation_seed);
            write_tsv_row(
                output, "fixed_seed", "Handcoded", "pooled",
                std::to_string(panel.evaluation_seed),
                std::to_string(panel.challenger_stats.games),
                std::to_string(panel.challenger_stats.wins),
                std::to_string(panel.challenger_stats.losses),
                std::to_string(panel.challenger_stats.draws),
                "", "", status);
            write_tsv_row(
                output, "fixed_seed_clustered", "Handcoded",
                "quartet_cr1",
                std::to_string(panel.evaluation_seed),
                std::to_string(
                    panel.challenger_quartet_cr1.records),
                std::to_string(
                    panel.challenger_quartet_cr1.clusters),
                audit_common::format_real(
                    100.0 *
                    panel.challenger_quartet_cr1.mean),
                audit_common::format_real(
                    100.0 *
                    panel.challenger_quartet_cr1
                        .standard_error),
                audit_common::format_real(
                    100.0 *
                    panel.challenger_quartet_cr1
                        .confidence_low_95),
                audit_common::format_real(
                    100.0 *
                    panel.challenger_quartet_cr1
                        .confidence_high_95),
                status);
            for (std::size_t deck = 0;
                 deck < kDeckCount; ++deck) {
                const auto& stats =
                    panel.challenger_decks[deck];
                write_tsv_row(
                    output, "fixed_seed_deck",
                    deck_name(static_cast<DeckId>(deck)),
                    "Handcoded",
                    std::to_string(panel.evaluation_seed),
                    std::to_string(stats.games),
                    std::to_string(stats.wins),
                    std::to_string(stats.losses),
                    std::to_string(stats.draws), "", "",
                    status);
            }
            if (gate_present) {
                const auto& panel_gate =
                    stage.gate.panels[index];
                write_gate_check_tsv(
                    output, gate_scope, "identity_exact",
                    panel_gate.identity_exact);
                write_gate_check_tsv(
                    output, gate_scope, "accounting_exact",
                    panel_gate.accounting_exact);
                write_gate_check_tsv(
                    output, gate_scope,
                    "clustered_estimate_valid",
                    panel_gate.clustered_estimate_valid);
                write_gate_check_tsv(
                    output, gate_scope,
                    "aggregate_non_losing",
                    panel_gate.aggregate_non_losing);
                write_gate_failures_tsv(
                    output, gate_scope,
                    panel_gate.failures);
            }
        }
        write_gate_check_tsv(
            output, "fixed_seed_panels",
            "panel_count_exact",
            stage.gate.panel_count_exact);
        write_gate_check_tsv(
            output, "fixed_seed_panels", "seeds_exact",
            stage.gate.seeds_exact);
        write_gate_check_tsv(
            output, "fixed_seed_panels",
            "every_panel_passed",
            stage.gate.every_panel_passed);
        write_gate_failures_tsv(
            output, "fixed_seed_panels",
            stage.gate.failures);
    }
    if (result.evidence.final_direct_pool.has_value()) {
        const auto& gate =
            *result.evidence.final_direct_pool;
        const auto totals =
            final_direct_totals(result.evidence);
        write_tsv_row(
            output, "final_pool", "Handcoded", "pooled",
            "", std::to_string(totals.pooled.games),
            std::to_string(totals.pooled.wins),
            std::to_string(totals.pooled.losses),
            std::to_string(totals.pooled.draws),
            audit_common::format_real(
                gate.pooled.challenger_win_rate_percent),
            audit_common::format_real(
                gate.pooled.wilson_lower_95_percent),
            pass_fail(gate.passed));
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const auto& stats = totals.by_deck[deck];
            write_tsv_row(
                output, "final_pool_deck",
                deck_name(static_cast<DeckId>(deck)),
                "Handcoded", "", std::to_string(stats.games),
                std::to_string(stats.wins),
                std::to_string(stats.losses),
                std::to_string(stats.draws), "", "",
                pass_fail(
                    gate.pooled
                        .challenger_deck_strict_wins[deck]));
        }
        write_gate_check_tsv(
            output, "final_pool", "primary_passed",
            gate.primary.passed);
        write_gate_check_tsv(
            output, "final_pool", "fixed_seed_panels_passed",
            gate.fixed_seed_panels.passed);
        write_gate_check_tsv(
            output, "final_pool", "merge_succeeded",
            gate.merge_succeeded);
        write_gate_check_tsv(
            output, "final_pool", "accounting_exact",
            gate.pooled.accounting_exact);
        write_gate_check_tsv(
            output, "final_pool", "rates_finite",
            gate.pooled.rates_finite);
        write_gate_check_tsv(
            output, "final_pool", "aggregate_strict_win",
            gate.pooled.aggregate_strict_win);
        write_gate_check_tsv(
            output, "final_pool",
            "wilson_lower_above_half",
            gate.pooled.wilson_lower_above_half);
        write_gate_check_tsv(
            output, "final_pool",
            "every_challenger_deck_strict_win",
            gate.pooled.every_challenger_deck_strict_win);
        write_gate_failures_tsv(
            output, "final_pool", gate.failures);
        write_gate_failures_tsv(
            output, "final_pool_pooled",
            gate.pooled.failures);
    }
    if (result.evidence.mixed_field.has_value()) {
        const auto& stage = *result.evidence.mixed_field;
        for (const auto& panel : stage.evidence) {
            for (std::size_t deck = 0;
                 deck < kDeckCount; ++deck) {
                const std::size_t random_index =
                    static_cast<std::size_t>(
                        BotKind::Random);
                OutcomeTotals random;
                add_outcomes(
                    panel.summary
                        .deck_bots[deck][random_index],
                    random);
                const double random_rate =
                    win_rate_percent(random);
                for (std::size_t bot = 0;
                     bot < kBotKindCount; ++bot) {
                    OutcomeTotals totals;
                    add_outcomes(
                        panel.summary.deck_bots[deck][bot],
                        totals);
                    const double rate =
                        win_rate_percent(totals);
                    write_tsv_row(
                        output, "mixed_field_seed",
                        deck_name(
                            static_cast<DeckId>(deck)),
                        bot_name(static_cast<BotKind>(bot)),
                        std::to_string(
                            panel.summary.evaluation_seed),
                        std::to_string(totals.games),
                        std::to_string(totals.wins),
                        std::to_string(totals.losses),
                        std::to_string(totals.draws),
                        audit_common::format_real(rate),
                        audit_common::format_real(
                            rate - random_rate),
                        "DESCRIPTIVE");
                }
            }
        }
        const auto totals =
            mixed_field_totals(stage.evidence);
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const auto& deck_gate =
                stage.gate.by_deck[deck];
            const std::size_t random_index =
                static_cast<std::size_t>(BotKind::Random);
            const double random_rate =
                win_rate_percent(
                    totals[deck][random_index]);
            for (std::size_t bot = 0;
                 bot < kBotKindCount; ++bot) {
                const double rate =
                    win_rate_percent(totals[deck][bot]);
                const auto& stats = totals[deck][bot];
                write_tsv_row(
                    output, "mixed_field_policy",
                    deck_name(static_cast<DeckId>(deck)),
                    bot_name(static_cast<BotKind>(bot)), "",
                    std::to_string(stats.games),
                    std::to_string(stats.wins),
                    std::to_string(stats.losses),
                    std::to_string(stats.draws),
                    audit_common::format_real(rate),
                    audit_common::format_real(
                        rate - random_rate),
                    bot == static_cast<std::size_t>(
                               BotKind::Learned)
                        ? pass_fail(
                              deck_gate
                                  .learned_lift_is_best)
                        : "REFERENCE");
            }
            write_tsv_row(
                output, "mixed_field",
                deck_name(deck_gate.deck),
                "learned_lift", "", "",
                audit_common::format_real(
                    deck_gate.random_win_rate_percent),
                audit_common::format_real(
                    deck_gate.learned_win_rate_percent),
                audit_common::format_real(
                    deck_gate
                        .learned_lift_percentage_points),
                audit_common::format_real(
                    deck_gate
                        .best_other_lift_percentage_points),
                bot_name(deck_gate.best_other),
                pass_fail(
                    deck_gate.learned_lift_is_best));
        }
        write_gate_check_tsv(
            output, "mixed_field", "panel_count_exact",
            stage.gate.panel_count_exact);
        write_gate_check_tsv(
            output, "mixed_field", "seeds_exact",
            stage.gate.seeds_exact);
        write_gate_check_tsv(
            output, "mixed_field",
            "policy_identity_exact",
            stage.gate.policy_identity_exact);
        write_gate_check_tsv(
            output, "mixed_field", "accounting_exact",
            stage.gate.accounting_exact);
        write_gate_check_tsv(
            output, "mixed_field", "rates_finite",
            stage.gate.rates_finite);
        write_gate_check_tsv(
            output, "mixed_field",
            "learned_lift_best_on_every_deck",
            stage.gate.learned_lift_best_on_every_deck);
        write_gate_failures_tsv(
            output, "mixed_field", stage.gate.failures);
    }
    output << "C17_J1_TSV_END\n";
}

} // namespace old_school::joint_c17_orchestration
