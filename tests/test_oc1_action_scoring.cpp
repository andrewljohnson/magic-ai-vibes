#include "old_school/oc1_action_scoring.hpp"

#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace scoring = old_school::oc1_action_scoring;
namespace probes = old_school::probes;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_invalid(Function&& function,
                    std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool bit_identical(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool bit_identical(const std::vector<double>& first,
                   const std::vector<double>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index) {
        if (!bit_identical(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<const old_school::LearnedModel> test_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0x41523154455354ULL);
    return model;
}

probes::DecisionProbe priority_probe() {
    return probes::make_force_spike_policy_controls_v1().front();
}

probes::DecisionProbe attack_probe() {
    return probes::make_attack_regression_v1().front();
}

probes::DecisionProbe block_probe() {
    const auto corpus = probes::make_field_regressions_v1();
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [](const probes::DecisionProbe& probe) {
            return probe.decision_kind ==
                   probes::DecisionKind::Block;
        });
    if (found == corpus.end()) {
        throw std::runtime_error(
            "field corpus has no Block probe");
    }
    return *found;
}

probes::DecisionProbe empty_priority_probe() {
    probes::DecisionProbe probe;
    probe.stable_id = "test.ar1.empty-priority.v1";
    probe.decision_kind = probes::DecisionKind::Priority;
    probe.root_player = 0;
    probe.phase = old_school::TurnPhase::FirstMain;
    probe.consecutive_passes = 0;
    probe.state.turn_number = 1;
    probe.state.active_player = 0;
    probe.state.starting_player = 0;
    probe.candidates = {
        probes::Candidate{
            .descriptor = "pass",
            .action = old_school::PriorityAction::pass(),
        },
    };
    return probe;
}

scoring::SearchRecipe small_recipe() {
    return {
        .seed_tag = "old-school-ar1-scorer-unit-v1",
        .seed_base = 0xA11CEULL,
        .worlds = 2,
        .horizon_turns = 0,
        .rollouts_per_world = 1,
        .blend_shallow_prior = false,
        .evaluation_threads = 2,
    };
}

const scoring::DescriptorScore& action_for(
    const scoring::DecisionScore& score,
    std::string_view descriptor) {
    const auto found = std::find_if(
        score.actions.begin(), score.actions.end(),
        [&](const scoring::DescriptorScore& action) {
            return action.descriptor == descriptor;
        });
    if (found == score.actions.end()) {
        throw std::runtime_error(
            "scored result omitted a descriptor");
    }
    return *found;
}

void require_accounting(
    const scoring::DecisionScore& score,
    std::size_t worlds, std::size_t rollouts) {
    const std::size_t expected =
        score.actions.size() * worlds * rollouts;
    expect(score.accounting.sampled_worlds == worlds,
           "sampled-world accounting is wrong");
    expect(score.accounting.rollout_evaluations == expected,
           "rollout accounting is wrong");
    expect(
        score.accounting.terminal_evaluations +
                score.accounting.bootstrapped_evaluations ==
            expected,
        "terminal/bootstrap accounting does not cross-sum");
    for (const auto& action : score.actions) {
        expect(action.raw_samples.size() == worlds * rollouts,
               "descriptor sample width is wrong");
        expect(std::isfinite(action.raw_score),
               "descriptor raw score is non-finite");
        for (const double sample : action.raw_samples) {
            expect(std::isfinite(sample),
                   "descriptor raw sample is non-finite");
        }
    }
}

std::string descriptor_for_attack_branch(
    const probes::DecisionProbe& probe, bool include) {
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* attack =
            std::get_if<probes::BinaryAttackDecision>(
                &candidate.action);
        if (attack != nullptr &&
            attack->include == include) {
            return candidate.descriptor;
        }
    }
    throw std::runtime_error("Attack branch is missing");
}

std::string descriptor_for_block_branch(
    const probes::DecisionProbe& probe, bool include) {
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* block =
            std::get_if<probes::BinaryBlockDecision>(
                &candidate.action);
        if (block != nullptr &&
            block->include == include) {
            return candidate.descriptor;
        }
    }
    throw std::runtime_error("Block branch is missing");
}

old_school::PermanentId attack_subject(
    const probes::DecisionProbe& probe) {
    return std::get<probes::BinaryAttackDecision>(
               probe.candidates.front().action)
        .attacker;
}

