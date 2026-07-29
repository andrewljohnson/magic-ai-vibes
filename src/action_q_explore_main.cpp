#include "old_school/action_q_explore.hpp"
#include "old_school/action_q_field_gate.hpp"
#include "old_school/action_q_offline_gate.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq = old_school::action_q_explore;
namespace field = old_school::action_q_field_gate;
namespace offline = old_school::action_q_offline_gate;

constexpr std::string_view kParentArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
constexpr std::uint64_t kGameplaySeed = 202607281801ULL;
constexpr std::size_t kGameplayRepetitions = 1;
constexpr std::size_t kExpectedGameplayGames = 60;
constexpr std::size_t kExpectedGameplayGamesPerDeck = 12;
static_assert(
    aq::kHorizonTurns ==
    old_school::kLearnedValueSearchHorizonTurns);
static_assert(
    aq::kRolloutsPerWorld ==
    old_school::kLearnedValueSearchRolloutsPerWorld);

std::shared_ptr<const old_school::LearnedModel> load_parent() {
    const auto artifact =
        old_school::load_learned_value_challenger_artifact(
            std::string(kParentArtifactPath),
            800, 424242, 16);
    const auto model = artifact.model();
    if (old_school::learned_model_fingerprint(model) !=
        aq::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ0 loaded parent fingerprint drifted");
    }
    return model;
}

double score_for(
    const old_school::LearnedValuePriorityDiagnostic& diagnostic,
    const old_school::PriorityAction& wanted) {
    for (std::size_t index = 0;
         index < diagnostic.actions.size(); ++index) {
        if (diagnostic.actions[index] == wanted) {
            return diagnostic.scores[index];
        }
    }
    throw std::runtime_error(
        "AQ0 Ancestral preflight omitted a legal action");
}

void run_census_preflight(
    const std::shared_ptr<const old_school::LearnedModel>& parent) {
    const field::AncestralFieldRoot root =
        field::make_ancestral_field_root();
    const field::AncestralFieldRoot hidden =
        field::hidden_repartition_clone(root);
    const aq::RootCoordinate coordinate{
        .block = aq::kFitBlock,
        .schedule_index = 0,
        .pairing_index = 0,
        .game_seed = field::kCaptureGameSeed,
        .starting_player = root.state.starting_player,
        .actor = root.actor,
        .trace_ordinal = 0,
        .nontrivial_ordinal = 0,
        .seat_decks = {
            old_school::DeckId::Blue,
            old_school::DeckId::Red,
        },
        .search_seed =
            aq::root_search_seed(
                aq::kFitBlock, 0, root.actor, 0),
    };
    const aq::RootExample direct =
        aq::build_root_example(
            root.state, root.original_decks,
            root.context, coordinate, parent);
    const aq::RootExample hidden_direct =
        aq::build_root_example(
            hidden.state, hidden.original_decks,
            hidden.context, coordinate, parent);
    if (direct != hidden_direct) {
        throw std::runtime_error(
            "AQ0 root example exposed a hidden repartition");
    }

    old_school::LearnedSearchConfig resolved;
    resolved.seed = coordinate.search_seed;
    resolved.worlds = aq::kWorlds;
    resolved.rollouts_per_world =
        aq::kRolloutsPerWorld;
    resolved.horizon_turns = aq::kHorizonTurns;
    resolved.continuation_variant =
        old_school::LearnedVariant::ValueSearchChampion;
    resolved.blend_shallow_prior = true;
    resolved.value_resolved_shallow_prior_weight = 1.0;
    const auto sampled =
        old_school::learned_priority_action_samples(
            root.state, root.original_decks,
            root.actor, root.context.sorcery_actions,
            root.context.phase,
            root.context.consecutive_passes,
            root.legal_actions, parent, resolved);
    if (direct.actions != root.legal_actions ||
        direct.teacher_samples !=
            sampled.priority_shallow_prior_samples) {
        throw std::runtime_error(
            "AQ0 direct teacher differs from the resolved sampler");
    }

    const auto historic =
        field::score_learned_value(root, parent);
    const auto hidden_historic =
        field::score_learned_value(hidden, parent);
    const auto pass =
        old_school::PriorityAction::pass();
    const auto self =
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(root.actor));
    const auto opponent =
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(1 - root.actor));
    if (historic != hidden_historic ||
        historic.selected_action != opponent ||
        !(score_for(historic, opponent) >
              score_for(historic, pass) &&
          score_for(historic, pass) >
              score_for(historic, self)) ||
        std::abs(
            score_for(historic, pass) -
            0.9876290663) >= 1.0e-10 ||
        std::abs(
            score_for(historic, self) -
            0.9854725965) >= 1.0e-10 ||
        std::abs(
            score_for(historic, opponent) -
            0.9887476869) >= 1.0e-10) {
        throw std::runtime_error(
            "AQ0 frozen Ancestral witness drifted");
    }

    const auto logits =
        old_school::learned_policy_head_logits(
            direct.options,
            old_school::LearnedPolicyDecisionKind::Priority,
            parent);
    const auto scores =
        aq::combined_scores(
            direct.base_scores, logits,
            aq::kCandidateResidualWeight);
    auto reversed_options = direct.options;
    auto reversed_base = direct.base_scores;
    std::reverse(
        reversed_options.begin(), reversed_options.end());
    std::reverse(
        reversed_base.begin(), reversed_base.end());
    const auto reversed_logits =
        old_school::learned_policy_head_logits(
            reversed_options,
            old_school::LearnedPolicyDecisionKind::Priority,
            parent);
    auto reversed_scores =
        aq::combined_scores(
            reversed_base, reversed_logits,
            aq::kCandidateResidualWeight);
    std::reverse(
        reversed_scores.begin(), reversed_scores.end());
    if (scores != reversed_scores ||
        aq::exact_max_support(scores) !=
            aq::exact_max_support(reversed_scores)) {
        throw std::runtime_error(
            "AQ0 parent scores changed under action order");
    }
}

