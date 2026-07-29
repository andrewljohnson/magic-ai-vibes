#include "old_school/joint_c17_eval.hpp"

#include "old_school/audit_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::joint_c17_eval {
namespace {

constexpr std::size_t kControlModelIndex =
    static_cast<std::size_t>(
        terminal_weight_eval::CriticModel::TW50);
constexpr std::size_t kTreatmentModelIndex =
    static_cast<std::size_t>(
        terminal_weight_eval::CriticModel::TW75);

void record_failure(
    bool condition, std::string_view message,
    std::vector<std::string>& failures) {
    if (!condition) {
        failures.emplace_back(message);
    }
}

template <typename Stats>
OutcomeCounts outcome_counts(const Stats& stats) {
    return {
        .games = stats.games,
        .wins = stats.wins,
        .losses = stats.losses,
        .draws = stats.draws,
    };
}

bool checked_add(
    std::size_t first, std::size_t second,
    std::size_t& result) {
    if (first >
        std::numeric_limits<std::size_t>::max() - second) {
        return false;
    }
    result = first + second;
    return true;
}

bool add_to(
    std::size_t value, std::size_t& accumulator) {
    std::size_t result = 0;
    if (!checked_add(accumulator, value, result)) {
        return false;
    }
    accumulator = result;
    return true;
}

bool outcomes_valid(const OutcomeCounts& outcomes) {
    std::size_t completed = 0;
    return outcomes.wins <= outcomes.games &&
           outcomes.losses <= outcomes.games &&
           outcomes.draws <= outcomes.games &&
           checked_add(
               outcomes.wins, outcomes.losses,
               completed) &&
           add_to(outcomes.draws, completed) &&
           completed == outcomes.games;
}

bool add_outcomes(
    const OutcomeCounts& source,
    OutcomeCounts& destination) {
    OutcomeCounts merged = destination;
    if (!add_to(source.games, merged.games) ||
        !add_to(source.wins, merged.wins) ||
        !add_to(source.losses, merged.losses) ||
        !add_to(source.draws, merged.draws)) {
        return false;
    }
    destination = merged;
    return true;
}

BenchmarkCountSummary benchmark_count_summary(
    const BotBenchmarkSummary& summary) {
    BenchmarkCountSummary counts{
        .panel_count = 1,
        .repetitions_per_deck_pairing =
            summary.repetitions_per_deck_pairing,
        .total_games = summary.total_games,
        .challenger = outcome_counts(
            summary.challenger_stats),
        .baseline = outcome_counts(summary.baseline_stats),
    };
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        counts.challenger_decks[deck] =
            outcome_counts(summary.challenger_decks[deck]);
        counts.baseline_decks[deck] =
            outcome_counts(summary.baseline_decks[deck]);
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            counts.challenger_deck_matchups[deck][opponent] =
                outcome_counts(
                    summary.challenger_deck_matchups
                        [deck][opponent]);
        }
    }
    counts.challenger_outcome_quadrants =
        summary.challenger_outcome_quadrants;
    counts.baseline_outcome_quadrants =
        summary.baseline_outcome_quadrants;
    return counts;
}

bool outcomes_equal(
    const OutcomeCounts& first,
    const OutcomeCounts& second) {
    return first == second;
}

bool quadrants_match_decks(
    const OutcomeQuadrants& quadrants,
    const std::array<OutcomeCounts, kDeckCount>& decks) {
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        OutcomeCounts total;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                const auto& cell =
                    quadrants[deck][seat][play_draw];
                if (!outcomes_valid(cell) ||
                    !add_outcomes(cell, total)) {
                    return false;
                }
            }
        }
        if (!outcomes_equal(total, decks[deck])) {
            return false;
        }
    }
    return true;
}

bool quadrants_have_exact_games(
    const OutcomeQuadrants& quadrants,
    std::size_t expected_games) {
    for (const auto& deck : quadrants) {
        for (const auto& seat : deck) {
            for (const auto& play_draw : seat) {
                if (play_draw.games != expected_games) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool deck_stats_match_quadrants(
    const std::array<DeckSimulationStats, kDeckCount>& stats,
    const OutcomeQuadrants& quadrants) {
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        std::array<OutcomeCounts, 2> by_play_draw{};
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                if (!add_outcomes(
                        quadrants[deck][seat][play_draw],
                        by_play_draw[play_draw])) {
                    return false;
                }
            }
        }
        const auto& deck_stats = stats[deck];
        if (by_play_draw[0].games != deck_stats.on_play_games ||
            by_play_draw[0].wins != deck_stats.on_play_wins ||
            by_play_draw[1].games != deck_stats.on_draw_games ||
            by_play_draw[1].wins != deck_stats.on_draw_wins) {
            return false;
        }
        std::size_t partitioned_games = 0;
        std::size_t partitioned_wins = 0;
        if (!checked_add(
                deck_stats.on_play_games,
                deck_stats.on_draw_games,
                partitioned_games) ||
            !checked_add(
                deck_stats.on_play_wins,
                deck_stats.on_draw_wins,
                partitioned_wins) ||
            partitioned_games != deck_stats.games ||
            partitioned_wins != deck_stats.wins) {
            return false;
        }
    }
    return true;
}

bool benchmark_counts_self_consistent(
    const BenchmarkCountSummary& counts) {
    if (counts.panel_count == 0 ||
        !outcomes_valid(counts.challenger) ||
        !outcomes_valid(counts.baseline) ||
        counts.challenger.games != counts.total_games ||
        counts.baseline.games != counts.total_games ||
        counts.challenger.wins != counts.baseline.losses ||
        counts.challenger.losses != counts.baseline.wins ||
        counts.challenger.draws != counts.baseline.draws) {
        return false;
    }

    OutcomeCounts challenger_deck_total;
    OutcomeCounts baseline_deck_total;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        if (!outcomes_valid(counts.challenger_decks[deck]) ||
            !outcomes_valid(counts.baseline_decks[deck]) ||
            !add_outcomes(
                counts.challenger_decks[deck],
                challenger_deck_total) ||
            !add_outcomes(
                counts.baseline_decks[deck],
                baseline_deck_total)) {
            return false;
        }

        OutcomeCounts challenger_row;
        OutcomeCounts baseline_column;
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const auto& row_cell =
                counts.challenger_deck_matchups
                    [deck][opponent];
            const auto& column_cell =
                counts.challenger_deck_matchups
                    [opponent][deck];
            if (!outcomes_valid(row_cell) ||
                !outcomes_valid(column_cell) ||
                !add_outcomes(row_cell, challenger_row)) {
                return false;
            }
            const OutcomeCounts baseline_view{
                .games = column_cell.games,
                .wins = column_cell.losses,
                .losses = column_cell.wins,
                .draws = column_cell.draws,
            };
            if (!add_outcomes(
                    baseline_view, baseline_column)) {
                return false;
            }
        }
        if (!outcomes_equal(
                challenger_row,
                counts.challenger_decks[deck]) ||
            !outcomes_equal(
                baseline_column,
                counts.baseline_decks[deck])) {
            return false;
        }
    }
    return outcomes_equal(
               challenger_deck_total, counts.challenger) &&
           outcomes_equal(
               baseline_deck_total, counts.baseline) &&
           quadrants_match_decks(
               counts.challenger_outcome_quadrants,
               counts.challenger_decks) &&
           quadrants_match_decks(
               counts.baseline_outcome_quadrants,
               counts.baseline_decks);
}

bool benchmark_schedule_exact(
    const BenchmarkCountSummary& counts,
    std::size_t expected_panel_count,
    std::size_t expected_repetitions,
    std::size_t expected_total_games,
    std::size_t expected_games_per_deck,
    std::size_t expected_diagonal_games,
    std::size_t expected_off_diagonal_games,
    std::size_t expected_quadrant_games) {
    if (counts.panel_count != expected_panel_count ||
        counts.repetitions_per_deck_pairing !=
            expected_repetitions ||
        counts.total_games != expected_total_games ||
        !benchmark_counts_self_consistent(counts)) {
        return false;
    }
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        if (counts.challenger_decks[deck].games !=
                expected_games_per_deck ||
            counts.baseline_decks[deck].games !=
                expected_games_per_deck) {
            return false;
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const std::size_t expected_games =
                deck == opponent
                    ? expected_diagonal_games
                    : expected_off_diagonal_games;
            if (counts.challenger_deck_matchups
                    [deck][opponent]
                        .games != expected_games) {
                return false;
            }
        }
    }
    return quadrants_have_exact_games(
               counts.challenger_outcome_quadrants,
               expected_quadrant_games) &&
           quadrants_have_exact_games(
               counts.baseline_outcome_quadrants,
               expected_quadrant_games);
}

bool benchmark_panel_schedule_exact(
    const BotBenchmarkSummary& summary,
    std::size_t expected_repetitions,
    std::size_t expected_total_games,
    std::size_t expected_games_per_deck,
    std::size_t expected_diagonal_games,
    std::size_t expected_off_diagonal_games,
    std::size_t expected_quadrant_games) {
    return benchmark_schedule_exact(
               benchmark_count_summary(summary), 1,
               expected_repetitions, expected_total_games,
               expected_games_per_deck,
               expected_diagonal_games,
               expected_off_diagonal_games,
               expected_quadrant_games) &&
           deck_stats_match_quadrants(
               summary.challenger_decks,
               summary.challenger_outcome_quadrants) &&
           deck_stats_match_quadrants(
               summary.baseline_decks,
               summary.baseline_outcome_quadrants);
}

bool merge_benchmark_counts(
    const BenchmarkCountSummary& source,
    BenchmarkCountSummary& destination) {
    BenchmarkCountSummary merged = destination;
    if (!add_to(source.panel_count, merged.panel_count) ||
        !add_to(
            source.repetitions_per_deck_pairing,
            merged.repetitions_per_deck_pairing) ||
        !add_to(source.total_games, merged.total_games) ||
        !add_outcomes(source.challenger, merged.challenger) ||
        !add_outcomes(source.baseline, merged.baseline)) {
        return false;
    }
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        if (!add_outcomes(
                source.challenger_decks[deck],
                merged.challenger_decks[deck]) ||
            !add_outcomes(
                source.baseline_decks[deck],
                merged.baseline_decks[deck])) {
            return false;
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            if (!add_outcomes(
                    source.challenger_deck_matchups
                        [deck][opponent],
                    merged.challenger_deck_matchups
                        [deck][opponent])) {
                return false;
            }
        }
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                if (!add_outcomes(
                        source.challenger_outcome_quadrants
                            [deck][seat][play_draw],
                        merged.challenger_outcome_quadrants
                            [deck][seat][play_draw]) ||
                    !add_outcomes(
                        source.baseline_outcome_quadrants
                            [deck][seat][play_draw],
                        merged.baseline_outcome_quadrants
                            [deck][seat][play_draw])) {
                    return false;
                }
            }
        }
    }
    destination = merged;
    return true;
}

std::optional<double> win_rate_percent(
    const OutcomeCounts& outcomes) {
    if (!outcomes_valid(outcomes) || outcomes.games == 0) {
        return std::nullopt;
    }
    const double rate =
        100.0 * static_cast<double>(outcomes.wins) /
        static_cast<double>(outcomes.games);
    if (!std::isfinite(rate)) {
        return std::nullopt;
    }
    return rate;
}

std::optional<double> wilson_lower_95_percent(
    const OutcomeCounts& outcomes) {
    if (!outcomes_valid(outcomes) || outcomes.games == 0) {
        return std::nullopt;
    }
    constexpr double z = 1.959963984540054;
    const double games = static_cast<double>(outcomes.games);
    const double proportion =
        static_cast<double>(outcomes.wins) / games;
    const double z_squared = z * z;
    const double denominator = 1.0 + z_squared / games;
    const double center =
        proportion + z_squared / (2.0 * games);
    const double radius =
        z * std::sqrt(
                (proportion * (1.0 - proportion) +
                 z_squared / (4.0 * games)) /
                games);
    const double lower =
        100.0 * std::max(
                    0.0,
                    (center - radius) / denominator);
    if (!std::isfinite(lower)) {
        return std::nullopt;
    }
    return lower;
}

bool is_lower_hex(
    std::string_view value, std::size_t length) {
    return value.size() == length &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' && character <= '9') ||
                          (character >= 'a' && character <= 'f');
               });
}

bool is_sha256(std::string_view value) {
    return is_lower_hex(value, 64);
}

