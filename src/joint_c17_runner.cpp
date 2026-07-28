#include "old_school/joint_c17_runner.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <utility>

namespace old_school::joint_c17_runner {
namespace {

using joint_c17_eval::DeepReferenceGateReport;
using joint_c17_eval::DirectGameplayGateReport;
using joint_c17_eval::FieldRegressionGateReport;
using joint_c17_eval::FinalDirectGateReport;
using joint_c17_eval::FixedSeedPanelGateReport;
using joint_c17_eval::FixedSeedPanelSetGateReport;
using joint_c17_eval::HeldoutGateReport;
using joint_c17_eval::MixedFieldGateReport;

bool is_lower_hex(std::string_view value, std::size_t length) {
    return value.size() == length &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return
                       (character >= '0' && character <= '9') ||
                       (character >= 'a' && character <= 'f');
               });
}

bool is_sha256(std::string_view value) {
    return is_lower_hex(value, 64);
}

void record_failure(
    bool condition, std::string message,
    std::vector<std::string>& failures) {
    if (!condition) {
        failures.push_back(std::move(message));
    }
}

bool resolved_path_valid(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const std::filesystem::path path(value);
    return path.is_absolute() &&
           path.lexically_normal() == path;
}

bool snapshot_valid(
    const TrainingArtifactSnapshot& snapshot) {
    if (!resolved_path_valid(snapshot.resolved_path)) {
        return false;
    }
    if (!snapshot.exists) {
        return !snapshot.regular_file &&
               snapshot.byte_size == 0 &&
               snapshot.sha256.empty() &&
               !snapshot.sha256_verified;
    }
    return snapshot.regular_file &&
           snapshot.byte_size > 0 &&
           is_sha256(snapshot.sha256) &&
           snapshot.sha256_verified;
}

bool trusted_snapshot_valid(
    const EvaluationArtifactRequirement& requirement,
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    const auto expected_path =
        std::filesystem::absolute(
            std::filesystem::path(
                requirement.requested_path))
            .lexically_normal();
    const std::filesystem::path lexical(snapshot.path);
    const std::filesystem::path physical(
        snapshot.physical_path);
    return lexical == expected_path &&
           resolved_path_valid(snapshot.path) &&
           resolved_path_valid(snapshot.physical_path) &&
           lexical.filename() == physical.filename() &&
           snapshot.byte_size > 0 &&
           is_sha256(snapshot.sha256) &&
           snapshot.link_count > 0 &&
           snapshot.modification_nanoseconds >= 0 &&
           snapshot.modification_nanoseconds <
               1'000'000'000 &&
           snapshot.change_nanoseconds >= 0 &&
           snapshot.change_nanoseconds <
               1'000'000'000 &&
           snapshot.byte_size ==
               requirement.expected_byte_size &&
           snapshot.sha256 ==
               requirement.expected_sha256;
}

bool capture_report_passed(
    const ArtifactSetCaptureReport& report) {
    return report.snapshotter_present &&
           report.manifest_valid &&
           report.required_roles_exact &&
           report.canonical_bundle_present &&
           report.all_snapshots_captured;
}

bool learned_bot_config_exact(
    const BotConfig& bot, bool pass_dominance,
    LearnedContinuationController controller) {
    return bot.kind == BotKind::Learned &&
           bot.learned_variant ==
               LearnedVariant::ValueSearchChampion &&
           bot.rollouts_per_action == 8 &&
           bot.exploration_rate == 0.0 &&
           bot.value_continuation_epsilon == 0.0 &&
           bot.value_priority_residual_weight == 0.0 &&
           bot.value_pass_dominance == pass_dominance &&
           !bot.value_adversarial_blocks &&
           bot.value_continuation_controller == controller &&
           bot.training_games == kCanonicalTrainingGames;
}

bool learned_search_config_exact(
    const LearnedSearchConfig& search, bool pass_dominance,
    LearnedContinuationController controller) {
    return search.worlds == 8 &&
           search.rollouts_per_world == 1 &&
           search.horizon_turns ==
               kLearnedValueSearchHorizonTurns &&
           search.continuation_variant ==
               LearnedVariant::ValueSearchChampion &&
           search.value_continuation_epsilon == 0.0 &&
           search.blend_shallow_prior &&
           search.value_priority_residual_weight == 0.0 &&
           search.value_pass_dominance == pass_dominance &&
           search.value_continuation_controller == controller &&
           search.evaluation_threads == 1;
}

bool deployment_recipe_exact(
    const LearnedJointC17Deployment& deployment,
    LearnedJointC17Arm arm, std::string_view policy_token,
    bool pass_dominance,
    LearnedContinuationController controller) {
    return deployment.arm == arm &&
           deployment.policy_token == policy_token &&
           learned_bot_config_exact(
               deployment.bot, pass_dominance, controller) &&
           learned_search_config_exact(
               deployment.search, pass_dominance, controller);
}

bool heldout_consistent(const HeldoutGateReport& gate) {
    return gate.passed ==
           (gate.accounting_exact &&
            gate.inputs_finite &&
            gate.pooled_losses_improved &&
            gate.every_deck_loss_guard &&
            gate.green_bias_strictly_shrank &&
            gate.blue_bias_strictly_shrank &&
            gate.ru_bias_guard &&
            gate.no_new_material_bias);
}

bool heldout_structure_valid(const HeldoutGateReport& gate) {
    return gate.accounting_exact && gate.inputs_finite;
}

bool force_spike_consistent(
    const joint_c17_eval::ForceSpikeSelectionGateReport& gate) {
    return gate.passed ==
           (gate.identities_exact &&
            gate.live_uniquely_selects_force_spike &&
            gate.payable_uniquely_selects_pass &&
            gate.hidden_repartition_passed);
}