void print_block(
    const aq::CorpusBlock& block,
    std::string_view name) {
    const aq::BlockCensus& census = block.census;
    std::cout
        << "block name=" << name
        << " index=" << census.block
        << " games=" << census.games
        << " roots=" << census.retained_roots()
        << " options=" << census.retained_options()
        << " teacher_samples="
        << census.teacher_sample_count()
        << " base_score_calls="
        << census.base_score_calls
        << " base_worlds="
        << census.base_sampled_worlds
        << " base_rollouts="
        << census.base_rollout_evaluations
        << " base_terminal="
        << census.base_terminal_evaluations
        << " base_bootstrapped="
        << census.base_bootstrapped_evaluations
        << '\n';
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const aq::DeckCensus& row =
            census.decks[deck];
        std::cout
            << "deck block=" << name
            << " name="
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << " actor_games=" << row.actor_games
            << " nontrivial_roots="
            << row.nontrivial_roots
            << " retained_roots="
            << row.retained_roots
            << " retained_options="
            << row.retained_options
            << " teacher_samples="
            << row.teacher_samples
            << " teacher_zero="
            << row.teacher_exact_zero_samples
            << " teacher_one="
            << row.teacher_exact_one_samples
            << " spread_roots="
            << row.nonzero_spread_roots
            << " min_width="
            << row.minimum_legal_width
            << " max_width="
            << row.maximum_legal_width
            << " total_weight="
            << std::setprecision(17)
            << row.root_weight << '\n';
    }
}

struct FrozenDeckCensus {
    std::size_t nontrivial_roots;
    std::size_t retained_roots;
    std::size_t retained_options;
    std::size_t teacher_samples;
    std::size_t teacher_exact_zero_samples;
    std::size_t teacher_exact_one_samples;
    std::size_t nonzero_spread_roots;
    std::size_t minimum_legal_width;
    std::size_t maximum_legal_width;
};

struct FrozenBlockCensus {
    std::size_t block;
    std::size_t retained_roots;
    std::size_t retained_options;
    std::size_t teacher_samples;
    std::size_t base_score_calls;
    std::size_t base_sampled_worlds;
    std::size_t base_rollout_evaluations;
    std::size_t base_terminal_evaluations;
    std::size_t base_bootstrapped_evaluations;
    std::array<FrozenDeckCensus, old_school::kDeckCount> decks;
};

constexpr FrozenBlockCensus kFrozenFitCensus{
    .block = aq::kFitBlock,
    .retained_roots = 632,
    .retained_options = 2085,
    .teacher_samples = 16680,
    .base_score_calls = 632,
    .base_sampled_worlds = 5056,
    .base_rollout_evaluations = 16680,
    .base_terminal_evaluations = 6369,
    .base_bootstrapped_evaluations = 10311,
    .decks = {{
        {255, 128, 351, 2808, 0, 0, 128, 2, 8},
        {301, 128, 497, 3976, 8, 64, 128, 2, 13},
        {355, 120, 290, 2320, 0, 0, 120, 2, 5},
        {458, 128, 421, 3368, 0, 0, 128, 2, 7},
        {301, 128, 526, 4208, 0, 0, 128, 2, 34},
    }},
};

