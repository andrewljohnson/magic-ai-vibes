#include "old_school/joint_c17_runner.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace eval = old_school::joint_c17_eval;
namespace runner = old_school::joint_c17_runner;

class TestRunner {
  public:
    void run(
        std::string_view name,
        const std::function<void()>& test) {
        try {
            test();
            ++passed_;
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "FAIL " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        if (failed_ != 0) {
            std::cerr << failed_ << " test(s) failed; "
                      << passed_ << " passed\n";
            return 1;
        }
        std::cout << passed_
                  << " joint-C17 runner tests passed\n";
        return 0;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::shared_ptr<const old_school::LearnedModel>
test_control_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xC17C0A01ULL);
    return model;
}

std::shared_ptr<const old_school::LearnedModel>
test_treatment_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xC17C0A02ULL);
    return model;
}

const eval::JointC17ExpectedModelFingerprints&
test_fingerprints() {
    static const eval::JointC17ExpectedModelFingerprints
        fingerprints{
            .control =
                old_school::learned_model_fingerprint(
                    test_control_model()),
            .treatment =
                old_school::learned_model_fingerprint(
                    test_treatment_model()),
        };
    return fingerprints;
}

runner::TrainingPublicationEvidence
passing_training_publication() {
    return {
        .before =
            {
                .requested_path =
                    std::string(
                        runner::kCanonicalArtifactPath),
                .resolved_path =
                    "/workspace/build/model-cache/"
                    "old-school-value-joint-c17-v1-t800-p424242-r202607261145.bin",
                .exists = false,
                .regular_file = false,
                .byte_size = 0,
                .sha256 = {},
            },
        .after =
            {
                .requested_path =
                    std::string(
                        runner::kCanonicalArtifactPath),
                .resolved_path =
                    "/workspace/build/model-cache/"
                    "old-school-value-joint-c17-v1-t800-p424242-r202607261145.bin",
                .exists = true,
                .regular_file = true,
                .byte_size = 4096,
                .sha256 = std::string(64, 'c'),
                .sha256_verified = true,
            },
        .atomic_no_replace_confirmed = true,
    };
}

runner::testing::ArtifactSnapshotter passing_snapshotter() {
    return [](std::string_view path) {
        std::uintmax_t byte_size = 0;
        std::string sha256;
        std::uint64_t inode = 0;
        if (path == runner::kCanonicalArtifactPath) {
            byte_size = 4096;
            sha256 = std::string(64, 'c');
            inode = 11;
        } else if (path == runner::kParentArtifactPath) {
            byte_size =
                runner::kParentArtifactByteSize;
            sha256 =
                std::string(
                    runner::kParentArtifactSha256);
            inode = 12;
        } else if (
            path == runner::kLabelCacheArtifactPath) {
            byte_size =
                runner::kLabelCacheArtifactByteSize;
            sha256 =
                std::string(
                    runner::kLabelCacheArtifactSha256);
            inode = 13;
        }
        const auto resolved =
            std::filesystem::absolute(
                std::filesystem::path(path))
                .lexically_normal()
                .string();
        return old_school::artifact_integrity::
            RegularFileSnapshot{
            .path = resolved,
            .physical_path = resolved,
            .byte_size = byte_size,
            .sha256 = std::move(sha256),
            .device = 1,
            .inode = inode,
            .link_count = 1,
            .modification_seconds = 3,
            .modification_nanoseconds = 4,
            .change_seconds = 5,
            .change_nanoseconds = 6,
        };
    };
}

runner::EvaluationArtifactIntegrityRequest
passing_artifact_integrity_request() {
    return runner::
        make_evaluation_artifact_integrity_request(
            4096, std::string(64, 'c'));
}

old_school::LearnedSearchConfig exact_search(
    bool treatment) {
    return {
        .seed = 0,
        .worlds = 8,
        .rollouts_per_world = 1,
        .horizon_turns =
            old_school::kLearnedValueSearchHorizonTurns,
        .continuation_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = true,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = treatment,
        .value_continuation_controller =
            treatment
                ? old_school::
                      LearnedContinuationController::
                          PublicStackPassV1
                : old_school::
                      LearnedContinuationController::Legacy,
        .evaluation_threads = 1,
    };
}

runner::FrozenDeploymentEvidence passing_deployments() {
    auto control_model = test_control_model();
    auto treatment_model = test_treatment_model();
    return {
        .control =
            {
                .arm =
                    old_school::
                        LearnedJointC17Arm::Control,
                .policy_token =
                    std::string(
                        old_school::
                            kLearnedJointC17ControlPolicyToken),
                .model = control_model,
                .bot =
                    runner::make_control_bot(
                        control_model),
                .search = exact_search(false),
            },
        .treatment =
            {
                .arm =
                    old_school::
                        LearnedJointC17Arm::Treatment,
                .policy_token =
                    std::string(
                        old_school::
                            kLearnedJointC17TreatmentPolicyToken),
                .model = treatment_model,
                .bot =
                    runner::make_treatment_bot(
                        treatment_model),
                .search = exact_search(true),
            },
        .artifact_control_fingerprint =
            test_fingerprints().control,
        .artifact_treatment_fingerprint =
            test_fingerprints().treatment,
    };
}

