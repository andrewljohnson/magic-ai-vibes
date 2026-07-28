#pragma once

#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_coverage_census.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_neutral_supplement {

inline constexpr std::string_view kSchema =
    "old-school-fq4-priority-neutral-supplement-v1";
inline constexpr std::string_view kProductionArtifactPath =
    "data/old-school-fq4-priority-neutral-supplement-v1.fq4neutral";
inline constexpr std::string_view kRepresentativeRankDomain =
    "old-school-fq4-dev5-neutral-representative-rank-v1";
inline constexpr std::string_view kGameRankDomain =
    "old-school-fq4-dev5-neutral-game-rank-v1";
inline constexpr std::string_view kSelectedOrderDomain =
    "old-school-fq4-dev5-neutral-selected-order-v1";
inline constexpr std::string_view kProductionSeedTag =
    "old-school-oc1-action-regression-v1.production";
inline constexpr std::string_view kProductionSeedEnvironment =
    "old-school-environment-v3-cleanup-discard";
inline constexpr std::uint64_t kProductionSeedBase =
    5787775625948253273ULL;

inline constexpr std::size_t kSplitCount = 2;
inline constexpr std::size_t kDeckCount =
    fq4_dev_bundle::kDeckCount;
inline constexpr std::size_t kBlockCount = 4;
inline constexpr std::size_t kQuadrantCount = 4;
inline constexpr std::size_t kRowsPerSplit = 160;
inline constexpr std::size_t kRowsPerDeckAndSplit = 32;
inline constexpr std::size_t kRowsPerBlock = 8;
inline constexpr std::size_t kRowsPerOpponent = 2;
inline constexpr std::size_t kRowsPerQuadrant = 2;
inline constexpr std::size_t kTotalRows =
    kSplitCount * kRowsPerSplit;
inline constexpr std::size_t kMaximumArtifactBytes =
    128U * 1024U * 1024U;
inline constexpr std::size_t kMaximumCandidateRoots = 16'384;

using Hash256 = fq4_dev_bundle::Hash256;

struct FileIdentity {
    std::uint64_t bytes = 0;
    std::string sha256;

    bool operator==(const FileIdentity&) const = default;
};

// The exact byte fields consumed by both rank functions. No phase, card,
// descriptor, score, outcome, or source-selected action has an input path.
struct RankKey {
    fq4_dev_bundle::Split split =
        fq4_dev_bundle::Split::Fit;
    std::uint8_t owner_deck = 0;
    std::uint8_t schedule_block = 0;
    Hash256 physical_game_sha256{};
    Hash256 stable_root_id{};

    bool operator==(const RankKey&) const = default;
};

// Pure selection input. The eligibility witnesses are explicit so a caller
// cannot silently pass a broader or post-hoc-filtered stratum.
struct EligibleRoot {
    RankKey rank;
    std::uint16_t schedule_index = 0;
    std::uint8_t owner_seat = 0;
    bool owner_on_play = false;
    std::uint8_t opponent_deck = 0;
    std::uint32_t trace_ordinal = 0;
    std::uint8_t legal_action_count = 0;
    bool retained_nontrivial = false;
    std::uint8_t public_stack_size = 0;
    bool dominance_positive = false;
    std::uint8_t existing_selected_roles = 0;

    bool operator==(const EligibleRoot&) const = default;
};

struct RankedLocator {
    EligibleRoot root;
    Hash256 representative_rank{};
    Hash256 game_rank{};

    bool operator==(const RankedLocator&) const = default;
};

struct FrozenSelection {
    // Exact artifact/training order:
    // (FIT=0/CHECK=1, deck=0..4, block=0..3, unsigned game rank).
    std::vector<RankedLocator> rows;
    Hash256 selected_order_sha256{};

    bool operator==(const FrozenSelection&) const = default;
};

// Rank framing is exactly LE64(domain byte length), domain bytes,
// split/deck/block bytes, raw physical-game digest, raw stable-root digest.
std::string rank_preimage(
    std::string_view domain, const RankKey& key);
Hash256 representative_rank(const RankKey& key);
Hash256 game_rank(const RankKey& key);