constexpr FrozenBlockCensus kFrozenCheckCensus{
    .block = aq::kCheckBlock,
    .retained_roots = 640,
    .retained_options = 2007,
    .teacher_samples = 16056,
    .base_score_calls = 640,
    .base_sampled_worlds = 5120,
    .base_rollout_evaluations = 16056,
    .base_terminal_evaluations = 5496,
    .base_bootstrapped_evaluations = 10560,
    .decks = {{
        {334, 128, 325, 2600, 0, 0, 128, 2, 7},
        {332, 128, 417, 3336, 8, 24, 128, 2, 12},
        {404, 128, 321, 2568, 0, 0, 128, 2, 10},
        {514, 128, 386, 3088, 0, 0, 128, 2, 9},
        {329, 128, 558, 4464, 24, 24, 128, 2, 50},
    }},
};

void require_frozen_block(
    const aq::CorpusBlock& block,
    const FrozenBlockCensus& expected,
    std::string_view name) {
    const aq::BlockCensus& actual = block.census;
    if (block.block != expected.block ||
        actual.block != expected.block ||
        actual.games != 40 ||
        actual.retained_roots() != expected.retained_roots ||
        actual.retained_options() != expected.retained_options ||
        actual.teacher_sample_count() != expected.teacher_samples ||
        actual.base_score_calls != expected.base_score_calls ||
        actual.base_sampled_worlds != expected.base_sampled_worlds ||
        actual.base_rollout_evaluations !=
            expected.base_rollout_evaluations ||
        actual.base_terminal_evaluations !=
            expected.base_terminal_evaluations ||
        actual.base_bootstrapped_evaluations !=
            expected.base_bootstrapped_evaluations) {
        throw std::runtime_error(
            "AQ0 " + std::string(name) +
            " global census drifted from the notebook freeze");
    }
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const aq::DeckCensus& row = actual.decks[deck];
        const FrozenDeckCensus& frozen = expected.decks[deck];
        if (row.actor_games != 16 ||
            row.nontrivial_roots != frozen.nontrivial_roots ||
            row.retained_roots != frozen.retained_roots ||
            row.retained_options != frozen.retained_options ||
            row.teacher_samples != frozen.teacher_samples ||
            row.teacher_exact_zero_samples !=
                frozen.teacher_exact_zero_samples ||
            row.teacher_exact_one_samples !=
                frozen.teacher_exact_one_samples ||
            row.nonzero_spread_roots !=
                frozen.nonzero_spread_roots ||
            row.minimum_legal_width !=
                frozen.minimum_legal_width ||
            row.maximum_legal_width !=
                frozen.maximum_legal_width ||
            row.root_weight != 1.0) {
            throw std::runtime_error(
                "AQ0 " + std::string(name) + " " +
                std::string(old_school::deck_name(
                    static_cast<old_school::DeckId>(deck))) +
                " census drifted from the notebook freeze");
        }
    }
}

void require_frozen_census(const aq::Corpus& corpus) {
    if (corpus.root_seed != aq::kCollectionRootSeed ||
        corpus.parent_fingerprint !=
            aq::kRequiredParentFingerprint) {
        throw std::runtime_error(
            "AQ0 corpus identity drifted from the notebook freeze");
    }
    require_frozen_block(
        corpus.fit, kFrozenFitCensus, "FIT");
    require_frozen_block(
        corpus.check, kFrozenCheckCensus, "CHECK");
}

void print_metrics(
    const aq::Metrics& metrics,
    std::string_view split,
    std::string_view policy) {
    std::cout
        << std::setprecision(17)
        << "metrics split=" << split
        << " policy=" << policy
        << " roots=" << metrics.roots
        << " options=" << metrics.options
        << " top1_expected_agreement="
        << metrics.equal_deck_top_one_expected_agreement
        << " mean_regret="
        << metrics.equal_deck_mean_regret << '\n';
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const aq::DeckMetrics& row = metrics.decks[deck];
        std::cout
            << "metrics_deck split=" << split
            << " policy=" << policy
            << " deck="
            << old_school::deck_name(row.deck)
            << " roots=" << row.roots
            << " options=" << row.options
            << " top1_expected_agreement="
            << row.top_one_expected_agreement
            << " mean_regret=" << row.mean_regret << '\n';
    }
}