std::pair<old_school::PermanentId, old_school::PermanentId>
block_subject(const probes::DecisionProbe& probe) {
    const auto& block =
        std::get<probes::BinaryBlockDecision>(
            probe.candidates.front().action);
    return {block.attacker, block.blocker};
}

old_school::LearnedSearchConfig direct_config(
    const scoring::DecisionScore& score) {
    expect(score.recipe.resolved_seed.has_value(),
           "search score omitted its resolved seed");
    return {
        .seed = *score.recipe.resolved_seed,
        .worlds = score.recipe.worlds,
        .rollouts_per_world =
            score.recipe.rollouts_per_world,
        .horizon_turns =
            score.recipe.horizon_turns,
        .continuation_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior =
            score.recipe.blend_shallow_prior,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            old_school::LearnedContinuationController::Legacy,
        .evaluation_threads =
            score.recipe.evaluation_threads,
    };
}

probes::DecisionProbe descriptor_canonical_probe(
    probes::DecisionProbe probe) {
    std::sort(
        probe.candidates.begin(), probe.candidates.end(),
        [](const probes::Candidate& left,
           const probes::Candidate& right) {
            return left.descriptor < right.descriptor;
        });
    return probe;
}

std::vector<old_school::PriorityAction> priority_actions(
    const probes::DecisionProbe& probe) {
    std::vector<old_school::PriorityAction> actions;
    actions.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate :
         probe.candidates) {
        actions.push_back(
            std::get<old_school::PriorityAction>(
                candidate.action));
    }
    return actions;
}

double exact_mean(const std::vector<double>& samples) {
    double sum = 0.0;
    for (const double sample : samples) {
        sum += sample;
    }
    return sum / static_cast<double>(samples.size());
}

void test_frozen_recipe_and_seed_constants() {
    expect(
        scoring::kBalancedReferenceRecipe ==
            scoring::SearchRecipe{
                .seed_tag =
                    "old-school-oc1-action-regression-v1."
                    "value-balanced",
                .seed_base = 1414213562ULL,
                .worlds = 64,
                .horizon_turns = 8,
                .rollouts_per_world = 1,
                .blend_shallow_prior = false,
                .evaluation_threads = 4,
            },
        "balanced reference recipe drifted");
    expect(
        scoring::kProductionPriorityRecipe ==
            scoring::SearchRecipe{
                .seed_tag =
                    "old-school-oc1-action-regression-v1."
                    "production",
                .seed_base =
                    5787775625948253273ULL,
                .worlds = 8,
                .horizon_turns = 4,
                .rollouts_per_world = 1,
                .blend_shallow_prior = true,
                .evaluation_threads = 1,
            },
        "production recipe drifted");
    expect(
        old_school::probe_runner::reference_seed_for_probe(
            scoring::kBalancedReferenceTag,
            "control.blue.force-spike-live-gray-ogre.v1",
            scoring::kReferenceSeedBase) ==
            1760865120769354728ULL,
        "balanced seed derivation drifted");
    expect(
        old_school::probe_runner::reference_seed_for_probe(
            scoring::kProductionTag,
            "control.blue.force-spike-live-gray-ogre.v1",
            scoring::kProductionSeedBase) ==
            871660984047741346ULL,
        "production seed derivation drifted");
    expect(
        scoring::kContinuationVariant ==
                old_school::LearnedVariant::
                    ValueSearchChampion &&
            scoring::kValueContinuationEpsilon == 0.0 &&
            scoring::kValuePriorityResidualWeight == 0.0 &&
            !scoring::kValuePassDominance &&
            scoring::kContinuationController ==
                old_school::LearnedContinuationController::Legacy,
        "fixed Value-mirror controls drifted");
}

