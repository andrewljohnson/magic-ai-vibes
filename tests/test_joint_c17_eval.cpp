#include "old_school/joint_c17_eval.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace joint = old_school::joint_c17_eval;
namespace probe = old_school::probe_eval;
namespace runner = old_school::probe_runner;
namespace terminal = old_school::terminal_weight_eval;
using old_school::BotBenchmarkSummary;
using old_school::BotKind;
using old_school::DeckId;
using old_school::TournamentSummary;

const joint::JointC17ExpectedModelFingerprints kFingerprints{
    .control = std::string(64, 'a'),
    .treatment = std::string(64, 'b'),
};

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
                  << " joint-C17 evaluator tests passed\n";
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

void expect_near(
    double actual, double expected, double tolerance,
    std::string_view message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) + ": expected " +
            std::to_string(expected) + ", got " +
            std::to_string(actual));
    }
}

terminal::ClusteredEstimate estimate(
    double mean, double lower, double upper) {
    return {
        .records = 20,
        .clusters = 10,
        .mean = mean,
        .standard_error = 0.001,
        .confidence_lower_95 = lower,
        .confidence_upper_95 = upper,
    };
}

terminal::HoldoutReport passing_holdout_report() {
    terminal::HoldoutReport report;
    report.pooled.records = 10;
    report.pooled.perspectives =
        old_school::kDeckCount *
        terminal::kHoldoutPerspectivesPerDeck;
    report.pooled.physical_games =
        terminal::kHoldoutPhysicalGames;
    report.pooled.treatment_comparisons[0].brier_delta =
        estimate(-0.01, -0.02, -0.001);
    report.pooled.treatment_comparisons[0]
        .soft_log_loss_delta =
        estimate(-0.02, -0.03, -0.001);

    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& scope = report.by_deck[deck];
        scope.records = 2;
        scope.perspectives =
            terminal::kHoldoutPerspectivesPerDeck;
        scope.treatment_comparisons[0].brier_delta =
            estimate(
                joint::kMaximumDeckLossDelta,
                -0.01, 0.02);
        scope.treatment_comparisons[0]
            .soft_log_loss_delta =
            estimate(
                joint::kMaximumDeckLossDelta,
                -0.01, 0.02);
        scope.models[1].signed_bias =
            estimate(0.02, -0.01, 0.05);
        scope.models[2].signed_bias =
            estimate(0.01, -0.01, 0.03);
    }
    report
        .by_deck[static_cast<std::size_t>(DeckId::RUAggro)]
        .models[1]
        .signed_bias =
        estimate(0.005, -0.01, 0.02);
    report
        .by_deck[static_cast<std::size_t>(DeckId::RUAggro)]
        .models[2]
        .signed_bias =
        estimate(
            joint::kRuSignedBiasFloor, -0.01, 0.03);
    return report;
}

probe::ProbeLabel stable_label(
    std::string stable_id, DeckId deck,
    double delta = 0.04, double paired_se = 0.01,
    bool reverse_pair = false) {
    probe::ProbeLabel label{
        .stable_id = std::move(stable_id),
        .root_deck = deck,
        .candidates =
            {
                {
                    .key = "best",
                    .q = 0.60,
                    .standard_error = 0.01,
                },
                {
                    .key = "other",
                    .q = 0.60 - delta,
                    .standard_error = 0.01,
                },
            },
        .pairs = {},
        .reference_best_set = {"best"},
        .reference_value = 0.60,
    };
    if (reverse_pair) {
        label.pairs.push_back({
            .first = "other",
            .second = "best",
            .delta_q = -delta,
            .paired_standard_error = paired_se,
        });
    } else {
        label.pairs.push_back({
            .first = "best",
            .second = "other",
            .delta_q = delta,
            .paired_standard_error = paired_se,
        });
    }
    return label;
}

runner::ValueProbeDecisionDetail decision_for(
    const probe::ProbeLabel& label, std::string selected) {
    return {
        .stable_id = label.stable_id,
        .root_deck = label.root_deck,
        .selected_keys = {std::move(selected)},
        .deterministic_selection = true,
        .reference_best_set = label.reference_best_set,
        .critic_prediction = 0.50,
    };
}

std::vector<probe::ProbeLabel> passing_labels() {
    std::vector<probe::ProbeLabel> labels;
    for (const auto stable_id :
         joint::kRequiredStableBlueProbeIds) {
        labels.push_back(stable_label(
            std::string(stable_id), DeckId::Blue));
    }
    for (std::size_t index = 0; index < 4; ++index) {
        labels.push_back(stable_label(
            "green.stable.v" + std::to_string(index + 1),
            DeckId::Green));
        labels.push_back(stable_label(
            "red.stable.v" + std::to_string(index + 1),
            DeckId::Red));
        labels.push_back(stable_label(
            "white.stable.v" + std::to_string(index + 1),
            DeckId::White));
        labels.push_back(stable_label(
            "ru.stable.v" + std::to_string(index + 1),
            DeckId::RUAggro));
    }
    return labels;
}

std::vector<runner::ValueProbeDecisionDetail>
passing_decisions(
    const std::vector<probe::ProbeLabel>& labels) {
    std::vector<runner::ValueProbeDecisionDetail> decisions;
    decisions.reserve(labels.size());
    for (const auto& label : labels) {
        decisions.push_back(decision_for(label, "best"));
    }
    return decisions;
}

probe::ProbeMetricSummary metrics_for(
    const std::vector<probe::ProbeLabel>& labels,
    double top_one = 0.90, double regret = 0.02) {
    probe::ProbeMetricSummary metrics{
        .probe_count = labels.size(),
        .top1_expected_agreement = top_one,
        .mean_regret = regret,
    };
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto& deck_metrics = metrics.by_deck[deck];
        deck_metrics.root_deck =
            static_cast<DeckId>(deck);
        deck_metrics.probe_count =
            static_cast<std::size_t>(std::count_if(
                labels.begin(), labels.end(),
                [deck](const probe::ProbeLabel& label) {
                    return static_cast<std::size_t>(
                               label.root_deck) == deck;
                }));
        deck_metrics.top1_expected_agreement = top_one;
        deck_metrics.mean_regret = regret;
    }
    return metrics;
}

runner::ForceSpikePolicyControlReport
passing_force_spike_report() {
    runner::ForceSpikePolicyControlReport report;
    report.live.stable_id =
        std::string(joint::kLiveForceSpikeProbeId);
    report.live.selected_keys = {
        std::string(joint::kForceSpikeCandidateKey)};
    report.payable.stable_id =
        std::string(joint::kPayableForceSpikeProbeId);
    report.payable.selected_keys = {
        std::string(joint::kPassCandidateKey)};
    report.hidden_repartition_passed = true;
    return report;
}

joint::DeepReferenceGateReport passing_deep_gate() {
    const auto labels = passing_labels();
    const auto control = passing_decisions(labels);
    const auto treatment = passing_decisions(labels);
    const auto control_metrics =
        metrics_for(labels, 0.90, 0.02);
    const auto treatment_metrics =
        metrics_for(labels, 0.90, 0.02);
    return joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, control,
        treatment, passing_force_spike_report(), true);
}

template <typename Destination, typename Source>
void add_stats(
    Destination& destination, const Source& source,
    bool invert = false) {
    destination.games += source.games;
    destination.wins +=
        invert ? source.losses : source.wins;
    destination.losses +=
        invert ? source.wins : source.losses;
    destination.draws += source.draws;
}

old_school::BotConfig learned_bot(bool treatment) {
    old_school::BotConfig bot;
    bot.kind = BotKind::Learned;
    bot.learned_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    bot.rollouts_per_action = 8;
    bot.value_pass_dominance = treatment;
    bot.value_continuation_controller =
        treatment
            ? old_school::LearnedContinuationController::
                  PublicStackPassV1
            : old_school::LearnedContinuationController::Legacy;
    bot.training_games = 800;
    return bot;
}

old_school::BotConfig handcoded_bot() {
    old_school::BotConfig bot;
    bot.kind = BotKind::Handcrafted;
    bot.rollouts_per_action = 1;
    bot.training_games = 800;
    return bot;
}

joint::PolicyRecipeEvidence learned_recipe(bool treatment) {
    return {
        .policy_token =
            std::string(
                treatment
                    ? old_school::
                          kLearnedJointC17TreatmentPolicyToken
                    : old_school::
                          kLearnedJointC17ControlPolicyToken),
        .horizon_turns =
            old_school::kLearnedValueSearchHorizonTurns,
        .blend_shallow_prior = true,
    };
}

joint::PolicyRecipeEvidence parent_recipe() {
    return {
        .policy_token =
            std::string(
                joint::kFrozenC16EvidencePolicyToken),
        .horizon_turns =
            old_school::kLearnedValueSearchHorizonTurns,
        .blend_shallow_prior = true,
    };
}

