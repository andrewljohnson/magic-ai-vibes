#include "old_school/fq4_dev5_gameplay.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace old_school::fq4_dev5_gameplay {
namespace {

namespace artifact = fq4_dev_candidate_artifact;
namespace integrity = artifact_integrity;

static_assert(kHorizonTurns ==
              kLearnedValueSearchHorizonTurns);
static_assert(kRolloutsPerWorld ==
              kLearnedValueSearchRolloutsPerWorld);
static_assert(kBlendShallowPrior ==
              kLearnedValueSearchBlendsShallowPrior);
static_assert(kSmokeGames == 240);
static_assert(kSmokeGamesPerDeck == 48);

constexpr std::array<std::string_view, kDeckCount>
    kDeckNames{
        "Green",
        "Red",
        "Blue",
        "White",
        "RU_Aggro",
    };
constexpr std::string_view kFamily =
    "FQ4-DEV5-NEUTRAL-ANCHORED";
constexpr std::string_view kEnvironment =
    "old-school-environment-v3-cleanup-discard;"
    "dev1-bytes=2250909;"
    "dev1-sha256="
    "0911fc2eb8b14ddc9165543eb1e4c4ed"
    "b0b058256a58dedf61f6c4ea4ca859df;"
    "neutral-bytes=661475;"
    "neutral-sha256="
    "47d94823f043971f6f9f0aa5f552bfae"
    "210af9615d8f6dc7392e52dad3eaa105";
static_assert(kEnvironment.size() == 238);
constexpr std::string_view kCorpusArtifactSha256 =
    "0911fc2eb8b14ddc9165543eb1e4c4ed"
    "b0b058256a58dedf61f6c4ea4ca859df";
constexpr std::string_view kFitInputSha256 =
    "a13c2bca589a42d020fcb7abfa1826fa"
    "e5a9745be41602442fa7e7bc1d768fef";
constexpr std::string_view kParentCriticFingerprint =
    "2982b155a02a4a2a3ce8442ae28f6d8c"
    "f7829103e538c60f0625b3332502e568";
constexpr std::string_view kParentPriorityFingerprint =
    "32dc6688a5c970e3eda4325bea5ee4190"
    "77027e160697899e3b00c963fa1bb22";
constexpr std::string_view kParentAttackFingerprint =
    "dfd3aaa16755bee5d0c2c40956851b94"
    "ef5676a271a602eb23a57719f7358b01";
constexpr std::string_view kParentBlockFingerprint =
    "d64e40796bd1587958b7386996e6a1e5"
    "660778d40ec7b40b0ee6324b8e39adbb";
constexpr std::string_view kParentDamageOrderFingerprint =
    "f0a84ed549bbf95197dd00c13ab04c0a"
    "4f6b1771f14bdb30a7dca937d2d79c76";
constexpr std::string_view kCandidatePriorityFingerprint =
    "e279267435b9644d42b66c0b2cb917b8"
    "6b1b8c3fceacae65a4f3cd565ddb6732";
constexpr std::string_view kTensorParentSha256 =
    "4593663a4c2512ca9996d08c64dd28de"
    "217e430ea691174af2168d11d066e4ee";
constexpr std::string_view kTensorCandidateSha256 =
    "77cf99a4ff1f9d460f6b80294f91a01d"
    "846a3c6515de40a5106e5398e688f769";
constexpr std::string_view kTensorXorSha256 =
    "23dd5dec0944b855fc523ef607aee6fac"
    "3d5ae3a59b866f7aa40f446efa8b297";
constexpr std::uint32_t kPriorityHiddenCount = 32;
constexpr std::uint32_t kPriorityFeatureCount = 893;
constexpr std::uint64_t kPriorityParameterCount = 29'534;

constexpr LearnedValuePriorityHeadUpdateConfig
    kFrozenOptimizer{
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

[[noreturn]] void fail(std::string_view category) {
    throw std::runtime_error(
        "FQ4 DEV5 gameplay validation failed: " +
        std::string(category));
}

void require(bool condition, std::string_view category) {
    if (!condition) {
        fail(category);
    }
}

bool same_real(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool same_optimizer(
    const LearnedValuePriorityHeadUpdateConfig& first,
    const LearnedValuePriorityHeadUpdateConfig& second) {
    return first.batch_size == second.batch_size &&
           first.epochs == second.epochs &&
           same_real(first.learning_rate,
                     second.learning_rate) &&
           same_real(first.beta1, second.beta1) &&
           same_real(first.beta2, second.beta2) &&
           same_real(first.epsilon, second.epsilon) &&
           same_real(first.global_gradient_norm_clip,
                     second.global_gradient_norm_clip) &&
           first.seed == second.seed &&
           same_real(first.residual_weight,
                     second.residual_weight) &&
           same_real(first.policy_temperature,
                     second.policy_temperature);
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

bool candidate_components_exact(
    const LearnedModelComponentFingerprints& components) {
    return components.critic ==
               kParentCriticFingerprint &&
           components.priority ==
               kCandidatePriorityFingerprint &&
           components.attack ==
               kParentAttackFingerprint &&
           components.block ==
               kParentBlockFingerprint &&
           components.damage_order ==
               kParentDamageOrderFingerprint;
}

bool only_priority_differs(
    const LearnedModelComponentFingerprints& parent,
    const LearnedModelComponentFingerprints& candidate) {
    return parent.critic == candidate.critic &&
           parent.attack == candidate.attack &&
           parent.block == candidate.block &&
           parent.damage_order == candidate.damage_order &&
           parent.priority != candidate.priority;
}

bool contract_exact(const artifact::Contract& contract) {
    const auto& parent = contract.parent;
    const auto& fit = contract.fit;
    const auto& deployment = contract.deployment;
    return contract.family == kFamily &&
           contract.environment == kEnvironment &&
           parent.artifact_bytes == kParentArtifactBytes &&
           parent.artifact_sha256 ==
               kParentArtifactSha256 &&
           parent.model_fingerprint ==
               kParentModelFingerprint &&
           parent_components_exact(parent.components) &&
           parent.training_games == kTrainingGames &&
           parent.training_seed == kTrainingSeed &&
           parent.generation == kParentGenerations &&
           contract.corpus.artifact_bytes == 2'250'909 &&
           contract.corpus.artifact_sha256 ==
               kCorpusArtifactSha256 &&
           fit.input_sha256 == kFitInputSha256 &&
           fit.examples == 248 &&
           fit.options == 987 &&
           fit.check_examples == 0 &&
           fit.background_only_examples == 0 &&
           fit.optimizer_calls == 1 &&
           same_optimizer(fit.optimizer,
                          kFrozenOptimizer) &&
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
           same_real(deployment.root_exploration,
                     kRootExploration) &&
           same_real(deployment.continuation_epsilon,
                     kContinuationEpsilon) &&
           same_real(
               deployment.priority_residual_weight,
               kPriorityResidualWeight) &&
           deployment.pass_dominance == kPassDominance &&
           deployment.continuation_controller ==
               kContinuationController &&
           deployment.max_turns == kMaximumTurns;
}

artifact::FileIdentity expected_candidate_file() {
    return {
        .bytes = kCandidateArtifactBytes,
        .sha256 =
            std::string(kCandidateArtifactSha256),
    };
}

bool candidate_report_exact(
    const artifact::Report& report) {
    const auto& manifest = report.manifest;
    return report.artifact ==
               expected_candidate_file() &&
           contract_exact(manifest.contract) &&
           manifest.candidate_components ==
               LearnedModelComponentFingerprints{
                   .critic = std::string(
                       kParentCriticFingerprint),
                   .priority = std::string(
                       kCandidatePriorityFingerprint),
                   .attack = std::string(
                       kParentAttackFingerprint),
                   .block = std::string(
                       kParentBlockFingerprint),
                   .damage_order = std::string(
                       kParentDamageOrderFingerprint),
               } &&
           manifest.tensors.hidden_count ==
               kPriorityHiddenCount &&
           manifest.tensors.feature_count ==
               kPriorityFeatureCount &&
           manifest.tensors.parameter_count ==
               kPriorityParameterCount &&
           manifest.tensors.parent_sha256 ==
               kTensorParentSha256 &&
           manifest.tensors.candidate_sha256 ==
               kTensorCandidateSha256 &&
           manifest.tensors.xor_delta_sha256 ==
               kTensorXorSha256;
}

bool snapshot_has_identity(
    const integrity::RegularFileSnapshot& snapshot,
    std::uint64_t bytes, std::string_view sha256) {
    return snapshot.byte_size == bytes &&
           snapshot.sha256 == sha256 &&
           !snapshot.path.empty() &&
           !snapshot.physical_path.empty() &&
           snapshot.link_count == 1;
}

bool fixed_deployment_exact(
    const FixedDeployment& deployment) {
    return deployment.fixed_identity_valid &&
           deployment.parent &&
           deployment.candidate &&
           deployment.parent != deployment.candidate &&
           deployment.parent_model_fingerprint ==
               kParentModelFingerprint &&
           deployment.candidate_model_fingerprint ==
               kCandidateModelFingerprint &&
           parent_components_exact(
               deployment.parent_components) &&
           candidate_components_exact(
               deployment.candidate_components) &&
           only_priority_differs(
               deployment.parent_components,
               deployment.candidate_components) &&
           snapshot_has_identity(
               deployment.parent_before,
               kParentArtifactBytes,
               kParentArtifactSha256) &&
           snapshot_has_identity(
               deployment.candidate_artifact_before,
               kCandidateArtifactBytes,
               kCandidateArtifactSha256) &&
           candidate_report_exact(
               deployment.candidate_report);
}

bool same_bot(
    const BotConfig& first, const BotConfig& second) {
    return first.kind == second.kind &&
           first.learned_variant ==
               second.learned_variant &&
           first.rollouts_per_action ==
               second.rollouts_per_action &&
           same_real(first.exploration_rate,
                     second.exploration_rate) &&
           same_real(
               first.value_continuation_epsilon,
               second.value_continuation_epsilon) &&
           same_real(
               first.value_priority_residual_weight,
               second.value_priority_residual_weight) &&
           first.value_pass_dominance ==
               second.value_pass_dominance &&
           first.value_adversarial_blocks ==
               second.value_adversarial_blocks &&
           first.value_continuation_controller ==
               second.value_continuation_controller &&
           first.training_games == second.training_games &&
           first.learned_model == second.learned_model;
}

bool valid_outcomes(
    const BotBenchmarkOutcomeCounts& outcomes,
    std::size_t expected_games) {
    return outcomes.games == expected_games &&
           outcomes.wins + outcomes.losses +
                   outcomes.draws ==
               outcomes.games;
}

bool valid_bot_stats(
    const BotSimulationStats& stats,
    std::size_t expected_games) {
    return stats.games == expected_games &&
           stats.wins + stats.losses + stats.draws ==
               stats.games;
}

bool valid_deck_stats(
    const DeckSimulationStats& stats,
    std::size_t expected_games) {
    return stats.games == expected_games &&
           stats.wins + stats.losses + stats.draws ==
               stats.games &&
           stats.on_play_games + stats.on_draw_games ==
               stats.games &&
           stats.on_play_games == expected_games / 2 &&
           stats.on_draw_games == expected_games / 2 &&
           stats.on_play_wins + stats.on_draw_wins ==
               stats.wins &&
           stats.on_play_wins <= stats.on_play_games &&
           stats.on_draw_wins <= stats.on_draw_games;
}

struct OutcomeAccumulator {
    std::size_t games = 0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
    std::size_t on_play_games = 0;
    std::size_t on_play_wins = 0;
    std::size_t on_draw_games = 0;
    std::size_t on_draw_wins = 0;
};

void add_outcomes(
    OutcomeAccumulator& total,
    const BotBenchmarkOutcomeCounts& outcomes,
    std::size_t play_draw) {
    total.games += outcomes.games;
    total.wins += outcomes.wins;
    total.losses += outcomes.losses;
    total.draws += outcomes.draws;
    if (play_draw == 0) {
        total.on_play_games += outcomes.games;
        total.on_play_wins += outcomes.wins;
    } else {
        total.on_draw_games += outcomes.games;
        total.on_draw_wins += outcomes.wins;
    }
}

void add_deck(
    OutcomeAccumulator& total,
    const DeckSimulationStats& stats) {
    total.games += stats.games;
    total.wins += stats.wins;
    total.losses += stats.losses;
    total.draws += stats.draws;
    total.on_play_games += stats.on_play_games;
    total.on_play_wins += stats.on_play_wins;
    total.on_draw_games += stats.on_draw_games;
    total.on_draw_wins += stats.on_draw_wins;
}

bool same_deck_totals(
    const OutcomeAccumulator& total,
    const DeckSimulationStats& stats) {
    return total.games == stats.games &&
           total.wins == stats.wins &&
           total.losses == stats.losses &&
           total.draws == stats.draws &&
           total.on_play_games == stats.on_play_games &&
           total.on_play_wins == stats.on_play_wins &&
           total.on_draw_games == stats.on_draw_games &&
           total.on_draw_wins == stats.on_draw_wins;
}

bool benchmark_accounting_exact_impl(
    const BotBenchmarkSummary& summary,
    const BotConfig& expected_challenger,
    const BotConfig& expected_baseline,
    std::string_view expected_challenger_fingerprint,
    std::string_view expected_baseline_fingerprint) {
    constexpr std::size_t kExpectedQuadrant =
        3 * kSmokeRepetitions;
    if (summary.evaluation_seed != kSmokeSeed ||
        summary.learned_training_seed != kTrainingSeed ||
        summary.repetitions_per_deck_pairing !=
            kSmokeRepetitions ||
        summary.total_games != kSmokeGames ||
        !same_bot(summary.challenger,
                  expected_challenger) ||
        !same_bot(summary.baseline, expected_baseline) ||
        summary.challenger_model_fingerprint !=
            expected_challenger_fingerprint ||
        summary.baseline_model_fingerprint !=
            expected_baseline_fingerprint ||
        !valid_bot_stats(summary.challenger_stats,
                         kSmokeGames) ||
        !valid_bot_stats(summary.baseline_stats,
                         kSmokeGames) ||
        summary.challenger_stats.wins !=
            summary.baseline_stats.losses ||
        summary.challenger_stats.losses !=
            summary.baseline_stats.wins ||
        summary.challenger_stats.draws !=
            summary.baseline_stats.draws ||
        summary.total_turns < kSmokeGames ||
        summary.life_total_finishes +
                summary.empty_library_finishes +
                summary.turn_limit_draws !=
            kSmokeGames ||
        summary.challenger_quartet_cr1.clusters !=
            15 * kSmokeRepetitions ||
        summary.challenger_quartet_cr1.records !=
            kSmokeGames) {
        return false;
    }

    OutcomeAccumulator challenger_total;
    OutcomeAccumulator baseline_total;
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const auto& challenger_deck =
            summary.challenger_decks[deck];
        const auto& baseline_deck =
            summary.baseline_decks[deck];
        if (!valid_deck_stats(challenger_deck,
                              kSmokeGamesPerDeck) ||
            !valid_deck_stats(baseline_deck,
                              kSmokeGamesPerDeck)) {
            return false;
        }
        add_deck(challenger_total, challenger_deck);
        add_deck(baseline_total, baseline_deck);

        OutcomeAccumulator challenger_quadrants;
        OutcomeAccumulator baseline_quadrants;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                const auto& challenger =
                    summary.challenger_outcome_quadrants
                        [deck][seat][play_draw];
                const auto& baseline =
                    summary.baseline_outcome_quadrants
                        [deck][seat][play_draw];
                if (!valid_outcomes(challenger,
                                    kExpectedQuadrant) ||
                    !valid_outcomes(baseline,
                                    kExpectedQuadrant)) {
                    return false;
                }
                add_outcomes(challenger_quadrants,
                             challenger, play_draw);
                add_outcomes(baseline_quadrants,
                             baseline, play_draw);
            }
        }
        if (!same_deck_totals(challenger_quadrants,
                              challenger_deck) ||
            !same_deck_totals(baseline_quadrants,
                              baseline_deck)) {
            return false;
        }

        OutcomeAccumulator matchup_row;
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const std::size_t expected_games =
                (deck == opponent ? 4U : 2U) *
                kSmokeRepetitions;
            const auto& matchup =
                summary.challenger_deck_matchups
                    [deck][opponent];
            if (!valid_deck_stats(matchup,
                                  expected_games)) {
                return false;
            }
            add_deck(matchup_row, matchup);
        }
        if (!same_deck_totals(matchup_row,
                              challenger_deck)) {
            return false;
        }
    }

