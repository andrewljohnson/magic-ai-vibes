#pragma once

#include "old_school/probe_eval.hpp"
#include "old_school/probes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::probe_runner {

inline constexpr std::string_view kProbeCacheSchema =
    "old-school-probe-label-cache-v2";
inline constexpr std::string_view kProbeReferenceAlgorithm =
    "actor-mirror-common-world-v2";
inline constexpr std::string_view kProbeSemanticRevision =
    "old-school-probe-score-semantics-v2";
inline constexpr std::uint64_t kProbeReferenceSeed =
    0x50524F4245524546ULL;
inline constexpr std::uint64_t kProbeProductionPolicySeed =
    0x50524F44504F4C59ULL;

struct ProbeScoreConfig {
    std::size_t training_games = 800;
    std::uint64_t training_seed = kDefaultLearnedTrainingSeed;
    std::size_t reference_worlds = 128;
    std::size_t reference_horizon_turns = 12;
    std::size_t reference_rollouts_per_world = 1;
    std::filesystem::path cache_path =
        "data/old-school-probe-dev-v3.labels.tsv";
    bool refresh_cache = false;
};

struct ProbeCacheMetadata {
    std::string schema;
    std::string algorithm;
    std::string semantic_revision;
    std::string corpus_id;
    std::uint64_t reference_seed = 0;
    std::uint64_t production_policy_seed = 0;
    std::uint64_t training_seed = 0;
    std::size_t training_games = 0;
    std::size_t worlds = 0;
    std::size_t horizon_turns = 0;
    std::size_t rollouts_per_world = 0;
    std::size_t probe_count = 0;
    std::string reference_model_fingerprint;
    std::string information_set_fingerprint;

    bool operator==(const ProbeCacheMetadata&) const = default;
};

struct ProbeReferenceSamples {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::vector<probe_eval::CandidateSamples> candidates;
};

enum class ProbeCacheStatus : std::uint8_t {
    Loaded,
    Generated,
};

struct PolicyProbeReport {
    std::string name;
    std::string configuration;
    probe_eval::ProbeMetricSummary metrics;
    bool has_critic_metrics = true;
    // Present only when every policy score in the row is an estimated Q
    // probability for the corresponding candidate.
    std::optional<probe_eval::CandidateQFitSummary> candidate_q_fit;
};

struct ReferenceSensitivityFlag {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::string first;
    std::string second;
    double actor_delta_q = 0.0;
    double value_delta_q = 0.0;
    bool value_pair_is_stable = false;
};

struct DeckReferenceSensitivity {
    DeckId root_deck = DeckId::Green;
    std::size_t actor_stable_pair_count = 0;
    std::size_t point_sign_reversal_count = 0;
    std::size_t dual_stable_reversal_count = 0;
};

struct ReferenceSensitivitySummary {
    std::size_t actor_stable_pair_count = 0;
    std::size_t point_sign_reversal_count = 0;
    std::size_t dual_stable_reversal_count = 0;
    std::array<DeckReferenceSensitivity, kDeckCount> by_deck{};
    std::vector<ReferenceSensitivityFlag> flags;
};

struct LowMarginBestPair {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::string reference_best;
    std::string other;
    double delta_q = 0.0;
    double paired_standard_error = 0.0;
    bool effect_below_stable_threshold = false;
    bool confidence_interval_crosses_zero = false;
};

struct DeckLowMarginSummary {
    DeckId root_deck = DeckId::Green;
    std::size_t pair_count = 0;
};

struct LowMarginSummary {
    std::size_t pair_count = 0;
    std::array<DeckLowMarginSummary, kDeckCount> by_deck{};
    std::vector<LowMarginBestPair> pairs;
};

struct HiddenRepartitionSummary {
    bool passed = false;
    std::size_t policy_count = 0;
    std::size_t probe_count = 0;
};

struct ValueProbeDecisionDetail {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    // A deterministic deployed selector has exactly one key. Otherwise this
    // is the complete exact-score argmax set and deployment is uniform over
    // the keys, matching probe_eval semantics.
    std::vector<std::string> selected_keys;
    bool deterministic_selection = false;
    std::vector<std::string> reference_best_set;
    double regret = 0.0;
    double critic_prediction = 0.0;
    double selected_action_reference_q = 0.0;
    double critic_error = 0.0;
    // Compares the exact deployed selection distribution with legacy G0.
    bool selection_changed_from_reference = false;
    // Compares the exact deployed selection distribution with the immediately
    // preceding reported checkpoint.
    bool selection_changed_from_previous = false;
};

struct ValueCheckpointProbeReport {
    std::string name;
    std::string fingerprint;
    probe_eval::ProbeMetricSummary metrics;
    // Stable-ID order, independent of fixture or cache row order.
    std::vector<ValueProbeDecisionDetail> decisions;
};

