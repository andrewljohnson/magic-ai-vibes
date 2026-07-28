#include "old_school/fq4_dev1_gameplay.hpp"

#include "old_school/fq4_dev_candidate_artifact.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace old_school::fq4_dev1_gameplay {
namespace {

namespace candidate_artifact =
    fq4_dev_candidate_artifact;

static_assert(
    kHorizonTurns ==
    kLearnedValueSearchHorizonTurns);
static_assert(
    kRolloutsPerWorld ==
    kLearnedValueSearchRolloutsPerWorld);
static_assert(
    kBlendShallowPrior ==
    kLearnedValueSearchBlendsShallowPrior);

constexpr std::array<std::string_view, kDeckCount> kDeckNames = {
    "Green",
    "Red",
    "Blue",
    "White",
    "RU_Aggro",
};

constexpr std::string_view kEnvironment =
    "old-school-environment-v3-cleanup-discard";
constexpr std::string_view kArtifactFamily = "FQ4-DEV1";
constexpr std::string_view kParentCriticFingerprint =
    "2982b155a02a4a2a3ce8442ae28f6d8cf7829103e538c60f0625b3332502e568";
constexpr std::string_view kParentPriorityFingerprint =
    "32dc6688a5c970e3eda4325bea5ee419077027e160697899e3b00c963fa1bb22";
constexpr std::string_view kParentAttackFingerprint =
    "dfd3aaa16755bee5d0c2c40956851b94ef5676a271a602eb23a57719f7358b01";
constexpr std::string_view kParentBlockFingerprint =
    "d64e40796bd1587958b7386996e6a1e5660778d40ec7b40b0ee6324b8e39adbb";
constexpr std::string_view kParentDamageOrderFingerprint =
    "f0a84ed549bbf95197dd00c13ab04c0a4f6b1771f14bdb30a7dca937d2d79c76";
constexpr std::size_t kCorpusArtifactBytes = 2'250'909;
constexpr std::string_view kCorpusArtifactSha256 =
    "0911fc2eb8b14ddc9165543eb1e4c4edb0b058256a58dedf61f6c4ea4ca859df";
constexpr std::string_view kFitInputSha256 =
    "586b121c3c9bdb1a61305cac86882cd20b5d2ba332b4d5a54defc2c7756393a1";
constexpr std::size_t kPriorityHiddenCount = 32;
constexpr std::size_t kPriorityFeatureCount = 893;
constexpr std::size_t kPriorityParameterCount = 29'534;
constexpr LearnedValuePriorityHeadUpdateConfig kOptimizer{
    .batch_size = 64,
    .epochs = 16,
    .learning_rate = 0.001,
    .beta1 = 0.9,
    .beta2 = 0.999,
    .epsilon = 1.0e-8,
    .global_gradient_norm_clip = 5.0,
    .seed = 202607280212ULL,
    .residual_weight = 0.10,
    .policy_temperature = 0.10,
};

bool canonical_sha256(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(), [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

[[noreturn]] void fail_production_identity(
    std::string_view detail) {
    throw std::runtime_error(
        "FQ4 DEV1 production identity mismatch: " +
        std::string(detail));
}

void require_production_identity(
    bool condition, std::string_view detail) {
    if (!condition) {
        fail_production_identity(detail);
    }
}

bool parent_components_exact(
    const LearnedModelComponentFingerprints& components) {
    return components.critic ==
               kParentCriticFingerprint &&
           components.priority ==
               kParentPriorityFingerprint &&
           components.attack ==
               kParentAttackFingerprint &&
           components.block ==
               kParentBlockFingerprint &&
           components.damage_order ==
               kParentDamageOrderFingerprint;
}

bool production_contract_exact(
    const candidate_artifact::Contract& contract) {
    const auto& deployment = contract.deployment;
    return contract.family == kArtifactFamily &&
           contract.environment == kEnvironment &&
           contract.parent.artifact_bytes ==
               kParentArtifactBytes &&
           contract.parent.artifact_sha256 ==
               kParentArtifactSha256 &&
           contract.parent.model_fingerprint ==
               kParentModelFingerprint &&
           parent_components_exact(
               contract.parent.components) &&
           contract.parent.training_games ==
               kTrainingGames &&
           contract.parent.training_seed ==
               kTrainingSeed &&
           contract.parent.generation ==
               kParentGenerations &&
           contract.corpus.artifact_bytes ==
               kCorpusArtifactBytes &&
           contract.corpus.artifact_sha256 ==
               kCorpusArtifactSha256 &&
           contract.fit.input_sha256 ==
               kFitInputSha256 &&
           contract.fit.examples == 88 &&
           contract.fit.options == 548 &&
           contract.fit.check_examples == 0 &&
           contract.fit.background_only_examples == 0 &&
           contract.fit.optimizer_calls == 1 &&
           contract.fit.optimizer == kOptimizer &&
           contract.candidate_model_fingerprint ==
               kCandidateModelFingerprint &&
           contract.priority_hidden_count ==
               kPriorityHiddenCount &&
           contract.priority_feature_count ==
               kPriorityFeatureCount &&
           contract.priority_parameter_count ==
               kPriorityParameterCount &&
           deployment.variant ==
               LearnedVariant::ValueSearchChampion &&
           deployment.training_games == kTrainingGames &&
           deployment.worlds_per_action ==
               kWorldsPerAction &&
           deployment.horizon_turns == kHorizonTurns &&
           deployment.rollouts_per_world ==
               kRolloutsPerWorld &&
           deployment.root_search_depth ==
               kRootSearchDepth &&
           deployment.shallow_prior ==
               kBlendShallowPrior &&
           deployment.root_exploration ==
               kRootExploration &&
           deployment.continuation_epsilon ==
               kContinuationEpsilon &&
           deployment.priority_residual_weight ==
               kPriorityResidualWeight &&
           deployment.pass_dominance ==
               kPassDominance &&
           deployment.continuation_controller ==
               kContinuationController &&
           deployment.max_turns == kMaximumTurns;
}

bool only_priority_differs(
    const LearnedModelComponentFingerprints& parent,
    const LearnedModelComponentFingerprints& candidate) {
    return parent.critic == candidate.critic &&
           parent.attack == candidate.attack &&
           parent.block == candidate.block &&
           parent.damage_order ==
               candidate.damage_order &&
           parent.priority != candidate.priority;
}

bool same_bot(const BotConfig& left, const BotConfig& right) {
    return left.kind == right.kind &&
           left.learned_variant == right.learned_variant &&
           left.rollouts_per_action ==
               right.rollouts_per_action &&
           left.exploration_rate == right.exploration_rate &&
           left.value_continuation_epsilon ==
               right.value_continuation_epsilon &&
           left.value_priority_residual_weight ==
               right.value_priority_residual_weight &&
           left.value_pass_dominance ==
               right.value_pass_dominance &&
           left.value_continuation_controller ==
               right.value_continuation_controller &&
           left.training_games == right.training_games &&
           left.learned_model == right.learned_model;
}

bool valid_outcomes(const BotBenchmarkOutcomeCounts& outcomes,
                    std::size_t expected_games) {
    return outcomes.games == expected_games &&
           outcomes.wins + outcomes.losses + outcomes.draws ==
               outcomes.games;
}

bool valid_bot_stats(const BotSimulationStats& stats,
                     std::size_t expected_games) {
    return stats.games == expected_games &&
           stats.wins + stats.losses + stats.draws ==
               stats.games;
}

bool valid_deck_stats(const DeckSimulationStats& stats,
                      std::size_t expected_games) {
    return stats.games == expected_games &&
           stats.wins + stats.losses + stats.draws ==
               stats.games &&
           stats.on_play_games + stats.on_draw_games ==
               stats.games &&
           stats.on_play_games == expected_games / 2 &&
           stats.on_draw_games == expected_games / 2 &&
           stats.on_play_wins <= stats.on_play_games &&
           stats.on_draw_wins <= stats.on_draw_games;
}

bool benchmark_accounting_exact(
    const BotBenchmarkSummary& summary, const ModeSpec& spec,
    const BotConfig& expected_challenger,
    const BotConfig& expected_baseline,
    std::string_view expected_challenger_fingerprint,
    std::string_view expected_baseline_fingerprint) {
    if (spec.repetitions >
        std::numeric_limits<std::size_t>::max() / 60U) {
        return false;
    }
    const std::size_t expected_total =
        60U * spec.repetitions;
    const std::size_t expected_deck =
        12U * spec.repetitions;
    const std::size_t expected_quadrant =
        3U * spec.repetitions;
    if (summary.evaluation_seed != spec.evaluation_seed ||
        summary.learned_training_seed != kTrainingSeed ||
        summary.repetitions_per_deck_pairing !=
            spec.repetitions ||
        summary.total_games != expected_total ||
        !same_bot(summary.challenger,
                  expected_challenger) ||
        !same_bot(summary.baseline, expected_baseline) ||
        summary.challenger_model_fingerprint !=
            expected_challenger_fingerprint ||
        summary.baseline_model_fingerprint !=
            expected_baseline_fingerprint ||
        !valid_bot_stats(
            summary.challenger_stats, expected_total) ||
        !valid_bot_stats(
            summary.baseline_stats, expected_total) ||
        summary.challenger_stats.wins !=
            summary.baseline_stats.losses ||
        summary.challenger_stats.losses !=
            summary.baseline_stats.wins ||
        summary.challenger_stats.draws !=
            summary.baseline_stats.draws ||
        summary.challenger_quartet_cr1.clusters !=
            15U * spec.repetitions ||
        summary.challenger_quartet_cr1.records !=
            expected_total) {
        return false;
    }

    std::size_t challenger_deck_games = 0;
    std::size_t baseline_deck_games = 0;
    std::size_t challenger_deck_wins = 0;
    std::size_t challenger_deck_losses = 0;
    std::size_t challenger_deck_draws = 0;
    std::size_t baseline_deck_wins = 0;
    std::size_t baseline_deck_losses = 0;
    std::size_t baseline_deck_draws = 0;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        if (!valid_deck_stats(
                summary.challenger_decks[deck],
                expected_deck) ||
            !valid_deck_stats(
                summary.baseline_decks[deck],
                expected_deck)) {
            return false;
        }
        challenger_deck_games +=
            summary.challenger_decks[deck].games;
        baseline_deck_games +=
            summary.baseline_decks[deck].games;
        challenger_deck_wins +=
            summary.challenger_decks[deck].wins;
        challenger_deck_losses +=
            summary.challenger_decks[deck].losses;
        challenger_deck_draws +=
            summary.challenger_decks[deck].draws;
        baseline_deck_wins +=
            summary.baseline_decks[deck].wins;
        baseline_deck_losses +=
            summary.baseline_decks[deck].losses;
        baseline_deck_draws +=
            summary.baseline_decks[deck].draws;
        BotBenchmarkOutcomeCounts challenger_deck_outcomes;
        BotBenchmarkOutcomeCounts baseline_deck_outcomes;
        BotBenchmarkOutcomeCounts challenger_matchup_outcomes;
        std::array<std::size_t, 2> challenger_wins_by_play_draw{};
        std::array<std::size_t, 2> baseline_wins_by_play_draw{};
        std::array<std::size_t, 2>
            challenger_matchup_wins_by_play_draw{};
        for (std::size_t policy_seat = 0;
             policy_seat < 2; ++policy_seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                const auto& challenger =
                    summary.challenger_outcome_quadrants
                        [deck][policy_seat][play_draw];
                const auto& baseline =
                    summary.baseline_outcome_quadrants
                        [deck][policy_seat][play_draw];
                if (!valid_outcomes(
                        challenger, expected_quadrant) ||
                    !valid_outcomes(
                        baseline, expected_quadrant) ||
                    challenger.wins != baseline.losses ||
                    challenger.losses != baseline.wins ||
                    challenger.draws != baseline.draws) {
                    return false;
                }
                challenger_deck_outcomes.games +=
                    challenger.games;
                challenger_deck_outcomes.wins +=
                    challenger.wins;
                challenger_deck_outcomes.losses +=
                    challenger.losses;
                challenger_deck_outcomes.draws +=
                    challenger.draws;
                baseline_deck_outcomes.games += baseline.games;
                baseline_deck_outcomes.wins += baseline.wins;
                baseline_deck_outcomes.losses += baseline.losses;
                baseline_deck_outcomes.draws += baseline.draws;
                challenger_wins_by_play_draw[play_draw] +=
                    challenger.wins;
                baseline_wins_by_play_draw[play_draw] +=
                    baseline.wins;
            }
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const std::size_t expected_matchup_games =
                (deck == opponent ? 4U : 2U) *
                spec.repetitions;
            const auto& matchup =
                summary.challenger_deck_matchups
                    [deck][opponent];
            if (!valid_deck_stats(
                    matchup, expected_matchup_games)) {
                return false;
            }
            challenger_matchup_outcomes.games +=
                matchup.games;
            challenger_matchup_outcomes.wins +=
                matchup.wins;
            challenger_matchup_outcomes.losses +=
                matchup.losses;
            challenger_matchup_outcomes.draws +=
                matchup.draws;
            challenger_matchup_wins_by_play_draw[0] +=
                matchup.on_play_wins;
            challenger_matchup_wins_by_play_draw[1] +=
                matchup.on_draw_wins;
        }
        const auto& challenger_deck =
            summary.challenger_decks[deck];
        const auto& baseline_deck =
            summary.baseline_decks[deck];
        if (challenger_deck_outcomes.games !=
                challenger_deck.games ||
            challenger_deck_outcomes.wins !=
                challenger_deck.wins ||
            challenger_deck_outcomes.losses !=
                challenger_deck.losses ||
            challenger_deck_outcomes.draws !=
                challenger_deck.draws ||
            baseline_deck_outcomes.games !=
                baseline_deck.games ||
            baseline_deck_outcomes.wins !=
                baseline_deck.wins ||
            baseline_deck_outcomes.losses !=
                baseline_deck.losses ||
            baseline_deck_outcomes.draws !=
                baseline_deck.draws ||
            challenger_deck.wins !=
                baseline_deck.losses ||
            challenger_deck.losses !=
                baseline_deck.wins ||
            challenger_deck.draws !=
                baseline_deck.draws ||
            challenger_wins_by_play_draw[0] !=
                challenger_deck.on_play_wins ||
            challenger_wins_by_play_draw[1] !=
                challenger_deck.on_draw_wins ||
            baseline_wins_by_play_draw[0] !=
                baseline_deck.on_play_wins ||
            baseline_wins_by_play_draw[1] !=
                baseline_deck.on_draw_wins ||
            challenger_matchup_outcomes.games !=
                challenger_deck.games ||
            challenger_matchup_outcomes.wins !=
                challenger_deck.wins ||
            challenger_matchup_outcomes.losses !=
                challenger_deck.losses ||
            challenger_matchup_outcomes.draws !=
                challenger_deck.draws ||
            challenger_matchup_wins_by_play_draw[0] !=
                challenger_deck.on_play_wins ||
            challenger_matchup_wins_by_play_draw[1] !=
                challenger_deck.on_draw_wins) {
            return false;
        }
    }
    return challenger_deck_games == expected_total &&
           baseline_deck_games == expected_total &&
           challenger_deck_wins ==
               summary.challenger_stats.wins &&
           challenger_deck_losses ==
               summary.challenger_stats.losses &&
           challenger_deck_draws ==
               summary.challenger_stats.draws &&
           baseline_deck_wins ==
               summary.baseline_stats.wins &&
           baseline_deck_losses ==
               summary.baseline_stats.losses &&
           baseline_deck_draws ==
               summary.baseline_stats.draws;
}

bool fixed_deployment_identity(
    const FixedDeployment& deployment) {
    return deployment.fixed_identity_valid &&
           deployment.parent &&
           deployment.candidate &&
           deployment.parent != deployment.candidate &&
           deployment.parent_model_fingerprint ==
               kParentModelFingerprint &&
           deployment.candidate_model_fingerprint ==
               kCandidateModelFingerprint &&
           deployment.parent_components.critic ==
               deployment.candidate_components.critic &&
           deployment.parent_components.attack ==
               deployment.candidate_components.attack &&
           deployment.parent_components.block ==
               deployment.candidate_components.block &&
           deployment.parent_components.damage_order ==
               deployment.candidate_components.damage_order &&
           deployment.parent_components.priority !=
               deployment.candidate_components.priority &&
           deployment.parent_before.byte_size ==
               kParentArtifactBytes &&
           deployment.parent_before.sha256 ==
               kParentArtifactSha256 &&
           deployment.candidate_artifact_before.byte_size > 0 &&
           canonical_sha256(
               deployment.candidate_artifact_before.sha256) &&
           !deployment.candidate_artifact_family.empty() &&
           !deployment.candidate_artifact_recipe.empty();
}

void add_failure(
    GateReport& gate, bool condition, std::string message) {
    if (!condition) {
        gate.failures.push_back(std::move(message));
    }
}

std::string_view mode_name(Mode mode) {
    return mode_spec(mode).token;
}

std::string licensed_usage() {
    std::string result =
        "Usage: old-school-fq4-dev1-gameplay ";
    bool first = true;
    const auto append =
        [&](Mode mode, std::string_view token) {
            if (!mode_is_licensed(mode)) {
                return;
            }
            if (!first) {
                result.push_back('|');
            }
            result.append(token);
            first = false;
        };
    append(Mode::Smoke, "--smoke");
    append(Mode::MilestoneC16, "--milestone-c16");
    append(
        Mode::MilestoneHandcrafted,
        "--milestone-handcrafted");
    result.push_back('\n');
    return result;
}

std::string_view controller_name(
    LearnedContinuationController controller) {
    switch (controller) {
    case LearnedContinuationController::Legacy:
        return "Legacy";
    case LearnedContinuationController::PublicStackPassV1:
        return "PublicStackPassV1";
    }
    throw std::invalid_argument(
        "unknown continuation controller");
}

void format_snapshot(
    std::ostringstream& output, std::string_view role,
    std::string_view when,
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    output << "artifact role=" << role
           << " when=" << when
           << " path=" << snapshot.path
           << " bytes=" << snapshot.byte_size
           << " sha256=" << snapshot.sha256 << '\n';
}

void format_policy(
    std::ostringstream& output, std::string_view role,
    std::string_view model_fingerprint,
    std::string_view priority_fingerprint) {
    output
        << "policy role=" << role
        << " variant=ValueSearchChampion"
        << " model_fingerprint=" << model_fingerprint
        << " priority_fingerprint=" << priority_fingerprint
        << " training_games=" << kTrainingGames
        << " worlds_per_action=" << kWorldsPerAction
        << " rollouts_per_world=" << kRolloutsPerWorld
        << " horizon_turns=" << kHorizonTurns
        << " shallow_prior="
        << (kBlendShallowPrior ? 1 : 0)
        << " root_exploration=" << kRootExploration
        << " continuation_epsilon=" << kContinuationEpsilon
        << " priority_residual_weight="
        << kPriorityResidualWeight
        << " pass_dominance="
        << (kPassDominance ? 1 : 0)
        << " continuation_controller="
        << controller_name(kContinuationController)
        << " max_turns=" << kMaximumTurns
        << " root_search_depth=" << kRootSearchDepth
        << '\n';
}

void format_benchmark(
    std::ostringstream& output, std::string_view role,
    const TimedBenchmark& timed) {
    const BotBenchmarkSummary& summary = timed.summary;
    output
        << "benchmark role=" << role
        << " seed=" << summary.evaluation_seed
        << " repetitions="
        << summary.repetitions_per_deck_pairing
        << " total_games=" << summary.total_games
        << " elapsed_seconds=" << timed.elapsed_seconds
        << '\n'
        << "overall role=" << role
        << " wins=" << summary.challenger_stats.wins
        << " losses=" << summary.challenger_stats.losses
        << " draws=" << summary.challenger_stats.draws
        << " win_rate=" << summary.challenger_win_rate()
        << " wilson_low_95="
        << summary.confidence_low_95()
        << " wilson_high_95="
        << summary.confidence_high_95() << '\n';
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        const auto& challenger =
            summary.challenger_decks[deck];
        const auto& baseline =
            summary.baseline_decks[deck];
        output
            << "deck role=" << role
            << " deck=" << kDeckNames[deck]
            << " challenger=" << challenger.wins << '-'
            << challenger.losses << '-' << challenger.draws
            << " baseline=" << baseline.wins << '-'
            << baseline.losses << '-' << baseline.draws
            << " games=" << challenger.games << '\n';
        for (std::size_t policy_seat = 0;
             policy_seat < 2; ++policy_seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                const auto& challenger_quadrant =
                    summary.challenger_outcome_quadrants
                        [deck][policy_seat][play_draw];
                const auto& baseline_quadrant =
                    summary.baseline_outcome_quadrants
                        [deck][policy_seat][play_draw];
                output
                    << "quadrant role=" << role
                    << " deck=" << kDeckNames[deck]
                    << " policy_seat=" << policy_seat
                    << " play_draw="
                    << (play_draw == 0 ? "play" : "draw")
                    << " challenger="
                    << challenger_quadrant.wins << '-'
                    << challenger_quadrant.losses << '-'
                    << challenger_quadrant.draws
                    << " challenger_games="
                    << challenger_quadrant.games
                    << " baseline="
                    << baseline_quadrant.wins << '-'
                    << baseline_quadrant.losses << '-'
                    << baseline_quadrant.draws
                    << " baseline_games="
                    << baseline_quadrant.games << '\n';
            }
        }
    }
}