bool expected_fingerprints_valid(
    const JointC17ExpectedModelFingerprints& fingerprints) {
    return is_sha256(fingerprints.control) &&
           is_sha256(fingerprints.treatment) &&
           fingerprints.control != fingerprints.treatment &&
           fingerprints.control !=
               kLearnedJointC17ParentFingerprint &&
           fingerprints.treatment !=
               kLearnedJointC17ParentFingerprint;
}

std::size_t expected_field_candidate_count(
    std::size_t fixture) {
    return fixture == 3 ? 3U : 2U;
}

std::string_view expected_field_candidate(
    std::size_t fixture, std::size_t candidate) {
    constexpr std::array<std::string_view, 2> block = {
        "no-blocks",
        "block-air-elemental-with-flying-men",
    };
    constexpr std::array<std::string_view, 2>
        sick_bear = {
            "pass",
            "growth-own-summoning-sick-grizzly-bears",
        };
    constexpr std::array<std::string_view, 3>
        begin_combat = {
            "pass",
            "growth-own-ironroot-treefolk",
            "growth-opponent-tapped-air-elemental",
        };
    constexpr std::array<std::string_view, 2> attack = {
        "skip-ironroot-treefolk",
        "include-ironroot-treefolk",
    };
    if (fixture < 2 && candidate < block.size()) {
        return block[candidate];
    }
    if (fixture == 2 && candidate < sick_bear.size()) {
        return sick_bear[candidate];
    }
    if (fixture == 3 && candidate < begin_combat.size()) {
        return begin_combat[candidate];
    }
    if (fixture >= 4 &&
        fixture < kFieldRegressionFixtureCount &&
        candidate < attack.size()) {
        return attack[candidate];
    }
    return {};
}

DeckId expected_field_deck(std::size_t fixture) {
    return fixture < 2 ? DeckId::RUAggro : DeckId::Green;
}

probes::DecisionKind expected_field_kind(
    std::size_t fixture) {
    if (fixture < 2) {
        return probes::DecisionKind::Block;
    }
    if (fixture < 4) {
        return probes::DecisionKind::Priority;
    }
    return probes::DecisionKind::Attack;
}

bool field_descriptors_exact(
    const std::vector<std::string>& descriptors,
    std::size_t fixture) {
    if (descriptors.size() !=
        expected_field_candidate_count(fixture)) {
        return false;
    }
    for (std::size_t candidate = 0;
         candidate < descriptors.size(); ++candidate) {
        if (descriptors[candidate] !=
            expected_field_candidate(
                fixture, candidate)) {
            return false;
        }
    }
    return true;
}

bool checked_multiply(
    std::size_t first, std::size_t second,
    std::size_t& result) {
    if (first != 0 &&
        second >
            std::numeric_limits<std::size_t>::max() /
                first) {
        return false;
    }
    result = first * second;
    return true;
}

bool field_accounting_exact(
    const probe_runner::FieldRegressionEvaluationAccounting&
        accounting,
    std::size_t candidates, std::size_t worlds) {
    std::size_t expected_evaluations = 0;
    std::size_t completed_evaluations = 0;
    return candidates >= 2 && worlds != 0 &&
           checked_multiply(
               candidates, worlds,
               expected_evaluations) &&
           accounting.sampled_worlds == worlds &&
           accounting.rollout_evaluations ==
               expected_evaluations &&
           checked_add(
               accounting.terminal_evaluations,
               accounting.bootstrapped_evaluations,
               completed_evaluations) &&
           completed_evaluations ==
               accounting.rollout_evaluations;
}

bool field_samples_exact(
    const std::vector<probe_eval::CandidateSamples>& samples,
    std::size_t fixture, std::size_t worlds) {
    if (samples.size() !=
        expected_field_candidate_count(fixture)) {
        return false;
    }
    for (std::size_t candidate = 0;
         candidate < samples.size(); ++candidate) {
        if (samples[candidate].key !=
                expected_field_candidate(
                    fixture, candidate) ||
            samples[candidate].q_samples.size() != worlds ||
            !std::all_of(
                samples[candidate].q_samples.begin(),
                samples[candidate].q_samples.end(),
                [](double value) {
                    return std::isfinite(value) &&
                           value >= 0.0 && value <= 1.0;
                })) {
            return false;
        }
    }
    return true;
}

bool field_scores_exact(
    const std::vector<probe_eval::PolicyScore>& scores,
    std::size_t fixture) {
    if (scores.size() !=
        expected_field_candidate_count(fixture)) {
        return false;
    }
    for (std::size_t candidate = 0;
         candidate < scores.size(); ++candidate) {
        if (scores[candidate].key !=
                expected_field_candidate(
                    fixture, candidate) ||
            !std::isfinite(scores[candidate].score)) {
            return false;
        }
    }
    return true;
}

bool field_selected_keys_exact(
    const probe_runner::FieldRegressionPolicyDecision&
        policy,
    std::size_t fixture) {
    if (policy.selected_keys.empty() ||
        !field_scores_exact(policy.scores, fixture)) {
        return false;
    }
    double maximum =
        -std::numeric_limits<double>::infinity();
    for (const auto& score : policy.scores) {
        maximum = std::max(maximum, score.score);
    }
    std::vector<std::string> expected;
    for (const auto& score : policy.scores) {
        if (score.score == maximum) {
            expected.push_back(score.key);
        }
    }
    if (policy.deterministic_selection) {
        return policy.selected_keys.size() == 1 &&
               !expected.empty() &&
               policy.selected_keys.front() ==
                   expected.front();
    }
    return policy.selected_keys == expected;
}

bool field_policy_exact(
    const probe_runner::FieldRegressionPolicyDecision&
        policy,
    std::size_t fixture, std::string_view expected_name,
    std::string_view expected_fingerprint,
    bool value_pass_dominance,
    LearnedContinuationController controller) {
    const bool priority =
        expected_field_kind(fixture) ==
        probes::DecisionKind::Priority;
    if (policy.name != expected_name ||
        policy.fingerprint != expected_fingerprint ||
        policy.score_kind !=
            (priority
                 ? probe_runner::
                       FieldRegressionScoreKind::
                           DeployedPrioritySearch
                 : probe_runner::
                       FieldRegressionScoreKind::
                           ImmediateCombat) ||
        policy.deployment_worlds !=
            probe_runner::kFieldDeploymentWorlds ||
        policy.deployment_horizon_turns !=
            probe_runner::kFieldDeploymentHorizonTurns ||
        !policy.blend_shallow_prior ||
        policy.value_continuation_epsilon != 0.0 ||
        policy.value_priority_residual_weight != 0.0 ||
        policy.value_pass_dominance !=
            value_pass_dominance ||
        policy.value_continuation_controller != controller ||
        !field_selected_keys_exact(policy, fixture)) {
        return false;
    }
    if (!priority) {
        return policy.samples.empty() &&
               policy.accounting ==
                   probe_runner::
                       FieldRegressionEvaluationAccounting{} &&
               policy.deterministic_selection &&
               !policy
                    .policy_scores_adjusted_for_deployment;
    }
    return !policy.deterministic_selection &&
           field_samples_exact(
               policy.samples, fixture,
               probe_runner::kFieldDeploymentWorlds) &&
           field_accounting_exact(
               policy.accounting,
               expected_field_candidate_count(fixture),
               probe_runner::kFieldDeploymentWorlds) &&
           (value_pass_dominance ||
            !policy
                 .policy_scores_adjusted_for_deployment);
}

bool field_forced_consequences_exact(
    const std::vector<
        probe_runner::FieldRegressionForcedConsequence>&
        consequences,
    std::size_t fixture) {
    if (consequences.size() !=
        expected_field_candidate_count(fixture)) {
        return false;
    }
    for (std::size_t candidate = 0;
         candidate < consequences.size(); ++candidate) {
        if (consequences[candidate].descriptor !=
                expected_field_candidate(
                    fixture, candidate) ||
            consequences[candidate]
                    .public_state_fingerprint !=
                kRequiredFieldConsequenceFingerprints
                    [fixture][candidate]) {
            return false;
        }
    }
    return true;
}

bool selection_intersects(
    const std::vector<std::string>& selected,
    const std::vector<std::string>& reference_best) {
    return std::any_of(
        selected.begin(), selected.end(),
        [&reference_best](const std::string& key) {
            return std::find(
                       reference_best.begin(),
                       reference_best.end(),
                       key) != reference_best.end();
        });
}

bool recipe_exact(
    const PolicyRecipeEvidence& evidence,
    std::string_view token, std::size_t horizon,
    bool blend_shallow_prior) {
    return evidence.policy_token == token &&
           evidence.horizon_turns == horizon &&
           evidence.blend_shallow_prior ==
               blend_shallow_prior;
}

bool learned_value_config_exact(
    const BotConfig& bot, bool value_pass_dominance,
    LearnedContinuationController controller) {
    return bot.kind == BotKind::Learned &&
           bot.learned_variant ==
               LearnedVariant::ValueSearchChampion &&
           bot.rollouts_per_action == 8 &&
           bot.exploration_rate == 0.0 &&
           bot.value_continuation_epsilon == 0.0 &&
           bot.value_priority_residual_weight == 0.0 &&
           bot.value_pass_dominance ==
               value_pass_dominance &&
           !bot.value_resolved_shallow_prior &&
           !bot.value_adversarial_blocks &&
           bot.value_continuation_controller == controller &&
           bot.training_games == 800;
}

bool treatment_identity_exact(
    const BotConfig& bot, std::string_view model_fingerprint,
    const PolicyRecipeEvidence& recipe,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    return expected_fingerprints_valid(fingerprints) &&
           learned_value_config_exact(
               bot, true,
               LearnedContinuationController::
                   PublicStackPassV1) &&
           model_fingerprint == fingerprints.treatment &&
           recipe_exact(
               recipe, kLearnedJointC17TreatmentPolicyToken,
               kLearnedValueSearchHorizonTurns, true);
}

bool control_identity_exact(
    const BotConfig& bot, std::string_view model_fingerprint,
    const PolicyRecipeEvidence& recipe,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    return expected_fingerprints_valid(fingerprints) &&
           learned_value_config_exact(
               bot, false,
               LearnedContinuationController::Legacy) &&
           model_fingerprint == fingerprints.control &&
           recipe_exact(
               recipe, kLearnedJointC17ControlPolicyToken,
               kLearnedValueSearchHorizonTurns, true);
}

bool parent_identity_exact(
    const BotConfig& bot, std::string_view model_fingerprint,
    const PolicyRecipeEvidence& recipe) {
    return learned_value_config_exact(
               bot, false,
               LearnedContinuationController::Legacy) &&
           model_fingerprint ==
               kLearnedJointC17ParentFingerprint &&
           recipe_exact(
               recipe, kFrozenC16EvidencePolicyToken,
               kLearnedValueSearchHorizonTurns, true);
}

bool handcoded_identity_exact(
    const BotConfig& bot, std::string_view model_fingerprint,
    const PolicyRecipeEvidence& recipe) {
    return bot.kind == BotKind::Handcrafted &&
           bot.rollouts_per_action == 1 &&
           bot.exploration_rate == 0.0 &&
           bot.value_continuation_epsilon == 0.0 &&
           bot.value_priority_residual_weight == 0.0 &&
           !bot.value_pass_dominance &&
           !bot.value_resolved_shallow_prior &&
           !bot.value_adversarial_blocks &&
           bot.value_continuation_controller ==
               LearnedContinuationController::Legacy &&
           bot.training_games == 800 &&
           model_fingerprint.empty() &&
           recipe_exact(
               recipe, kHandcodedEvidencePolicyToken,
               0, false);
}

bool seed_is_fixed_panel_seed(std::uint64_t seed) {
    return std::find(
               kFixedSeedPanelSeeds.begin(),
               kFixedSeedPanelSeeds.end(),
               seed) != kFixedSeedPanelSeeds.end();
}

