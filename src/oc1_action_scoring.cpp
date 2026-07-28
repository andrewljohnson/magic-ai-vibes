#include "old_school/oc1_action_scoring.hpp"

#include "old_school/probe_runner.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace old_school::oc1_action_scoring {
namespace {

bool sorcery_actions_for(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

void require_model(
    const std::shared_ptr<const LearnedModel>& model) {
    if (model == nullptr) {
        throw std::invalid_argument(
            "AR1 action scorer requires a Learned model");
    }
}

void require_search_dimensions(const SearchRecipe& recipe) {
    if (recipe.worlds == 0 ||
        recipe.rollouts_per_world == 0 ||
        recipe.evaluation_threads == 0) {
        throw std::invalid_argument(
            "AR1 search dimensions must be nonzero");
    }
    if (recipe.worlds >
        std::numeric_limits<std::size_t>::max() /
            recipe.rollouts_per_world) {
        throw std::overflow_error(
            "AR1 samples per action overflow size_t");
    }
}

void require_derived_search_recipe(
    const SearchRecipe& recipe) {
    require_search_dimensions(recipe);
    if (recipe.seed_tag.empty()) {
        throw std::invalid_argument(
            "AR1 derived-seed recipe requires a nonempty tag");
    }
}

void require_complete_legal_actions(
    const probes::DecisionProbe& probe) {
    if (probe.stable_id.empty()) {
        throw std::invalid_argument(
            "AR1 probe stable ID must be nonempty");
    }
    const probes::Validation validation =
        probes::validate_probe(probe);
    if (!validation.candidates_legal_and_complete) {
        std::string message =
            probe.stable_id +
            ": AR1 requires the complete legal action set";
        for (const std::string& error : validation.errors) {
            if (error.find("candidate") !=
                    std::string::npos ||
                error.find("block") != std::string::npos ||
                error.find("attack") != std::string::npos) {
                message += ": " + error;
                break;
            }
        }
        throw std::invalid_argument(message);
    }
}

probes::DecisionProbe canonical_probe(
    const probes::DecisionProbe& probe) {
    probes::DecisionProbe canonical = probe;
    std::sort(
        canonical.candidates.begin(),
        canonical.candidates.end(),
        [](const probes::Candidate& left,
           const probes::Candidate& right) {
            return left.descriptor < right.descriptor;
        });
    return canonical;
}

std::vector<PriorityAction> priority_candidates(
    const probes::DecisionProbe& probe) {
    std::vector<PriorityAction> result;
    result.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr) {
            throw std::invalid_argument(
                probe.stable_id +
                ": Priority probe contains a non-Priority "
                "candidate");
        }
        result.push_back(*action);
    }
    return result;
}

PermanentId binary_attack_subject(
    const probes::DecisionProbe& probe) {
    std::optional<PermanentId> subject;
    std::array<bool, 2> branches{};
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* attack =
            std::get_if<probes::BinaryAttackDecision>(
                &candidate.action);
        if (attack == nullptr) {
            throw std::invalid_argument(
                probe.stable_id +
                ": Attack probe contains a non-Attack "
                "candidate");
        }
        if (subject.has_value() &&
            *subject != attack->attacker) {
            throw std::invalid_argument(
                probe.stable_id +
                ": Attack probe has multiple subjects");
        }
        subject = attack->attacker;
        branches[attack->include ? 1U : 0U] = true;
    }
    if (!subject.has_value() ||
        probe.candidates.size() != 2 ||
        !branches[0] || !branches[1]) {
        throw std::invalid_argument(
            probe.stable_id +
            ": Attack probe must contain exact Skip/Include "
            "branches");
    }
    return *subject;
}

struct BinaryBlockSubject {
    PermanentId attacker = 0;
    PermanentId blocker = 0;
};