TimedBenchmark execute_production_benchmark(
    const BenchmarkRequest& request) {
    const auto started = std::chrono::steady_clock::now();
    BotBenchmarkSummary summary = run_bot_benchmark(
        request.repetitions, request.evaluation_seed,
        request.challenger, request.baseline, request.game,
        request.allow_identical_policy_control);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    return {
        .summary = std::move(summary),
        .elapsed_seconds = elapsed.count(),
    };
}

FixedDeployment load_production_deployment();

} // namespace

ModeSpec mode_spec(Mode mode) {
    switch (mode) {
    case Mode::Smoke:
        return {
            .mode = mode,
            .token = "smoke",
            .evaluation_seed = kSmokeSeed,
            .repetitions = kSmokeRepetitions,
            .run_identical_control = true,
            .handcrafted_baseline = false,
        };
    case Mode::MilestoneC16:
        return {
            .mode = mode,
            .token = "milestone-c16",
            .evaluation_seed = kMilestoneC16Seed,
            .repetitions = kMilestoneRepetitions,
            .run_identical_control = false,
            .handcrafted_baseline = false,
        };
    case Mode::MilestoneHandcrafted:
        return {
            .mode = mode,
            .token = "milestone-handcrafted",
            .evaluation_seed = kMilestoneHandcraftedSeed,
            .repetitions = kMilestoneRepetitions,
            .run_identical_control = false,
            .handcrafted_baseline = true,
        };
    }
    throw std::invalid_argument("unknown FQ4 DEV1 gameplay mode");
}

