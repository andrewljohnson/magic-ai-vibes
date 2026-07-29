#pragma once

#include "old_school/action_q_nested_actor_distill.hpp"
#include "old_school/action_q_offline_gate.hpp"
#include "old_school/learned_iteration.hpp"

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

namespace old_school::action_q_nested_actor_broad_distill {

namespace g1 = action_q_nested_actor_distill;

inline constexpr std::uint64_t kCollectionRootSeed =
    202607282301ULL;
inline constexpr std::uint64_t kSelectorSeed =
    202607282311ULL;
inline constexpr std::size_t kScheduleGeneration = 0;
inline constexpr std::size_t kTrainBlock = 0;
inline constexpr std::size_t kDevBlock = 1;
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kTrainMaximumRootsPerActorGame = 6;
inline constexpr std::size_t kDevMaximumRootsPerActorGame = 2;
inline constexpr std::size_t kPolicyFeatureCount =
    g1::kPolicyFeatureCount;
inline constexpr std::size_t kBaseWorlds = g1::kBaseWorlds;
inline constexpr std::size_t kTeacherWorlds =
    g1::kTeacherWorlds;
inline constexpr std::size_t kInnerWorlds = g1::kInnerWorlds;
inline constexpr double kCandidateResidualWeight =
    g1::kCandidateResidualWeight;
inline constexpr double kMaximumDevDeckRegretIncrease = 0.01;
inline constexpr std::size_t kActorGamesPerDeckAndSplit = 16;
inline constexpr std::size_t kGamesPerSplit =
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kActorGamesPerSplit =
    kGamesPerSplit * 2;
inline constexpr std::size_t kTrainRootCeiling =
    kActorGamesPerSplit * kTrainMaximumRootsPerActorGame;
inline constexpr std::size_t kDevRootCeiling =
    kActorGamesPerSplit * kDevMaximumRootsPerActorGame;
inline constexpr std::string_view kRequiredParentFingerprint =
    g1::kRequiredParentFingerprint;

// Filled only after the control-only `--preflight` replay is recorded.
// Keeping this empty seals `--census` and `--run` before a bit-exact G1
// preflight report has been reviewed and frozen.
inline constexpr std::string_view kFrozenPreflightDigest = "";

// Filled only after the source-only `--census` result is recorded. Keeping
// this empty seals `--run` before any G4B search coordinate can be opened.
inline constexpr std::string_view kFrozenCensusManifestHash = "";

enum class Command {
    Preflight,
    Census,
    Run,
};

enum class Split : std::uint8_t {
    Train = 0,
    Dev = 1,
};

using SelectorDisposition = g1::SelectorDisposition;
using RootAccounting = g1::RootAccounting;

struct RootCoordinate {
    Split split = Split::Train;
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
    std::size_t actor_game_retained_roots = 0;
    std::uint64_t search_seed = 0;

    DeckId owner_deck() const;
    bool operator==(const RootCoordinate&) const = default;
};

// This is the complete retained representation. It contains the owner's
// information-set features and public typed actions, never a GameState or an
// opponent-hidden card identity.
struct ManifestRoot {
    RootCoordinate coordinate;
    std::string stable_root_id;
    std::string information_action_fingerprint;
    std::vector<PriorityAction> actions;
    std::vector<std::string> action_descriptors;
    std::vector<std::vector<double>> options;

    bool operator==(const ManifestRoot&) const = default;
};

struct ActorGameCensus {
    Split split = Split::Train;
    std::size_t schedule_index = 0;
    std::size_t actor = 0;
    DeckId owner_deck = DeckId::Green;
    std::size_t nontrivial_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t retained_options = 0;

    bool operator==(const ActorGameCensus&) const = default;
};

struct DeckCensus {
    std::size_t actor_games = 0;
    std::size_t nontrivial_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t retained_options = 0;

    bool operator==(const DeckCensus&) const = default;
};

struct SplitCensus {
    Split split = Split::Train;
    std::size_t games = 0;
    std::array<DeckCensus, kDeckCount> decks{};
    std::vector<ActorGameCensus> actor_games;

    std::size_t retained_roots() const;
    std::size_t retained_options() const;
    bool operator==(const SplitCensus&) const = default;
};

struct Census {
    std::uint64_t root_seed = kCollectionRootSeed;
    std::string parent_fingerprint;
    std::array<SplitCensus, 2> splits;
    std::vector<ManifestRoot> roots;
    std::string manifest_hash;

    std::size_t games() const;
    bool operator==(const Census&) const = default;
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
    LearnedModelComponentFingerprints parent_components;
    std::vector<RootExample> train;
    std::vector<RootExample> dev;
    std::string digest;

