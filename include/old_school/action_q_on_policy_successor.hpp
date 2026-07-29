#pragma once

#include "old_school/action_q_nested_actor_broad_distill.hpp"

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

namespace old_school::action_q_on_policy_successor {

namespace g4b = action_q_nested_actor_broad_distill;
namespace g1 = action_q_nested_actor_distill;

inline constexpr std::uint64_t kCollectionRootSeed =
    202607290101ULL;
inline constexpr std::uint64_t kSelectorSeed =
    202607290111ULL;
inline constexpr std::size_t kScheduleGeneration = 2;
inline constexpr std::size_t kTrainBlock = 0;
inline constexpr std::size_t kDevBlock = 1;
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kSelectorTurnCap = 500;
inline constexpr std::size_t kTrainMaximumRootsPerActorGame = 6;
inline constexpr std::size_t kDevMaximumRootsPerActorGame = 2;
inline constexpr std::size_t kGamesPerSplit =
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kActorGamesPerSplit =
    kGamesPerSplit * 2;
inline constexpr std::size_t kActorGamesPerDeckAndSplit = 16;
inline constexpr std::size_t kTrainRootCeiling =
    kActorGamesPerSplit * kTrainMaximumRootsPerActorGame;
inline constexpr std::size_t kDevRootCeiling =
    kActorGamesPerSplit * kDevMaximumRootsPerActorGame;
inline constexpr std::size_t kPolicyFeatureCount =
    g4b::kPolicyFeatureCount;
inline constexpr std::size_t kBaseWorlds = g4b::kBaseWorlds;
inline constexpr std::size_t kTeacherWorlds =
    g4b::kTeacherWorlds;
inline constexpr std::size_t kInnerWorlds = g4b::kInnerWorlds;
inline constexpr double kResidualWeight =
    g4b::kCandidateResidualWeight;
inline constexpr double kMaximumDevDeckRegretIncrease = 0.01;
inline constexpr std::string_view kRequiredWarmParentFingerprint =
    "d0d46d2b4b365686d0c7109df8b32c6ec0b8229b5df9fb43f304c5f33e1003f8";

// Filled only after the source-only `--census` result is recorded in both
// source and the notebook. The hash and every measured count are independent
// conjunctive seals; leaving either empty keeps `--run` from opening a single
// OP1 label or selector coordinate.
inline constexpr std::string_view kFrozenCensusManifestHash =
    "2900062d0df381463663de6d7f25ce562197bfac0d859949ca0803e48b14aef7";

enum class Command {
    Census,
    Run,
};

using Split = g4b::Split;
using SelectorDisposition = g4b::SelectorDisposition;
using RootAccounting = g4b::RootAccounting;
using RootCoordinate = g4b::RootCoordinate;
using ManifestRoot = g4b::ManifestRoot;
using ActorGameCensus = g4b::ActorGameCensus;
using DeckCensus = g4b::DeckCensus;
using SplitCensus = g4b::SplitCensus;
using Census = g4b::Census;
using RootExample = g4b::RootExample;
using Corpus = g4b::Corpus;
using DeckMetrics = g4b::DeckMetrics;
using Metrics = g4b::Metrics;
using FitReport = g4b::FitReport;
using OfflineReport = g4b::OfflineReport;

struct SplitCountSeal {
    std::size_t games = 0;
    std::size_t actor_games = 0;
    std::size_t nontrivial_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t retained_options = 0;
    std::array<DeckCensus, kDeckCount> decks{};

    bool operator==(const SplitCountSeal&) const = default;
};

struct CensusCountSeal {
    std::array<SplitCountSeal, 2> splits{};