bool deep_reference_consistent(
    const DeepReferenceGateReport& gate) {
    const bool expected =
        gate.accounting_exact &&
        gate.metrics_finite &&
        gate.pooled_regret_no_worse &&
        gate.pooled_top_one_no_lower &&
        gate.every_deck_regret_guard &&
        gate.stable_best_set_loss_guard &&
        gate.required_blue_probes_exact &&
        gate.required_blue_selections_passed &&
        gate.hidden_repartition_passed &&
        gate.force_spike.passed &&
        gate.common_state_critics.accounting_exact &&
        gate.common_state_critics.predictions_valid &&
        gate.common_state_critics.metrics_finite;
    bool deck_reports_consistent = true;
    for (std::size_t deck = 0;
         deck < gate.by_deck.size(); ++deck) {
        const auto& report = gate.by_deck[deck];
        deck_reports_consistent =
            deck_reports_consistent &&
            report.root_deck ==
                static_cast<DeckId>(deck) &&
            report.control_agreements <=
                report.eligible_probes &&
            report.treatment_agreements <=
                report.eligible_probes &&
            report.lost_agreements <=
                report.control_agreements &&
            report.passed ==
                (report.lost_agreements <=
                 joint_c17_eval::
                     kMaximumStableBestSetLossesPerDeck);
    }
    const bool stable_guard_consistent =
        gate.stable_best_set_loss_guard ==
        std::all_of(
            gate.by_deck.begin(), gate.by_deck.end(),
            [](const auto& deck) {
                return deck.passed;
            });
    const auto critic_metrics_valid =
        [](const joint_c17_eval::
               CommonStateCriticMetrics& metrics,
           std::size_t expected_probes) {
            return metrics.probe_count ==
                       expected_probes &&
                   std::isfinite(metrics.brier) &&
                   std::isfinite(
                       metrics.soft_log_loss) &&
                   std::isfinite(metrics.signed_bias) &&
                   std::isfinite(metrics.ece);
        };
    bool common_state_content_valid = true;
    for (const auto& metrics :
         gate.common_state_critics.pooled.models) {
        common_state_content_valid =
            common_state_content_valid &&
            critic_metrics_valid(
                metrics,
                joint_c17_eval::kExpectedProbeCount);
    }
    for (const auto& deck :
         gate.common_state_critics.by_deck) {
        for (const auto& metrics : deck.models) {
            common_state_content_valid =
                common_state_content_valid &&
                critic_metrics_valid(
                    metrics,
                    joint_c17_eval::
                        kExpectedProbesPerDeck);
        }
    }
    return force_spike_consistent(gate.force_spike) &&
           deck_reports_consistent &&
           stable_guard_consistent &&
           common_state_content_valid &&
           gate.passed == expected;
}

bool deep_reference_structure_valid(
    const DeepReferenceGateReport& gate) {
    return gate.accounting_exact &&
           gate.metrics_finite &&
           gate.required_blue_probes_exact &&
           gate.hidden_repartition_passed &&
           gate.force_spike.identities_exact &&
           gate.force_spike.hidden_repartition_passed &&
           gate.common_state_critics.accounting_exact &&
           gate.common_state_critics.predictions_valid &&
           gate.common_state_critics.metrics_finite;
}

bool field_regression_consistent(
    const FieldRegressionGateReport& gate) {
    const bool fixture_count_exact =
        gate.fixtures.size() ==
        joint_c17_eval::kFieldRegressionFixtureCount;
    bool fixture_semantics_exact =
        fixture_count_exact;
    for (std::size_t index = 0;
         index < gate.fixtures.size(); ++index) {
        const auto& fixture = gate.fixtures[index];
        const bool stable_loss =
            fixture.stable_reference_best_set &&
            fixture.control_agrees &&
            !fixture.treatment_agrees;
        fixture_semantics_exact =
            fixture_semantics_exact &&
            index <
                joint_c17_eval::
                    kRequiredFieldRegressionIds.size() &&
            fixture.stable_id ==
                joint_c17_eval::
                    kRequiredFieldRegressionIds[index] &&
            fixture
                    .treatment_lost_control_agreement ==
                stable_loss &&
            (fixture.stable_reference_best_set ||
             (!fixture.control_agrees &&
              !fixture.treatment_agrees));
    }
    const bool every_fixture_valid =
        fixture_count_exact &&
        std::all_of(
            gate.fixtures.begin(), gate.fixtures.end(),
            [](const auto& fixture) {
                return fixture.identity_exact &&
                       fixture.reference_valid &&
                       fixture.deployment_valid;
            });
    const auto stable_fixture_count =
        static_cast<std::size_t>(std::count_if(
            gate.fixtures.begin(), gate.fixtures.end(),
            [](const auto& fixture) {
                return fixture.stable_reference_best_set;
            }));
    const auto control_agreements =
        static_cast<std::size_t>(std::count_if(
            gate.fixtures.begin(), gate.fixtures.end(),
            [](const auto& fixture) {
                return
                    fixture.stable_reference_best_set &&
                    fixture.control_agrees;
            }));
    const auto treatment_agreements =
        static_cast<std::size_t>(std::count_if(
            gate.fixtures.begin(), gate.fixtures.end(),
            [](const auto& fixture) {
                return
                    fixture.stable_reference_best_set &&
                    fixture.treatment_agrees;
            }));
    const auto treatment_losses =
        static_cast<std::size_t>(std::count_if(
            gate.fixtures.begin(), gate.fixtures.end(),
            [](const auto& fixture) {
                return
                    fixture.treatment_lost_control_agreement;
            }));
    const auto treatment_gains =
        static_cast<std::size_t>(std::count_if(
            gate.fixtures.begin(), gate.fixtures.end(),
            [](const auto& fixture) {
                return
                    fixture.stable_reference_best_set &&
                    !fixture.control_agrees &&
                    fixture.treatment_agrees;
            }));
    return fixture_semantics_exact &&
           gate.fixture_count_exact ==
               fixture_count_exact &&
           gate.every_fixture_valid ==
               every_fixture_valid &&
           gate.stable_fixture_count ==
               stable_fixture_count &&
           gate.control_agreements ==
               control_agreements &&
           gate.treatment_agreements ==
               treatment_agreements &&
           gate.treatment_losses ==
               treatment_losses &&
           gate.treatment_gains ==
               treatment_gains &&
           gate.passed ==
           (gate.metadata_exact &&
            gate.fixture_count_exact &&
            gate.every_fixture_valid &&
            gate.treatment_losses == 0);
}

bool field_regression_structure_valid(
    const FieldRegressionGateReport& gate) {
    return gate.metadata_exact &&
           gate.fixture_count_exact &&
           gate.every_fixture_valid;
}