void print_probe_metrics(
    const old_school::probe_eval::ProbeMetricSummary& metrics,
    std::string_view policy) {
    std::cout
        << std::setprecision(17)
        << "dev_metrics policy=" << policy
        << " probes=" << metrics.probe_count
        << " stable_pairs=" << metrics.stable_pair_count
        << " top1_expected_agreement="
        << metrics.top1_expected_agreement
        << " stable_pair_agreement="
        << metrics.stable_pair_agreement
        << " mean_regret=" << metrics.mean_regret
        << " critic_brier=" << metrics.critic_brier
        << " critic_log_loss=" << metrics.critic_log_loss
        << " critic_bias=" << metrics.critic_bias
        << " critic_ece=" << metrics.critic_ece << '\n';
    for (const auto& row : metrics.by_deck) {
        std::cout
            << "dev_metrics_deck policy=" << policy
            << " deck=" << old_school::deck_name(row.root_deck)
            << " probes=" << row.probe_count
            << " stable_pairs=" << row.stable_pair_count
            << " top1_expected_agreement="
            << row.top1_expected_agreement
            << " stable_pair_agreement="
            << row.stable_pair_agreement
            << " mean_regret=" << row.mean_regret << '\n';
    }
}

void print_keys(
    std::string_view label,
    const std::vector<std::string>& keys) {
    std::cout << label << '=';
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }
        std::cout << keys[index];
    }
    std::cout << '\n';
}