eval::HeldoutGateReport passing_heldout() {
    return {
        .accounting_exact = true,
        .inputs_finite = true,
        .pooled_losses_improved = true,
        .every_deck_loss_guard = true,
        .green_bias_strictly_shrank = true,
        .blue_bias_strictly_shrank = true,
        .ru_bias_guard = true,
        .no_new_material_bias = true,
        .passed = true,
        .failures = {},
    };
}

eval::DeepReferenceGateReport passing_deep_reference() {
    eval::DeepReferenceGateReport gate;
    gate.accounting_exact = true;
    gate.metrics_finite = true;
    gate.pooled_regret_no_worse = true;
    gate.pooled_top_one_no_lower = true;
    gate.every_deck_regret_guard = true;
    gate.stable_best_set_loss_guard = true;
    gate.required_blue_probes_exact = true;
    gate.required_blue_selections_passed = true;
    gate.hidden_repartition_passed = true;
    gate.common_state_critics.accounting_exact = true;
    gate.common_state_critics.predictions_valid = true;
    gate.common_state_critics.metrics_finite = true;
    for (auto& metrics :
         gate.common_state_critics.pooled.models) {
        metrics.probe_count =
            eval::kExpectedProbeCount;
    }
    gate.force_spike.identities_exact = true;
    gate.force_spike.live_uniquely_selects_force_spike =
        true;
    gate.force_spike.payable_uniquely_selects_pass = true;
    gate.force_spike.hidden_repartition_passed = true;
    gate.force_spike.passed = true;
    for (std::size_t deck = 0;
         deck < gate.by_deck.size(); ++deck) {
        gate.by_deck[deck].root_deck =
            static_cast<old_school::DeckId>(deck);
        gate.by_deck[deck].passed = true;
        for (auto& metrics :
             gate.common_state_critics
                 .by_deck[deck]
                 .models) {
            metrics.probe_count =
                eval::kExpectedProbesPerDeck;
        }
    }
    gate.passed = true;
    return gate;
}

eval::FieldRegressionGateReport passing_field_regression() {
    eval::FieldRegressionGateReport gate;
    gate.metadata_exact = true;
    gate.fixture_count_exact = true;
    gate.every_fixture_valid = true;
    gate.fixtures.resize(
        eval::kFieldRegressionFixtureCount);
    for (std::size_t fixture = 0;
         fixture < gate.fixtures.size(); ++fixture) {
        gate.fixtures[fixture].stable_id =
            std::string(
                eval::
                    kRequiredFieldRegressionIds[fixture]);
        auto& row = gate.fixtures[fixture];
        row.identity_exact = true;
        row.reference_valid = true;
        row.deployment_valid = true;
    }
    gate.passed = true;
    return gate;
}

eval::DirectGameplayGateReport passing_direct_gameplay() {
    eval::DirectGameplayGateReport gate;
    gate.identity_exact = true;
    gate.accounting_exact = true;
    gate.clustered_estimate_valid = true;
    gate.rates_finite = true;
    gate.aggregate_strict_win = true;
    gate.wilson_lower_above_half = true;
    gate.challenger_deck_strict_wins.fill(true);
    gate.every_challenger_deck_strict_win = true;
    gate.challenger_win_rate_percent = 55.0;
    gate.wilson_lower_95_percent = 51.0;
    gate.passed = true;
    return gate;
}

eval::FixedSeedPanelGateReport passing_fixed_panel() {
    return {
        .identity_exact = true,
        .accounting_exact = true,
        .clustered_estimate_valid = true,
        .aggregate_non_losing = true,
        .passed = true,
        .failures = {},
    };
}

eval::FixedSeedPanelSetGateReport
passing_fixed_panel_set() {
    eval::FixedSeedPanelSetGateReport gate;
    gate.panel_count_exact = true;
    gate.seeds_exact = true;
    gate.panels.assign(
        eval::kFixedSeedPanelCount,
        passing_fixed_panel());
    gate.every_panel_passed = true;
    gate.passed = true;
    return gate;
}

eval::FinalDirectPoolGateReport passing_final_pool() {
    eval::FinalDirectPoolGateReport gate;
    gate.accounting_exact = true;
    gate.rates_finite = true;
    gate.aggregate_strict_win = true;
    gate.wilson_lower_above_half = true;
    gate.challenger_deck_strict_wins.fill(true);
    gate.every_challenger_deck_strict_win = true;
    gate.challenger_win_rate_percent = 55.0;
    gate.wilson_lower_95_percent = 51.0;
    gate.passed = true;
    return gate;
}

eval::FinalDirectGateReport passing_final_direct() {
    return {
        .primary = passing_direct_gameplay(),
        .fixed_seed_panels =
            passing_fixed_panel_set(),
        .merge_succeeded = true,
        .pooled = passing_final_pool(),
        .passed = true,
        .failures = {},
    };
}

eval::MixedFieldGateReport passing_mixed_field() {
    eval::MixedFieldGateReport gate;
    gate.panel_count_exact = true;
    gate.seeds_exact = true;
    gate.policy_identity_exact = true;
    gate.accounting_exact = true;
    gate.rates_finite = true;
    for (std::size_t deck = 0;
         deck < gate.by_deck.size(); ++deck) {
        gate.by_deck[deck].deck =
            static_cast<old_school::DeckId>(deck);
        gate.by_deck[deck].rates_finite = true;
        gate.by_deck[deck].learned_lift_is_best = true;
    }
    gate.learned_lift_best_on_every_deck = true;
    gate.passed = true;
    return gate;
}

