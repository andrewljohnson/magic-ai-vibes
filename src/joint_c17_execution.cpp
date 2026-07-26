#include "old_school/joint_c17_execution.hpp"

#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace old_school::joint_c17_execution {
namespace {

using artifact_integrity::RegularFileSnapshot;
using testing::CanonicalArtifactLocations;
using testing::ExpectedRegularFile;

struct SnapshotSet {
    RegularFileSnapshot bundle;
    RegularFileSnapshot parent;
    RegularFileSnapshot label_cache;
};

bool is_lower_sha256(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(), [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

[[noreturn]] void fail(std::string message) {
    throw std::runtime_error(
        "canonical C17-J1 context: " + std::move(message));
}

std::string exception_text(std::exception_ptr exception) {
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "non-standard exception";
    }
}

SnapshotSet capture(const CanonicalArtifactLocations& locations) {
    return {
        .bundle = artifact_integrity::snapshot_regular_file(
            locations.bundle),
        .parent = artifact_integrity::snapshot_regular_file(
            locations.parent),
        .label_cache =
            artifact_integrity::snapshot_regular_file(
                locations.label_cache),
    };
}

void require_identity(
    const RegularFileSnapshot& snapshot,
    const ExpectedRegularFile& expected,
    std::string_view role) {
    if (expected.byte_size == 0 ||
        !is_lower_sha256(expected.sha256)) {
        fail(
            std::string(role) +
            " expected byte identity is malformed");
    }
    if (snapshot.byte_size != expected.byte_size) {
        fail(
            std::string(role) + " byte size mismatch: expected " +
            std::to_string(expected.byte_size) + ", got " +
            std::to_string(snapshot.byte_size));
    }
    if (snapshot.sha256 != expected.sha256) {
        fail(std::string(role) + " SHA-256 mismatch");
    }
}

void require_path_spellings(
    const CanonicalArtifactLocations& locations) {
    if (!locations.require_canonical_path_spellings) {
        return;
    }
    if (locations.bundle.generic_string() !=
            joint_c17_runner::kCanonicalArtifactPath ||
        locations.parent.generic_string() !=
            joint_c17_runner::kParentArtifactPath ||
        locations.label_cache.generic_string() !=
            joint_c17_runner::kLabelCacheArtifactPath) {
        fail("production artifact path spelling mismatch");
    }
}

void require_preflight(
    const CanonicalArtifactLocations& locations,
    const SnapshotSet& snapshots) {
    require_path_spellings(locations);
    if (snapshots.bundle.byte_size == 0 ||
        !is_lower_sha256(snapshots.bundle.sha256)) {
        fail("joint bundle snapshot has no byte identity");
    }
    if (locations.bundle_identity.has_value()) {
        require_identity(
            snapshots.bundle, *locations.bundle_identity,
            "joint bundle");
    }
    require_identity(
        snapshots.parent,
        locations.parent_identity.value_or(
            ExpectedRegularFile{
                .byte_size =
                    joint_c17_runner::kParentArtifactByteSize,
                .sha256 = std::string(
                    joint_c17_runner::kParentArtifactSha256),
            }),
        "frozen C16 parent");
    require_identity(
        snapshots.label_cache,
        locations.label_cache_identity.value_or(
            ExpectedRegularFile{
                .byte_size =
                    joint_c17_runner::
                        kLabelCacheArtifactByteSize,
                .sha256 = std::string(
                    joint_c17_runner::
                        kLabelCacheArtifactSha256),
            }),
        "frozen Dev-v3 label cache");
}

void require_postflight(
    const SnapshotSet& before, const SnapshotSet& after) {
    if (before.bundle != after.bundle) {
        fail("joint bundle changed while context was loading");
    }
    if (before.parent != after.parent) {
        fail("frozen C16 parent changed while context was loading");
    }
    if (before.label_cache != after.label_cache) {
        fail(
            "frozen Dev-v3 label cache changed while context "
            "was loading");
    }
}

probe_runner::ProbeCacheMetadata
canonical_label_metadata(
    const std::filesystem::path& cache_path,
    const std::vector<probes::DecisionProbe>& corpus) {
    probe_runner::ProbeScoreConfig config;
    config.training_games =
        joint_c17_runner::kCanonicalTrainingGames;
    config.training_seed = kDefaultLearnedTrainingSeed;
    config.reference_worlds = kCanonicalLabelWorlds;
    config.reference_horizon_turns =
        kCanonicalLabelHorizonTurns;
    config.reference_rollouts_per_world =
        kCanonicalLabelRolloutsPerWorld;
    config.scoring_value_worlds = 8;
    config.scoring_value_continuation_epsilon = 0.0;
    config.cache_path = cache_path;
    config.refresh_cache = false;
    auto metadata = probe_runner::make_probe_cache_metadata(
        probe_runner::ProbeCorpusKind::DevV3, config, corpus,
        kCanonicalLabelReferenceModelFingerprint);
    if (metadata.information_set_fingerprint !=
        kCanonicalLabelInformationSetFingerprint) {
        fail(
            "frozen Dev-v3 corpus information-set fingerprint "
            "changed");
    }
    return metadata;
}

bool five_deck_balance_exact(
    std::span<const probe_eval::ProbeLabel> labels) {
    if (labels.size() !=
        joint_c17_eval::kExpectedProbeCount) {
        return false;
    }
    std::array<std::size_t, kDeckCount> counts{};
    for (const probe_eval::ProbeLabel& label : labels) {
        const auto deck =
            static_cast<std::size_t>(label.root_deck);
        if (deck >= counts.size()) {
            return false;
        }
        ++counts[deck];
    }
    return std::all_of(
        counts.begin(), counts.end(), [](std::size_t count) {
            return count ==
                   joint_c17_eval::kExpectedProbesPerDeck;
        });
}

void require_bundle_parent_binding(
    const LearnedJointC17Artifact& joint,
    const LearnedValueChallengerArtifact& parent,
    const std::string& parent_fingerprint) {
    const LearnedJointC17Report& report = joint.report();
    if (parent.training_games() !=
            joint_c17_runner::kCanonicalTrainingGames ||
        parent.seed() != kDefaultLearnedTrainingSeed ||
        parent.self_play_generations() != 16) {
        fail("frozen C16 parent coordinates are not exact");
    }
    if (parent_fingerprint !=
            kLearnedJointC17ParentFingerprint ||
        report.parent_fingerprint != parent_fingerprint) {
        fail(
            "joint bundle does not bind the exact frozen C16 "
            "parent fingerprint");
    }
    if (report.parent_components !=
        learned_model_component_fingerprints(
            parent.model())) {
        fail(
            "joint bundle parent component fingerprints do not "
            "match the frozen C16 artifact");
    }
}

} // namespace

CanonicalJointC17Context
CanonicalJointC17Context::load_impl(
    const CanonicalArtifactLocations& locations,
    std::ostream& progress) {
    if (locations.bundle.empty() ||
        locations.parent.empty() ||
        locations.label_cache.empty()) {
        throw std::invalid_argument(
            "canonical C17-J1 artifact paths must not be empty");
    }

    progress
        << "Snapshotting frozen C17-J1 artifact set..."
        << std::flush;
    const SnapshotSet before = capture(locations);
    progress << " done\n";

    const auto verify_failure_postflight =
        [&locations, &before](std::exception_ptr original) {
            try {
                const SnapshotSet after = capture(locations);
                require_postflight(before, after);
            } catch (...) {
                fail(
                    "artifact postflight failed after '" +
                    exception_text(original) +
                    "': " +
                    exception_text(std::current_exception()));
            }
            std::rethrow_exception(original);
        };

    try {
        require_preflight(locations, before);

        progress
            << "Loading exact frozen C17-J1 joint bundle..."
            << std::flush;
        LearnedJointC17Artifact joint =
            load_learned_joint_c17_artifact(
                before.bundle.physical_path,
                joint_c17_runner::kCanonicalTrainingGames,
                kDefaultLearnedTrainingSeed,
                kLearnedJointC17ShardSeed);
        progress << " done\n";

        progress << "Loading exact frozen C16 parent..."
                 << std::flush;
        LearnedValueChallengerArtifact parent =
            load_learned_value_challenger_artifact(
                before.parent.physical_path,
                joint_c17_runner::kCanonicalTrainingGames,
                kDefaultLearnedTrainingSeed, 16);
        const std::string parent_fingerprint =
            learned_model_fingerprint(parent.model());
        require_bundle_parent_binding(
            joint, parent, parent_fingerprint);
        progress << " done\n";

        progress
            << "Loading exact frozen five-deck Dev-v3 labels..."
            << std::flush;
        const std::vector<probes::DecisionProbe> corpus =
            probes::make_probe_dev_v3();
        const probe_runner::ProbeCacheMetadata metadata =
            canonical_label_metadata(
                before.label_cache.physical_path, corpus);
        std::vector<probe_eval::ProbeLabel> labels =
            probe_runner::load_probe_label_cache(
                probe_runner::ProbeCorpusKind::DevV3,
                before.label_cache.physical_path, metadata,
                corpus);
        const bool labels_balanced =
            five_deck_balance_exact(labels);
        if (!labels_balanced) {
            fail(
                "frozen Dev-v3 labels are not exactly four "
                "positions on each of five decks");
        }
        progress << " done\n";

        LearnedJointC17Deployment control =
            joint.control_deployment();
        LearnedJointC17Deployment treatment =
            joint.treatment_deployment();
        const auto fingerprints =
            joint_c17_eval::JointC17ExpectedModelFingerprints{
                .control = joint.report().control_fingerprint,
                .treatment =
                    joint.report().treatment_fingerprint,
            };
        const auto fingerprint_gate =
            joint_c17_runner::validate_model_fingerprints(
                fingerprints);
        if (!fingerprint_gate.passed) {
            fail(
                "joint bundle model fingerprint gate failed");
        }
        const auto deployment_gate =
            joint_c17_runner::validate_frozen_deployments(
                {
                    .control = control,
                    .treatment = treatment,
                    .artifact_control_fingerprint =
                        joint.report().control_fingerprint,
                    .artifact_treatment_fingerprint =
                        joint.report().treatment_fingerprint,
                },
                fingerprints);
        if (!deployment_gate.passed) {
            fail(
                "joint bundle frozen deployment gate failed");
        }

        const SnapshotSet after = capture(locations);
        require_postflight(before, after);
        progress
            << "C17-J1 artifact provenance verified.\n";

        return CanonicalJointC17Context(
            {
                .bundle =
                    {
                        .before = before.bundle,
                        .after = after.bundle,
                    },
                .parent =
                    {
                        .before = before.parent,
                        .after = after.parent,
                    },
                .label_cache =
                    {
                        .before = before.label_cache,
                        .after = after.label_cache,
                    },
                .label_metadata = metadata,
                .model_fingerprints = fingerprints,
                .parent_model_fingerprint =
                    parent_fingerprint,
                .model_fingerprint_gate =
                    fingerprint_gate,
                .deployment_gate = deployment_gate,
                .five_deck_label_balance_exact =
                    labels_balanced,
                .all_bindings_verified = true,
            },
            std::move(joint), std::move(parent),
            std::move(labels), std::move(control),
            std::move(treatment));
    } catch (...) {
        verify_failure_postflight(std::current_exception());
        throw std::logic_error(
            "canonical C17-J1 failure postflight returned");
    }
}

CanonicalJointC17Context::CanonicalJointC17Context(
    CanonicalJointC17Provenance provenance,
    LearnedJointC17Artifact joint_artifact,
    LearnedValueChallengerArtifact parent_artifact,
    std::vector<probe_eval::ProbeLabel> labels,
    LearnedJointC17Deployment control,
    LearnedJointC17Deployment treatment)
    : provenance_(std::move(provenance)),
      joint_artifact_(std::move(joint_artifact)),
      parent_artifact_(std::move(parent_artifact)),
      labels_(std::move(labels)),
      control_(std::move(control)),
      treatment_(std::move(treatment)) {}

const CanonicalJointC17Provenance&
CanonicalJointC17Context::provenance() const {
    return provenance_;
}

const LearnedJointC17Report&
CanonicalJointC17Context::joint_report() const {
    return joint_artifact_.report();
}

const LearnedJointC17Deployment&
CanonicalJointC17Context::control_deployment() const {
    return control_;
}

const LearnedJointC17Deployment&
CanonicalJointC17Context::treatment_deployment() const {
    return treatment_;
}

std::shared_ptr<const LearnedModel>
CanonicalJointC17Context::parent_model() const {
    return parent_artifact_.model();
}

std::span<const probe_eval::ProbeLabel>
CanonicalJointC17Context::labels() const {
    return labels_;
}

CanonicalJointC17Context
load_canonical_joint_c17_context(std::ostream& progress) {
    return CanonicalJointC17Context::load_impl(
        {
            .bundle =
                std::filesystem::path(
                    joint_c17_runner::
                        kCanonicalArtifactPath),
            .parent =
                std::filesystem::path(
                    joint_c17_runner::kParentArtifactPath),
            .label_cache =
                std::filesystem::path(
                    joint_c17_runner::
                        kLabelCacheArtifactPath),
            .require_canonical_path_spellings = true,
        },
        progress);
}

namespace testing {

CanonicalJointC17Context load_canonical_joint_c17_context(
    const CanonicalArtifactLocations& locations,
    std::ostream& progress) {
    return CanonicalJointC17Context::load_impl(
        locations, progress);
}

} // namespace testing

} // namespace old_school::joint_c17_execution