    for (std::size_t baseline_deck = 0;
         baseline_deck < kDeckCount; ++baseline_deck) {
        OutcomeAccumulator reciprocal_column;
        for (std::size_t challenger_deck = 0;
             challenger_deck < kDeckCount;
             ++challenger_deck) {
            const auto& matchup =
                summary.challenger_deck_matchups
                    [challenger_deck][baseline_deck];
            reciprocal_column.games += matchup.games;
            reciprocal_column.wins += matchup.losses;
            reciprocal_column.losses += matchup.wins;
            reciprocal_column.draws += matchup.draws;
            reciprocal_column.on_play_games +=
                matchup.on_draw_games;
            reciprocal_column.on_draw_games +=
                matchup.on_play_games;
        }
        const auto& expected =
            summary.baseline_decks[baseline_deck];
        if (reciprocal_column.games != expected.games ||
            reciprocal_column.wins != expected.wins ||
            reciprocal_column.losses != expected.losses ||
            reciprocal_column.draws != expected.draws ||
            reciprocal_column.on_play_games !=
                expected.on_play_games ||
            reciprocal_column.on_draw_games !=
                expected.on_draw_games) {
            return false;
        }
    }

    return challenger_total.games == kSmokeGames &&
           challenger_total.wins ==
               summary.challenger_stats.wins &&
           challenger_total.losses ==
               summary.challenger_stats.losses &&
           challenger_total.draws ==
               summary.challenger_stats.draws &&
           baseline_total.games == kSmokeGames &&
           baseline_total.wins ==
               summary.baseline_stats.wins &&
           baseline_total.losses ==
               summary.baseline_stats.losses &&
           baseline_total.draws ==
               summary.baseline_stats.draws;
}

