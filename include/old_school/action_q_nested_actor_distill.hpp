#pragma once

#include "old_school/action_q_explore.hpp"
#include "old_school/action_q_nested_actor_diagnostic.hpp"
#include "old_school/action_q_offline_gate.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::action_q_nested_actor_distill {

inline constexpr std::uint64_t kCollectionRootSeed =
    202607282031ULL;
inline constexpr std::uint64_t kFitSeed =
    12262988820247274425ULL;
inline constexpr std::uint64_t kSelectorSeed =
    202607282041ULL;
inline constexpr std::uint64_t kActorLocalSeed =
    855487582482885674ULL;
inline constexpr std::size_t kScheduleGeneration = 0;
inline constexpr std::size_t kScheduleBlock = 0;
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kMaximumRootsPerActorGame = 8;
inline constexpr std::size_t kPolicyFeatureCount = 893;
inline constexpr std::size_t kBaseWorlds = 8;
inline constexpr std::size_t kBaseHorizonTurns = 4;
inline constexpr std::size_t kTeacherWorlds = 8;
inline constexpr std::size_t kTeacherHorizonTurns = 8;
inline constexpr std::size_t kTeacherEvaluationThreads = 4;
inline constexpr std::size_t kInnerWorlds = 2;
inline constexpr double kCandidateResidualWeight = 0.10;
inline constexpr double kMaximumCheckDeckRegretIncrease = 0.01;
inline constexpr std::size_t kSelectorRepetitions = 1;
inline constexpr std::size_t kExpectedSelectorGames = 60;
inline constexpr std::size_t kExpectedSelectorGamesPerDeck = 12;
inline constexpr std::size_t kFastGoWins = 37;
inline constexpr std::size_t kManualOnlyWins = 31;
inline constexpr std::size_t kMinimumDeckWins = 3;
inline constexpr std::string_view kRequiredParentFingerprint =
    action_q_explore::kRequiredParentFingerprint;

// Filled only after `--census` has been recorded. An empty value makes
// `--run` fail before any AQ4 preflight or teacher coordinate is opened.
inline constexpr std::string_view kFrozenCensusManifestHash =
    "c67d345dba6f2ea1c59014aefd56aadfbf6560daa610445f"
    "f686a9fe0999d80b";

enum class Command {
    Census,
    Run,
};

enum class Split : std::uint8_t {
    Fit = 0,
    Check = 1,
};

enum class SelectorDisposition {
    FastGo,
    ManualOnly,
    Reject,
};

struct RootCoordinate {
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t starting_player = 0;
    std::array<DeckId, 2> seat_decks{
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t actor = 0;
    std::size_t trace_ordinal = 0;
    std::size_t nontrivial_ordinal = 0;
    std::size_t actor_game_nontrivial_roots = 0;
    std::size_t retained_position = 0;
    Split split = Split::Fit;
    std::uint64_t search_seed = 0;

    DeckId owner_deck() const;
    bool operator==(const RootCoordinate&) const = default;
};

// Owner-safe immutable root identity. Features encode the owner's private
// hand plus public information; no physical GameState or hidden-zone payload
// can enter this type.
struct ManifestRoot {
    RootCoordinate coordinate;
    std::vector<PriorityAction> actions;
    std::vector<std::string> action_descriptors;
    std::vector<std::vector<double>> options;

    bool operator==(const ManifestRoot&) const = default;
};

struct DeckCensus {
    std::size_t actor_games = 0;
    std::size_t nontrivial_roots = 0;
    std::array<std::size_t, 2> retained_roots{};
    std::array<std::size_t, 2> retained_options{};

    bool operator==(const DeckCensus&) const = default;
};

struct Census {
    std::uint64_t root_seed = kCollectionRootSeed;
    std::string parent_fingerprint;
    std::size_t games = 0;
    std::array<DeckCensus, kDeckCount> decks{};
    std::vector<ManifestRoot> roots;
    std::string manifest_hash;

    bool operator==(const Census&) const = default;
};

struct RootAccounting {
    std::size_t base_sampled_worlds = 0;
    std::size_t base_rollout_evaluations = 0;
    std::size_t base_terminal_evaluations = 0;
    std::size_t base_bootstrapped_evaluations = 0;
    std::size_t teacher_sampled_worlds = 0;
    std::size_t teacher_rollout_evaluations = 0;
    std::size_t teacher_terminal_evaluations = 0;
    std::size_t teacher_bootstrapped_evaluations = 0;
    std::size_t teacher_inner_rollout_evaluations = 0;
    std::size_t teacher_inner_search_invocations = 0;
    std::size_t teacher_inner_search_max_depth = 0;

