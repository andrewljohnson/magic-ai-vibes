#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::action_q_explore {

inline constexpr std::uint64_t kCollectionRootSeed = 202607281751ULL;
inline constexpr std::uint64_t kScheduleGeneration = 0;
inline constexpr std::size_t kFitBlock = 0;
inline constexpr std::size_t kCheckBlock = 1;
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kWorlds = 8;
inline constexpr std::size_t kRolloutsPerWorld = 1;
inline constexpr std::size_t kHorizonTurns = 4;
inline constexpr std::size_t kMaximumRootsPerActorGame = 8;
inline constexpr std::size_t kPolicyFeatureCount = 893;
inline constexpr double kTeacherTemperature = 0.10;
inline constexpr double kTeacherPrimaryWeight = 0.90;
inline constexpr double kCandidateResidualWeight = 0.10;
inline constexpr std::uint64_t kFitSeed =
    15687967101834164397ULL;
inline constexpr std::string_view kRequiredParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

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

// AQ0 retains no GameState or hidden-zone payload. The complete legal action
// set and its owner-information feature rows are sufficient for fitting and
// held-out evaluation.
struct RootExample {
    RootCoordinate coordinate;
    std::vector<PriorityAction> actions;
    std::vector<std::vector<double>> options;
    std::vector<double> base_scores;
    // [action][common hidden-information world].
    std::vector<std::vector<double>> teacher_samples;
    std::vector<double> teacher_scores;
    std::vector<double> target_probabilities;
    std::size_t base_sampled_worlds = 0;
    std::size_t base_rollout_evaluations = 0;
    std::size_t base_terminal_evaluations = 0;
    std::size_t base_bootstrapped_evaluations = 0;
    double weight = 0.0;

    DeckId owner_deck() const;
    bool operator==(const RootExample&) const = default;
};

struct DeckCensus {
    std::size_t actor_games = 0;
    std::size_t nontrivial_roots = 0;
    std::size_t retained_roots = 0;
    std::size_t retained_options = 0;
    std::size_t teacher_samples = 0;
    std::size_t teacher_exact_zero_samples = 0;
    std::size_t teacher_exact_one_samples = 0;
    std::size_t nonzero_spread_roots = 0;
    std::size_t minimum_legal_width = 0;
    std::size_t maximum_legal_width = 0;
    double root_weight = 0.0;

    bool operator==(const DeckCensus&) const = default;
};

struct BlockCensus {
    std::size_t block = 0;
    std::size_t games = 0;
    std::array<DeckCensus, kDeckCount> decks{};
    std::size_t base_score_calls = 0;
    std::size_t base_sampled_worlds = 0;
    std::size_t base_rollout_evaluations = 0;
    std::size_t base_terminal_evaluations = 0;
    std::size_t base_bootstrapped_evaluations = 0;

    std::size_t retained_roots() const;
    std::size_t retained_options() const;
    std::size_t teacher_sample_count() const;
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

struct RootMetrics {
    std::size_t action_count = 0;
    std::vector<std::size_t> teacher_support;
    std::vector<std::size_t> policy_support;
    double top_one_expected_agreement = 0.0;
    double regret = 0.0;

    bool operator==(const RootMetrics&) const = default;
};

struct DeckMetrics {
    DeckId deck = DeckId::Green;
    std::size_t roots = 0;
    std::size_t options = 0;
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

// Pure AQ0 helpers. Exact maxima use binary64 equality and therefore match
// the production selector's support semantics.
std::vector<double> teacher_distribution(
    std::span<const double> teacher_scores);
std::vector<std::size_t> exact_max_support(
    std::span<const double> scores);
std::vector<double> combined_scores(
    std::span<const double> base_scores,
    std::span<const double> policy_logits,
    double residual_weight = kCandidateResidualWeight);
RootMetrics evaluate_root(
    std::span<const double> teacher_scores,
    std::span<const double> policy_scores);

std::uint64_t root_search_seed(
    std::size_t block, std::size_t schedule_index,
    std::size_t actor, std::size_t nontrivial_ordinal);
std::uint64_t fit_seed();
LearnedValuePriorityHeadUpdateConfig optimizer_config();

// Builds one hidden-safe feature/score row. The supplied state is used only
// transiently; no hidden-zone identities are retained in the result.
RootExample build_root_example(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    RootCoordinate coordinate,
    std::shared_ptr<const LearnedModel> parent);

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

} // namespace old_school::action_q_explore
