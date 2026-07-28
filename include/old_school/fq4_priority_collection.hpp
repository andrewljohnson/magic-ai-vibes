#pragma once

#include "old_school/fq0_dominance.hpp"
#include "old_school/game.hpp"
#include "old_school/probes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_priority_collection {

inline constexpr std::size_t kPlayerCount = 2;
inline constexpr std::size_t kMaximumLegalActions = 32;
inline constexpr std::size_t kMaximumRootsPerOwnerGame = 16;
inline constexpr std::size_t kDominanceWorlds = 8;

// Every experiment supplies its own domains. This module intentionally owns
// no P0/D1 or development seed, schema, corpus hash, or artifact identity.
struct CollectionSpec {
    std::string_view owner_information_schema;
    std::string_view stable_root_schema;
    std::uint64_t hidden_seed_namespace = 0;
    std::string_view hidden_seed_scope;
    std::uint64_t dominance_seed_namespace = 0;
    std::string_view dominance_seed_scope;
    std::size_t maximum_legal_actions =
        kMaximumLegalActions;
    std::size_t maximum_roots_per_owner_game =
        kMaximumRootsPerOwnerGame;
    std::size_t dominance_worlds = kDominanceWorlds;

    bool valid() const;
};

struct SourceGame {
    std::size_t source_block = 0;
    std::uint64_t source_seed_base = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::array<DeckId, 2> seat_decks{
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t starting_player = 0;
    std::uint64_t game_seed = 0;

    bool operator==(const SourceGame&) const = default;
};

struct RootLocator {
    std::size_t source_block = 0;
    std::uint64_t source_seed_base = 0;
    std::size_t schedule_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t owner_seat = 0;
    std::size_t trace_ordinal = 0;

    bool operator==(const RootLocator&) const = default;
};

std::string physical_game_id(const RootLocator& locator);
std::string stable_root_id(
    const RootLocator& locator,
    std::string_view information_action_fingerprint,
    std::string_view stable_root_schema);

struct RetentionCandidate {
    std::size_t trace_ordinal = 0;
    std::string information_action_fingerprint;
    std::string information_action_bytes;
    std::string stable_id;

    bool operator==(const RetentionCandidate&) const = default;
};

struct RetentionResult {
    std::vector<std::size_t> unique_input_indices;
    std::vector<std::size_t> retained_input_indices;
    std::size_t duplicate_count = 0;
    std::size_t hash_collision_count = 0;
    bool valid = false;

    bool operator==(const RetentionResult&) const = default;
};

RetentionResult retain_owner_game_roots(
    const std::vector<RetentionCandidate>& candidates,
    std::size_t cap = kMaximumRootsPerOwnerGame);

struct ReplayRootManifest {
    RootLocator locator;
    DeckId owner_deck = DeckId::Green;
    DeckId opponent_deck = DeckId::Red;
    std::string stable_id;
    std::string information_action_fingerprint;
    std::vector<std::string> canonical_descriptors;
    std::size_t pass_index = 0;

    bool operator==(const ReplayRootManifest&) const = default;
};

bool validate_replay_manifest(
    const std::vector<ReplayRootManifest>& roots,
    std::string_view stable_root_schema,
    std::size_t maximum_legal_actions =
        kMaximumLegalActions);
std::string serialize_replay_manifest(
    const std::vector<ReplayRootManifest>& roots,
    std::string_view manifest_schema,
    std::string_view stable_root_schema,
    std::size_t maximum_legal_actions =
        kMaximumLegalActions);
std::string replay_manifest_sha256(
    const std::vector<ReplayRootManifest>& roots,
    std::string_view manifest_schema,
    std::string_view stable_root_schema,
    std::size_t maximum_legal_actions =
        kMaximumLegalActions);

enum class RootDisposition : std::uint8_t {
    Malformed,
    Trivial,
    OverCap,
    RetentionCandidate,
};

struct CanonicalRoot {
    probes::DecisionProbe probe;
    ReplayRootManifest manifest;
    std::string information_action_bytes;
};

struct RootBuildResult {
    RootDisposition disposition = RootDisposition::Malformed;
    std::optional<CanonicalRoot> root;
    // The selected descriptor is retained only to validate source-trajectory
    // integrity. Development selection and wire rows deliberately have no
    // field capable of receiving it.
    std::string selected_descriptor;
    std::string information_action_fingerprint;
    std::string error;
};

RootBuildResult build_canonical_root(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal, const CollectionSpec& spec);

struct HiddenClone {
    probes::DecisionProbe probe;
    bool eligible = false;
    bool distinct = false;
};

HiddenClone make_hidden_clone(const CanonicalRoot& root);
bool replay_exact(
    const CanonicalRoot& root, const HiddenClone& clone,
    const CollectionSpec& spec);
std::vector<std::vector<double>> priority_option_features(
    const probes::DecisionProbe& probe);
bool priority_feature_bits_identical(
    const probes::DecisionProbe& visible,
    const probes::DecisionProbe& hidden);

struct CanonicalHiddenDiagnostic {
    bool materialized = false;
    GameState canonical_state;
    GameState hidden_clone_state;
    std::vector<std::string> canonical_descriptors;
    std::string information_action_fingerprint;
    bool owner_hand_preserved = false;
    bool reporting_statistics_zero = false;
    bool second_replay_exact = false;
    bool hidden_feature_bits_identical = false;
    bool hidden_clone_eligible = false;
    bool hidden_clone_distinct = false;

