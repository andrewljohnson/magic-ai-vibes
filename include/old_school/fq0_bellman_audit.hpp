#pragma once

#include "old_school/ac1_teacher_audit.hpp"
#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_bellman.hpp"
#include "old_school/fq0_dominance.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::fq0_bellman_audit {

inline constexpr std::string_view kEvidenceSchema =
    "old-school-fq0-t0-bellman-evidence-v1";
inline constexpr std::string_view kEvidencePath =
    "build/experiments/"
    "old-school-fq0-t0-bellman-v1.evidence.tsv";
inline constexpr std::string_view kModelArtifactPath =
    "build/model-cache/"
    "old-school-value-challenger-v3-c16-t800-s424242.bin";
inline constexpr std::uintmax_t kModelArtifactBytes = 3111437;
inline constexpr std::string_view kModelArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::size_t kModelTrainingGames = 800;
inline constexpr std::uint64_t kModelTrainingSeed = 424242;
inline constexpr std::size_t kModelGenerations = 16;

inline constexpr std::uint64_t kRootTransitionSeedBase =
    202607262351ULL;
inline constexpr std::uint64_t kBankASeedBase =
    202607262352ULL;
inline constexpr std::uint64_t kBankBSeedBase =
    202607262353ULL;
inline constexpr std::size_t kRootWorlds = 64;
inline constexpr std::size_t kBlocks =
    fq0_bellman::kBlockCount;
inline constexpr std::size_t kWorldsPerBlock =
    kRootWorlds / kBlocks;
inline constexpr std::size_t kPrimaryMinimumPositiveBlocks = 6;
inline constexpr std::size_t kEvaluationThreads = 4;
inline constexpr std::size_t kSuccessorWorlds = 64;
inline constexpr std::size_t kMaximumActionsApplied = 4096;
inline constexpr std::size_t kMaximumPhaseTransitions = 1024;
inline constexpr std::size_t kMaximumTurnAdvances = 64;

inline constexpr std::string_view kC16RankingSeedTag =
    "old-school-oc1-action-regression-v1.production";
inline constexpr std::uint64_t kC16RankingSeedBase =
    5787775625948253273ULL;
inline constexpr std::size_t kC16RankingWorlds = 8;
inline constexpr std::size_t kC16RankingHorizonTurns = 4;
inline constexpr std::size_t kC16RankingRolloutsPerWorld = 1;
inline constexpr bool kC16RankingBlendShallowPrior = true;
inline constexpr std::size_t kC16RankingThreads = 1;
inline constexpr double kC16RankingContinuationEpsilon = 0.0;
inline constexpr double
    kC16RankingPriorityResidualWeight = 0.0;
inline constexpr bool kC16RankingPassDominance = false;
inline constexpr LearnedContinuationController
    kC16RankingContinuationController =
        LearnedContinuationController::Legacy;

inline constexpr std::string_view kMacroPolicy =
    "ValueSearchChampion";
inline constexpr std::size_t kMacroRolloutsPerAction = 8;
inline constexpr std::size_t kMacroLearnedSearchDepth = 1;
inline constexpr double kMacroExplorationRate = 0.0;
inline constexpr double kMacroContinuationEpsilon = 0.0;
inline constexpr double kMacroPriorityResidualWeight = 0.0;
inline constexpr bool kMacroPassDominance = false;
inline constexpr LearnedContinuationController
    kMacroContinuationController =
        LearnedContinuationController::Legacy;

enum class ScopeKind : std::uint8_t {
    FullK64,
    BlockK8,
};

enum class GateRole : std::uint8_t {
    Primary,
    LiveForceGuard,
    GrowthTargetGuard,
    ProductiveCounterGuard,
    RedundantCounterGuard,
    XZeroGuard,
    DominanceConsistencyGuard,
    IncomparableControlGuard,
    Descriptive,
};

struct BitIdentityEvidence {
    std::string domain;
    std::string coordinate;
    std::string baseline_sha256;
    std::string comparison_sha256;

    bool operator==(
        const BitIdentityEvidence&) const = default;
};