bool direct_panel_identity_exact(
    const DirectPanelEvidence& evidence,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    const auto& summary = evidence.summary;
    if (summary.learned_training_seed !=
            kDefaultLearnedTrainingSeed ||
        !treatment_identity_exact(
            summary.challenger,
            summary.challenger_model_fingerprint,
            evidence.challenger_policy, fingerprints)) {
        return false;
    }
    switch (evidence.role) {
    case DirectPanelRole::TreatmentVsControl:
        return summary.evaluation_seed ==
                   kLearnedJointC17MatchedControlGameplaySeed &&
               control_identity_exact(
                   summary.baseline,
                   summary.baseline_model_fingerprint,
                   evidence.baseline_policy, fingerprints);
    case DirectPanelRole::TreatmentVsParent:
        return summary.evaluation_seed ==
                   kLearnedJointC17FrozenC16GameplaySeed &&
               parent_identity_exact(
                   summary.baseline,
                   summary.baseline_model_fingerprint,
                   evidence.baseline_policy);
    case DirectPanelRole::TreatmentVsHandcodedPrimary:
        return summary.evaluation_seed ==
                   kLearnedJointC17HandcodedGameplaySeed &&
               handcoded_identity_exact(
                   summary.baseline,
                   summary.baseline_model_fingerprint,
                   evidence.baseline_policy);
    case DirectPanelRole::TreatmentVsHandcodedFixedSeed:
        return seed_is_fixed_panel_seed(
                   summary.evaluation_seed) &&
               handcoded_identity_exact(
                   summary.baseline,
                   summary.baseline_model_fingerprint,
                   evidence.baseline_policy);
    }
    return false;
}

bool clustered_estimate_valid(
    const BotBenchmarkClusteredScoreEstimate& estimate,
    const OutcomeCounts& outcomes,
    std::size_t expected_clusters,
    std::size_t expected_records) {
    if (!outcomes_valid(outcomes) || outcomes.games == 0 ||
        estimate.clusters != expected_clusters ||
        estimate.records != expected_records ||
        !std::isfinite(estimate.mean) ||
        !std::isfinite(estimate.standard_error) ||
        !std::isfinite(estimate.confidence_low_95) ||
        !std::isfinite(estimate.confidence_high_95) ||
        estimate.standard_error < 0.0 ||
        estimate.confidence_low_95 >
            estimate.confidence_high_95) {
        return false;
    }
    const double expected_mean =
        (static_cast<double>(outcomes.wins) +
         0.5 * static_cast<double>(outcomes.draws)) /
        static_cast<double>(outcomes.games);
    constexpr double z = 1.959963984540054;
    const double expected_lower =
        estimate.mean - z * estimate.standard_error;
    const double expected_upper =
        estimate.mean + z * estimate.standard_error;
    constexpr double tolerance = 1e-12;
    return std::abs(estimate.mean - expected_mean) <= tolerance &&
           std::abs(
               estimate.confidence_low_95 -
               expected_lower) <= tolerance &&
           std::abs(
               estimate.confidence_high_95 -
               expected_upper) <= tolerance;
}

bool checked_add_signed(
    std::int64_t first, std::int64_t second,
    std::int64_t& result) {
    if ((second > 0 &&
         first >
             std::numeric_limits<std::int64_t>::max() -
                 second) ||
        (second < 0 &&
         first <
             std::numeric_limits<std::int64_t>::min() -
                 second)) {
        return false;
    }
    result = first + second;
    return true;
}

bool add_signed_to(
    std::int64_t value, std::int64_t& accumulator) {
    std::int64_t result = 0;
    if (!checked_add_signed(accumulator, value, result)) {
        return false;
    }
    accumulator = result;
    return true;
}

bool deck_stats_valid(const DeckSimulationStats& stats) {
    const OutcomeCounts outcomes = outcome_counts(stats);
    std::size_t play_draw_games = 0;
    std::size_t play_draw_wins = 0;
    return outcomes_valid(outcomes) &&
           stats.on_play_wins <= stats.on_play_games &&
           stats.on_draw_wins <= stats.on_draw_games &&
           checked_add(
               stats.on_play_games, stats.on_draw_games,
               play_draw_games) &&
           checked_add(
               stats.on_play_wins, stats.on_draw_wins,
               play_draw_wins) &&
           play_draw_games == stats.games &&
           play_draw_wins == stats.wins;
}

bool add_deck_stats(
    const DeckSimulationStats& source,
    DeckSimulationStats& destination) {
    DeckSimulationStats merged = destination;
    if (!add_to(source.games, merged.games) ||
        !add_to(source.wins, merged.wins) ||
        !add_to(source.losses, merged.losses) ||
        !add_to(source.draws, merged.draws) ||
        !add_to(source.on_play_games, merged.on_play_games) ||
        !add_to(source.on_play_wins, merged.on_play_wins) ||
        !add_to(source.on_draw_games, merged.on_draw_games) ||
        !add_to(source.on_draw_wins, merged.on_draw_wins) ||
        !add_signed_to(
            source.total_ending_life,
            merged.total_ending_life) ||
        !add_to(
            source.total_cards_drawn,
            merged.total_cards_drawn) ||
        !add_to(
            source.total_lands_played,
            merged.total_lands_played) ||
        !add_to(
            source.total_spells_cast,
            merged.total_spells_cast) ||
        !add_to(
            source.total_spells_countered,
            merged.total_spells_countered) ||
        !add_to(
            source.total_damage_to_opponent,
            merged.total_damage_to_opponent) ||
        !add_to(
            source.total_cards_milled,
            merged.total_cards_milled)) {
        return false;
    }
    destination = merged;
    return true;
}

bool deck_stats_equal(
    const DeckSimulationStats& first,
    const DeckSimulationStats& second) {
    return first.games == second.games &&
           first.wins == second.wins &&
           first.losses == second.losses &&
           first.draws == second.draws &&
           first.on_play_games == second.on_play_games &&
           first.on_play_wins == second.on_play_wins &&
           first.on_draw_games == second.on_draw_games &&
           first.on_draw_wins == second.on_draw_wins &&
           first.total_ending_life ==
               second.total_ending_life &&
           first.total_cards_drawn ==
               second.total_cards_drawn &&
           first.total_lands_played ==
               second.total_lands_played &&
           first.total_spells_cast ==
               second.total_spells_cast &&
           first.total_spells_countered ==
               second.total_spells_countered &&
           first.total_damage_to_opponent ==
               second.total_damage_to_opponent &&
           first.total_cards_milled ==
               second.total_cards_milled;
}

bool bot_stats_valid(const BotSimulationStats& stats) {
    return outcomes_valid(outcome_counts(stats));
}

bool add_bot_stats(
    const BotSimulationStats& source,
    BotSimulationStats& destination) {
    BotSimulationStats merged = destination;
    if (!add_to(source.games, merged.games) ||
        !add_to(source.wins, merged.wins) ||
        !add_to(source.losses, merged.losses) ||
        !add_to(source.draws, merged.draws) ||
        !add_to(
            source.total_decisions,
            merged.total_decisions) ||
        !add_to(
            source.total_rollouts,
            merged.total_rollouts)) {
        return false;
    }
    destination = merged;
    return true;
}

bool bot_stats_equal(
    const BotSimulationStats& first,
    const BotSimulationStats& second) {
    return first.games == second.games &&
           first.wins == second.wins &&
           first.losses == second.losses &&
           first.draws == second.draws &&
           first.total_decisions == second.total_decisions &&
           first.total_rollouts == second.total_rollouts;
}

bool bot_matchup_valid(const BotMatchupStats& stats) {
    std::size_t outcomes = 0;
    return static_cast<std::size_t>(stats.first_bot) <
               kBotKindCount &&
           static_cast<std::size_t>(stats.second_bot) <
               kBotKindCount &&
           stats.first_bot < stats.second_bot &&
           checked_add(
               stats.first_wins, stats.second_wins,
               outcomes) &&
           add_to(stats.draws, outcomes) &&
           outcomes == stats.games;
}

bool add_bot_matchup_stats(
    const BotMatchupStats& source,
    BotMatchupStats& destination) {
    if (source.first_bot != destination.first_bot ||
        source.second_bot != destination.second_bot) {
        return false;
    }
    BotMatchupStats merged = destination;
    if (!add_to(source.games, merged.games) ||
        !add_to(source.first_wins, merged.first_wins) ||
        !add_to(source.second_wins, merged.second_wins) ||
        !add_to(source.draws, merged.draws)) {
        return false;
    }
    destination = merged;
    return true;
}

bool bot_matchup_equal(
    const BotMatchupStats& first,
    const BotMatchupStats& second) {
    return first.first_bot == second.first_bot &&
           first.second_bot == second.second_bot &&
           first.games == second.games &&
           first.first_wins == second.first_wins &&
           first.second_wins == second.second_wins &&
           first.draws == second.draws;
}

std::array<BotMatchupStats, kBotMatchupCount>
empty_canonical_bot_matchups() {
    std::array<BotMatchupStats, kBotMatchupCount> result{};
    std::size_t index = 0;
    for (std::size_t first = 0;
         first < kBotKindCount; ++first) {
        for (std::size_t second = first + 1;
             second < kBotKindCount; ++second) {
            result[index++] = {
                .first_bot = static_cast<BotKind>(first),
                .second_bot = static_cast<BotKind>(second),
            };
        }
    }
    return result;
}

constexpr std::array<
    std::pair<DeckId, DeckId>,
    kDistinctDeckPairingCount>
    kCanonicalDeckPairings = {{
        {DeckId::Green, DeckId::Red},
        {DeckId::Green, DeckId::Blue},
        {DeckId::Green, DeckId::White},
        {DeckId::Green, DeckId::RUAggro},
        {DeckId::Red, DeckId::Blue},
        {DeckId::Red, DeckId::White},
        {DeckId::Red, DeckId::RUAggro},
        {DeckId::Blue, DeckId::White},
        {DeckId::Blue, DeckId::RUAggro},
        {DeckId::White, DeckId::RUAggro},
    }};

bool physical_outcomes_complement(
    const DeckSimulationStats& first,
    const DeckSimulationStats& second,
    std::size_t expected_games) {
    return deck_stats_valid(first) &&
           deck_stats_valid(second) &&
           first.games == expected_games &&
           second.games == expected_games &&
           first.wins == second.losses &&
           first.losses == second.wins &&
           first.draws == second.draws;
}

bool simulation_finish_accounting_exact(
    const SimulationSummary& summary,
    std::size_t expected_games) {
    std::size_t finishes = 0;
    return summary.games == expected_games &&
           checked_add(
               summary.life_total_finishes,
               summary.empty_library_finishes,
               finishes) &&
           add_to(summary.turn_limit_draws, finishes) &&
           finishes == expected_games &&
           summary.turn_limit_draws == summary.draws;
}

bool mixed_matchup_exact(
    const MatchupSummary& matchup,
    std::size_t matchup_index) {
    if (matchup_index >= kCanonicalDeckPairings.size() ||
        matchup.first_deck !=
            kCanonicalDeckPairings[matchup_index].first ||
        matchup.second_deck !=
            kCanonicalDeckPairings[matchup_index].second ||
        !simulation_finish_accounting_exact(
            matchup.result,
            kMixedFieldGamesPerMatchup) ||
        matchup.result.draws !=
            matchup.result.decks[0].draws ||
        !physical_outcomes_complement(
            matchup.result.decks[0],
            matchup.result.decks[1],
            kMixedFieldGamesPerMatchup)) {
        return false;
    }

    const auto canonical_matchups =
        empty_canonical_bot_matchups();
    for (std::size_t seat = 0; seat < 2; ++seat) {
        const auto& deck = matchup.result.decks[seat];
        if (deck.on_play_games !=
                kMixedFieldGamesPerMatchup / 2 ||
            deck.on_draw_games !=
                kMixedFieldGamesPerMatchup / 2) {
            return false;
        }
        DeckSimulationStats summed;
        for (std::size_t bot = 0;
             bot < kBotKindCount; ++bot) {
            const auto& cell =
                matchup.result.deck_bots[seat][bot];
            if (!deck_stats_valid(cell) ||
                cell.games !=
                    kMixedFieldGamesPerMatchup /
                        kBotKindCount ||
                cell.on_play_games !=
                    kMixedFieldGamesPerMatchup /
                        (2 * kBotKindCount) ||
                cell.on_draw_games !=
                    kMixedFieldGamesPerMatchup /
                        (2 * kBotKindCount) ||
                !add_deck_stats(cell, summed)) {
                return false;
            }
        }
        if (!deck_stats_equal(summed, deck)) {
            return false;
        }
    }

    OutcomeCounts all_bot_outcomes;
    for (std::size_t bot = 0;
         bot < kBotKindCount; ++bot) {
        const auto& stats = matchup.result.bots[bot];
        OutcomeCounts from_decks;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            if (!add_outcomes(
                    outcome_counts(
                        matchup.result.deck_bots[seat][bot]),
                    from_decks)) {
                return false;
            }
        }
        if (!bot_stats_valid(stats) ||
            stats.games !=
                2 * kMixedFieldGamesPerMatchup /
                    kBotKindCount ||
            !outcomes_equal(
                outcome_counts(stats), from_decks) ||
            !add_outcomes(
                outcome_counts(stats), all_bot_outcomes)) {
            return false;
        }
    }
    if (!outcomes_valid(all_bot_outcomes) ||
        all_bot_outcomes.games !=
            2 * kMixedFieldGamesPerMatchup ||
        all_bot_outcomes.wins != all_bot_outcomes.losses ||
        all_bot_outcomes.draws !=
            2 * matchup.result.draws) {
        return false;
    }

    std::array<OutcomeCounts, kBotKindCount>
        distinct_bot_outcomes{};
    for (std::size_t index = 0;
         index < kBotMatchupCount; ++index) {
        const auto& stats =
            matchup.result.bot_matchups[index];
        if (!bot_matchup_valid(stats) ||
            stats.first_bot !=
                canonical_matchups[index].first_bot ||
            stats.second_bot !=
                canonical_matchups[index].second_bot ||
            stats.games !=
                kMixedFieldGamesPerBotPairPerMatchup) {
            return false;
        }
        const auto first =
            static_cast<std::size_t>(stats.first_bot);
        const auto second =
            static_cast<std::size_t>(stats.second_bot);
        const OutcomeCounts first_view{
            .games = stats.games,
            .wins = stats.first_wins,
            .losses = stats.second_wins,
            .draws = stats.draws,
        };
        const OutcomeCounts second_view{
            .games = stats.games,
            .wins = stats.second_wins,
            .losses = stats.first_wins,
            .draws = stats.draws,
        };
        if (!add_outcomes(
                first_view,
                distinct_bot_outcomes[first]) ||
            !add_outcomes(
                second_view,
                distinct_bot_outcomes[second])) {
            return false;
        }
    }
    for (std::size_t bot = 0;
         bot < kBotKindCount; ++bot) {
        const auto total =
            outcome_counts(matchup.result.bots[bot]);
        const auto distinct =
            distinct_bot_outcomes[bot];
        if (!outcomes_valid(distinct) ||
            total.games < distinct.games ||
            total.wins < distinct.wins ||
            total.losses < distinct.losses ||
            total.draws < distinct.draws) {
            return false;
        }
        const OutcomeCounts same_bot{
            .games = total.games - distinct.games,
            .wins = total.wins - distinct.wins,
            .losses = total.losses - distinct.losses,
            .draws = total.draws - distinct.draws,
        };
        if (!outcomes_valid(same_bot) ||
            same_bot.games != 8 ||
            same_bot.wins != same_bot.losses ||
            same_bot.draws % 2 != 0) {
            return false;
        }
    }
    return true;
}