bool control_score_exact(
    const BotBenchmarkSummary& summary) {
    return summary.challenger_stats.wins == 120 &&
           summary.challenger_stats.losses == 120 &&
           summary.challenger_stats.draws == 0 &&
           summary.baseline_stats.wins == 120 &&
           summary.baseline_stats.losses == 120 &&
           summary.baseline_stats.draws == 0;
}

void add_failure(
    GateReport& gate, bool condition,
    std::string message) {
    if (!condition) {
        gate.failures.push_back(std::move(message));
    }
}

std::string_view controller_name(
    LearnedContinuationController controller) {
    switch (controller) {
    case LearnedContinuationController::Legacy:
        return "Legacy";
    case LearnedContinuationController::PublicStackPassV1:
        return "PublicStackPassV1";
    }
    fail("unknown_continuation_controller");
}

void format_snapshot(
    std::ostringstream& output, std::string_view role,
    std::string_view when,
    const integrity::RegularFileSnapshot& snapshot) {
    output << "artifact role=" << role
           << " when=" << when
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
    const std::size_t decisions =
        summary.challenger_stats.total_decisions +
        summary.baseline_stats.total_decisions;
    const std::size_t rollouts =
        summary.challenger_stats.total_rollouts +
        summary.baseline_stats.total_rollouts;
    const double rollouts_per_decision =
        decisions == 0
            ? 0.0
            : static_cast<double>(rollouts) /
                  static_cast<double>(decisions);
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
        << summary.confidence_high_95() << '\n'
        << "work role=" << role
        << " decisions=" << decisions
        << " rollouts=" << rollouts
        << " rollouts_per_decision="
        << rollouts_per_decision << '\n'
        << "trajectory role=" << role
        << " total_turns=" << summary.total_turns
        << " life_total_finishes="
        << summary.life_total_finishes
        << " empty_library_finishes="
        << summary.empty_library_finishes
        << " turn_limit_draws="
        << summary.turn_limit_draws << '\n';
    for (std::size_t deck = 0; deck < kDeckCount;
         ++deck) {
        const auto& challenger =
            summary.challenger_decks[deck];
        const auto& baseline =
            summary.baseline_decks[deck];
        output
            << "deck role=" << role
            << " deck=" << kDeckNames[deck]
            << " challenger=" << challenger.wins
            << '-' << challenger.losses
            << '-' << challenger.draws
            << " baseline=" << baseline.wins
            << '-' << baseline.losses
            << '-' << baseline.draws
            << " games=" << challenger.games << '\n';
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                const auto& challenger_quadrant =
                    summary.challenger_outcome_quadrants
                        [deck][seat][play_draw];
                const auto& baseline_quadrant =
                    summary.baseline_outcome_quadrants
                        [deck][seat][play_draw];
                output
                    << "quadrant role=" << role
                    << " deck=" << kDeckNames[deck]
                    << " policy_seat=" << seat
                    << " play_draw="
                    << (play_draw == 0
                            ? "play"
                            : "draw")
                    << " challenger="
                    << challenger_quadrant.wins
                    << '-' << challenger_quadrant.losses
                    << '-' << challenger_quadrant.draws
                    << " challenger_games="
                    << challenger_quadrant.games
                    << " baseline="
                    << baseline_quadrant.wins
                    << '-' << baseline_quadrant.losses
                    << '-' << baseline_quadrant.draws
                    << " baseline_games="
                    << baseline_quadrant.games << '\n';
            }
        }
    }
}