void test_reference_dispatch_mapping_and_accounting() {
    const auto model = test_model();
    const scoring::SearchRecipe recipe = small_recipe();

    const probes::DecisionProbe priority = priority_probe();
    const scoring::DecisionScore priority_score =
        scoring::score_reference(priority, model, recipe);
    expect(priority_score.decision_kind ==
               probes::DecisionKind::Priority,
           "Priority dispatch changed decision kind");
    require_accounting(priority_score, 2, 1);

    const probes::DecisionProbe attack = attack_probe();
    const scoring::DecisionScore attack_score =
        scoring::score_reference(attack, model, recipe);
    expect(attack_score.decision_kind ==
               probes::DecisionKind::Attack,
           "Attack dispatch changed decision kind");
    require_accounting(attack_score, 2, 1);
    const old_school::LearnedActionSamples attack_direct =
        old_school::learned_binary_attack_samples(
            attack.state, attack.original_decks,
            attack.root_player, {}, attack_subject(attack), {},
            model, direct_config(attack_score));
    for (std::size_t branch = 0; branch < 2; ++branch) {
        const auto& mapped = action_for(
            attack_score,
            descriptor_for_attack_branch(
                attack, branch == 1));
        expect(mapped.raw_samples ==
                   attack_direct.q_samples[branch],
               "Attack Skip/Include row mapping is wrong");
        expect(mapped.shallow_prior_samples.empty() &&
                   mapped.continuation_samples.empty(),
               "binary Attack invented Priority trace components");
    }

    const probes::DecisionProbe block = block_probe();
    const scoring::DecisionScore block_score =
        scoring::score_reference(block, model, recipe);
    expect(block_score.decision_kind ==
               probes::DecisionKind::Block,
           "Block dispatch changed decision kind");
    require_accounting(block_score, 2, 1);
    const auto [attacker, blocker] = block_subject(block);
    const old_school::LearnedActionSamples block_direct =
        old_school::learned_binary_block_samples(
            block.state, block.original_decks,
            block.root_player, attacker, blocker, model,
            direct_config(block_score));
    for (std::size_t branch = 0; branch < 2; ++branch) {
        const auto& mapped = action_for(
            block_score,
            descriptor_for_block_branch(
                block, branch == 1));
        expect(mapped.raw_samples ==
                   block_direct.q_samples[branch],
               "Block No Block/Block row mapping is wrong");
        expect(mapped.shallow_prior_samples.empty() &&
                   mapped.continuation_samples.empty(),
               "binary Block invented Priority trace components");
    }
}

void test_complete_set_rejection() {
    probes::DecisionProbe incomplete = priority_probe();
    expect(incomplete.candidates.size() > 1,
           "Priority fixture cannot exercise omission");
    incomplete.candidates.pop_back();
    expect_invalid(
        [&]() {
            static_cast<void>(scoring::score_reference(
                incomplete, test_model(), small_recipe()));
        },
        "incomplete legal action set was accepted");

    probes::DecisionProbe duplicate = attack_probe();
    duplicate.candidates[1].descriptor =
        duplicate.candidates[0].descriptor;
    expect_invalid(
        [&]() {
            static_cast<void>(scoring::score_reference(
                duplicate, test_model(), small_recipe()));
        },
        "duplicate descriptor was accepted");
}

void test_hidden_and_reverse_order_bit_identity() {
    const probes::DecisionProbe probe = priority_probe();
    probes::DecisionProbe hidden = probe;
    hidden.state =
        old_school::probe_runner::hidden_repartition_clone(
            probe);
    expect(
        old_school::observe_game_state(
            probe.state, probe.root_player) ==
            old_school::observe_game_state(
                hidden.state, hidden.root_player),
        "test hidden clone changed the observation");
    expect(probe.state != hidden.state,
           "test hidden clone did not change physical state");

    const scoring::DecisionScore original =
        scoring::score_reference(
            probe, test_model(), small_recipe());
    const scoring::DecisionScore hidden_score =
        scoring::score_reference_hidden_clone(
            probe, hidden, test_model(), small_recipe());
    expect(scoring::bit_identical(original, hidden_score),
           "hidden repartition changed reference score bits");

    probes::DecisionProbe reversed = probe;
    std::reverse(
        reversed.candidates.begin(),
        reversed.candidates.end());
    const scoring::DecisionScore reversed_score =
        scoring::score_reference(
            reversed, test_model(), small_recipe());
    expect(scoring::bit_identical(original, reversed_score),
           "reverse candidate input changed canonical score bits");

    probes::DecisionProbe visible_change = hidden;
    ++visible_change.state.players[probe.root_player].life;
    expect_invalid(
        [&]() {
            static_cast<void>(
                scoring::score_reference_hidden_clone(
                    probe, visible_change, test_model(),
                    small_recipe()));
        },
        "hidden scorer accepted a changed observation");

    probes::DecisionProbe action_change = hidden;
    action_change.candidates.front().descriptor += "-changed";
    expect_invalid(
        [&]() {
            static_cast<void>(
                scoring::score_reference_hidden_clone(
                    probe, action_change, test_model(),
                    small_recipe()));
        },
        "hidden scorer accepted changed action identity");
}