bool mixed_panel_accounting_exact(
    const TournamentSummary& summary) {
    if (summary.games_per_matchup !=
            kMixedFieldGamesPerMatchup ||
        summary.total_games !=
            kMixedFieldGamesPerSeed) {
        return false;
    }

    std::array<DeckSimulationStats, kDeckCount> decks{};
    std::array<
        std::array<DeckSimulationStats, kBotKindCount>,
        kDeckCount>
        deck_bots{};
    std::array<BotSimulationStats, kBotKindCount> bots{};
    auto bot_matchups = empty_canonical_bot_matchups();
    std::size_t draws = 0;
    std::size_t life_total_finishes = 0;
    std::size_t empty_library_finishes = 0;
    std::size_t turn_limit_draws = 0;
    std::size_t total_turns = 0;

    for (std::size_t index = 0;
         index < summary.matchups.size(); ++index) {
        const auto& matchup = summary.matchups[index];
        if (!mixed_matchup_exact(matchup, index) ||
            !add_to(matchup.result.draws, draws) ||
            !add_to(
                matchup.result.life_total_finishes,
                life_total_finishes) ||
            !add_to(
                matchup.result.empty_library_finishes,
                empty_library_finishes) ||
            !add_to(
                matchup.result.turn_limit_draws,
                turn_limit_draws) ||
            !add_to(
                matchup.result.total_turns,
                total_turns)) {
            return false;
        }
        const auto first =
            static_cast<std::size_t>(matchup.first_deck);
        const auto second =
            static_cast<std::size_t>(matchup.second_deck);
        if (!add_deck_stats(
                matchup.result.decks[0],
                decks[first]) ||
            !add_deck_stats(
                matchup.result.decks[1],
                decks[second])) {
            return false;
        }
        for (std::size_t bot = 0;
             bot < kBotKindCount; ++bot) {
            if (!add_bot_stats(
                    matchup.result.bots[bot],
                    bots[bot]) ||
                !add_deck_stats(
                    matchup.result.deck_bots[0][bot],
                    deck_bots[first][bot]) ||
                !add_deck_stats(
                    matchup.result.deck_bots[1][bot],
                    deck_bots[second][bot])) {
                return false;
            }
        }
        for (std::size_t bot_matchup = 0;
             bot_matchup < kBotMatchupCount;
             ++bot_matchup) {
            if (!add_bot_matchup_stats(
                    matchup.result
                        .bot_matchups[bot_matchup],
                    bot_matchups[bot_matchup])) {
                return false;
            }
        }
    }

    if (summary.draws != draws ||
        summary.life_total_finishes !=
            life_total_finishes ||
        summary.empty_library_finishes !=
            empty_library_finishes ||
        summary.turn_limit_draws !=
            turn_limit_draws ||
        summary.total_turns != total_turns) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (!deck_stats_valid(summary.decks[deck]) ||
            summary.decks[deck].games !=
                4 * kMixedFieldGamesPerMatchup ||
            summary.decks[deck].on_play_games !=
                2 * kMixedFieldGamesPerMatchup ||
            summary.decks[deck].on_draw_games !=
                2 * kMixedFieldGamesPerMatchup ||
            !deck_stats_equal(
                summary.decks[deck], decks[deck])) {
            return false;
        }
        for (std::size_t bot = 0;
             bot < kBotKindCount; ++bot) {
            const auto& cell =
                summary.deck_bots[deck][bot];
            if (!deck_stats_valid(cell) ||
                cell.games !=
                    kMixedFieldGamesPerDeckPolicyPerSeed ||
                cell.on_play_games !=
                    kMixedFieldPlayDrawGamesPerDeckPolicyPerSeed ||
                cell.on_draw_games !=
                    kMixedFieldPlayDrawGamesPerDeckPolicyPerSeed ||
                !deck_stats_equal(
                    cell, deck_bots[deck][bot])) {
                return false;
            }
        }
    }
    for (std::size_t bot = 0;
         bot < kBotKindCount; ++bot) {
        if (!bot_stats_valid(summary.bots[bot]) ||
            summary.bots[bot].games !=
                4 * kMixedFieldGamesPerMatchup ||
            !bot_stats_equal(
                summary.bots[bot], bots[bot])) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < kBotMatchupCount; ++index) {
        if (!bot_matchup_valid(
                summary.bot_matchups[index]) ||
            summary.bot_matchups[index].games !=
                4 * kMixedFieldGamesPerMatchup /
                    kBotKindCount ||
            !bot_matchup_equal(
                summary.bot_matchups[index],
                bot_matchups[index])) {
            return false;
        }
    }
    return true;
}

bool finite_estimate(
    const terminal_weight_eval::ClusteredEstimate& estimate) {
    return std::isfinite(estimate.mean) &&
           std::isfinite(estimate.standard_error) &&
           std::isfinite(estimate.confidence_lower_95) &&
           std::isfinite(estimate.confidence_upper_95);
}

bool estimate_has_material_bias(
    const terminal_weight_eval::ClusteredEstimate& estimate) {
    return std::abs(estimate.mean) >= kMaterialSignedBias &&
           (estimate.confidence_lower_95 > 0.0 ||
            estimate.confidence_upper_95 < 0.0);
}

std::optional<std::size_t> deck_index(DeckId deck) {
    const auto index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        return std::nullopt;
    }
    return index;
}

bool contains_key(
    const std::vector<std::string>& keys,
    std::string_view wanted) {
    return std::any_of(
        keys.begin(), keys.end(),
        [wanted](const std::string& key) {
            return key == wanted;
        });
}

bool has_duplicate_keys(
    const std::vector<std::string>& keys) {
    for (std::size_t first = 0; first < keys.size(); ++first) {
        for (std::size_t second = first + 1;
             second < keys.size(); ++second) {
            if (keys[first] == keys[second]) {
                return true;
            }
        }
    }
    return false;
}

bool label_has_candidate(
    const probe_eval::ProbeLabel& label,
    std::string_view key) {
    return std::any_of(
        label.candidates.begin(), label.candidates.end(),
        [key](const probe_eval::CandidateLabel& candidate) {
            return candidate.key == key;
        });
}

bool valid_label(const probe_eval::ProbeLabel& label) {
    if (label.stable_id.empty() ||
        !deck_index(label.root_deck).has_value() ||
        label.candidates.empty() ||
        label.reference_best_set.empty() ||
        has_duplicate_keys(label.reference_best_set) ||
        !std::isfinite(label.reference_value) ||
        label.reference_value < 0.0 ||
        label.reference_value > 1.0) {
        return false;
    }

    std::vector<std::string> candidate_keys;
    candidate_keys.reserve(label.candidates.size());
    for (const auto& candidate : label.candidates) {
        if (candidate.key.empty() ||
            !std::isfinite(candidate.q) ||
            candidate.q < 0.0 || candidate.q > 1.0 ||
            !std::isfinite(candidate.standard_error) ||
            candidate.standard_error < 0.0) {
            return false;
        }
        candidate_keys.push_back(candidate.key);
    }
    if (has_duplicate_keys(candidate_keys)) {
        return false;
    }
    for (const auto& best : label.reference_best_set) {
        if (!label_has_candidate(label, best)) {
            return false;
        }
    }

    for (const auto& pair : label.pairs) {
        if (pair.first.empty() || pair.second.empty() ||
            pair.first == pair.second ||
            !label_has_candidate(label, pair.first) ||
            !label_has_candidate(label, pair.second) ||
            !std::isfinite(pair.delta_q) ||
            !std::isfinite(pair.paired_standard_error) ||
            pair.paired_standard_error < 0.0) {
            return false;
        }
    }

    for (std::size_t first = 0;
         first < candidate_keys.size(); ++first) {
        for (std::size_t second = first + 1;
             second < candidate_keys.size(); ++second) {
            std::size_t pair_count = 0;
            for (const auto& pair : label.pairs) {
                if ((pair.first == candidate_keys[first] &&
                     pair.second == candidate_keys[second]) ||
                    (pair.first == candidate_keys[second] &&
                     pair.second == candidate_keys[first])) {
                    ++pair_count;
                }
            }
            if (pair_count != 1) {
                return false;
            }
        }
    }
    const std::size_t expected_pairs =
        candidate_keys.size() *
        (candidate_keys.size() - 1) / 2;
    return label.pairs.size() == expected_pairs;
}

std::optional<std::pair<double, double>> oriented_pair(
    const probe_eval::ProbeLabel& label,
    std::string_view first, std::string_view second) {
    std::optional<std::pair<double, double>> result;
    for (const auto& pair : label.pairs) {
        if (pair.first == first && pair.second == second) {
            if (result.has_value()) {
                return std::nullopt;
            }
            result = std::pair{
                pair.delta_q, pair.paired_standard_error};
        } else if (
            pair.first == second && pair.second == first) {
            if (result.has_value()) {
                return std::nullopt;
            }
            result = std::pair{
                -pair.delta_q, pair.paired_standard_error};
        }
    }
    return result;
}

bool stable_positive_pair(
    const probe_eval::ProbeLabel& label,
    std::string_view best, std::string_view outside) {
    const auto pair = oriented_pair(label, best, outside);
    if (!pair.has_value()) {
        return false;
    }
    const double delta = pair->first;
    const double standard_error = pair->second;
    return delta > 0.0 &&
           std::abs(delta) >=
               probe_eval::kStablePairMinimumDelta &&
           std::abs(delta) >
               probe_eval::kNormal95CriticalValue *
                   standard_error;
}

std::size_t label_count(
    std::span<const probe_eval::ProbeLabel> labels,
    std::string_view stable_id) {
    return static_cast<std::size_t>(std::count_if(
        labels.begin(), labels.end(),
        [stable_id](const probe_eval::ProbeLabel& label) {
            return label.stable_id == stable_id;
        }));
}

const probe_eval::ProbeLabel* find_unique_label(
    std::span<const probe_eval::ProbeLabel> labels,
    std::string_view stable_id) {
    const probe_eval::ProbeLabel* result = nullptr;
    for (const auto& label : labels) {
        if (label.stable_id != stable_id) {
            continue;
        }
        if (result != nullptr) {
            return nullptr;
        }
        result = &label;
    }
    return result;
}

