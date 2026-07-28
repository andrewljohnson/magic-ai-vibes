#pragma once

#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_priority_collection.hpp"
#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_work0_cache {

inline constexpr std::string_view kSchema =
    "old-school-fq4-work0-trajectory-root-cache-v1";
inline constexpr std::string_view kEnvironment =
    "old-school-environment-v3-cleanup-discard";
inline constexpr std::string_view kOwnerInformationSchema =
    "old-school-fq4-priority-dev-owner-information-action-v2";
inline constexpr std::string_view kProductionPath =
    "data/old-school-fq4-work0-trajectory-root-cache-v1."
    "fq4work0";

inline constexpr std::size_t kDev1FitRootCount = 93;
inline constexpr std::size_t kDev1CheckRootCount = 99;
inline constexpr std::size_t kDev1RootCount =
    kDev1FitRootCount + kDev1CheckRootCount;
inline constexpr std::size_t kNeutralFitRootCount = 160;
inline constexpr std::size_t kNeutralCheckRootCount = 160;
inline constexpr std::size_t kNeutralRootCount =
    kNeutralFitRootCount + kNeutralCheckRootCount;
inline constexpr std::size_t kRootCount =
    kDev1RootCount + kNeutralRootCount;
inline constexpr std::size_t kDev1PositiveRootCount = 182;
inline constexpr std::size_t kDev1BackgroundRootCount = 10;
inline constexpr std::size_t kDev1OptionCount = 1'141;
inline constexpr std::size_t kNeutralOptionCount = 877;
inline constexpr std::size_t kOptionCount =
    kDev1OptionCount + kNeutralOptionCount;
// Frozen DEV1+DEV5 union census in DeckId order:
// Green, Red, Blue, White, RU Aggro.
inline constexpr std::array<std::size_t, kDeckCount>
    kOwnerDeckRootCounts{97, 75, 128, 86, 126};
inline constexpr std::size_t kMaximumCacheBytes =
    64U * 1024U * 1024U;

inline constexpr std::uint64_t kParentArtifactBytes =
    3'111'437;
inline constexpr std::string_view kParentArtifactSha256 =
    "53aeb904bd87311b37201859317f05ab0"
    "66bdfe134c72460cf94bff6d1f944ca";
inline constexpr std::string_view kParentModelFingerprint =
    "68126afc5a3e3757eb1d510a056585aa9"
    "74c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::uint64_t kDev1ArtifactBytes =
    fq4_dev_bundle::kPublishedArtifactBytes;
inline constexpr std::string_view kDev1ArtifactSha256 =
    fq4_dev_bundle::kPublishedArtifactSha256;
inline constexpr std::uint64_t kNeutralArtifactBytes =
    661'475;
inline constexpr std::string_view kNeutralArtifactSha256 =
    "47d94823f043971f6f9f0aa5f552bfa"
    "e210af9615d8f6dc7392e52dad3eaa105";

using Hash256 = fq4_dev_bundle::Hash256;

} // namespace old_school::fq4_work0_cache

namespace old_school::fq4_neutral_supplement {
struct Artifact;
}

namespace old_school::fq4_work0_cache {

struct FileIdentity {
    std::uint64_t bytes = 0;
    Hash256 sha256{};

    bool operator==(const FileIdentity&) const = default;
};

enum class SourceFamily : std::uint8_t {
    Dev1Selected = 0,
    Dev5Neutral = 1,
};

struct SourceBindings {
    FileIdentity parent_artifact;
    Hash256 parent_model_fingerprint{};
    FileIdentity dev1_artifact;
    Hash256 dev1_fit_selection_sha256{};
    Hash256 dev1_fit_scored_sha256{};
    Hash256 dev1_check_selection_sha256{};
    Hash256 dev1_check_scored_sha256{};
    FileIdentity neutral_artifact;
    Hash256 neutral_selected_order_sha256{};

    bool operator==(const SourceBindings&) const = default;
};

struct CanonicalAction {
    std::string descriptor;
    std::uint8_t raw_index = 0;

    bool operator==(const CanonicalAction&) const = default;
};

// The cache wire contains exactly this owner-visible state. It has public
// zones and counts, the owner's hand, and the minimum rules-internal state
// needed to reconstruct a legal GameState. It has no opponent-hand identity,
// no library identity/order, and no reporting-only PlayerGameStats.
struct OwnerVisibleState {
    std::size_t observer = 0;
    std::array<PublicPlayerState, 2> players;
    std::vector<CardId> owner_hand;
    std::vector<StackObject> stack;
    std::array<std::size_t, 2> extra_turns_pending{
        0, 0};
    std::size_t active_player = 0;
    std::size_t starting_player = 0;
    std::size_t turn_number = 0;
    std::array<bool, 2> failed_draw{false, false};
    PermanentId next_permanent_id = 1;
    StackObjectId next_stack_object_id = 1;