Mode parse_mode(
    std::span<const std::string_view> arguments) {
    if (arguments.size() != 1) {
        throw std::invalid_argument(
            "exactly one fixed gameplay mode is required");
    }
    Mode mode = Mode::Smoke;
    if (arguments.front() == "--smoke") {
        mode = Mode::Smoke;
    } else if (arguments.front() == "--milestone-c16") {
        mode = Mode::MilestoneC16;
    } else if (
        arguments.front() ==
        "--milestone-handcrafted") {
        mode = Mode::MilestoneHandcrafted;
    } else {
        throw std::invalid_argument(
            "unknown fixed FQ4 DEV1 gameplay mode");
    }
    if (!mode_is_licensed(mode)) {
        throw std::invalid_argument(
            "fixed FQ4 DEV1 gameplay mode is not licensed");
    }
    return mode;
}

namespace testing {

BotConfig make_learned_bot(
    std::shared_ptr<const LearnedModel> model) {
    return {
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = kWorldsPerAction,
        .exploration_rate = kRootExploration,
        .value_continuation_epsilon =
            kContinuationEpsilon,
        .value_priority_residual_weight =
            kPriorityResidualWeight,
        .value_pass_dominance = kPassDominance,
        .value_continuation_controller =
            kContinuationController,
        .training_games = kTrainingGames,
        .learned_model = std::move(model),
    };
}

BotConfig make_handcrafted_bot() {
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
        .training_games = kTrainingGames,
        .learned_model = {},
    };
}