std::size_t decision_count(
    std::span<
        const probe_runner::ValueProbeDecisionDetail>
        decisions,
    std::string_view stable_id) {
    return static_cast<std::size_t>(std::count_if(
        decisions.begin(), decisions.end(),
        [stable_id](
            const probe_runner::ValueProbeDecisionDetail&
                decision) {
            return decision.stable_id == stable_id;
        }));
}

const probe_runner::ValueProbeDecisionDetail*
find_unique_decision(
    std::span<
        const probe_runner::ValueProbeDecisionDetail>
        decisions,
    std::string_view stable_id) {
    const probe_runner::ValueProbeDecisionDetail* result =
        nullptr;
    for (const auto& decision : decisions) {
        if (decision.stable_id != stable_id) {
            continue;
        }
        if (result != nullptr) {
            return nullptr;
        }
        result = &decision;
    }
    return result;
}

bool valid_decision(
    const probe_runner::ValueProbeDecisionDetail& decision,
    const probe_eval::ProbeLabel& label) {
    if (decision.stable_id != label.stable_id ||
        decision.root_deck != label.root_deck ||
        decision.selected_keys.empty() ||
        has_duplicate_keys(decision.selected_keys) ||
        (decision.deterministic_selection &&
         decision.selected_keys.size() != 1)) {
        return false;
    }
    return std::all_of(
        decision.selected_keys.begin(),
        decision.selected_keys.end(),
        [&label](const std::string& key) {
            return label_has_candidate(label, key);
        });
}

bool selection_agrees(
    const probe_runner::ValueProbeDecisionDetail& decision,
    const probe_eval::ProbeLabel& label) {
    return std::any_of(
        decision.selected_keys.begin(),
        decision.selected_keys.end(),
        [&label](const std::string& selected) {
            return contains_key(
                label.reference_best_set, selected);
        });
}

bool unique_selection(
    const probe_runner::ForceSpikeControlDecision& decision,
    std::string_view expected) {
    return decision.selected_keys.size() == 1 &&
           decision.selected_keys.front() == expected;
}

bool finite_deep_metrics(
    const probe_eval::ProbeMetricSummary& metrics) {
    const auto finite_scope =
        [](double top_one, double stable_pair,
           double regret, double brier, double mse,
           double log_loss, double bias, double ece) {
            return std::isfinite(top_one) &&
                   std::isfinite(stable_pair) &&
                   std::isfinite(regret) &&
                   std::isfinite(brier) &&
                   std::isfinite(mse) &&
                   std::isfinite(log_loss) &&
                   std::isfinite(bias) &&
                   std::isfinite(ece);
        };
    if (!finite_scope(
            metrics.top1_expected_agreement,
            metrics.stable_pair_agreement,
            metrics.mean_regret, metrics.critic_brier,
            metrics.critic_mse, metrics.critic_log_loss,
            metrics.critic_bias, metrics.critic_ece)) {
        return false;
    }
    return std::all_of(
        metrics.by_deck.begin(), metrics.by_deck.end(),
        [&finite_scope](
            const probe_eval::DeckProbeMetrics& deck) {
            return finite_scope(
                deck.top1_expected_agreement,
                deck.stable_pair_agreement,
                deck.mean_regret, deck.critic_brier,
                deck.critic_mse, deck.critic_log_loss,
                deck.critic_bias, deck.critic_ece);
        });
}

struct CommonCriticAccumulator {
    std::size_t probe_count = 0;
    double squared_error_sum = 0.0;
    double log_loss_sum = 0.0;
    double bias_sum = 0.0;
    std::array<
        std::size_t, probe_eval::kCalibrationBinCount>
        bin_counts{};
    std::array<double, probe_eval::kCalibrationBinCount>
        bin_prediction_sums{};
    std::array<double, probe_eval::kCalibrationBinCount>
        bin_reference_sums{};
};

bool is_probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

std::size_t calibration_bin(double prediction) {
    if (prediction >= 1.0) {
        return probe_eval::kCalibrationBinCount - 1;
    }
    const auto index = static_cast<std::size_t>(
        prediction *
        static_cast<double>(
            probe_eval::kCalibrationBinCount));
    return std::min(
        index, probe_eval::kCalibrationBinCount - 1);
}

void add_common_critic_observation(
    CommonCriticAccumulator& accumulator,
    double prediction, double reference) {
    const double error = prediction - reference;
    accumulator.squared_error_sum += error * error;
    accumulator.bias_sum += error;
    accumulator.log_loss_sum +=
        audit_common::soft_log_loss(
            prediction, reference);
    const std::size_t bin = calibration_bin(prediction);
    ++accumulator.bin_counts[bin];
    accumulator.bin_prediction_sums[bin] += prediction;
    accumulator.bin_reference_sums[bin] += reference;
    ++accumulator.probe_count;
}

CommonStateCriticMetrics finalize_common_critic(
    const CommonCriticAccumulator& accumulator) {
    CommonStateCriticMetrics metrics;
    metrics.probe_count = accumulator.probe_count;
    if (accumulator.probe_count == 0) {
        return metrics;
    }
    const double count =
        static_cast<double>(accumulator.probe_count);
    metrics.brier =
        accumulator.squared_error_sum / count;
    metrics.soft_log_loss =
        accumulator.log_loss_sum / count;
    metrics.signed_bias = accumulator.bias_sum / count;
    double weighted_calibration_error = 0.0;
    for (std::size_t bin = 0;
         bin < probe_eval::kCalibrationBinCount; ++bin) {
        if (accumulator.bin_counts[bin] == 0) {
            continue;
        }
        const double bin_count = static_cast<double>(
            accumulator.bin_counts[bin]);
        const double prediction_mean =
            accumulator.bin_prediction_sums[bin] /
            bin_count;
        const double reference_mean =
            accumulator.bin_reference_sums[bin] /
            bin_count;
        weighted_calibration_error +=
            bin_count *
            std::abs(prediction_mean - reference_mean);
    }
    metrics.ece = weighted_calibration_error / count;
    return metrics;
}

bool finite_common_metrics(
    const CommonStateCriticMetrics& metrics) {
    return std::isfinite(metrics.brier) &&
           std::isfinite(metrics.soft_log_loss) &&
           std::isfinite(metrics.signed_bias) &&
           std::isfinite(metrics.ece);
}

} // namespace

HeldoutGateReport evaluate_heldout_gate(
    const terminal_weight_eval::HoldoutReport& report) {
    HeldoutGateReport gate;

    std::size_t summed_records = 0;
    std::size_t summed_perspectives = 0;
    bool deck_accounting_exact = true;
    for (const auto& deck : report.by_deck) {
        deck_accounting_exact =
            deck_accounting_exact &&
            add_to(deck.records, summed_records) &&
            add_to(deck.perspectives, summed_perspectives) &&
            deck.records != 0 &&
            deck.perspectives ==
                terminal_weight_eval::
                    kHoldoutPerspectivesPerDeck;
    }
    gate.accounting_exact =
        deck_accounting_exact &&
        report.pooled.records != 0 &&
        report.pooled.records == summed_records &&
        report.pooled.perspectives ==
            kDeckCount *
                terminal_weight_eval::
                    kHoldoutPerspectivesPerDeck &&
        report.pooled.perspectives ==
            summed_perspectives &&
        report.pooled.physical_games ==
            terminal_weight_eval::kHoldoutPhysicalGames;

    const auto& pooled_comparison =
        report.pooled.treatment_comparisons[0];
    gate.inputs_finite =
        finite_estimate(pooled_comparison.brier_delta) &&
        finite_estimate(
            pooled_comparison.soft_log_loss_delta);

    for (const auto& deck : report.by_deck) {
        const auto& comparison =
            deck.treatment_comparisons[0];
        const auto& control_bias =
            deck.models[kControlModelIndex].signed_bias;
        const auto& treatment_bias =
            deck.models[kTreatmentModelIndex].signed_bias;
        gate.inputs_finite =
            gate.inputs_finite &&
            finite_estimate(comparison.brier_delta) &&
            finite_estimate(
                comparison.soft_log_loss_delta) &&
            finite_estimate(control_bias) &&
            finite_estimate(treatment_bias);
    }

    gate.pooled_losses_improved =
        pooled_comparison.brier_delta.confidence_upper_95 <
            0.0 &&
        pooled_comparison.soft_log_loss_delta
                .confidence_upper_95 <
            0.0;

    gate.every_deck_loss_guard = std::all_of(
        report.by_deck.begin(), report.by_deck.end(),
        [](const terminal_weight_eval::HoldoutScopeMetrics&
               deck) {
            const auto& comparison =
                deck.treatment_comparisons[0];
            return comparison.brier_delta.mean <=
                       kMaximumDeckLossDelta &&
                   comparison.soft_log_loss_delta.mean <=
                       kMaximumDeckLossDelta;
        });

    const auto treatment_abs_bias =
        [&report](DeckId deck) {
            return std::abs(
                report
                    .by_deck[static_cast<std::size_t>(deck)]
                    .models[kTreatmentModelIndex]
                    .signed_bias.mean);
        };
    const auto control_abs_bias =
        [&report](DeckId deck) {
            return std::abs(
                report
                    .by_deck[static_cast<std::size_t>(deck)]
                    .models[kControlModelIndex]
                    .signed_bias.mean);
        };

    gate.green_bias_strictly_shrank =
        treatment_abs_bias(DeckId::Green) <
        control_abs_bias(DeckId::Green);
    gate.blue_bias_strictly_shrank =
        treatment_abs_bias(DeckId::Blue) <
        control_abs_bias(DeckId::Blue);
    gate.ru_bias_guard =
        treatment_abs_bias(DeckId::RUAggro) <=
        std::max(
            control_abs_bias(DeckId::RUAggro),
            kRuSignedBiasFloor);

    gate.no_new_material_bias = true;
    for (const auto& deck : report.by_deck) {
        const auto& control =
            deck.models[kControlModelIndex].signed_bias;
        const auto& treatment =
            deck.models[kTreatmentModelIndex].signed_bias;
        if (estimate_has_material_bias(treatment) &&
            !(estimate_has_material_bias(control) &&
              audit_common::same_strict_sign(
                  control.mean, treatment.mean))) {
            gate.no_new_material_bias = false;
        }
    }

    record_failure(
        gate.accounting_exact,
        "held-out accounting is not 200 games/80 perspectives per deck",
        gate.failures);
    record_failure(
        gate.inputs_finite, "held-out metrics are not finite",
        gate.failures);
    record_failure(
        gate.pooled_losses_improved,
        "pooled Brier/log-loss CR1 upper bounds are not both below zero",
        gate.failures);
    record_failure(
        gate.every_deck_loss_guard,
        "a deck loss point delta exceeds +0.005",
        gate.failures);
    record_failure(
        gate.green_bias_strictly_shrank,
        "Green absolute signed bias did not strictly shrink",
        gate.failures);
    record_failure(
        gate.blue_bias_strictly_shrank,
        "Blue absolute signed bias did not strictly shrink",
        gate.failures);
    record_failure(
        gate.ru_bias_guard,
        "RU absolute signed bias exceeds its control/floor guard",
        gate.failures);
    record_failure(
        gate.no_new_material_bias,
        "treatment introduced material signed bias",
        gate.failures);

    gate.passed =
        gate.accounting_exact &&
        gate.inputs_finite &&
        gate.pooled_losses_improved &&
        gate.every_deck_loss_guard &&
        gate.green_bias_strictly_shrank &&
        gate.blue_bias_strictly_shrank &&
        gate.ru_bias_guard &&
        gate.no_new_material_bias;
    return gate;
}

bool is_stable_best_set_probe(
    const probe_eval::ProbeLabel& label) {
    if (!valid_label(label)) {
        return false;
    }

    std::vector<std::string_view> outside;
    for (const auto& candidate : label.candidates) {
        if (!contains_key(
                label.reference_best_set, candidate.key)) {
            outside.push_back(candidate.key);
        }
    }
    if (outside.empty()) {
        return false;
    }

    return std::any_of(
        label.reference_best_set.begin(),
        label.reference_best_set.end(),
        [&label, &outside](const std::string& best) {
            return std::all_of(
                outside.begin(), outside.end(),
                [&label, &best](std::string_view other) {
                    return stable_positive_pair(
                        label, best, other);
                });
        });
}

