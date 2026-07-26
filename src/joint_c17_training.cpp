#include "old_school/joint_c17_training.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <exception>
#include <filesystem>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>

namespace old_school::joint_c17_training {
namespace {

using artifact_integrity::RegularFileSnapshot;
using joint_c17_runner::TrainingArtifactSnapshot;

struct RunCoordinates {
    std::filesystem::path parent;
    std::filesystem::path label_cache;
    std::filesystem::path target;
    LearnedJointC17Config config;
    bool production = false;
};

[[noreturn]] void fail(std::string message) {
    throw std::runtime_error(
        "C17-J1 training publication: " + std::move(message));
}

void record(
    bool condition, std::string message,
    TrainingValidationGateReport& gate) {
    if (!condition) {
        gate.failures.push_back(std::move(message));
    }
}

bool is_lower_sha256(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(), [](char character) {
                   return
                       (character >= '0' && character <= '9') ||
                       (character >= 'a' && character <= 'f');
               });
}

std::filesystem::path absolute_normal(
    const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path result =
        std::filesystem::absolute(path, error).lexically_normal();
    if (error) {
        fail(
            "cannot resolve path '" + path.string() +
            "': " + error.message());
    }
    return result;
}

TrainingArtifactSnapshot missing_target_snapshot(
    const std::filesystem::path& target) {
    const std::filesystem::path resolved =
        absolute_normal(target);
    struct stat status {};
    while (::lstat(resolved.c_str(), &status) != 0) {
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        if (error == ENOENT) {
            return {
                .requested_path =
                    std::string(
                        joint_c17_runner::
                            kCanonicalArtifactPath),
                .resolved_path = resolved.string(),
                .exists = false,
                .regular_file = false,
                .byte_size = 0,
                .sha256 = {},
                .sha256_verified = false,
            };
        }
        throw std::system_error(
            error, std::generic_category(),
            "cannot inspect C17-J1 publication target '" +
                resolved.string() + "'");
    }

    std::string kind = "filesystem object";
    if (S_ISREG(status.st_mode)) {
        kind = "regular file";
    } else if (S_ISDIR(status.st_mode)) {
        kind = "directory";
    } else if (S_ISLNK(status.st_mode)) {
        kind = "symbolic link";
    }
    fail(
        "target must be missing before training; found " + kind +
        " at '" + resolved.string() + "'");
}

TrainingArtifactSnapshot published_snapshot(
    const RegularFileSnapshot& snapshot) {
    return {
        .requested_path =
            std::string(
                joint_c17_runner::kCanonicalArtifactPath),
        .resolved_path = snapshot.path,
        .exists = true,
        .regular_file = true,
        .byte_size = snapshot.byte_size,
        .sha256 = snapshot.sha256,
        .sha256_verified = true,
    };
}

bool config_is_canonical(
    const LearnedJointC17Config& config) {
    return config.training_games ==
               joint_c17_runner::kCanonicalTrainingGames &&
           config.parent_training_seed ==
               kDefaultLearnedTrainingSeed &&
           config.parent_generations == 16 &&
           config.shard_seed == kLearnedJointC17ShardSeed &&
           config.balanced_blocks == 5 &&
           config.max_game_turns == 500 &&
           config.required_parent_fingerprint ==
               kLearnedJointC17ParentFingerprint;
}

bool is_reserved_seed(std::uint64_t seed) {
    constexpr std::array<std::uint64_t, 13> kReserved = {
        kLearnedJointC17ShardSeed,
        kLearnedJointC17HoldoutSeed,
        kLearnedJointC17MatchedControlGameplaySeed,
        kLearnedJointC17FrozenC16GameplaySeed,
        kLearnedJointC17HandcodedGameplaySeed,
        101ULL,
        202ULL,
        303ULL,
        404ULL,
        505ULL,
        606ULL,
        707ULL,
        808ULL,
    };
    return std::find(
               kReserved.begin(), kReserved.end(), seed) !=
           kReserved.end();
}

void require_miniature_request(
    const testing::MiniatureTrainingRequest& request) {
    const LearnedJointC17Config& config = request.config;
    if (request.logical_root.empty()) {
        throw std::invalid_argument(
            "miniature C17-J1 logical root must not be empty");
    }
    if (config.training_games == 0 ||
        config.training_games > 4 ||
        config.parent_generations < 2 ||
        config.parent_generations > 4 ||
        config.balanced_blocks != 1 ||
        config.max_game_turns == 0 ||
        config.max_game_turns > 16 ||
        config.required_parent_fingerprint.empty() ||
        !is_lower_sha256(
            config.required_parent_fingerprint) ||
        is_reserved_seed(config.parent_training_seed) ||
        is_reserved_seed(config.shard_seed)) {
        throw std::invalid_argument(
            "miniature C17-J1 request must use a bounded "
            "nonreserved fixture recipe");
    }

    const std::filesystem::path root =
        absolute_normal(request.logical_root);
    const std::filesystem::path production_target =
        absolute_normal(
            std::filesystem::path(
                joint_c17_runner::kCanonicalArtifactPath));
    const std::filesystem::path relocated_target =
        (root /
         std::filesystem::path(
             joint_c17_runner::kCanonicalArtifactPath))
            .lexically_normal();
    if (relocated_target == production_target) {
        throw std::invalid_argument(
            "miniature C17-J1 root must not address the "
            "production artifact tree");
    }
}

bool report_matches_request(
    const LearnedJointC17Report& report,
    const LearnedJointC17Config& config,
    const LearnedValueChallengerArtifact& parent) {
    return report.training_games == config.training_games &&
           report.parent_training_seed ==
               config.parent_training_seed &&
           report.parent_generations ==
               config.parent_generations &&
           report.shard_seed == config.shard_seed &&
           report.shard_generation ==
               config.parent_generations + 1 &&
           report.balanced_blocks ==
               config.balanced_blocks &&
           report.collection_max_game_turns ==
               config.max_game_turns &&
           report.parent_fingerprint ==
               config.required_parent_fingerprint &&
           report.parent_fingerprint ==
               learned_model_fingerprint(parent.model()) &&
           report.parent_components ==
               learned_model_component_fingerprints(
                   parent.model());
}

bool deployment_exact(
    const LearnedJointC17Deployment& deployment,
    LearnedJointC17Arm arm, std::string_view token,
    std::size_t training_games, bool pass_dominance,
    LearnedContinuationController controller,
    std::string_view expected_fingerprint) {
    return deployment.arm == arm &&
           deployment.policy_token == token &&
           deployment.model &&
           deployment.bot.kind == BotKind::Learned &&
           deployment.bot.learned_variant ==
               LearnedVariant::ValueSearchChampion &&
           deployment.bot.rollouts_per_action == 8 &&
           deployment.bot.exploration_rate == 0.0 &&
           deployment.bot.value_continuation_epsilon == 0.0 &&
           deployment.bot.value_priority_residual_weight ==
               0.0 &&
           deployment.bot.value_pass_dominance ==
               pass_dominance &&
           deployment.bot.value_continuation_controller ==
               controller &&
           deployment.bot.training_games == training_games &&
           deployment.bot.learned_model == deployment.model &&
           deployment.search.seed == 0 &&
           deployment.search.worlds == 8 &&
           deployment.search.rollouts_per_world == 1 &&
           deployment.search.horizon_turns ==
               kLearnedValueSearchHorizonTurns &&
           deployment.search.continuation_variant ==
               LearnedVariant::ValueSearchChampion &&
           deployment.search.value_continuation_epsilon ==
               0.0 &&
           deployment.search.blend_shallow_prior &&
           deployment.search.value_priority_residual_weight ==
               0.0 &&
           deployment.search.value_pass_dominance ==
               pass_dominance &&
           deployment.search.value_continuation_controller ==
               controller &&
           deployment.search.evaluation_threads == 1 &&
           learned_model_fingerprint(deployment.model) ==
               expected_fingerprint;
}

bool deployments_exact(
    const LearnedJointC17Deployment& control,
    const LearnedJointC17Deployment& treatment,
    const LearnedJointC17Report& report) {
    return control.model != treatment.model &&
           deployment_exact(
               control, LearnedJointC17Arm::Control,
               kLearnedJointC17ControlPolicyToken,
               report.training_games, false,
               LearnedContinuationController::Legacy,
               report.control_fingerprint) &&
           deployment_exact(
               treatment, LearnedJointC17Arm::Treatment,
               kLearnedJointC17TreatmentPolicyToken,
               report.training_games, true,
               LearnedContinuationController::
                   PublicStackPassV1,
               report.treatment_fingerprint);
}

bool model_fingerprints_exact(
    const LearnedJointC17Artifact& artifact,
    const LearnedValueChallengerArtifact& parent) {
    const LearnedJointC17Report& report = artifact.report();
    return report.control_fingerprint ==
               learned_model_fingerprint(
                   artifact.control_model()) &&
           report.treatment_fingerprint ==
               learned_model_fingerprint(
                   artifact.treatment_model()) &&
           report.control_components ==
               learned_model_component_fingerprints(
                   artifact.control_model()) &&
           report.treatment_components ==
               learned_model_component_fingerprints(
                   artifact.treatment_model()) &&
           report.control_fingerprint !=
               report.treatment_fingerprint &&
           report.control_fingerprint !=
               learned_model_fingerprint(parent.model()) &&
           report.treatment_fingerprint !=
               learned_model_fingerprint(parent.model());
}

bool evaluation_manifest_exact(
    const joint_c17_runner::
        EvaluationArtifactIntegrityRequest& request,
    const joint_c17_runner::TrainingPublicationEvidence&
        publication) {
    return request.artifacts.size() == 3 &&
           request.artifacts[0].role ==
               joint_c17_runner::kJointBundleArtifactRole &&
           request.artifacts[0].requested_path ==
               joint_c17_runner::kCanonicalArtifactPath &&
           request.artifacts[0].expected_byte_size ==
               publication.after.byte_size &&
           request.artifacts[0].expected_sha256 ==
               publication.after.sha256 &&
           request.artifacts[1].role ==
               joint_c17_runner::kParentModelArtifactRole &&
           request.artifacts[1].requested_path ==
               joint_c17_runner::kParentArtifactPath &&
           request.artifacts[1].expected_byte_size ==
               joint_c17_runner::kParentArtifactByteSize &&
           request.artifacts[1].expected_sha256 ==
               joint_c17_runner::kParentArtifactSha256 &&
           request.artifacts[2].role ==
               joint_c17_runner::kLabelCacheArtifactRole &&
           request.artifacts[2].requested_path ==
               joint_c17_runner::kLabelCacheArtifactPath &&
           request.artifacts[2].expected_byte_size ==
               joint_c17_runner::kLabelCacheArtifactByteSize &&
           request.artifacts[2].expected_sha256 ==
               joint_c17_runner::kLabelCacheArtifactSha256;
}

TrainingPublicationResult train_and_publish(
    RunCoordinates coordinates, std::ostream& progress) {
    TrainingPublicationResult result;
    TrainingValidationGateReport& gate = result.gate;
    gate.request_exact =
        coordinates.production
            ? config_is_canonical(coordinates.config)
            : !config_is_canonical(coordinates.config) &&
                  !is_reserved_seed(
                      coordinates.config.parent_training_seed) &&
                  !is_reserved_seed(
                      coordinates.config.shard_seed);
    if (!gate.request_exact) {
        fail("training request is not the sealed recipe");
    }

    progress << "Snapshotting frozen C17-J1 parent..."
             << std::flush;
    result.parent_before =
        artifact_integrity::snapshot_regular_file(
            coordinates.parent);
    progress << " done\n";

    gate.parent_byte_identity_verified =
        !coordinates.production ||
        (result.parent_before.byte_size ==
             joint_c17_runner::kParentArtifactByteSize &&
         result.parent_before.sha256 ==
             joint_c17_runner::kParentArtifactSha256);
    if (!gate.parent_byte_identity_verified) {
        fail("frozen C16 parent byte identity mismatch");
    }

    progress << "Loading and verifying frozen C16 parent..."
             << std::flush;
    const LearnedValueChallengerArtifact parent =
        load_learned_value_challenger_artifact(
            result.parent_before.physical_path,
            coordinates.config.training_games,
            coordinates.config.parent_training_seed,
            coordinates.config.parent_generations);
    gate.parent_coordinates_exact =
        parent.training_games() ==
            coordinates.config.training_games &&
        parent.seed() ==
            coordinates.config.parent_training_seed &&
        parent.self_play_generations() ==
            coordinates.config.parent_generations &&
        (!coordinates.production ||
         (parent.training_games() ==
              joint_c17_runner::kCanonicalTrainingGames &&
          parent.seed() == kDefaultLearnedTrainingSeed &&
          parent.self_play_generations() == 16));
    gate.parent_fingerprint_exact =
        learned_model_fingerprint(parent.model()) ==
            coordinates.config.required_parent_fingerprint &&
        (!coordinates.production ||
         learned_model_fingerprint(parent.model()) ==
             kLearnedJointC17ParentFingerprint);
    if (!gate.parent_coordinates_exact ||
        !gate.parent_fingerprint_exact) {
        fail(
            "frozen C16 parent coordinates or fingerprint "
            "mismatch");
    }
    progress << " done\n";

    progress << "Snapshotting frozen Dev-v3 label cache..."
             << std::flush;
    result.label_cache_before =
        artifact_integrity::snapshot_regular_file(
            coordinates.label_cache);
    gate.label_cache_byte_identity_verified =
        result.label_cache_before.byte_size ==
            joint_c17_runner::kLabelCacheArtifactByteSize &&
        result.label_cache_before.sha256 ==
            joint_c17_runner::kLabelCacheArtifactSha256;
    if (!gate.label_cache_byte_identity_verified) {
        fail("frozen Dev-v3 label-cache byte identity mismatch");
    }
    progress << " done\n";

    result.publication.before =
        missing_target_snapshot(coordinates.target);
    gate.target_resolved_path_exact =
        result.publication.before.resolved_path ==
        absolute_normal(coordinates.target).string();
    gate.target_missing_before_training =
        !result.publication.before.exists;
    if (!gate.target_resolved_path_exact ||
        !gate.target_missing_before_training) {
        fail(
            "publication target coordinate was not exact and "
            "missing");
    }

    progress << "Training sealed C17-J1 paired family..."
             << std::flush;
    LearnedJointC17Artifact trained =
        train_learned_joint_c17_family(coordinates.config);
    progress << " done\n";

    const LearnedJointC17Report& trained_report =
        trained.report();
    const auto trained_control =
        trained.control_deployment();
    const auto trained_treatment =
        trained.treatment_deployment();
    gate.trained_report_exact =
        report_matches_request(
            trained_report, coordinates.config, parent);
    gate.trained_model_fingerprints_exact =
        model_fingerprints_exact(trained, parent);
    gate.trained_deployments_exact =
        deployments_exact(
            trained_control, trained_treatment,
            trained_report);
    if (!gate.trained_report_exact ||
        !gate.trained_model_fingerprints_exact ||
        !gate.trained_deployments_exact) {
        fail(
            "trained family failed report, fingerprint, or "
            "deployment validation");
    }

    result.parent_before_publication =
        artifact_integrity::snapshot_regular_file(
            coordinates.parent);
    result.label_cache_before_publication =
        artifact_integrity::snapshot_regular_file(
            coordinates.label_cache);
    gate.prerequisites_unchanged_before_publication =
        result.parent_before_publication ==
            result.parent_before &&
        result.label_cache_before_publication ==
            result.label_cache_before;
    if (!gate.prerequisites_unchanged_before_publication) {
        fail(
            "frozen parent or label cache changed during "
            "training; refusing publication");
    }

    progress << "Publishing C17-J1 artifact atomically..."
             << std::flush;
    write_learned_joint_c17_artifact_atomic(
        coordinates.target.string(), trained);
    result.publication.atomic_no_replace_confirmed = true;
    progress << " done\n";

    const RegularFileSnapshot published =
        artifact_integrity::snapshot_regular_file(
            coordinates.target);
    result.publication.after =
        published_snapshot(published);
    result.publication_gate =
        joint_c17_runner::validate_training_publication(
            result.publication);
    gate.publication_gate_passed =
        result.publication_gate.passed;
    gate.published_bytes_pinned =
        published.byte_size > 0 &&
        is_lower_sha256(published.sha256) &&
        result.publication.after.sha256_verified &&
        result.publication.after.resolved_path ==
            absolute_normal(coordinates.target).string();
    if (!gate.publication_gate_passed ||
        !gate.published_bytes_pinned) {
        fail("published artifact evidence failed validation");
    }

    progress << "Reloading and checking published C17-J1 artifact..."
             << std::flush;
    LearnedJointC17Artifact reloaded =
        load_learned_joint_c17_artifact(
            published.physical_path,
            coordinates.config.training_games,
            coordinates.config.parent_training_seed,
            coordinates.config.shard_seed);
    const auto reloaded_control =
        reloaded.control_deployment();
    const auto reloaded_treatment =
        reloaded.treatment_deployment();
    gate.reloaded_report_identical =
        reloaded.report() == trained.report();
    gate.reloaded_model_fingerprints_identical =
        model_fingerprints_exact(reloaded, parent) &&
        learned_model_fingerprint(
            reloaded.control_model()) ==
            learned_model_fingerprint(
                trained.control_model()) &&
        learned_model_fingerprint(
            reloaded.treatment_model()) ==
            learned_model_fingerprint(
                trained.treatment_model()) &&
        learned_model_component_fingerprints(
            reloaded.control_model()) ==
            learned_model_component_fingerprints(
                trained.control_model()) &&
        learned_model_component_fingerprints(
            reloaded.treatment_model()) ==
            learned_model_component_fingerprints(
                trained.treatment_model());
    gate.reloaded_deployments_exact =
        deployments_exact(
            reloaded_control, reloaded_treatment,
            reloaded.report());
    result.published_after_reload =
        artifact_integrity::snapshot_regular_file(
            coordinates.target);
    gate.published_artifact_unchanged =
        result.published_after_reload == published;
    result.parent_after =
        artifact_integrity::snapshot_regular_file(
            coordinates.parent);
    gate.parent_unchanged =
        result.parent_after == result.parent_before;
    result.label_cache_after =
        artifact_integrity::snapshot_regular_file(
            coordinates.label_cache);
    gate.label_cache_unchanged =
        result.label_cache_after ==
        result.label_cache_before;
    progress << " done\n";

    result.report = reloaded.report();
    result.model_fingerprints = {
        .control = result.report.control_fingerprint,
        .treatment = result.report.treatment_fingerprint,
    };
    result.model_fingerprint_gate =
        joint_c17_runner::validate_model_fingerprints(
            result.model_fingerprints);

    if (coordinates.production) {
        result.canonical_deployment_gate =
            joint_c17_runner::validate_frozen_deployments(
                {
                    .control = reloaded_control,
                    .treatment = reloaded_treatment,
                    .artifact_control_fingerprint =
                        result.report.control_fingerprint,
                    .artifact_treatment_fingerprint =
                        result.report.treatment_fingerprint,
                },
                result.model_fingerprints);
        result.evaluation_integrity =
            joint_c17_runner::
                evaluation_artifact_integrity_from_publication(
                    result.publication);
        gate.canonical_runner_gates_passed =
            result.model_fingerprint_gate.passed &&
            result.canonical_deployment_gate->passed;
        gate.evaluation_manifest_ready =
            result.evaluation_integrity.has_value() &&
            evaluation_manifest_exact(
                *result.evaluation_integrity,
                result.publication);
    } else {
        gate.canonical_runner_gates_passed =
            result.model_fingerprint_gate.passed;
        gate.evaluation_manifest_ready = true;
    }

    record(
        gate.request_exact,
        "training request was not exact", gate);
    record(
        gate.parent_byte_identity_verified,
        "parent byte identity was not verified", gate);
    record(
        gate.parent_coordinates_exact,
        "parent coordinates were not exact", gate);
    record(
        gate.parent_fingerprint_exact,
        "parent fingerprint was not exact", gate);
    record(
        gate.label_cache_byte_identity_verified,
        "label-cache byte identity was not verified", gate);
    record(
        gate.target_resolved_path_exact,
        "target resolved path was not exact", gate);
    record(
        gate.target_missing_before_training,
        "target was not missing before training", gate);
    record(
        gate.trained_report_exact,
        "trained report was not exact", gate);
    record(
        gate.trained_model_fingerprints_exact,
        "trained model fingerprints were not exact", gate);
    record(
        gate.trained_deployments_exact,
        "trained deployments were not exact", gate);
    record(
        gate.prerequisites_unchanged_before_publication,
        "prerequisites changed before publication", gate);
    record(
        gate.publication_gate_passed,
        "publication gate failed", gate);
    record(
        gate.published_bytes_pinned,
        "published bytes were not pinned", gate);
    record(
        gate.reloaded_report_identical,
        "reloaded report differed", gate);
    record(
        gate.reloaded_model_fingerprints_identical,
        "reloaded model fingerprints differed", gate);
    record(
        gate.reloaded_deployments_exact,
        "reloaded deployments were not exact", gate);
    record(
        gate.parent_unchanged,
        "parent changed during training/publication", gate);
    record(
        gate.label_cache_unchanged,
        "label cache changed during training/publication", gate);
    record(
        gate.published_artifact_unchanged,
        "published artifact changed during reload", gate);
    record(
        gate.canonical_runner_gates_passed,
        "canonical runner fingerprint/deployment gates failed",
        gate);
    record(
        gate.evaluation_manifest_ready,
        "evaluation integrity manifest is not ready", gate);

    gate.passed = gate.failures.empty();
    if (!gate.passed) {
        fail(
            "post-publication validation failed: " +
            gate.failures.front());
    }
    progress
        << "C17-J1 training publication verified and frozen.\n";
    return result;
}

} // namespace