joint::PolicyRecipeEvidence handcoded_recipe() {
    return {
        .policy_token =
            std::string(
                joint::kHandcodedEvidencePolicyToken),
        .horizon_turns = 0,
        .blend_shallow_prior = false,
    };
}

void distribute_quadrants(
    const std::array<
        old_school::DeckSimulationStats,
        old_school::kDeckCount>& decks,
    std::size_t games_per_quadrant,
    joint::OutcomeQuadrants& quadrants,
    std::array<
        old_school::DeckSimulationStats,
        old_school::kDeckCount>& mutable_decks) {
    quadrants = {};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        std::size_t wins = decks[deck].wins;
        std::size_t losses = decks[deck].losses;
        std::size_t draws = decks[deck].draws;
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                auto& cell =
                    quadrants[deck][seat][play_draw];
                cell.games = games_per_quadrant;
                cell.wins =
                    std::min(wins, games_per_quadrant);
                wins -= cell.wins;
                const std::size_t after_wins =
                    games_per_quadrant - cell.wins;
                cell.losses =
                    std::min(losses, after_wins);
                losses -= cell.losses;
                cell.draws =
                    games_per_quadrant -
                    cell.wins - cell.losses;
                draws -= cell.draws;
            }
        }
        expect(
            wins == 0 && losses == 0 && draws == 0,
            "quadrant distribution");
        auto& stats = mutable_decks[deck];
        stats.on_play_games = 2 * games_per_quadrant;
        stats.on_draw_games = 2 * games_per_quadrant;
        stats.on_play_wins =
            quadrants[deck][0][0].wins +
            quadrants[deck][1][0].wins;
        stats.on_draw_wins =
            quadrants[deck][0][1].wins +
            quadrants[deck][1][1].wins;
    }
}

void recompute_benchmark_totals(
    BotBenchmarkSummary& summary) {
    summary.challenger_stats = {};
    summary.baseline_stats = {};
    summary.challenger_decks = {};
    summary.baseline_decks = {};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount; ++opponent) {
            const auto& cell =
                summary.challenger_deck_matchups
                    [deck][opponent];
            add_stats(
                summary.challenger_decks[deck], cell);
            add_stats(summary.challenger_stats, cell);
            add_stats(
                summary.baseline_decks[opponent],
                cell, true);
            add_stats(summary.baseline_stats, cell, true);
        }
    }
    summary.total_games = summary.challenger_stats.games;
    const std::size_t games_per_quadrant =
        summary.challenger_decks[0].games / 4;
    const auto challenger_decks = summary.challenger_decks;
    const auto baseline_decks = summary.baseline_decks;
    distribute_quadrants(
        challenger_decks, games_per_quadrant,
        summary.challenger_outcome_quadrants,
        summary.challenger_decks);
    distribute_quadrants(
        baseline_decks, games_per_quadrant,
        summary.baseline_outcome_quadrants,
        summary.baseline_decks);
    const double mean =
        (static_cast<double>(
             summary.challenger_stats.wins) +
         0.5 * static_cast<double>(
                   summary.challenger_stats.draws)) /
        static_cast<double>(summary.total_games);
    constexpr double standard_error = 0.01;
    constexpr double z = 1.959963984540054;
    summary.challenger_quartet_cr1 = {
        .clusters = summary.total_games / 4,
        .records = summary.total_games,
        .mean = mean,
        .standard_error = standard_error,
        .confidence_low_95 =
            mean - z * standard_error,
        .confidence_high_95 =
            mean + z * standard_error,
    };
}

BotBenchmarkSummary benchmark_fixture(
    std::size_t repetitions,
    std::size_t diagonal_wins,
    std::size_t off_diagonal_wins) {
    BotBenchmarkSummary summary;
    summary.repetitions_per_deck_pairing = repetitions;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount; ++opponent) {
            auto& cell =
                summary.challenger_deck_matchups
                    [deck][opponent];
            cell.games =
                (deck == opponent ? 4U : 2U) *
                repetitions;
            cell.wins =
                deck == opponent
                    ? diagonal_wins
                    : off_diagonal_wins;
            cell.losses = cell.games - cell.wins;
        }
    }
    recompute_benchmark_totals(summary);
    return summary;
}

joint::DirectPanelEvidence panel_evidence(
    joint::DirectPanelRole role,
    BotBenchmarkSummary summary,
    std::size_t fixed_seed_index = 0) {
    joint::DirectPanelEvidence evidence{
        .role = role,
        .challenger_policy = learned_recipe(true),
        .summary = std::move(summary),
    };
    auto& panel = evidence.summary;
    panel.challenger = learned_bot(true);
    panel.challenger_model_fingerprint =
        kFingerprints.treatment;
    panel.learned_training_seed =
        old_school::kDefaultLearnedTrainingSeed;
    switch (role) {
    case joint::DirectPanelRole::TreatmentVsControl:
        evidence.baseline_policy = learned_recipe(false);
        panel.baseline = learned_bot(false);
        panel.baseline_model_fingerprint =
            kFingerprints.control;
        panel.evaluation_seed =
            old_school::
                kLearnedJointC17MatchedControlGameplaySeed;
        break;
    case joint::DirectPanelRole::TreatmentVsParent:
        evidence.baseline_policy = parent_recipe();
        panel.baseline = learned_bot(false);
        panel.baseline_model_fingerprint =
            std::string(
                old_school::
                    kLearnedJointC17ParentFingerprint);
        panel.evaluation_seed =
            old_school::
                kLearnedJointC17FrozenC16GameplaySeed;
        break;
    case joint::DirectPanelRole::TreatmentVsHandcodedPrimary:
        evidence.baseline_policy = handcoded_recipe();
        panel.baseline = handcoded_bot();
        panel.evaluation_seed =
            old_school::
                kLearnedJointC17HandcodedGameplaySeed;
        break;
    case joint::DirectPanelRole::TreatmentVsHandcodedFixedSeed:
        evidence.baseline_policy = handcoded_recipe();
        panel.baseline = handcoded_bot();
        panel.evaluation_seed =
            joint::kFixedSeedPanelSeeds.at(
                fixed_seed_index);
        break;
    }
    return evidence;
}

joint::DirectPanelEvidence passing_direct_panel(
    joint::DirectPanelRole role =
        joint::DirectPanelRole::
            TreatmentVsHandcodedPrimary) {
    return panel_evidence(
        role,
        benchmark_fixture(
            joint::kDirectPanelRepetitions, 84, 37));
}

joint::DirectPanelEvidence passing_fixed_seed_panel(
    std::size_t seed_index = 0) {
    return panel_evidence(
        joint::DirectPanelRole::
            TreatmentVsHandcodedFixedSeed,
        benchmark_fixture(
            joint::kFixedSeedPanelRepetitions, 10, 5),
        seed_index);
}

void transfer_cell_wins(
    BotBenchmarkSummary& summary, std::size_t row,
    std::size_t from_column, std::size_t to_column,
    std::size_t wins) {
    auto& from =
        summary.challenger_deck_matchups
            [row][from_column];
    auto& to =
        summary.challenger_deck_matchups
            [row][to_column];
    from.wins -= wins;
    from.losses += wins;
    to.wins += wins;
    to.losses -= wins;
    recompute_benchmark_totals(summary);
}

void recompute_count_totals(
    joint::BenchmarkCountSummary& summary) {
    summary.challenger = {};
    summary.baseline = {};
    summary.challenger_decks = {};
    summary.baseline_decks = {};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount; ++opponent) {
            const auto& cell =
                summary.challenger_deck_matchups
                    [deck][opponent];
            add_stats(summary.challenger_decks[deck], cell);
            add_stats(summary.challenger, cell);
            add_stats(
                summary.baseline_decks[opponent],
                cell, true);
            add_stats(summary.baseline, cell, true);
        }
    }
    summary.total_games = summary.challenger.games;
    const std::size_t games_per_quadrant =
        summary.challenger_decks[0].games / 4;
    auto distribute =
        [games_per_quadrant](
            const std::array<
                joint::OutcomeCounts,
                old_school::kDeckCount>& decks,
            joint::OutcomeQuadrants& quadrants) {
            quadrants = {};
            for (std::size_t deck = 0;
                 deck < old_school::kDeckCount; ++deck) {
                std::size_t wins = decks[deck].wins;
                std::size_t losses = decks[deck].losses;
                std::size_t draws = decks[deck].draws;
                for (std::size_t seat = 0;
                     seat < 2; ++seat) {
                    for (std::size_t play_draw = 0;
                         play_draw < 2; ++play_draw) {
                        auto& cell =
                            quadrants[deck][seat][play_draw];
                        cell.games = games_per_quadrant;
                        cell.wins =
                            std::min(
                                wins,
                                games_per_quadrant);
                        wins -= cell.wins;
                        cell.losses =
                            std::min(
                                losses,
                                games_per_quadrant -
                                    cell.wins);
                        losses -= cell.losses;
                        cell.draws =
                            games_per_quadrant -
                            cell.wins - cell.losses;
                        draws -= cell.draws;
                    }
                }
                expect(
                    wins == 0 && losses == 0 &&
                        draws == 0,
                    "count quadrant distribution");
            }
        };
    distribute(
        summary.challenger_decks,
        summary.challenger_outcome_quadrants);
    distribute(
        summary.baseline_decks,
        summary.baseline_outcome_quadrants);
}

