#pragma once

#include "old_school/probes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::oc1_action_scoring {

inline constexpr std::string_view kBalancedReferenceTag =
    "old-school-oc1-action-regression-v1.value-balanced";
inline constexpr std::string_view kFocusedScoutTag =
    "old-school-oc1-action-regression-v1.focused-scout";
inline constexpr std::string_view kFocusedConfirmationTag =
    "old-school-oc1-action-regression-v1.focused-confirmation";
inline constexpr std::string_view kProductionTag =
    "old-school-oc1-action-regression-v1.production";

inline constexpr std::uint64_t kReferenceSeedBase = 1414213562ULL;
inline constexpr std::uint64_t kProductionSeedBase =
    5787775625948253273ULL;

inline constexpr std::size_t kReferenceWorlds = 64;
inline constexpr std::size_t kReferenceHorizonTurns = 8;
inline constexpr std::size_t kReferenceRolloutsPerWorld = 1;
inline constexpr std::size_t kReferenceEvaluationThreads = 4;
inline constexpr bool kReferenceBlendShallowPrior = false;

inline constexpr std::size_t kProductionWorlds = 8;
inline constexpr std::size_t kProductionHorizonTurns = 4;
inline constexpr std::size_t kProductionRolloutsPerWorld = 1;
inline constexpr std::size_t kProductionEvaluationThreads = 1;
inline constexpr bool kProductionBlendShallowPrior = true;

inline constexpr LearnedVariant kContinuationVariant =
    LearnedVariant::ValueSearchChampion;
inline constexpr double kValueContinuationEpsilon = 0.0;
inline constexpr double kValuePriorityResidualWeight = 0.0;
inline constexpr bool kValuePassDominance = false;
inline constexpr LearnedContinuationController kContinuationController =
    LearnedContinuationController::Legacy;

// A derived-seed Value-mirror recipe. The scorer always derives the seed
// from seed_tag, the probe stable ID, and seed_base. An exact DVR2
// confirmation seed uses the separate score_reference_with_seed entry point
// and therefore cannot silently override a normal tag/base derivation.
struct SearchRecipe {
    std::string_view seed_tag;
    std::uint64_t seed_base = 0;
    std::size_t worlds = 0;
    std::size_t horizon_turns = 0;
    std::size_t rollouts_per_world = 0;
    bool blend_shallow_prior = false;
    std::size_t evaluation_threads = 0;

    bool operator==(const SearchRecipe&) const = default;
};

inline constexpr SearchRecipe kBalancedReferenceRecipe{
    .seed_tag = kBalancedReferenceTag,
    .seed_base = kReferenceSeedBase,
    .worlds = kReferenceWorlds,
    .horizon_turns = kReferenceHorizonTurns,
    .rollouts_per_world = kReferenceRolloutsPerWorld,
    .blend_shallow_prior = kReferenceBlendShallowPrior,
    .evaluation_threads = kReferenceEvaluationThreads,
};

inline constexpr SearchRecipe kFocusedScoutRecipe{
    .seed_tag = kFocusedScoutTag,
    .seed_base = kReferenceSeedBase,
    .worlds = kReferenceWorlds,
    .horizon_turns = kReferenceHorizonTurns,
    .rollouts_per_world = kReferenceRolloutsPerWorld,
    .blend_shallow_prior = kReferenceBlendShallowPrior,
    .evaluation_threads = kReferenceEvaluationThreads,
};

inline constexpr SearchRecipe kFocusedConfirmationRecipe{
    .seed_tag = kFocusedConfirmationTag,
    .seed_base = kReferenceSeedBase,
    .worlds = kReferenceWorlds,
    .horizon_turns = kReferenceHorizonTurns,
    .rollouts_per_world = kReferenceRolloutsPerWorld,
    .blend_shallow_prior = kReferenceBlendShallowPrior,
    .evaluation_threads = kReferenceEvaluationThreads,
};