GameConfig make_game_config() {
    GameConfig result;
    result.max_turns = kMaximumTurns;
    result.learned_training_seed = kTrainingSeed;
    result.learned_search_depth = kRootSearchDepth;
    return result;
}

GateReport evaluate_gate(const RunReport& report) {
    GateReport gate;
    gate.fixed_identity =
        fixed_deployment_identity(report.deployment);
    gate.artifacts_unchanged =
        report.deployment.parent_before ==
            report.parent_after &&
        report.deployment.candidate_artifact_before ==
            report.candidate_artifact_after;
    gate.models_unchanged =
        report.parent_model_after.fingerprint ==
            report.deployment.parent_model_fingerprint &&
        report.parent_model_after.components ==
            report.deployment.parent_components &&
        report.candidate_model_after.fingerprint ==
            report.deployment.candidate_model_fingerprint &&
        report.candidate_model_after.components ==
            report.deployment.candidate_components;
    gate.requests_exact = report.request_contract_exact;

    const BotConfig treatment = make_learned_bot(
        report.deployment.candidate);
    const BotConfig control = make_learned_bot(
        report.deployment.parent);
    const BotConfig baseline =
        report.spec.handcrafted_baseline
            ? make_handcrafted_bot()
            : control;
    const std::string_view baseline_fingerprint =
        report.spec.handcrafted_baseline
            ? std::string_view{}
            : std::string_view(
                  report.deployment
                      .parent_model_fingerprint);
    gate.accounting_complete =
        benchmark_accounting_exact(
            report.candidate.summary, report.spec,
            treatment, baseline,
            report.deployment.candidate_model_fingerprint,
            baseline_fingerprint);
    if (report.spec.run_identical_control) {
        gate.accounting_complete =
            gate.accounting_complete &&
            report.identical_control.has_value() &&
            benchmark_accounting_exact(
                report.identical_control->summary,
                report.spec, control, control,
                report.deployment.parent_model_fingerprint,
                report.deployment.parent_model_fingerprint);
        gate.control_exact =
            report.identical_control.has_value() &&
            report.identical_control->summary
                    .challenger_stats.wins == 120 &&
            report.identical_control->summary
                    .challenger_stats.losses == 120 &&
            report.identical_control->summary
                    .challenger_stats.draws == 0 &&
            report.identical_control->summary
                    .baseline_stats.wins == 120 &&
            report.identical_control->summary
                    .baseline_stats.losses == 120 &&
            report.identical_control->summary
                    .baseline_stats.draws == 0;
    }

    if (report.spec.mode == Mode::Smoke) {
        gate.aggregate_floor =
            report.candidate.summary.challenger_win_rate() >=
            40.0;
        gate.runtime_ratio =
            report.identical_control.has_value() &&
            std::isfinite(
                report.identical_control->elapsed_seconds) &&
            report.identical_control->elapsed_seconds > 0.0 &&
            std::isfinite(report.candidate.elapsed_seconds) &&
            report.candidate.elapsed_seconds > 0.0 &&
            report.candidate.elapsed_seconds <=
                1.25 *
                    report.identical_control
                        ->elapsed_seconds;
    } else {
        gate.aggregate_majority =
            report.candidate.summary.challenger_stats.wins >
            report.candidate.summary.challenger_stats.losses;
        gate.aggregate_wilson =
            report.candidate.summary.confidence_low_95() >
            50.0;
        gate.every_deck_majority =
            std::all_of(
                report.candidate.summary
                    .challenger_decks.begin(),
                report.candidate.summary
                    .challenger_decks.end(),
                [](const DeckSimulationStats& deck) {
                    return deck.wins > deck.losses;
                });
    }

    gate.infrastructure_valid =
        gate.fixed_identity &&
        gate.artifacts_unchanged &&
        gate.models_unchanged &&
        gate.requests_exact &&
        gate.accounting_complete &&
        gate.control_exact;
    gate.passed =
        gate.infrastructure_valid &&
        gate.aggregate_floor &&
        gate.runtime_ratio &&
        gate.aggregate_majority &&
        gate.aggregate_wilson &&
        gate.every_deck_majority;

    add_failure(
        gate, gate.fixed_identity,
        "fixed parent/candidate identity is invalid");
    add_failure(
        gate, gate.artifacts_unchanged,
        "an immutable artifact changed during gameplay");
    add_failure(
        gate, gate.models_unchanged,
        "an immutable model changed during gameplay");
    add_failure(
        gate, gate.requests_exact,
        "a benchmark request differs from the fixed recipe");
    add_failure(
        gate, gate.accounting_complete,
        "paired benchmark accounting is incomplete or unbalanced");
    add_failure(
        gate, gate.control_exact,
        "identical C16 control is not exactly 120-120");
    add_failure(
        gate, gate.aggregate_floor,
        "smoke candidate win rate is below 40 percent");
    add_failure(
        gate, gate.runtime_ratio,
        "smoke candidate runtime exceeds 1.25 times control");
    add_failure(
        gate, gate.aggregate_majority,
        "milestone candidate lacks an aggregate majority");
    add_failure(
        gate, gate.aggregate_wilson,
        "milestone Wilson lower bound does not exceed 50 percent");
    add_failure(
        gate, gate.every_deck_majority,
        "milestone candidate lacks a majority on every deck");
    return gate;
}