runner::SealedRunRequest passing_request(
    std::vector<runner::RunnerStage>* calls = nullptr) {
    const auto note =
        [calls](runner::RunnerStage stage) {
            if (calls != nullptr) {
                calls->push_back(stage);
            }
        };
    runner::SealedRunRequest request;
    request.artifact_integrity =
        passing_artifact_integrity_request();
    request.model_fingerprints = test_fingerprints();
    request.deployments = passing_deployments();
    request.stages.heldout =
        [note]() {
            note(runner::RunnerStage::Heldout);
            return passing_heldout();
        };
    request.stages.deep_reference =
        [note]() {
            note(runner::RunnerStage::DeepReference);
            return passing_deep_reference();
        };
    request.stages.field_regression =
        [note]() {
            note(runner::RunnerStage::FieldRegression);
            return passing_field_regression();
        };
    request.stages.treatment_vs_control =
        [note]() {
            note(
                runner::RunnerStage::
                    TreatmentVsControl);
            return passing_direct_gameplay();
        };
    request.stages.treatment_vs_parent =
        [note]() {
            note(
                runner::RunnerStage::
                    TreatmentVsParent);
            return passing_direct_gameplay();
        };
    request.stages.treatment_vs_handcoded =
        [note]() {
            note(
                runner::RunnerStage::
                    TreatmentVsHandcoded);
            return passing_direct_gameplay();
        };
    request.stages.fixed_seed_panels =
        [note]() {
            note(runner::RunnerStage::FixedSeedPanels);
            return passing_fixed_panel_set();
        };
    request.stages.final_direct_pool =
        [note]() {
            note(runner::RunnerStage::FinalDirectPool);
            return passing_final_direct();
        };
    request.stages.mixed_field =
        [note]() {
            note(runner::RunnerStage::MixedField);
            return passing_mixed_field();
        };
    return request;
}

runner::SealedRunReport run_for_testing(
    const runner::SealedRunRequest& request) {
    return runner::testing::run_sealed_evaluation(
        request, passing_snapshotter());
}

void test_exact_policy_builders() {
    const auto control = runner::make_control_bot();
    expect(
        control.kind == old_school::BotKind::Learned,
        "control kind");
    expect(
        control.learned_variant ==
            old_school::LearnedVariant::
                ValueSearchChampion,
        "control variant");
    expect(
        control.rollouts_per_action == 8,
        "control rollout count");
    expect(
        control.exploration_rate == 0.0 &&
            control.value_continuation_epsilon == 0.0 &&
            control.value_priority_residual_weight == 0.0,
        "control zero tuning fields");
    expect(
        !control.value_pass_dominance &&
            control.value_continuation_controller ==
                old_school::
                    LearnedContinuationController::Legacy,
        "control deployment controller");
    expect(
        control.training_games ==
            runner::kCanonicalTrainingGames &&
            !control.learned_model,
        "control training identity");

    const auto treatment = runner::make_treatment_bot();
    expect(
        treatment.kind == control.kind &&
            treatment.learned_variant ==
                control.learned_variant &&
            treatment.rollouts_per_action ==
                control.rollouts_per_action &&
            treatment.exploration_rate ==
                control.exploration_rate &&
            treatment.value_continuation_epsilon ==
                control.value_continuation_epsilon &&
            treatment.value_priority_residual_weight ==
                control.value_priority_residual_weight &&
            treatment.training_games ==
                control.training_games,
        "treatment preserves shared recipe");
    expect(
        treatment.value_pass_dominance &&
            treatment.value_continuation_controller ==
                old_school::
                    LearnedContinuationController::
                        PublicStackPassV1,
        "treatment exact joint components");

    const auto parent = runner::make_parent_bot();
    expect(parent.kind == control.kind, "parent kind");
    expect(
        !parent.value_pass_dominance &&
            parent.value_continuation_controller ==
                old_school::
                    LearnedContinuationController::Legacy,
        "parent exact deployment");

    const auto handcoded = runner::make_handcoded_bot();
    expect(
        handcoded.kind ==
                old_school::BotKind::Handcrafted &&
            handcoded.rollouts_per_action == 1 &&
            handcoded.training_games ==
                runner::kCanonicalTrainingGames,
        "Handcoded exact deployment");
}

void test_exact_policy_recipe_evidence() {
    expect(
        runner::make_control_recipe() ==
            eval::PolicyRecipeEvidence{
                .policy_token =
                    std::string(
                        old_school::
                            kLearnedJointC17ControlPolicyToken),
                .horizon_turns =
                    old_school::
                        kLearnedValueSearchHorizonTurns,
                .blend_shallow_prior = true,
            },
        "control recipe evidence");
    expect(
        runner::make_treatment_recipe() ==
            eval::PolicyRecipeEvidence{
                .policy_token =
                    std::string(
                        old_school::
                            kLearnedJointC17TreatmentPolicyToken),
                .horizon_turns =
                    old_school::
                        kLearnedValueSearchHorizonTurns,
                .blend_shallow_prior = true,
            },
        "treatment recipe evidence");
    expect(
        runner::make_parent_recipe().policy_token ==
                eval::kFrozenC16EvidencePolicyToken &&
            runner::make_parent_recipe()
                    .blend_shallow_prior,
        "parent recipe evidence");
    expect(
        runner::make_handcoded_recipe() ==
            eval::PolicyRecipeEvidence{
                .policy_token =
                    std::string(
                        eval::
                            kHandcodedEvidencePolicyToken),
                .horizon_turns = 0,
                .blend_shallow_prior = false,
            },
        "Handcoded recipe evidence");
    expect(
        runner::kCanonicalArtifactFilename ==
            "old-school-value-joint-c17-v1-t800-p424242-r202607261145.bin",
        "canonical artifact filename");
    expect(
        runner::kCanonicalArtifactPath ==
            "build/model-cache/"
            "old-school-value-joint-c17-v1-t800-p424242-r202607261145.bin",
        "canonical artifact path");
}

