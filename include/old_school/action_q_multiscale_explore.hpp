#pragma once

#include "old_school/action_q_bellman_teacher.hpp"
#include "old_school/action_q_explore.hpp"
#include "old_school/action_q_multiscale_teacher.hpp"
#include "old_school/action_q_offline_gate.hpp"
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

namespace old_school::action_q_multiscale_explore {

inline constexpr std::uint64_t kCollectionRootSeed =
    202607281935ULL;
inline constexpr std::uint64_t kReservedSelectorSeed =
    202607281945ULL;
inline constexpr std::size_t kSelectorRepetitions = 1;
inline constexpr std::size_t kExpectedSelectorGames = 60;
inline constexpr std::size_t kExpectedSelectorGamesPerDeck = 12;
inline constexpr std::size_t kSelectorWinsRequired = 31;
inline constexpr std::uint64_t kScheduleGeneration = 0;
inline constexpr std::size_t kFitBlock = 0;
inline constexpr std::size_t kCheckBlock = 1;
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kBaseWorlds = 8;
inline constexpr std::size_t kBaseRolloutsPerWorld = 1;
inline constexpr std::size_t kBaseHorizonTurns = 4;
inline constexpr std::size_t kBaseEvaluationThreads = 4;
inline constexpr std::size_t kMaximumRootsPerActorGame = 8;
inline constexpr std::size_t kPolicyFeatureCount = 893;
inline constexpr double kTeacherTemperature = 0.10;
inline constexpr double kTeacherPrimaryWeight = 0.90;
inline constexpr double kCandidateResidualWeight = 0.10;
inline constexpr std::uint64_t kFitSeed =
    6876098192504870922ULL;
inline constexpr std::string_view kRequiredParentFingerprint =
    action_q_explore::kRequiredParentFingerprint;
inline constexpr std::uint64_t kPreflightRootSeed =
    202607281913ULL;
inline constexpr std::size_t kPreflightFixtureCount = 4;

// Filled only after the model-free --census result is recorded. Keeping this
// empty makes --run fail closed while the census remains unmeasured.
inline constexpr std::string_view kFrozenCensusIdentity = "";

struct RootCoordinate {
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t starting_player = 0;
    std::size_t actor = 0;
    std::size_t trace_ordinal = 0;
    std::size_t nontrivial_ordinal = 0;
    std::size_t actor_game_nontrivial_roots = 0;
    std::size_t retained_position = 0;
    std::array<DeckId, 2> seat_decks{
        DeckId::Green,
        DeckId::Red,
    };
    std::uint64_t search_seed = 0;

    bool operator==(const RootCoordinate&) const = default;
};

struct RootExample {
    RootCoordinate coordinate;
    std::string information_set_fingerprint;
    std::vector<std::string> action_descriptors;
    std::vector<PriorityAction> actions;
    std::vector<std::vector<double>> options;
    std::vector<double> base_scores;
    std::vector<double> bellman_scores;
    std::vector<double> resolved_scores;
    std::vector<double> teacher_scores;
    std::vector<std::vector<
        action_q_multiscale_teacher::ResolvedSample>>
        resolved_samples;
    std::vector<
        action_q_multiscale_teacher::ResolvedActionAccounting>
        resolved_action_accounting;
    std::vector<double> target_probabilities;
    std::size_t base_sampled_worlds = 0;
    std::size_t base_rollout_evaluations = 0;
    std::size_t base_terminal_evaluations = 0;
    std::size_t base_bootstrapped_evaluations = 0;
    action_q_bellman_teacher::TeacherAccounting
        bellman_accounting;
    action_q_multiscale_teacher::ResolvedAccounting
        resolved_accounting;
    double weight = 0.0;

    DeckId owner_deck() const;
    bool operator==(const RootExample&) const = default;
};

struct DeckCensus {
    std::size_t actor_games = 0;
    std::size_t nontrivial_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t retained_options = 0;
    std::size_t nonzero_spread_roots = 0;
    std::size_t minimum_legal_width = 0;
    std::size_t maximum_legal_width = 0;
    std::size_t base_score_calls = 0;
    std::size_t base_sampled_worlds = 0;
    std::size_t base_rollout_evaluations = 0;
    std::size_t base_terminal_evaluations = 0;
    std::size_t base_bootstrapped_evaluations = 0;
    action_q_bellman_teacher::TeacherAccounting
        bellman_accounting;
    action_q_multiscale_teacher::ResolvedAccounting
        resolved_accounting;
    double root_weight = 0.0;