BinaryBlockSubject binary_block_subject(
    const probes::DecisionProbe& probe) {
    std::optional<PermanentId> attacker;
    std::optional<PermanentId> blocker;
    std::array<bool, 2> branches{};
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* block =
            std::get_if<probes::BinaryBlockDecision>(
                &candidate.action);
        if (block == nullptr) {
            throw std::invalid_argument(
                probe.stable_id +
                ": Block probe contains a non-Block candidate");
        }
        if ((attacker.has_value() &&
             *attacker != block->attacker) ||
            (blocker.has_value() &&
             *blocker != block->blocker)) {
            throw std::invalid_argument(
                probe.stable_id +
                ": Block probe has multiple subjects");
        }
        attacker = block->attacker;
        blocker = block->blocker;
        branches[block->include ? 1U : 0U] = true;
    }
    if (!attacker.has_value() || !blocker.has_value() ||
        probe.candidates.size() != 2 ||
        !branches[0] || !branches[1]) {
        throw std::invalid_argument(
            probe.stable_id +
            ": Block probe must contain exact No Block/Block "
            "branches");
    }
    return {
        .attacker = *attacker,
        .blocker = *blocker,
    };
}

LearnedSearchConfig learned_search_config(
    const SearchRecipe& recipe, std::uint64_t seed) {
    return {
        .seed = seed,
        .worlds = recipe.worlds,
        .rollouts_per_world =
            recipe.rollouts_per_world,
        .horizon_turns = recipe.horizon_turns,
        .continuation_variant = kContinuationVariant,
        .value_continuation_epsilon =
            kValueContinuationEpsilon,
        .blend_shallow_prior =
            recipe.blend_shallow_prior,
        .value_priority_residual_weight =
            kValuePriorityResidualWeight,
        .value_pass_dominance = kValuePassDominance,
        .value_continuation_controller =
            kContinuationController,
        .evaluation_threads = recipe.evaluation_threads,
    };
}

LearnedActionSamples score_search_actions(
    const probes::DecisionProbe& probe,
    const std::shared_ptr<const LearnedModel>& model,
    const SearchRecipe& recipe, std::uint64_t seed) {
    const LearnedSearchConfig search =
        learned_search_config(recipe, seed);
    switch (probe.decision_kind) {
    case probes::DecisionKind::Priority:
        return learned_priority_action_samples(
            probe.state, probe.original_decks,
            probe.root_player,
            sorcery_actions_for(probe.phase), probe.phase,
            probe.consecutive_passes,
            priority_candidates(probe), model, search);
    case probes::DecisionKind::Attack: {
        const PermanentId subject =
            binary_attack_subject(probe);
        return learned_binary_attack_samples(
            probe.state, probe.original_decks,
            probe.root_player, {}, subject, {}, model, search);
    }
    case probes::DecisionKind::Block: {
        const BinaryBlockSubject subject =
            binary_block_subject(probe);
        return learned_binary_block_samples(
            probe.state, probe.original_decks,
            probe.root_player, subject.attacker,
            subject.blocker, model, search);
    }
    }
    throw std::invalid_argument(
        "AR1 scorer received an unknown decision kind");
}

std::size_t checked_expected_evaluations(
    std::size_t candidates, const SearchRecipe& recipe) {
    const std::size_t samples_per_action =
        recipe.worlds * recipe.rollouts_per_world;
    if (candidates >
        std::numeric_limits<std::size_t>::max() /
            samples_per_action) {
        throw std::overflow_error(
            "AR1 rollout evaluation count overflows size_t");
    }
    return candidates * samples_per_action;
}

double finite_mean(const std::vector<double>& samples) {
    if (samples.empty()) {
        throw std::runtime_error(
            "AR1 search returned an empty sample row");
    }
    double sum = 0.0;
    for (const double sample : samples) {
        if (!std::isfinite(sample)) {
            throw std::runtime_error(
                "AR1 search returned a non-finite sample");
        }
        sum += sample;
    }
    const double mean =
        sum / static_cast<double>(samples.size());
    if (!std::isfinite(mean)) {
        throw std::runtime_error(
            "AR1 search produced a non-finite raw score");
    }
    return mean;
}