struct RootTransitionParticleEvidence {
    std::size_t world_index = 0;
    std::uint64_t determinization_seed = 0;
    std::uint64_t macro_seed = 0;
    std::string redacted_result_hash;
    bool terminal = false;
    std::uint64_t terminal_root_owner_value_bits = 0;
    std::string successor_information_set_fingerprint;
    std::size_t successor_owner = 0;
    bool forced_root_action_applied = false;
    bool successful_disposition = false;
    std::size_t actions_applied = 0;
    std::size_t priority_actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;

    bool operator==(
        const RootTransitionParticleEvidence&) const = default;
};

struct LeafSampleEvidence {
    std::size_t world_index = 0;
    std::uint64_t determinization_seed = 0;
    std::uint64_t macro_seed = 0;
    std::uint64_t score_bits = 0;
    std::string redacted_leaf_hash;
    bool terminal = false;
    bool forced_action_applied = false;
    bool critic_evaluated = false;
    bool contextual_legacy_critic_bit_identical = false;
    std::uint64_t contextual_score_bits = 0;
    std::uint64_t legacy_score_bits = 0;
    std::size_t actions_applied = 0;
    std::size_t priority_actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;

    bool operator==(const LeafSampleEvidence&) const = default;
};

struct GroupActionEvidence {
    std::string descriptor;
    PriorityAction action;
    std::string feature_row_id;
    std::vector<double> policy_features;
    std::string canonical_consequence_fingerprint;
    std::vector<LeafSampleEvidence> samples;

    bool operator==(const GroupActionEvidence&) const = default;
};

struct GroupBankEvidence {
    std::string bank;
    std::string stream_key;
    std::vector<GroupActionEvidence> actions;

    bool operator==(const GroupBankEvidence&) const = default;
};

struct SuccessorGroupEvidence {
    std::string information_set_fingerprint;
    std::size_t successor_owner = 0;
    fq0_bellman::OwnerRelation relation =
        fq0_bellman::OwnerRelation::SameOwner;
    std::vector<std::size_t> root_world_indices;
    std::size_t representative_root_world = 0;
    std::string representative_root_action_descriptor;
    GroupBankEvidence bank_a;
    GroupBankEvidence bank_b;
    fq0_bellman::CrossFitValue cross_fit;
    std::vector<BitIdentityEvidence>
        representative_reconstruction_witnesses;
    BitIdentityEvidence hidden_repartition_witness;
    bool every_representative_reconstructs = false;
    bool hidden_repartition_eligible = false;
    bool hidden_identity_changed = false;
    bool hidden_repartition_invariant = false;
    bool complete = false;

    bool operator==(
        const SuccessorGroupEvidence&) const = default;
};

struct ScopeEvidence {
    ScopeKind kind = ScopeKind::FullK64;
    std::size_t block = 0;
    std::vector<std::size_t> root_world_indices;
    std::vector<fq0_bellman::TerminalParticle> terminals;
    std::vector<SuccessorGroupEvidence> groups;
    fq0_bellman::BackedTarget target;
    bool exact_particle_partition = false;
    bool complete = false;

    bool operator==(const ScopeEvidence&) const = default;
};

struct RootActionEvidence {
    std::string descriptor;
    PriorityAction action;
    std::string feature_row_id;
    fq0_bellman::TargetBlocks target;
    std::vector<double> policy_features;
    std::string canonical_consequence_fingerprint;
    std::vector<RootTransitionParticleEvidence>
        root_transitions;
    std::vector<ScopeEvidence> scopes;
    bool complete = false;

    bool operator==(const RootActionEvidence&) const = default;
};

struct RootEvidence {
    std::string stable_id;
    std::string manifest_information_action_fingerprint;
    DeckId root_deck = DeckId::Green;
    std::size_t root_player = 0;
    std::vector<RootActionEvidence> actions;
    std::vector<std::string> exact_support;
    BitIdentityEvidence hidden_repartition_witness;
    BitIdentityEvidence descriptor_order_witness;
    bool hidden_repartition_bit_identical = false;
    bool descriptor_order_bit_identical = false;
    bool complete = false;

    bool operator==(const RootEvidence&) const = default;
};

