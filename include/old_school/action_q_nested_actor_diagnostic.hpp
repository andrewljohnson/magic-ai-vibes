#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::action_q_nested_actor_diagnostic {

inline constexpr std::uint64_t kRootSeed = 202607282011ULL;
inline constexpr std::size_t kFixtureCount = 4;
inline constexpr std::size_t kWorlds = 32;
inline constexpr std::size_t kRolloutsPerWorld = 1;
inline constexpr std::size_t kHorizonTurns = 8;
inline constexpr std::size_t kEvaluationThreads = 4;
inline constexpr std::size_t kInnerWorlds = 2;
inline constexpr std::size_t kInnerHorizonTurns =
    kLearnedValueSearchHorizonTurns;
inline constexpr std::uint64_t kActorLocalSeed =
    9047197069870339270ULL;
inline constexpr std::string_view kRequiredParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

enum class DirectionKind {
    StrictPair,
    ExcludeXZero,
};

struct FixtureSpec {
    std::size_t fixture_index = 0;
    std::string_view stable_id;
    DirectionKind kind = DirectionKind::StrictPair;
    std::string_view positive_key;
    std::string_view negative_key;
    std::string_view secondary_negative_key;
    std::array<std::string_view, 2> excluded_keys{};
    std::uint64_t expected_seed = 0;

    bool operator==(const FixtureSpec&) const = default;
};

struct ActionScore {
    std::string probe_key;
    std::string typed_descriptor;
    PriorityAction action;
    std::vector<double> samples;
    std::vector<std::size_t> inner_rollout_evaluations;
    std::vector<std::size_t> inner_search_invocations;
    std::vector<std::size_t> inner_search_max_depth;
    double mean = 0.0;
    bool exact_max = false;
};

struct EvaluationAccounting {
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    std::size_t inner_rollout_evaluations = 0;
    std::size_t inner_search_invocations = 0;
    std::size_t inner_search_max_depth = 0;

    bool operator==(const EvaluationAccounting&) const = default;
};

struct RootScore {
    std::vector<ActionScore> actions;
    EvaluationAccounting accounting;
    std::string selected_probe_key;
};

struct DirectionSummary {
    bool passed = false;
    double required_margin = 0.0;
    double positive_value = 0.0;
    double negative_value = 0.0;
    std::array<double, 2> excluded_margins{};
    std::vector<std::string> exact_max_support;
};

struct FixtureReport {
    FixtureSpec spec;
    std::uint64_t seed = 0;
    RootScore score;
    DirectionSummary direction;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_repartition_bit_identical = false;
    bool reversed_action_bit_identical = false;

    bool gate_passed() const;
};

struct ActorLocalReport {
    std::uint64_t seed = 0;
    RootScore score;
    bool hidden_repartition_nonvacuous = false;
    bool observation_bit_identical = false;
    bool legal_actions_bit_identical = false;
    bool score_bit_identical = false;
    bool one_level_nesting_bounded = false;

    bool gate_passed() const;
};

struct Report {
    std::string parent_fingerprint;
    std::vector<FixtureReport> fixtures;
    ActorLocalReport actor_local;
    std::array<bool, kFixtureCount> direction_passed{};
    bool hypothesis_passed = false;

    bool gate_passed() const;
};

enum class Command {
    Diagnose,
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

std::uint64_t fixture_seed(std::size_t fixture_index);
std::uint64_t actor_local_seed();
std::array<FixtureSpec, kFixtureCount> fixture_manifest();
LearnedSearchConfig outer_search_config(std::uint64_t seed);
LearnedSearchConfig actor_search_config(std::uint64_t seed);

DirectionSummary evaluate_direction(
    const FixtureSpec& spec,
    std::span<const ActionScore> actions);
bool root_scores_bit_identical(
    const RootScore& first, const RootScore& second);
void require_invariant_root_scores(
    const RootScore& direct,
    const RootScore& hidden_repartition,
    const RootScore& reversed_actions);

// Non-science preflight. It validates all root witnesses and constructs the
// actor-local Red response pair without scoring either state.
void validate_fixture_witnesses();
Report diagnose(std::shared_ptr<const LearnedModel> parent);
void print_report(std::ostream& output, const Report& report);

} // namespace old_school::action_q_nested_actor_diagnostic