std::vector<DescriptorScore> map_search_rows(
    const probes::DecisionProbe& probe,
    const LearnedActionSamples& samples) {
    std::vector<DescriptorScore> result;
    result.reserve(probe.candidates.size());
    if (probe.decision_kind ==
        probes::DecisionKind::Priority) {
        if (samples.q_samples.size() !=
                probe.candidates.size() ||
            samples.priority_shallow_prior_samples.size() !=
                probe.candidates.size() ||
            samples.priority_continuation_samples.size() !=
                probe.candidates.size() ||
            samples.exact_priority_aggregate_scores.size() !=
                probe.candidates.size()) {
            throw std::runtime_error(
                probe.stable_id +
                ": Priority sample/component/aggregate row count "
                "is wrong");
        }
        for (std::size_t index = 0;
             index < probe.candidates.size(); ++index) {
            const auto& row = samples.q_samples[index];
            const double aggregate =
                samples.exact_priority_aggregate_scores[index];
            if (!std::isfinite(aggregate)) {
                throw std::runtime_error(
                    probe.stable_id +
                    ": Priority aggregate score is non-finite");
            }
            result.push_back({
                .descriptor =
                    probe.candidates[index].descriptor,
                .raw_samples = row,
                .shallow_prior_samples =
                    samples.priority_shallow_prior_samples[index],
                .continuation_samples =
                    samples.priority_continuation_samples[index],
                .raw_score = aggregate,
            });
        }
        return result;
    }

    if (samples.q_samples.size() != 2) {
        throw std::runtime_error(
            probe.stable_id +
            ": binary sample row count is not two");
    }
    for (const probes::Candidate& candidate :
         probe.candidates) {
        std::size_t branch = 0;
        if (probe.decision_kind ==
            probes::DecisionKind::Attack) {
            branch =
                std::get<probes::BinaryAttackDecision>(
                    candidate.action)
                        .include
                    ? 1U
                    : 0U;
        } else if (probe.decision_kind ==
                   probes::DecisionKind::Block) {
            branch =
                std::get<probes::BinaryBlockDecision>(
                    candidate.action)
                        .include
                    ? 1U
                    : 0U;
        } else {
            throw std::invalid_argument(
                "AR1 scorer received an unknown decision kind");
        }
        const auto& row = samples.q_samples[branch];
        result.push_back({
            .descriptor = candidate.descriptor,
            .raw_samples = row,
            .raw_score = finite_mean(row),
        });
    }
    return result;
}