void test_training_publication_validation() {
    const auto passing =
        runner::validate_training_publication(
            passing_training_publication());
    expect(passing.passed, "valid publication passes");
    const auto integrity =
        runner::
            evaluation_artifact_integrity_from_publication(
                passing_training_publication());
    expect(
        integrity.has_value() &&
            integrity->artifacts.size() == 3 &&
            integrity->artifacts.front()
                    .expected_byte_size ==
                4096 &&
            integrity->artifacts.front()
                    .expected_sha256 ==
                std::string(64, 'c'),
        "trusted publication pins the bundle identity");

    auto evidence = passing_training_publication();
    evidence.before.exists = true;
    evidence.before.regular_file = true;
    evidence.before.byte_size = 10;
    evidence.before.sha256 = std::string(64, 'd');
    evidence.before.sha256_verified = true;
    auto gate =
        runner::validate_training_publication(
            evidence);
    expect(
        !gate.passed &&
            gate.before_snapshot_valid &&
            !gate.target_absent_before,
        "existing target is a no-replace failure");

    evidence = passing_training_publication();
    evidence.atomic_no_replace_confirmed = false;
    gate =
        runner::validate_training_publication(
            evidence);
    expect(
        !gate.passed &&
            !gate.atomic_no_replace_confirmed,
        "atomic witness is mandatory");

    evidence = passing_training_publication();
    evidence.after.sha256 = std::string(64, 'A');
    gate =
        runner::validate_training_publication(
            evidence);
    expect(
        !gate.passed && !gate.after_snapshot_valid,
        "artifact digest is exact lower-case SHA-256");

    evidence = passing_training_publication();
    evidence.after.requested_path =
        "build/model-cache/other.bin";
    gate =
        runner::validate_training_publication(
            evidence);
    expect(
        !gate.passed && !gate.canonical_paths_exact,
        "publication path is sealed");
}

void test_evaluation_integrity_uses_trusted_snapshotter() {
    const auto integrity_request =
        passing_artifact_integrity_request();
    expect(
        integrity_request.artifacts[1]
                    .requested_path ==
                runner::kParentArtifactPath &&
            integrity_request.artifacts[1]
                    .expected_byte_size ==
                runner::kParentArtifactByteSize &&
            integrity_request.artifacts[1]
                    .expected_sha256 ==
                runner::kParentArtifactSha256 &&
            integrity_request.artifacts[2]
                    .requested_path ==
                runner::kLabelCacheArtifactPath &&
            integrity_request.artifacts[2]
                    .expected_byte_size ==
                runner::kLabelCacheArtifactByteSize &&
            integrity_request.artifacts[2]
                    .expected_sha256 ==
                runner::kLabelCacheArtifactSha256,
        "parent and label identities are frozen");
    const auto before =
        runner::testing::
            capture_evaluation_artifact_set(
                integrity_request,
                passing_snapshotter());
    const auto after =
        runner::testing::
            capture_evaluation_artifact_set(
                integrity_request,
                passing_snapshotter());
    const auto passing =
        runner::validate_evaluation_artifact_integrity(
            before, after);
    expect(
        passing.passed &&
            passing.before.artifacts.size() == 3 &&
            passing.before.artifacts.front()
                    .requested_path ==
                runner::kCanonicalArtifactPath &&
            passing.before.artifacts.front()
                    .resolved.path ==
                std::filesystem::absolute(
                    std::filesystem::path(
                        runner::
                            kCanonicalArtifactPath))
                    .lexically_normal()
                    .string(),
        "logical and resolved artifact paths are retained");

    bool canonical_path_requested = false;
    const auto malformed_snapshotter =
        [&canonical_path_requested](
            std::string_view path) {
            canonical_path_requested =
                canonical_path_requested ||
                path == runner::kCanonicalArtifactPath;
            auto snapshot =
                passing_snapshotter()(path);
            snapshot.sha256 = std::string(63, 'c');
            return snapshot;
        };
    const auto unresolved =
        runner::testing::
            capture_evaluation_artifact_set(
                integrity_request,
                malformed_snapshotter);
    expect(
        canonical_path_requested &&
            !unresolved.all_snapshots_captured,
        "malformed trusted snapshot fails closed");

    const auto missing =
        runner::testing::
            capture_evaluation_artifact_set(
                integrity_request, {});
    expect(
        !missing.snapshotter_present &&
            !missing.all_snapshots_captured,
        "missing trusted snapshotter fails closed");

    auto omission_request =
        passing_artifact_integrity_request();
    omission_request.artifacts.pop_back();
    const auto omitted =
        runner::testing::
            capture_evaluation_artifact_set(
                omission_request,
                passing_snapshotter());
    expect(
        !omitted.manifest_valid &&
            !omitted.required_roles_exact &&
            !omitted.all_snapshots_captured,
        "label-cache omission fails before snapshots");

    auto extra_request =
        passing_artifact_integrity_request();
    extra_request.artifacts.push_back({
        .role = "unexpected-artifact",
        .requested_path = "build/unexpected.bin",
        .expected_byte_size = 1,
        .expected_sha256 = std::string(64, 'd'),
    });
    const auto extra =
        runner::testing::
            capture_evaluation_artifact_set(
                extra_request,
                passing_snapshotter());
    expect(
        !extra.manifest_valid &&
            !extra.required_roles_exact &&
            !extra.all_snapshots_captured,
        "extra artifact role fails before snapshots");

    auto wrong_parent =
        passing_artifact_integrity_request();
    ++wrong_parent.artifacts[1].expected_byte_size;
    const auto wrong_parent_report =
        runner::testing::
            capture_evaluation_artifact_set(
                wrong_parent,
                passing_snapshotter());
    expect(
        !wrong_parent_report.manifest_valid &&
            !wrong_parent_report
                 .all_snapshots_captured,
        "parent size is pinned");

    auto wrong_label =
        passing_artifact_integrity_request();
    wrong_label.artifacts[2].requested_path =
        "data/other.labels.tsv";
    const auto wrong_label_report =
        runner::testing::
            capture_evaluation_artifact_set(
                wrong_label,
                passing_snapshotter());
    expect(
        !wrong_label_report.manifest_valid &&
            !wrong_label_report
                 .all_snapshots_captured,
        "label-cache path is pinned");

    auto wrong_bundle =
        passing_artifact_integrity_request();
    wrong_bundle.artifacts[0].expected_sha256 =
        std::string(64, 'd');
    const auto wrong_bundle_report =
        runner::testing::
            capture_evaluation_artifact_set(
                wrong_bundle,
                passing_snapshotter());
    expect(
        wrong_bundle_report.manifest_valid &&
            !wrong_bundle_report
                 .all_snapshots_captured,
        "published bundle digest must match bytes");

    const runner::testing::ArtifactSnapshotter
        aliased_snapshotter =
            [](std::string_view path) {
                auto snapshot =
                    passing_snapshotter()(path);
                snapshot.inode = 77;
                return snapshot;
            };
    const auto aliased =
        runner::testing::
            capture_evaluation_artifact_set(
                integrity_request,
                aliased_snapshotter);
    expect(
        !aliased.all_snapshots_captured,
        "distinct roles cannot alias one device/inode");

    auto changed = after;
    changed.artifacts.back().resolved.sha256 =
        std::string(64, 'd');
    const auto mutation =
        runner::validate_evaluation_artifact_integrity(
            before, changed);
    expect(
        !mutation.passed &&
            !mutation.artifact_set_unchanged,
        "any manifest artifact mutation fails postflight");
}

