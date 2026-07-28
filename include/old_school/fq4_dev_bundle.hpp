#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_dev_bundle {

inline constexpr std::string_view kBundleSchema =
    "old-school-fq4-priority-dev-bundle-v1";
inline constexpr std::string_view kArtifactPath =
    "data/old-school-fq4-priority-dev-v1.fq4dev";
inline constexpr std::string_view kPurpose =
    "adaptive-development-only";
inline constexpr std::string_view kProductionRecipe =
    "c16-value-mirror-k8-h4-r1-shallow-prior-on-"
    "source-priority-residual-zero-exploration-zero-pd0-off-"
    "legacy-continuation-turn-cap-128-retained-root-cap-16-"
    "action-cap-32";
inline constexpr std::string_view kFeatureSchema =
    "learned-priority-policy-features-v1";
inline constexpr std::string_view kStableRootSchema =
    "old-school-fq4-priority-dev-stable-root-v1";
inline constexpr std::string_view kFeatureContractSha256 =
    "240c91d19bb55279d7cbc58e64f4fddaef8da4248a089de8bc6e4cf96708a4f4";
inline constexpr std::string_view kCollectionSpecSha256 =
    "581c23222e6e0abe84c3c8bb2733ab5ecf0ea8e580f3698511eb8ab33682ff3a";
inline constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab066bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kParentModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::string_view kParentCriticFingerprint =
    "2982b155a02a4a2a3ce8442ae28f6d8cf7829103e538c60f0625b3332502e568";
inline constexpr std::string_view kParentPriorityFingerprint =
    "32dc6688a5c970e3eda4325bea5ee419077027e160697899e3b00c963fa1bb22";
inline constexpr std::string_view kParentAttackFingerprint =
    "dfd3aaa16755bee5d0c2c40956851b94ef5676a271a602eb23a57719f7358b01";
inline constexpr std::string_view kParentBlockFingerprint =
    "d64e40796bd1587958b7386996e6a1e5660778d40ec7b40b0ee6324b8e39adbb";
inline constexpr std::string_view kParentDamageOrderFingerprint =
    "f0a84ed549bbf95197dd00c13ab04c0a4f6b1771f14bdb30a7dca937d2d79c76";
inline constexpr std::size_t kSectionCount = 5;
inline constexpr std::size_t kDeckCount = 5;
inline constexpr std::size_t kWorldCount = 8;
inline constexpr std::size_t kFeatureCount = 893;
inline constexpr std::size_t kMaximumCensusRowsPerSplit = 1280;
inline constexpr std::size_t
    kMaximumCensusRowsPerOwnerPerspective = 16;
inline constexpr std::size_t kMaximumCensusRowsPerDeck = 256;
inline constexpr std::size_t kMaximumSelectedRowsPerSplit = 80;
inline constexpr std::size_t kMaximumRowsPerDeckAndSplit = 16;
inline constexpr std::size_t kMaximumActions = 32;
inline constexpr std::size_t kMaximumFeaturesPerAction =
    kFeatureCount;
inline constexpr std::size_t kMaximumArtifactBytes =
    128U * 1024U * 1024U;
inline constexpr std::uint64_t kFitSeedBase = 202607280210ULL;
inline constexpr std::uint64_t kCheckSeedBase = 202607280211ULL;
inline constexpr std::uint64_t kGenerationNamespace =
    0x46513444455631ULL;
inline constexpr std::uint64_t kHiddenNamespace =
    0x4651344456484944ULL;
inline constexpr std::uint64_t kDominanceNamespace =
    0x4651344456444f4dULL;

using Hash256 = std::array<std::uint8_t, 32>;

enum class Split : std::uint8_t {
    Fit = 0,
    Check = 1,
};

enum class Section : std::uint8_t {
    Manifest = 0,
    FitCensus = 1,
    FitRows = 2,
    CheckCensus = 3,
    CheckRows = 4,
};

enum Role : std::uint8_t {
    DominancePositive = 1U << 0U,
    BackgroundControl = 1U << 1U,
};

struct ComponentFingerprints {
    Hash256 critic{};
    Hash256 priority{};
    Hash256 attack{};
    Hash256 block{};
    Hash256 damage_order{};

    bool operator==(const ComponentFingerprints&) const = default;
};

struct SplitManifest {
    std::uint64_t source_seed_base = 0;
    Hash256 schedule_sha256{};
    Hash256 trajectory_sha256{};
    Hash256 retained_sha256{};
    Hash256 dominance_sha256{};
    Hash256 selection_sha256{};
    Hash256 scored_sha256{};
    std::uint32_t census_rows = 0;
    std::uint32_t selected_rows = 0;
    std::array<std::uint16_t, kDeckCount> census_by_deck{};
    std::array<std::uint16_t, kDeckCount> selected_by_deck{};
    std::array<std::uint16_t, kDeckCount> positive_by_deck{};
    std::array<std::uint16_t, kDeckCount> background_by_deck{};

    bool operator==(const SplitManifest&) const = default;
};