std::vector<joint::DirectPanelEvidence>
passing_fixed_seed_panels() {
    std::vector<joint::DirectPanelEvidence> panels;
    panels.reserve(joint::kFixedSeedPanelCount);
    for (std::size_t index = 0;
         index < joint::kFixedSeedPanelCount; ++index) {
        panels.push_back(passing_fixed_seed_panel(index));
    }
    return panels;
}

joint::BenchmarkCountSummary passing_final_pool() {
    const auto direct = passing_direct_panel();
    const auto fixed = passing_fixed_seed_panels();
    const auto merged =
        joint::merge_final_direct_panels(
            direct, fixed, kFingerprints);
    expect(merged.has_value(), "passing panels must merge");
    return *merged;
}

void add_deck_stats_for_fixture(
    old_school::DeckSimulationStats& destination,
    const old_school::DeckSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
    destination.on_play_games += source.on_play_games;
    destination.on_play_wins += source.on_play_wins;
    destination.on_draw_games += source.on_draw_games;
    destination.on_draw_wins += source.on_draw_wins;
}

void add_bot_stats_for_fixture(
    old_school::BotSimulationStats& destination,
    const old_school::BotSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
}

std::array<
    old_school::BotMatchupStats,
    old_school::kBotMatchupCount>
canonical_bot_matchups_fixture() {
    std::array<
        old_school::BotMatchupStats,
        old_school::kBotMatchupCount>
        result{};
    std::size_t index = 0;
    for (std::size_t first = 0;
         first < old_school::kBotKindCount; ++first) {
        for (std::size_t second = first + 1;
             second < old_school::kBotKindCount;
             ++second) {
            result[index++] = {
                .first_bot =
                    static_cast<BotKind>(first),
                .second_bot =
                    static_cast<BotKind>(second),
            };
        }
    }
    return result;
}