void test_evaluation_snapshot_metadata_fails_closed() {
    const auto request =
        passing_artifact_integrity_request();
    const auto capture_with =
        [&request](
            const runner::testing::ArtifactSnapshotter&
                snapshotter) {
            return runner::testing::
                capture_evaluation_artifact_set(
                    request, snapshotter);
        };

    const runner::testing::ArtifactSnapshotter
        non_normal_physical_path =
            [](std::string_view path) {
                auto snapshot =
                    passing_snapshotter()(path);
                snapshot.physical_path =
                    (std::filesystem::path(
                         snapshot.physical_path)
                         .parent_path() /
                     ".." /
                     std::filesystem::path(
                         snapshot.physical_path)
                         .parent_path()
                         .filename() /
                     std::filesystem::path(
                         snapshot.physical_path)
                         .filename())
                        .string();
                return snapshot;
            };
    expect(
        !capture_with(non_normal_physical_path)
             .all_snapshots_captured,
        "non-normal physical paths fail closed");

    const runner::testing::ArtifactSnapshotter
        zero_link_count =
            [](std::string_view path) {
                auto snapshot =
                    passing_snapshotter()(path);
                snapshot.link_count = 0;
                return snapshot;
            };
    expect(
        !capture_with(zero_link_count)
             .all_snapshots_captured,
        "zero-link snapshots fail closed");

    const runner::testing::ArtifactSnapshotter
        invalid_timestamp =
            [](std::string_view path) {
                auto snapshot =
                    passing_snapshotter()(path);
                snapshot.change_nanoseconds =
                    1'000'000'000;
                return snapshot;
            };
    expect(
        !capture_with(invalid_timestamp)
             .all_snapshots_captured,
        "out-of-range timestamp metadata fails closed");
}

void test_model_fingerprint_validation() {
    expect(
        runner::validate_model_fingerprints(
            test_fingerprints())
            .passed,
        "distinct model fingerprints pass");

    auto fingerprints = test_fingerprints();
    fingerprints.treatment = fingerprints.control;
    auto gate =
        runner::validate_model_fingerprints(
            fingerprints);
    expect(
        !gate.passed && !gate.fingerprints_distinct,
        "identical models fail");

    fingerprints = test_fingerprints();
    fingerprints.control =
        std::string(
            old_school::
                kLearnedJointC17ParentFingerprint);
    gate =
        runner::validate_model_fingerprints(
            fingerprints);
    expect(
        !gate.passed && !gate.parent_excluded,
        "parent fingerprint cannot impersonate a joint arm");

    fingerprints = test_fingerprints();
    fingerprints.control = std::string(64, 'A');
    gate =
        runner::validate_model_fingerprints(
            fingerprints);
    expect(
        !gate.passed && !gate.control_is_sha256,
        "fingerprint syntax fails closed");
}