RunReport run_with(
    Mode mode, FixedDeployment deployment,
    const BenchmarkExecutor& benchmark,
    const Snapshotter& snapshot,
    const ModelInspector& inspect_model) {
    if (!benchmark || !snapshot || !inspect_model) {
        throw std::invalid_argument(
            "FQ4 DEV1 gameplay requires every execution seam");
    }
    const ModeSpec spec = mode_spec(mode);
    const BotConfig control =
        make_learned_bot(deployment.parent);
    const BotConfig treatment =
        make_learned_bot(deployment.candidate);
    const BotConfig baseline =
        spec.handcrafted_baseline
            ? make_handcrafted_bot()
            : control;
    const GameConfig game = make_game_config();

    RunReport report{
        .spec = spec,
        .deployment = std::move(deployment),
        .request_contract_exact = true,
    };
    if (spec.run_identical_control) {
        report.identical_control = benchmark({
            .role = "identical-control",
            .repetitions = spec.repetitions,
            .evaluation_seed = spec.evaluation_seed,
            .challenger = control,
            .baseline = control,
            .game = game,
            .allow_identical_policy_control = true,
        });
        const ModelIdentity parent_after_control =
            inspect_model(report.deployment.parent);
        const ModelIdentity candidate_after_control =
            inspect_model(report.deployment.candidate);
        if (parent_after_control.fingerprint !=
                report.deployment
                    .parent_model_fingerprint ||
            parent_after_control.components !=
                report.deployment.parent_components ||
            candidate_after_control.fingerprint !=
                report.deployment
                    .candidate_model_fingerprint ||
            candidate_after_control.components !=
                report.deployment.candidate_components) {
            throw std::runtime_error(
                "FQ4 DEV1 model changed during its "
                "identical control");
        }
        const auto parent_artifact_after_control =
            snapshot(
                report.deployment.parent_before.path);
        const auto candidate_artifact_after_control =
            snapshot(
                report.deployment
                    .candidate_artifact_before.path);
        if (parent_artifact_after_control !=
                report.deployment.parent_before ||
            candidate_artifact_after_control !=
                report.deployment
                    .candidate_artifact_before) {
            throw std::runtime_error(
                "FQ4 DEV1 artifact changed during its "
                "identical control");
        }
    }
    report.candidate = benchmark({
        .role = "candidate",
        .repetitions = spec.repetitions,
        .evaluation_seed = spec.evaluation_seed,
        .challenger = treatment,
        .baseline = baseline,
        .game = game,
        .allow_identical_policy_control = false,
    });
    report.parent_model_after =
        inspect_model(report.deployment.parent);
    report.candidate_model_after =
        inspect_model(report.deployment.candidate);
    report.parent_after = snapshot(
        report.deployment.parent_before.path);
    report.candidate_artifact_after = snapshot(
        report.deployment
            .candidate_artifact_before.path);
    report.gate = evaluate_gate(report);
    return report;
}