// Performs representative selection and exhaustive balanced eight-game
// subset selection. The supplied census must be the exact accepted DEV4
// capacity object, and every split/deck candidate root and legal-option
// cross-sum must reproduce its eligible-stack-empty totals before ranking.
// It returns locators only and cannot score a root.
FrozenSelection freeze_selection(
    const std::vector<EligibleRoot>& eligible_roots,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity);

// Materializers may retain one split's 160 selected live roots while the
// other split is still being reconstructed. The authoritative publisher
// must subsequently call freeze_selection() over both complete lightweight
// candidate sets and require the same locators before scoring.
std::vector<RankedLocator> freeze_split_selection(
    fq4_dev_bundle::Split split,
    const std::vector<EligibleRoot>& eligible_roots,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity);

// Exact declared framing over an already canonical ordered selection.
Hash256 selected_order_sha256(
    std::span<const RankedLocator> ordered_rows);

// Pure exact DEV4 contract object accepted by the frozen DEV5 selector. It
// exists so a split-local materializer can prove completeness before the
// second split has been reconstructed.
fq4_dev_coverage_census::CoverageCensus
accepted_dev4_capacity();

// Exact production seed derivation over the fixed environment/tag, the
// lower-case hexadecimal stable-root digest, and the frozen seed base.
std::uint64_t production_seed_for_stable_root(
    const Hash256& stable_root_id);

struct ScientificSplitBinding {
    std::uint64_t source_seed_base = 0;
    Hash256 schedule_sha256{};
    Hash256 trajectory_sha256{};
    Hash256 retained_sha256{};
    Hash256 dominance_sha256{};
    Hash256 selection_sha256{};
    Hash256 scored_sha256{};

    bool operator==(const ScientificSplitBinding&) const = default;
};

struct Dev1Binding {
    FileIdentity bundle;
    FileIdentity parent_artifact;
    Hash256 parent_model_fingerprint{};
    fq4_dev_bundle::ComponentFingerprints parent_components;
    std::uint64_t generation_namespace = 0;
    std::uint64_t hidden_namespace = 0;
    std::uint64_t dominance_namespace = 0;
    Hash256 collection_spec_sha256{};
    std::string production_recipe;
    std::string feature_schema;
    std::string stable_root_schema;
    std::uint16_t feature_count = 0;
    Hash256 feature_contract_sha256{};
    ScientificSplitBinding fit;
    ScientificSplitBinding check;

    bool operator==(const Dev1Binding&) const = default;
};

struct SelectionRecipe {
    std::string representative_rank_domain;
    std::string game_rank_domain;
    std::string selected_order_domain;
    std::uint16_t rows_per_split = 0;
    std::uint16_t rows_per_deck_and_split = 0;
    std::uint8_t rows_per_block = 0;
    std::uint8_t rows_per_opponent = 0;
    std::uint8_t rows_per_quadrant = 0;

    bool operator==(const SelectionRecipe&) const = default;
};

struct Contract {
    Dev1Binding dev1;
    std::string dev4_schema;
    fq4_dev_coverage_census::CoverageCensus dev4_capacity;
    SelectionRecipe selection;

    bool operator==(const Contract&) const = default;
};

// Constructs the immutable contract from an exactly reconstructed DEV1
// manifest and the already accepted DEV4 aggregate census.
Contract make_contract(
    const fq4_dev_bundle::Manifest& dev1_manifest,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity);

struct NeutralAction {
    bool is_pass = false;
    fq4_dev_bundle::DominanceCount dominance;
    std::array<std::uint64_t, fq4_dev_bundle::kWorldCount>
        raw_sample_bits{};
    std::array<std::uint64_t, fq4_dev_bundle::kWorldCount>
        shallow_prior_sample_bits{};
    std::array<std::uint64_t, fq4_dev_bundle::kWorldCount>
        continuation_sample_bits{};
    std::uint64_t base_score_bits = 0;
    std::uint64_t parent_residual_bits = 0;
    std::vector<fq4_dev_bundle::SparseFeature> features;