void print_offline_report(
    const aq::Corpus& corpus,
    const aq::FitReport& fit,
    const offline::Report& report,
    double collection_seconds,
    double fit_seconds,
    double gate_seconds) {
    std::cout
        << "schema=old-school-action-q-aq0-run-v1\n"
        << "mode=run\n"
        << "parent_artifact=" << kParentArtifactPath << '\n'
        << "parent_fingerprint="
        << report.parent_fingerprint << '\n'
        << "candidate_fingerprint="
        << report.candidate_fingerprint << '\n'
        << "parent_component_critic="
        << fit.parent_components.critic << '\n'
        << "parent_component_priority="
        << fit.parent_components.priority << '\n'
        << "parent_component_attack="
        << fit.parent_components.attack << '\n'
        << "parent_component_block="
        << fit.parent_components.block << '\n'
        << "parent_component_damage_order="
        << fit.parent_components.damage_order << '\n'
        << "candidate_component_critic="
        << fit.candidate_components.critic << '\n'
        << "candidate_component_priority="
        << fit.candidate_components.priority << '\n'
        << "candidate_component_attack="
        << fit.candidate_components.attack << '\n'
        << "candidate_component_block="
        << fit.candidate_components.block << '\n'
        << "candidate_component_damage_order="
        << fit.candidate_components.damage_order << '\n'
        << "root_seed=" << corpus.root_seed << '\n'
        << "fit_seed=" << aq::fit_seed() << '\n'
        << "collection_seconds=" << std::setprecision(6)
        << collection_seconds
        << " fit_seconds=" << fit_seconds
        << " offline_gate_seconds=" << gate_seconds << '\n'
        << "fit_examples=" << fit.fit_examples
        << " fit_options=" << fit.fit_options
        << " batch_size=" << fit.optimizer.batch_size
        << " epochs=" << fit.optimizer.epochs
        << " learning_rate=" << fit.optimizer.learning_rate
        << " residual_weight=" << fit.optimizer.residual_weight
        << '\n';
    print_block(corpus.fit, "FIT");
    print_block(corpus.check, "CHECK");
    print_metrics(fit.parent_fit, "FIT", "parent");
    print_metrics(fit.candidate_fit, "FIT", "candidate");
    print_metrics(report.check.parent, "CHECK", "parent");
    print_metrics(report.check.candidate, "CHECK", "candidate");
    print_probe_metrics(report.frozen_dev.parent, "parent");
    print_probe_metrics(report.frozen_dev.candidate, "candidate");
    std::cout
        << "isolation parent_identity_exact="
        << report.isolation.parent_identity_exact
        << " candidate_identity_exact="
        << report.isolation.candidate_identity_exact
        << " parent_immutable="
        << report.isolation.parent_immutable
        << " repeated_fit_bit_identical="
        << report.isolation.repeated_fit_bit_identical
        << " only_priority_component_changed="
        << report.isolation.only_priority_component_changed << '\n'
        << "check regret_strictly_improved="
        << report.check.regret_strictly_improved
        << " top_one_not_lower="
        << report.check.top_one_not_lower
        << " metrics_match_fit_report="
        << report.check.metrics_match_fit_report << '\n';
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        std::cout
            << "check_deck_guard deck="
            << old_school::deck_name(
                   static_cast<old_school::DeckId>(deck))
            << " passed="
            << report.check.deck_regret_guard[deck] << '\n';
    }
    std::cout
        << "frozen_dev labels=" << report.frozen_dev.labels
        << " stable_parent_agreements="
        << report.frozen_dev.stable_parent_agreements
        << " lost_stable_parent_agreements="
        << report.frozen_dev.lost_stable_parent_agreements
        << " pair_hidden_repartition="
        << report.frozen_dev.pair_hidden_repartition.passed
        << " explicit_hidden_repartition="
        << report.frozen_dev.explicit_hidden_repartition.passed
        << " pooled_regret_no_worse="
        << report.frozen_dev.pooled_regret_no_worse
        << " cache_bytes="
        << report.frozen_dev.cache_before.byte_size
        << " cache_sha256="
        << report.frozen_dev.cache_before.sha256
        << " cache_unchanged="
        << (report.frozen_dev.cache_before ==
            report.frozen_dev.cache_after)
        << '\n'
        << "ancestral self_score="
        << report.ancestral.self_score
        << " opponent_score="
        << report.ancestral.opponent_score
        << " support_size="
        << report.ancestral.selected_support.size()
        << " legal_action_count="
        << report.ancestral.legal_actions.size()
        << " complete_legal_actions_exact="
        << report.ancestral.complete_legal_actions_exact
        << " information_action_fingerprint="
        << report.ancestral.information_action_fingerprint
        << " fingerprint_exact="
        << report.ancestral
               .information_action_fingerprint_exact
        << " hidden_repartition="
        << report.ancestral.hidden_repartition_bit_identical
        << " self_above_opponent="
        << report.ancestral.self_strictly_above_opponent
        << " opponent_absent_from_support="
        << report.ancestral.opponent_absent_from_support << '\n'
        << "descriptor_order models="
        << report.descriptor_order.model_count
        << " probes=" << report.descriptor_order.probe_count
        << " scores_bit_identical="
        << report.descriptor_order.action_keyed_scores_bit_identical
        << " supports_identical="
        << report.descriptor_order.selected_supports_identical
        << " hidden_models="
        << report.descriptor_order.hidden_model_count
        << " hidden_probes="
        << report.descriptor_order.hidden_probe_count
        << " hidden_distinct_owner_equivalent="
        << report.descriptor_order
               .hidden_repartitions_distinct_owner_equivalent
        << " hidden_scores_bit_identical="
        << report.descriptor_order
               .hidden_action_keyed_scores_bit_identical
        << " hidden_supports_identical="
        << report.descriptor_order
               .hidden_selected_supports_identical
        << '\n'
        << "behavior force_spike_hidden_repartition="
        << report.behavior.force_spike.hidden_repartition_passed
        << " live_force_spike="
        << report.behavior.live_force_spike_preserved
        << " one_open_payable_pass_descriptive="
        << report.behavior.one_open_payable_selects_pass
        << " five_open_payable_pass="
        << report.behavior.five_open_force_spike_selects_pass
        << " redundant_counter_pass="
        << report.behavior.redundant_counter_selects_pass
        << " intervening_counter_correct="
        << report.behavior
               .intervening_counter_selects_opposing_counter
        << " sick_bear_growth_pass="
        << report.behavior.sick_bear_growth_selects_pass
        << " opponent_growth_excluded="
        << report.behavior.opponent_growth_excluded
        << " braingeyser_x0_excluded="
        << report.behavior.braingeyser_x_zero_excluded << '\n';
    print_keys(
        "behavior_five_open_selection",
        report.behavior.five_open_selected_keys);
    print_keys(
        "behavior_redundant_counter_selection",
        report.behavior.redundant_counter_selected_keys);
    print_keys(
        "behavior_intervening_counter_selection",
        report.behavior.intervening_counter_selected_keys);
    print_keys(
        "behavior_sick_bear_growth_selection",
        report.behavior.sick_bear_growth_selected_keys);
    print_keys(
        "behavior_opponent_growth_selection",
        report.behavior.opponent_growth_selected_keys);
    print_keys(
        "behavior_braingeyser_selection",
        report.behavior.braingeyser_selected_keys);
}

int run_census() {
    const auto parent = load_parent();
    run_census_preflight(parent);
    const aq::Corpus corpus =
        aq::collect_corpus(parent);
    require_frozen_census(corpus);
    std::cout
        << "schema=old-school-action-q-aq0-census-v1\n"
        << "mode=census\n"
        << "parent_artifact=" << kParentArtifactPath << '\n'
        << "parent_fingerprint="
        << corpus.parent_fingerprint << '\n'
        << "root_seed=" << corpus.root_seed << '\n'
        << "schedule_generation="
        << aq::kScheduleGeneration << '\n'
        << "fit_seed=" << aq::fit_seed() << '\n'
        << "worlds=" << aq::kWorlds
        << " horizon=" << aq::kHorizonTurns
        << " rollouts_per_world="
        << aq::kRolloutsPerWorld
        << " root_cap="
        << aq::kMaximumRootsPerActorGame
        << " source_turn_cap="
        << aq::kSourceTurnCap << '\n';
    std::cout
        << "preflight direct_teacher_bit_identical=1"
        << " hidden_repartition_bit_identical=1"
        << " action_order_bit_identical=1"
        << " ancestral_witness_exact=1\n";
    print_block(corpus.fit, "FIT");
    print_block(corpus.check, "CHECK");
    std::cout
        << "result=PASS disposition=CENSUS_ONLY"
        << " model_created=0 gameplay_seed_opened=0\n";
    return 0;
}