TournamentSummary passing_mixed_field_seed_panel(
    std::size_t seed_index) {
    TournamentSummary summary;
    summary.games_per_matchup =
        joint::kMixedFieldGamesPerMatchup;
    summary.total_games =
        joint::kMixedFieldGamesPerSeed;
    summary.evaluation_seed =
        joint::kFixedSeedPanelSeeds.at(seed_index);
    summary.learned_training_seed =
        old_school::kDefaultLearnedTrainingSeed;
    summary.effective_learned_bot =
        learned_bot(true);
    summary.effective_learned_model_fingerprint =
        kFingerprints.treatment;
    summary.bot_matchups =
        canonical_bot_matchups_fixture();

    constexpr std::array<
        std::pair<DeckId, DeckId>,
        old_school::kDistinctDeckPairingCount>
        pairings = {{
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
    constexpr std::array<
        std::size_t,
        old_school::kBotKindCount>
        wins = {4, 7, 8, 9, 12};
    constexpr std::array<
        std::size_t,
        old_school::kBotKindCount>
        losses = {12, 9, 8, 7, 4};

    for (std::size_t matchup_index = 0;
         matchup_index < pairings.size();
         ++matchup_index) {
        auto& matchup = summary.matchups[matchup_index];
        matchup.first_deck =
            pairings[matchup_index].first;
        matchup.second_deck =
            pairings[matchup_index].second;
        auto& result = matchup.result;
        result.games =
            joint::kMixedFieldGamesPerMatchup;
        result.draws = 20;
        result.life_total_finishes = 80;
        result.turn_limit_draws = 20;
        result.bot_matchups =
            canonical_bot_matchups_fixture();
        constexpr std::array<
            std::size_t,
            old_school::kBotMatchupCount>
            first_wins = {
                1, 1, 1, 1, 1, 2, 2, 1, 1, 0,
            };
        for (std::size_t bot_matchup = 0;
             bot_matchup <
                 result.bot_matchups.size();
             ++bot_matchup) {
            auto& stats =
                result.bot_matchups[bot_matchup];
            stats.games = 8;
            stats.first_wins =
                first_wins[bot_matchup];
            stats.second_wins =
                6 - stats.first_wins;
            stats.draws = 2;
        }

        for (std::size_t seat = 0;
             seat < 2; ++seat) {
            auto& deck = result.decks[seat];
            deck.games = 100;
            deck.wins = 40;
            deck.losses = 40;
            deck.draws = 20;
            deck.on_play_games = 50;
            deck.on_play_wins = 19;
            deck.on_draw_games = 50;
            deck.on_draw_wins = 21;
            for (std::size_t bot = 0;
                 bot < old_school::kBotKindCount;
                 ++bot) {
                auto& cell =
                    result.deck_bots[seat][bot];
                cell.games = 20;
                cell.wins = wins[bot];
                cell.losses = losses[bot];
                cell.draws =
                    20 - wins[bot] - losses[bot];
                cell.on_play_games = 10;
                cell.on_play_wins = wins[bot] / 2;
                cell.on_draw_games = 10;
                cell.on_draw_wins =
                    wins[bot] - cell.on_play_wins;
                add_stats(result.bots[bot], cell);
            }
        }

        const auto first =
            static_cast<std::size_t>(
                matchup.first_deck);
        const auto second =
            static_cast<std::size_t>(
                matchup.second_deck);
        add_deck_stats_for_fixture(
            summary.decks[first],
            result.decks[0]);
        add_deck_stats_for_fixture(
            summary.decks[second],
            result.decks[1]);
        for (std::size_t bot = 0;
             bot < old_school::kBotKindCount;
             ++bot) {
            add_bot_stats_for_fixture(
                summary.bots[bot],
                result.bots[bot]);
            add_deck_stats_for_fixture(
                summary.deck_bots[first][bot],
                result.deck_bots[0][bot]);
            add_deck_stats_for_fixture(
                summary.deck_bots[second][bot],
                result.deck_bots[1][bot]);
        }
        for (std::size_t bot_matchup = 0;
             bot_matchup <
                 old_school::kBotMatchupCount;
             ++bot_matchup) {
            auto& destination =
                summary.bot_matchups[bot_matchup];
            const auto& source =
                result.bot_matchups[bot_matchup];
            destination.games += source.games;
            destination.first_wins +=
                source.first_wins;
            destination.second_wins +=
                source.second_wins;
            destination.draws += source.draws;
        }
        summary.draws += result.draws;
        summary.life_total_finishes +=
            result.life_total_finishes;
        summary.turn_limit_draws +=
            result.turn_limit_draws;
    }
    return summary;
}

std::vector<joint::MixedFieldSeedPanelEvidence>
passing_mixed_field_panels() {
    std::vector<joint::MixedFieldSeedPanelEvidence> panels;
    panels.reserve(joint::kFixedSeedPanelCount);
    for (std::size_t index = 0;
         index < joint::kFixedSeedPanelCount; ++index) {
        panels.push_back({
            .learned_policy = learned_recipe(true),
            .summary =
                passing_mixed_field_seed_panel(index),
        });
    }
    return panels;
}

std::vector<std::string> field_candidates(
    std::size_t fixture) {
    if (fixture < 2) {
        return {
            "no-blocks",
            "block-air-elemental-with-flying-men",
        };
    }
    if (fixture == 2) {
        return {
            "pass",
            "growth-own-summoning-sick-grizzly-bears",
        };
    }
    if (fixture == 3) {
        return {
            "pass",
            "growth-own-ironroot-treefolk",
            "growth-opponent-tapped-air-elemental",
        };
    }
    return {
        "skip-ironroot-treefolk",
        "include-ironroot-treefolk",
    };
}

old_school::probes::DecisionKind field_kind(
    std::size_t fixture) {
    if (fixture < 2) {
        return old_school::probes::DecisionKind::Block;
    }
    if (fixture < 4) {
        return old_school::probes::DecisionKind::Priority;
    }
    return old_school::probes::DecisionKind::Attack;
}

std::vector<probe::CandidateSamples> field_samples(
    const std::vector<std::string>& candidates,
    std::size_t worlds, bool stable) {
    std::vector<probe::CandidateSamples> samples;
    samples.reserve(candidates.size());
    for (std::size_t candidate = 0;
         candidate < candidates.size(); ++candidate) {
        const double value =
            candidate == 0
                ? 0.60
                : 0.60 -
                      (stable ? 0.10 : 0.005) *
                          static_cast<double>(candidate);
        samples.push_back({
            .key = candidates[candidate],
            .q_samples =
                std::vector<double>(worlds, value),
        });
    }
    return samples;
}

runner::FieldRegressionPolicyDecision field_policy(
    std::size_t fixture,
    const std::vector<std::string>& candidates,
    std::string name, std::string fingerprint,
    bool treatment, std::size_t selected) {
    const bool priority =
        field_kind(fixture) ==
        old_school::probes::DecisionKind::Priority;
    runner::FieldRegressionPolicyDecision policy{
        .name = std::move(name),
        .fingerprint = std::move(fingerprint),
        .score_kind =
            priority
                ? runner::FieldRegressionScoreKind::
                      DeployedPrioritySearch
                : runner::FieldRegressionScoreKind::
                      ImmediateCombat,
        .deployment_worlds =
            runner::kFieldDeploymentWorlds,
        .deployment_horizon_turns =
            runner::kFieldDeploymentHorizonTurns,
        .blend_shallow_prior = true,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = treatment,
        .value_continuation_controller =
            treatment
                ? old_school::
                      LearnedContinuationController::
                          PublicStackPassV1
                : old_school::
                      LearnedContinuationController::Legacy,
    };
    if (priority) {
        policy.samples = field_samples(
            candidates,
            runner::kFieldDeploymentWorlds, true);
        policy.accounting = {
            .sampled_worlds =
                runner::kFieldDeploymentWorlds,
            .rollout_evaluations =
                candidates.size() *
                runner::kFieldDeploymentWorlds,
            .terminal_evaluations = 0,
            .bootstrapped_evaluations =
                candidates.size() *
                runner::kFieldDeploymentWorlds,
        };
    } else {
        policy.deterministic_selection = true;
    }
    for (std::size_t candidate = 0;
         candidate < candidates.size(); ++candidate) {
        policy.scores.push_back({
            .key = candidates[candidate],
            .score =
                candidate == selected ? 1.0 : 0.0,
        });
    }
    policy.selected_keys = {candidates.at(selected)};
    return policy;
}

runner::FieldRegressionReport passing_field_report() {
    runner::FieldRegressionReport report{
        .corpus_id =
            std::string(
                old_school::probes::
                    kFieldRegressionsV1),
        .reference_model_fingerprint =
            std::string(
                old_school::
                    kLearnedJointC17ParentFingerprint),
        .reference_worlds =
            runner::kFieldReferenceWorlds,
        .reference_horizon_turns =
            runner::kFieldReferenceHorizonTurns,
        .reference_rollouts_per_world = 1,
        .reference_blend_shallow_prior = false,
        .reference_value_continuation_epsilon = 0.0,
        .reference_value_priority_residual_weight = 0.0,
        .reference_value_pass_dominance = false,
        .reference_value_continuation_controller =
            old_school::
                LearnedContinuationController::Legacy,
        .hidden_repartition = {
            .passed = true,
            .policy_count = 4,
            .probe_count =
                joint::kFieldRegressionFixtureCount,
        },
        .rules_contract_passed = true,
    };
    report.decisions.reserve(
        joint::kFieldRegressionFixtureCount);
    for (std::size_t fixture = 0;
         fixture <
         joint::kFieldRegressionFixtureCount;
         ++fixture) {
        const auto candidates =
            field_candidates(fixture);
        runner::FieldRegressionDecisionReport decision{
            .stable_id =
                std::string(
                    joint::
                        kRequiredFieldRegressionIds[
                            fixture]),
            .root_deck =
                fixture < 2
                    ? DeckId::RUAggro
                    : DeckId::Green,
            .decision_kind = field_kind(fixture),
            .candidate_descriptors = candidates,
            .reference_samples =
                field_samples(
                    candidates,
                    runner::kFieldReferenceWorlds,
                    true),
            .reference_accounting = {
                .sampled_worlds =
                    runner::kFieldReferenceWorlds,
                .rollout_evaluations =
                    candidates.size() *
                    runner::kFieldReferenceWorlds,
                .terminal_evaluations = 0,
                .bootstrapped_evaluations =
                    candidates.size() *
                    runner::kFieldReferenceWorlds,
            },
            .parent =
                field_policy(
                    fixture, candidates,
                    std::string(
                        joint::
                            kFrozenC16EvidencePolicyToken),
                    std::string(
                        old_school::
                            kLearnedJointC17ParentFingerprint),
                    false, 0),
            .control =
                field_policy(
                    fixture, candidates,
                    std::string(
                        old_school::
                            kLearnedJointC17ControlPolicyToken),
                    kFingerprints.control,
                    false, 0),
            .treatment =
                field_policy(
                    fixture, candidates,
                    std::string(
                        old_school::
                            kLearnedJointC17TreatmentPolicyToken),
                    kFingerprints.treatment,
                    true, 0),
        };
        for (std::size_t candidate = 0;
             candidate < candidates.size();
             ++candidate) {
            decision.forced_consequences.push_back({
                .descriptor = candidates[candidate],
                .public_state_fingerprint =
                    std::string(
                        joint::
                            kRequiredFieldConsequenceFingerprints
                                [fixture][candidate]),
            });
        }
        report.decisions.push_back(
            std::move(decision));
    }
    return report;
}

void test_heldout_gate_accepts_exact_inclusive_guards() {
    const auto gate =
        joint::evaluate_heldout_gate(
            passing_holdout_report());
    expect(gate.accounting_exact, "exact held-out accounting");
    expect(gate.inputs_finite, "finite held-out inputs");
    expect(
        gate.pooled_losses_improved,
        "strict pooled CR1 improvement");
    expect(
        gate.every_deck_loss_guard,
        "+0.005 deck boundary must pass");
    expect(
        gate.green_bias_strictly_shrank &&
            gate.blue_bias_strictly_shrank,
        "Green/Blue strict shrink");
    expect(
        gate.ru_bias_guard,
        "RU +0.010 floor equality must pass");
    expect(
        gate.no_new_material_bias,
        "nonmaterial bias must pass");
    expect(gate.passed, "passing held-out report");
}

void test_heldout_pooled_and_deck_boundaries_are_exact() {
    auto report = passing_holdout_report();
    report.pooled.treatment_comparisons[0]
        .brier_delta.confidence_upper_95 = 0.0;
    auto gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.pooled_losses_improved && !gate.passed,
        "pooled upper bound equal to zero must fail");

    report = passing_holdout_report();
    report.by_deck[static_cast<std::size_t>(DeckId::Red)]
        .treatment_comparisons[0]
        .soft_log_loss_delta.mean =
        std::nextafter(
            joint::kMaximumDeckLossDelta,
            std::numeric_limits<double>::infinity());
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.every_deck_loss_guard && !gate.passed,
        "point delta above +0.005 must fail");

    report = passing_holdout_report();
    report.pooled.treatment_comparisons[0]
        .soft_log_loss_delta.confidence_upper_95 =
        std::numeric_limits<double>::quiet_NaN();
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.inputs_finite && !gate.passed,
        "nonfinite held-out metric must fail closed");

    report = passing_holdout_report();
    --report.pooled.physical_games;
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.accounting_exact && !gate.passed,
        "held-out physical-game undercount must fail");

    report = passing_holdout_report();
    --report
          .by_deck[static_cast<std::size_t>(DeckId::White)]
          .perspectives;
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.accounting_exact && !gate.passed,
        "held-out deck perspective undercount must fail");

    report = passing_holdout_report();
    report.by_deck[0].records =
        std::numeric_limits<std::size_t>::max();
    report.by_deck[1].records = 1;
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.accounting_exact && !gate.passed,
        "held-out deck-record sum overflow must fail closed");
}

void test_heldout_bias_boundaries_and_inheritance() {
    auto report = passing_holdout_report();
    auto& green =
        report.by_deck[
            static_cast<std::size_t>(DeckId::Green)];
    green.models[2].signed_bias.mean =
        green.models[1].signed_bias.mean;
    auto gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.green_bias_strictly_shrank,
        "equal Green absolute bias must fail");

    report = passing_holdout_report();
    auto& ru =
        report.by_deck[
            static_cast<std::size_t>(DeckId::RUAggro)];
    ru.models[2].signed_bias.mean =
        std::nextafter(
            joint::kRuSignedBiasFloor,
            std::numeric_limits<double>::infinity());
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.ru_bias_guard,
        "RU bias above the floor/control maximum must fail");

    report = passing_holdout_report();
    auto& red =
        report.by_deck[
            static_cast<std::size_t>(DeckId::Red)];
    red.models[1].signed_bias =
        estimate(0.06, 0.01, 0.10);
    red.models[2].signed_bias =
        estimate(0.05, 0.001, 0.10);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        gate.no_new_material_bias && gate.passed,
        "same-sign inherited material bias must pass");

    red.models[1].signed_bias =
        estimate(0.06, -0.01, 0.10);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.no_new_material_bias && !gate.passed,
        "control interval crossing zero is not material");

    red.models[1].signed_bias =
        estimate(-0.06, -0.10, -0.01);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        !gate.no_new_material_bias,
        "opposite-sign material bias is not inherited");

    red.models[2].signed_bias =
        estimate(
            joint::kMaterialSignedBias, 0.0, 0.10);
    gate = joint::evaluate_heldout_gate(report);
    expect(
        gate.no_new_material_bias,
        "interval touching zero is not material");
}