// Raw per-action successor Q evidence is carried independently of empirical
// successor mass. A successor information set present in FullK64 may be
// absent from a particular root K8 partition; all nine streams are still
// evaluated independently so no block target is synthesized or imputed.
struct SuccessorRepresentativeCoordinateEvidence {
    std::size_t root_world = 0;
    std::string root_action_descriptor;

    bool operator==(
        const SuccessorRepresentativeCoordinateEvidence&)
        const = default;
};

struct SuccessorFeatureScopeEvidence {
    ScopeKind kind = ScopeKind::FullK64;
    std::size_t block = 0;
    GroupBankEvidence bank_a;
    GroupBankEvidence bank_b;
    std::vector<SuccessorRepresentativeCoordinateEvidence>
        representative_catalog;
    std::vector<BitIdentityEvidence>
        representative_reconstruction_witnesses;
    BitIdentityEvidence hidden_repartition_witness;
    bool every_representative_reconstructs = false;
    bool hidden_repartition_eligible = false;
    bool hidden_identity_changed = false;
    bool hidden_repartition_invariant = false;
    bool complete = false;

    bool operator==(
        const SuccessorFeatureScopeEvidence&) const = default;
};

struct SuccessorFeatureEvaluationEvidence {
    std::string root_stable_id;
    std::string information_set_fingerprint;
    std::size_t successor_owner = 0;
    std::size_t representative_root_world = 0;
    std::string representative_root_action_descriptor;
    std::vector<SuccessorFeatureScopeEvidence> scopes;
    bool complete = false;

    bool operator==(
        const SuccessorFeatureEvaluationEvidence&) const = default;
};

struct C16ActionRankingEvidence {
    std::string descriptor;
    std::uint64_t score_bits = 0;

    bool operator==(
        const C16ActionRankingEvidence&) const = default;
};

struct C16RootRankingEvidence {
    std::string stable_id;
    std::vector<C16ActionRankingEvidence> actions;
    std::vector<std::string> exact_support;
    std::uint64_t pairwise_change_fraction_bits = 0;
    bool support_changed = false;

    bool operator==(
        const C16RootRankingEvidence&) const = default;
};

struct RankingDeckSummaryEvidence {
    DeckId deck = DeckId::Green;
    std::size_t roots = 0;
    std::size_t support_changed_roots = 0;
    std::uint64_t mean_pairwise_change_fraction_bits = 0;
    std::uint64_t support_changed_fraction_bits = 0;

    bool operator==(
        const RankingDeckSummaryEvidence&) const = default;
};

struct RankingSummaryEvidence {
    std::vector<C16RootRankingEvidence> roots;
    std::array<RankingDeckSummaryEvidence, kDeckCount> decks;
    std::uint64_t equal_deck_pairwise_change_fraction_bits = 0;
    std::uint64_t equal_deck_support_changed_fraction_bits = 0;
    bool complete = false;

    bool operator==(
        const RankingSummaryEvidence&) const = default;
};

struct ContrastEvidence {
    GateRole role = GateRole::Descriptive;
    std::string name;
    std::string stable_id;
    std::string positive_descriptor;
    std::string negative_descriptor;
    fq0_bellman::BlockContrast contrast;
    bool support_condition = true;
    bool directional_passed = false;

    bool operator==(const ContrastEvidence&) const = default;
};

struct DominanceWorldEvidence {
    std::size_t world_index = 0;
    std::uint64_t determinization_seed = 0;
    std::string common_world_key;
    fq0_dominance::Comparison comparison;
    fq0_dominance::Orientation orientation =
        fq0_dominance::Orientation::Incomparable;
    BitIdentityEvidence hidden_repartition_witness;
    bool hidden_repartition_bit_identical = false;

    bool operator==(const DominanceWorldEvidence&) const = default;
};

struct DominancePairEvidence {
    GateRole role = GateRole::Descriptive;
    std::string stable_id;
    std::string first_descriptor;
    std::string second_descriptor;
    fq0_dominance::Orientation required_orientation =
        fq0_dominance::Orientation::Incomparable;
    std::vector<DominanceWorldEvidence> worlds;
    std::size_t required_worlds = 0;
    std::size_t matching_worlds = 0;
    bool passed = false;