old_school::BotConfig aq0_bot(
    std::shared_ptr<const old_school::LearnedModel> model,
    double residual_weight) {
    return {
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = aq::kWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = residual_weight,
        .value_pass_dominance = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_adversarial_blocks = false,
        .value_continuation_controller =
            old_school::LearnedContinuationController::Legacy,
        .training_games = 800,
        .learned_model = std::move(model),
    };
}

void require_frozen_gameplay(
    const old_school::BotBenchmarkSummary& result,
    const std::shared_ptr<const old_school::LearnedModel>& parent,
    const std::shared_ptr<const old_school::LearnedModel>& candidate) {
    if (result.evaluation_seed != kGameplaySeed ||
        result.learned_training_seed != 424242 ||
        result.repetitions_per_deck_pairing !=
            kGameplayRepetitions ||
        result.total_games != kExpectedGameplayGames ||
        result.challenger_stats.games != kExpectedGameplayGames ||
        result.baseline_stats.games != kExpectedGameplayGames ||
        result.challenger_model_fingerprint !=
            old_school::learned_model_fingerprint(candidate) ||
        result.baseline_model_fingerprint !=
            old_school::learned_model_fingerprint(parent) ||
        result.challenger.learned_model != candidate ||
        result.baseline.learned_model != parent ||
        result.challenger.kind != old_school::BotKind::Learned ||
        result.baseline.kind != old_school::BotKind::Learned ||
        result.challenger.learned_variant !=
            old_school::LearnedVariant::ValueSearchChampion ||
        result.baseline.learned_variant !=
            old_school::LearnedVariant::ValueSearchChampion ||
        result.challenger.rollouts_per_action != aq::kWorlds ||
        result.baseline.rollouts_per_action != aq::kWorlds ||
        result.challenger.exploration_rate != 0.0 ||
        result.baseline.exploration_rate != 0.0 ||
        result.challenger.value_continuation_epsilon != 0.0 ||
        result.baseline.value_continuation_epsilon != 0.0 ||
        result.challenger.value_priority_residual_weight !=
            aq::kCandidateResidualWeight ||
        result.baseline.value_priority_residual_weight != 0.0 ||
        result.challenger.value_pass_dominance ||
        result.baseline.value_pass_dominance ||
        result.challenger.value_resolved_shallow_prior_weight != 0.0 ||
        result.baseline.value_resolved_shallow_prior_weight != 0.0 ||
        result.challenger.value_adversarial_blocks ||
        result.baseline.value_adversarial_blocks ||
        result.challenger.value_continuation_controller !=
            old_school::LearnedContinuationController::Legacy ||
        result.baseline.value_continuation_controller !=
            old_school::LearnedContinuationController::Legacy ||
        result.challenger.training_games != 800 ||
        result.baseline.training_games != 800) {
        throw std::runtime_error(
            "AQ0 gameplay result drifted from the frozen selector");
    }
    const auto accounted =
        [](const old_school::BotSimulationStats& stats) {
            return stats.games ==
                stats.wins + stats.losses + stats.draws;
        };
    if (!accounted(result.challenger_stats) ||
        !accounted(result.baseline_stats) ||
        result.challenger_stats.wins !=
            result.baseline_stats.losses ||
        result.challenger_stats.losses !=
            result.baseline_stats.wins ||
        result.challenger_stats.draws !=
            result.baseline_stats.draws ||
        result.life_total_finishes +
                result.empty_library_finishes +
                result.turn_limit_draws !=
            kExpectedGameplayGames ||
        result.challenger_quartet_cr1.clusters != 15 ||
        result.challenger_quartet_cr1.records !=
            kExpectedGameplayGames) {
        throw std::runtime_error(
            "AQ0 gameplay aggregate accounting drifted");
    }
    std::size_t challenger_deck_games = 0;
    std::size_t baseline_deck_games = 0;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto& challenger_deck =
            result.challenger_decks[deck];
        const auto& baseline_deck =
            result.baseline_decks[deck];
        challenger_deck_games += challenger_deck.games;
        baseline_deck_games += baseline_deck.games;
        if (challenger_deck.games !=
                kExpectedGameplayGamesPerDeck ||
            baseline_deck.games !=
                kExpectedGameplayGamesPerDeck ||
            challenger_deck.wins + challenger_deck.losses +
                    challenger_deck.draws !=
                challenger_deck.games ||
            baseline_deck.wins + baseline_deck.losses +
                    baseline_deck.draws !=
                baseline_deck.games ||
            challenger_deck.on_play_games != 6 ||
            challenger_deck.on_draw_games != 6 ||
            baseline_deck.on_play_games != 6 ||
            baseline_deck.on_draw_games != 6) {
            throw std::runtime_error(
                "AQ0 gameplay deck balance drifted");
        }
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                if (result.challenger_outcome_quadrants
                        [deck][seat][play_draw]
                            .games != 3 ||
                    result.baseline_outcome_quadrants
                        [deck][seat][play_draw]
                            .games != 3) {
                    throw std::runtime_error(
                        "AQ0 gameplay quadrant balance drifted");
                }
            }
        }
        for (std::size_t opposing_deck = 0;
             opposing_deck < old_school::kDeckCount;
             ++opposing_deck) {
            const std::size_t expected =
                deck == opposing_deck ? 4 : 2;
            if (result.challenger_deck_matchups
                    [deck][opposing_deck]
                        .games != expected) {
                throw std::runtime_error(
                    "AQ0 gameplay matchup matrix drifted");
            }
        }
    }
    if (challenger_deck_games != kExpectedGameplayGames ||
        baseline_deck_games != kExpectedGameplayGames) {
        throw std::runtime_error(
            "AQ0 gameplay deck accounting drifted");
    }
}