    bool operator==(const DeckCensus&) const = default;
};

struct BlockCensus {
    std::size_t block = 0;
    std::size_t games = 0;
    std::array<DeckCensus, kDeckCount> decks{};

    std::size_t retained_roots() const;
    std::size_t retained_options() const;
    DeckCensus total() const;
    bool operator==(const BlockCensus&) const = default;
};

struct CorpusBlock {
    std::size_t block = 0;
    std::vector<RootExample> roots;
    BlockCensus census;

    bool operator==(const CorpusBlock&) const = default;
};

struct Corpus {
    std::uint64_t root_seed = kCollectionRootSeed;
    std::string parent_fingerprint;
    CorpusBlock fit;
    CorpusBlock check;

    bool operator==(const Corpus&) const = default;
};

using Metrics = action_q_explore::Metrics;
using FitReport = action_q_explore::FitReport;

struct OfflineReport {
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    action_q_offline_gate::IsolationGate isolation;
    action_q_offline_gate::CheckGate check;
    action_q_offline_gate::ModelGateReport model;
    bool frozen_dev_blue_regret_no_worse = false;
    bool corpus_accounting_complete = false;

    bool gate_passed() const;
    std::vector<std::string> failures() const;
};

enum class PreflightKind {
    StrictPair,
    ExcludeXZero,
};

struct PreflightSpec {
    std::size_t fixture_index = 0;
    std::string_view stable_id;
    PreflightKind kind = PreflightKind::StrictPair;
    std::string_view positive_key;
    std::string_view negative_key;
    std::array<std::string_view, 2> excluded_keys{};
    std::uint64_t expected_seed = 0;

    bool operator==(const PreflightSpec&) const = default;
};

struct PreflightAction {
    std::string probe_key;
    std::string typed_descriptor;
    PriorityAction action;
    double bellman_value = 0.0;
    double resolved_value = 0.0;
    double value = 0.0;
    bool exact_max = false;

    bool operator==(const PreflightAction&) const = default;
};

struct DirectionSummary {
    bool passed = false;
    double required_margin = 0.0;
    double positive_value = 0.0;
    double negative_value = 0.0;
    std::array<double, 2> excluded_margins{};
    std::vector<std::string> exact_max_support;

    bool operator==(const DirectionSummary&) const = default;
};

struct PreflightFixtureReport {
    PreflightSpec spec;
    std::uint64_t seed = 0;
    std::string information_set_fingerprint;
    std::vector<PreflightAction> actions;
    action_q_bellman_teacher::TeacherAccounting
        bellman_accounting;
    action_q_multiscale_teacher::ResolvedAccounting
        resolved_accounting;
    DirectionSummary direction;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_repartition_bit_identical = false;
    bool reversed_action_bit_identical = false;

    bool gate_passed() const;
};

struct PreflightReport {
    std::string parent_fingerprint;
    std::vector<PreflightFixtureReport> fixtures;
    std::array<bool, kPreflightFixtureCount>
        direction_passed{};
    bool hypothesis_passed = false;

    bool gate_passed() const;
};

enum class Command {
    Census,
    Run,
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

std::uint64_t root_search_seed(
    std::size_t block, std::size_t schedule_index,
    std::size_t actor, std::size_t nontrivial_ordinal);
std::uint64_t fit_seed();
LearnedValuePriorityHeadUpdateConfig optimizer_config();
bool selector_wins_advance(std::size_t challenger_wins);

std::uint64_t preflight_seed(std::size_t fixture_index);
std::array<PreflightSpec, kPreflightFixtureCount>
preflight_manifest();
DirectionSummary evaluate_direction(
    const PreflightSpec& spec,
    std::span<const PreflightAction> actions);
PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent);
void print_preflight_report(
    std::ostream& output,
    const PreflightReport& report);

bool owner_partition_complete(
    const action_q_bellman_teacher::TeacherAccounting&
        accounting,
    std::size_t action_count);
void validate_block(const CorpusBlock& block);
void validate_corpus(const Corpus& corpus);
Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent);
Metrics evaluate(
    const CorpusBlock& block,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight);
FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);
OfflineReport evaluate_offline(
    const Corpus& corpus,
    const FitReport& fit,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate);

std::string canonical_census_identity(
    const Corpus& corpus);
void require_frozen_census(const Corpus& corpus);
void print_census(
    std::ostream& output, const Corpus& corpus);
void print_model_gate_report(
    std::ostream& output,
    const action_q_offline_gate::ModelGateReport& report);

} // namespace old_school::action_q_multiscale_explore