    bool operator==(const DominancePairEvidence&) const = default;
};

struct InvarianceEvidence {
    BitIdentityEvidence independent_manifest_witness;
    BitIdentityEvidence repeated_construction_witness;
    BitIdentityEvidence descriptor_order_witness;
    BitIdentityEvidence thread_count_witness;
    BitIdentityEvidence hidden_repartition_witness;
    BitIdentityEvidence contextual_legacy_critic_witness;
    bool independent_manifest_bit_identical = false;
    bool repeated_construction_bit_identical = false;
    bool descriptor_order_bit_identical = false;
    bool thread_count_bit_identical = false;
    bool hidden_repartition_bit_identical = false;
    bool contextual_legacy_critic_bit_identical = false;
    bool passed = false;

    bool operator==(const InvarianceEvidence&) const = default;
};

struct ScientificEvidence {
    ac1_teacher_audit::Manifest manifest;
    std::string model_fingerprint;
    std::string primary_core_sha256;
    std::vector<RootEvidence> roots;
    std::vector<SuccessorFeatureEvaluationEvidence>
        successor_feature_evaluations;
    std::vector<ContrastEvidence> contrasts;
    std::vector<DominancePairEvidence> dominance_pairs;
    std::vector<fq0_bellman::FeatureTargetRow> feature_rows;
    fq0_bellman::FeatureCollisionAnalysis feature_collisions;
    RankingSummaryEvidence c16_ranking_changes;
    InvarianceEvidence invariance;
    std::array<std::size_t, kDeckCount> roots_by_deck{};
    bool primary_passed = false;
    bool reject_only_guards_passed = false;
    bool complete = false;
    bool passed = false;

    bool operator==(const ScientificEvidence&) const = default;
};

struct IntegrityEvidence {
    artifact_integrity::RegularFileSnapshot model_before;
    artifact_integrity::RegularFileSnapshot model_after;
    LearnedModelComponentFingerprints
        model_components_before;
    LearnedModelComponentFingerprints
        model_components_after;
    bool artifact_requirement_matched = false;
    bool artifact_unchanged = false;
    bool model_identity_matched = false;
    bool passed = false;

    bool operator==(const IntegrityEvidence&) const = default;
};

struct GateReport {
    bool integrity_passed = false;
    bool complete_evidence = false;
    bool scientific_passed = false;
    bool infrastructure_failure = false;
    bool passed = false;
    std::vector<std::string> failures;

    bool operator==(const GateReport&) const = default;
};

struct EvidenceBundle {
    std::string bytes;
    std::string payload_sha256;
    // SHA-256 of every byte through the payload_sha256 row. The final
    // complete_sha256 row is deliberately excluded to avoid a self-
    // referential digest. EvidencePublication::sha256 covers the exact
    // complete file, including this footer.
    std::string complete_sha256;
    std::vector<std::string> section_names;
    std::vector<std::string> section_sha256;

    bool operator==(const EvidenceBundle&) const = default;
};

struct EvidencePublication {
    std::string path;
    std::uintmax_t byte_size = 0;
    std::string sha256;
    std::string payload_sha256;
    bool atomic_no_replace = false;
    bool published = false;

    bool operator==(const EvidencePublication&) const = default;
};

struct RunReport {
    ScientificEvidence scientific;
    IntegrityEvidence integrity;
    GateReport gate;
    EvidencePublication publication;
};