void test_artifact_deployments_are_validated() {
    auto deployments = passing_deployments();
    auto gate =
        runner::validate_frozen_deployments(
            deployments, test_fingerprints());
    expect(
        gate.passed &&
            gate.model_fingerprints_computed &&
            gate.computed_control_fingerprint ==
                test_fingerprints().control &&
            gate.computed_treatment_fingerprint ==
                test_fingerprints().treatment,
        "actual artifact deployment models are fingerprinted");

    deployments.treatment.search
        .value_pass_dominance = false;
    gate =
        runner::validate_frozen_deployments(
            deployments, test_fingerprints());
    expect(
        !gate.passed && !gate.treatment_exact,
        "reconstructed wrong treatment is rejected");

    deployments = passing_deployments();
    deployments.control.bot.learned_model =
        deployments.treatment.model;
    gate =
        runner::validate_frozen_deployments(
            deployments, test_fingerprints());
    expect(
        !gate.passed && !gate.model_bindings_exact,
        "BotConfig must retain its artifact model");

    deployments = passing_deployments();
    std::swap(
        deployments.control.model,
        deployments.treatment.model);
    deployments.control.bot.learned_model =
        deployments.control.model;
    deployments.treatment.bot.learned_model =
        deployments.treatment.model;
    gate =
        runner::validate_frozen_deployments(
            deployments, test_fingerprints());
    expect(
        !gate.passed &&
            gate.model_bindings_exact &&
            gate.model_fingerprints_computed &&
            !gate.fingerprints_bound,
        "claimed fingerprints cannot hide swapped model objects");

    deployments = passing_deployments();
    deployments.artifact_treatment_fingerprint =
        std::string(64, 'd');
    gate =
        runner::validate_frozen_deployments(
            deployments, test_fingerprints());
    expect(
        !gate.passed && !gate.fingerprints_bound,
        "artifact fingerprints bind evaluator identities");
}

void test_all_stages_run_in_sealed_order() {
    std::vector<runner::RunnerStage> calls;
    const auto result =
        run_for_testing(
            passing_request(&calls));
    const std::vector<runner::RunnerStage> expected = {
        runner::RunnerStage::Heldout,
        runner::RunnerStage::DeepReference,
        runner::RunnerStage::FieldRegression,
        runner::RunnerStage::TreatmentVsControl,
        runner::RunnerStage::TreatmentVsParent,
        runner::RunnerStage::TreatmentVsHandcoded,
        runner::RunnerStage::FixedSeedPanels,
        runner::RunnerStage::FinalDirectPool,
        runner::RunnerStage::MixedField,
    };
    expect(calls == expected, "sealed callback order");
    expect(
        result.disposition ==
                runner::RunnerDisposition::Passed &&
            result.terminal_stage ==
                runner::RunnerStage::Complete,
        "all-stage run passes");
    expect(
        result.decision.complete &&
            result.decision.passed,
        "joint evaluator records completion");
    expect(
        result.stages.size() == expected.size() + 3 &&
            result.stages[
                result.stages.size() - 2]
                    .stage ==
                runner::RunnerStage::
                    ArtifactPostflight &&
            result.stages.back().stage ==
                runner::RunnerStage::Complete,
        "preflight, postflight, and completion are recorded");
    expect(
        result.outcomes.heldout_passed &&
            result.outcomes.deep_reference_passed ==
                true &&
            result.outcomes.field_regression_passed ==
                true &&
            result.outcomes.treatment_vs_control_passed ==
                true &&
            result.outcomes.treatment_vs_parent_passed ==
                true &&
            result.outcomes
                    .treatment_vs_handcoded_passed ==
                true &&
            result.outcomes.fixed_seed_panel_passed ==
                true &&
            result.outcomes.final_direct_pool_passed ==
                true &&
            result.outcomes.mixed_field_passed == true,
        "all outcomes are retained");
}

void test_scientific_rejection_stops_without_salvage() {
    std::vector<runner::RunnerStage> calls;
    auto request = passing_request(&calls);
    request.stages.field_regression =
        [&calls]() {
            calls.push_back(
                runner::RunnerStage::FieldRegression);
            auto gate = passing_field_regression();
            auto& fixture = gate.fixtures.front();
            fixture.stable_reference_best_set = true;
            fixture.control_agrees = true;
            fixture.treatment_agrees = false;
            fixture.treatment_lost_control_agreement =
                true;
            gate.stable_fixture_count = 1;
            gate.control_agreements = 1;
            gate.treatment_losses = 1;
            gate.passed = false;
            gate.failures = {
                "treatment lost a stable control field "
                "agreement",
            };
            return gate;
        };

    const auto result =
        run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::Rejected &&
            result.terminal_stage ==
                runner::RunnerStage::FieldRegression,
        "scientific failure is a rejection");
    expect(
        calls ==
            std::vector<runner::RunnerStage>{
                runner::RunnerStage::Heldout,
                runner::RunnerStage::DeepReference,
                runner::RunnerStage::FieldRegression,
            },
        "no gameplay stage runs after field rejection");
    expect(
        result.outcomes.field_regression_passed ==
                false &&
            !result.outcomes
                 .treatment_vs_control_passed
                 .has_value(),
        "later outcomes stay absent");
}

