#pragma once

#include "old_school/ac1_teacher_audit.hpp"
#include "old_school/fq0_bellman.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq0_bellman_science {

inline constexpr std::uint64_t kProductionRootSeedBase =
    202607262351ULL;
inline constexpr std::uint64_t kProductionBankASeedBase =
    202607262352ULL;
inline constexpr std::uint64_t kProductionBankBSeedBase =
    202607262353ULL;
inline constexpr std::size_t kProductionRootWorlds = 64;
inline constexpr std::size_t kProductionSuccessorWorlds = 64;
inline constexpr std::size_t kProductionWorkers = 4;
inline constexpr std::string_view kProductionModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

enum class ScopeKind : std::uint8_t {
    Full,
    Block,
};

struct ExecutionMetadata {
    std::size_t workers_requested = 0;
    std::size_t maximum_workers_started = 0;
    std::size_t parallel_batches = 0;
    std::size_t indexed_tasks = 0;

    bool operator==(const ExecutionMetadata&) const = default;
};

struct RootSampledWorld {
    std::size_t world_index = 0;
    std::uint64_t determinization_seed = 0;
    GameState state;

    bool operator==(const RootSampledWorld&) const = default;
};

struct RootTransition {
    std::size_t world_index = 0;
    std::uint64_t determinization_seed = 0;
    std::uint64_t macro_seed = 0;
    std::string redacted_result_hash;
    bool terminal = false;
    double terminal_root_owner_value = 0.0;
    std::string successor_information_set_fingerprint;
    std::size_t successor_owner = 0;
    GameState successor_state;
    LearnedDecisionContext successor_context;
    std::vector<PriorityAction> successor_legal_actions;
    std::size_t actions_applied = 0;
    std::size_t priority_actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;
    bool forced_action_applied = false;

    bool operator==(const RootTransition&) const = default;
};

struct LeafSample {
    std::size_t world_index = 0;
    std::uint64_t determinization_seed = 0;
    std::uint64_t macro_seed = 0;
    double score = 0.0;
    std::uint64_t contextual_score_bits = 0;
    std::uint64_t legacy_score_bits = 0;
    std::string redacted_leaf_hash;
    bool terminal = false;
    bool critic_evaluated = false;
    bool contextual_legacy_critic_bit_identical = false;
    std::size_t actions_applied = 0;
    std::size_t priority_actions_applied = 0;
    std::size_t phase_transitions = 0;
    std::size_t turn_advances = 0;
    bool forced_action_applied = false;

    bool operator==(const LeafSample&) const = default;
};

struct GroupAction {
    std::string descriptor;
    PriorityAction action;
    std::string feature_row_id;
    std::vector<double> policy_features;
    std::string canonical_consequence_fingerprint;
    std::vector<LeafSample> samples;

    bool operator==(const GroupAction&) const = default;
};

struct GroupBank {
    std::string bank;
    std::string stream_key;
    std::vector<GroupAction> actions;

    bool operator==(const GroupBank&) const = default;
};

struct SuccessorGroup {
    std::string information_set_fingerprint;
    std::size_t successor_owner = 0;
    fq0_bellman::OwnerRelation relation =
        fq0_bellman::OwnerRelation::SameOwner;
    std::vector<std::size_t> root_world_indices;
    std::size_t representative_root_world = 0;
    std::string representative_root_action_descriptor;
    GroupBank bank_a;
    GroupBank bank_b;
    fq0_bellman::CrossFitValue cross_fit;

    bool operator==(const SuccessorGroup&) const = default;
};

struct Scope {
    ScopeKind kind = ScopeKind::Full;
    std::size_t block = 0;
    std::vector<std::size_t> root_world_indices;
    std::vector<fq0_bellman::TerminalParticle> terminals;
    std::vector<SuccessorGroup> groups;
    fq0_bellman::BackedTarget target;
    bool exact_particle_partition = false;

    bool operator==(const Scope&) const = default;
};

struct RootAction {
    std::string descriptor;
    PriorityAction action;
    std::string feature_row_id;
    fq0_bellman::TargetBlocks target;
    std::vector<double> policy_features;
    std::string canonical_consequence_fingerprint;
    std::vector<RootTransition> root_transitions;
    std::vector<Scope> scopes;

    bool operator==(const RootAction&) const = default;
};

struct Root {
    std::string stable_id;
    std::string manifest_information_action_fingerprint;
    std::string canonical_information_set_fingerprint;
    DeckId root_deck = DeckId::Green;
    std::size_t root_player = 0;
    std::vector<RootSampledWorld> sampled_worlds;
    std::vector<RootAction> actions;
    std::vector<std::string> exact_support;

    bool operator==(const Root&) const = default;
};

struct SuccessorFeatureScope {
    ScopeKind kind = ScopeKind::Full;
    std::size_t block = 0;
    GroupBank bank_a;
    GroupBank bank_b;

    bool operator==(const SuccessorFeatureScope&) const = default;
};

struct SuccessorFeatureEvaluation {
    struct Member {
        std::string root_action_descriptor;
        std::size_t root_world = 0;

        bool operator==(const Member&) const = default;
    };

    std::string root_stable_id;
    std::string information_set_fingerprint;
    std::size_t successor_owner = 0;
    std::size_t representative_root_world = 0;
    std::string representative_root_action_descriptor;
    std::vector<Member> members;
    std::vector<SuccessorFeatureScope> scopes;

    bool operator==(
        const SuccessorFeatureEvaluation&) const = default;
};

