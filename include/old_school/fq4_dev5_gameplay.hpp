#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_candidate_artifact.hpp"
#include "old_school/game.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev5_gameplay {

inline constexpr std::string_view kReportSchema =
    "old-school-fq4-dev5-gameplay-v1";
inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::uint64_t kParentArtifactBytes =
    3'111'437;
inline constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab0"
    "66bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kParentModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa9"
    "74c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::string_view kCandidateArtifactPath =
    "data/"
    "old-school-fq4-dev5-neutral-anchored-candidate-v1."
    "fq4candidate";
inline constexpr std::uint64_t kCandidateArtifactBytes =
    237'496;
inline constexpr std::string_view kCandidateArtifactSha256 =
    "8252646b8eef00d1abb5779d91fdc6b1d"
    "a2a67d35cd786a835818461b210f5fc";
inline constexpr std::string_view kCandidateModelFingerprint =
    "22834a951e8338568be93561a34c6b1df"
    "588faa71feb9d184ab62021b03b2171";

inline constexpr std::size_t kTrainingGames = 800;
inline constexpr std::uint64_t kTrainingSeed = 424242;
inline constexpr std::size_t kParentGenerations = 16;
inline constexpr std::size_t kWorldsPerAction = 8;
inline constexpr std::size_t kRolloutsPerWorld =
    kLearnedValueSearchRolloutsPerWorld;
inline constexpr std::size_t kHorizonTurns = 4;
inline constexpr bool kBlendShallowPrior =
    kLearnedValueSearchBlendsShallowPrior;
inline constexpr double kRootExploration = 0.0;
inline constexpr double kContinuationEpsilon = 0.0;
inline constexpr double kPriorityResidualWeight = 0.10;
inline constexpr bool kPassDominance = false;
inline constexpr LearnedContinuationController
    kContinuationController =
        LearnedContinuationController::Legacy;
inline constexpr std::size_t kMaximumTurns = 500;
inline constexpr std::size_t kRootSearchDepth = 1;

inline constexpr std::uint64_t kSmokeSeed =
    202607280701ULL;
inline constexpr std::size_t kSmokeRepetitions = 4;
inline constexpr std::size_t kSmokeGames =
    60 * kSmokeRepetitions;
inline constexpr std::size_t kSmokeGamesPerDeck =
    12 * kSmokeRepetitions;
inline constexpr double kSmokeWinRateFloor = 40.0;
inline constexpr double kSmokeRuntimeRatioLimit = 1.25;

const fq4_dev_candidate_artifact::Contract&
anchored_contract();

struct FixedDeployment {
    std::shared_ptr<const LearnedModel> parent;
    std::shared_ptr<const LearnedModel> candidate;
    std::string parent_model_fingerprint;
    std::string candidate_model_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot
        candidate_artifact_before;
    fq4_dev_candidate_artifact::Report candidate_report;
    bool fixed_identity_valid = false;
};

struct BenchmarkRequest {
    std::string role;
    std::size_t repetitions = 0;
    std::uint64_t evaluation_seed = 0;
    BotConfig challenger;
    BotConfig baseline;
    GameConfig game;
    bool allow_identical_policy_control = false;
};

struct TimedBenchmark {
    BotBenchmarkSummary summary;
    double elapsed_seconds = 0.0;
};

struct ModelIdentity {
    std::string fingerprint;
    LearnedModelComponentFingerprints components;

    bool operator==(const ModelIdentity&) const = default;
};

struct GateReport {
    bool fixed_identity = false;
    bool artifacts_unchanged = false;
    bool models_unchanged = false;
    bool accounting_complete = false;
    bool control_exact = false;
    bool aggregate_floor = false;
    bool runtime_ratio = false;
    bool infrastructure_valid = false;
    bool passed = false;
    std::vector<std::string> failures;
};

struct RunReport {
    FixedDeployment deployment;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot
        candidate_artifact_after;
    ModelIdentity parent_model_after;
    ModelIdentity candidate_model_after;
    TimedBenchmark identical_control;
    TimedBenchmark candidate;
    GateReport gate;
};

std::string format_report(const RunReport& report);
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

namespace testing {

struct ParentArtifact {
    std::shared_ptr<const LearnedModel> model;
    std::size_t training_games = 0;
    std::uint64_t training_seed = 0;
    std::size_t generations = 0;
};

struct LoadedCandidate {
    std::shared_ptr<const LearnedModel> model;
    fq4_dev_candidate_artifact::Report report;
};

struct LoaderDependencies {
    std::function<artifact_integrity::RegularFileSnapshot(
        const std::filesystem::path&)>
        snapshot;
    std::function<ParentArtifact(
        const std::filesystem::path&)>
        load_parent;
    std::function<LoadedCandidate(
        const std::filesystem::path&,
        std::shared_ptr<const LearnedModel>,
        const fq4_dev_candidate_artifact::Contract&,
        const fq4_dev_candidate_artifact::FileIdentity&)>
        load_candidate;
    std::function<ModelIdentity(
        std::shared_ptr<const LearnedModel>)>
        inspect_model;
};

using BenchmarkExecutor =
    std::function<TimedBenchmark(const BenchmarkRequest&)>;
using Snapshotter = std::function<
    artifact_integrity::RegularFileSnapshot(
        const std::string&)>;
using ModelInspector = std::function<ModelIdentity(
    std::shared_ptr<const LearnedModel>)>;
using SmokeRunner = std::function<RunReport()>;

void validate_anchored_contract(
    const fq4_dev_candidate_artifact::Contract& contract);
FixedDeployment load_fixed_deployment_with(
    const LoaderDependencies& dependencies);
BotConfig make_learned_bot(
    std::shared_ptr<const LearnedModel> model);
GameConfig make_game_config();
bool benchmark_accounting_exact(
    const BotBenchmarkSummary& summary,
    const BotConfig& expected_challenger,
    const BotConfig& expected_baseline,
    std::string_view expected_challenger_fingerprint,
    std::string_view expected_baseline_fingerprint);
GateReport evaluate_gate(const RunReport& report);
RunReport run_with(
    FixedDeployment deployment,
    const BenchmarkExecutor& benchmark,
    const Snapshotter& snapshot,
    const ModelInspector& inspect_model);
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, const SmokeRunner& run_smoke);

} // namespace testing

} // namespace old_school::fq4_dev5_gameplay