void test_stable_best_set_uses_paired_strict_boundaries() {
    auto label = stable_label(
        "boundary", DeckId::Green,
        probe::kStablePairMinimumDelta, 0.01);
    expect(
        joint::is_stable_best_set_probe(label),
        "minimum delta equality with separated CI must pass");

    label.pairs[0].delta_q = std::nextafter(
        probe::kStablePairMinimumDelta, 0.0);
    expect(
        !joint::is_stable_best_set_probe(label),
        "delta below stable minimum must fail");

    label = stable_label(
        "ci-boundary", DeckId::Green, 0.04,
        std::nextafter(
            0.04 / probe::kNormal95CriticalValue,
            std::numeric_limits<double>::infinity()));
    expect(
        !joint::is_stable_best_set_probe(label),
        "paired CI touching/crossing zero must fail");

    label = stable_label(
        "reverse", DeckId::Green, 0.04, 0.01, true);
    expect(
        joint::is_stable_best_set_probe(label),
        "reverse pair orientation must be normalized");

    label.reference_best_set = {"best", "other"};
    expect(
        !joint::is_stable_best_set_probe(label),
        "a best set with no outside candidate is ineligible");
}

void test_common_state_critics_use_reference_max_for_both_models() {
    const std::vector<probe::ProbeLabel> labels = {
        stable_label("common-label.v1", DeckId::Green)};
    auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    control.front().selected_keys = {"other"};
    treatment.front().selected_keys = {"other"};
    control.front().critic_prediction = 0.50;
    treatment.front().critic_prediction = 0.70;

    const auto report = joint::score_common_state_critics(
        labels, control, treatment);
    expect(
        report.accounting_exact &&
            report.predictions_valid &&
            report.metrics_finite,
        "common-state critic report validity");
    const auto& control_metrics =
        report.pooled
            .models[joint::kCommonStateControlIndex];
    const auto& treatment_metrics =
        report.pooled
            .models[joint::kCommonStateTreatmentIndex];
    expect(control_metrics.probe_count == 1, "common count");
    expect_near(
        control_metrics.brier, 0.01, 1.0e-12,
        "control Brier uses reference maximum 0.60");
    expect_near(
        control_metrics.signed_bias, -0.10, 1.0e-12,
        "control bias uses common state label");
    expect_near(
        treatment_metrics.brier, 0.01, 1.0e-12,
        "treatment Brier uses same state label");
    expect_near(
        treatment_metrics.signed_bias, 0.10, 1.0e-12,
        "treatment bias uses common state label");
    expect_near(
        control_metrics.soft_log_loss,
        -0.60 * std::log(0.50) -
            0.40 * std::log(0.50),
        1.0e-12, "common-label soft log loss");
    expect_near(
        control_metrics.ece, 0.10, 1.0e-12,
        "common-label calibration");

    treatment.front().critic_prediction =
        std::numeric_limits<double>::quiet_NaN();
    const auto invalid = joint::score_common_state_critics(
        labels, control, treatment);
    expect(
        !invalid.predictions_valid &&
            !invalid.metrics_finite,
        "nonfinite common critic prediction must fail");
}

void test_deep_gate_accepts_equal_metric_boundaries() {
    const auto gate = passing_deep_gate();
    expect(gate.accounting_exact, "deep accounting");
    expect(gate.metrics_finite, "deep metrics finite");
    expect(
        gate.pooled_regret_no_worse &&
            gate.pooled_top_one_no_lower,
        "equal pooled metrics must pass");
    expect(
        gate.every_deck_regret_guard,
        "deck regret guard");
    expect(
        gate.stable_best_set_loss_guard,
        "stable best-set loss guard");
    expect(
        gate.required_blue_probes_exact &&
            gate.required_blue_selections_passed,
        "required Blue probes");
    expect(gate.force_spike.passed, "Force Spike gate");
    expect(
        gate.common_state_critics.accounting_exact &&
            gate.common_state_critics.metrics_finite,
        "common-state critic report");
    expect(gate.passed, "passing deep gate");

    const auto labels = passing_labels();
    const auto decisions = passing_decisions(labels);
    auto control_metrics = metrics_for(labels, 0.90, 0.02);
    auto treatment_metrics = control_metrics;
    treatment_metrics
        .by_deck[static_cast<std::size_t>(DeckId::Red)]
        .mean_regret =
        control_metrics
            .by_deck[static_cast<std::size_t>(DeckId::Red)]
            .mean_regret +
        joint::kMaximumDeckRegretIncrease;
    const auto boundary =
        joint::evaluate_deep_reference_gate(
            control_metrics, treatment_metrics, labels,
            decisions, decisions, passing_force_spike_report(),
            true);
    expect(
        boundary.every_deck_regret_guard,
        "deck regret +0.01 equality must pass");
}

void test_deep_metric_regressions_fail_closed() {
    const auto labels = passing_labels();
    const auto decisions = passing_decisions(labels);
    const auto control_metrics =
        metrics_for(labels, 0.90, 0.02);
    auto treatment_metrics = control_metrics;
    treatment_metrics.mean_regret =
        std::nextafter(
            control_metrics.mean_regret,
            std::numeric_limits<double>::infinity());
    auto gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, decisions,
        decisions, passing_force_spike_report(), true);
    expect(
        !gate.pooled_regret_no_worse && !gate.passed,
        "pooled regret regression must fail");

    treatment_metrics = control_metrics;
    treatment_metrics.top1_expected_agreement =
        std::nextafter(
            control_metrics.top1_expected_agreement, 0.0);
    gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, decisions,
        decisions, passing_force_spike_report(), true);
    expect(
        !gate.pooled_top_one_no_lower && !gate.passed,
        "pooled top-one regression must fail");

    treatment_metrics = control_metrics;
    treatment_metrics
        .by_deck[static_cast<std::size_t>(DeckId::White)]
        .mean_regret =
        std::nextafter(
            control_metrics
                    .by_deck[static_cast<std::size_t>(
                        DeckId::White)]
                    .mean_regret +
                joint::kMaximumDeckRegretIncrease,
            std::numeric_limits<double>::infinity());
    gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, decisions,
        decisions, passing_force_spike_report(), true);
    expect(
        !gate.every_deck_regret_guard && !gate.passed,
        "deck regret above +0.01 must fail");
}

void test_stable_losses_are_per_probe_and_not_net() {
    auto labels = passing_labels();
    auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    for (auto& decision : treatment) {
        if (decision.stable_id == "green.stable.v1") {
            decision.selected_keys = {"other"};
        }
    }
    auto control_metrics = metrics_for(labels);
    auto treatment_metrics = control_metrics;
    auto gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, control,
        treatment, passing_force_spike_report(), true);
    const auto green_index =
        static_cast<std::size_t>(DeckId::Green);
    expect(
        gate.by_deck[green_index].lost_agreements == 1 &&
            gate.stable_best_set_loss_guard,
        "one lost probe per deck must pass");

    for (auto& decision : treatment) {
        if (decision.stable_id == "green.stable.v2") {
            decision.selected_keys = {"other"};
        }
        if (decision.stable_id == "ru.stable.v1") {
            decision.selected_keys = {"best"};
        }
    }
    auto& ru_control = *std::find_if(
        control.begin(), control.end(),
        [](const runner::ValueProbeDecisionDetail& decision) {
            return decision.stable_id == "ru.stable.v1";
        });
    ru_control.selected_keys = {"other"};
    gate = joint::evaluate_deep_reference_gate(
        control_metrics, treatment_metrics, labels, control,
        treatment, passing_force_spike_report(), true);
    expect(
        gate.by_deck[green_index].lost_agreements == 2,
        "losses must count per probe");
    expect(
        gate.by_deck[static_cast<std::size_t>(
            DeckId::RUAggro)]
                .control_agreements == 3 &&
            gate.by_deck[static_cast<std::size_t>(
                DeckId::RUAggro)]
                .treatment_agreements == 4,
        "treatment gain fixture");
    expect(
        !gate.stable_best_set_loss_guard && !gate.passed,
        "gain on another probe must not cancel losses");
}

void test_required_blue_probes_and_selections_are_conjunctive() {
    auto labels = passing_labels();
    const auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    treatment.front().selected_keys = {"other"};
    const auto metrics = metrics_for(labels);
    auto gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        gate.required_blue_probes_exact &&
            !gate.required_blue_selections_passed &&
            !gate.passed,
        "wrong required Blue selection must fail");

    treatment = passing_decisions(labels);
    labels.front().pairs.front().delta_q = 0.01;
    gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        !gate.required_blue_probes_exact && !gate.passed,
        "unstable required Blue probe must fail");

    labels = passing_labels();
    labels.push_back(labels.front());
    const auto duplicate_metrics = metrics_for(labels);
    const auto duplicate_decisions =
        passing_decisions(labels);
    gate = joint::evaluate_deep_reference_gate(
        duplicate_metrics, duplicate_metrics, labels,
        duplicate_decisions, duplicate_decisions,
        passing_force_spike_report(), true);
    expect(
        !gate.accounting_exact &&
            !gate.required_blue_probes_exact &&
            !gate.passed,
        "duplicate required Blue identity must fail");
}