bool direct_gameplay_consistent(
    const DirectGameplayGateReport& gate) {
    const bool every_deck =
        std::all_of(
            gate.challenger_deck_strict_wins.begin(),
            gate.challenger_deck_strict_wins.end(),
            [](bool passed) { return passed; });
    const bool numeric_rates_valid =
        std::isfinite(
            gate.challenger_win_rate_percent) &&
        std::isfinite(
            gate.wilson_lower_95_percent) &&
        gate.challenger_win_rate_percent >= 0.0 &&
        gate.challenger_win_rate_percent <= 100.0 &&
        gate.wilson_lower_95_percent >= 0.0 &&
        gate.wilson_lower_95_percent <= 100.0;
    return (!gate.rates_finite ||
            numeric_rates_valid) &&
           gate.every_challenger_deck_strict_win ==
               every_deck &&
           gate.passed ==
               (gate.identity_exact &&
                gate.accounting_exact &&
                gate.clustered_estimate_valid &&
                gate.rates_finite &&
                gate.aggregate_strict_win &&
                gate.wilson_lower_above_half &&
                gate.every_challenger_deck_strict_win);
}

bool direct_gameplay_structure_valid(
    const DirectGameplayGateReport& gate) {
    return gate.identity_exact &&
           gate.accounting_exact &&
           gate.clustered_estimate_valid &&
           gate.rates_finite;
}

bool fixed_panel_consistent(
    const FixedSeedPanelGateReport& gate) {
    return gate.passed ==
           (gate.identity_exact &&
            gate.accounting_exact &&
            gate.clustered_estimate_valid &&
            gate.aggregate_non_losing);
}

bool fixed_panel_structure_valid(
    const FixedSeedPanelGateReport& gate) {
    return gate.identity_exact &&
           gate.accounting_exact &&
           gate.clustered_estimate_valid;
}

bool fixed_panel_set_consistent(
    const FixedSeedPanelSetGateReport& gate) {
    const bool panel_count_exact =
        gate.panels.size() ==
        joint_c17_eval::kFixedSeedPanelCount;
    const bool every_panel =
        std::all_of(
            gate.panels.begin(), gate.panels.end(),
            [](const auto& panel) {
                return panel.passed;
            });
    return std::all_of(
               gate.panels.begin(), gate.panels.end(),
               fixed_panel_consistent) &&
           gate.panel_count_exact == panel_count_exact &&
           gate.every_panel_passed == every_panel &&
           gate.passed ==
               (gate.panel_count_exact &&
                gate.seeds_exact &&
                gate.every_panel_passed);
}

bool fixed_panel_set_structure_valid(
    const FixedSeedPanelSetGateReport& gate) {
    return gate.panel_count_exact &&
           gate.seeds_exact &&
           std::all_of(
               gate.panels.begin(), gate.panels.end(),
               fixed_panel_structure_valid);
}

bool final_pool_consistent(
    const joint_c17_eval::FinalDirectPoolGateReport& gate) {
    const bool every_deck =
        std::all_of(
            gate.challenger_deck_strict_wins.begin(),
            gate.challenger_deck_strict_wins.end(),
            [](bool passed) { return passed; });
    const bool numeric_rates_valid =
        std::isfinite(
            gate.challenger_win_rate_percent) &&
        std::isfinite(
            gate.wilson_lower_95_percent) &&
        gate.challenger_win_rate_percent >= 0.0 &&
        gate.challenger_win_rate_percent <= 100.0 &&
        gate.wilson_lower_95_percent >= 0.0 &&
        gate.wilson_lower_95_percent <= 100.0;
    return (!gate.rates_finite ||
            numeric_rates_valid) &&
           gate.every_challenger_deck_strict_win ==
               every_deck &&
           gate.passed ==
               (gate.accounting_exact &&
                gate.rates_finite &&
                gate.aggregate_strict_win &&
                gate.wilson_lower_above_half &&
                gate.every_challenger_deck_strict_win);
}

bool final_direct_consistent(
    const FinalDirectGateReport& gate) {
    return direct_gameplay_consistent(gate.primary) &&
           fixed_panel_set_consistent(
               gate.fixed_seed_panels) &&
           final_pool_consistent(gate.pooled) &&
           gate.passed ==
               (gate.primary.passed &&
                gate.fixed_seed_panels.passed &&
                gate.merge_succeeded &&
                gate.pooled.passed);
}

bool final_direct_structure_valid(
    const FinalDirectGateReport& gate,
    const DirectGameplayGateReport& primary,
    const FixedSeedPanelSetGateReport& fixed_panels) {
    return gate.primary == primary &&
           gate.fixed_seed_panels == fixed_panels &&
           direct_gameplay_structure_valid(gate.primary) &&
           fixed_panel_set_structure_valid(
               gate.fixed_seed_panels) &&
           gate.merge_succeeded &&
           gate.pooled.accounting_exact &&
           gate.pooled.rates_finite;
}