// Canonical, witness-free payload digests used by the assembly layer and
// independently rederived by semantic validation. Comparison digests may
// come from an alternate construction, but baseline digests must be these
// exact values for the supplied primary evidence.
namespace binding {

std::string root_feature_information_set_id(
    std::string_view root_stable_id,
    std::string_view manifest_information_action_fingerprint);
std::string successor_feature_information_set_id(
    std::string_view root_stable_id,
    std::string_view successor_information_set_fingerprint);
std::string dominance_common_world_key(
    std::string_view root_stable_id,
    std::string_view manifest_information_action_fingerprint,
    std::size_t world);
std::string hidden_repartition_coordinate(
    std::string_view parent_coordinate,
    std::size_t representative_root_world,
    std::string_view representative_root_action_descriptor);

std::string manifest_payload_sha256(
    const ac1_teacher_audit::Manifest& manifest);
std::string root_payload_sha256(const RootEvidence& root);
// Exact full v1 digest of the raw successor A/B banks and recomputed
// cross-fit. This remains consequence-bound for canonical/copy integrity.
std::string successor_bank_pair_payload_sha256(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b,
    const fq0_bellman::CrossFitValue& cross_fit);
// Exact v2 operator digest used by fq0_bellman_science reconstruction
// witnesses. It omits only the canonical representative's descriptive,
// representative-local consequence fingerprint.
std::string successor_operator_bank_pair_payload_sha256(
    const GroupBankEvidence& bank_a,
    const GroupBankEvidence& bank_b,
    const fq0_bellman::CrossFitValue& cross_fit);
std::string group_bank_pair_payload_sha256(
    std::string_view root_stable_id,
    std::string_view root_action_descriptor,
    ScopeKind kind, std::size_t block,
    const SuccessorGroupEvidence& group);
std::string group_representative_payload_sha256(
    std::string_view root_stable_id,
    std::string_view root_action_descriptor,
    ScopeKind kind, std::size_t block,
    const SuccessorGroupEvidence& group,
    std::size_t member_root_world);
std::string successor_feature_scope_payload_sha256(
    const SuccessorFeatureEvaluationEvidence& evaluation,
    const SuccessorFeatureScopeEvidence& scope);
std::string
successor_feature_scope_representative_payload_sha256(
    const SuccessorFeatureEvaluationEvidence& evaluation,
    const SuccessorFeatureScopeEvidence& scope,
    const SuccessorRepresentativeCoordinateEvidence&
        representative);
std::string dominance_comparison_payload_sha256(
    std::string_view root_stable_id,
    std::string_view first_descriptor,
    std::string_view second_descriptor,
    const DominanceWorldEvidence& world);
// Hidden-repartition witness digest over exactly the fields consumed or
// produced by the dominance operator. Unlike the full v1 comparison payload,
// this omits only the two absolute owner-observable consequence byte strings;
// their equality relation remains bound.
std::string dominance_operator_payload_sha256(
    std::string_view root_stable_id,
    std::string_view first_descriptor,
    std::string_view second_descriptor,
    std::size_t actor,
    const DominanceWorldEvidence& world);
std::string scientific_core_payload_sha256(
    const ScientificEvidence& scientific);
std::array<std::string, 2> critic_stream_payload_sha256(
    const ScientificEvidence& scientific);

BitIdentityEvidence make_witness(
    std::string domain, std::string coordinate,
    std::string baseline_sha256,
    std::string comparison_sha256);

} // namespace binding

GateReport evaluate_gate(
    const ScientificEvidence& scientific,
    const IntegrityEvidence& integrity);
int exit_code(const GateReport& gate);

// Semantically validates and serializes `report`, then publishes only at the
// frozen path with atomic no-replace semantics. The frozen model is
// re-snapshotted immediately before the publication commit.
EvidencePublication publish_evidence_atomic_no_replace(
    const RunReport& report);

RunReport run(std::ostream& progress);
int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

namespace testing {

EvidenceBundle serialize_evidence_bundle(
    const RunReport& report);

EvidenceBundle validate_evidence_bundle(
    std::string_view bytes);

void write_evidence_atomic_no_replace(
    std::string_view path, std::string_view bytes);

// Directly exercises the global collision detector used by every scientific
// seed domain. Exact coordinate reuse is allowed; one numeric seed owned by
// two distinct semantic coordinates fails closed.
void validate_seed_coordinate_ownership(
    const std::vector<std::pair<std::uint64_t, std::string>>&
        claims);

// Exercises the production precommit parent re-snapshot and no-replace
// mechanics without weakening the fixed-path semantic publication API.
EvidencePublication publish_evidence_for_parent(
    std::string_view path, const EvidenceBundle& bundle,
    const artifact_integrity::RegularFileSnapshot& expected_model,
    std::string_view expected_model_fingerprint,
    std::string_view observed_model_fingerprint);

} // namespace testing

} // namespace old_school::fq0_bellman_audit
