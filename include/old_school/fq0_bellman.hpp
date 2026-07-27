#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace old_school::fq0_bellman {

inline constexpr std::size_t kBlockCount = 8;
inline constexpr double kStudentT95Df7 = 2.3646242515927844;
inline constexpr double kNormal95 = 1.959963984540054;

struct ActionSamples {
    std::string descriptor;
    // All actions in one bank share a nonempty stream key and the same
    // canonical world coordinates. Independent A/B banks must use different
    // keys.
    std::string sample_stream_key;
    std::vector<std::size_t> world_indices;
    std::vector<double> samples;

    bool operator==(const ActionSamples&) const = default;
};

struct ActionMean {
    std::string descriptor;
    double value = 0.0;

    bool operator==(const ActionMean&) const = default;
};

struct CrossFitValue {
    std::vector<ActionMean> bank_a;
    std::vector<ActionMean> bank_b;
    std::vector<std::string> support_a;
    std::vector<std::string> support_b;
    double a_selected_b_value = 0.0;
    double b_selected_a_value = 0.0;
    double value = 0.0;

    bool operator==(const CrossFitValue&) const = default;
};

// Returns descriptor-canonical means. Every row must have the same positive
// sample width, and every sample must be a finite probability.
std::vector<ActionMean> canonical_action_means(
    std::span<const ActionSamples> actions);

// Selects the numerically greatest value, then includes only rows whose
// aggregate IEEE-754 bits exactly equal the already-selected maximum.
std::vector<std::string> exact_max_support(
    std::span<const ActionMean> actions);

// Computes the symmetric two-bank estimator:
//   0.5 * (mean_{a in support(A)} B(a)
//        + mean_{a in support(B)} A(a)).
CrossFitValue cross_fit_v0(
    std::span<const ActionSamples> bank_a,
    std::span<const ActionSamples> bank_b);

enum class OwnerRelation {
    SameOwner,
    OpponentOwner,
};

double root_owner_continuation_value(
    double successor_owner_value, OwnerRelation relation);

struct TerminalParticle {
    std::size_t world_index = 0;
    double root_owner_value = 0.0;

    bool operator==(const TerminalParticle&) const = default;
};

struct SuccessorGroup {
    std::string fingerprint;
    std::size_t mass = 0;
    // Exact root-particle membership. This is retained in addition to mass so
    // the backup can prove a disjoint, complete particle partition.
    std::vector<std::size_t> world_indices;
    OwnerRelation relation = OwnerRelation::SameOwner;
    double successor_owner_value = 0.0;

    bool operator==(const SuccessorGroup&) const = default;
};

struct BackedTarget {
    double value = 0.0;
    std::size_t particles = 0;
    std::size_t terminal_particles = 0;
    std::size_t same_owner_particles = 0;
    std::size_t opponent_owner_particles = 0;

    bool operator==(const BackedTarget&) const = default;
};

// Terminal particles are summed by world index, followed by
// descriptor-canonical same-owner groups and then descriptor-canonical
// opponent-owner groups. Counts must exactly partition particle_count.
BackedTarget back_up_root_target(
    std::size_t particle_count,
    std::span<const TerminalParticle> terminals,
    std::span<const SuccessorGroup> groups);

struct TargetBlocks {
    double full = 0.0;
    std::array<double, kBlockCount> blocks{};

    bool operator==(const TargetBlocks&) const = default;
};

struct BlockContrast {
    double delta64 = 0.0;
    std::array<double, kBlockCount> block_deltas{};
    double block_mean = 0.0;
    double sample_standard_deviation = 0.0;
    double lower_95 = 0.0;
    std::size_t positive_blocks = 0;
    std::size_t nonnegative_blocks = 0;

    bool operator==(const BlockContrast&) const = default;
};

BlockContrast summarize_block_contrast(
    const TargetBlocks& positive,
    const TargetBlocks& negative);

bool passes_directional_gate(
    const BlockContrast& contrast,
    std::size_t minimum_positive_blocks = 6);

struct FeatureTargetRow {
    std::string row_id;
    std::string information_set_id;
    std::string legal_set_id;
    std::string common_world_key;
    std::string action_descriptor;
    std::vector<double> features;
    std::string canonical_consequence_fingerprint;
    TargetBlocks target;
    bool unique_exact_max = false;

    bool operator==(const FeatureTargetRow&) const = default;
};

enum class CollisionTargetMethod {
    PairedStudentT,
    IndependentNormal,
};

struct FeatureCollision {
    std::string first_row_id;
    std::string second_row_id;
    CollisionTargetMethod target_method =
        CollisionTargetMethod::IndependentNormal;
    double target_separation_lower_95 = 0.0;
    bool consequence_conflict = false;
    bool target_conflict = false;
    bool support_conflict = false;
    bool harmful = false;

    bool operator==(const FeatureCollision&) const = default;
};

struct FeatureCollisionAnalysis {
    std::size_t rows = 0;
    std::size_t colliding_feature_classes = 0;
    std::vector<FeatureCollision> collisions;
    std::size_t harmful_collisions = 0;
    bool passed = false;

    bool operator==(const FeatureCollisionAnalysis&) const = default;
};

// Performs one global bit-exact feature census. Rows are compared across
// information sets and decks, not merely within one legal action set.
FeatureCollisionAnalysis analyze_global_feature_collisions(
    std::span<const FeatureTargetRow> rows);

} // namespace old_school::fq0_bellman