void test_explicit_seed_reference_path() {
    constexpr std::uint64_t kExactSeed =
        0xD0C2C0AF1A5EULL;
    const scoring::DecisionScore score =
        scoring::score_reference_with_seed(
            empty_priority_probe(), test_model(), kExactSeed);
    expect(score.recipe.seed_source ==
               scoring::SeedSource::Explicit,
           "explicit scorer did not record explicit seed source");
    expect(score.recipe.seed_tag.empty() &&
               score.recipe.seed_base == 0 &&
               score.recipe.resolved_seed ==
                   std::optional<std::uint64_t>(kExactSeed),
           "explicit scorer leaked a tag/base derivation");
    expect(score.recipe.worlds == 64 &&
               score.recipe.horizon_turns == 8 &&
               score.recipe.rollouts_per_world == 1 &&
               !score.recipe.blend_shallow_prior &&
               score.recipe.evaluation_threads == 4,
           "explicit scorer did not use DVR confirmation recipe");
    require_accounting(score, 64, 1);
}

void test_production_priority_and_exact_tie_support() {
    const probes::DecisionProbe probe = priority_probe();
    expect(probe.candidates.size() > 1,
           "production Priority test requires multiple actions");
    const scoring::DecisionScore score =
        scoring::score_production(
            probe, test_model());
    expect(score.score_mode ==
               scoring::ScoreMode::
                   ProductionPrioritySearch &&
               !score.deterministic_selection,
           "production Priority selection semantics are wrong");
    expect(score.recipe.seed_source ==
               scoring::SeedSource::Derived &&
               score.recipe.seed_tag ==
                   scoring::kProductionTag &&
               score.recipe.seed_base ==
                   scoring::kProductionSeedBase,
           "production Priority seed recipe is wrong");
    expect(score.recipe.worlds == 8 &&
               score.recipe.horizon_turns == 4 &&
               score.recipe.rollouts_per_world == 1 &&
               score.recipe.blend_shallow_prior &&
               score.recipe.evaluation_threads == 1,
           "production Priority K/H/R/blend/threads drifted");
    require_accounting(score, 8, 1);
    expect(score.selected_support ==
               scoring::exact_max_support(score.actions),
           "production Priority did not expose exact-max support");

    const probes::DecisionProbe canonical =
        descriptor_canonical_probe(probe);
    const old_school::LearnedActionSamples direct =
        old_school::learned_priority_action_samples(
            canonical.state, canonical.original_decks,
            canonical.root_player, true, canonical.phase,
            canonical.consecutive_passes,
            priority_actions(canonical), test_model(),
            direct_config(score));
    expect(direct.q_samples.size() ==
                   canonical.candidates.size() &&
               direct.priority_shallow_prior_samples.size() ==
                   canonical.candidates.size() &&
               direct.priority_continuation_samples.size() ==
                   canonical.candidates.size() &&
               direct.exact_priority_aggregate_scores.size() ==
                   canonical.candidates.size(),
           "direct production Priority trace/aggregate count is "
           "wrong");
    bool distinguished_old_row_mean = false;
    std::vector<scoring::DescriptorScore> exact_actions;
    exact_actions.reserve(canonical.candidates.size());
    for (std::size_t action = 0;
         action < canonical.candidates.size(); ++action) {
        const auto& mapped = action_for(
            score,
            canonical.candidates[action].descriptor);
        expect(mapped.raw_samples ==
                   direct.q_samples[action],
               "production Priority raw samples differ from "
               "the direct sampler");
        expect(bit_identical(
                   mapped.shallow_prior_samples,
                   direct.priority_shallow_prior_samples[action]) &&
                   bit_identical(
                       mapped.continuation_samples,
                       direct.priority_continuation_samples[action]),
               "production Priority components differ from "
               "the direct sampler");
        expect(mapped.raw_samples.size() ==
                   mapped.shallow_prior_samples.size() &&
                   mapped.raw_samples.size() ==
                       mapped.continuation_samples.size(),
               "production Priority component widths are wrong");
        const double continuation_weight =
            static_cast<double>(mapped.raw_samples.size());
        double reconstructed_aggregate = 0.0;
        for (std::size_t sample = 0;
             sample < mapped.raw_samples.size(); ++sample) {
            const double reconstructed_q =
                (mapped.shallow_prior_samples[sample] +
                 continuation_weight *
                     mapped.continuation_samples[sample]) /
                (continuation_weight + 1.0);
            expect(bit_identical(
                       mapped.raw_samples[sample],
                       reconstructed_q),
                   "production Priority Q sample does not "
                   "bit-match its deployed blend");
            reconstructed_aggregate +=
                mapped.shallow_prior_samples[sample];
        }
        reconstructed_aggregate /= continuation_weight;
        for (const double continuation :
             mapped.continuation_samples) {
            reconstructed_aggregate += continuation;
        }
        reconstructed_aggregate /=
            continuation_weight + 1.0;
        expect(bit_identical(
                   mapped.raw_score,
                   reconstructed_aggregate),
               "production Priority aggregate is not bit-exactly "
               "reconstructible in deployed order");
        expect(bit_identical(
                   mapped.raw_score,
                   direct.exact_priority_aggregate_scores[action]),
               "production Priority raw score differs from "
               "the engine's exact aggregate");
        distinguished_old_row_mean =
            distinguished_old_row_mean ||
            !bit_identical(
                mapped.raw_score,
                exact_mean(direct.q_samples[action]));
        exact_actions.push_back({
            .descriptor =
                canonical.candidates[action].descriptor,
            .raw_samples = {},
            .raw_score =
                direct.exact_priority_aggregate_scores[action],
        });
    }
    expect(
        distinguished_old_row_mean,
        "fixture did not distinguish exact live aggregation "
        "from the old row-mean arithmetic");
    expect(
        score.selected_support ==
            scoring::exact_max_support(exact_actions),
        "multi-action production support did not use exact "
        "engine aggregates");
    expect(score.accounting.sampled_worlds ==
               direct.sampled_worlds &&
               score.accounting.rollout_evaluations ==
                   direct.rollout_evaluations &&
               score.accounting.terminal_evaluations ==
                   direct.terminal_evaluations &&
               score.accounting.bootstrapped_evaluations ==
                   direct.bootstrapped_evaluations,
           "production Priority accounting differs from direct "
           "sampler");
    expect(score.recipe.resolved_seed ==
               std::optional<std::uint64_t>(
                   old_school::probe_runner::
                       reference_seed_for_probe(
                           scoring::kProductionTag,
                           probe.stable_id,
                           scoring::kProductionSeedBase)),
           "production Priority did not use exact derived seed");

    probes::DecisionProbe hidden = probe;
    hidden.state =
        old_school::probe_runner::hidden_repartition_clone(
            probe);
    expect(hidden.state != probe.state,
           "production Priority hidden clone was vacuous");
    const scoring::DecisionScore hidden_score =
        scoring::score_production_hidden_clone(
            probe, hidden, test_model());
    expect(scoring::bit_identical(score, hidden_score),
           "hidden repartition changed production Priority bits");

    const std::vector<scoring::DescriptorScore> tied = {
        {.descriptor = "beta", .raw_samples = {}, .raw_score = 0.5},
        {.descriptor = "low", .raw_samples = {}, .raw_score = 0.4},
        {.descriptor = "alpha", .raw_samples = {}, .raw_score = 0.5},
    };
    expect(scoring::exact_max_support(tied) ==
               (std::vector<std::string>{"alpha", "beta"}),
           "Priority exact ties did not retain complete support");
}