    bool operator==(const RootAccounting&) const = default;
};

struct RootExample {
    ManifestRoot manifest;
    std::vector<double> base_scores;
    std::vector<double> teacher_scores;
    std::vector<double> target_probabilities;
    RootAccounting accounting;
    double weight = 0.0;

    bool operator==(const RootExample&) const = default;
};

struct Corpus {
    Census census;
    std::vector<RootExample> fit;
    std::vector<RootExample> check;

    bool operator==(const Corpus&) const = default;
};

using Metrics = action_q_explore::Metrics;

struct FitReport {
    std::shared_ptr<const LearnedModel> candidate;
    std::string parent_fingerprint_before;
    std::string parent_fingerprint_after;
    std::string candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    LearnedValuePriorityHeadUpdateConfig optimizer;
    std::size_t fit_examples = 0;
    std::size_t fit_options = 0;
    bool parent_immutable = false;
    bool repeated_fit_bit_identical = false;
    bool only_priority_component_changed = false;
    Metrics parent_fit;
    Metrics candidate_fit;
    Metrics parent_check;
    Metrics candidate_check;
};

struct OfflineReport {
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    bool parent_immutable = false;
    bool repeated_fit_bit_identical = false;
    bool only_priority_component_changed = false;
    bool fit_regret_strictly_improved = false;
    bool check_regret_strictly_improved = false;
    std::array<bool, kDeckCount> check_deck_regret_guard{};
    bool descriptor_order_identity = false;
    bool redundant_counter_pass = false;
    bool braingeyser_x_zero_excluded = false;
    bool sick_bear_growth_pass = false;
    bool live_force_spike = false;
    action_q_offline_gate::FrozenDevGate frozen_dev;
    action_q_offline_gate::AncestralGate ancestral;
    Metrics parent_fit;
    Metrics candidate_fit;
    Metrics parent_check;
    Metrics candidate_check;

    bool gate_passed() const;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

Split split_for_retained_position(std::size_t retained_position);
std::uint64_t root_search_seed(
    std::size_t schedule_index, std::size_t actor,
    std::size_t nontrivial_ordinal);
action_q_nested_actor_diagnostic::PreflightRecipe
preflight_recipe();
LearnedSearchConfig base_search_config(std::uint64_t seed);
LearnedSearchConfig teacher_search_config(std::uint64_t seed);
LearnedValuePriorityHeadUpdateConfig optimizer_config();

std::string canonical_manifest_hash(const Census& census);
void validate_census(const Census& census);
void require_frozen_census(const Census& census);
Census collect_census(std::shared_ptr<const LearnedModel> parent);
Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const Census& frozen_census);
void validate_corpus(const Corpus& corpus);

Metrics evaluate(
    std::span<const RootExample> examples,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight);
FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);
FitReport fit_with_optimizer(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    LearnedValuePriorityHeadUpdateConfig optimizer);
OfflineReport evaluate_offline(
    const Corpus& corpus, const FitReport& fit,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate);

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary);
BotConfig selector_bot_config(
    std::shared_ptr<const LearnedModel> model,
    double residual_weight);
void validate_selector_summary(
    const BotBenchmarkSummary& summary,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    std::uint64_t expected_seed);
BotBenchmarkSummary run_selector(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const FitReport& fit, const OfflineReport& offline);

void print_census(std::ostream& output, const Census& census);
void print_preflight(
    std::ostream& output,
    const action_q_nested_actor_diagnostic::PreflightReport&
        report);
void print_offline(
    std::ostream& output, const Corpus& corpus,
    const FitReport& fit, const OfflineReport& report);
void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary,
    SelectorDisposition disposition);

namespace testing {

// Pure census constructor for unit tests. Production census collection never
// accepts caller-supplied roots.
Census make_census(
    std::string parent_fingerprint,
    std::vector<ManifestRoot> roots,
    std::array<DeckCensus, kDeckCount> decks,
    std::size_t games);

} // namespace testing

} // namespace old_school::action_q_nested_actor_distill