EvaluationAccounting require_exact_accounting(
    const probes::DecisionProbe& probe,
    const LearnedActionSamples& samples,
    const SearchRecipe& recipe) {
    const std::size_t expected =
        checked_expected_evaluations(
            probe.candidates.size(), recipe);
    const std::size_t samples_per_action =
        recipe.worlds * recipe.rollouts_per_world;
    if (samples.sampled_worlds != recipe.worlds ||
        samples.rollout_evaluations != expected ||
        samples.terminal_evaluations >
            samples.rollout_evaluations ||
        samples.bootstrapped_evaluations !=
            samples.rollout_evaluations -
                samples.terminal_evaluations) {
        throw std::runtime_error(
            probe.stable_id +
            ": AR1 search accounting does not cross-sum");
    }
    if (samples.q_samples.size() !=
        probe.candidates.size()) {
        throw std::runtime_error(
            probe.stable_id +
            ": AR1 search row count does not match candidates");
    }
    if (probe.decision_kind ==
            probes::DecisionKind::Priority) {
        if (samples.exact_priority_aggregate_scores.size() !=
                probe.candidates.size() ||
            samples.priority_shallow_prior_samples.size() !=
                probe.candidates.size() ||
            samples.priority_continuation_samples.size() !=
                probe.candidates.size()) {
            throw std::runtime_error(
                probe.stable_id +
                ": AR1 Priority aggregate/component count does "
                "not match candidates");
        }
        for (const double aggregate :
             samples.exact_priority_aggregate_scores) {
            if (!std::isfinite(aggregate)) {
                throw std::runtime_error(
                    probe.stable_id +
                    ": AR1 Priority aggregate is non-finite");
            }
        }
    } else if (
        !samples.exact_priority_aggregate_scores.empty() ||
        !samples.priority_shallow_prior_samples.empty() ||
        !samples.priority_continuation_samples.empty()) {
        throw std::runtime_error(
            probe.stable_id +
            ": AR1 binary search invented Priority traces");
    }
    for (std::size_t action = 0;
         action < samples.q_samples.size(); ++action) {
        if (samples.q_samples[action].size() !=
            samples_per_action) {
            throw std::runtime_error(
                probe.stable_id +
                ": AR1 search row width is wrong");
        }
        if (probe.decision_kind ==
                probes::DecisionKind::Priority &&
            (samples.priority_shallow_prior_samples[action]
                     .size() != samples_per_action ||
             samples.priority_continuation_samples[action]
                     .size() != samples_per_action)) {
            throw std::runtime_error(
                probe.stable_id +
                ": AR1 Priority component row width is wrong");
        }
    }
    return {
        .sampled_worlds = samples.sampled_worlds,
        .rollout_evaluations =
            samples.rollout_evaluations,
        .terminal_evaluations =
            samples.terminal_evaluations,
        .bootstrapped_evaluations =
            samples.bootstrapped_evaluations,
    };
}

AppliedRecipe applied_search_recipe(
    const SearchRecipe& recipe, SeedSource seed_source,
    std::uint64_t resolved_seed) {
    return {
        .seed_source = seed_source,
        .seed_tag =
            seed_source == SeedSource::Derived
                ? std::string(recipe.seed_tag)
                : std::string{},
        .seed_base =
            seed_source == SeedSource::Derived
                ? recipe.seed_base
                : 0,
        .resolved_seed = resolved_seed,
        .worlds = recipe.worlds,
        .horizon_turns = recipe.horizon_turns,
        .rollouts_per_world =
            recipe.rollouts_per_world,
        .blend_shallow_prior =
            recipe.blend_shallow_prior,
        .evaluation_threads =
            recipe.evaluation_threads,
        .value_mirror = true,
        .value_continuation_epsilon =
            kValueContinuationEpsilon,
        .value_priority_residual_weight =
            kValuePriorityResidualWeight,
        .value_pass_dominance = kValuePassDominance,
        .value_continuation_controller =
            kContinuationController,
    };
}

DecisionScore score_search(
    const probes::DecisionProbe& probe,
    const std::shared_ptr<const LearnedModel>& model,
    const SearchRecipe& recipe, std::uint64_t seed,
    SeedSource seed_source, ScoreMode score_mode) {
    require_model(model);
    require_search_dimensions(recipe);
    if (seed_source == SeedSource::Derived &&
        recipe.seed_tag.empty()) {
        throw std::invalid_argument(
            "AR1 derived-seed search lost its tag");
    }
    if (seed_source == SeedSource::Explicit &&
        (!recipe.seed_tag.empty() ||
         recipe.seed_base != 0)) {
        throw std::invalid_argument(
            "AR1 explicit-seed search must not carry tag/base "
            "derivation");
    }
    require_complete_legal_actions(probe);
    const probes::DecisionProbe canonical =
        canonical_probe(probe);
    const LearnedActionSamples samples =
        score_search_actions(
            canonical, model, recipe, seed);
    DecisionScore result{
        .stable_id = canonical.stable_id,
        .decision_kind = canonical.decision_kind,
        .score_mode = score_mode,
        .recipe = applied_search_recipe(
            recipe, seed_source, seed),
        .actions = map_search_rows(canonical, samples),
        .selected_support = {},
        .deterministic_selection = false,
        .accounting = require_exact_accounting(
            canonical, samples, recipe),
    };
    result.selected_support =
        exact_max_support(result.actions);
    return result;
}