void test_force_spike_gate_requires_unique_exact_selections() {
    auto report = passing_force_spike_report();
    auto gate =
        joint::evaluate_force_spike_selection_gate(report);
    expect(gate.passed, "passing Force Spike controls");

    report.live.selected_keys.push_back("pass");
    gate = joint::evaluate_force_spike_selection_gate(report);
    expect(
        !gate.live_uniquely_selects_force_spike &&
            !gate.passed,
        "live tie must fail unique selection");

    report = passing_force_spike_report();
    report.payable.selected_keys = {
        std::string(joint::kForceSpikeCandidateKey)};
    gate = joint::evaluate_force_spike_selection_gate(report);
    expect(
        !gate.payable_uniquely_selects_pass && !gate.passed,
        "payable Force Spike selection must fail");

    report = passing_force_spike_report();
    report.live.stable_id = "wrong";
    report.hidden_repartition_passed = false;
    gate = joint::evaluate_force_spike_selection_gate(report);
    expect(
        !gate.identities_exact &&
            !gate.hidden_repartition_passed &&
            !gate.passed,
        "identity and hidden checks are conjunctive");
}

void test_deep_gate_rejects_missing_or_nonfinite_rows() {
    auto labels = passing_labels();
    auto control = passing_decisions(labels);
    auto treatment = passing_decisions(labels);
    auto metrics = metrics_for(labels);

    treatment.pop_back();
    auto gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        !gate.accounting_exact && !gate.passed,
        "missing decision must fail accounting");

    treatment = passing_decisions(labels);
    metrics.mean_regret =
        std::numeric_limits<double>::infinity();
    gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), true);
    expect(
        !gate.metrics_finite && !gate.passed,
        "nonfinite deep metric must fail");

    metrics = metrics_for(labels);
    gate = joint::evaluate_deep_reference_gate(
        metrics, metrics, labels, control, treatment,
        passing_force_spike_report(), false);
    expect(
        !gate.hidden_repartition_passed && !gate.passed,
        "hidden repartition must be conjunctive");
}

void test_field_regression_gate_is_reject_only() {
    auto report = passing_field_report();
    auto gate =
        joint::evaluate_field_regression_gate(
            report, kFingerprints);
    expect(
        gate.metadata_exact &&
            gate.fixture_count_exact &&
            gate.every_fixture_valid &&
            gate.stable_fixture_count ==
                joint::kFieldRegressionFixtureCount &&
            gate.control_agreements ==
                joint::kFieldRegressionFixtureCount &&
            gate.treatment_agreements ==
                joint::kFieldRegressionFixtureCount &&
            gate.treatment_losses == 0 &&
            gate.passed,
        "passing six-fixture field gate");

    report = passing_field_report();
    report.decisions[0].treatment =
        field_policy(
            0, field_candidates(0),
            std::string(
                old_school::
                    kLearnedJointC17TreatmentPolicyToken),
            kFingerprints.treatment, true, 1);
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        gate.every_fixture_valid &&
            gate.treatment_losses == 1 &&
            !gate.passed,
        "stable treatment loss must reject");

    report = passing_field_report();
    report.decisions[0].control =
        field_policy(
            0, field_candidates(0),
            std::string(
                old_school::
                    kLearnedJointC17ControlPolicyToken),
            kFingerprints.control, false, 1);
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        gate.treatment_gains == 1 &&
            gate.treatment_losses == 0 &&
            gate.passed,
        "treatment gain passes but does not promote");

    report = passing_field_report();
    const auto candidates = field_candidates(0);
    report.decisions[0].reference_samples =
        field_samples(
            candidates,
            runner::kFieldReferenceWorlds,
            false);
    report.decisions[0].treatment =
        field_policy(
            0, candidates,
            std::string(
                old_school::
                    kLearnedJointC17TreatmentPolicyToken),
            kFingerprints.treatment, true, 1);
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        gate.every_fixture_valid &&
            !gate.fixtures[0]
                 .stable_reference_best_set &&
            gate.treatment_losses == 0 &&
            gate.passed,
        "unstable treatment difference cannot reject");
}

void test_field_regression_evidence_fails_closed() {
    auto report = passing_field_report();
    report.corpus_id = "wrong-corpus";
    report.rules_contract_passed = false;
    report.hidden_repartition.policy_count = 3;
    auto gate =
        joint::evaluate_field_regression_gate(
            report, kFingerprints);
    expect(
        !gate.metadata_exact && !gate.passed,
        "corpus/rules/hidden metadata must be exact");

    report = passing_field_report();
    std::swap(
        report.decisions[0],
        report.decisions[1]);
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        !gate.every_fixture_valid && !gate.passed,
        "reordered field identities must fail");

    report = passing_field_report();
    --report.decisions[2]
          .reference_accounting
          .rollout_evaluations;
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        !gate.fixtures[2].reference_valid &&
            !gate.passed,
        "reference accounting must be exact");

    report = passing_field_report();
    report.decisions[3]
        .treatment.deployment_worlds = 7;
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        !gate.fixtures[3].deployment_valid &&
            !gate.passed,
        "deployment config must be exact");

    report = passing_field_report();
    report.decisions[3]
        .treatment.value_continuation_epsilon = 0.1;
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        !gate.fixtures[3].deployment_valid &&
            !gate.passed,
        "deployment continuation epsilon must be exact");

    report = passing_field_report();
    report.decisions[4]
        .forced_consequences[0]
        .public_state_fingerprint =
        "0000000000000000";
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        !gate.fixtures[4].reference_valid &&
            !gate.passed,
        "forced public consequence evidence must be canonical");

    report = passing_field_report();
    report.decisions[5]
        .reference_samples[0]
        .q_samples.pop_back();
    gate = joint::evaluate_field_regression_gate(
        report, kFingerprints);
    expect(
        !gate.fixtures[5].reference_valid &&
            !gate.passed,
        "misaligned reference samples must fail closed");
}

void test_direct_gameplay_gate_uses_challenger_own_deck_outcomes() {
    auto evidence = passing_direct_panel();
    transfer_cell_wins(
        evidence.summary, 0, 0, 1, 30);
    for (std::size_t deck = 1;
         deck < old_school::kDeckCount; ++deck) {
        transfer_cell_wins(
            evidence.summary, deck, 0, deck, 10);
    }

    const auto green =
        static_cast<std::size_t>(DeckId::Green);
    expect(
        evidence.summary.challenger_decks[green].wins <
            evidence.summary.baseline_decks[green].wins,
        "fixture must trigger the baseline-deck trap");
    const auto gate =
        joint::evaluate_direct_gameplay_panel(
            evidence, kFingerprints);
    expect(
        gate.identity_exact &&
            gate.accounting_exact &&
            gate.clustered_estimate_valid &&
            gate.aggregate_strict_win &&
            gate.wilson_lower_above_half &&
            gate.challenger_deck_strict_wins[green] &&
            gate.every_challenger_deck_strict_win &&
            gate.passed,
        "direct deck gate must use challenger wins/losses");
}

void test_direct_gameplay_strict_wilson_and_count_boundaries() {
    auto evidence = passing_direct_panel(
        joint::DirectPanelRole::TreatmentVsControl);
    auto gate =
        joint::evaluate_direct_gameplay_panel(
            evidence, kFingerprints);
    expect(gate.passed, "exact direct evidence must pass");

    auto wrong_seed = evidence;
    ++wrong_seed.summary.evaluation_seed;
    gate = joint::evaluate_direct_gameplay_panel(
        wrong_seed, kFingerprints);
    expect(
        !gate.identity_exact && !gate.passed,
        "wrong direct evaluation seed must fail");

    auto wrong_policy = evidence;
    wrong_policy.challenger_policy.policy_token =
        "same-marginals-different-policy";
    gate = joint::evaluate_direct_gameplay_panel(
        wrong_policy, kFingerprints);
    expect(
        !gate.identity_exact && !gate.passed,
        "wrong challenger policy token must fail");

    auto wrong_model = evidence;
    wrong_model.summary.challenger_model_fingerprint =
        kFingerprints.control;
    gate = joint::evaluate_direct_gameplay_panel(
        wrong_model, kFingerprints);
    expect(
        !gate.identity_exact && !gate.passed,
        "wrong challenger model must fail");

    auto wrong_quadrant = evidence;
    auto& short_cell =
        wrong_quadrant.summary
            .challenger_outcome_quadrants[0][0][0];
    auto& long_cell =
        wrong_quadrant.summary
            .challenger_outcome_quadrants[0][0][1];
    --short_cell.games;
    --short_cell.losses;
    ++long_cell.games;
    ++long_cell.losses;
    gate = joint::evaluate_direct_gameplay_panel(
        wrong_quadrant, kFingerprints);
    expect(
        !gate.accounting_exact && !gate.passed,
        "marginal-preserving quadrant redistribution must fail");

    auto wrong_cluster = evidence;
    --wrong_cluster.summary.challenger_quartet_cr1.clusters;
    gate = joint::evaluate_direct_gameplay_panel(
        wrong_cluster, kFingerprints);
    expect(
        !gate.clustered_estimate_valid && !gate.passed,
        "wrong quartet cluster count must fail");

    wrong_cluster = evidence;
    wrong_cluster.summary.challenger_quartet_cr1.mean +=
        1.0e-6;
    gate = joint::evaluate_direct_gameplay_panel(
        wrong_cluster, kFingerprints);
    expect(
        !gate.clustered_estimate_valid && !gate.passed,
        "clustered mean inconsistent with outcomes must fail");

    auto weak = panel_evidence(
        joint::DirectPanelRole::TreatmentVsControl,
        benchmark_fixture(
            joint::kDirectPanelRepetitions, 69, 34));
    gate = joint::evaluate_direct_gameplay_panel(
        weak, kFingerprints);
    expect(
        gate.identity_exact &&
            gate.accounting_exact &&
            gate.aggregate_strict_win &&
            !gate.wilson_lower_above_half &&
            !gate.passed,
        "small strict edge must fail Wilson lower bound");
}