    bool operator==(const OwnerVisibleState&) const = default;
};

struct NeutralLocator {
    fq4_dev_bundle::Split split =
        fq4_dev_bundle::Split::Fit;
    std::uint8_t owner_deck = 0;
    std::uint8_t schedule_block = 0;
    Hash256 physical_game_sha256{};
    Hash256 stable_root_id{};
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
    Hash256 representative_rank{};
    Hash256 game_rank{};

    bool operator==(const NeutralLocator&) const = default;
};

// One exact row selected by an already-frozen source artifact. `source_row`
// is its zero-based ordinal within DEV1's FIT-then-CHECK concatenation or
// DEV5's published neutral row vector. `source_roles` preserves DEV1's
// existing positive/background bits and is zero for DEV5.
//
// No full source GameState, outcome, score, preferred/source-selected action,
// learned feature vector, opponent-hand identity, or library identity/order
// is retained. Consumers obtain common information-set worlds only through
// sample_world(), which always invokes sample_determinization().
struct Root {
    SourceFamily source = SourceFamily::Dev1Selected;
    fq4_dev_bundle::Split split =
        fq4_dev_bundle::Split::Fit;
    std::uint32_t source_row = 0;
    std::uint8_t source_roles = 0;
    std::uint64_t production_seed = 0;
    fq4_priority_collection::RootLocator locator;
    std::uint8_t owner_deck = 0;
    std::uint8_t opponent_deck = 0;
    Hash256 stable_root_id{};
    Hash256 physical_game_sha256{};
    Hash256 information_action_sha256{};
    Hash256 descriptor_set_sha256{};
    Hash256 raw_actions_sha256{};
    Hash256 canonical_actions_sha256{};
    bool has_neutral_locator = false;
    NeutralLocator neutral_locator;
    OwnerVisibleState state;
    std::array<
        std::array<std::uint8_t, kCardCount>, 2>
        deck_compositions{};
    LearnedDecisionContext context;
    std::vector<PriorityAction> raw_actions;
    std::vector<CanonicalAction> canonical_actions;
    std::uint8_t pass_index = 0;

    bool operator==(const Root&) const = default;
};

struct Manifest {
    std::string schema;
    std::string environment;
    SourceBindings sources;
    std::uint32_t dev1_options = 0;
    std::uint32_t neutral_options = 0;
    Hash256 root_order_sha256{};

    bool operator==(const Manifest&) const = default;
};

struct Artifact {
    Manifest manifest;
    std::vector<Root> roots;

    bool operator==(const Artifact&) const = default;
};

// The only state-producing cache API. The result is a fresh information-set
// determinization under the caller's common-world seed.
GameState sample_world(const Root& root, std::uint64_t seed);

Hash256 raw_actions_sha256(
    std::span<const PriorityAction> actions);
Hash256 canonical_actions_sha256(
    std::span<const CanonicalAction> actions);
Hash256 root_order_sha256(std::span<const Root> roots);

// Forms the explicit descriptor-order -> raw-engine-order bijection. It
// rejects duplicate raw actions instead of silently aliasing the first match.
std::vector<CanonicalAction> bind_canonical_actions(
    std::span<const PriorityAction> raw_actions,
    std::span<const PriorityAction>
        descriptor_canonical_actions);

// Structural codec validation only: framing, bounded fields, internal
// identities, and the frozen 512-row census. It is not an authenticity gate,
// does not prove that the rows came from the pinned source bytes, and must
// never be used by an evaluator as permission to consume an artifact.
void validate(const Artifact& artifact);

// Re-encodes and authenticates the exact pinned DEV1/DEV5 source bytes, then
// independently joins every source-row ordinal, locator, role, descriptor,
// Pass, and action-count field. This is the authenticity check that a future
// load_published() must perform after decode; evaluators must use only that
// strict loader and must not call decode()/validate() directly. Once source
// authenticity is proven, engine-produced public stack/card shape semantics
// remain the game engine's responsibility rather than a duplicated cache
// rules implementation.
void validate_against_sources(
    const Artifact& artifact,
    const fq4_dev_bundle::Bundle& dev1,
    const fq4_neutral_supplement::Artifact& neutral);

std::string encode(const Artifact& artifact);
// Parses and applies structural validation only; see validate() and
// validate_against_sources() above.
Artifact decode(std::string_view bytes);
std::string encoded_sha256(const Artifact& artifact);

namespace testing {

// Focused positive codec seam for a single semantically complete root. It is
// not an artifact loader and cannot bypass the fixed 512-row source contract.
std::string encode_root(const Root& root);
Root decode_root(std::string_view bytes);
std::string encode_priority_actions(
    std::span<const PriorityAction> actions);
std::vector<PriorityAction> decode_priority_actions(
    std::string_view bytes);

// Exercises the same exact global source/order/action/role/deck census gate
// as validate(), without reconstructing or loading any frozen source game.
void validate_census(std::span<const Root> roots);

} // namespace testing

// Publication and strict fixed-path loading are deliberately not part of
// this first cache seam. The next preregistered phase must add atomic
// no-replace publication plus load_published(), binding the whole-cache SHA
// and calling validate_against_sources(). That loader will be the sole
// production consumer path before any evaluator can consume kProductionPath.

} // namespace old_school::fq4_work0_cache
