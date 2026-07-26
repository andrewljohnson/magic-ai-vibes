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
    "old-school-probe-label-cache-v3";
inline constexpr std::string_view kProbeReferenceAlgorithm =
    "actor-mirror-common-world-v3";
inline constexpr std::string_view kProbeSemanticRevision =
    "old-school-probe-score-semantics-v3";
// A rules correction changes continuation outcomes even when a probe's
// visible position is unchanged. Persist that boundary explicitly instead of
// relying on an incidental model or corpus fingerprint mismatch.
inline constexpr std::string_view kProbeEnvironmentRevision =
    "old-school-environment-v3-cleanup-discard";
inline constexpr std::string_view kProbeValidationCacheSchema =
    "old-school-probe-validation-label-cache-v2";
inline constexpr std::string_view kProbeValidationSemanticRevision =
    "old-school-probe-validation-score-semantics-v2";
inline constexpr std::uint64_t kProbeReferenceSeed =
    0x50524F4245524546ULL;
inline constexpr std::uint64_t kProbeProductionPolicySeed =
    0x50524F44504F4C59ULL;

enum class ProbeCorpusKind : std::uint8_t {
    DevV3,
    ValidationV1,
};

struct ProbeScoreConfig {
    std::size_t training_games = 800;
    std::uint64_t training_seed = kDefaultLearnedTrainingSeed;
    std::size_t reference_worlds = 128;
    std::size_t reference_horizon_turns = 12;
    std::size_t reference_rollouts_per_world = 1;
    // Information-set worlds used to score each Value policy. This is
    // deliberately excluded from Actor-owned cache metadata: changing a
    // candidate's deployed search width must never relabel the reference.
    std::size_t scoring_value_worlds = 2;
    // Research-only Value continuation epsilon. Like scoring width, this
    // changes only scoring policies and is deliberately excluded from
    // Actor-owned cache metadata and serialization.
    double scoring_value_continuation_epsilon = 0.0;
    std::filesystem::path cache_path =
        "data/old-school-probe-dev-v3-env-v3.labels.tsv";
    bool refresh_cache = false;
};

struct ProbeCacheMetadata {
    std::string schema;
    std::string algorithm;
    std::string semantic_revision;
    std::string environment_revision;
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
    // True when deployment eligibility changed the ranking scores while raw
    // finite candidate Q estimates were retained in the deployment
    // diagnostic. Such a row must never be described as candidate-Q fit.
    bool policy_scores_adjusted_for_deployment = false;
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
    // Empty for the G0 baseline. Every other row names the actual checkpoint
    // used for its adjacent-transition comparison; this need not be the
    // immediately preceding displayed row when families are independent.
    std::string transition_parent_name;
    // Zero is the legacy deployed Value selector. Nonzero candidates add the
    // bounded, learned Priority-head residual after the unchanged Value score.
    double value_priority_residual_weight = 0.0;
    bool value_pass_dominance = false;
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;
    bool policy_scores_adjusted_for_deployment = false;
    probe_eval::ProbeMetricSummary metrics;
    // Stable-ID order, independent of fixture or cache row order.
    std::vector<ValueProbeDecisionDetail> decisions;
};

struct CandidatePairEstimate {
    std::string name;
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    std::string first_key;
    std::string second_key;
    std::size_t samples_per_candidate = 0;
    double delta_q = 0.0;
    double paired_standard_error = 0.0;
    double confidence_lower_95 = 0.0;
    double confidence_upper_95 = 0.0;

    bool operator==(const CandidatePairEstimate&) const = default;
};

// Extracts and explicitly orients a cached common-world pair as
// Q(first)-Q(second), regardless of the cache's canonical candidate order.
CandidatePairEstimate make_candidate_pair_estimate(
    const probe_eval::ProbeLabel& label, std::string name,
    std::string_view first_key, std::string_view second_key);

struct ForceSpikeControlDecision {
    std::string stable_id;
    double pass_score = 0.0;
    double force_spike_score = 0.0;
    // Priority deployment samples uniformly from all exact-score maxima.
    // Keeping the complete set makes a tie visible instead of accidentally
    // blessing one possible random draw.
    std::vector<std::string> selected_keys;