TrainingPublicationResult
train_and_publish_canonical_joint_c17(std::ostream& progress) {
    return train_and_publish(
        {
            .parent =
                std::filesystem::path(
                    joint_c17_runner::kParentArtifactPath),
            .label_cache =
                std::filesystem::path(
                    joint_c17_runner::
                        kLabelCacheArtifactPath),
            .target =
                std::filesystem::path(
                    joint_c17_runner::kCanonicalArtifactPath),
            .config =
                LearnedJointC17Config{
                    .training_games =
                        joint_c17_runner::
                            kCanonicalTrainingGames,
                    .parent_training_seed =
                        kDefaultLearnedTrainingSeed,
                    .parent_generations = 16,
                    .shard_seed =
                        kLearnedJointC17ShardSeed,
                    .balanced_blocks = 5,
                    .max_game_turns = 500,
                    .required_parent_fingerprint =
                        std::string(
                            kLearnedJointC17ParentFingerprint),
                },
            .production = true,
        },
        progress);
}

namespace testing {

TrainingPublicationResult
train_and_publish_miniature_joint_c17(
    const MiniatureTrainingRequest& request,
    std::ostream& progress) {
    require_miniature_request(request);
    const std::filesystem::path root =
        absolute_normal(request.logical_root);
    return train_and_publish(
        {
            .parent =
                (root /
                 std::filesystem::path(
                     joint_c17_runner::
                         kParentArtifactPath))
                    .lexically_normal(),
            .label_cache =
                (root /
                 std::filesystem::path(
                     joint_c17_runner::
                         kLabelCacheArtifactPath))
                    .lexically_normal(),
            .target =
                (root /
                 std::filesystem::path(
                     joint_c17_runner::
                         kCanonicalArtifactPath))
                    .lexically_normal(),
            .config = request.config,
            .production = false,
        },
        progress);
}

} // namespace testing

} // namespace old_school::joint_c17_training