TimedBenchmark execute_production_benchmark(
    const BenchmarkRequest& request) {
    const auto started =
        std::chrono::steady_clock::now();
    BotBenchmarkSummary summary = run_bot_benchmark(
        request.repetitions, request.evaluation_seed,
        request.challenger, request.baseline,
        request.game,
        request.allow_identical_policy_control);
    const std::chrono::duration<double> elapsed =
        std::chrono::steady_clock::now() - started;
    return {
        .summary = std::move(summary),
        .elapsed_seconds = elapsed.count(),
    };
}

FixedDeployment load_production_deployment() {
    return testing::load_fixed_deployment_with({
        .snapshot =
            [](const std::filesystem::path& path) {
                return integrity::snapshot_regular_file(
                    path);
            },
        .load_parent =
            [](const std::filesystem::path& path) {
                const LearnedValueChallengerArtifact
                    loaded =
                        load_learned_value_challenger_artifact(
                            path.string(), kTrainingGames,
                            kTrainingSeed,
                            kParentGenerations);
                return testing::ParentArtifact{
                    .model = loaded.model(),
                    .training_games =
                        loaded.training_games(),
                    .training_seed = loaded.seed(),
                    .generations =
                        loaded.self_play_generations(),
                };
            },
        .load_candidate =
            [](const std::filesystem::path& path,
               std::shared_ptr<const LearnedModel> parent,
               const artifact::Contract& contract,
               const artifact::FileIdentity& identity) {
                const artifact::LoadedCandidate loaded =
                    artifact::load(
                        path, std::move(parent),
                        contract, identity);
                return testing::LoadedCandidate{
                    .model = loaded.model(),
                    .report = loaded.report(),
                };
            },
        .inspect_model =
            [](std::shared_ptr<const LearnedModel> model) {
                return ModelIdentity{
                    .fingerprint =
                        learned_model_fingerprint(model),
                    .components =
                        learned_model_component_fingerprints(
                            model),
                };
            },
    });
}