void print_gameplay(
    const old_school::BotBenchmarkSummary& result,
    double gameplay_seconds) {
    std::cout
        << std::setprecision(17)
        << "gameplay seed=" << result.evaluation_seed
        << " repetitions="
        << result.repetitions_per_deck_pairing
        << " games=" << result.total_games
        << " challenger_wins="
        << result.challenger_stats.wins
        << " challenger_losses="
        << result.challenger_stats.losses
        << " draws=" << result.challenger_stats.draws
        << " win_rate=" << result.challenger_win_rate()
        << " seconds=" << gameplay_seconds
        << " challenger_decisions="
        << result.challenger_stats.total_decisions
        << " challenger_rollouts="
        << result.challenger_stats.total_rollouts
        << " baseline_decisions="
        << result.baseline_stats.total_decisions
        << " baseline_rollouts="
        << result.baseline_stats.total_rollouts
        << " challenger_rollouts_per_decision="
        << result.challenger_stats
               .average_rollouts_per_decision()
        << " baseline_rollouts_per_decision="
        << result.baseline_stats
               .average_rollouts_per_decision()
        << " total_turns=" << result.total_turns
        << " life_finishes=" << result.life_total_finishes
        << " library_finishes="
        << result.empty_library_finishes
        << " turn_limit_draws="
        << result.turn_limit_draws
        << " cr1_mean="
        << result.challenger_quartet_cr1.mean
        << " cr1_low95="
        << result.challenger_quartet_cr1.confidence_low_95
        << " cr1_high95="
        << result.challenger_quartet_cr1.confidence_high_95
        << " schedule_accounting=PASS"
        << '\n';
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const auto id =
            static_cast<old_school::DeckId>(deck);
        const auto& challenger =
            result.challenger_decks[deck];
        const auto& baseline =
            result.baseline_decks[deck];
        std::cout
            << "gameplay_deck deck="
            << old_school::deck_name(id)
            << " games=" << challenger.games
            << " challenger="
            << challenger.wins << '-'
            << challenger.losses << '-'
            << challenger.draws
            << " challenger_win_rate="
            << challenger.win_rate()
            << " on_play="
            << challenger.on_play_wins << '/'
            << challenger.on_play_games
            << " on_draw="
            << challenger.on_draw_wins << '/'
            << challenger.on_draw_games
            << " baseline="
            << baseline.wins << '-'
            << baseline.losses << '-'
            << baseline.draws << '\n';
    }
}