ForceSpikeSelectionGateReport
evaluate_force_spike_selection_gate(
    const probe_runner::ForceSpikePolicyControlReport&
        report) {
    ForceSpikeSelectionGateReport gate;
    gate.identities_exact =
        report.live.stable_id == kLiveForceSpikeProbeId &&
        report.payable.stable_id ==
            kPayableForceSpikeProbeId;
    gate.live_uniquely_selects_force_spike =
        unique_selection(
            report.live, kForceSpikeCandidateKey);
    gate.payable_uniquely_selects_pass =
        unique_selection(report.payable, kPassCandidateKey);
    gate.hidden_repartition_passed =
        report.hidden_repartition_passed;

    record_failure(
        gate.identities_exact,
        "Force Spike control identities do not match",
        gate.failures);
    record_failure(
        gate.live_uniquely_selects_force_spike,
        "live Force Spike control is not uniquely Force Spike",
        gate.failures);
    record_failure(
        gate.payable_uniquely_selects_pass,
        "payable Force Spike control is not uniquely Pass",
        gate.failures);
    record_failure(
        gate.hidden_repartition_passed,
        "Force Spike controls failed hidden repartition",
        gate.failures);

    gate.passed =
        gate.identities_exact &&
        gate.live_uniquely_selects_force_spike &&
        gate.payable_uniquely_selects_pass &&
        gate.hidden_repartition_passed;
    return gate;
}

CommonStateCriticReport score_common_state_critics(
    std::span<const probe_eval::ProbeLabel> labels,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        control_decisions,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        treatment_decisions) {
    CommonStateCriticReport report;
    report.accounting_exact =
        labels.size() == control_decisions.size() &&
        labels.size() == treatment_decisions.size();
    report.predictions_valid = true;

    std::array<
        CommonCriticAccumulator, kCommonStateCriticCount>
        pooled;
    std::array<
        std::array<
            CommonCriticAccumulator,
            kCommonStateCriticCount>,
        kDeckCount>
        by_deck;

    for (std::size_t label_index = 0;
         label_index < labels.size(); ++label_index) {
        const auto& label = labels[label_index];
        if (!valid_label(label)) {
            report.accounting_exact = false;
            report.predictions_valid = false;
            continue;
        }
        for (std::size_t other = label_index + 1;
             other < labels.size(); ++other) {
            if (labels[other].stable_id ==
                label.stable_id) {
                report.accounting_exact = false;
            }
        }

        const auto* control = find_unique_decision(
            control_decisions, label.stable_id);
        const auto* treatment = find_unique_decision(
            treatment_decisions, label.stable_id);
        if (decision_count(
                control_decisions, label.stable_id) != 1 ||
            decision_count(
                treatment_decisions, label.stable_id) != 1 ||
            control == nullptr || treatment == nullptr ||
            control->root_deck != label.root_deck ||
            treatment->root_deck != label.root_deck) {
            report.accounting_exact = false;
            continue;
        }
        const auto index = deck_index(label.root_deck);
        if (!index.has_value()) {
            report.accounting_exact = false;
            continue;
        }

        const std::array<double, kCommonStateCriticCount>
            predictions = {
                control->critic_prediction,
                treatment->critic_prediction,
            };
        if (!is_probability(predictions[0]) ||
            !is_probability(predictions[1])) {
            report.predictions_valid = false;
            continue;
        }
        for (std::size_t model = 0;
             model < kCommonStateCriticCount; ++model) {
            add_common_critic_observation(
                pooled[model], predictions[model],
                label.reference_value);
            add_common_critic_observation(
                by_deck[*index][model],
                predictions[model],
                label.reference_value);
        }
    }

    for (std::size_t model = 0;
         model < kCommonStateCriticCount; ++model) {
        report.pooled.models[model] =
            finalize_common_critic(pooled[model]);
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        for (std::size_t model = 0;
             model < kCommonStateCriticCount; ++model) {
            report.by_deck[deck].models[model] =
                finalize_common_critic(
                    by_deck[deck][model]);
        }
    }

    report.accounting_exact =
        report.accounting_exact &&
        report.pooled.models[kCommonStateControlIndex]
                .probe_count ==
            labels.size() &&
        report.pooled.models[kCommonStateTreatmentIndex]
                .probe_count ==
            labels.size();
    report.metrics_finite =
        report.predictions_valid;
    for (const auto& metrics : report.pooled.models) {
        report.metrics_finite =
            report.metrics_finite &&
            finite_common_metrics(metrics);
    }
    for (const auto& scope : report.by_deck) {
        for (const auto& metrics : scope.models) {
            report.metrics_finite =
                report.metrics_finite &&
                finite_common_metrics(metrics);
        }
    }
    return report;
}

DeepReferenceGateReport evaluate_deep_reference_gate(
    const probe_eval::ProbeMetricSummary& control_metrics,
    const probe_eval::ProbeMetricSummary& treatment_metrics,
    std::span<const probe_eval::ProbeLabel> labels,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        control_decisions,
    std::span<const probe_runner::ValueProbeDecisionDetail>
        treatment_decisions,
    const probe_runner::ForceSpikePolicyControlReport&
        treatment_force_spike,
    bool hidden_repartition_passed) {
    DeepReferenceGateReport gate;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        gate.by_deck[deck].root_deck =
            static_cast<DeckId>(deck);
    }

    std::array<std::size_t, kDeckCount> label_counts{};
    bool labels_valid = true;
    for (std::size_t index = 0; index < labels.size();
         ++index) {
        const auto& label = labels[index];
        labels_valid = labels_valid && valid_label(label);
        const auto index_for_deck =
            deck_index(label.root_deck);
        if (index_for_deck.has_value()) {
            ++label_counts[*index_for_deck];
        }
        for (std::size_t other = index + 1;
             other < labels.size(); ++other) {
            if (label.stable_id ==
                labels[other].stable_id) {
                labels_valid = false;
            }
        }
    }

    bool decisions_exact =
        control_decisions.size() == labels.size() &&
        treatment_decisions.size() == labels.size();
    for (const auto& label : labels) {
        const bool unique_control =
            decision_count(
                control_decisions, label.stable_id) == 1;
        const bool unique_treatment =
            decision_count(
                treatment_decisions, label.stable_id) == 1;
        decisions_exact =
            decisions_exact && unique_control &&
            unique_treatment;
        if (!unique_control || !unique_treatment) {
            continue;
        }
        decisions_exact =
            decisions_exact &&
            valid_decision(
                *find_unique_decision(
                    control_decisions, label.stable_id),
                label) &&
            valid_decision(
                *find_unique_decision(
                    treatment_decisions, label.stable_id),
                label);
    }

    bool metric_accounting =
        labels.size() == kExpectedProbeCount &&
        control_metrics.probe_count == kExpectedProbeCount &&
        treatment_metrics.probe_count ==
            kExpectedProbeCount;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        metric_accounting =
            metric_accounting &&
            label_counts[deck] ==
                kExpectedProbesPerDeck &&
            control_metrics.by_deck[deck].root_deck ==
                static_cast<DeckId>(deck) &&
            treatment_metrics.by_deck[deck].root_deck ==
                static_cast<DeckId>(deck) &&
            control_metrics.by_deck[deck].probe_count ==
                label_counts[deck] &&
            treatment_metrics.by_deck[deck].probe_count ==
                label_counts[deck];
    }
    const bool frozen_label_accounting =
        labels.size() == kExpectedProbeCount &&
        std::all_of(
            label_counts.begin(), label_counts.end(),
            [](std::size_t count) {
                return count ==
                       kExpectedProbesPerDeck;
            });
    gate.accounting_exact =
        frozen_label_accounting &&
        labels_valid && decisions_exact &&
        metric_accounting;

    gate.metrics_finite =
        finite_deep_metrics(control_metrics) &&
        finite_deep_metrics(treatment_metrics);
    gate.pooled_regret_no_worse =
        treatment_metrics.mean_regret <=
        control_metrics.mean_regret;
    gate.pooled_top_one_no_lower =
        treatment_metrics.top1_expected_agreement >=
        control_metrics.top1_expected_agreement;
    gate.every_deck_regret_guard = true;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        if (treatment_metrics.by_deck[deck].mean_regret >
            control_metrics.by_deck[deck].mean_regret +
                kMaximumDeckRegretIncrease) {
            gate.every_deck_regret_guard = false;
        }
    }

    for (const auto& label : labels) {
        const auto index = deck_index(label.root_deck);
        if (!index.has_value() ||
            !is_stable_best_set_probe(label)) {
            continue;
        }
        auto& deck = gate.by_deck[*index];
        ++deck.eligible_probes;
        const auto* control = find_unique_decision(
            control_decisions, label.stable_id);
        const auto* treatment = find_unique_decision(
            treatment_decisions, label.stable_id);
        if (control == nullptr || treatment == nullptr) {
            continue;
        }
        const bool control_agrees =
            selection_agrees(*control, label);
        const bool treatment_agrees =
            selection_agrees(*treatment, label);
        if (control_agrees) {
            ++deck.control_agreements;
        }
        if (treatment_agrees) {
            ++deck.treatment_agreements;
        }
        if (control_agrees && !treatment_agrees) {
            ++deck.lost_agreements;
        }
    }
    gate.stable_best_set_loss_guard = true;
    for (auto& deck : gate.by_deck) {
        deck.passed =
            deck.lost_agreements <=
            kMaximumStableBestSetLossesPerDeck;
        gate.stable_best_set_loss_guard =
            gate.stable_best_set_loss_guard &&
            deck.passed;
    }

    gate.required_blue_probes_exact = true;
    gate.required_blue_selections_passed = true;
    for (const auto stable_id :
         kRequiredStableBlueProbeIds) {
        const auto* label =
            find_unique_label(labels, stable_id);
        const bool probe_exact =
            label_count(labels, stable_id) == 1 &&
            label != nullptr &&
            label->root_deck == DeckId::Blue &&
            is_stable_best_set_probe(*label);
        gate.required_blue_probes_exact =
            gate.required_blue_probes_exact &&
            probe_exact;
        if (!probe_exact) {
            gate.required_blue_selections_passed = false;
            continue;
        }
        const auto* treatment = find_unique_decision(
            treatment_decisions, stable_id);
        gate.required_blue_selections_passed =
            gate.required_blue_selections_passed &&
            treatment != nullptr &&
            valid_decision(*treatment, *label) &&
            selection_agrees(*treatment, *label);
    }

    gate.hidden_repartition_passed =
        hidden_repartition_passed;
    gate.force_spike =
        evaluate_force_spike_selection_gate(
            treatment_force_spike);
    gate.common_state_critics =
        score_common_state_critics(
            labels, control_decisions,
            treatment_decisions);

    record_failure(
        gate.accounting_exact,
        "deep-reference labels/decisions/metrics do not align",
        gate.failures);
    record_failure(
        gate.metrics_finite,
        "deep-reference action metrics are not finite",
        gate.failures);
    record_failure(
        gate.pooled_regret_no_worse,
        "treatment pooled regret is worse than control",
        gate.failures);
    record_failure(
        gate.pooled_top_one_no_lower,
        "treatment pooled top-one agreement is below control",
        gate.failures);
    record_failure(
        gate.every_deck_regret_guard,
        "a deck regret increase exceeds +0.01",
        gate.failures);
    record_failure(
        gate.stable_best_set_loss_guard,
        "a deck lost more than one stable best-set agreement",
        gate.failures);
    record_failure(
        gate.required_blue_probes_exact,
        "required stable Blue probe identities are missing or unstable",
        gate.failures);
    record_failure(
        gate.required_blue_selections_passed,
        "treatment missed a required Blue reference-best set",
        gate.failures);
    record_failure(
        gate.hidden_repartition_passed,
        "deep-reference hidden repartition failed",
        gate.failures);
    record_failure(
        gate.force_spike.passed,
        "supplemental Force Spike selection gate failed",
        gate.failures);
    record_failure(
        gate.common_state_critics.accounting_exact &&
            gate.common_state_critics.predictions_valid &&
            gate.common_state_critics.metrics_finite,
        "common-state-label critic report is invalid",
        gate.failures);

    gate.passed =
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
    return gate;
}