inline constexpr SearchRecipe kProductionPriorityRecipe{
    .seed_tag = kProductionTag,
    .seed_base = kProductionSeedBase,
    .worlds = kProductionWorlds,
    .horizon_turns = kProductionHorizonTurns,
    .rollouts_per_world = kProductionRolloutsPerWorld,
    .blend_shallow_prior = kProductionBlendShallowPrior,
    .evaluation_threads = kProductionEvaluationThreads,
};

enum class ScoreMode : std::uint8_t {
    ReferenceSearch,
    ProductionPrioritySearch,
    ProductionImmediateAttack,
    ProductionImmediateBlock,
};

enum class SeedSource : std::uint8_t {
    Derived,
    Explicit,
    Seedless,
};

struct AppliedRecipe {
    SeedSource seed_source = SeedSource::Derived;
    std::string seed_tag;
    std::uint64_t seed_base = 0;
    std::optional<std::uint64_t> resolved_seed;
    std::size_t worlds = 0;
    std::size_t horizon_turns = 0;
    std::size_t rollouts_per_world = 0;
    bool blend_shallow_prior = false;
    std::size_t evaluation_threads = 0;
    bool value_mirror = false;
    double value_continuation_epsilon = 0.0;
    double value_priority_residual_weight = 0.0;
    bool value_pass_dominance = false;
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;

    bool operator==(const AppliedRecipe&) const = default;
};

struct EvaluationAccounting {
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;

    bool operator==(const EvaluationAccounting&) const = default;
};

struct DescriptorScore {
    std::string descriptor;
    // Search rows contain the exact world-major, rollout-major Q values.
    // Immediate production selectors have no rollout samples and leave this
    // vector empty.
    std::vector<double> raw_samples;
    // Priority search exposes the engine's exact aggregate score, preserving
    // the deployed shallow-then-continuation arithmetic. Binary reference
    // search uses the arithmetic mean of raw_samples. Immediate selectors
    // expose their exact native score here.
    double raw_score = 0.0;

    bool operator==(const DescriptorScore&) const = default;
};

struct DecisionScore {
    std::string stable_id;
    probes::DecisionKind decision_kind =
        probes::DecisionKind::Priority;
    ScoreMode score_mode = ScoreMode::ReferenceSearch;
    AppliedRecipe recipe;
    // Always descriptor-canonical and complete.
    std::vector<DescriptorScore> actions;
    // Always descriptor-canonical. Priority search exposes every exact
    // maximum; immediate combat exposes the deployed native singleton.
    std::vector<std::string> selected_support;
    bool deterministic_selection = false;
    EvaluationAccounting accounting;

    bool operator==(const DecisionScore&) const = default;
};

// Returns every descriptor whose finite raw score is exactly equal to the
// maximum raw score. The result is sorted and contains no tolerance ties.
std::vector<std::string> exact_max_support(
    const std::vector<DescriptorScore>& actions);

DecisionScore score_reference(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    const SearchRecipe& recipe);

// Scores an externally supplied hidden repartition after checking only the
// owner's observation and exact action/context identity. The generic search
// sampler remains responsible for drawing determinizations.
DecisionScore score_reference_hidden_clone(
    const probes::DecisionProbe& probe,
    const probes::DecisionProbe& hidden_clone,
    std::shared_ptr<const LearnedModel> model,
    const SearchRecipe& recipe);

// DVR2 confirmation path: the exact stored seed is used directly with the
// frozen K64/H8/R1, unblended, four-thread Value-mirror recipe.
DecisionScore score_reference_with_seed(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t exact_seed);

DecisionScore score_reference_with_seed_hidden_clone(
    const probes::DecisionProbe& probe,
    const probes::DecisionProbe& hidden_clone,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t exact_seed);

DecisionScore score_production(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model);

DecisionScore score_production_hidden_clone(
    const probes::DecisionProbe& probe,
    const probes::DecisionProbe& hidden_clone,
    std::shared_ptr<const LearnedModel> model);

// Exact structural comparison with IEEE-754 bit comparison for every score
// and sample. Canonical rows make this suitable for reverse-input and
// hidden-repartition audits.
bool bit_identical(const DecisionScore& first,
                   const DecisionScore& second);

} // namespace old_school::oc1_action_scoring