bool mixed_field_consistent(
    const MixedFieldGateReport& gate) {
    bool deck_identities_exact = true;
    bool deck_metrics_consistent = true;
    for (std::size_t deck = 0;
         deck < gate.by_deck.size(); ++deck) {
        const auto& report = gate.by_deck[deck];
        deck_identities_exact =
            deck_identities_exact &&
            report.deck ==
                static_cast<DeckId>(deck);
        const bool bot_identity_valid =
            static_cast<std::size_t>(
                report.best_other) <
                kBotKindCount &&
            report.best_other != BotKind::Learned &&
            (report.best_other == BotKind::Random
                 ? std::abs(
                       report
                           .best_other_lift_percentage_points) <=
                       joint_c17_eval::
                           kMixedFieldLiftTolerance
                 : report
                           .best_other_lift_percentage_points >
                       joint_c17_eval::
                           kMixedFieldLiftTolerance);
        const bool numerics_valid =
            std::isfinite(
                report.random_win_rate_percent) &&
            std::isfinite(
                report.learned_win_rate_percent) &&
            std::isfinite(
                report
                    .learned_lift_percentage_points) &&
            std::isfinite(
                report
                    .best_other_lift_percentage_points) &&
            report.random_win_rate_percent >= 0.0 &&
            report.random_win_rate_percent <= 100.0 &&
            report.learned_win_rate_percent >= 0.0 &&
            report.learned_win_rate_percent <= 100.0 &&
            report.best_other_lift_percentage_points >=
                0.0;
        const bool lift_exact =
            numerics_valid &&
            std::abs(
                report.learned_lift_percentage_points -
                (report.learned_win_rate_percent -
                 report.random_win_rate_percent)) <=
                joint_c17_eval::
                    kMixedFieldLiftTolerance;
        const bool expected_best =
            numerics_valid &&
            lift_exact &&
            report.learned_lift_percentage_points +
                    joint_c17_eval::
                        kMixedFieldLiftTolerance >=
                0.0 &&
            report.learned_lift_percentage_points +
                    joint_c17_eval::
                        kMixedFieldLiftTolerance >=
                report
                    .best_other_lift_percentage_points;
        deck_metrics_consistent =
            deck_metrics_consistent &&
            bot_identity_valid &&
            report.rates_finite == numerics_valid &&
            lift_exact &&
            report.learned_lift_is_best ==
                expected_best;
    }
    const bool all_rates_finite =
        std::all_of(
            gate.by_deck.begin(), gate.by_deck.end(),
            [](const auto& deck) {
                return deck.rates_finite;
            });
    const bool every_deck =
        std::all_of(
            gate.by_deck.begin(), gate.by_deck.end(),
            [](const auto& deck) {
                return deck.learned_lift_is_best;
            });
    return deck_identities_exact &&
           deck_metrics_consistent &&
           gate.rates_finite == all_rates_finite &&
           gate.learned_lift_best_on_every_deck ==
               every_deck &&
           gate.passed ==
               (gate.panel_count_exact &&
                gate.seeds_exact &&
                gate.policy_identity_exact &&
                gate.accounting_exact &&
                gate.rates_finite &&
                gate.learned_lift_best_on_every_deck);
}

bool mixed_field_structure_valid(
    const MixedFieldGateReport& gate) {
    return gate.panel_count_exact &&
           gate.seeds_exact &&
           gate.policy_identity_exact &&
           gate.accounting_exact &&
           gate.rates_finite &&
           std::all_of(
               gate.by_deck.begin(), gate.by_deck.end(),
               [](const auto& deck) {
                   return deck.rates_finite;
               });
}

void append_failures(
    std::vector<std::string>& destination,
    const std::vector<std::string>& source) {
    destination.insert(
        destination.end(), source.begin(), source.end());
}

void finish_failure(
    SealedRunReport& result, RunnerStage stage,
    RunnerDisposition disposition,
    std::vector<std::string> failures) {
    result.disposition = disposition;
    result.terminal_stage = stage;
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);
    result.stages.push_back({
        .stage = stage,
        .disposition = disposition,
        .failures = std::move(failures),
    });
}

void record_pass(
    SealedRunReport& result, RunnerStage stage) {
    result.stages.push_back({
        .stage = stage,
        .disposition = RunnerDisposition::Passed,
        .failures = {},
    });
}

template <typename Report>
bool invoke_stage(
    const std::function<Report()>& callback,
    std::optional<Report>& destination,
    RunnerStage stage, SealedRunReport& result) {
    if (!callback) {
        finish_failure(
            result, stage,
            RunnerDisposition::InfrastructureFailure,
            {"sealed stage callback is missing"});
        return false;
    }
    try {
        destination = callback();
        return true;
    } catch (const std::exception& error) {
        finish_failure(
            result, stage,
            RunnerDisposition::InfrastructureFailure,
            {"sealed stage callback threw: " +
             std::string(error.what())});
    } catch (...) {
        finish_failure(
            result, stage,
            RunnerDisposition::InfrastructureFailure,
            {"sealed stage callback threw a non-standard exception"});
    }
    return false;
}

template <typename Report, typename OutcomeSetter>
bool accept_or_stop(
    const Report& gate, bool consistent,
    bool structure_valid, RunnerStage stage,
    SealedRunReport& result,
    OutcomeSetter&& set_outcome) {
    if (!consistent || !structure_valid) {
        std::vector<std::string> failures;
        if (!consistent) {
            failures.push_back(
                "stage gate report is internally inconsistent");
        }
        if (!structure_valid) {
            failures.push_back(
                "stage gate evidence or accounting is invalid");
        }
        append_failures(failures, gate.failures);
        finish_failure(
            result, stage,
            RunnerDisposition::InfrastructureFailure,
            std::move(failures));
        return false;
    }
    std::forward<OutcomeSetter>(
        set_outcome)(gate.passed);
    if (!gate.passed) {
        std::vector<std::string> failures = gate.failures;
        if (failures.empty()) {
            failures.push_back(
                "stage scientific acceptance gate rejected");
        }
        finish_failure(
            result, stage, RunnerDisposition::Rejected,
            std::move(failures));
        return false;
    }
    record_pass(result, stage);
    return true;
}

bool require_stage_enabled(
    bool enabled, RunnerStage stage,
    SealedRunReport& result) {
    if (enabled) {
        return true;
    }
    finish_failure(
        result, stage,
        RunnerDisposition::InfrastructureFailure,
        {"joint evaluator did not enable the expected next stage"});
    return false;
}

} // namespace

BotConfig make_control_bot(
    std::shared_ptr<const LearnedModel> model) {
    return {
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 8,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = kCanonicalTrainingGames,
        .learned_model = std::move(model),
    };
}

BotConfig make_treatment_bot(
    std::shared_ptr<const LearnedModel> model) {
    auto bot = make_control_bot(std::move(model));
    bot.value_pass_dominance = true;
    bot.value_continuation_controller =
        LearnedContinuationController::PublicStackPassV1;
    return bot;
}

BotConfig make_parent_bot(
    std::shared_ptr<const LearnedModel> model) {
    return make_control_bot(std::move(model));
}

BotConfig make_handcoded_bot() {
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
        .training_games = kCanonicalTrainingGames,
        .learned_model = {},
    };
}

joint_c17_eval::PolicyRecipeEvidence make_control_recipe() {
    return {
        .policy_token =
            std::string(kLearnedJointC17ControlPolicyToken),
        .horizon_turns = kLearnedValueSearchHorizonTurns,
        .blend_shallow_prior = true,
    };
}

joint_c17_eval::PolicyRecipeEvidence make_treatment_recipe() {
    return {
        .policy_token =
            std::string(kLearnedJointC17TreatmentPolicyToken),
        .horizon_turns = kLearnedValueSearchHorizonTurns,
        .blend_shallow_prior = true,
    };
}