    bool operator==(
        const CanonicalHiddenDiagnostic&) const = default;
};

CanonicalHiddenDiagnostic diagnose_canonical_hidden_root(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal, const CollectionSpec& spec);

struct DominanceWorldRow {
    bool pass_complete = false;
    std::vector<bool> candidate_complete;
    // Row i compares Pass (first) against candidate i (second).
    std::vector<fq0_dominance::Orientation> orientations;

    bool operator==(const DominanceWorldRow&) const = default;
};

struct RobustDominance {
    std::size_t pass_index = 0;
    std::vector<std::size_t> complete_world_counts;
    std::vector<std::size_t> strict_world_counts;
    std::vector<bool> robustly_pass_dominated;
    std::size_t complete_comparisons = 0;
    std::size_t transition_count = 0;
    bool shape_valid = false;

    bool any_dominated() const;
    bool operator==(const RobustDominance&) const = default;
};

RobustDominance summarize_robust_dominance(
    std::size_t pass_index, std::size_t action_count,
    const std::vector<DominanceWorldRow>& worlds,
    std::size_t expected_worlds = kDominanceWorlds,
    std::size_t maximum_legal_actions =
        kMaximumLegalActions);
RobustDominance evaluate_robust_dominance(
    const CanonicalRoot& root, const CollectionSpec& spec,
    std::vector<std::string>& infrastructure_failures);

enum class ParentClass : std::uint8_t {
    Safe,
    Class1,
    Class2,
    Class3,
    Invalid,
};

std::string_view parent_class_name(ParentClass classification);

struct ParentClassInput {
    std::vector<std::string> canonical_descriptors;
    std::vector<double> base_scores;
    std::vector<double> combined_scores;
    std::vector<std::vector<double>> base_samples;
    std::vector<bool> robustly_pass_dominated;

    bool operator==(const ParentClassInput&) const = default;
};

struct ParentClassResult {
    ParentClass classification = ParentClass::Invalid;
    std::size_t best_dominated_index = 0;
    std::size_t best_nondominated_index = 0;
    double margin = 0.0;
    double paired_standard_error = 0.0;
    double sigma = 0.0;
    bool valid = false;

    bool high_confidence_unsafe() const;
    bool operator==(const ParentClassResult&) const = default;
};

ParentClassResult classify_parent(
    const ParentClassInput& input,
    std::size_t expected_worlds = kDominanceWorlds);

struct RootCounts {
    std::size_t raw = 0;
    std::size_t nontrivial = 0;
    std::size_t malformed = 0;
    std::size_t trivial = 0;
    std::size_t over_cap = 0;
    std::size_t eligible = 0;
    std::size_t duplicate = 0;
    std::size_t unique = 0;
    std::size_t retained = 0;
    std::size_t cap_dropped = 0;
    std::size_t dominance_positive = 0;
    std::array<std::size_t, 4> parent_classes{};

    RootCounts& operator+=(const RootCounts& other);
    bool terminal_cross_sums_valid() const;
    bool operator==(const RootCounts&) const = default;
};

struct ProductionAccounting {
    std::size_t score_calls = 0;
    std::size_t scored_actions = 0;
    std::size_t sampled_worlds = 0;
    std::size_t rollout_evaluations = 0;
    std::size_t terminal_evaluations = 0;
    std::size_t bootstrapped_evaluations = 0;
    std::size_t dominance_transitions = 0;

    ProductionAccounting& operator+=(
        const ProductionAccounting& other);
    bool valid(
        std::size_t worlds_per_call = kDominanceWorlds,
        std::size_t rollouts_per_world = 1) const;
    bool operator==(
        const ProductionAccounting&) const = default;
};

struct GameCensus {
    SourceGame source;
    std::array<RootCounts, 2> owners;

    bool operator==(const GameCensus&) const = default;
};

struct DeckGameCoverage {
    std::size_t owner_games = 0;
    std::size_t games_with_raw = 0;
    std::size_t games_with_retained = 0;
    std::size_t games_with_dominance_positive = 0;

    bool operator==(const DeckGameCoverage&) const = default;
};

enum DevelopmentRole : std::uint8_t {
    DevelopmentRoleNone = 0,
    DevelopmentRolePositive = 1U << 0U,
    DevelopmentRoleBackground = 1U << 1U,
};

struct BlindSelectionInput {
    std::string stable_id;
    DeckId owner_deck = DeckId::Green;
    bool dominance_positive = false;

    bool operator==(const BlindSelectionInput&) const = default;
};

struct BlindSelectionRow {
    std::size_t input_index = 0;
    std::uint8_t roles = DevelopmentRoleNone;

    bool operator==(const BlindSelectionRow&) const = default;
};

struct BlindSelection {
    std::vector<BlindSelectionRow> rows;
    std::array<std::size_t, kDeckCount> rows_by_deck{};
    std::array<std::size_t, kDeckCount> positives_by_deck{};
    bool valid = false;

    bool operator==(const BlindSelection&) const = default;
};

// Reserves the first retained root of each deck as background, then selects
// at most fifteen evenly spaced positives from the remaining chronological
// positive stratum. The input deliberately has no score, outcome, selected
// action, card, or candidate-model field.
BlindSelection select_development_rows(
    const std::vector<BlindSelectionInput>& chronological_rows);

} // namespace old_school::fq4_priority_collection