struct Manifest {
    std::string purpose;
    Hash256 producer_commit_sha256{};
    Hash256 producer_executable_sha256{};
    Hash256 parent_artifact_sha256{};
    Hash256 parent_model_fingerprint{};
    ComponentFingerprints parent_components;
    std::uint64_t generation_namespace = 0;
    std::uint64_t hidden_namespace = 0;
    std::uint64_t dominance_namespace = 0;
    Hash256 collection_spec_sha256{};
    std::string production_recipe;
    std::string feature_schema;
    std::uint16_t feature_count = 0;
    Hash256 feature_contract_sha256{};
    SplitManifest fit;
    SplitManifest check;

    bool operator==(const Manifest&) const = default;
};

struct DominanceCount {
    std::uint8_t complete = 0;
    std::uint8_t strict = 0;

    bool operator==(const DominanceCount&) const = default;
};

struct CensusRow {
    std::uint16_t schedule_index = 0;
    std::uint8_t owner_seat = 0;
    std::uint32_t trace_ordinal = 0;
    std::uint8_t owner_deck = 0;
    std::uint8_t opponent_deck = 0;
    Hash256 stable_root_id{};
    // SHA-256 of the collector's public physical_game_id bytes. The source
    // seed is already fixed by the split manifest and schedule_index remains
    // explicit, so no private or outcome-bearing coordinate is introduced.
    Hash256 physical_game_sha256{};
    Hash256 information_action_sha256{};
    Hash256 descriptor_set_sha256{};
    std::uint8_t pass_index = 0;
    std::vector<DominanceCount> dominance;

    bool operator==(const CensusRow&) const = default;
};

struct SparseFeature {
    std::uint16_t index = 0;
    std::uint64_t value_bits = 0;

    bool operator==(const SparseFeature&) const = default;
};

struct ActionRow {
    std::string descriptor;
    bool is_pass = false;
    DominanceCount dominance;
    std::array<std::uint64_t, kWorldCount> raw_sample_bits{};
    std::array<std::uint64_t, kWorldCount>
        shallow_prior_sample_bits{};
    std::array<std::uint64_t, kWorldCount>
        continuation_sample_bits{};
    std::uint64_t base_score_bits = 0;
    std::uint64_t parent_residual_bits = 0;
    std::vector<SparseFeature> features;

    bool operator==(const ActionRow&) const = default;
};

struct ScoreAccounting {
    std::uint64_t score_calls = 0;
    std::uint64_t scored_actions = 0;
    std::uint64_t sampled_worlds = 0;
    std::uint64_t rollout_evaluations = 0;
    std::uint64_t terminal_evaluations = 0;
    std::uint64_t bootstrap_evaluations = 0;

    bool operator==(const ScoreAccounting&) const = default;
};

struct SelectedRow {
    Split split = Split::Fit;
    CensusRow census;
    std::uint8_t roles = 0;
    std::uint64_t production_seed = 0;
    ScoreAccounting accounting;
    std::vector<ActionRow> actions;

    bool operator==(const SelectedRow&) const = default;
};

struct Bundle {
    Manifest manifest;
    std::vector<CensusRow> fit_census;
    std::vector<SelectedRow> fit_rows;
    std::vector<CensusRow> check_census;
    std::vector<SelectedRow> check_rows;

    bool operator==(const Bundle&) const = default;
};

struct PublishedArtifactExpectation {
    std::size_t byte_size = 0;
    std::string sha256;
};

std::string format_sha256(const Hash256& digest);
Hash256 parse_sha256(std::string_view hexadecimal);
Hash256 sha256(std::string_view bytes);
Hash256 expected_physical_game_sha256(
    Split split, std::uint16_t schedule_index);
Hash256 expected_stable_root_sha256(
    Split split, std::uint16_t schedule_index,
    std::uint8_t owner_seat,
    std::uint32_t trace_ordinal,
    const Hash256& information_action_sha256);
Hash256 descriptor_set_sha256(
    const std::vector<std::string>& descriptors);

// The encoder and decoder both apply the complete semantic validator.
std::string encode(const Bundle& bundle);
Bundle decode(std::string_view bytes);
void validate(const Bundle& bundle);

// Production loading is deliberately fixed to kArtifactPath. The final byte
// count and outer SHA-256 are supplied by the caller only after publication
// freezes them; there is no unfrozen production-load overload.
Bundle load_published(
    const PublishedArtifactExpectation& expectation);

// Writes the encoded bundle through a same-directory temporary file and a
// no-replace atomic link. It never overwrites kArtifactPath.
void publish_atomic_no_replace(const Bundle& bundle);

namespace testing {

// Emits canonical checksums around the supplied wire values without running
// semantic validation. Focused tests use this to prove that coherently
// rehashed semantic corruption is still rejected by decode().
std::string encode_wire_unchecked(const Bundle& bundle);

Bundle load_from(
    const std::filesystem::path& path,
    const PublishedArtifactExpectation& expectation);

void publish_atomic_no_replace_at(
    const std::filesystem::path& path,
    const Bundle& bundle);

} // namespace testing

} // namespace old_school::fq4_dev_bundle
