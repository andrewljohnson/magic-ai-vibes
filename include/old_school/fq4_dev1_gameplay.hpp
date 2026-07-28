#pragma once

#include "old_school/artifact_integrity.hpp"
#include "old_school/game.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev1_gameplay {

inline constexpr std::string_view kReportSchema =
    "old-school-fq4-dev1-gameplay-v1";
inline constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::size_t kParentArtifactBytes = 3'111'437;
inline constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kParentModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::string_view kCandidateModelFingerprint =
    "712600783152e89ff1a53394149764db227e55289a656530342226b7e1ee6151";
// Pinned from the first and only no-replace publication of the canonical
// artifact.
inline constexpr std::size_t kCandidateArtifactBytes = 237'282;
inline constexpr std::string_view kCandidateArtifactSha256 =
    "aca8ba9c337a5b41d0cf624f7ec46ab652c7bebc1b5c2c29fa844b900c467f63";
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
inline constexpr LearnedContinuationController kContinuationController =
    LearnedContinuationController::Legacy;
inline constexpr std::size_t kMaximumTurns = 500;
inline constexpr std::size_t kRootSearchDepth = 1;

inline constexpr std::uint64_t kSmokeSeed = 202607280601ULL;
inline constexpr std::size_t kSmokeRepetitions = 4;
inline constexpr std::uint64_t kMilestoneC16Seed =
    202607280602ULL;
inline constexpr std::uint64_t kMilestoneHandcraftedSeed =
    202607280603ULL;
inline constexpr std::size_t kMilestoneRepetitions = 34;

enum class Mode : std::uint8_t {
    Smoke,
    MilestoneC16,
    MilestoneHandcrafted,
};

// Promotion is deliberately a code-review boundary. GP0 is the only
// licensed production mode until its preregistered result is accepted and a
// later commit changes exactly the next license.
inline constexpr bool kSmokeLicensed = true;
inline constexpr bool kMilestoneC16Licensed = false;
inline constexpr bool kMilestoneHandcraftedLicensed = false;

constexpr bool mode_is_licensed(Mode mode) noexcept {
    switch (mode) {
    case Mode::Smoke:
        return kSmokeLicensed;
    case Mode::MilestoneC16:
        return kMilestoneC16Licensed;
    case Mode::MilestoneHandcrafted:
        return kMilestoneC16Licensed &&
               kMilestoneHandcraftedLicensed;
    }
    return false;
}

struct ModeSpec {
    Mode mode = Mode::Smoke;
    std::string_view token;
    std::uint64_t evaluation_seed = 0;
    std::size_t repetitions = 0;
    bool run_identical_control = false;
    bool handcrafted_baseline = false;

    bool operator==(const ModeSpec&) const = default;
};

ModeSpec mode_spec(Mode mode);
Mode parse_mode(std::span<const std::string_view> arguments);

struct FixedDeployment {
    std::shared_ptr<const LearnedModel> parent;
    std::shared_ptr<const LearnedModel> candidate;
    std::string parent_model_fingerprint;
    std::string candidate_model_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    artifact_integrity::RegularFileSnapshot parent_before;
    artifact_integrity::RegularFileSnapshot candidate_artifact_before;
    std::string candidate_artifact_family;
    std::string candidate_artifact_recipe;
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
    bool requests_exact = false;
    bool accounting_complete = false;
    bool control_exact = true;
    bool aggregate_floor = true;
    bool runtime_ratio = true;
    bool aggregate_majority = true;
    bool aggregate_wilson = true;
    bool every_deck_majority = true;
    bool infrastructure_valid = false;
    bool passed = false;
    std::vector<std::string> failures;
};

struct RunReport {
    ModeSpec spec;
    FixedDeployment deployment;
    artifact_integrity::RegularFileSnapshot parent_after;
    artifact_integrity::RegularFileSnapshot candidate_artifact_after;
    ModelIdentity parent_model_after;
    ModelIdentity candidate_model_after;
    bool request_contract_exact = false;
    std::optional<TimedBenchmark> identical_control;
    TimedBenchmark candidate;
    GateReport gate;
};

std::string format_report(const RunReport& report);
RunReport run_fixed_mode(Mode mode);
int run_cli(int argc, char* argv[], std::ostream& output,
            std::ostream& error);

namespace testing {

using BenchmarkExecutor =
    std::function<TimedBenchmark(const BenchmarkRequest&)>;
using DeploymentLoader =
    std::function<FixedDeployment()>;
using Snapshotter = std::function<
    artifact_integrity::RegularFileSnapshot(const std::string&)>;
using ModelInspector = std::function<
    ModelIdentity(std::shared_ptr<const LearnedModel>)>;

BotConfig make_learned_bot(
    std::shared_ptr<const LearnedModel> model);
BotConfig make_handcrafted_bot();
GameConfig make_game_config();
RunReport run_with(
    Mode mode, FixedDeployment deployment,
    const BenchmarkExecutor& benchmark,
    const Snapshotter& snapshot,
    const ModelInspector& inspect_model);
RunReport run_licensed_with(
    Mode mode, const DeploymentLoader& load_deployment,
    const BenchmarkExecutor& benchmark,
    const Snapshotter& snapshot,
    const ModelInspector& inspect_model);
GateReport evaluate_gate(const RunReport& report);

} // namespace testing

} // namespace old_school::fq4_dev1_gameplay