    bool operator==(const CensusCountSeal&) const = default;
};

inline constexpr CensusCountSeal kFrozenCensusCounts{
    .splits = {{
        SplitCountSeal{
            .games = 40,
            .actor_games = 80,
            .nontrivial_roots = 2079,
            .retained_roots = 480,
            .retained_options = 1568,
            .decks = {{
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 330,
                    .retained_roots = 96,
                    .retained_options = 241,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 393,
                    .retained_roots = 96,
                    .retained_options = 323,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 386,
                    .retained_roots = 96,
                    .retained_options = 223,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 583,
                    .retained_roots = 96,
                    .retained_options = 307,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 387,
                    .retained_roots = 96,
                    .retained_options = 474,
                },
            }},
        },
        SplitCountSeal{
            .games = 40,
            .actor_games = 80,
            .nontrivial_roots = 2442,
            .retained_roots = 160,
            .retained_options = 436,
            .decks = {{
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 425,
                    .retained_roots = 32,
                    .retained_options = 77,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 418,
                    .retained_roots = 32,
                    .retained_options = 101,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 368,
                    .retained_roots = 32,
                    .retained_options = 73,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 811,
                    .retained_roots = 32,
                    .retained_options = 93,
                },
                DeckCensus{
                    .actor_games = 16,
                    .nontrivial_roots = 420,
                    .retained_roots = 32,
                    .retained_options = 92,
                },
            }},
        },
    }},
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

GameConfig source_game_config(
    std::shared_ptr<const LearnedModel> warm_parent,
    std::size_t starting_player);
LearnedSearchConfig base_search_config(std::uint64_t seed);
LearnedSearchConfig teacher_search_config(std::uint64_t seed);
LearnedValuePriorityHeadUpdateConfig optimizer_config();

std::string canonical_manifest_hash(const Census& census);
std::string canonical_corpus_digest(const Corpus& corpus);
CensusCountSeal census_count_seal(const Census& census);
bool frozen_census_seal_populated() noexcept;
void validate_census(const Census& census);
void require_frozen_census(const Census& census);
void validate_corpus(const Corpus& corpus);

Census collect_census(
    std::shared_ptr<const LearnedModel> warm_parent);
Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> warm_parent,
    const Census& frozen_census,
    const action_q_nested_actor_diagnostic::PreflightReport&
        frozen_preflight);

Metrics evaluate(
    std::span<const RootExample> examples,
    std::shared_ptr<const LearnedModel> model);
FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> warm_parent);
OfflineReport evaluate_offline(
    const Corpus& corpus, const FitReport& fit,
    const action_q_nested_actor_diagnostic::PreflightReport&
        frozen_preflight,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> candidate);

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary);
BotBenchmarkSummary run_selector(
    const Corpus& corpus,
    const action_q_nested_actor_diagnostic::PreflightReport&
        frozen_preflight,
    std::shared_ptr<const LearnedModel> c16,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> candidate,
    const FitReport& fit, const OfflineReport& offline);

void print_census(
    std::ostream& output, const Census& census,
    std::size_t replayed_g4b_train_labels,
    std::size_t replayed_g4b_dev_labels);
void print_offline(
    std::ostream& output, const Corpus& corpus,
    const FitReport& fit, const OfflineReport& report);
void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary,
    SelectorDisposition disposition);

namespace testing {

// Pure assembly seams for validation/mutation tests. Production collection
// never accepts caller-supplied manifests, examples, or counts.
Census make_census(
    std::string parent_fingerprint,
    std::array<SplitCensus, 2> splits,
    std::vector<ManifestRoot> roots);
Corpus make_corpus(
    Census census,
    LearnedModelComponentFingerprints parent_components,
    std::vector<RootExample> train,
    std::vector<RootExample> dev);

// Exact fit projection. DEV is deliberately absent.
std::vector<LearnedValuePriorityTrainingExample>
training_examples(const Corpus& corpus);

// Pure selector authorization predicate. Production recomputes all live
// identities before calling the same predicate.
bool selector_binding_exact(
    const Corpus& corpus, const FitReport& fit,
    const OfflineReport& offline,
    std::string_view c16_fingerprint,
    std::string_view warm_parent_fingerprint,
    std::string_view candidate_fingerprint,
    std::string_view required_census_hash,
    const CensusCountSeal& required_census_counts);

} // namespace testing

} // namespace old_school::action_q_on_policy_successor