void test_fixed_seed_panels_are_exact_and_non_losing() {
    auto panels = passing_fixed_seed_panels();
    auto set_gate =
        joint::evaluate_fixed_seed_panel_set(
            panels, kFingerprints);
    expect(
        set_gate.panel_count_exact &&
            set_gate.seeds_exact &&
            set_gate.every_panel_passed &&
            set_gate.passed,
        "ordered fixed-seed set must pass");

    std::swap(panels[2], panels[3]);
    set_gate =
        joint::evaluate_fixed_seed_panel_set(
            panels, kFingerprints);
    expect(
        !set_gate.seeds_exact && !set_gate.passed,
        "reordered fixed seeds must fail");

    panels = passing_fixed_seed_panels();
    panels[3].summary.evaluation_seed =
        panels[2].summary.evaluation_seed;
    set_gate =
        joint::evaluate_fixed_seed_panel_set(
            panels, kFingerprints);
    expect(
        !set_gate.seeds_exact && !set_gate.passed,
        "duplicate fixed seed must fail");

    auto losing = passing_fixed_seed_panel();
    auto& cell =
        losing.summary
            .challenger_deck_matchups[0][0];
    --cell.wins;
    ++cell.losses;
    recompute_benchmark_totals(losing.summary);
    auto panel_gate =
        joint::evaluate_fixed_seed_panel(
            losing, kFingerprints);
    expect(
        panel_gate.identity_exact &&
            panel_gate.accounting_exact &&
            !panel_gate.aggregate_non_losing &&
            !panel_gate.passed,
        "149-151 fixed panel must fail");

    auto wrong_cluster = passing_fixed_seed_panel();
    ++wrong_cluster.summary
          .challenger_quartet_cr1.records;
    panel_gate =
        joint::evaluate_fixed_seed_panel(
            wrong_cluster, kFingerprints);
    expect(
        !panel_gate.clustered_estimate_valid &&
            !panel_gate.passed,
        "fixed panel CR1 record mismatch must fail");
}

void test_final_pool_merge_and_exact_accounting() {
    const auto primary = passing_direct_panel();
    auto fixed = passing_fixed_seed_panels();
    const auto merged =
        joint::merge_final_direct_panels(
            primary, fixed, kFingerprints);
    expect(merged.has_value(), "exact panels must merge");
    expect(
        merged->panel_count ==
                joint::kFinalDirectPanelCount &&
            merged->repetitions_per_deck_pairing ==
                joint::kFinalDirectRepetitions &&
            merged->total_games ==
                joint::kFinalDirectGames,
        "final aggregate schedule");
    for (const auto& deck :
         merged->challenger_outcome_quadrants) {
        for (const auto& seat : deck) {
            for (const auto& play_draw : seat) {
                expect(
                    play_draw.games ==
                        joint::
                            kFinalDirectQuadrantGames,
                    "final exact quadrant count");
            }
        }
    }

    const auto composite =
        joint::evaluate_final_direct_gate(
            primary, fixed, kFingerprints);
    expect(
        composite.primary.passed &&
            composite.fixed_seed_panels.passed &&
            composite.merge_succeeded &&
            composite.pooled.passed &&
            composite.passed,
        "mandatory final direct gate");

    fixed[4].summary.evaluation_seed =
        fixed[3].summary.evaluation_seed;
    expect(
        !joint::merge_final_direct_panels(
             primary, fixed, kFingerprints)
             .has_value(),
        "duplicated seed must not merge");
}

void test_final_pool_strength_and_matrix_boundaries() {
    auto summary = passing_final_pool();
    auto gate =
        joint::evaluate_final_direct_pool(summary);
    expect(gate.passed, "passing final pool");

    auto wrong_quadrant = summary;
    --wrong_quadrant
          .challenger_outcome_quadrants[0][0][0]
          .games;
    ++wrong_quadrant
          .challenger_outcome_quadrants[0][0][1]
          .games;
    gate =
        joint::evaluate_final_direct_pool(
            wrong_quadrant);
    expect(
        !gate.accounting_exact && !gate.passed,
        "pooled quadrant redistribution must fail");

    auto weak = summary;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t opponent = 0;
             opponent < old_school::kDeckCount;
             ++opponent) {
            auto& cell =
                weak.challenger_deck_matchups
                    [deck][opponent];
            if (deck == opponent) {
                cell.wins = 149;
                cell.losses = 147;
            } else {
                cell.wins = 74;
                cell.losses = 74;
            }
        }
    }
    recompute_count_totals(weak);
    gate = joint::evaluate_final_direct_pool(weak);
    expect(
        gate.accounting_exact &&
            gate.aggregate_strict_win &&
            gate.every_challenger_deck_strict_win &&
            !gate.wilson_lower_above_half &&
            !gate.passed,
        "small pooled edge must fail Wilson gate");
}

void swap_learned_and_handcoded(
    TournamentSummary& summary) {
    const auto handcoded =
        static_cast<std::size_t>(
            BotKind::Handcrafted);
    const auto learned =
        static_cast<std::size_t>(BotKind::Learned);
    const auto swap_matchups =
        [](
            std::array<
                old_school::BotMatchupStats,
                old_school::kBotMatchupCount>& matchups) {
            const auto original = matchups;
            matchups =
                canonical_bot_matchups_fixture();
            for (const auto& source : original) {
                auto first =
                    static_cast<std::size_t>(
                        source.first_bot);
                auto second =
                    static_cast<std::size_t>(
                        source.second_bot);
                first =
                    first == handcoded
                        ? learned
                        : first == learned
                              ? handcoded
                              : first;
                second =
                    second == handcoded
                        ? learned
                        : second == learned
                              ? handcoded
                              : second;
                const auto low =
                    std::min(first, second);
                const auto high =
                    std::max(first, second);
                const auto destination =
                    std::find_if(
                        matchups.begin(),
                        matchups.end(),
                        [low, high](
                            const old_school::
                                BotMatchupStats&
                                    candidate) {
                            return static_cast<
                                       std::size_t>(
                                       candidate
                                           .first_bot) ==
                                       low &&
                                   static_cast<
                                       std::size_t>(
                                       candidate
                                           .second_bot) ==
                                       high;
                        });
                expect(
                    destination != matchups.end(),
                    "permuted bot matchup identity");
                destination->games = source.games;
                destination->draws = source.draws;
                const bool same_orientation =
                    first < second;
                destination->first_wins =
                    same_orientation
                        ? source.first_wins
                        : source.second_wins;
                destination->second_wins =
                    same_orientation
                        ? source.second_wins
                        : source.first_wins;
            }
        };
    std::swap(
        summary.bots[handcoded],
        summary.bots[learned]);
    for (auto& deck : summary.deck_bots) {
        std::swap(deck[handcoded], deck[learned]);
    }
    for (auto& matchup : summary.matchups) {
        swap_matchups(
            matchup.result.bot_matchups);
        std::swap(
            matchup.result.bots[handcoded],
            matchup.result.bots[learned]);
        for (auto& seat :
             matchup.result.deck_bots) {
            std::swap(
                seat[handcoded], seat[learned]);
        }
    }
    swap_matchups(summary.bot_matchups);
}