void test_production_immediate_native_singletons() {
    const auto model = test_model();
    const probes::DecisionProbe attack = attack_probe();
    const scoring::DecisionScore attack_score =
        scoring::score_production(attack, model);
    expect(
        attack_score.score_mode ==
                scoring::ScoreMode::
                    ProductionImmediateAttack &&
            attack_score.deterministic_selection &&
            attack_score.selected_support.size() == 1,
        "production Attack did not expose native singleton");
    const std::uint64_t attack_seed =
        old_school::probe_runner::reference_seed_for_probe(
            scoring::kProductionTag, attack.stable_id,
            scoring::kProductionSeedBase);
    expect(attack_score.recipe.resolved_seed ==
               std::optional<std::uint64_t>(attack_seed),
           "production Attack seed is wrong");
    const old_school::LearnedValueAttackSetScores
        direct_attack =
            old_school::learned_value_attack_set_scores(
                attack.state, attack.root_player,
                {{}, {attack_subject(attack)}}, model,
                attack_seed);
    for (std::size_t branch = 0; branch < 2; ++branch) {
        const auto& mapped = action_for(
            attack_score,
            descriptor_for_attack_branch(
                attack, branch == 1));
        expect(mapped.raw_samples.empty(),
               "immediate Attack invented rollout samples");
        expect(bit_identical(
                   mapped.raw_score,
                   direct_attack.scores[branch]),
               "immediate Attack raw score mapping is wrong");
    }
    expect(
        attack_score.selected_support.front() ==
            descriptor_for_attack_branch(
                attack,
                direct_attack.selected_candidate == 1),
        "immediate Attack native selection mapping is wrong");

    const probes::DecisionProbe block = block_probe();
    const scoring::DecisionScore block_score =
        scoring::score_production(block, model);
    expect(
        block_score.score_mode ==
                scoring::ScoreMode::
                    ProductionImmediateBlock &&
            block_score.deterministic_selection &&
            block_score.selected_support.size() == 1 &&
            block_score.recipe.seed_source ==
                scoring::SeedSource::Seedless &&
            !block_score.recipe.resolved_seed.has_value(),
        "production Block did not expose seedless singleton");
    const auto [attacker, blocker] = block_subject(block);
    const old_school::LearnedValueBinaryBlockScores
        direct_block =
            old_school::learned_value_binary_block_scores(
                block.state, block.root_player, attacker,
                blocker, model);
    for (std::size_t branch = 0; branch < 2; ++branch) {
        const auto& mapped = action_for(
            block_score,
            descriptor_for_block_branch(
                block, branch == 1));
        expect(mapped.raw_samples.empty(),
               "immediate Block invented rollout samples");
        expect(bit_identical(
                   mapped.raw_score,
                   direct_block.scores[branch]),
               "immediate Block raw score mapping is wrong");
    }
    expect(
        block_score.selected_support.front() ==
            descriptor_for_block_branch(
                block,
                direct_block.selected_candidate == 1),
        "immediate Block native selection mapping is wrong");
    expect(block_score.accounting ==
               scoring::EvaluationAccounting{},
           "immediate combat invented rollout accounting");

    probes::DecisionProbe hidden_block = block;
    hidden_block.state =
        old_school::probe_runner::hidden_repartition_clone(
            block);
    expect(hidden_block.state != block.state,
           "production Block hidden clone was vacuous");
    const scoring::DecisionScore hidden_block_score =
        scoring::score_production_hidden_clone(
            block, hidden_block, model);
    expect(scoring::bit_identical(
               block_score, hidden_block_score),
           "hidden repartition changed immediate Block bits");

    probes::DecisionProbe hidden_attack = attack;
    hidden_attack.state =
        old_school::probe_runner::hidden_repartition_clone(
            attack);
    const scoring::DecisionScore hidden_attack_score =
        scoring::score_production_hidden_clone(
            attack, hidden_attack, model);
    expect(scoring::bit_identical(
               attack_score, hidden_attack_score),
           "hidden repartition changed immediate Attack bits");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests = {
            {"frozen recipe and seed constants",
             test_frozen_recipe_and_seed_constants},
            {"reference dispatch mapping and accounting",
             test_reference_dispatch_mapping_and_accounting},
            {"complete-set rejection",
             test_complete_set_rejection},
            {"hidden and reverse bit identity",
             test_hidden_and_reverse_order_bit_identity},
            {"explicit seed reference path",
             test_explicit_seed_reference_path},
            {"production Priority and exact tie support",
             test_production_priority_and_exact_tie_support},
            {"production immediate native singletons",
             test_production_immediate_native_singletons},
        };

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "[FAIL] " << name << ": "
                      << exception.what() << '\n';
        }
    }
    std::cout << passed << " passed, "
              << tests.size() - passed << " failed\n";
    return passed == tests.size() ? 0 : 1;
}