RunReport run_licensed_with(
    Mode mode, const DeploymentLoader& load_deployment,
    const BenchmarkExecutor& benchmark,
    const Snapshotter& snapshot,
    const ModelInspector& inspect_model) {
    if (!mode_is_licensed(mode)) {
        throw std::invalid_argument(
            "FQ4 DEV1 gameplay mode is not licensed");
    }
    if (!load_deployment) {
        throw std::invalid_argument(
            "FQ4 DEV1 gameplay requires a deployment loader");
    }
    return run_with(
        mode, load_deployment(), benchmark, snapshot,
        inspect_model);
}

} // namespace testing

std::string format_report(const RunReport& report) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17)
           << "schema=" << kReportSchema
           << " mode=" << mode_name(report.spec.mode)
           << " artifact_family="
           << report.deployment.candidate_artifact_family
           << " artifact_recipe="
           << report.deployment.candidate_artifact_recipe
           << '\n';
    format_snapshot(
        output, "c16-parent", "before",
        report.deployment.parent_before);
    format_snapshot(
        output, "fq4-dev1-candidate", "before",
        report.deployment.candidate_artifact_before);
    format_snapshot(
        output, "c16-parent", "after",
        report.parent_after);
    format_snapshot(
        output, "fq4-dev1-candidate", "after",
        report.candidate_artifact_after);
    format_policy(
        output, "control",
        report.deployment.parent_model_fingerprint,
        report.deployment.parent_components.priority);
    format_policy(
        output, "treatment",
        report.deployment.candidate_model_fingerprint,
        report.deployment.candidate_components.priority);
    output
        << "model_identity role=control when=after"
        << " model_fingerprint="
        << report.parent_model_after.fingerprint
        << " priority_fingerprint="
        << report.parent_model_after.components.priority
        << '\n'
        << "model_identity role=treatment when=after"
        << " model_fingerprint="
        << report.candidate_model_after.fingerprint
        << " priority_fingerprint="
        << report.candidate_model_after.components.priority
        << '\n';
    if (report.identical_control.has_value()) {
        format_benchmark(
            output, "identical-control",
            *report.identical_control);
    }
    format_benchmark(output, "candidate", report.candidate);
    output
        << "gate fixed_identity="
        << (report.gate.fixed_identity ? 1 : 0)
        << " artifacts_unchanged="
        << (report.gate.artifacts_unchanged ? 1 : 0)
        << " models_unchanged="
        << (report.gate.models_unchanged ? 1 : 0)
        << " requests_exact="
        << (report.gate.requests_exact ? 1 : 0)
        << " accounting_complete="
        << (report.gate.accounting_complete ? 1 : 0)
        << " control_exact="
        << (report.gate.control_exact ? 1 : 0)
        << " aggregate_floor="
        << (report.gate.aggregate_floor ? 1 : 0)
        << " runtime_ratio="
        << (report.gate.runtime_ratio ? 1 : 0)
        << " aggregate_majority="
        << (report.gate.aggregate_majority ? 1 : 0)
        << " aggregate_wilson="
        << (report.gate.aggregate_wilson ? 1 : 0)
        << " every_deck_majority="
        << (report.gate.every_deck_majority ? 1 : 0)
        << " infrastructure_valid="
        << (report.gate.infrastructure_valid ? 1 : 0)
        << '\n';
    for (const std::string& failure : report.gate.failures) {
        output << "failure=" << failure << '\n';
    }
    output << "result="
           << (report.gate.passed
                   ? "PASS"
                   : report.gate.infrastructure_valid
                         ? "REJECT"
                         : "ERROR")
           << '\n';
    return output.str();
}

