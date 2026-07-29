#pragma once

#include "old_school/action_q_nested_actor_broad_distill.hpp"

#include <array>
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::decision_boundary_critic {

namespace source = action_q_nested_actor_broad_distill;

inline constexpr std::size_t kPolicyFeatureCount =
    source::kPolicyFeatureCount;
inline constexpr std::size_t kExpectedRootsPerSplit =
    source::kActorGamesPerSplit;
inline constexpr std::size_t kExpectedRootsPerDeckAndSplit =
    source::kActorGamesPerDeckAndSplit;
inline constexpr std::string_view kRequiredParentFingerprint =
    source::kRequiredParentFingerprint;
inline constexpr std::string_view kRequiredSourceManifestHash =
    source::kFrozenCensusManifestHash;
inline constexpr std::string_view kFrozenSubsetHash =
    "850fe865b474b6b49e0794dc5dff5e917b322cc7fdabcf0ab673858a3e43c76a";

static_assert(kPolicyFeatureCount == 893);
static_assert(kExpectedRootsPerSplit == 80);
static_assert(kExpectedRootsPerDeckAndSplit == 16);

using Split = source::Split;
using ManifestRoot = source::ManifestRoot;

struct DeckCensus {
    std::size_t actor_games = 0;
    std::size_t roots = 0;
    std::size_t legal_options = 0;

    bool operator==(const DeckCensus&) const = default;
};

struct SplitCensus {
    Split split = Split::Train;
    std::size_t games = 0;
    std::size_t actor_games = 0;
    std::size_t roots = 0;
    std::size_t legal_options = 0;
    std::array<DeckCensus, kDeckCount> decks{};

    bool operator==(const SplitCensus&) const = default;
};

// DBC0 deliberately retains only material already present in the G4B
// owner-information manifest. It contains no GameState, hidden opponent
// identity, source outcome, sampled world, or teacher label.
struct Census {
    std::string parent_fingerprint;
    std::string source_manifest_hash;
    std::array<SplitCensus, 2> splits;
    std::vector<ManifestRoot> roots;
    std::string subset_hash;

    bool operator==(const Census&) const = default;
};

bool parse_census_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

std::string canonical_subset_hash(const Census& census);
void validate_census(const Census& census);
void require_frozen_census(const Census& census);

// Production authenticates the complete frozen G4B census before applying
// the preregistered position-zero projection.
Census project_frozen_census(const source::Census& full);
Census collect_census(
    std::shared_ptr<const LearnedModel> parent);

void print_census(
    std::ostream& output, const Census& census);

namespace testing {

// Pure selection seam used to prove that production selection depends only
// on the preexisting retained-position coordinate.
std::vector<ManifestRoot> select_position_zero(
    std::span<const ManifestRoot> roots);

// Pure assembly seam for validation and hash mutation tests. Production
// never accepts caller-supplied summaries or roots.
Census make_census(
    std::array<SplitCensus, 2> splits,
    std::vector<ManifestRoot> roots);

} // namespace testing

} // namespace old_school::decision_boundary_critic