joint_c17_eval::PolicyRecipeEvidence make_parent_recipe() {
    return {
        .policy_token =
            std::string(
                joint_c17_eval::
                    kFrozenC16EvidencePolicyToken),
        .horizon_turns = kLearnedValueSearchHorizonTurns,
        .blend_shallow_prior = true,
    };
}

joint_c17_eval::PolicyRecipeEvidence make_handcoded_recipe() {
    return {
        .policy_token =
            std::string(
                joint_c17_eval::kHandcodedEvidencePolicyToken),
        .horizon_turns = 0,
        .blend_shallow_prior = false,
    };
}

TrainingPublicationGateReport validate_training_publication(
    const TrainingPublicationEvidence& evidence) {
    TrainingPublicationGateReport gate;
    gate.canonical_paths_exact =
        evidence.before.requested_path ==
            kCanonicalArtifactPath &&
        evidence.after.requested_path ==
            kCanonicalArtifactPath &&
        evidence.before.resolved_path ==
            evidence.after.resolved_path &&
        resolved_path_valid(
            evidence.before.resolved_path);
    gate.before_snapshot_valid =
        snapshot_valid(evidence.before);
    gate.target_absent_before =
        !evidence.before.exists;
    gate.after_snapshot_valid =
        snapshot_valid(evidence.after) &&
        evidence.after.exists;
    gate.atomic_no_replace_confirmed =
        evidence.atomic_no_replace_confirmed;

    record_failure(
        gate.canonical_paths_exact,
        "artifact snapshots do not use the canonical C17-J1 path",
        gate.failures);
    record_failure(
        gate.before_snapshot_valid,
        "artifact pre-publication snapshot is malformed",
        gate.failures);
    record_failure(
        gate.target_absent_before,
        "canonical C17-J1 artifact already existed before publication",
        gate.failures);
    record_failure(
        gate.after_snapshot_valid,
        "artifact post-publication snapshot is malformed or absent",
        gate.failures);
    record_failure(
        gate.atomic_no_replace_confirmed,
        "artifact publication was not confirmed atomic no-replace",
        gate.failures);
    gate.passed =
        gate.canonical_paths_exact &&
        gate.before_snapshot_valid &&
        gate.target_absent_before &&
        gate.after_snapshot_valid &&
        gate.atomic_no_replace_confirmed;
    return gate;
}

artifact_integrity::RegularFileSnapshot
snapshot_evaluation_artifact(
    std::string_view requested_path) {
    return artifact_integrity::snapshot_regular_file(
        std::filesystem::path(requested_path));
}

EvaluationArtifactIntegrityRequest
make_evaluation_artifact_integrity_request(
    std::uintmax_t bundle_byte_size,
    std::string bundle_sha256) {
    return {
        .artifacts =
            {
                {
                    .role =
                        std::string(
                            kJointBundleArtifactRole),
                    .requested_path =
                        std::string(
                            kCanonicalArtifactPath),
                    .expected_byte_size =
                        bundle_byte_size,
                    .expected_sha256 =
                        std::move(bundle_sha256),
                },
                {
                    .role =
                        std::string(
                            kParentModelArtifactRole),
                    .requested_path =
                        std::string(
                            kParentArtifactPath),
                    .expected_byte_size =
                        kParentArtifactByteSize,
                    .expected_sha256 =
                        std::string(
                            kParentArtifactSha256),
                },
                {
                    .role =
                        std::string(
                            kLabelCacheArtifactRole),
                    .requested_path =
                        std::string(
                            kLabelCacheArtifactPath),
                    .expected_byte_size =
                        kLabelCacheArtifactByteSize,
                    .expected_sha256 =
                        std::string(
                            kLabelCacheArtifactSha256),
                },
            },
    };
}

std::optional<EvaluationArtifactIntegrityRequest>
evaluation_artifact_integrity_from_publication(
    const TrainingPublicationEvidence& publication) {
    if (!validate_training_publication(publication).passed) {
        return std::nullopt;
    }
    return make_evaluation_artifact_integrity_request(
        publication.after.byte_size,
        publication.after.sha256);
}