RunReport run_fixed_mode(Mode mode) {
    return testing::run_licensed_with(
        mode, load_production_deployment,
        execute_production_benchmark,
        [](const std::string& path) {
            return artifact_integrity::
                snapshot_regular_file(path);
        },
        [](std::shared_ptr<const LearnedModel> model) {
            return ModelIdentity{
                .fingerprint =
                    learned_model_fingerprint(model),
                .components =
                    learned_model_component_fingerprints(
                        model),
            };
        });
}

int run_cli(int argc, char* argv[], std::ostream& output,
            std::ostream& error) {
    const std::string usage = licensed_usage();
    if (argc != 2) {
        error << usage;
        return 2;
    }
    Mode mode = Mode::Smoke;
    try {
        const std::array<std::string_view, 1> arguments = {
            argv[1],
        };
        mode = parse_mode(arguments);
    } catch (const std::invalid_argument&) {
        error << usage;
        return 2;
    }
    try {
        const RunReport report = run_fixed_mode(mode);
        output << format_report(report);
        output.flush();
        if (!output.good()) {
            return 2;
        }
        if (!report.gate.infrastructure_valid) {
            return 2;
        }
        return report.gate.passed ? 0 : 1;
    } catch (const std::exception&) {
        error << "result=ERROR"
                 " reason=fixed_fq4_dev1_gameplay_failed\n";
        return 2;
    }
}