void test_structural_failure_is_infrastructure_failure() {
    std::vector<runner::RunnerStage> calls;
    auto request = passing_request(&calls);
    request.stages.treatment_vs_control =
        [&calls]() {
            calls.push_back(
                runner::RunnerStage::
                    TreatmentVsControl);
            auto gate = passing_direct_gameplay();
            gate.identity_exact = false;
            gate.passed = false;
            gate.failures = {
                "direct panel identity is not exact",
            };
            return gate;
        };
    const auto result =
        run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            result.terminal_stage ==
                runner::RunnerStage::
                    TreatmentVsControl,
        "malformed evidence is infrastructure");
    expect(
        calls.size() == 4,
        "structural failure stops remaining callbacks");
}

void test_inconsistent_report_fails_closed() {
    auto request = passing_request();
    request.stages.heldout =
        []() {
            auto gate = passing_heldout();
            gate.passed = false;
            return gate;
        };
    const auto result =
        run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            result.terminal_stage ==
                runner::RunnerStage::Heldout,
        "fabricated evaluator report fails closed");
    expect(
        !result.stages.back().failures.empty(),
        "inconsistency is diagnosed");
}

void test_malformed_passes_do_not_advance_outcomes() {
    auto request = passing_request();
    request.stages.heldout =
        []() {
            auto gate = passing_heldout();
            gate.accounting_exact = false;
            return gate;
        };
    auto result = run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            !result.outcomes.heldout_passed &&
            !result.outcomes
                 .deep_reference_passed
                 .has_value(),
        "malformed heldout pass cannot enable deep reference");

    request = passing_request();
    request.stages.deep_reference =
        []() {
            auto gate = passing_deep_reference();
            gate.by_deck[0].root_deck =
                old_school::DeckId::Red;
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::DeepReference &&
            !result.outcomes
                 .deep_reference_passed
                 .has_value() &&
            !result.outcomes
                 .field_regression_passed
                 .has_value(),
        "malformed deep deck identity stays absent");

    request = passing_request();
    request.stages.deep_reference =
        []() {
            auto gate = passing_deep_reference();
            gate.common_state_critics.pooled
                .models[0]
                .brier =
                std::numeric_limits<double>::
                    quiet_NaN();
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::DeepReference &&
            !result.outcomes
                 .deep_reference_passed
                 .has_value(),
        "nonfinite deep critic content stays absent");

    request = passing_request();
    request.stages.field_regression =
        []() {
            auto gate = passing_field_regression();
            gate.fixtures[0].stable_id = "wrong-order";
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::FieldRegression &&
            !result.outcomes
                 .field_regression_passed
                 .has_value() &&
            !result.outcomes
                 .treatment_vs_control_passed
                 .has_value(),
        "malformed field identity stays absent");

    request = passing_request();
    request.stages.field_regression =
        []() {
            auto gate = passing_field_regression();
            auto& fixture = gate.fixtures[0];
            fixture.stable_reference_best_set = true;
            fixture.control_agrees = true;
            fixture.treatment_agrees = false;
            gate.stable_fixture_count = 1;
            gate.control_agreements = 1;
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::FieldRegression &&
            !result.outcomes
                 .field_regression_passed
                 .has_value(),
        "malformed field loss boolean stays absent");

    request = passing_request();
    request.stages.treatment_vs_control =
        []() {
            auto gate = passing_direct_gameplay();
            gate.identity_exact = false;
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::
                    TreatmentVsControl &&
            !result.outcomes
                 .treatment_vs_control_passed
                 .has_value(),
        "malformed direct pass stays absent");

    request = passing_request();
    request.stages.fixed_seed_panels =
        []() {
            auto gate = passing_fixed_panel_set();
            gate.panel_count_exact = false;
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::FixedSeedPanels &&
            !result.outcomes
                 .fixed_seed_panel_passed
                 .has_value(),
        "malformed fixed-panel pass stays absent");

    request = passing_request();
    request.stages.final_direct_pool =
        []() {
            auto gate = passing_final_direct();
            gate.primary.challenger_win_rate_percent =
                56.0;
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::FinalDirectPool &&
            !result.outcomes
                 .final_direct_pool_passed
                 .has_value(),
        "malformed final-pool pass stays absent");

    request = passing_request();
    request.stages.mixed_field =
        []() {
            auto gate = passing_mixed_field();
            gate.by_deck[0]
                .learned_lift_percentage_points = 1.0;
            return gate;
        };
    result = run_for_testing(request);
    expect(
        result.terminal_stage ==
                runner::RunnerStage::MixedField &&
            !result.outcomes
                 .mixed_field_passed
                 .has_value() &&
            !result.decision.complete,
        "malformed mixed-field pass cannot complete");
}

void test_missing_or_throwing_callback_fails_closed() {
    auto request = passing_request();
    request.stages.deep_reference = {};
    auto result =
        run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            result.terminal_stage ==
                runner::RunnerStage::DeepReference,
        "missing callback is infrastructure");

    request = passing_request();
    request.stages.deep_reference =
        []() -> eval::DeepReferenceGateReport {
            throw std::runtime_error("synthetic failure");
        };
    result = run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            result.terminal_stage ==
                runner::RunnerStage::DeepReference &&
            result.stages.back().failures.front().find(
                "synthetic failure") != std::string::npos,
        "callback exception is captured");
}