    bool operator==(const NeutralAction&) const = default;
};

// Privacy-safe wire row. It deliberately has no GameState, hand, card,
// descriptor, outcome, or source-selected-action field.
struct NeutralRow {
    RankedLocator locator;
    Hash256 information_action_sha256{};
    Hash256 descriptor_set_sha256{};
    std::uint8_t pass_index = 0;
    std::uint64_t production_seed = 0;
    // Descriptive only. DEV5 never filters selection or scoring on clone
    // distinctness, but preserves the existing eligible==distinct invariant.
    bool hidden_clone_eligible = false;
    bool hidden_clone_distinct = false;
    fq4_dev_bundle::ScoreAccounting accounting;
    std::vector<NeutralAction> actions;

    bool operator==(const NeutralRow&) const = default;
};

struct ReconstructionLedger {
    std::uint64_t source_games = 0;
    std::uint64_t retained_roots = 0;
    std::uint64_t retained_options = 0;
    fq4_dev_bundle::ScoreAccounting parent_scoring;

    bool operator==(const ReconstructionLedger&) const = default;
};

struct PublisherAccounting {
    ReconstructionLedger reconstruction;
    fq4_dev_bundle::ScoreAccounting canonical_neutral;
    fq4_dev_bundle::ScoreAccounting hidden_clone;
    std::uint64_t bit_identical_actions = 0;
    std::array<
        std::array<std::uint16_t, kDeckCount>,
        kSplitCount>
        distinct_hidden_controls{};
    std::array<
        std::array<std::uint16_t, kDeckCount>,
        kSplitCount>
        nondistinct_hidden_controls{};
    bool selection_frozen_before_scoring = false;
    bool dev1_scientific_sections_exact = false;
    bool canonical_hidden_bit_identical = false;
    bool parent_immutable = false;
    bool bundle_immutable = false;
    bool executable_immutable = false;
    std::uint64_t parent_models_loaded = 0;
    std::uint64_t fits = 0;
    std::uint64_t candidate_rollout_evaluations = 0;
    std::uint64_t gameplay_evaluation_seeds = 0;

    bool operator==(const PublisherAccounting&) const = default;
};

struct Manifest {
    Contract contract;
    // Exact lower-case 40-hex Git commit, not a hash of its spelling.
    std::string producer_commit;
    Hash256 producer_executable_sha256{};
    Hash256 selected_order_sha256{};
    PublisherAccounting accounting;

    bool operator==(const Manifest&) const = default;
};

struct Artifact {
    Manifest manifest;
    std::vector<NeutralRow> rows;

    bool operator==(const Artifact&) const = default;
};

struct PublicationReport {
    FileIdentity artifact;
    Manifest manifest;

    bool operator==(const PublicationReport&) const = default;
};

void validate(const Artifact& artifact);
std::string encode(const Artifact& artifact);
Artifact decode(std::string_view bytes);

std::filesystem::path production_artifact_path();
std::filesystem::path production_temporary_path();

// Production publication and loading have fixed paths. Publication is a
// same-directory atomic no-replace link and never overwrites either the
// final path or its deterministic temporary.
PublicationReport publish_atomic_no_replace(
    const Artifact& artifact);
Artifact load_published(
    const Contract& expected_contract,
    const FileIdentity& expected_identity);

namespace testing {

using RankFunction =
    std::function<Hash256(std::string_view)>;

FrozenSelection freeze_selection_with_rank_function(
    const std::vector<EligibleRoot>& eligible_roots,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity,
    const RankFunction& rank_function);

// Retains wire integrity while bypassing semantic validation so focused
// tests can prove that coherently rehashed corruption fails in decode().
std::string encode_wire_unchecked(const Artifact& artifact);

std::filesystem::path temporary_path_for(
    const std::filesystem::path& destination);
PublicationReport publish_atomic_no_replace_at(
    const std::filesystem::path& destination,
    const Artifact& artifact);
Artifact load_from(
    const std::filesystem::path& path,
    const Contract& expected_contract,
    const FileIdentity& expected_identity);

} // namespace testing

} // namespace old_school::fq4_neutral_supplement