FieldRegressionGateReport evaluate_field_regression_gate(
    const probe_runner::FieldRegressionReport& report,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    FieldRegressionGateReport gate;
    gate.metadata_exact =
        expected_fingerprints_valid(fingerprints) &&
        report.corpus_id == probes::kFieldRegressionsV1 &&
        report.reference_model_fingerprint ==
            kLearnedJointC17ParentFingerprint &&
        report.reference_worlds ==
            probe_runner::kFieldReferenceWorlds &&
        report.reference_horizon_turns ==
            probe_runner::kFieldReferenceHorizonTurns &&
        report.reference_rollouts_per_world == 1 &&
        !report.reference_blend_shallow_prior &&
        report.reference_value_continuation_epsilon == 0.0 &&
        report.reference_value_priority_residual_weight ==
            0.0 &&
        !report.reference_value_pass_dominance &&
        report.reference_value_continuation_controller ==
            LearnedContinuationController::Legacy &&
        report.hidden_repartition.passed &&
        report.hidden_repartition.policy_count == 4 &&
        report.hidden_repartition.probe_count ==
            kFieldRegressionFixtureCount &&
        report.rules_contract_passed;
    gate.fixture_count_exact =
        report.decisions.size() ==
        kFieldRegressionFixtureCount;
    gate.every_fixture_valid =
        gate.fixture_count_exact;
    gate.fixtures.reserve(report.decisions.size());

    for (std::size_t fixture = 0;
         fixture < report.decisions.size(); ++fixture) {
        const auto& decision = report.decisions[fixture];
        FieldRegressionFixtureGateReport fixture_gate{
            .stable_id = decision.stable_id,
        };
        fixture_gate.identity_exact =
            fixture < kFieldRegressionFixtureCount &&
            decision.stable_id ==
                kRequiredFieldRegressionIds[fixture] &&
            decision.root_deck ==
                expected_field_deck(fixture) &&
            decision.decision_kind ==
                expected_field_kind(fixture) &&
            field_descriptors_exact(
                decision.candidate_descriptors,
                fixture);
        fixture_gate.reference_valid =
            fixture_gate.identity_exact &&
            field_samples_exact(
                decision.reference_samples, fixture,
                probe_runner::kFieldReferenceWorlds) &&
            field_accounting_exact(
                decision.reference_accounting,
                expected_field_candidate_count(fixture),
                probe_runner::kFieldReferenceWorlds) &&
            field_forced_consequences_exact(
                decision.forced_consequences,
                fixture);
        fixture_gate.deployment_valid =
            fixture_gate.identity_exact &&
            field_policy_exact(
                decision.parent, fixture,
                kFrozenC16EvidencePolicyToken,
                kLearnedJointC17ParentFingerprint,
                false,
                LearnedContinuationController::Legacy) &&
            field_policy_exact(
                decision.control, fixture,
                kLearnedJointC17ControlPolicyToken,
                fingerprints.control, false,
                LearnedContinuationController::Legacy) &&
            field_policy_exact(
                decision.treatment, fixture,
                kLearnedJointC17TreatmentPolicyToken,
                fingerprints.treatment, true,
                LearnedContinuationController::
                    PublicStackPassV1);

        if (fixture_gate.reference_valid &&
            fixture_gate.deployment_valid) {
            try {
                const auto label =
                    probe_eval::make_probe_label(
                        decision.stable_id,
                        decision.root_deck,
                        decision.reference_samples);
                fixture_gate.stable_reference_best_set =
                    is_stable_best_set_probe(label);
                if (fixture_gate
                        .stable_reference_best_set) {
                    ++gate.stable_fixture_count;
                    fixture_gate.control_agrees =
                        selection_intersects(
                            decision.control.selected_keys,
                            label.reference_best_set);
                    fixture_gate.treatment_agrees =
                        selection_intersects(
                            decision.treatment.selected_keys,
                            label.reference_best_set);
                    gate.control_agreements +=
                        fixture_gate.control_agrees ? 1U : 0U;
                    gate.treatment_agreements +=
                        fixture_gate.treatment_agrees ? 1U : 0U;
                    fixture_gate
                        .treatment_lost_control_agreement =
                        fixture_gate.control_agrees &&
                        !fixture_gate.treatment_agrees;
                    gate.treatment_losses +=
                        fixture_gate
                                .treatment_lost_control_agreement
                            ? 1U
                            : 0U;
                    gate.treatment_gains +=
                        !fixture_gate.control_agrees &&
                                fixture_gate.treatment_agrees
                            ? 1U
                            : 0U;
                }
            } catch (const std::exception&) {
                fixture_gate.reference_valid = false;
            }
        }
        gate.every_fixture_valid =
            gate.every_fixture_valid &&
            fixture_gate.identity_exact &&
            fixture_gate.reference_valid &&
            fixture_gate.deployment_valid;
        gate.fixtures.push_back(
            std::move(fixture_gate));
    }

    record_failure(
        gate.metadata_exact,
        "field-regression corpus/reference/rules/hidden metadata is not exact",
        gate.failures);
    record_failure(
        gate.fixture_count_exact,
        "field-regression fixture count is not six",
        gate.failures);
    record_failure(
        gate.every_fixture_valid,
        "a field-regression fixture, label, or deployment is invalid",
        gate.failures);
    record_failure(
        gate.treatment_losses == 0,
        "treatment lost a stable control field agreement",
        gate.failures);
    gate.passed =
        gate.metadata_exact &&
        gate.fixture_count_exact &&
        gate.every_fixture_valid &&
        gate.treatment_losses == 0;
    return gate;
}

DirectGameplayGateReport evaluate_direct_gameplay_panel(
    const DirectPanelEvidence& evidence,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    const auto& summary = evidence.summary;
    const auto counts = benchmark_count_summary(summary);
    DirectGameplayGateReport gate;
    gate.identity_exact =
        evidence.role !=
            DirectPanelRole::TreatmentVsHandcodedFixedSeed &&
        direct_panel_identity_exact(evidence, fingerprints);
    gate.accounting_exact = benchmark_panel_schedule_exact(
        summary, kDirectPanelRepetitions,
        kDirectPanelGames, kDirectPanelGamesPerDeck,
        kDirectPanelDiagonalGames,
        kDirectPanelOffDiagonalGames,
        kDirectPanelQuadrantGames);
    gate.clustered_estimate_valid =
        clustered_estimate_valid(
            summary.challenger_quartet_cr1,
            counts.challenger, kDirectPanelQuartets,
            kDirectPanelGames);

    const auto win_rate = win_rate_percent(counts.challenger);
    const auto wilson =
        wilson_lower_95_percent(counts.challenger);
    gate.rates_finite =
        win_rate.has_value() && wilson.has_value();
    if (win_rate.has_value()) {
        gate.challenger_win_rate_percent = *win_rate;
    }
    if (wilson.has_value()) {
        gate.wilson_lower_95_percent = *wilson;
    }
    gate.aggregate_strict_win =
        outcomes_valid(counts.challenger) &&
        counts.challenger.wins > counts.challenger.losses;
    gate.wilson_lower_above_half =
        wilson.has_value() && *wilson > 50.0;
    gate.every_challenger_deck_strict_win = true;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        gate.challenger_deck_strict_wins[deck] =
            outcomes_valid(counts.challenger_decks[deck]) &&
            counts.challenger_decks[deck].wins >
                counts.challenger_decks[deck].losses;
        gate.every_challenger_deck_strict_win =
            gate.every_challenger_deck_strict_win &&
            gate.challenger_deck_strict_wins[deck];
    }

    record_failure(
        gate.identity_exact,
        "direct panel seed or policy/model identity is not exact",
        gate.failures);
    record_failure(
        gate.accounting_exact,
        "direct panel schedule accounting is not exact",
        gate.failures);
    record_failure(
        gate.clustered_estimate_valid,
        "direct panel clustered quartet estimate is invalid",
        gate.failures);
    record_failure(
        gate.rates_finite,
        "direct panel rates are not finite",
        gate.failures);
    record_failure(
        gate.aggregate_strict_win,
        "direct panel challenger does not have more wins than losses",
        gate.failures);
    record_failure(
        gate.wilson_lower_above_half,
        "direct panel Wilson lower bound is not above 50%",
        gate.failures);
    record_failure(
        gate.every_challenger_deck_strict_win,
        "a challenger deck does not have more wins than losses",
        gate.failures);
    gate.passed =
        gate.identity_exact &&
        gate.accounting_exact &&
        gate.clustered_estimate_valid &&
        gate.rates_finite &&
        gate.aggregate_strict_win &&
        gate.wilson_lower_above_half &&
        gate.every_challenger_deck_strict_win;
    return gate;
}

FixedSeedPanelGateReport evaluate_fixed_seed_panel(
    const DirectPanelEvidence& evidence,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    const auto& summary = evidence.summary;
    const auto counts = benchmark_count_summary(summary);
    FixedSeedPanelGateReport gate;
    gate.identity_exact =
        evidence.role ==
            DirectPanelRole::TreatmentVsHandcodedFixedSeed &&
        direct_panel_identity_exact(evidence, fingerprints);
    gate.accounting_exact = benchmark_panel_schedule_exact(
        summary, kFixedSeedPanelRepetitions,
        kFixedSeedPanelGames,
        kFixedSeedPanelGamesPerDeck,
        kFixedSeedPanelDiagonalGames,
        kFixedSeedPanelOffDiagonalGames,
        kFixedSeedPanelQuadrantGames);
    gate.clustered_estimate_valid =
        clustered_estimate_valid(
            summary.challenger_quartet_cr1,
            counts.challenger, kFixedSeedPanelQuartets,
            kFixedSeedPanelGames);
    gate.aggregate_non_losing =
        outcomes_valid(counts.challenger) &&
        counts.challenger.wins >= counts.challenger.losses;
    record_failure(
        gate.identity_exact,
        "fixed-seed panel seed or policy/model identity is not exact",
        gate.failures);
    record_failure(
        gate.accounting_exact,
        "fixed-seed panel schedule accounting is not exact",
        gate.failures);
    record_failure(
        gate.clustered_estimate_valid,
        "fixed-seed panel clustered quartet estimate is invalid",
        gate.failures);
    record_failure(
        gate.aggregate_non_losing,
        "fixed-seed panel challenger has fewer wins than losses",
        gate.failures);
    gate.passed =
        gate.identity_exact &&
        gate.accounting_exact &&
        gate.clustered_estimate_valid &&
        gate.aggregate_non_losing;
    return gate;
}

FixedSeedPanelSetGateReport evaluate_fixed_seed_panel_set(
    std::span<const DirectPanelEvidence> panels,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    FixedSeedPanelSetGateReport gate;
    gate.panel_count_exact =
        panels.size() == kFixedSeedPanelCount;
    gate.seeds_exact = gate.panel_count_exact;
    gate.panels.reserve(panels.size());
    for (std::size_t index = 0;
         index < panels.size(); ++index) {
        if (index >= kFixedSeedPanelSeeds.size() ||
            panels[index].summary.evaluation_seed !=
                kFixedSeedPanelSeeds[index]) {
            gate.seeds_exact = false;
        }
        gate.panels.push_back(
            evaluate_fixed_seed_panel(
                panels[index], fingerprints));
    }
    gate.every_panel_passed = std::all_of(
        gate.panels.begin(), gate.panels.end(),
        [](const FixedSeedPanelGateReport& panel) {
            return panel.passed;
        });
    record_failure(
        gate.panel_count_exact,
        "fixed-seed panel count is not eight",
        gate.failures);
    record_failure(
        gate.seeds_exact,
        "fixed-seed panels are missing, duplicated, or reordered",
        gate.failures);
    record_failure(
        gate.every_panel_passed,
        "a fixed-seed panel failed its non-losing gate",
        gate.failures);
    gate.passed =
        gate.panel_count_exact &&
        gate.seeds_exact &&
        gate.every_panel_passed;
    return gate;
}