void test_final_pool_cannot_swap_prior_evidence() {
    std::vector<runner::RunnerStage> calls;
    auto request = passing_request(&calls);
    request.stages.final_direct_pool =
        [&calls]() {
            calls.push_back(
                runner::RunnerStage::FinalDirectPool);
            auto gate = passing_final_direct();
            gate.primary.challenger_win_rate_percent =
                56.0;
            return gate;
        };
    const auto result =
        run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            result.terminal_stage ==
                runner::RunnerStage::FinalDirectPool,
        "final pool must bind prior primary and panel reports");
    expect(
        calls.back() ==
                runner::RunnerStage::FinalDirectPool,
        "mixed field is suppressed after final mismatch");
}

void test_mixed_field_scientific_failure_completes_rejected() {
    auto request = passing_request();
    request.stages.mixed_field =
        []() {
            auto gate = passing_mixed_field();
            auto& deck = gate.by_deck.front();
            deck.random_win_rate_percent = 50.0;
            deck.learned_win_rate_percent = 40.0;
            deck.learned_lift_percentage_points = -10.0;
            deck.best_other_lift_percentage_points = 0.0;
            deck.learned_lift_is_best = false;
            gate.learned_lift_best_on_every_deck = false;
            gate.passed = false;
            gate.failures = {
                "Learned lift is not best on every deck",
            };
            return gate;
        };
    const auto result =
        run_for_testing(request);
    expect(
        result.disposition ==
                runner::RunnerDisposition::Rejected &&
            result.terminal_stage ==
                runner::RunnerStage::MixedField,
        "well-formed lift miss is a rejection");
    expect(
        result.decision.complete &&
            !result.decision.passed,
        "final rejection is a completed evaluation");
}

void test_preflight_failure_runs_no_stage() {
    std::vector<runner::RunnerStage> calls;
    auto request = passing_request(&calls);
    const auto result =
        runner::testing::run_sealed_evaluation(
            request, {});
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            result.terminal_stage ==
                runner::RunnerStage::Preflight,
        "publication failure blocks evaluation");
    expect(calls.empty(), "preflight executes no callback");
}

void test_runner_detects_postflight_artifact_mutation() {
    auto request = passing_request();
    auto captures = std::make_shared<std::size_t>(0);
    const runner::testing::ArtifactSnapshotter snapshotter =
        [captures](std::string_view path) {
            ++*captures;
            auto snapshot =
                passing_snapshotter()(path);
            if (*captures > 3 &&
                path ==
                    runner::kLabelCacheArtifactPath) {
                snapshot.modification_nanoseconds = 5;
            }
            return snapshot;
        };
    const auto result =
        runner::testing::run_sealed_evaluation(
            request, snapshotter);
    expect(
        result.disposition ==
                runner::RunnerDisposition::
                    InfrastructureFailure &&
            result.terminal_stage ==
                runner::RunnerStage::
                    ArtifactPostflight,
        "postflight mutation overrides scientific pass");
    expect(
        result.decision.complete &&
            result.decision.passed &&
            !result.artifact_integrity.passed,
        "scientific result remains inspectable but uncertified");
    expect(
        result.stages.back().stage ==
                runner::RunnerStage::
                    ArtifactPostflight,
        "postflight failure is recorded");
}

void test_stage_names_are_stable() {
    expect(
        runner::runner_stage_name(
            runner::RunnerStage::Preflight) ==
                "preflight" &&
            runner::runner_stage_name(
                runner::RunnerStage::
                    TreatmentVsHandcoded) ==
                "treatment-vs-handcoded" &&
            runner::runner_stage_name(
                runner::RunnerStage::
                    ArtifactPostflight) ==
                "artifact-postflight" &&
            runner::runner_stage_name(
                runner::RunnerStage::Complete) ==
                "complete",
        "stage names are stable");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "exact policy builders",
        test_exact_policy_builders);
    tests.run(
        "exact policy recipe evidence",
        test_exact_policy_recipe_evidence);
    tests.run(
        "training publication validation",
        test_training_publication_validation);
    tests.run(
        "evaluation trusted snapshotter",
        test_evaluation_integrity_uses_trusted_snapshotter);
    tests.run(
        "evaluation snapshot metadata",
        test_evaluation_snapshot_metadata_fails_closed);
    tests.run(
        "model fingerprint validation",
        test_model_fingerprint_validation);
    tests.run(
        "artifact deployment validation",
        test_artifact_deployments_are_validated);
    tests.run(
        "sealed stage order",
        test_all_stages_run_in_sealed_order);
    tests.run(
        "scientific rejection stops",
        test_scientific_rejection_stops_without_salvage);
    tests.run(
        "structural failure typing",
        test_structural_failure_is_infrastructure_failure);
    tests.run(
        "inconsistent report fails closed",
        test_inconsistent_report_fails_closed);
    tests.run(
        "malformed passes stay absent",
        test_malformed_passes_do_not_advance_outcomes);
    tests.run(
        "callback failure handling",
        test_missing_or_throwing_callback_fails_closed);
    tests.run(
        "final pool evidence binding",
        test_final_pool_cannot_swap_prior_evidence);
    tests.run(
        "mixed-field completed rejection",
        test_mixed_field_scientific_failure_completes_rejected);
    tests.run(
        "preflight blocks callbacks",
        test_preflight_failure_runs_no_stage);
    tests.run(
        "postflight artifact mutation",
        test_runner_detects_postflight_artifact_mutation);
    tests.run(
        "stable stage names",
        test_stage_names_are_stable);
    return tests.finish();
}