namespace {

ArtifactSetCaptureReport capture_evaluation_artifact_set_with(
    const EvaluationArtifactIntegrityRequest& request,
    const testing::ArtifactSnapshotter& snapshotter) {
    ArtifactSetCaptureReport report;
    report.snapshotter_present =
        static_cast<bool>(snapshotter);
    if (!report.snapshotter_present) {
        report.failures.push_back(
            "trusted artifact snapshotter is missing");
        return report;
    }

    report.manifest_valid =
        request.artifacts.size() == 3;
    if (request.artifacts.size() != 3) {
        report.failures.push_back(
            "artifact integrity manifest must contain "
            "exactly three artifacts");
    }
    std::size_t joint_bundles = 0;
    std::size_t parent_models = 0;
    std::size_t label_caches = 0;
    for (std::size_t index = 0;
         index < request.artifacts.size(); ++index) {
        const auto& artifact = request.artifacts[index];
        const bool fields_present =
            !artifact.role.empty() &&
            !artifact.requested_path.empty() &&
            artifact.expected_byte_size > 0 &&
            is_sha256(artifact.expected_sha256);
        report.manifest_valid =
            report.manifest_valid && fields_present;
        if (!fields_present) {
            report.failures.push_back(
                "artifact manifest identity is incomplete");
        }
        for (std::size_t prior = 0;
             prior < index; ++prior) {
            const auto& other =
                request.artifacts[prior];
            if (artifact.role == other.role) {
                report.manifest_valid = false;
                report.failures.push_back(
                    "artifact manifest contains a duplicate "
                    "role: " +
                    artifact.role);
            }
            if (artifact.requested_path ==
                other.requested_path) {
                report.manifest_valid = false;
                report.failures.push_back(
                    "artifact manifest contains a duplicate "
                    "requested path: " +
                    artifact.requested_path);
            }
        }
        if (artifact.role ==
                kJointBundleArtifactRole) {
            ++joint_bundles;
            report.canonical_bundle_present =
                artifact.requested_path ==
                    kCanonicalArtifactPath &&
                artifact.expected_byte_size > 0 &&
                is_sha256(
                    artifact.expected_sha256);
            if (!report.canonical_bundle_present) {
                report.manifest_valid = false;
                report.failures.push_back(
                    "joint bundle path or published identity "
                    "is invalid");
            }
        } else if (artifact.role ==
                   kParentModelArtifactRole) {
            ++parent_models;
            const bool exact =
                artifact.requested_path ==
                    kParentArtifactPath &&
                artifact.expected_byte_size ==
                    kParentArtifactByteSize &&
                artifact.expected_sha256 ==
                    kParentArtifactSha256;
            report.manifest_valid =
                report.manifest_valid && exact;
            if (!exact) {
                report.failures.push_back(
                    "parent model requirement does not match "
                    "its frozen path, size, and SHA-256");
            }
        } else if (artifact.role ==
                   kLabelCacheArtifactRole) {
            ++label_caches;
            const bool exact =
                artifact.requested_path ==
                    kLabelCacheArtifactPath &&
                artifact.expected_byte_size ==
                    kLabelCacheArtifactByteSize &&
                artifact.expected_sha256 ==
                    kLabelCacheArtifactSha256;
            report.manifest_valid =
                report.manifest_valid && exact;
            if (!exact) {
                report.failures.push_back(
                    "label-cache requirement does not match "
                    "its frozen path, size, and SHA-256");
            }
        } else {
            report.manifest_valid = false;
            report.failures.push_back(
                "artifact manifest contains an unknown role: " +
                artifact.role);
        }
    }
    report.required_roles_exact =
        request.artifacts.size() == 3 &&
        joint_bundles == 1 &&
        parent_models == 1 &&
        label_caches == 1;
    record_failure(
        report.required_roles_exact,
        "artifact manifest must contain exactly one parent "
        "model, joint bundle, and label cache",
        report.failures);
    record_failure(
        report.canonical_bundle_present,
        "artifact manifest does not contain the canonical "
        "joint C17 bundle",
        report.failures);
    if (!report.manifest_valid ||
        !report.required_roles_exact ||
        !report.canonical_bundle_present) {
        return report;
    }

    report.all_snapshots_captured = true;
    report.artifacts.reserve(
        request.artifacts.size());
    for (const auto& artifact : request.artifacts) {
        try {
            auto resolved =
                snapshotter(
                    artifact.requested_path);
            if (!trusted_snapshot_valid(
                    artifact, resolved)) {
                report.all_snapshots_captured = false;
                report.failures.push_back(
                    "trusted snapshot does not match the "
                    "pinned identity for role: " +
                    artifact.role);
                continue;
            }
            report.artifacts.push_back({
                .role = artifact.role,
                .requested_path =
                    artifact.requested_path,
                .expected_byte_size =
                    artifact.expected_byte_size,
                .expected_sha256 =
                    artifact.expected_sha256,
                .resolved = std::move(resolved),
            });
        } catch (const std::exception& error) {
            report.all_snapshots_captured = false;
            report.failures.push_back(
                "trusted snapshotter failed for role '" +
                artifact.role + "': " + error.what());
        } catch (...) {
            report.all_snapshots_captured = false;
            report.failures.push_back(
                "trusted snapshotter threw a non-standard "
                "exception for role: " +
                artifact.role);
        }
    }
    report.all_snapshots_captured =
        report.all_snapshots_captured &&
        report.artifacts.size() ==
            request.artifacts.size();
    for (std::size_t index = 0;
         index < report.artifacts.size(); ++index) {
        for (std::size_t prior = 0;
             prior < index; ++prior) {
            const auto& left =
                report.artifacts[prior].resolved;
            const auto& right =
                report.artifacts[index].resolved;
            if (left.device == right.device &&
                left.inode == right.inode) {
                report.all_snapshots_captured = false;
                report.failures.push_back(
                    "distinct artifact roles resolve to the "
                    "same device and inode");
            }
        }
    }
    return report;
}

} // namespace

ArtifactSetCaptureReport capture_evaluation_artifact_set(
    const EvaluationArtifactIntegrityRequest& request) {
    return capture_evaluation_artifact_set_with(
        request, snapshot_evaluation_artifact);
}

EvaluationArtifactIntegrityGateReport
validate_evaluation_artifact_integrity(
    ArtifactSetCaptureReport before,
    ArtifactSetCaptureReport after) {
    EvaluationArtifactIntegrityGateReport gate;
    gate.before = std::move(before);
    gate.after = std::move(after);
    gate.preflight_passed =
        capture_report_passed(gate.before);
    gate.postflight_passed =
        capture_report_passed(gate.after);
    gate.artifact_set_unchanged =
        gate.preflight_passed &&
        gate.postflight_passed &&
        gate.before.artifacts ==
            gate.after.artifacts;
    append_failures(
        gate.failures, gate.before.failures);
    append_failures(
        gate.failures, gate.after.failures);
    record_failure(
        gate.preflight_passed,
        "artifact integrity preflight failed",
        gate.failures);
    record_failure(
        gate.postflight_passed,
        "artifact integrity postflight failed",
        gate.failures);
    record_failure(
        gate.artifact_set_unchanged,
        "an evaluation artifact changed between preflight "
        "and postflight",
        gate.failures);
    gate.passed =
        gate.preflight_passed &&
        gate.postflight_passed &&
        gate.artifact_set_unchanged;
    return gate;
}

ModelFingerprintGateReport validate_model_fingerprints(
    const joint_c17_eval::JointC17ExpectedModelFingerprints&
        fingerprints) {
    ModelFingerprintGateReport gate;
    gate.control_is_sha256 =
        is_sha256(fingerprints.control);
    gate.treatment_is_sha256 =
        is_sha256(fingerprints.treatment);
    gate.fingerprints_distinct =
        fingerprints.control != fingerprints.treatment;
    gate.parent_excluded =
        fingerprints.control !=
            kLearnedJointC17ParentFingerprint &&
        fingerprints.treatment !=
            kLearnedJointC17ParentFingerprint;
    record_failure(
        gate.control_is_sha256,
        "control model fingerprint is not a lower-case SHA-256",
        gate.failures);
    record_failure(
        gate.treatment_is_sha256,
        "treatment model fingerprint is not a lower-case SHA-256",
        gate.failures);
    record_failure(
        gate.fingerprints_distinct,
        "control and treatment model fingerprints are identical",
        gate.failures);
    record_failure(
        gate.parent_excluded,
        "a C17-J1 model fingerprint equals the frozen C16 parent",
        gate.failures);
    gate.passed =
        gate.control_is_sha256 &&
        gate.treatment_is_sha256 &&
        gate.fingerprints_distinct &&
        gate.parent_excluded;
    return gate;
}