// Truthful Bellman-construction core. Gate, dominance, invariance, artifact,
// and publication verdicts deliberately live in the separate audit layer.
struct Construction {
    ac1_teacher_audit::Manifest manifest;
    std::string model_fingerprint;
    std::vector<Root> roots;
    std::vector<SuccessorFeatureEvaluation>
        successor_feature_evaluations;
    std::vector<fq0_bellman::FeatureTargetRow> feature_rows;
    fq0_bellman::FeatureCollisionAnalysis feature_collisions;
    std::array<std::size_t, kDeckCount> roots_by_deck{};
    std::string semantic_sha256;
    ExecutionMetadata execution;
};

struct BitIdentityWitness {
    std::string baseline_sha256;
    std::string comparison_sha256;

    bool bit_identical() const {
        return !baseline_sha256.empty() &&
               baseline_sha256 == comparison_sha256;
    }

    bool operator==(const BitIdentityWitness&) const = default;
};

struct RepresentativeReconstructionWitness {
    std::string root_action_descriptor;
    std::size_t root_world = 0;
    BitIdentityWitness identity;

    bool operator==(
        const RepresentativeReconstructionWitness&) const = default;
};

struct HiddenRepartitionReconstructionWitness {
    RepresentativeReconstructionWitness representative;
    bool eligible = false;
    bool changed = false;
    bool bit_identical = false;

    bool operator==(
        const HiddenRepartitionReconstructionWitness&) const = default;
};

struct GroupReconstructionWitnesses {
    std::vector<RepresentativeReconstructionWitness>
        representatives;
    RepresentativeReconstructionWitness hidden_repartition;
    std::vector<HiddenRepartitionReconstructionWitness>
        empirical_group_hidden_repartitions;
    ExecutionMetadata execution;
    bool hidden_repartition_eligible = false;
    bool hidden_repartition_changed = false;
    bool every_representative_bit_identical = false;
    bool hidden_repartition_bit_identical = false;

    bool operator==(
        const GroupReconstructionWitnesses&) const = default;
};

struct ProductionFeatureScopeReconstruction {
    std::size_t root_index = 0;
    std::size_t feature_evaluation_index = 0;
    std::size_t scope_index = 0;
    GroupReconstructionWitnesses witnesses;

    bool operator==(
        const ProductionFeatureScopeReconstruction&) const = default;
};

struct HiddenRepartitionDiagnostic {
    GameState state;
    bool eligible = false;
    bool changed = false;

    bool operator==(
        const HiddenRepartitionDiagnostic&) const = default;
};

// Rules-neutral hidden-zone perturbation used by production dominance and
// invariance audits. No seed or policy participates.
HiddenRepartitionDiagnostic hidden_repartition(
    const GameState& state, std::size_t observer);

// No-knob scientific construction. The caller remains responsible for
// artifact integrity and supplies the independently rebuilt frozen manifest.
// This entry rejects any non-production manifest or model fingerprint.
Construction construct_production(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> frozen_c16);

// Fixed one-worker rerun of the same production coordinates. It exists only
// for the schedule/thread-count invariance witness and exposes no recipe knob.
Construction construct_production_single_worker_invariance(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> frozen_c16);

// These no-knob variants derive their input from a fresh canonical manifest,
// then alter only the named invariant dimension.
Construction construct_production_descriptor_order_invariance(
    std::shared_ptr<const LearnedModel> frozen_c16);
Construction construct_production_hidden_repartition_invariance(
    std::shared_ptr<const LearnedModel> frozen_c16);

// Validates the exact primary construction once, then re-evaluates every
// physical member, the canonical owner-observation-equivalent opponent
// hand/library repartition, and every distinct empirical-group representative
// repartition for every successor Full+8 feature scope. Exposing only the
// complete bulk operation prevents reduced, fabricated, or selectively chosen
// subobjects from reaching the registered seed recipe.
std::vector<ProductionFeatureScopeReconstruction>
reconstruct_all_production_feature_scopes(
    const Construction& primary,
    std::shared_ptr<const LearnedModel> frozen_c16);

namespace testing {

// Test-only scale seam. Its defaults are intentionally unrelated to all
// sealed scientific seeds; production code cannot route through this API.
struct ReducedRecipe {
    std::uint64_t root_seed_base = 0xF00D000000000101ULL;
    std::uint64_t bank_a_seed_base = 0xF00D000000000202ULL;
    std::uint64_t bank_b_seed_base = 0xF00D000000000303ULL;
    std::size_t root_worlds = fq0_bellman::kBlockCount;
    std::size_t successor_worlds = 1;
    std::size_t workers = 1;
};

Construction construct_reduced(
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe = {});

GroupReconstructionWitnesses reconstruct_group(
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const RootAction& root_action, const Scope& scope,
    const SuccessorGroup& group,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe = {});

GroupReconstructionWitnesses reconstruct_feature_scope(
    const ac1_teacher_audit::ManifestRoot& manifest_root,
    const Root& root,
    const SuccessorFeatureEvaluation& evaluation,
    const SuccessorFeatureScope& scope,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe = {});

HiddenRepartitionDiagnostic hidden_repartition(
    const GameState& state, std::size_t observer);

// Runs the same complete, no-evaluation preflight used immediately before
// production feature-scope reconstruction, but against a quarantined reduced
// recipe. This seam exists only so malformed retained evidence can be proven
// to fail before any reconstruction callback opens a stream.
void validate_complete_preflight(
    const Construction& construction,
    const ac1_teacher_audit::Manifest& manifest,
    std::shared_ptr<const LearnedModel> model,
    ReducedRecipe recipe = {});

// Hashes only canonical scientific contents; worker-count diagnostics and
// retained hidden physical states are intentionally excluded.
std::string semantic_sha256(const Construction& construction);

} // namespace testing

} // namespace old_school::fq0_bellman_science