    bool operator==(const ForceSpikeControlDecision&) const = default;
};

struct ForceSpikePolicyControlReport {
    std::string policy_name;
    std::string model_fingerprint;
    std::size_t worlds = 0;
    std::size_t horizon_turns = 0;
    ForceSpikeControlDecision live;
    ForceSpikeControlDecision payable;
    bool hidden_repartition_passed = false;
    double value_priority_residual_weight = 0.0;
    bool value_pass_dominance = false;
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;
    bool policy_scores_adjusted_for_deployment = false;

    bool live_selects_force_spike() const;
    bool payable_selects_pass() const;
    bool gate_passed() const;

    bool operator==(const ForceSpikePolicyControlReport&) const =
        default;
};

inline constexpr std::size_t kTeacherAuditBlockWorlds = 8;

struct OrderedPairBlockSummary {
    std::size_t worlds_per_block = kTeacherAuditBlockWorlds;
    std::size_t block_count = 0;
    std::size_t correct_block_count = 0;

    // The preregistered practical-teacher gate is at least three quarters of
    // the ordered K=8 blocks. Exact ties are counted as incorrect.
    std::size_t required_correct_block_count() const;
    bool gate_passed() const;

    bool operator==(const OrderedPairBlockSummary&) const = default;
};

// Summarizes paired, world-major Q samples as disjoint ordered blocks. A block
// is correct only when mean(first) is strictly greater than mean(second).
OrderedPairBlockSummary summarize_ordered_pair_blocks(
    const std::vector<double>& first,
    const std::vector<double>& second,
    std::size_t worlds_per_block = kTeacherAuditBlockWorlds);

struct TeacherOptionComparison {
    std::string description;
    CandidatePairEstimate estimate;
    // Complete exact-score argmax set across every legal action in the
    // fixture. This deliberately exposes ties instead of sampling one key.
    std::vector<std::string> selected_keys;
    OrderedPairBlockSummary ordered_blocks;
    bool hidden_repartition_bit_identical = false;
    // Full accounting is retained even for the historical shallow audit.
    // A terminal-credit audit additionally requires every candidate sample
    // to correspond one-for-one with a terminal rollout.
    std::size_t candidate_count = 0;
    std::size_t recorded_candidate_samples = 0;
    std::size_t expected_evaluations = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    // Empty-library termination is conservatively guaranteed when the
    // configured horizon covers the sum of both libraries plus one turn.
    std::size_t conservative_terminal_bound_turns = 0;
    bool conservative_terminal_bound_satisfied = false;

    bool confidence_gate_passed() const;
    bool block_gate_passed() const;
    bool gate_passed() const;
    bool evaluation_accounting_is_exact() const;
    bool terminal_results_gate_passed() const;
    bool second_key_excluded_from_selected_set() const;

    bool operator==(const TeacherOptionComparison&) const = default;
};

struct TeacherSufficiencyAuditConfig {
    std::size_t worlds = 256;
    std::size_t horizon_turns = 4;
    LearnedVariant continuation_variant =
        LearnedVariant::ValueSearchChampion;
    bool blend_shallow_prior = false;
    // Requires a horizon that conservatively reaches a terminal result for
    // every fixture and enables fail-closed terminal-result accounting.
    bool require_terminal_results = false;
    // Passed only to the evaluation sampler. One is the canonical serial
    // path; higher values preserve the exact preindexed sample matrix.
    std::size_t evaluation_threads = 1;

    bool operator==(const TeacherSufficiencyAuditConfig&) const =
        default;
};

struct TeacherSufficiencyAuditReport {
    std::string policy_name;
    std::string model_fingerprint;
    TeacherSufficiencyAuditConfig config;
    TeacherOptionComparison force_spike_live;
    TeacherOptionComparison force_spike_payable;
    TeacherOptionComparison disintegrate_x_zero;
    bool hidden_repartition_bit_identical = false;

    bool gate_passed() const;