void require_hidden_clone_identity(
    const probes::DecisionProbe& probe,
    const probes::DecisionProbe& hidden_clone) {
    if (probe.stable_id != hidden_clone.stable_id ||
        probe.decision_kind !=
            hidden_clone.decision_kind ||
        probe.root_player != hidden_clone.root_player ||
        probe.phase != hidden_clone.phase ||
        probe.consecutive_passes !=
            hidden_clone.consecutive_passes ||
        probe.original_decks !=
            hidden_clone.original_decks ||
        probe.candidates != hidden_clone.candidates) {
        throw std::invalid_argument(
            probe.stable_id +
            ": hidden clone changed action/context identity");
    }
    if (observe_game_state(
            probe.state, probe.root_player) !=
        observe_game_state(
            hidden_clone.state,
            hidden_clone.root_player)) {
        throw std::invalid_argument(
            probe.stable_id +
            ": hidden clone changed the owner's observation");
    }
}

AppliedRecipe immediate_attack_recipe(
    std::uint64_t seed) {
    return {
        .seed_source = SeedSource::Derived,
        .seed_tag = std::string(kProductionTag),
        .seed_base = kProductionSeedBase,
        .resolved_seed = seed,
        .worlds = 0,
        .horizon_turns = 0,
        .rollouts_per_world = 0,
        .blend_shallow_prior = false,
        .evaluation_threads = 0,
        .value_mirror = false,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
    };
}

AppliedRecipe immediate_block_recipe() {
    AppliedRecipe result = immediate_attack_recipe(0);
    result.seed_source = SeedSource::Seedless;
    result.seed_tag.clear();
    result.seed_base = 0;
    result.resolved_seed.reset();
    return result;
}