int run_experiment() {
    using Clock = std::chrono::steady_clock;
    const auto parent = load_parent();
    run_census_preflight(parent);

    const auto collection_start = Clock::now();
    const aq::Corpus corpus = aq::collect_corpus(parent);
    const auto collection_end = Clock::now();
    require_frozen_census(corpus);

    const auto fit_start = Clock::now();
    const aq::FitReport fit = aq::fit(corpus, parent);
    const auto fit_end = Clock::now();
    if (!fit.candidate) {
        throw std::runtime_error(
            "AQ0 fit produced a null candidate");
    }

    const auto gate_start = Clock::now();
    const offline::Report report =
        offline::evaluate(
            corpus, fit, parent, fit.candidate);
    const auto gate_end = Clock::now();
    const auto seconds =
        [](Clock::time_point start, Clock::time_point end) {
            return std::chrono::duration<double>(
                       end - start)
                .count();
        };
    print_offline_report(
        corpus, fit, report,
        seconds(collection_start, collection_end),
        seconds(fit_start, fit_end),
        seconds(gate_start, gate_end));

    if (!report.gate_passed()) {
        for (const std::string& failure : report.failures()) {
            std::cout << "offline_failure=" << failure << '\n';
        }
        std::cout
            << "result=REJECT stage=OFFLINE"
            << " gameplay_seed_opened=0 artifact_published=0\n";
        return 1;
    }

    old_school::GameConfig game;
    game.max_turns = 500;
    game.learned_training_seed = 424242;
    game.learned_search_depth = 1;
    const old_school::BotConfig challenger =
        aq0_bot(
            fit.candidate,
            aq::kCandidateResidualWeight);
    const old_school::BotConfig baseline =
        aq0_bot(parent, 0.0);
    if (parent == fit.candidate ||
        game.learned_model ||
        old_school::learned_model_fingerprint(parent) !=
            aq::kRequiredParentFingerprint ||
        old_school::learned_model_fingerprint(fit.candidate) !=
            report.candidate_fingerprint ||
        report.candidate_fingerprint ==
            report.parent_fingerprint) {
        throw std::runtime_error(
            "AQ0 gameplay boundary identity check failed");
    }
    const std::string parent_before =
        old_school::learned_model_fingerprint(parent);
    const std::string candidate_before =
        old_school::learned_model_fingerprint(fit.candidate);
    const auto parent_components_before =
        old_school::learned_model_component_fingerprints(parent);
    const auto candidate_components_before =
        old_school::learned_model_component_fingerprints(
            fit.candidate);
    std::cout
        << "gameplay_recipe seed=" << kGameplaySeed
        << " repetitions=" << kGameplayRepetitions
        << " strict_wins_required=31"
        << " worlds=" << aq::kWorlds
        << " horizon=" << aq::kHorizonTurns
        << " rollouts_per_world=" << aq::kRolloutsPerWorld
        << " candidate_residual="
        << aq::kCandidateResidualWeight
        << " baseline_residual=0"
        << " exploration=0 continuation_epsilon=0"
        << " pass_dominance=0 resolved_alpha=0"
        << " adversarial_blocks=0 continuation=Legacy"
        << " max_turns=" << game.max_turns << '\n';
    const auto gameplay_start = Clock::now();
    const old_school::BotBenchmarkSummary gameplay =
        old_school::run_bot_benchmark(
            kGameplayRepetitions, kGameplaySeed,
            challenger, baseline, game, false);
    const auto gameplay_end = Clock::now();
    require_frozen_gameplay(
        gameplay, parent, fit.candidate);
    if (old_school::learned_model_fingerprint(parent) !=
            parent_before ||
        old_school::learned_model_fingerprint(fit.candidate) !=
            candidate_before ||
        old_school::learned_model_component_fingerprints(parent) !=
            parent_components_before ||
        old_school::learned_model_component_fingerprints(
            fit.candidate) != candidate_components_before) {
        throw std::runtime_error(
            "AQ0 gameplay mutated a frozen model");
    }
    print_gameplay(
        gameplay,
        seconds(gameplay_start, gameplay_end));
    const bool advances =
        gameplay.challenger_stats.wins > 30;
    std::cout
        << "result=" << (advances ? "PASS" : "REJECT")
        << " stage=GAMEPLAY disposition="
        << (advances
                ? "MANUAL_PILOT_ELIGIBLE"
                : "AQ0_CLOSED")
        << " gameplay_seed_opened=1 artifact_published=0\n";
    return advances ? 0 : 1;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-action-q-explore "
           "(--census|--run)\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        print_usage(std::cerr);
        return 2;
    }
    const std::string_view mode = argv[1];
    try {
        if (mode == "--census") {
            return run_census();
        }
        if (mode == "--run") {
            return run_experiment();
        }
        print_usage(std::cerr);
        return 2;
    } catch (const std::exception& error) {
        std::cerr
            << "result=ERROR reason=action_q_explore_failed"
            << " message=" << error.what() << '\n';
        return 1;
    }
}