void test_mixed_field_gate_covers_all_decks_and_policies() {
    auto panels = passing_mixed_field_panels();
    auto gate =
        joint::evaluate_mixed_field_pool(
            panels, kFingerprints);
    expect(gate.panel_count_exact, "mixed panel count");
    expect(gate.seeds_exact, "mixed seeds");
    expect(
        gate.policy_identity_exact,
        "mixed policy identity");
    expect(gate.accounting_exact, "mixed accounting");
    expect(gate.rates_finite, "mixed rates");
    expect(
        gate.learned_lift_best_on_every_deck,
        "mixed lift");
    expect(gate.passed, "passing eight-seed mixed field");
    for (const auto& deck : gate.by_deck) {
        expect(
            deck.rates_finite &&
                deck.learned_lift_is_best &&
                deck.best_other ==
                    BotKind::Handcrafted,
            "all five deck lifts");
        expect_near(
            deck.learned_lift_percentage_points,
            40.0, 1e-12,
            "Learned lift");
    }

    for (auto& panel : panels) {
        swap_learned_and_handcoded(panel.summary);
    }
    gate = joint::evaluate_mixed_field_pool(
        panels, kFingerprints);
    expect(
        gate.accounting_exact &&
            !gate.learned_lift_best_on_every_deck &&
            !gate.passed,
        "stronger Handcoded field must reject Learned");
}

void test_mixed_field_ties_counts_and_overflow_fail_closed() {
    auto panels = passing_mixed_field_panels();
    auto anonymous = panels;
    anonymous.resize(1);
    auto gate =
        joint::evaluate_mixed_field_pool(
            anonymous, kFingerprints);
    expect(
        !gate.panel_count_exact &&
            !gate.accounting_exact &&
            !gate.passed,
        "anonymous pooled marginals must fail");

    auto wrong_seed = panels;
    std::swap(
        wrong_seed[0].summary.evaluation_seed,
        wrong_seed[1].summary.evaluation_seed);
    gate = joint::evaluate_mixed_field_pool(
        wrong_seed, kFingerprints);
    expect(
        !gate.seeds_exact && !gate.passed,
        "reordered mixed seeds must fail");

    auto wrong_identity = panels;
    wrong_identity[0]
        .summary.effective_learned_model_fingerprint =
        kFingerprints.control;
    gate = joint::evaluate_mixed_field_pool(
        wrong_identity, kFingerprints);
    expect(
        !gate.policy_identity_exact && !gate.passed,
        "wrong mixed frozen model must fail");

    auto top_only = panels;
    --top_only[0].summary.deck_bots[0][0].games;
    --top_only[0].summary.deck_bots[0][0].losses;
    ++top_only[0].summary.deck_bots[0][1].games;
    ++top_only[0].summary.deck_bots[0][1].losses;
    gate = joint::evaluate_mixed_field_pool(
        top_only, kFingerprints);
    expect(
        !gate.accounting_exact && !gate.passed,
        "top-level marginal redistribution must fail child reconciliation");

    auto wrong_pair = panels;
    wrong_pair[0].summary.matchups[0].second_deck =
        DeckId::Blue;
    gate = joint::evaluate_mixed_field_pool(
        wrong_pair, kFingerprints);
    expect(
        !gate.accounting_exact && !gate.passed,
        "wrong deck-matchup identity must fail");

    auto wrong_play_draw = panels;
    auto& cell =
        wrong_play_draw[0].summary
            .matchups[0].result.deck_bots[0][0];
    --cell.on_play_games;
    ++cell.on_draw_games;
    gate = joint::evaluate_mixed_field_pool(
        wrong_play_draw, kFingerprints);
    expect(
        !gate.accounting_exact && !gate.passed,
        "39/41 play-draw redistribution must fail");

    auto wrong_bot_outcomes = panels;
    auto& matchup_bot =
        wrong_bot_outcomes[0].summary
            .matchups[0].result.bot_matchups[0];
    auto& panel_bot =
        wrong_bot_outcomes[0].summary
            .bot_matchups[0];
    ++matchup_bot.first_wins;
    --matchup_bot.second_wins;
    ++panel_bot.first_wins;
    --panel_bot.second_wins;
    gate = joint::evaluate_mixed_field_pool(
        wrong_bot_outcomes, kFingerprints);
    expect(
        !gate.accounting_exact && !gate.passed,
        "bot-pair outcomes must reconcile with bot totals");

    auto overflow = panels;
    overflow[0].summary.deck_bots[0][0].games =
        std::numeric_limits<std::size_t>::max();
    overflow[0].summary.deck_bots[0][0].wins =
        std::numeric_limits<std::size_t>::max();
    overflow[0].summary.deck_bots[0][0].losses = 1;
    gate = joint::evaluate_mixed_field_pool(
        overflow, kFingerprints);
    expect(
        !gate.accounting_exact &&
            !gate.rates_finite &&
            !gate.passed,
        "mixed overflow must fail closed");
}

void test_stage_decision_suppresses_every_later_stage() {
    joint::StageOutcomes outcomes{
        .heldout_passed = true,
        .deep_reference_passed = true,
        .field_regression_passed = true,
        .treatment_vs_control_passed = true,
        .treatment_vs_parent_passed = true,
        .treatment_vs_handcoded_passed = true,
        .fixed_seed_panel_passed = true,
        .final_direct_pool_passed = std::nullopt,
        .mixed_field_passed = true,
    };
    auto decision =
        joint::evaluation_stage_decision(outcomes);
    expect(
        decision.run_final_direct_pool &&
            !decision.run_mixed_field &&
            !decision.complete &&
            !decision.passed,
        "mixed gate cannot run before pooled direct result");

    outcomes.final_direct_pool_passed = false;
    decision =
        joint::evaluation_stage_decision(outcomes);
    expect(
        decision.run_final_direct_pool &&
            !decision.run_mixed_field &&
            !decision.complete,
        "failed final direct pool suppresses mixed field");

    outcomes.final_direct_pool_passed = true;
    decision =
        joint::evaluation_stage_decision(outcomes);
    expect(
        decision.run_mixed_field &&
            decision.complete &&
            decision.passed,
        "all stages including final pool must pass");

    outcomes.field_regression_passed = false;
    decision =
        joint::evaluation_stage_decision(outcomes);
    expect(
        decision.run_field_regression &&
            !decision.run_treatment_vs_control &&
            !decision.run_final_direct_pool &&
            !decision.run_mixed_field &&
            !decision.complete,
        "field rejection suppresses all gameplay");

    outcomes.field_regression_passed = true;
    outcomes.heldout_passed = false;
    decision =
        joint::evaluation_stage_decision(outcomes);
    expect(
        !decision.run_deep_reference &&
            !decision.run_final_direct_pool &&
            !decision.run_mixed_field &&
            !decision.complete,
        "early failure suppresses supplied later wins");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "held-out exact inclusive guards",
        test_heldout_gate_accepts_exact_inclusive_guards);
    tests.run(
        "held-out pooled/deck boundaries",
        test_heldout_pooled_and_deck_boundaries_are_exact);
    tests.run(
        "held-out bias inheritance",
        test_heldout_bias_boundaries_and_inheritance);
    tests.run(
        "stable best-set paired boundaries",
        test_stable_best_set_uses_paired_strict_boundaries);
    tests.run(
        "common-state critic labels",
        test_common_state_critics_use_reference_max_for_both_models);
    tests.run(
        "deep equal metric boundaries",
        test_deep_gate_accepts_equal_metric_boundaries);
    tests.run(
        "deep metric regressions",
        test_deep_metric_regressions_fail_closed);
    tests.run(
        "per-probe stable losses",
        test_stable_losses_are_per_probe_and_not_net);
    tests.run(
        "required Blue probes",
        test_required_blue_probes_and_selections_are_conjunctive);
    tests.run(
        "Force Spike exact selections",
        test_force_spike_gate_requires_unique_exact_selections);
    tests.run(
        "deep accounting and finiteness",
        test_deep_gate_rejects_missing_or_nonfinite_rows);
    tests.run(
        "field reject-only stable losses",
        test_field_regression_gate_is_reject_only);
    tests.run(
        "field exact evidence",
        test_field_regression_evidence_fails_closed);
    tests.run(
        "direct challenger-deck semantics",
        test_direct_gameplay_gate_uses_challenger_own_deck_outcomes);
    tests.run(
        "direct strict/Wilson/count boundaries",
        test_direct_gameplay_strict_wilson_and_count_boundaries);
    tests.run(
        "fixed-seed exact non-losing panels",
        test_fixed_seed_panels_are_exact_and_non_losing);
    tests.run(
        "final pool exact deterministic merge",
        test_final_pool_merge_and_exact_accounting);
    tests.run(
        "final pool strength/matrix boundaries",
        test_final_pool_strength_and_matrix_boundaries);
    tests.run(
        "mixed-field all deck-policy lifts",
        test_mixed_field_gate_covers_all_decks_and_policies);
    tests.run(
        "mixed-field ties/counts/overflow",
        test_mixed_field_ties_counts_and_overflow_fail_closed);
    tests.run(
        "no-salvage stage suppression",
        test_stage_decision_suppresses_every_later_stage);
    return tests.finish();
}
