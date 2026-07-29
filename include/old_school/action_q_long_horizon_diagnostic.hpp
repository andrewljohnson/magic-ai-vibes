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

namespace old_school::action_q_long_horizon_diagnostic {

inline constexpr std::uint64_t kRootSeed = 202607281951ULL;
inline constexpr std::size_t kFixtureCount = 4;
inline constexpr std::size_t kWorlds = 64;
inline constexpr std::size_t kRolloutsPerWorld = 1;
inline constexpr std::size_t kHorizonTurns = 32;
inline constexpr std::size_t kEvaluationThreads = 4;
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
    std::array<std::string_view, 2> excluded_keys{};
    std::uint64_t expected_seed = 0;

    bool operator==(const FixtureSpec&) const = default;
};

// Scalar continuation evidence only. No sampled GameState or hidden-zone
// payload is retained by the AQ3 diagnostic.
struct ActionScore {
    std::string probe_key;
    std::string typed_descriptor;
    PriorityAction action;
    std::vector<double> samples;
    double mean = 0.0;
    bool exact_max = false;
};

struct EvaluationAccounting {
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;

    bool operator==(const EvaluationAccounting&) const = default;
};

struct RootScore {
    std::vector<ActionScore> actions;
    EvaluationAccounting accounting;
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
    std::vector<ActionScore> actions;
    EvaluationAccounting accounting;
    DirectionSummary direction;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_repartition_bit_identical = false;
    bool reversed_action_bit_identical = false;

    bool gate_passed() const;
};

struct Report {
    std::string parent_fingerprint;
    std::vector<FixtureReport> fixtures;
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
std::array<FixtureSpec, kFixtureCount> fixture_manifest();
LearnedSearchConfig search_config(std::uint64_t seed);

DirectionSummary evaluate_direction(
    const FixtureSpec& spec,
    std::span<const ActionScore> actions);
bool root_scores_bit_identical(
    const RootScore& first, const RootScore& second);
// Structural failures are invalid evidence, not a directional rejection.
// This throws before any fixture direction is interpreted.
void require_invariant_root_scores(
    const RootScore& direct,
    const RootScore& hidden_repartition,
    const RootScore& reversed_actions);

// Non-science preflight used by ordinary tests and by diagnose() before the
// first rollout: all four fixtures must have a nonvacuous hidden repartition
// and an unchanged complete legal action set.
void validate_fixture_witnesses();
Report diagnose(std::shared_ptr<const LearnedModel> parent);
void print_report(std::ostream& output, const Report& report);

} // namespace old_school::action_q_long_horizon_diagnostic