    bool operator==(const TeacherSufficiencyAuditReport&) const =
        default;
};

struct ProbeScoreReport {
    ProbeCorpusKind corpus_kind = ProbeCorpusKind::DevV3;
    // Neither the small development corpus nor the focused harvested
    // validation corpus can promote a policy.
    bool promotion_eligible = false;
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
    // Focused cached Actor-reference comparisons. Validation-v1 reports the
    // immutable teacher's Q(Pass)-Q(X=0) here. These values do not measure
    // any scoring Value candidate.
    std::vector<CandidatePairEstimate> candidate_pairs;
    // The same focused comparisons independently re-estimated from each
    // Value policy's own common-world Value-mirror search samples. G0 is
    // always first, followed by every scoring Value model in caller order.
    std::vector<CandidatePairEstimate> value_candidate_pairs;
    // Supplemental paired behavioral controls for deployed Value search.
    // They are deliberately excluded from cached labels, deck-balanced
    // metrics, cache identity, and promotion claims.
    std::vector<ForceSpikePolicyControlReport>
        force_spike_controls;
    HiddenRepartitionSummary hidden_repartition;
};

std::filesystem::path default_probe_cache_path(
    ProbeCorpusKind corpus_kind);

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

std::string corpus_information_set_fingerprint(
    ProbeCorpusKind corpus_kind,
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

ProbeCacheMetadata make_probe_cache_metadata(
    ProbeCorpusKind corpus_kind, const ProbeScoreConfig& config,
    const std::vector<probes::DecisionProbe>& corpus,
    std::string_view reference_model_fingerprint);

void write_probe_label_cache_atomic(
    const std::filesystem::path& path,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples);

void write_probe_label_cache_atomic(
    ProbeCorpusKind corpus_kind, const std::filesystem::path& path,
    const ProbeCacheMetadata& metadata,
    const std::vector<probes::DecisionProbe>& corpus,
    const std::vector<ProbeReferenceSamples>& samples);

// Cache corruption or any metadata/corpus mismatch is rejected with a
// refresh instruction. Training and runtime policy code never calls this.
std::vector<probe_eval::ProbeLabel> load_probe_label_cache(
    const std::filesystem::path& path,
    const ProbeCacheMetadata& expected_metadata,
    const std::vector<probes::DecisionProbe>& corpus);

std::vector<probe_eval::ProbeLabel> load_probe_label_cache(
    ProbeCorpusKind corpus_kind, const std::filesystem::path& path,
    const ProbeCacheMetadata& expected_metadata,
    const std::vector<probes::DecisionProbe>& corpus);

LowMarginSummary summarize_low_margin_best_pairs(
    const std::vector<probe_eval::ProbeLabel>& labels);

ProbeReferenceSamples generate_probe_reference_samples(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> actor_model,
    const ProbeScoreConfig& config);

ProbeReferenceSamples generate_probe_reference_samples(
    ProbeCorpusKind corpus_kind,
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> actor_model,
    const ProbeScoreConfig& config);

ProbeScoreReport score_probe_dev(
    const ProbeScoreConfig& config, std::ostream& progress);

struct NamedValueScoringModel {
    std::string name;
    std::shared_ptr<const LearnedModel> model;
    // Checkpoints sharing a nonempty family form one ordered transition
    // ladder. The first row in each family compares directly with G0.
    // An empty family is a standalone candidate compared directly with G0.
    std::string transition_family;
    // Zero preserves legacy deployed Value scoring bit-for-bit.
    double value_priority_residual_weight = 0.0;
    // Default-off deployment metadata. PD0 applies at the probe root and in
    // Value-mirror continuations. The controller applies only inside K-search
    // continuations, matching Learned Value deployment.
    bool value_pass_dominance = false;
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;
};

inline constexpr std::size_t kFieldReferenceWorlds = 64;
inline constexpr std::size_t kFieldReferenceHorizonTurns = 8;
inline constexpr std::size_t kFieldDeploymentWorlds = 8;
inline constexpr std::size_t kFieldDeploymentHorizonTurns = 4;

enum class FieldRegressionScoreKind : std::uint8_t {
    DeployedPrioritySearch,
    ImmediateCombat,
};

struct FieldRegressionEvaluationAccounting {
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;

    bool operator==(
        const FieldRegressionEvaluationAccounting&) const = default;
};

struct FieldRegressionForcedConsequence {
    std::string descriptor;
    // FNV-1a over the observer's post-branch information set: every public
    // zone and counter, the observer's hand, and hidden-zone sizes.
    std::string public_state_fingerprint;

    bool operator==(
        const FieldRegressionForcedConsequence&) const = default;
};

struct FieldRegressionPolicyDecision {
    std::string name;
    std::string fingerprint;
    FieldRegressionScoreKind score_kind =
        FieldRegressionScoreKind::DeployedPrioritySearch;
    std::size_t deployment_worlds = kFieldDeploymentWorlds;
    std::size_t deployment_horizon_turns =
        kFieldDeploymentHorizonTurns;
    bool blend_shallow_prior = true;
    double value_continuation_epsilon = 0.0;
    double value_priority_residual_weight = 0.0;
    bool value_pass_dominance = false;
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;
    // Present for Priority search and empty for the immediate combat
    // selector. Rows remain descriptor-keyed in frozen fixture order.
    std::vector<probe_eval::CandidateSamples> samples;
    FieldRegressionEvaluationAccounting accounting;
    std::vector<probe_eval::PolicyScore> scores;
    std::vector<std::string> selected_keys;
    // Attack and Block use the deployed selector's exact first-on-tie
    // candidate. Priority exposes its complete exact-score argmax set.
    bool deterministic_selection = false;
    bool policy_scores_adjusted_for_deployment = false;

};

struct FieldRegressionDecisionReport {
    std::string stable_id;
    DeckId root_deck = DeckId::Green;
    probes::DecisionKind decision_kind =
        probes::DecisionKind::Priority;
    std::vector<std::string> candidate_descriptors;
    // The only reference label source: frozen parent Value mirror,
    // K64/H8, one rollout/world, unblended, Legacy, and PD0 off.
    std::vector<probe_eval::CandidateSamples> reference_samples;
    FieldRegressionEvaluationAccounting reference_accounting;
    std::vector<FieldRegressionForcedConsequence>
        forced_consequences;
    // All three views reproduce deployment: Priority is K8/H4 with its
    // serialized metadata, while Attack and Block use the immediate
    // production selector.
    FieldRegressionPolicyDecision parent;
    FieldRegressionPolicyDecision control;
    FieldRegressionPolicyDecision treatment;

};

struct FieldRegressionReport {
    std::string corpus_id;
    std::string reference_model_fingerprint;
    std::size_t reference_worlds = kFieldReferenceWorlds;
    std::size_t reference_horizon_turns =
        kFieldReferenceHorizonTurns;
    std::size_t reference_rollouts_per_world = 1;
    bool reference_blend_shallow_prior = false;
    double reference_value_continuation_epsilon = 0.0;
    double reference_value_priority_residual_weight = 0.0;
    bool reference_value_pass_dominance = false;
    LearnedContinuationController
        reference_value_continuation_controller =
            LearnedContinuationController::Legacy;
    HiddenRepartitionSummary hidden_repartition;
    bool rules_contract_passed = false;
    std::vector<FieldRegressionDecisionReport> decisions;

};

// Separate field-only mapper. Generic DevV3 mapping deliberately remains
// Block-closed. Priority rows are caller order; Attack rows are Skip/Include;
// Block rows are No Block/Block. Every binary action mapping is validated
// before any row is associated with a descriptor.
std::vector<probe_eval::CandidateSamples>
map_field_candidate_samples(
    const probes::DecisionProbe& probe,
    const LearnedActionSamples& action_samples);

// Cache-, artifact-, trainer-, and label-free scoring of the exact six
// field-regressions-v1 fixtures. Model policy metadata is fail-closed:
// parent/control are residual-zero, Legacy, PD0-off; treatment is
// residual-zero, PublicStackPassV1, PD0-on. The reference is always the
// parent and is never replaced by either paired arm.
FieldRegressionReport score_field_regressions_v1(
    const NamedValueScoringModel& parent,
    const NamedValueScoringModel& control,
    const NamedValueScoringModel& treatment);

struct AttackRegressionPolicyReport {
    FieldRegressionPolicyDecision deployment;
    bool selects_reference_best = false;
    double regret = 0.0;
};

struct AttackRegressionReport {
    std::string corpus_id;
    std::string stable_id;
    DeckId root_deck = DeckId::RUAggro;
    std::vector<std::string> candidate_descriptors;
    std::vector<probe_eval::CandidateSamples> reference_samples;
    FieldRegressionEvaluationAccounting reference_accounting;
    probe_eval::ProbeLabel reference_label;
    std::vector<FieldRegressionForcedConsequence>
        forced_consequences;
    HiddenRepartitionSummary hidden_repartition;
    bool rules_contract_passed = false;
    AttackRegressionPolicyReport parent;
    AttackRegressionPolicyReport candidate;
};

// Post-C17, cache- and trainer-free Attack diagnostic. The immutable parent
// supplies the hidden-information-safe K64/H8 common-world reference. Both
// parent and candidate are scored through the production immediate Value
// attack-set selector; no Handcrafted score, combat heuristic, or card-name
// policy rule enters the result.
AttackRegressionReport score_attack_regression_v1(
    const NamedValueScoringModel& parent,
    const NamedValueScoringModel& candidate);

// Candidate-only, cache-free comparison against an already-loaded frozen
// label set. The control is the transition parent for the treatment. This
// path constructs the named frozen corpus internally and has no Actor,
// reference-Value, cache-path, refresh, or training input.
struct ValueProbePairAgainstLabelsReport {
    ProbeCorpusKind corpus_kind = ProbeCorpusKind::DevV3;
    ValueCheckpointProbeReport control;
    ValueCheckpointProbeReport treatment;
    HiddenRepartitionSummary hidden_repartition;
};

struct ValueProbeDeploymentDiagnostic {
    std::string stable_id;
    std::vector<probe_eval::PolicyScore> raw_candidate_q;
    std::vector<probe_eval::PolicyScore> deployed_policy_scores;
    std::vector<std::string> pass_dominated_keys;
    std::vector<std::string> selected_keys;
    bool policy_scores_adjusted_for_deployment = false;
    bool candidate_q_fit_eligible = true;
    bool value_pass_dominance = false;
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy;
};

struct ProbeScoringModels {
    std::shared_ptr<const LearnedModel> reference_actor_model;
    std::shared_ptr<const LearnedModel> scoring_actor_model;
    std::string scoring_actor_name;
    std::shared_ptr<const LearnedModel> reference_value_model;
    std::string reference_value_name;
    std::vector<NamedValueScoringModel> scoring_value_models;
};

// Cache-free, label-free invariance audit for immutable Value candidates.
// It scores the deployed selector on the requested corpus and an
// information-equivalent hidden-zone repartition using identical semantic
// seeds, then requires every critic value, action score, and selected-key set
// to be bit-identical. This makes no agreement or regret claim.
HiddenRepartitionSummary verify_value_hidden_repartition(
    ProbeCorpusKind corpus_kind,
    const std::vector<NamedValueScoringModel>& models,
    std::size_t scoring_value_worlds = 8,
    double value_continuation_epsilon = 0.0);

// Sealed Dev-v3 scorer for one immutable control/treatment pair. Labels must
// exactly cover the internally constructed corpus by stable ID, deck, and
// candidate descriptor. It never reads, writes, or regenerates a cache.
// Block decisions are deliberately unsupported.
ValueProbePairAgainstLabelsReport
score_value_probe_pair_against_labels(
    ProbeCorpusKind corpus_kind,
    const std::vector<probe_eval::ProbeLabel>& labels,
    const NamedValueScoringModel& control,
    const NamedValueScoringModel& treatment,
    std::size_t scoring_value_worlds = 8,
    double value_continuation_epsilon = 0.0);

// Cache-free single-position view of exact Value deployment semantics. Raw Q
// remains finite for every explicit candidate; when PD0 filters root actions,
// deployed_policy_scores is a finite ranking view that excludes them.
ValueProbeDeploymentDiagnostic diagnose_value_probe_deployment(
    const probes::DecisionProbe& probe,
    const NamedValueScoringModel& scoring,
    std::string_view corpus_id, std::size_t worlds = 8,
    double value_continuation_epsilon = 0.0);

// Scores explicit immutable Actor and ordered Value candidates. Actor cache
// identity depends only on `reference_actor_model`; the reference Value is
// used for continuation-sensitivity diagnostics and as checkpoint G0.
// Scoring candidates never change or regenerate otherwise matching labels.
ProbeScoreReport score_probe_dev_with_candidates(
    const ProbeScoreConfig& config, std::ostream& progress,
    ProbeScoringModels models);

// Generic eval-only scorer. Dev-v3 and validation-v1 retain separate corpus,
// schema, seed, magic-header, fingerprint, and default-path identities.
// Validation-v1 is a focused behavioral regression corpus and can never
// establish policy promotion. Callers selecting validation-v1 must use
// default_probe_cache_path(ValidationV1) or an explicit distinct path.
ProbeScoreReport score_probe_corpus_with_candidates(
    ProbeCorpusKind corpus_kind, const ProbeScoreConfig& config,
    std::ostream& progress, ProbeScoringModels models);

// Scores the paired live/payable Force Spike controls through the actual
// deployed Value priority path (K worlds, H=4, aggregate shallow-prior
// blend), including a bit-identical hidden-repartition check. This is a
// reject-only behavioral diagnostic, not a balanced policy metric.
ForceSpikePolicyControlReport
score_value_force_spike_policy_controls(
    std::shared_ptr<const LearnedModel> model,
    std::string policy_name, std::size_t worlds = 8,
    double value_continuation_epsilon = 0.0,
    double value_priority_residual_weight = 0.0,
    bool value_pass_dominance = false,
    LearnedContinuationController value_continuation_controller =
        LearnedContinuationController::Legacy);

// Eval-only P16 prerequisite. Scores the existing Force Spike live/payable
// controls and validation-v1 Pass/X=0 decision with a caller-supplied frozen
// model and search semantics. It neither reads nor writes probe-label caches.
TeacherSufficiencyAuditReport score_teacher_sufficiency_audit(
    std::shared_ptr<const LearnedModel> model,
    std::string policy_name,
    TeacherSufficiencyAuditConfig config = {});

// The first row is treated as the preregistered primary teacher; later rows
// are explicitly diagnostic controls and cannot substitute for it.
std::string format_teacher_sufficiency_audit_report(
    const std::vector<TeacherSufficiencyAuditReport>& reports);

// The full-terminal credit hypothesis concerns only the controlled Force
// Spike ordering flip. The X=0 row remains a diagnostic and cannot make this
// primary verdict pass or fail.
bool terminal_credit_primary_gate_passed(
    const TeacherSufficiencyAuditReport& report);

std::string format_terminal_credit_audit_report(
    const TeacherSufficiencyAuditReport& report);

// Builds the exact deployed-selection attribution used by checkpoint reports.
// Null selected_key means uniform choice over all exact score maxima; a
// selected_key means a deterministic deployed selector.
ValueProbeDecisionDetail make_value_probe_decision_detail(
    const probe_eval::ProbeLabel& label,
    const probe_eval::ProbePrediction& prediction,
    const ValueProbeDecisionDetail* reference = nullptr,
    const ValueProbeDecisionDetail* previous = nullptr);

// Priority decisions represent deployment over the complete exact-score
// argmax set and may leave deterministic_selection false. A singleton set is
// nevertheless a unique deployed choice. This helper intentionally keys only
// on that semantic set, so evaluation gates do not confuse selector metadata
// with uniqueness.
bool value_decision_uniquely_selects(
    const ValueProbeDecisionDetail& decision,
    std::string_view candidate_key);

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