    bool operator==(const Corpus&) const = default;
};

struct DeckMetrics {
    DeckId deck = DeckId::Green;
    std::size_t roots = 0;
    std::size_t options = 0;
    double weight_mass = 0.0;
    double top_one_expected_agreement = 0.0;
    double mean_regret = 0.0;

    bool operator==(const DeckMetrics&) const = default;
};

struct Metrics {
    std::array<DeckMetrics, kDeckCount> decks{};
    std::size_t roots = 0;
    std::size_t options = 0;
    double equal_deck_top_one_expected_agreement = 0.0;
    double equal_deck_mean_regret = 0.0;

    bool operator==(const Metrics&) const = default;
};

struct FitReport {
    std::shared_ptr<const LearnedModel> candidate;
    std::string corpus_digest;
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
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_dev;
    Metrics candidate_dev;
};

struct OfflineReport {
    std::string corpus_digest;
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    bool census_frozen = false;
    bool corpus_digest_exact = false;
    bool preflight_exact = false;
    bool parent_immutable = false;
    bool repeated_fit_bit_identical = false;
    bool only_priority_component_changed = false;
    bool train_regret_strictly_improved = false;
    bool dev_regret_strictly_improved = false;
    std::array<bool, kDeckCount> dev_deck_regret_guard{};
    std::array<bool, kDeckCount> parent_train_signal_nonzero{};
    std::array<bool, kDeckCount> parent_dev_signal_nonzero{};
    bool targets_finite_and_normalized = false;
    bool descriptor_order_identity = false;
    bool redundant_counter_pass = false;
    bool braingeyser_productive = false;
    bool sick_bear_growth_pass = false;
    bool live_force_spike = false;
    bool ancestral_pass = false;
    action_q_offline_gate::FrozenDevGate frozen_dev;
    action_q_offline_gate::AncestralGate ancestral;
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_dev;
    Metrics candidate_dev;
    std::vector<std::string> failures;

    bool gate_passed() const;
    bool operator==(const OfflineReport&) const;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

std::size_t split_index(Split split);
std::size_t maximum_roots_per_actor_game(Split split);
std::uint64_t root_search_seed(
    Split split, std::size_t schedule_index,
    std::size_t actor, std::size_t nontrivial_ordinal);
double root_weight(std::size_t actor_game_retained_roots);

action_q_nested_actor_diagnostic::PreflightRecipe
preflight_recipe();
std::string canonical_preflight_digest(
    const action_q_nested_actor_diagnostic::PreflightReport&
        report);
bool preflight_exact(
    const action_q_nested_actor_diagnostic::PreflightReport&
        report);
LearnedSearchConfig base_search_config(std::uint64_t seed);
LearnedSearchConfig teacher_search_config(std::uint64_t seed);
LearnedValuePriorityHeadUpdateConfig optimizer_config();

std::string canonical_manifest_hash(const Census& census);
std::string canonical_corpus_digest(const Corpus& corpus);
void validate_census(const Census& census);
void require_frozen_census(const Census& census);
void validate_corpus(const Corpus& corpus);

Census collect_census(std::shared_ptr<const LearnedModel> parent);
Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const Census& frozen_census,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight);

Metrics evaluate(
    std::span<const RootExample> examples,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight);
FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);
OfflineReport evaluate_offline(
    const Corpus& corpus, const FitReport& fit,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate);

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary);
BotBenchmarkSummary run_selector(
    const Corpus& corpus,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight,
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

// Pure assembly seam for mutation tests. Production collection never accepts
// caller-supplied roots or actor-game counts.
Census make_census(
    std::string parent_fingerprint,
    std::array<SplitCensus, 2> splits,
    std::vector<ManifestRoot> roots);

// Exact fit projection. DEV is deliberately absent from the returned vector.
// This performs full corpus validation but no model update or search.
std::vector<LearnedValuePriorityTrainingExample>
training_examples(const Corpus& corpus);

// Pure authorization-binding seam. Production first recomputes `recomputed`
// from the live models, then calls the same predicate before opening the
// selector. Explicit expected digests make the synthetic mutation tests
// independent of the deliberately empty production seals.
bool selector_binding_exact(
    const Corpus& corpus,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight,
    const FitReport& fit,
    const OfflineReport& supplied,
    const OfflineReport& recomputed,
    std::string_view parent_fingerprint,
    std::string_view candidate_fingerprint,
    std::string_view required_preflight_digest,
    std::string_view required_census_hash);

} // namespace testing

} // namespace old_school::action_q_nested_actor_broad_distill