struct ProbeScoreReport {
    ProbeCacheMetadata metadata;
    ProbeCacheStatus cache_status = ProbeCacheStatus::Loaded;
    std::filesystem::path cache_path;
    std::size_t reference_samples_per_candidate = 0;
    // The reference model owns the cached labels. The scoring model may be a
    // later immutable generation evaluated against those unchanged labels.
    std::string scoring_actor_model_fingerprint;
    // The reference Value model owns only the continuation-sensitivity
    // cross-check. A distinct scoring Value model is an evaluated candidate
    // and never affects cached Actor labels or the cross-check.
    std::string value_model_fingerprint;
    std::string scoring_value_model_fingerprint;
    std::vector<PolicyProbeReport> policies;
    // Multi-checkpoint attribution keeps the full legacy G0 policy row in
    // `policies` and reports later immutable Value checkpoints compactly.
    // G0 is first, followed by scoring candidates in caller-provided order.
    std::vector<ValueCheckpointProbeReport> value_checkpoints;
    ReferenceSensitivitySummary reference_sensitivity;
    LowMarginSummary low_margin;
    HiddenRepartitionSummary hidden_repartition;
};

// Stable FNV-1a derivation over corpus ID, probe ID, and the fixed reference
// seed. It intentionally does not depend on corpus iteration order.
std::uint64_t reference_seed_for_probe(
    std::string_view corpus_id, std::string_view stable_id,
    std::uint64_t reference_seed = kProbeReferenceSeed);

// Hashes only the information set represented by the corpus: the root hand,
// public zones/state, hidden-zone sizes, candidate schema, and declared
// deck IDs. Opponent hidden identities and library order never enter it.
std::string corpus_information_set_fingerprint(
    const std::vector<probes::DecisionProbe>& corpus);

GameState hidden_repartition_clone(
    const probes::DecisionProbe& probe);

// Converts scorer rows to descriptor-keyed samples. Priority scorer rows are
// in candidate order. Binary attack scorer rows are canonically Skip then
// Include and are explicitly remapped, so fixture candidate order is safe.
std::vector<probe_eval::CandidateSamples>
map_candidate_samples(
    const probes::DecisionProbe& probe,
    const LearnedActionSamples& action_samples);

ProbeCacheMetadata make_probe_cache_metadata(
    const ProbeScoreConfig& config,
    const std::vector<probes::DecisionProbe>& corpus,
    std::string_view reference_model_fingerprint);

void write_probe_label_cache_atomic(
    const std::filesystem::path& path,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples);

// Cache corruption or any metadata/corpus mismatch is rejected with a
// refresh instruction. Training and runtime policy code never calls this.
std::vector<probe_eval::ProbeLabel> load_probe_label_cache(
    const std::filesystem::path& path,
    const ProbeCacheMetadata& expected_metadata,
    const std::vector<probes::DecisionProbe>& corpus);

LowMarginSummary summarize_low_margin_best_pairs(
    const std::vector<probe_eval::ProbeLabel>& labels);

ProbeReferenceSamples generate_probe_reference_samples(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> actor_model,
    const ProbeScoreConfig& config);

ProbeScoreReport score_probe_dev(
    const ProbeScoreConfig& config, std::ostream& progress);

struct NamedValueScoringModel {
    std::string name;
    std::shared_ptr<const LearnedModel> model;
};

struct ProbeScoringModels {
    std::shared_ptr<const LearnedModel> reference_actor_model;
    std::shared_ptr<const LearnedModel> scoring_actor_model;
    std::string scoring_actor_name;
    std::shared_ptr<const LearnedModel> reference_value_model;
    std::string reference_value_name;
    std::vector<NamedValueScoringModel> scoring_value_models;
};

// Scores explicit immutable Actor and ordered Value candidates. Actor cache
// identity depends only on `reference_actor_model`; the reference Value is
// used for continuation-sensitivity diagnostics and as checkpoint G0.
// Scoring candidates never change or regenerate otherwise matching labels.
ProbeScoreReport score_probe_dev_with_candidates(
    const ProbeScoreConfig& config, std::ostream& progress,
    ProbeScoringModels models);

// Builds the exact deployed-selection attribution used by checkpoint reports.
// Null selected_key means uniform choice over all exact score maxima; a
// selected_key means a deterministic deployed selector.
ValueProbeDecisionDetail make_value_probe_decision_detail(
    const probe_eval::ProbeLabel& label,
    const probe_eval::ProbePrediction& prediction,
    const ValueProbeDecisionDetail* reference = nullptr,
    const ValueProbeDecisionDetail* previous = nullptr);

// Scores an immutable Actor candidate against labels owned by an immutable
// reference Actor. This is the offline generation-vs-generation path: changing
// `scoring_actor_model` never changes cache identity or regenerates labels.
ProbeScoreReport score_probe_dev_with_models(
    const ProbeScoreConfig& config, std::ostream& progress,
    std::shared_ptr<const LearnedModel> reference_actor_model,
    std::shared_ptr<const LearnedModel> scoring_actor_model,
    std::string scoring_actor_name);

std::string format_probe_score_report(
    const ProbeScoreReport& report);

} // namespace old_school::probe_runner