DecisionScore score_production_impl(
    const probes::DecisionProbe& probe,
    const std::shared_ptr<const LearnedModel>& model) {
    require_model(model);
    require_complete_legal_actions(probe);
    const probes::DecisionProbe canonical =
        canonical_probe(probe);

    if (canonical.decision_kind ==
        probes::DecisionKind::Priority) {
        const std::uint64_t seed =
            probe_runner::reference_seed_for_probe(
                kProductionTag, canonical.stable_id,
                kProductionSeedBase);
        return score_search(
            canonical, model, kProductionPriorityRecipe,
            seed, SeedSource::Derived,
            ScoreMode::ProductionPrioritySearch);
    }

    std::vector<double> branch_scores;
    std::size_t selected_branch = 0;
    AppliedRecipe recipe;
    ScoreMode score_mode =
        ScoreMode::ProductionImmediateAttack;
    if (canonical.decision_kind ==
        probes::DecisionKind::Attack) {
        const PermanentId subject =
            binary_attack_subject(canonical);
        const std::uint64_t seed =
            probe_runner::reference_seed_for_probe(
                kProductionTag, canonical.stable_id,
                kProductionSeedBase);
        const LearnedValueAttackSetScores immediate =
            learned_value_attack_set_scores(
                canonical.state, canonical.root_player,
                {{}, {subject}}, model, seed);
        if (immediate.scores.size() != 2 ||
            immediate.selected_candidate >= 2) {
            throw std::runtime_error(
                canonical.stable_id +
                ": production Attack selector returned an "
                "invalid schema");
        }
        branch_scores = immediate.scores;
        selected_branch = immediate.selected_candidate;
        recipe = immediate_attack_recipe(seed);
    } else if (canonical.decision_kind ==
               probes::DecisionKind::Block) {
        const BinaryBlockSubject subject =
            binary_block_subject(canonical);
        const LearnedValueBinaryBlockScores immediate =
            learned_value_binary_block_scores(
                canonical.state, canonical.root_player,
                subject.attacker, subject.blocker, model);
        branch_scores.assign(
            immediate.scores.begin(),
            immediate.scores.end());
        if (immediate.selected_candidate >= 2) {
            throw std::runtime_error(
                canonical.stable_id +
                ": production Block selector returned an "
                "invalid schema");
        }
        selected_branch = immediate.selected_candidate;
        recipe = immediate_block_recipe();
        score_mode = ScoreMode::ProductionImmediateBlock;
    } else {
        throw std::invalid_argument(
            "AR1 production scorer received an unknown "
            "decision kind");
    }

    DecisionScore result{
        .stable_id = canonical.stable_id,
        .decision_kind = canonical.decision_kind,
        .score_mode = score_mode,
        .recipe = std::move(recipe),
        .actions = {},
        .selected_support = {},
        .deterministic_selection = true,
        .accounting = {},
    };
    result.actions.reserve(canonical.candidates.size());
    for (const probes::Candidate& candidate :
         canonical.candidates) {
        std::size_t branch = 0;
        if (canonical.decision_kind ==
            probes::DecisionKind::Attack) {
            branch =
                std::get<probes::BinaryAttackDecision>(
                    candidate.action)
                        .include
                    ? 1U
                    : 0U;
        } else {
            branch =
                std::get<probes::BinaryBlockDecision>(
                    candidate.action)
                        .include
                    ? 1U
                    : 0U;
        }
        const double score = branch_scores[branch];
        if (!std::isfinite(score)) {
            throw std::runtime_error(
                canonical.stable_id +
                ": production immediate selector returned a "
                "non-finite score");
        }
        result.actions.push_back({
            .descriptor = candidate.descriptor,
            .raw_samples = {},
            .raw_score = score,
        });
        if (branch == selected_branch) {
            result.selected_support.push_back(
                candidate.descriptor);
        }
    }
    if (result.selected_support.size() != 1) {
        throw std::logic_error(
            canonical.stable_id +
            ": production immediate selection did not map to "
            "one candidate");
    }
    return result;
}

bool same_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool applied_recipe_bit_identical(
    const AppliedRecipe& first,
    const AppliedRecipe& second) {
    return first.seed_source == second.seed_source &&
           first.seed_tag == second.seed_tag &&
           first.seed_base == second.seed_base &&
           first.resolved_seed == second.resolved_seed &&
           first.worlds == second.worlds &&
           first.horizon_turns == second.horizon_turns &&
           first.rollouts_per_world ==
               second.rollouts_per_world &&
           first.blend_shallow_prior ==
               second.blend_shallow_prior &&
           first.evaluation_threads ==
               second.evaluation_threads &&
           first.value_mirror == second.value_mirror &&
           same_double(
               first.value_continuation_epsilon,
               second.value_continuation_epsilon) &&
           same_double(
               first.value_priority_residual_weight,
               second.value_priority_residual_weight) &&
           first.value_pass_dominance ==
               second.value_pass_dominance &&
           first.value_continuation_controller ==
               second.value_continuation_controller;
}

} // namespace

