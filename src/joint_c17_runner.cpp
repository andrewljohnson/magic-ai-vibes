#include "old_school/joint_c17_runner.hpp"

#include <algorithm>
#include <exception>
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

bool snapshot_valid(const ArtifactSnapshot& snapshot) {
    if (!snapshot.exists) {
        return !snapshot.regular_file &&
               snapshot.byte_size == 0 &&
               snapshot.sha256.empty();
    }
    return snapshot.regular_file &&
           snapshot.byte_size > 0 &&
           is_sha256(snapshot.sha256);
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
    const bool deck_reports_consistent =
        std::all_of(
            gate.by_deck.begin(), gate.by_deck.end(),
            [](const auto& deck) {
                return deck.passed ==
                       (deck.lost_agreements <=
                        joint_c17_eval::
                            kMaximumStableBestSetLossesPerDeck);
            });
    return force_spike_consistent(gate.force_spike) &&
           deck_reports_consistent &&
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
    return gate.fixture_count_exact ==
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
    return gate.every_challenger_deck_strict_win ==
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
    return gate.every_challenger_deck_strict_win ==
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
    for (std::size_t deck = 0;
         deck < gate.by_deck.size(); ++deck) {
        deck_identities_exact =
            deck_identities_exact &&
            gate.by_deck[deck].deck ==
                static_cast<DeckId>(deck);
    }
    const bool every_deck =
        std::all_of(
            gate.by_deck.begin(), gate.by_deck.end(),
            [](const auto& deck) {
                return deck.learned_lift_is_best;
            });
    return deck_identities_exact &&
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

template <typename Report>
bool accept_or_stop(
    const Report& gate, bool consistent,
    bool structure_valid, RunnerStage stage,
    SealedRunReport& result) {
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

ArtifactPublicationGateReport
validate_canonical_artifact_publication(
    const ArtifactPublicationEvidence& evidence) {
    ArtifactPublicationGateReport gate;
    gate.canonical_paths_exact =
        evidence.before.path == kCanonicalArtifactPath &&
        evidence.after.path == kCanonicalArtifactPath;
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
        case RunnerStage::Complete:
            return "complete";
    }
    return "unknown";
}

SealedRunReport run_sealed_evaluation(
    const SealedRunRequest& request) {
    SealedRunReport result;
    result.artifact_publication =
        validate_canonical_artifact_publication(
            request.artifact_publication);
    result.model_fingerprints =
        validate_model_fingerprints(
            request.model_fingerprints);
    result.decision =
        joint_c17_eval::evaluation_stage_decision(
            result.outcomes);

    if (!result.artifact_publication.passed ||
        !result.model_fingerprints.passed) {
        std::vector<std::string> failures;
        append_failures(
            failures,
            result.artifact_publication.failures);
        append_failures(
            failures, result.model_fingerprints.failures);
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
    result.outcomes.heldout_passed =
        result.heldout->passed;
    if (!accept_or_stop(
            *result.heldout,
            heldout_consistent(*result.heldout),
            heldout_structure_valid(*result.heldout),
            RunnerStage::Heldout, result)) {
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
    result.outcomes.deep_reference_passed =
        result.deep_reference->passed;
    if (!accept_or_stop(
            *result.deep_reference,
            deep_reference_consistent(
                *result.deep_reference),
            deep_reference_structure_valid(
                *result.deep_reference),
            RunnerStage::DeepReference, result)) {
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
    result.outcomes.field_regression_passed =
        result.field_regression->passed;
    if (!accept_or_stop(
            *result.field_regression,
            field_regression_consistent(
                *result.field_regression),
            field_regression_structure_valid(
                *result.field_regression),
            RunnerStage::FieldRegression, result)) {
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
    result.outcomes.treatment_vs_control_passed =
        result.treatment_vs_control->passed;
    if (!accept_or_stop(
            *result.treatment_vs_control,
            direct_gameplay_consistent(
                *result.treatment_vs_control),
            direct_gameplay_structure_valid(
                *result.treatment_vs_control),
            RunnerStage::TreatmentVsControl, result)) {
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
    result.outcomes.treatment_vs_parent_passed =
        result.treatment_vs_parent->passed;
    if (!accept_or_stop(
            *result.treatment_vs_parent,
            direct_gameplay_consistent(
                *result.treatment_vs_parent),
            direct_gameplay_structure_valid(
                *result.treatment_vs_parent),
            RunnerStage::TreatmentVsParent, result)) {
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
    result.outcomes.treatment_vs_handcoded_passed =
        result.treatment_vs_handcoded->passed;
    if (!accept_or_stop(
            *result.treatment_vs_handcoded,
            direct_gameplay_consistent(
                *result.treatment_vs_handcoded),
            direct_gameplay_structure_valid(
                *result.treatment_vs_handcoded),
            RunnerStage::TreatmentVsHandcoded, result)) {
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
    result.outcomes.fixed_seed_panel_passed =
        result.fixed_seed_panels->passed;
    if (!accept_or_stop(
            *result.fixed_seed_panels,
            fixed_panel_set_consistent(
                *result.fixed_seed_panels),
            fixed_panel_set_structure_valid(
                *result.fixed_seed_panels),
            RunnerStage::FixedSeedPanels, result)) {
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
    result.outcomes.final_direct_pool_passed =
        result.final_direct_pool->passed;
    if (!accept_or_stop(
            *result.final_direct_pool,
            final_direct_consistent(
                *result.final_direct_pool),
            final_direct_structure_valid(
                *result.final_direct_pool,
                *result.treatment_vs_handcoded,
                *result.fixed_seed_panels),
            RunnerStage::FinalDirectPool, result)) {
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
    result.outcomes.mixed_field_passed =
        result.mixed_field->passed;
    if (!accept_or_stop(
            *result.mixed_field,
            mixed_field_consistent(*result.mixed_field),
            mixed_field_structure_valid(
                *result.mixed_field),
            RunnerStage::MixedField, result)) {
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
    record_pass(result, RunnerStage::Complete);
    return result;
}

} // namespace old_school::joint_c17_runner