namespace {

FixedDeployment load_production_deployment() {
    require_production_identity(
        kCandidateArtifactBytes > 0 &&
            canonical_sha256(
                kCandidateArtifactSha256),
        "candidate artifact identity is not pinned");

    const candidate_artifact::Contract& contract =
        candidate_artifact::production_contract();
    require_production_identity(
        production_contract_exact(contract),
        "candidate production contract drifted");
    require_production_identity(
        candidate_artifact::production_artifact_path() ==
            candidate_artifact::kProductionArtifactPath,
        "candidate production path drifted");

    const auto parent_before =
        artifact_integrity::snapshot_regular_file(
            kParentArtifactPath);
    require_production_identity(
        parent_before.byte_size ==
                kParentArtifactBytes &&
            parent_before.sha256 ==
                kParentArtifactSha256,
        "C16 artifact identity differs from the frozen parent");

    const LearnedValueChallengerArtifact parent_artifact =
        load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            kTrainingGames, kTrainingSeed,
            kParentGenerations);
    const std::shared_ptr<const LearnedModel> parent =
        parent_artifact.model();
    require_production_identity(
        parent &&
            parent_artifact.training_games() ==
                kTrainingGames &&
            parent_artifact.seed() == kTrainingSeed &&
            parent_artifact.self_play_generations() ==
                kParentGenerations,
        "C16 model metadata differs from the frozen parent");
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const LearnedModelComponentFingerprints parent_components =
        learned_model_component_fingerprints(parent);
    require_production_identity(
        parent_fingerprint == kParentModelFingerprint &&
            parent_components_exact(parent_components) &&
            parent_fingerprint ==
                contract.parent.model_fingerprint &&
            parent_components ==
                contract.parent.components,
        "loaded C16 model identity differs from its contract");

    const std::filesystem::path candidate_path =
        candidate_artifact::production_artifact_path();
    const auto candidate_before =
        artifact_integrity::snapshot_regular_file(
            candidate_path);
    require_production_identity(
        candidate_before.byte_size ==
                kCandidateArtifactBytes &&
            candidate_before.sha256 ==
                kCandidateArtifactSha256,
        "candidate artifact identity differs from its pin");
    const candidate_artifact::FileIdentity
        expected_candidate_artifact{
            .bytes = kCandidateArtifactBytes,
            .sha256 =
                std::string(kCandidateArtifactSha256),
        };
    const candidate_artifact::LoadedCandidate loaded =
        candidate_artifact::load(
            candidate_path, parent, contract,
            expected_candidate_artifact);
    const std::shared_ptr<const LearnedModel> candidate =
        loaded.model();
    const std::string candidate_fingerprint =
        learned_model_fingerprint(candidate);
    const LearnedModelComponentFingerprints
        candidate_components =
            learned_model_component_fingerprints(
                candidate);
    require_production_identity(
        candidate &&
            candidate != parent &&
            loaded.report().artifact ==
                expected_candidate_artifact &&
            loaded.report().manifest ==
                loaded.manifest() &&
            loaded.manifest().contract == contract &&
            loaded.manifest().candidate_components ==
                candidate_components &&
            candidate_fingerprint ==
                kCandidateModelFingerprint &&
            candidate_fingerprint ==
                contract.candidate_model_fingerprint &&
            only_priority_differs(
                parent_components,
                candidate_components),
        "reconstructed candidate identity or isolation differs");

    return {
        .parent = parent,
        .candidate = candidate,
        .parent_model_fingerprint =
            parent_fingerprint,
        .candidate_model_fingerprint =
            candidate_fingerprint,
        .parent_components = parent_components,
        .candidate_components = candidate_components,
        .parent_before = parent_before,
        .candidate_artifact_before = candidate_before,
        .candidate_artifact_family =
            contract.family,
        .candidate_artifact_recipe =
            std::string(candidate_artifact::kSchema),
        .fixed_identity_valid = true,
    };
}

} // namespace

} // namespace old_school::fq4_dev1_gameplay