std::optional<BenchmarkCountSummary> merge_final_direct_panels(
    const DirectPanelEvidence& direct_panel,
    std::span<const DirectPanelEvidence> fixed_seed_panels,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    if (fixed_seed_panels.size() !=
            kFixedSeedPanelCount ||
        direct_panel.role !=
            DirectPanelRole::TreatmentVsHandcodedPrimary ||
        !direct_panel_identity_exact(
            direct_panel, fingerprints)) {
        return std::nullopt;
    }

    const auto direct_counts =
        benchmark_count_summary(direct_panel.summary);
    if (!benchmark_panel_schedule_exact(
            direct_panel.summary,
            kDirectPanelRepetitions,
            kDirectPanelGames,
            kDirectPanelGamesPerDeck,
            kDirectPanelDiagonalGames,
            kDirectPanelOffDiagonalGames,
            kDirectPanelQuadrantGames) ||
        !clustered_estimate_valid(
            direct_panel.summary.challenger_quartet_cr1,
            direct_counts.challenger,
            kDirectPanelQuartets,
            kDirectPanelGames)) {
        return std::nullopt;
    }

    BenchmarkCountSummary merged;
    if (!merge_benchmark_counts(
            direct_counts, merged)) {
        return std::nullopt;
    }
    for (std::size_t index = 0;
         index < fixed_seed_panels.size(); ++index) {
        const auto& panel = fixed_seed_panels[index];
        if (panel.role !=
                DirectPanelRole::
                    TreatmentVsHandcodedFixedSeed ||
            panel.summary.evaluation_seed !=
                kFixedSeedPanelSeeds[index] ||
            !direct_panel_identity_exact(
                panel, fingerprints)) {
            return std::nullopt;
        }
        const auto panel_counts =
            benchmark_count_summary(panel.summary);
        if (!benchmark_panel_schedule_exact(
                panel.summary,
                kFixedSeedPanelRepetitions,
                kFixedSeedPanelGames,
                kFixedSeedPanelGamesPerDeck,
                kFixedSeedPanelDiagonalGames,
                kFixedSeedPanelOffDiagonalGames,
                kFixedSeedPanelQuadrantGames) ||
            !clustered_estimate_valid(
                panel.summary.challenger_quartet_cr1,
                panel_counts.challenger,
                kFixedSeedPanelQuartets,
                kFixedSeedPanelGames) ||
            !merge_benchmark_counts(
                panel_counts, merged)) {
            return std::nullopt;
        }
    }
    if (!benchmark_schedule_exact(
            merged, kFinalDirectPanelCount,
            kFinalDirectRepetitions,
            kFinalDirectGames,
            kFinalDirectGamesPerDeck,
            kFinalDirectDiagonalGames,
            kFinalDirectOffDiagonalGames,
            kFinalDirectQuadrantGames)) {
        return std::nullopt;
    }
    return merged;
}

FinalDirectPoolGateReport evaluate_final_direct_pool(
    const BenchmarkCountSummary& summary) {
    FinalDirectPoolGateReport gate;
    gate.accounting_exact = benchmark_schedule_exact(
        summary, kFinalDirectPanelCount,
        kFinalDirectRepetitions,
        kFinalDirectGames,
        kFinalDirectGamesPerDeck,
        kFinalDirectDiagonalGames,
        kFinalDirectOffDiagonalGames,
        kFinalDirectQuadrantGames);

    const auto win_rate = win_rate_percent(summary.challenger);
    const auto wilson =
        wilson_lower_95_percent(summary.challenger);
    gate.rates_finite =
        win_rate.has_value() && wilson.has_value();
    if (win_rate.has_value()) {
        gate.challenger_win_rate_percent = *win_rate;
    }
    if (wilson.has_value()) {
        gate.wilson_lower_95_percent = *wilson;
    }
    gate.aggregate_strict_win =
        outcomes_valid(summary.challenger) &&
        summary.challenger.wins >
            summary.challenger.losses;
    gate.wilson_lower_above_half =
        wilson.has_value() && *wilson > 50.0;
    gate.every_challenger_deck_strict_win = true;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        gate.challenger_deck_strict_wins[deck] =
            outcomes_valid(summary.challenger_decks[deck]) &&
            summary.challenger_decks[deck].wins >
                summary.challenger_decks[deck].losses;
        gate.every_challenger_deck_strict_win =
            gate.every_challenger_deck_strict_win &&
            gate.challenger_deck_strict_wins[deck];
    }

    record_failure(
        gate.accounting_exact,
        "final direct pool schedule accounting is not exact",
        gate.failures);
    record_failure(
        gate.rates_finite,
        "final direct pool rates are not finite",
        gate.failures);
    record_failure(
        gate.aggregate_strict_win,
        "final direct pool challenger does not have more wins than losses",
        gate.failures);
    record_failure(
        gate.wilson_lower_above_half,
        "final direct pool Wilson lower bound is not above 50%",
        gate.failures);
    record_failure(
        gate.every_challenger_deck_strict_win,
        "a final-pool challenger deck does not have more wins than losses",
        gate.failures);
    gate.passed =
        gate.accounting_exact &&
        gate.rates_finite &&
        gate.aggregate_strict_win &&
        gate.wilson_lower_above_half &&
        gate.every_challenger_deck_strict_win;
    return gate;
}

FinalDirectGateReport evaluate_final_direct_gate(
    const DirectPanelEvidence& primary,
    std::span<const DirectPanelEvidence> fixed_seed_panels,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    FinalDirectGateReport gate;
    gate.primary =
        evaluate_direct_gameplay_panel(primary, fingerprints);
    gate.fixed_seed_panels =
        evaluate_fixed_seed_panel_set(
            fixed_seed_panels, fingerprints);
    const auto merged = merge_final_direct_panels(
        primary, fixed_seed_panels, fingerprints);
    gate.merge_succeeded = merged.has_value();
    if (merged.has_value()) {
        gate.pooled = evaluate_final_direct_pool(*merged);
    }
    record_failure(
        gate.primary.passed,
        "primary Handcoded panel failed",
        gate.failures);
    record_failure(
        gate.fixed_seed_panels.passed,
        "fixed-seed panel set failed",
        gate.failures);
    record_failure(
        gate.merge_succeeded,
        "final direct panels could not be pooled exactly",
        gate.failures);
    record_failure(
        gate.merge_succeeded && gate.pooled.passed,
        "mandatory 4,440-game final direct pool failed",
        gate.failures);
    gate.passed =
        gate.primary.passed &&
        gate.fixed_seed_panels.passed &&
        gate.merge_succeeded &&
        gate.pooled.passed;
    return gate;
}

MixedFieldGateReport evaluate_mixed_field_pool(
    std::span<const MixedFieldSeedPanelEvidence> panels,
    const JointC17ExpectedModelFingerprints& fingerprints) {
    MixedFieldGateReport gate;
    gate.panel_count_exact =
        panels.size() == kFixedSeedPanelCount;
    gate.seeds_exact = gate.panel_count_exact;
    gate.policy_identity_exact =
        gate.panel_count_exact &&
        expected_fingerprints_valid(fingerprints);
    gate.accounting_exact = gate.panel_count_exact;

    std::array<
        std::array<DeckSimulationStats, kBotKindCount>,
        kDeckCount>
        pooled{};
    for (std::size_t panel = 0;
         panel < panels.size(); ++panel) {
        const auto& evidence = panels[panel];
        const auto& summary = evidence.summary;
        if (panel >= kFixedSeedPanelSeeds.size() ||
            summary.evaluation_seed !=
                kFixedSeedPanelSeeds[panel]) {
            gate.seeds_exact = false;
        }
        if (summary.learned_training_seed !=
                kDefaultLearnedTrainingSeed ||
            !summary.effective_learned_bot.has_value() ||
            !treatment_identity_exact(
                *summary.effective_learned_bot,
                summary.effective_learned_model_fingerprint,
                evidence.learned_policy, fingerprints)) {
            gate.policy_identity_exact = false;
        }
        if (!mixed_panel_accounting_exact(summary)) {
            gate.accounting_exact = false;
        }
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            for (std::size_t bot = 0;
                 bot < kBotKindCount; ++bot) {
                if (!add_deck_stats(
                        summary.deck_bots[deck][bot],
                        pooled[deck][bot])) {
                    gate.accounting_exact = false;
                }
            }
        }
    }

    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        for (std::size_t bot = 0;
             bot < kBotKindCount; ++bot) {
            const auto& stats = pooled[deck][bot];
            if (!deck_stats_valid(stats) ||
                stats.games !=
                    kMixedFieldGamesPerDeckPolicy ||
                stats.on_play_games !=
                    kMixedFieldPlayDrawGamesPerDeckPolicy ||
                stats.on_draw_games !=
                    kMixedFieldPlayDrawGamesPerDeckPolicy) {
                gate.accounting_exact = false;
            }
        }
    }

    gate.rates_finite = true;
    gate.learned_lift_best_on_every_deck = true;
    const auto random_index =
        static_cast<std::size_t>(BotKind::Random);
    const auto learned_index =
        static_cast<std::size_t>(BotKind::Learned);
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        auto& deck_gate = gate.by_deck[deck];
        deck_gate.deck = static_cast<DeckId>(deck);
        std::array<double, kBotKindCount> rates{};
        bool deck_rates_finite = true;
        for (std::size_t bot = 0;
             bot < kBotKindCount; ++bot) {
            const auto rate = win_rate_percent(
                outcome_counts(pooled[deck][bot]));
            if (!rate.has_value()) {
                deck_rates_finite = false;
                continue;
            }
            rates[bot] = *rate;
        }
        deck_gate.rates_finite = deck_rates_finite;
        gate.rates_finite =
            gate.rates_finite && deck_rates_finite;
        if (!deck_rates_finite) {
            deck_gate.learned_lift_is_best = false;
            gate.learned_lift_best_on_every_deck = false;
            continue;
        }

        deck_gate.random_win_rate_percent =
            rates[random_index];
        deck_gate.learned_win_rate_percent =
            rates[learned_index];
        deck_gate.learned_lift_percentage_points =
            rates[learned_index] - rates[random_index];
        deck_gate.best_other = BotKind::Random;
        deck_gate.best_other_lift_percentage_points = 0.0;
        deck_gate.learned_lift_is_best = true;
        for (std::size_t bot = 0;
             bot < kBotKindCount; ++bot) {
            if (bot == learned_index) {
                continue;
            }
            const double lift =
                rates[bot] - rates[random_index];
            if (lift >
                deck_gate.best_other_lift_percentage_points) {
                deck_gate.best_other_lift_percentage_points =
                    lift;
                deck_gate.best_other =
                    static_cast<BotKind>(bot);
            }
            if (deck_gate.learned_lift_percentage_points +
                    kMixedFieldLiftTolerance <
                lift) {
                deck_gate.learned_lift_is_best = false;
            }
        }
        gate.learned_lift_best_on_every_deck =
            gate.learned_lift_best_on_every_deck &&
            deck_gate.learned_lift_is_best;
    }
    record_failure(
        gate.panel_count_exact,
        "mixed-field panel count is not eight",
        gate.failures);
    record_failure(
        gate.seeds_exact,
        "mixed-field seeds are missing, duplicated, or reordered",
        gate.failures);
    record_failure(
        gate.policy_identity_exact,
        "mixed-field frozen treatment identity is not exact",
        gate.failures);
    record_failure(
        gate.accounting_exact,
        "mixed-field per-seed or pooled accounting is not exact",
        gate.failures);
    record_failure(
        gate.rates_finite,
        "mixed-field deck-policy rates are not finite",
        gate.failures);
    record_failure(
        gate.learned_lift_best_on_every_deck,
        "Learned lift is not best on every deck",
        gate.failures);
    gate.passed =
        gate.panel_count_exact &&
        gate.seeds_exact &&
        gate.policy_identity_exact &&
        gate.accounting_exact &&
        gate.rates_finite &&
        gate.learned_lift_best_on_every_deck;
    return gate;
}

StageDecision evaluation_stage_decision(
    const StageOutcomes& outcomes) {
    StageDecision decision;
    decision.run_deep_reference =
        outcomes.heldout_passed;
    const bool deep_reference_passed =
        decision.run_deep_reference &&
        outcomes.deep_reference_passed.value_or(false);

    decision.run_field_regression =
        deep_reference_passed;
    const bool field_regression_passed =
        decision.run_field_regression &&
        outcomes.field_regression_passed.value_or(false);

    decision.run_treatment_vs_control =
        field_regression_passed;
    const bool control_passed =
        decision.run_treatment_vs_control &&
        outcomes.treatment_vs_control_passed.value_or(false);

    decision.run_treatment_vs_parent = control_passed;
    const bool parent_passed =
        decision.run_treatment_vs_parent &&
        outcomes.treatment_vs_parent_passed.value_or(false);

    decision.run_treatment_vs_handcoded = parent_passed;
    const bool handcoded_passed =
        decision.run_treatment_vs_handcoded &&
        outcomes.treatment_vs_handcoded_passed.value_or(false);

    decision.run_fixed_seed_panel = handcoded_passed;
    const bool fixed_panel_passed =
        decision.run_fixed_seed_panel &&
        outcomes.fixed_seed_panel_passed.value_or(false);

    decision.run_final_direct_pool = fixed_panel_passed;
    const bool final_direct_pool_passed =
        decision.run_final_direct_pool &&
        outcomes.final_direct_pool_passed.value_or(false);

    decision.run_mixed_field = final_direct_pool_passed;
    decision.complete =
        decision.run_mixed_field &&
        outcomes.mixed_field_passed.has_value();
    decision.passed =
        decision.complete &&
        *outcomes.mixed_field_passed;
    return decision;
}

} // namespace old_school::joint_c17_eval