std::vector<std::string> exact_max_support(
    const std::vector<DescriptorScore>& actions) {
    if (actions.empty()) {
        throw std::invalid_argument(
            "AR1 exact support requires at least one action");
    }
    std::unordered_set<std::string> descriptors;
    double maximum =
        -std::numeric_limits<double>::infinity();
    for (const DescriptorScore& action : actions) {
        if (action.descriptor.empty() ||
            !descriptors.insert(action.descriptor).second) {
            throw std::invalid_argument(
                "AR1 exact support requires unique nonempty "
                "descriptors");
        }
        if (!std::isfinite(action.raw_score)) {
            throw std::invalid_argument(
                "AR1 exact support requires finite scores");
        }
        maximum = std::max(maximum, action.raw_score);
    }
    std::vector<std::string> result;
    for (const DescriptorScore& action : actions) {
        if (action.raw_score == maximum) {
            result.push_back(action.descriptor);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

DecisionScore score_reference(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    const SearchRecipe& recipe) {
    require_derived_search_recipe(recipe);
    const std::uint64_t seed =
        probe_runner::reference_seed_for_probe(
            recipe.seed_tag, probe.stable_id,
            recipe.seed_base);
    return score_search(
        probe, model, recipe, seed, SeedSource::Derived,
        ScoreMode::ReferenceSearch);
}

DecisionScore score_reference_hidden_clone(
    const probes::DecisionProbe& probe,
    const probes::DecisionProbe& hidden_clone,
    std::shared_ptr<const LearnedModel> model,
    const SearchRecipe& recipe) {
    require_hidden_clone_identity(probe, hidden_clone);
    return score_reference(
        hidden_clone, std::move(model), recipe);
}

DecisionScore score_reference_with_seed(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t exact_seed) {
    SearchRecipe explicit_recipe =
        kBalancedReferenceRecipe;
    explicit_recipe.seed_tag = {};
    explicit_recipe.seed_base = 0;
    return score_search(
        probe, model, explicit_recipe, exact_seed,
        SeedSource::Explicit, ScoreMode::ReferenceSearch);
}

DecisionScore score_reference_with_seed_hidden_clone(
    const probes::DecisionProbe& probe,
    const probes::DecisionProbe& hidden_clone,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t exact_seed) {
    require_hidden_clone_identity(probe, hidden_clone);
    return score_reference_with_seed(
        hidden_clone, std::move(model), exact_seed);
}

DecisionScore score_production(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model) {
    return score_production_impl(probe, model);
}

DecisionScore score_production_hidden_clone(
    const probes::DecisionProbe& probe,
    const probes::DecisionProbe& hidden_clone,
    std::shared_ptr<const LearnedModel> model) {
    require_hidden_clone_identity(probe, hidden_clone);
    return score_production_impl(hidden_clone, model);
}

bool bit_identical(const DecisionScore& first,
                   const DecisionScore& second) {
    if (first.stable_id != second.stable_id ||
        first.decision_kind != second.decision_kind ||
        first.score_mode != second.score_mode ||
        !applied_recipe_bit_identical(
            first.recipe, second.recipe) ||
        first.selected_support !=
            second.selected_support ||
        first.deterministic_selection !=
            second.deterministic_selection ||
        first.accounting != second.accounting ||
        first.actions.size() != second.actions.size()) {
        return false;
    }
    for (std::size_t action = 0;
         action < first.actions.size(); ++action) {
        const DescriptorScore& left = first.actions[action];
        const DescriptorScore& right = second.actions[action];
        if (left.descriptor != right.descriptor ||
            !same_double(left.raw_score, right.raw_score) ||
            left.raw_samples.size() !=
                right.raw_samples.size() ||
            left.shallow_prior_samples.size() !=
                right.shallow_prior_samples.size() ||
            left.continuation_samples.size() !=
                right.continuation_samples.size()) {
            return false;
        }
        for (std::size_t sample = 0;
             sample < left.raw_samples.size(); ++sample) {
            if (!same_double(
                    left.raw_samples[sample],
                    right.raw_samples[sample])) {
                return false;
            }
        }
        for (std::size_t sample = 0;
             sample < left.shallow_prior_samples.size();
             ++sample) {
            if (!same_double(
                    left.shallow_prior_samples[sample],
                    right.shallow_prior_samples[sample])) {
                return false;
            }
        }
        for (std::size_t sample = 0;
             sample < left.continuation_samples.size();
             ++sample) {
            if (!same_double(
                    left.continuation_samples[sample],
                    right.continuation_samples[sample])) {
                return false;
            }
        }
    }
    return true;
}

} // namespace old_school::oc1_action_scoring