FrozenDeploymentGateReport validate_frozen_deployments(
    const FrozenDeploymentEvidence& deployments,
    const joint_c17_eval::JointC17ExpectedModelFingerprints&
        fingerprints) {
    FrozenDeploymentGateReport gate;
    gate.control_exact =
        deployment_recipe_exact(
            deployments.control,
            LearnedJointC17Arm::Control,
            kLearnedJointC17ControlPolicyToken, false,
            LearnedContinuationController::Legacy);
    gate.treatment_exact =
        deployment_recipe_exact(
            deployments.treatment,
            LearnedJointC17Arm::Treatment,
            kLearnedJointC17TreatmentPolicyToken, true,
            LearnedContinuationController::
                PublicStackPassV1);
    gate.model_bindings_exact =
        deployments.control.model &&
        deployments.treatment.model &&
        deployments.control.model !=
            deployments.treatment.model &&
        deployments.control.bot.learned_model ==
            deployments.control.model &&
        deployments.treatment.bot.learned_model ==
            deployments.treatment.model;
    if (gate.model_bindings_exact) {
        try {
            gate.computed_control_fingerprint =
                learned_model_fingerprint(
                    deployments.control.model);
            gate.computed_treatment_fingerprint =
                learned_model_fingerprint(
                    deployments.treatment.model);
            gate.model_fingerprints_computed =
                is_sha256(
                    gate.computed_control_fingerprint) &&
                is_sha256(
                    gate.computed_treatment_fingerprint);
        } catch (const std::exception& error) {
            gate.failures.push_back(
                "artifact deployment model fingerprinting "
                "failed: " +
                std::string(error.what()));
        } catch (...) {
            gate.failures.push_back(
                "artifact deployment model fingerprinting "
                "threw a non-standard exception");
        }
    }
    gate.fingerprints_bound =
        gate.model_fingerprints_computed &&
        deployments.artifact_control_fingerprint ==
            fingerprints.control &&
        deployments.artifact_treatment_fingerprint ==
            fingerprints.treatment &&
        gate.computed_control_fingerprint ==
            fingerprints.control &&
        gate.computed_treatment_fingerprint ==
            fingerprints.treatment;
    record_failure(
        gate.control_exact,
        "artifact control deployment recipe is not exact",
        gate.failures);
    record_failure(
        gate.treatment_exact,
        "artifact treatment deployment recipe is not exact",
        gate.failures);
    record_failure(
        gate.model_bindings_exact,
        "artifact deployment models are null, shared across "
        "arms, or not bound to their BotConfigs",
        gate.failures);
    record_failure(
        gate.model_fingerprints_computed,
        "artifact deployment model fingerprints could not "
        "be computed",
        gate.failures);
    record_failure(
        gate.fingerprints_bound,
        "artifact deployment fingerprints do not match the "
        "sealed evaluator identities",
        gate.failures);
    gate.passed =
        gate.control_exact &&
        gate.treatment_exact &&
        gate.model_bindings_exact &&
        gate.model_fingerprints_computed &&
        gate.fingerprints_bound;
    return gate;
}

std::string_view runner_stage_name(RunnerStage stage) {
    switch (stage) {
        case RunnerStage::Preflight:
            return "preflight";
        case RunnerStage::Heldout:
            return "heldout";
        case RunnerStage::DeepReference:
            return "deep-reference";
        case RunnerStage::FieldRegression:
            return "field-regression";
        case RunnerStage::TreatmentVsControl:
            return "treatment-vs-control";
        case RunnerStage::TreatmentVsParent:
            return "treatment-vs-parent";
        case RunnerStage::TreatmentVsHandcoded:
            return "treatment-vs-handcoded";
        case RunnerStage::FixedSeedPanels:
            return "fixed-seed-panels";
        case RunnerStage::FinalDirectPool:
            return "final-direct-pool";
        case RunnerStage::MixedField:
            return "mixed-field";
        case RunnerStage::ArtifactPostflight:
            return "artifact-postflight";
        case RunnerStage::Complete:
            return "complete";
    }
    return "unknown";
}

namespace {

SealedRunReport run_sealed_evaluation_core(
    const SealedRunRequest& request,
    const ArtifactSetCaptureReport& artifact_preflight) {
    SealedRunReport result;
    result.artifact_integrity.before =
        artifact_preflight;
    result.artifact_integrity.preflight_passed =
        capture_report_passed(artifact_preflight);
    result.artifact_integrity.failures =
        artifact_preflight.failures;
    record_failure(
        result.artifact_integrity.preflight_passed,
        "artifact integrity preflight failed",
        result.artifact_integrity.failures);
    result.model_fingerprints =
        validate_model_fingerprints(
            request.model_fingerprints);
    result.deployments =
        validate_frozen_deployments(
            request.deployments,
            request.model_fingerprints);
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!result.artifact_integrity.preflight_passed ||
        !result.model_fingerprints.passed ||
        !result.deployments.passed) {
        std::vector<std::string> failures;
        append_failures(
            failures,
            result.artifact_integrity.failures);
        append_failures(
            failures, result.model_fingerprints.failures);
        append_failures(
            failures, result.deployments.failures);
        finish_failure(
            result, RunnerStage::Preflight,
            RunnerDisposition::InfrastructureFailure,
            std::move(failures));
        return result;
    }
    record_pass(result, RunnerStage::Preflight);