RunReport run_fixed_smoke_internal() {
    const FixedDeployment deployment =
        load_production_deployment();
    return testing::run_with(
        deployment, execute_production_benchmark,
        [](const std::string& path) {
            return integrity::snapshot_regular_file(
                path);
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

} // namespace

const artifact::Contract& anchored_contract() {
    static const artifact::Contract contract{
        .family = std::string(kFamily),
        .environment = std::string(kEnvironment),
        .parent = {
            .artifact_bytes = kParentArtifactBytes,
            .artifact_sha256 =
                std::string(kParentArtifactSha256),
            .model_fingerprint =
                std::string(kParentModelFingerprint),
            .components = {
                .critic = std::string(
                    kParentCriticFingerprint),
                .priority = std::string(
                    kParentPriorityFingerprint),
                .attack = std::string(
                    kParentAttackFingerprint),
                .block = std::string(
                    kParentBlockFingerprint),
                .damage_order = std::string(
                    kParentDamageOrderFingerprint),
            },
            .training_games = kTrainingGames,
            .training_seed = kTrainingSeed,
            .generation = kParentGenerations,
        },
        .corpus = {
            .artifact_bytes = 2'250'909,
            .artifact_sha256 =
                std::string(kCorpusArtifactSha256),
        },
        .fit = {
            .input_sha256 =
                std::string(kFitInputSha256),
            .examples = 248,
            .options = 987,
            .check_examples = 0,
            .background_only_examples = 0,
            .optimizer_calls = 1,
            .optimizer = kFrozenOptimizer,
        },
        .candidate_model_fingerprint =
            std::string(kCandidateModelFingerprint),
        .priority_hidden_count =
            kPriorityHiddenCount,
        .priority_feature_count =
            kPriorityFeatureCount,
        .priority_parameter_count =
            kPriorityParameterCount,
        .deployment = {
            .variant =
                LearnedVariant::ValueSearchChampion,
            .training_games = kTrainingGames,
            .worlds_per_action = kWorldsPerAction,
            .horizon_turns = kHorizonTurns,
            .rollouts_per_world = kRolloutsPerWorld,
            .root_search_depth = kRootSearchDepth,
            .shallow_prior = kBlendShallowPrior,
            .root_exploration = kRootExploration,
            .continuation_epsilon =
                kContinuationEpsilon,
            .priority_residual_weight =
                kPriorityResidualWeight,
            .pass_dominance = kPassDominance,
            .continuation_controller =
                kContinuationController,
            .max_turns = kMaximumTurns,
        },
    };
    static const bool validated = [] {
        testing::validate_anchored_contract(contract);
        return true;
    }();
    static_cast<void>(validated);
    return contract;
}

namespace testing {

void validate_anchored_contract(
    const artifact::Contract& contract) {
    require(contract_exact(contract),
            "anchored_contract_mismatch");
}

FixedDeployment load_fixed_deployment_with(
    const LoaderDependencies& dependencies) {
    require(
        dependencies.snapshot &&
            dependencies.load_parent &&
            dependencies.load_candidate &&
            dependencies.inspect_model,
        "loader_dependency_missing");

    const std::filesystem::path parent_path(
        kParentArtifactPath);
    const std::filesystem::path candidate_path(
        kCandidateArtifactPath);
    const integrity::RegularFileSnapshot parent_before =
        dependencies.snapshot(parent_path);
    const integrity::RegularFileSnapshot candidate_before =
        dependencies.snapshot(candidate_path);
    require(
        snapshot_has_identity(
            parent_before, kParentArtifactBytes,
            kParentArtifactSha256),
        "parent_artifact_identity");
    require(
        snapshot_has_identity(
            candidate_before, kCandidateArtifactBytes,
            kCandidateArtifactSha256),
        "candidate_artifact_identity");

    const ParentArtifact parent_loaded =
        dependencies.load_parent(parent_path);
    require(
        parent_loaded.model &&
            parent_loaded.training_games ==
                kTrainingGames &&
            parent_loaded.training_seed ==
                kTrainingSeed &&
            parent_loaded.generations ==
                kParentGenerations,
        "parent_metadata");
    const ModelIdentity parent_identity =
        dependencies.inspect_model(parent_loaded.model);
    require(
        parent_identity.fingerprint ==
                kParentModelFingerprint &&
            parent_components_exact(
                parent_identity.components),
        "parent_model_identity");

    const artifact::Contract& contract =
        anchored_contract();
    validate_anchored_contract(contract);
    const artifact::FileIdentity expected_artifact =
        expected_candidate_file();
    const LoadedCandidate loaded =
        dependencies.load_candidate(
            candidate_path, parent_loaded.model,
            contract, expected_artifact);
    require(loaded.model &&
                loaded.model != parent_loaded.model,
            "candidate_model_alias");
    const ModelIdentity candidate_identity =
        dependencies.inspect_model(loaded.model);
    require(
        candidate_identity.fingerprint ==
                kCandidateModelFingerprint &&
            candidate_components_exact(
                candidate_identity.components) &&
            only_priority_differs(
                parent_identity.components,
                candidate_identity.components),
        "candidate_model_identity");
    require(candidate_report_exact(loaded.report),
            "candidate_manifest_identity");

    const integrity::RegularFileSnapshot parent_after =
        dependencies.snapshot(parent_path);
    const integrity::RegularFileSnapshot candidate_after =
        dependencies.snapshot(candidate_path);
    require(parent_after == parent_before &&
                candidate_after == candidate_before,
            "artifact_changed_during_load");

    return {
        .parent = parent_loaded.model,
        .candidate = loaded.model,
        .parent_model_fingerprint =
            parent_identity.fingerprint,
        .candidate_model_fingerprint =
            candidate_identity.fingerprint,
        .parent_components = parent_identity.components,
        .candidate_components =
            candidate_identity.components,
        .parent_before = parent_before,
        .candidate_artifact_before =
            candidate_before,
        .candidate_report = loaded.report,
        .fixed_identity_valid = true,
    };
}

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

GameConfig make_game_config() {
    GameConfig game;
    game.max_turns = kMaximumTurns;
    game.learned_training_seed = kTrainingSeed;
    game.learned_search_depth = kRootSearchDepth;
    return game;
}

bool benchmark_accounting_exact(
    const BotBenchmarkSummary& summary,
    const BotConfig& expected_challenger,
    const BotConfig& expected_baseline,
    std::string_view expected_challenger_fingerprint,
    std::string_view expected_baseline_fingerprint) {
    return benchmark_accounting_exact_impl(
        summary, expected_challenger,
        expected_baseline,
        expected_challenger_fingerprint,
        expected_baseline_fingerprint);
}

GateReport evaluate_gate(const RunReport& report) {
    GateReport gate;
    gate.fixed_identity =
        fixed_deployment_exact(report.deployment);
    gate.artifacts_unchanged =
        report.deployment.parent_before ==
            report.parent_after &&
        report.deployment.candidate_artifact_before ==
            report.candidate_artifact_after;
    gate.models_unchanged =
        report.parent_model_after ==
            ModelIdentity{
                .fingerprint =
                    report.deployment
                        .parent_model_fingerprint,
                .components =
                    report.deployment.parent_components,
            } &&
        report.candidate_model_after ==
            ModelIdentity{
                .fingerprint =
                    report.deployment
                        .candidate_model_fingerprint,
                .components =
                    report.deployment
                        .candidate_components,
            };
    const BotConfig parent =
        make_learned_bot(report.deployment.parent);
    const BotConfig candidate =
        make_learned_bot(report.deployment.candidate);
    gate.accounting_complete =
        benchmark_accounting_exact_impl(
            report.identical_control.summary,
            parent, parent,
            report.deployment.parent_model_fingerprint,
            report.deployment.parent_model_fingerprint) &&
        benchmark_accounting_exact_impl(
            report.candidate.summary,
            candidate, parent,
            report.deployment.candidate_model_fingerprint,
            report.deployment.parent_model_fingerprint);
    gate.control_exact =
        control_score_exact(
            report.identical_control.summary);
    gate.aggregate_floor =
        report.candidate.summary.challenger_win_rate() >=
        kSmokeWinRateFloor;
    gate.runtime_ratio =
        std::isfinite(
            report.identical_control.elapsed_seconds) &&
        report.identical_control.elapsed_seconds > 0.0 &&
        std::isfinite(report.candidate.elapsed_seconds) &&
        report.candidate.elapsed_seconds > 0.0 &&
        report.candidate.elapsed_seconds <=
            kSmokeRuntimeRatioLimit *
            report.identical_control.elapsed_seconds;
    gate.infrastructure_valid =
        gate.fixed_identity &&
        gate.artifacts_unchanged &&
        gate.models_unchanged &&
        gate.accounting_complete &&
        gate.control_exact;
    gate.passed =
        gate.infrastructure_valid &&
        gate.aggregate_floor &&
        gate.runtime_ratio;

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
        gate, gate.accounting_complete,
        "paired benchmark accounting is incomplete");
    add_failure(
        gate, gate.control_exact,
        "identical C16 control is not exactly 120-120");
    add_failure(
        gate, gate.aggregate_floor,
        "candidate win rate is below 40 percent");
    add_failure(
        gate, gate.runtime_ratio,
        "candidate runtime exceeds 1.25 times control");
    return gate;
}

RunReport run_with(
    FixedDeployment deployment,
    const BenchmarkExecutor& benchmark,
    const Snapshotter& snapshot,
    const ModelInspector& inspect_model) {
    require(benchmark && snapshot && inspect_model,
            "execution_seam_missing");
    require(fixed_deployment_exact(deployment),
            "deployment_identity_before_gameplay");

    const BotConfig parent =
        make_learned_bot(deployment.parent);
    const BotConfig candidate =
        make_learned_bot(deployment.candidate);
    const GameConfig game = make_game_config();
    RunReport report{
        .deployment = std::move(deployment),
    };
    report.identical_control = benchmark({
        .role = "identical-control",
        .repetitions = kSmokeRepetitions,
        .evaluation_seed = kSmokeSeed,
        .challenger = parent,
        .baseline = parent,
        .game = game,
        .allow_identical_policy_control = true,
    });

    const ModelIdentity parent_after_control =
        inspect_model(report.deployment.parent);
    const ModelIdentity candidate_after_control =
        inspect_model(report.deployment.candidate);
    const auto parent_file_after_control =
        snapshot(report.deployment.parent_before.path);
    const auto candidate_file_after_control =
        snapshot(
            report.deployment
                .candidate_artifact_before.path);
    require(
        parent_after_control ==
                ModelIdentity{
                    .fingerprint =
                        report.deployment
                            .parent_model_fingerprint,
                    .components =
                        report.deployment
                            .parent_components,
                } &&
            candidate_after_control ==
                ModelIdentity{
                    .fingerprint =
                        report.deployment
                            .candidate_model_fingerprint,
                    .components =
                        report.deployment
                            .candidate_components,
                },
        "model_changed_during_control");
    require(
        parent_file_after_control ==
                report.deployment.parent_before &&
            candidate_file_after_control ==
                report.deployment
                    .candidate_artifact_before,
        "artifact_changed_during_control");
    require(
        benchmark_accounting_exact_impl(
            report.identical_control.summary,
            parent, parent,
            report.deployment.parent_model_fingerprint,
            report.deployment.parent_model_fingerprint) &&
            control_score_exact(
                report.identical_control.summary),
        "identical_control_invalid");

    report.candidate = benchmark({
        .role = "candidate",
        .repetitions = kSmokeRepetitions,
        .evaluation_seed = kSmokeSeed,
        .challenger = candidate,
        .baseline = parent,
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

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, const SmokeRunner& run_smoke) {
    constexpr std::string_view kUsage =
        "Usage: old-school-fq4-dev5-gameplay --smoke\n";
    if (argc != 2 || argv == nullptr ||
        argv[1] == nullptr ||
        std::string_view(argv[1]) != "--smoke") {
        error << kUsage;
        return 2;
    }
    if (!run_smoke) {
        error << "result=ERROR"
                 " reason=fixed_fq4_dev5_gameplay_failed\n";
        return 2;
    }
    try {
        const RunReport report = run_smoke();
        output << format_report(report);
        output.flush();
        if (!output.good() ||
            !report.gate.infrastructure_valid) {
            return 2;
        }
        return report.gate.passed ? 0 : 1;
    } catch (const std::exception&) {
        error << "result=ERROR"
                 " reason=fixed_fq4_dev5_gameplay_failed\n";
        return 2;
    }
}

} // namespace testing

std::string format_report(const RunReport& report) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17)
           << "schema=" << kReportSchema
           << " mode=smoke"
           << " artifact_family="
           << report.deployment
                  .candidate_report.manifest.contract.family
           << " artifact_recipe=" << artifact::kSchema
           << '\n';
    format_snapshot(
        output, "c16-parent", "before",
        report.deployment.parent_before);
    format_snapshot(
        output, "fq4-dev5-candidate", "before",
        report.deployment.candidate_artifact_before);
    format_snapshot(
        output, "c16-parent", "after",
        report.parent_after);
    format_snapshot(
        output, "fq4-dev5-candidate", "after",
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
    format_benchmark(
        output, "identical-control",
        report.identical_control);
    format_benchmark(
        output, "candidate", report.candidate);
    output
        << "gate fixed_identity="
        << (report.gate.fixed_identity ? 1 : 0)
        << " artifacts_unchanged="
        << (report.gate.artifacts_unchanged ? 1 : 0)
        << " models_unchanged="
        << (report.gate.models_unchanged ? 1 : 0)
        << " accounting_complete="
        << (report.gate.accounting_complete ? 1 : 0)
        << " control_exact="
        << (report.gate.control_exact ? 1 : 0)
        << " aggregate_floor="
        << (report.gate.aggregate_floor ? 1 : 0)
        << " runtime_ratio="
        << (report.gate.runtime_ratio ? 1 : 0)
        << " infrastructure_valid="
        << (report.gate.infrastructure_valid ? 1 : 0)
        << '\n';
    for (const std::string& failure :
         report.gate.failures) {
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

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error) {
    return testing::run_cli(
        argc, argv, output, error,
        run_fixed_smoke_internal);
}

} // namespace old_school::fq4_dev5_gameplay