    if (!invoke_stage(
            request.stages.heldout, result.heldout,
            RunnerStage::Heldout, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.heldout,
            heldout_consistent(*result.heldout),
            heldout_structure_valid(*result.heldout),
            RunnerStage::Heldout, result,
            [&result](bool passed) {
                result.outcomes.heldout_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_deep_reference,
            RunnerStage::DeepReference, result) ||
        !invoke_stage(
            request.stages.deep_reference,
            result.deep_reference,
            RunnerStage::DeepReference, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.deep_reference,
            deep_reference_consistent(
                *result.deep_reference),
            deep_reference_structure_valid(
                *result.deep_reference),
            RunnerStage::DeepReference, result,
            [&result](bool passed) {
                result.outcomes.deep_reference_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_field_regression,
            RunnerStage::FieldRegression, result) ||
        !invoke_stage(
            request.stages.field_regression,
            result.field_regression,
            RunnerStage::FieldRegression, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.field_regression,
            field_regression_consistent(
                *result.field_regression),
            field_regression_structure_valid(
                *result.field_regression),
            RunnerStage::FieldRegression, result,
            [&result](bool passed) {
                result.outcomes.field_regression_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_treatment_vs_control,
            RunnerStage::TreatmentVsControl, result) ||
        !invoke_stage(
            request.stages.treatment_vs_control,
            result.treatment_vs_control,
            RunnerStage::TreatmentVsControl, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.treatment_vs_control,
            direct_gameplay_consistent(
                *result.treatment_vs_control),
            direct_gameplay_structure_valid(
                *result.treatment_vs_control),
            RunnerStage::TreatmentVsControl, result,
            [&result](bool passed) {
                result.outcomes
                    .treatment_vs_control_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_treatment_vs_parent,
            RunnerStage::TreatmentVsParent, result) ||
        !invoke_stage(
            request.stages.treatment_vs_parent,
            result.treatment_vs_parent,
            RunnerStage::TreatmentVsParent, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.treatment_vs_parent,
            direct_gameplay_consistent(
                *result.treatment_vs_parent),
            direct_gameplay_structure_valid(
                *result.treatment_vs_parent),
            RunnerStage::TreatmentVsParent, result,
            [&result](bool passed) {
                result.outcomes
                    .treatment_vs_parent_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_treatment_vs_handcoded,
            RunnerStage::TreatmentVsHandcoded, result) ||
        !invoke_stage(
            request.stages.treatment_vs_handcoded,
            result.treatment_vs_handcoded,
            RunnerStage::TreatmentVsHandcoded, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.treatment_vs_handcoded,
            direct_gameplay_consistent(
                *result.treatment_vs_handcoded),
            direct_gameplay_structure_valid(
                *result.treatment_vs_handcoded),
            RunnerStage::TreatmentVsHandcoded, result,
            [&result](bool passed) {
                result.outcomes
                    .treatment_vs_handcoded_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_fixed_seed_panel,
            RunnerStage::FixedSeedPanels, result) ||
        !invoke_stage(
            request.stages.fixed_seed_panels,
            result.fixed_seed_panels,
            RunnerStage::FixedSeedPanels, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.fixed_seed_panels,
            fixed_panel_set_consistent(
                *result.fixed_seed_panels),
            fixed_panel_set_structure_valid(
                *result.fixed_seed_panels),
            RunnerStage::FixedSeedPanels, result,
            [&result](bool passed) {
                result.outcomes
                    .fixed_seed_panel_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_final_direct_pool,
            RunnerStage::FinalDirectPool, result) ||
        !invoke_stage(
            request.stages.final_direct_pool,
            result.final_direct_pool,
            RunnerStage::FinalDirectPool, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.final_direct_pool,
            final_direct_consistent(
                *result.final_direct_pool),
            final_direct_structure_valid(
                *result.final_direct_pool,
                *result.treatment_vs_handcoded,
                *result.fixed_seed_panels),
            RunnerStage::FinalDirectPool, result,
            [&result](bool passed) {
                result.outcomes
                    .final_direct_pool_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!require_stage_enabled(
            result.decision.run_mixed_field,
            RunnerStage::MixedField, result) ||
        !invoke_stage(
            request.stages.mixed_field,
            result.mixed_field,
            RunnerStage::MixedField, result)) {
        return result;
    }
    if (!accept_or_stop(
            *result.mixed_field,
            mixed_field_consistent(*result.mixed_field),
            mixed_field_structure_valid(
                *result.mixed_field),
            RunnerStage::MixedField, result,
            [&result](bool passed) {
                result.outcomes.mixed_field_passed =
                    passed;
            })) {
        return result;
    }
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);
    if (!result.decision.complete ||
        !result.decision.passed) {
        finish_failure(
            result, RunnerStage::Complete,
            RunnerDisposition::InfrastructureFailure,
            {"joint evaluator did not complete after every "
             "sealed stage passed"});
        return result;
    }

    result.disposition = RunnerDisposition::Passed;
    result.terminal_stage = RunnerStage::Complete;
    return result;
}

SealedRunReport run_sealed_evaluation_with(
    const SealedRunRequest& request,
    const testing::ArtifactSnapshotter& snapshotter) {
    auto before =
        capture_evaluation_artifact_set_with(
            request.artifact_integrity,
            snapshotter);
    auto result =
        run_sealed_evaluation_core(request, before);
    if (!capture_report_passed(before)) {
        return result;
    }

    auto after =
        capture_evaluation_artifact_set_with(
            request.artifact_integrity,
            snapshotter);
    result.artifact_integrity =
        validate_evaluation_artifact_integrity(
            std::move(before), std::move(after));
    if (!result.artifact_integrity.passed) {
        result.disposition =
            RunnerDisposition::InfrastructureFailure;
        result.terminal_stage =
            RunnerStage::ArtifactPostflight;
        result.stages.push_back({
            .stage = RunnerStage::ArtifactPostflight,
            .disposition =
                RunnerDisposition::
                    InfrastructureFailure,
            .failures =
                result.artifact_integrity.failures,
        });
        return result;
    }
    if (result.disposition ==
        RunnerDisposition::Passed) {
        record_pass(
            result, RunnerStage::ArtifactPostflight);
        record_pass(result, RunnerStage::Complete);
    }
    return result;
}

} // namespace

SealedRunReport run_sealed_evaluation(
    const SealedRunRequest& request) {
    return run_sealed_evaluation_with(
        request, snapshot_evaluation_artifact);
}

namespace testing {

ArtifactSetCaptureReport capture_evaluation_artifact_set(
    const EvaluationArtifactIntegrityRequest& request,
    const ArtifactSnapshotter& snapshotter) {
    return capture_evaluation_artifact_set_with(
        request, snapshotter);
}

SealedRunReport run_sealed_evaluation(
    const SealedRunRequest& request,
    const ArtifactSnapshotter& snapshotter) {
    return run_sealed_evaluation_with(
        request, snapshotter);
}

} // namespace testing

} // namespace old_school::joint_c17_runner
