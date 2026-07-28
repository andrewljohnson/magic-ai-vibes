#include "old_school/fq4_neutral_supplement.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_dev_schedule.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

namespace old_school::fq4_neutral_supplement {
namespace {

static_assert(
    sizeof(double) == sizeof(std::uint64_t) &&
    std::numeric_limits<double>::is_iec559);
static_assert(kBlockCount == fq4_dev_schedule::kScheduleBlocks);
static_assert(kRowsPerBlock * kBlockCount ==
              kRowsPerDeckAndSplit);
static_assert(kRowsPerDeckAndSplit * kDeckCount ==
              kRowsPerSplit);

constexpr std::array<char, 8> kMagic{
    'O', 'S', 'F', 'Q', '4', 'N', '1', '\0',
};
constexpr std::uint32_t kWireVersion = 1;
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::size_t kMaximumTextBytes = 512;
constexpr std::size_t kMaximumActions =
    fq4_dev_bundle::kMaximumActions;
constexpr std::size_t kMaximumFeatures =
    fq4_dev_bundle::kMaximumFeaturesPerAction;
constexpr std::uint64_t kParentArtifactBytes = 3'111'437;
constexpr std::uint64_t kDev1BundleBytes =
    fq4_dev_bundle::kPublishedArtifactBytes;
constexpr std::uint64_t kExpectedSourceGames = 320;
constexpr std::uint64_t kExpectedRetainedRoots = 9'728;
constexpr std::uint64_t kExpectedRetainedOptions = 29'108;
constexpr std::uint64_t kExpectedReconstructionRows = 192;
constexpr std::uint64_t kExpectedReconstructionActions = 1'141;
constexpr std::uint64_t kExpectedReconstructionWorlds = 1'536;
constexpr std::uint64_t kExpectedReconstructionRollouts = 9'128;
constexpr std::uint64_t kExpectedReconstructionTerminal = 1'787;
constexpr std::uint64_t kExpectedReconstructionBootstrap = 7'341;
constexpr std::string_view kExpectedFitTrajectorySha256 =
    "8498bf1574a72bd7fd58fbf192ad2cf8fa8baffe8bbc860de65016d9fdad8e35";
constexpr std::string_view kExpectedFitRetainedSha256 =
    "0f1e814c764e89586b387dc1a4c188170829c49879576b4cadef1ff87ccd6357";
constexpr std::string_view kExpectedFitDominanceSha256 =
    "201b71c85f125a0cfb4d74298cb4fca36c7d0842e0c99f9a6859bbd0401961b3";
constexpr std::string_view kExpectedFitSelectionSha256 =
    "e4231f3a3793d74268aea5684e6b285143cd491fe1bf8f0baf829bbe11c7c7c4";
constexpr std::string_view kExpectedFitScoredSha256 =
    "54c8770295a3c2d303b7f803e74f99c710b3b685e5a9153a57941c42f3ed9148";
constexpr std::string_view kExpectedCheckTrajectorySha256 =
    "5b251b2e829e41beca105eb3ab40d63570d8a3860938572bfc0ba18547641615";
constexpr std::string_view kExpectedCheckRetainedSha256 =
    "715337fdf342060eb91ed4045401581a2ee7876eb31021196360758ab8fb475b";
constexpr std::string_view kExpectedCheckDominanceSha256 =
    "f1c6b64c1baf9174eec1b192376c3e70fed68ce6ddb55d09bb992da3c1bed1f0";
constexpr std::string_view kExpectedCheckSelectionSha256 =
    "e43cd880af3facdf6d9793965900309d3719c475e7d07faef621320c5b744d64";
constexpr std::string_view kExpectedCheckScoredSha256 =
    "372a25ebf6d14c7e947872b33689326c41af01e5d804b42fb914c46c0be53c92";
constexpr std::uint64_t kFnvOffsetBasis =
    14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

using CensusCount = fq4_dev_coverage_census::Count;

struct FrozenDeckFacts {
    CensusCount retained;
    CensusCount positive_empty;
    CensusCount positive_active;
    CensusCount negative_empty;
    CensusCount negative_active;
    CensusCount main;
    CensusCount other;
    std::size_t background_options = 0;
};

constexpr CensusCount frozen_count(
    std::size_t roots, std::size_t options,
    std::size_t games) {
    return {
        .roots = roots,
        .options = options,
        .distinct_games = games,
    };
}

constexpr std::array<FrozenDeckFacts, 10>
    kAcceptedDev4DeckFacts{{
        {frozen_count(921, 2318, 64),
         frozen_count(16, 50, 11),
         frozen_count(0, 0, 0),
         frozen_count(892, 2232, 64),
         frozen_count(12, 34, 12),
         frozen_count(887, 2221, 64),
         frozen_count(5, 11, 5), 2},
        {frozen_count(978, 3166, 64),
         frozen_count(10, 65, 4),
         frozen_count(0, 0, 0),
         frozen_count(948, 3019, 64),
         frozen_count(19, 80, 17),
         frozen_count(921, 2878, 64),
         frozen_count(27, 141, 11), 2},
        {frozen_count(1002, 2564, 64),
         frozen_count(35, 210, 31),
         frozen_count(54, 180, 30),
         frozen_count(683, 1616, 64),
         frozen_count(229, 556, 62),
         frozen_count(683, 1616, 64),
         frozen_count(0, 0, 0), 2},
        {frozen_count(1001, 3155, 64),
         frozen_count(6, 26, 6),
         frozen_count(7, 21, 7),
         frozen_count(906, 2835, 64),
         frozen_count(81, 271, 35),
         frozen_count(903, 2826, 64),
         frozen_count(3, 9, 1), 2},
        {frozen_count(969, 3062, 64),
         frozen_count(51, 509, 29),
         frozen_count(0, 0, 0),
         frozen_count(912, 2527, 64),
         frozen_count(5, 23, 5),
         frozen_count(902, 2487, 64),
         frozen_count(10, 40, 8), 3},
        {frozen_count(942, 2443, 64),
         frozen_count(22, 60, 20),
         frozen_count(0, 0, 0),
         frozen_count(909, 2350, 64),
         frozen_count(10, 31, 9),
         frozen_count(899, 2319, 64),
         frozen_count(10, 31, 7), 2},
        {frozen_count(977, 3226, 64),
         frozen_count(10, 64, 5),
         frozen_count(0, 0, 0),
         frozen_count(943, 3033, 64),
         frozen_count(23, 127, 18),
         frozen_count(915, 2873, 64),
         frozen_count(28, 160, 18), 2},
        {frozen_count(995, 2517, 64),
         frozen_count(32, 213, 30),
         frozen_count(46, 146, 24),
         frozen_count(688, 1597, 64),
         frozen_count(228, 559, 63),
         frozen_count(688, 1597, 64),
         frozen_count(0, 0, 0), 2},
        {frozen_count(993, 3031, 64),
         frozen_count(2, 7, 1),
         frozen_count(6, 18, 6),
         frozen_count(909, 2751, 64),
         frozen_count(75, 253, 33),
         frozen_count(908, 2748, 64),
         frozen_count(1, 3, 1), 2},
        {frozen_count(950, 3626, 64),
         frozen_count(112, 1250, 42),
         frozen_count(0, 0, 0),
         frozen_count(832, 2346, 64),
         frozen_count(5, 27, 4),
         frozen_count(818, 2282, 64),
         frozen_count(14, 64, 9), 3},
    }};

[[noreturn]] void fail(std::string_view message) {
    throw std::invalid_argument(
        "invalid FQ4 DEV5 neutral supplement: " +
        std::string(message));
}

bool canonical_sha256(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               });
}

bool canonical_git_commit(std::string_view value) {
    return value.size() == 40 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return (character >= '0' &&
                           character <= '9') ||
                          (character >= 'a' &&
                           character <= 'f');
               }) &&
           std::any_of(
               value.begin(), value.end(),
               [](char character) {
                   return character != '0';
               });
}

bool zero_hash(const Hash256& digest) {
    return std::all_of(
        digest.begin(), digest.end(),
        [](std::uint8_t byte) { return byte == 0; });
}

void require_hash(
    const Hash256& digest, std::string_view context) {
    if (zero_hash(digest)) {
        fail(std::string(context) + " is zero");
    }
}

void require_sha256_text(
    std::string_view digest, std::string_view context) {
    if (!canonical_sha256(digest) ||
        std::all_of(
            digest.begin(), digest.end(),
            [](char byte) { return byte == '0'; })) {
        fail(std::string(context) + " is not canonical SHA-256");
    }
}

std::uint8_t split_byte(fq4_dev_bundle::Split split) {
    switch (split) {
    case fq4_dev_bundle::Split::Fit:
        return 0;
    case fq4_dev_bundle::Split::Check:
        return 1;
    }
    fail("split is unknown");
}

fq4_dev_bundle::Split split_from_byte(std::uint8_t value) {
    if (value > 1) {
        fail("split byte is out of range");
    }
    return static_cast<fq4_dev_bundle::Split>(value);
}

fq4_dev_schedule::Split schedule_split(
    fq4_dev_bundle::Split split) {
    return split == fq4_dev_bundle::Split::Fit
               ? fq4_dev_schedule::Split::Fit
               : fq4_dev_schedule::Split::Check;
}

std::size_t quadrant(const EligibleRoot& root) {
    return static_cast<std::size_t>(root.owner_seat) * 2U +
           (root.owner_on_play ? 0U : 1U);
}

class Writer {
  public:
    void u8(std::uint8_t value) {
        ensure(1);
        bytes_.push_back(static_cast<char>(value));
    }

    void u16(std::uint16_t value) {
        for (std::size_t byte = 0; byte < 2; ++byte) {
            u8(static_cast<std::uint8_t>(
                (value >> (8U * byte)) & 0xffU));
        }
    }

    void u32(std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            u8(static_cast<std::uint8_t>(
                (value >> (8U * byte)) & 0xffU));
        }
    }

    void u64(std::uint64_t value) {
        for (std::size_t byte = 0; byte < 8; ++byte) {
            u8(static_cast<std::uint8_t>(
                (value >> (8U * byte)) & 0xffU));
        }
    }

    void boolean(bool value) {
        u8(value ? 1U : 0U);
    }

    void raw(std::string_view value) {
        ensure(value.size());
        bytes_.append(value);
    }

    void raw(std::span<const std::uint8_t> value) {
        ensure(value.size());
        bytes_.append(
            reinterpret_cast<const char*>(value.data()),
            value.size());
    }

    void text(std::string_view value) {
        if (value.size() >
            std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error(
                "FQ4 neutral text exceeds wire limit");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        raw(value);
    }

    void hash(const Hash256& value) {
        raw(std::span<const std::uint8_t>(value));
    }

    const std::string& bytes() const {
        return bytes_;
    }

    std::string take() {
        return std::move(bytes_);
    }

  private:
    void ensure(std::size_t count) const {
        if (bytes_.size() > kMaximumArtifactBytes ||
            count > kMaximumArtifactBytes - bytes_.size()) {
            throw std::length_error(
                "FQ4 neutral artifact exceeds byte limit");
        }
    }

    std::string bytes_;
};

class Reader {
  public:
    explicit Reader(std::string_view bytes)
        : bytes_(bytes) {}

    std::uint8_t u8(std::string_view context) {
        require(1, context);
        return static_cast<std::uint8_t>(
            static_cast<unsigned char>(bytes_[cursor_++]));
    }

    std::uint16_t u16(std::string_view context) {
        std::uint16_t value = 0;
        for (std::size_t byte = 0; byte < 2; ++byte) {
            value |= static_cast<std::uint16_t>(u8(context))
                     << (8U * byte);
        }
        return value;
    }

    std::uint32_t u32(std::string_view context) {
        std::uint32_t value = 0;
        for (std::size_t byte = 0; byte < 4; ++byte) {
            value |= static_cast<std::uint32_t>(u8(context))
                     << (8U * byte);
        }
        return value;
    }

    std::uint64_t u64(std::string_view context) {
        std::uint64_t value = 0;
        for (std::size_t byte = 0; byte < 8; ++byte) {
            value |= static_cast<std::uint64_t>(u8(context))
                     << (8U * byte);
        }
        return value;
    }

    bool boolean(std::string_view context) {
        const std::uint8_t value = u8(context);
        if (value > 1) {
            fail(std::string(context) + " is not boolean");
        }
        return value != 0;
    }

    std::string text(std::string_view context) {
        const std::uint32_t size = u32(context);
        if (size == 0 || size > kMaximumTextBytes) {
            fail(std::string(context) + " has invalid length");
        }
        return std::string(view(size, context));
    }

    Hash256 hash(std::string_view context) {
        const std::string_view raw =
            view(32, context);
        Hash256 value{};
        std::memcpy(value.data(), raw.data(), value.size());
        return value;
    }

    std::string_view view(
        std::size_t size, std::string_view context) {
        require(size, context);
        const std::string_view value =
            bytes_.substr(cursor_, size);
        cursor_ += size;
        return value;
    }

    bool empty() const {
        return cursor_ == bytes_.size();
    }

  private:
    void require(
        std::size_t size, std::string_view context) const {
        if (cursor_ > bytes_.size() ||
            size > bytes_.size() - cursor_) {
            fail(std::string(context) + " is truncated");
        }
    }

    std::string_view bytes_;
    std::size_t cursor_ = 0;
};

Hash256 digest_string(std::string_view bytes) {
    return fq4_dev_bundle::parse_sha256(
        artifact_integrity::sha256_string(bytes));
}

void validate_rank_key(const RankKey& key) {
    static_cast<void>(split_byte(key.split));
    if (key.owner_deck >= kDeckCount) {
        fail("rank owner deck is out of range");
    }
    if (key.schedule_block >= kBlockCount) {
        fail("rank schedule block is out of range");
    }
    require_hash(
        key.physical_game_sha256,
        "rank physical-game digest");
    require_hash(key.stable_root_id, "rank stable-root digest");
}

void validate_eligible_root(const EligibleRoot& root) {
    validate_rank_key(root.rank);
    if (root.schedule_index >=
            fq4_dev_schedule::kPhysicalGamesPerSplit ||
        root.schedule_index /
                fq4_dev_schedule::kPhysicalGamesPerBlock !=
            root.rank.schedule_block ||
        root.owner_seat >= 2 ||
        root.opponent_deck >= kDeckCount ||
        root.opponent_deck == root.rank.owner_deck ||
        root.legal_action_count < 2 ||
        root.legal_action_count > kMaximumActions ||
        !root.retained_nontrivial ||
        root.public_stack_size != 0 ||
        root.dominance_positive ||
        root.existing_selected_roles != 0) {
        fail("selection root is outside the frozen eligible stratum");
    }

    const auto& schedule =
        fq4_dev_schedule::source_schedule(
            schedule_split(root.rank.split));
    if (root.schedule_index >= schedule.size()) {
        fail("selection schedule index is absent");
    }
    const auto& source = schedule[root.schedule_index];
    if (source.schedule_index != root.schedule_index ||
        source.schedule_block != root.rank.schedule_block ||
        static_cast<std::uint8_t>(
            source.seat_decks[root.owner_seat]) !=
            root.rank.owner_deck ||
        static_cast<std::uint8_t>(
            source.seat_decks[1U - root.owner_seat]) !=
            root.opponent_deck ||
        (source.starting_player == root.owner_seat) !=
            root.owner_on_play ||
        root.rank.physical_game_sha256 !=
            fq4_dev_bundle::expected_physical_game_sha256(
                root.rank.split,
                root.rank.schedule_block,
                root.schedule_index)) {
        fail("selection root disagrees with the frozen schedule");
    }
}

bool locator_order(
    const RankedLocator& left,
    const RankedLocator& right) {
    return std::tuple{
               split_byte(left.root.rank.split),
               left.root.rank.owner_deck,
               left.root.rank.schedule_block,
               left.game_rank} <
           std::tuple{
               split_byte(right.root.rank.split),
               right.root.rank.owner_deck,
               right.root.rank.schedule_block,
               right.game_rank};
}

using CellKey =
    std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>;
using GameKey =
    std::tuple<std::uint8_t, std::uint8_t, std::uint8_t, Hash256>;

struct CollisionGuard {
    std::set<std::string> preimages;
    std::map<Hash256, std::string> preimage_by_digest;

    Hash256 insert(
        std::string preimage,
        const testing::RankFunction& rank_function) {
        if (!preimages.insert(preimage).second) {
            fail("duplicate rank preimage");
        }
        const Hash256 digest = rank_function(preimage);
        require_hash(digest, "rank digest");
        const auto [position, inserted] =
            preimage_by_digest.emplace(digest, preimage);
        if (!inserted && position->second != preimage) {
            fail("rank digest collision");
        }
        return digest;
    }
};

void validate_dev4_capacity(
    const fq4_dev_coverage_census::CoverageCensus& census);

std::vector<RankedLocator> freeze_rows_impl(
    const std::vector<EligibleRoot>& eligible_roots,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity,
    const testing::RankFunction& rank_function,
    std::uint8_t split_begin,
    std::uint8_t split_end) {
    if (!rank_function) {
        fail("rank function is empty");
    }
    if (split_begin >= split_end ||
        split_end > kSplitCount) {
        fail("selection split range is invalid");
    }
    if (eligible_roots.empty() ||
        eligible_roots.size() > kMaximumCandidateRoots) {
        fail("eligible-root count is outside its bound");
    }
    validate_dev4_capacity(dev4_capacity);

    // One guard spans both domains. A digest collision between the
    // representative and game namespaces is still a collision and must not
    // silently become two different rank coordinates.
    CollisionGuard rank_guard;
    std::set<Hash256> stable_roots;
    std::map<GameKey, RankedLocator> representatives;
    std::map<GameKey, std::tuple<
        std::uint16_t, std::uint8_t, bool, std::uint8_t>>
        game_contexts;
    struct Population {
        std::size_t roots = 0;
        std::size_t options = 0;
        std::set<Hash256> physical_games;
    };
    std::array<
        std::array<Population, kDeckCount>,
        kSplitCount>
        population;

    for (const EligibleRoot& root : eligible_roots) {
        validate_eligible_root(root);
        const std::uint8_t root_split =
            split_byte(root.rank.split);
        if (root_split < split_begin ||
            root_split >= split_end) {
            fail("selection root is outside the requested split range");
        }
        if (!stable_roots.insert(root.rank.stable_root_id).second) {
            fail("duplicate stable-root locator");
        }
        Population& observed =
            population[root_split][root.rank.owner_deck];
        ++observed.roots;
        observed.options += root.legal_action_count;
        observed.physical_games.insert(
            root.rank.physical_game_sha256);
        const std::string representative_preimage =
            rank_preimage(kRepresentativeRankDomain, root.rank);
        const std::string game_preimage =
            rank_preimage(kGameRankDomain, root.rank);
        const Hash256 representative_digest =
            rank_guard.insert(
                representative_preimage, rank_function);
        const Hash256 game_digest =
            rank_guard.insert(game_preimage, rank_function);

        const GameKey key{
            split_byte(root.rank.split),
            root.rank.owner_deck,
            root.rank.schedule_block,
            root.rank.physical_game_sha256};
        const auto context = std::tuple{
            root.schedule_index,
            root.owner_seat,
            root.owner_on_play,
            root.opponent_deck};
        const auto [known_context, inserted_context] =
            game_contexts.emplace(key, context);
        if (!inserted_context &&
            known_context->second != context) {
            fail("one physical game has conflicting public context");
        }

        RankedLocator ranked{
            .root = root,
            .representative_rank = representative_digest,
            .game_rank = game_digest,
        };
        const auto existing = representatives.find(key);
        if (existing == representatives.end() ||
            ranked.representative_rank <
                existing->second.representative_rank) {
            representatives[key] = std::move(ranked);
        }
    }

    const auto& accepted_splits = std::array{
        std::cref(dev4_capacity.fit),
        std::cref(dev4_capacity.check),
    };
    for (std::uint8_t split = split_begin;
         split < split_end; ++split) {
        for (std::uint8_t owner = 0;
             owner < kDeckCount; ++owner) {
            const Population& observed =
                population[split][owner];
            const auto& expected =
                accepted_splits[split]
                    .get()
                    .decks[owner]
                    .eligible_stack_empty.all;
            if (observed.roots != expected.roots ||
                observed.options != expected.options ||
                observed.physical_games.size() !=
                    expected.distinct_games) {
                fail(
                    "eligible population does not reproduce "
                    "the exact DEV4 split/deck census");
            }
        }
    }

    std::map<CellKey, std::vector<RankedLocator>> cells;
    for (const auto& [key, representative] : representatives) {
        static_cast<void>(key);
        cells[{split_byte(representative.root.rank.split),
               representative.root.rank.owner_deck,
               representative.root.rank.schedule_block}]
            .push_back(representative);
    }

    std::vector<RankedLocator> selected;
    const std::size_t expected_rows =
        static_cast<std::size_t>(split_end - split_begin) *
        kRowsPerSplit;
    selected.reserve(expected_rows);
    for (std::uint8_t split = split_begin;
         split < split_end; ++split) {
        for (std::uint8_t owner = 0; owner < kDeckCount; ++owner) {
            for (std::uint8_t block = 0;
                 block < kBlockCount; ++block) {
                auto found = cells.find({split, owner, block});
                if (found == cells.end() ||
                    found->second.size() != 16) {
                    fail("selection cell does not contain 16 games");
                }
                auto& games = found->second;
                std::array<
                    std::array<std::size_t, kQuadrantCount>,
                    kDeckCount>
                    matrix{};
                for (const RankedLocator& game : games) {
                    const std::size_t game_quadrant =
                        quadrant(game.root);
                    if (game.root.opponent_deck == owner ||
                        game_quadrant >= kQuadrantCount ||
                        ++matrix[game.root.opponent_deck]
                                [game_quadrant] != 1) {
                        fail("selection cell is not the frozen binary matrix");
                    }
                }
                for (std::size_t opponent = 0;
                     opponent < kDeckCount; ++opponent) {
                    for (std::size_t game_quadrant = 0;
                         game_quadrant < kQuadrantCount;
                         ++game_quadrant) {
                        const std::size_t expected =
                            opponent == owner ? 0 : 1;
                        if (matrix[opponent][game_quadrant] !=
                            expected) {
                            fail("selection cell coverage is incomplete");
                        }
                    }
                }

                bool found_subset = false;
                std::vector<Hash256> best_tuple;
                std::vector<std::size_t> best_indices;
                const std::uint32_t subset_limit =
                    1U << games.size();
                for (std::uint32_t mask = 0;
                     mask < subset_limit; ++mask) {
                    if (std::popcount(mask) != kRowsPerBlock) {
                        continue;
                    }
                    std::array<std::size_t, kDeckCount>
                        by_opponent{};
                    std::array<std::size_t, kQuadrantCount>
                        by_quadrant{};
                    std::vector<Hash256> ranks;
                    std::vector<std::size_t> indices;
                    ranks.reserve(kRowsPerBlock);
                    indices.reserve(kRowsPerBlock);
                    for (std::size_t index = 0;
                         index < games.size(); ++index) {
                        if ((mask & (1U << index)) == 0) {
                            continue;
                        }
                        ++by_opponent[
                            games[index].root.opponent_deck];
                        ++by_quadrant[
                            quadrant(games[index].root)];
                        ranks.push_back(games[index].game_rank);
                        indices.push_back(index);
                    }
                    bool balanced = true;
                    for (std::size_t opponent = 0;
                         opponent < kDeckCount; ++opponent) {
                        const std::size_t expected =
                            opponent == owner
                                ? 0
                                : kRowsPerOpponent;
                        balanced =
                            balanced &&
                            by_opponent[opponent] == expected;
                    }
                    for (std::size_t game_quadrant = 0;
                         game_quadrant < kQuadrantCount;
                         ++game_quadrant) {
                        balanced =
                            balanced &&
                            by_quadrant[game_quadrant] ==
                                kRowsPerQuadrant;
                    }
                    if (!balanced) {
                        continue;
                    }
                    std::sort(ranks.begin(), ranks.end());
                    if (!found_subset || ranks < best_tuple) {
                        found_subset = true;
                        best_tuple = std::move(ranks);
                        best_indices = std::move(indices);
                    }
                }
                if (!found_subset ||
                    best_indices.size() != kRowsPerBlock) {
                    fail("balanced eight-game subset is infeasible");
                }

                std::vector<RankedLocator> chosen;
                chosen.reserve(kRowsPerBlock);
                for (std::size_t index : best_indices) {
                    chosen.push_back(games[index]);
                }
                std::sort(
                    chosen.begin(), chosen.end(),
                    [](const RankedLocator& left,
                       const RankedLocator& right) {
                        return left.game_rank < right.game_rank;
                    });
                for (std::size_t index = 1;
                     index < chosen.size(); ++index) {
                    if (chosen[index - 1].game_rank ==
                        chosen[index].game_rank) {
                        fail("selected game ranks are equal");
                    }
                }
                selected.insert(
                    selected.end(), chosen.begin(), chosen.end());
            }
        }
    }

    if (selected.size() != expected_rows ||
        !std::is_sorted(
            selected.begin(), selected.end(), locator_order)) {
        fail("frozen selection order is invalid");
    }
    return selected;
}

void validate_file_identity(
    const FileIdentity& identity,
    std::string_view context,
    std::uint64_t maximum_bytes =
        kMaximumArtifactBytes) {
    if (identity.bytes == 0 ||
        identity.bytes > maximum_bytes) {
        fail(std::string(context) + " byte count is invalid");
    }
    require_sha256_text(
        identity.sha256,
        std::string(context) + " SHA-256");
}

void validate_split_binding(
    const ScientificSplitBinding& split,
    fq4_dev_bundle::Split identity) {
    const bool fit =
        identity == fq4_dev_bundle::Split::Fit;
    const std::uint64_t expected_seed =
        fit ? fq4_dev_bundle::kFitSeedBase
            : fq4_dev_bundle::kCheckSeedBase;
    const std::string_view expected_schedule =
        fit ? fq4_dev_schedule::kExpectedFitScheduleSha256
            : fq4_dev_schedule::kExpectedCheckScheduleSha256;
    const auto expected_hash =
        [](std::string_view hexadecimal) {
            return fq4_dev_bundle::parse_sha256(
                hexadecimal);
        };
    if (split.source_seed_base != expected_seed ||
        split.schedule_sha256 !=
            expected_hash(expected_schedule) ||
        split.trajectory_sha256 !=
            expected_hash(
                fit ? kExpectedFitTrajectorySha256
                    : kExpectedCheckTrajectorySha256) ||
        split.retained_sha256 !=
            expected_hash(
                fit ? kExpectedFitRetainedSha256
                    : kExpectedCheckRetainedSha256) ||
        split.dominance_sha256 !=
            expected_hash(
                fit ? kExpectedFitDominanceSha256
                    : kExpectedCheckDominanceSha256) ||
        split.selection_sha256 !=
            expected_hash(
                fit ? kExpectedFitSelectionSha256
                    : kExpectedCheckSelectionSha256) ||
        split.scored_sha256 !=
            expected_hash(
                fit ? kExpectedFitScoredSha256
                    : kExpectedCheckScoredSha256)) {
        fail("scientific split source binding drifted");
    }
}

void validate_count(
    const fq4_dev_coverage_census::Count& count,
    std::string_view context) {
    if (count.options < count.roots ||
        count.distinct_games > count.roots) {
        fail(std::string(context) + " counts are inconsistent");
    }
}

fq4_dev_coverage_census::CoverageCensus
make_accepted_dev4_capacity() {
    fq4_dev_coverage_census::CoverageCensus result;
    for (std::size_t split = 0;
         split < kSplitCount; ++split) {
        auto& target =
            split == 0 ? result.fit : result.check;
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const FrozenDeckFacts& source =
                kAcceptedDev4DeckFacts[
                    split * kDeckCount + deck];
            auto& output = target.decks[deck];
            output.retained = source.retained;
            output.dominance_positive = {
                .empty = source.positive_empty,
                .active = source.positive_active,
            };
            output.selected_background = {
                .empty = frozen_count(
                    1, source.background_options, 1),
                .active = frozen_count(0, 0, 0),
            };
            output.unselected_dominance_negative = {
                .empty = source.negative_empty,
                .active = source.negative_active,
            };
            output.eligible_stack_empty = {
                .all = source.negative_empty,
                .first_or_second_main = source.main,
                .other = source.other,
            };
            for (auto& block : output.eligible_blocks) {
                block.distinct_games = 16;
                block.balanced_eight_feasible = true;
                for (std::size_t opponent = 0;
                     opponent < kDeckCount; ++opponent) {
                    for (std::size_t game_quadrant = 0;
                         game_quadrant < kQuadrantCount;
                         ++game_quadrant) {
                        block.games_by_opponent_quadrant
                            [opponent][game_quadrant] =
                                opponent == deck ? 0 : 1;
                    }
                }
            }
            output.capacity_met = true;
        }
    }
    result.retained_rows = kExpectedRetainedRoots;
    result.retained_options = kExpectedRetainedOptions;
    result.selected_rows = 192;
    result.selected_positive_rows = 182;
    result.selected_background_rows = 10;
    result.action_invariant_stack_rows =
        kExpectedRetainedRoots;
    result.exact_public_stack_rows =
        kExpectedRetainedRoots;
    result.valid_public_phase_rows =
        kExpectedRetainedRoots;
    result.capacity_licensed = true;
    return result;
}

void validate_dev4_capacity(
    const fq4_dev_coverage_census::CoverageCensus& census) {
    if (census != make_accepted_dev4_capacity()) {
        fail("DEV4 capacity differs from the accepted exact census");
    }
    if (!census.capacity_licensed ||
        census.retained_rows != kExpectedRetainedRoots ||
        census.retained_options != kExpectedRetainedOptions ||
        census.selected_rows != 192 ||
        census.selected_positive_rows != 182 ||
        census.selected_background_rows != 10 ||
        census.action_invariant_stack_rows !=
            census.retained_rows ||
        census.exact_public_stack_rows !=
            census.retained_rows ||
        census.valid_public_phase_rows !=
            census.retained_rows) {
        fail("DEV4 aggregate capacity binding is invalid");
    }

    std::size_t retained_roots = 0;
    std::size_t retained_options = 0;
    const auto validate_split =
        [&](const fq4_dev_coverage_census::SplitCensus& split) {
            for (std::size_t owner = 0;
                 owner < kDeckCount; ++owner) {
                const auto& deck = split.decks[owner];
                validate_count(deck.retained, "retained");
                validate_count(
                    deck.dominance_positive.empty,
                    "empty positive");
                validate_count(
                    deck.dominance_positive.active,
                    "active positive");
                validate_count(
                    deck.selected_background.empty,
                    "empty background");
                validate_count(
                    deck.selected_background.active,
                    "active background");
                validate_count(
                    deck.unselected_dominance_negative.empty,
                    "empty negative");
                validate_count(
                    deck.unselected_dominance_negative.active,
                    "active negative");
                validate_count(
                    deck.eligible_stack_empty.all,
                    "eligible");
                validate_count(
                    deck.eligible_stack_empty.first_or_second_main,
                    "eligible main");
                validate_count(
                    deck.eligible_stack_empty.other,
                    "eligible other");
                if (!deck.capacity_met ||
                    deck.eligible_stack_empty.all.roots !=
                        deck.unselected_dominance_negative
                            .empty.roots ||
                    deck.eligible_stack_empty.all.options !=
                        deck.unselected_dominance_negative
                            .empty.options ||
                    deck.eligible_stack_empty.all.roots !=
                        deck.eligible_stack_empty
                                .first_or_second_main.roots +
                            deck.eligible_stack_empty.other.roots ||
                    deck.eligible_stack_empty.all.options !=
                        deck.eligible_stack_empty
                                .first_or_second_main.options +
                            deck.eligible_stack_empty.other.options) {
                    fail("DEV4 eligible capacity counts are inconsistent");
                }
                retained_roots += deck.retained.roots;
                retained_options += deck.retained.options;
                for (std::size_t block = 0;
                     block < kBlockCount; ++block) {
                    const auto& eligibility =
                        deck.eligible_blocks[block];
                    std::size_t games = 0;
                    for (std::size_t opponent = 0;
                         opponent < kDeckCount; ++opponent) {
                        for (std::size_t game_quadrant = 0;
                             game_quadrant < kQuadrantCount;
                             ++game_quadrant) {
                            const std::size_t expected =
                                opponent == owner ? 0 : 1;
                            const std::size_t observed =
                                eligibility
                                    .games_by_opponent_quadrant
                                        [opponent][game_quadrant];
                            if (observed != expected) {
                                fail("DEV4 per-cell capacity is not exact");
                            }
                            games += observed;
                        }
                    }
                    if (games != 16 ||
                        eligibility.distinct_games != 16 ||
                        !eligibility.balanced_eight_feasible) {
                        fail("DEV4 block capacity is not licensed");
                    }
                }
            }
        };
    validate_split(census.fit);
    validate_split(census.check);
    if (retained_roots != census.retained_rows ||
        retained_options != census.retained_options) {
        fail("DEV4 retained cross-sum is inconsistent");
    }
}

void validate_contract(const Contract& contract) {
    validate_file_identity(
        contract.dev1.bundle, "DEV1 bundle",
        fq4_dev_bundle::kMaximumArtifactBytes);
    validate_file_identity(
        contract.dev1.parent_artifact, "parent artifact",
        16U * 1024U * 1024U);
    if (contract.dev1.bundle.bytes != kDev1BundleBytes ||
        contract.dev1.bundle.sha256 !=
            fq4_dev_bundle::kPublishedArtifactSha256 ||
        contract.dev1.parent_artifact.bytes !=
            kParentArtifactBytes ||
        contract.dev1.parent_artifact.sha256 !=
            fq4_dev_bundle::kParentArtifactSha256 ||
        contract.dev1.parent_model_fingerprint !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kParentModelFingerprint) ||
        contract.dev1.parent_components.critic !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kParentCriticFingerprint) ||
        contract.dev1.parent_components.priority !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kParentPriorityFingerprint) ||
        contract.dev1.parent_components.attack !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kParentAttackFingerprint) ||
        contract.dev1.parent_components.block !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kParentBlockFingerprint) ||
        contract.dev1.parent_components.damage_order !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kParentDamageOrderFingerprint) ||
        contract.dev1.generation_namespace !=
            fq4_dev_bundle::kGenerationNamespace ||
        contract.dev1.hidden_namespace !=
            fq4_dev_bundle::kHiddenNamespace ||
        contract.dev1.dominance_namespace !=
            fq4_dev_bundle::kDominanceNamespace ||
        contract.dev1.collection_spec_sha256 !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kCollectionSpecSha256) ||
        contract.dev1.production_recipe !=
            fq4_dev_bundle::kProductionRecipe ||
        contract.dev1.feature_schema !=
            fq4_dev_bundle::kFeatureSchema ||
        contract.dev1.stable_root_schema !=
            fq4_dev_bundle::kStableRootSchema ||
        contract.dev1.feature_count !=
            fq4_dev_bundle::kFeatureCount ||
        contract.dev1.feature_contract_sha256 !=
            fq4_dev_bundle::parse_sha256(
                fq4_dev_bundle::kFeatureContractSha256)) {
        fail("DEV1 scientific contract binding drifted");
    }
    validate_split_binding(
        contract.dev1.fit, fq4_dev_bundle::Split::Fit);
    validate_split_binding(
        contract.dev1.check, fq4_dev_bundle::Split::Check);
    if (contract.dev4_schema !=
            fq4_dev_coverage_census::kSchema) {
        fail("DEV4 schema binding drifted");
    }
    validate_dev4_capacity(contract.dev4_capacity);
    const auto& selection = contract.selection;
    if (selection.representative_rank_domain !=
            kRepresentativeRankDomain ||
        selection.game_rank_domain != kGameRankDomain ||
        selection.selected_order_domain !=
            kSelectedOrderDomain ||
        selection.rows_per_split != kRowsPerSplit ||
        selection.rows_per_deck_and_split !=
            kRowsPerDeckAndSplit ||
        selection.rows_per_block != kRowsPerBlock ||
        selection.rows_per_opponent !=
            kRowsPerOpponent ||
        selection.rows_per_quadrant !=
            kRowsPerQuadrant) {
        fail("selection recipe drifted");
    }
}

ScientificSplitBinding split_binding(
    const fq4_dev_bundle::SplitManifest& split) {
    return {
        .source_seed_base = split.source_seed_base,
        .schedule_sha256 = split.schedule_sha256,
        .trajectory_sha256 = split.trajectory_sha256,
        .retained_sha256 = split.retained_sha256,
        .dominance_sha256 = split.dominance_sha256,
        .selection_sha256 = split.selection_sha256,
        .scored_sha256 = split.scored_sha256,
    };
}

} // namespace

std::string rank_preimage(
    std::string_view domain, const RankKey& key) {
    if (domain != kRepresentativeRankDomain &&
        domain != kGameRankDomain) {
        fail("rank domain is not licensed");
    }
    validate_rank_key(key);
    Writer output;
    output.u64(static_cast<std::uint64_t>(domain.size()));
    output.raw(domain);
    output.u8(split_byte(key.split));
    output.u8(key.owner_deck);
    output.u8(key.schedule_block);
    output.hash(key.physical_game_sha256);
    output.hash(key.stable_root_id);
    return output.take();
}

Hash256 representative_rank(const RankKey& key) {
    return digest_string(
        rank_preimage(kRepresentativeRankDomain, key));
}

Hash256 game_rank(const RankKey& key) {
    return digest_string(rank_preimage(kGameRankDomain, key));
}

fq4_dev_coverage_census::CoverageCensus
accepted_dev4_capacity() {
    const auto result = make_accepted_dev4_capacity();
    validate_dev4_capacity(result);
    return result;
}

std::uint64_t production_seed_for_stable_root(
    const Hash256& stable_root_id) {
    require_hash(stable_root_id, "production-seed stable root");
    std::uint64_t hash = kFnvOffsetBasis;
    const auto append_byte =
        [&](std::uint8_t byte) {
            hash ^= byte;
            hash *= kFnvPrime;
        };
    const auto append_u64 =
        [&](std::uint64_t value) {
            for (std::size_t byte = 0;
                 byte < sizeof(value); ++byte) {
                append_byte(static_cast<std::uint8_t>(
                    value >> (byte * 8U)));
            }
        };
    const auto append_text =
        [&](std::string_view value) {
            append_u64(
                static_cast<std::uint64_t>(value.size()));
            for (const unsigned char byte : value) {
                append_byte(byte);
            }
        };
    append_text(kProductionSeedEnvironment);
    append_text(kProductionSeedTag);
    append_text(
        fq4_dev_bundle::format_sha256(stable_root_id));
    append_u64(kProductionSeedBase);
    return hash;
}

FrozenSelection freeze_selection(
    const std::vector<EligibleRoot>& eligible_roots,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity) {
    std::vector<RankedLocator> rows =
        freeze_rows_impl(
            eligible_roots, dev4_capacity,
            [](std::string_view bytes) {
                return digest_string(bytes);
            },
            0, kSplitCount);
    return {
        .rows = rows,
        .selected_order_sha256 =
            selected_order_sha256(rows),
    };
}

std::vector<RankedLocator> freeze_split_selection(
    fq4_dev_bundle::Split split,
    const std::vector<EligibleRoot>& eligible_roots,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity) {
    const std::uint8_t identity = split_byte(split);
    return freeze_rows_impl(
        eligible_roots, dev4_capacity,
        [](std::string_view bytes) {
            return digest_string(bytes);
        },
        identity,
        static_cast<std::uint8_t>(identity + 1U));
}

Hash256 selected_order_sha256(
    std::span<const RankedLocator> ordered_rows) {
    if (ordered_rows.size() != kTotalRows) {
        fail("selected-order row count is not 320");
    }
    Writer output;
    output.u64(
        static_cast<std::uint64_t>(
            kSelectedOrderDomain.size()));
    output.raw(kSelectedOrderDomain);
    output.u64(
        static_cast<std::uint64_t>(ordered_rows.size()));
    for (std::size_t index = 0;
         index < ordered_rows.size(); ++index) {
        const RankedLocator& row = ordered_rows[index];
        validate_eligible_root(row.root);
        if (row.representative_rank !=
                representative_rank(row.root.rank) ||
            row.game_rank != game_rank(row.root.rank)) {
            fail("selected rank does not reproduce its preimage");
        }
        if (index != 0 &&
            !locator_order(ordered_rows[index - 1], row)) {
            fail("selected rows are not in canonical order");
        }
        output.u8(split_byte(row.root.rank.split));
        output.u8(row.root.rank.owner_deck);
        output.u8(row.root.rank.schedule_block);
        output.hash(row.game_rank);
        output.hash(row.representative_rank);
        output.hash(row.root.rank.physical_game_sha256);
        output.hash(row.root.rank.stable_root_id);
    }
    return digest_string(output.bytes());
}

FrozenSelection testing::freeze_selection_with_rank_function(
    const std::vector<EligibleRoot>& eligible_roots,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity,
    const RankFunction& rank_function) {
    std::vector<RankedLocator> rows =
        freeze_rows_impl(
            eligible_roots, dev4_capacity, rank_function,
            0, kSplitCount);
    return {
        .rows = rows,
        .selected_order_sha256 =
            selected_order_sha256(rows),
    };
}

Contract make_contract(
    const fq4_dev_bundle::Manifest& manifest,
    const fq4_dev_coverage_census::CoverageCensus&
        dev4_capacity) {
    Contract contract{
        .dev1 = {
            .bundle = {
                .bytes = kDev1BundleBytes,
                .sha256 = std::string(
                    fq4_dev_bundle::
                        kPublishedArtifactSha256),
            },
            .parent_artifact = {
                .bytes = kParentArtifactBytes,
                .sha256 = std::string(
                    fq4_dev_bundle::kParentArtifactSha256),
            },
            .parent_model_fingerprint =
                manifest.parent_model_fingerprint,
            .parent_components =
                manifest.parent_components,
            .generation_namespace =
                manifest.generation_namespace,
            .hidden_namespace = manifest.hidden_namespace,
            .dominance_namespace =
                manifest.dominance_namespace,
            .collection_spec_sha256 =
                manifest.collection_spec_sha256,
            .production_recipe =
                manifest.production_recipe,
            .feature_schema = manifest.feature_schema,
            .stable_root_schema =
                std::string(
                    fq4_dev_bundle::kStableRootSchema),
            .feature_count = manifest.feature_count,
            .feature_contract_sha256 =
                manifest.feature_contract_sha256,
            .fit = split_binding(manifest.fit),
            .check = split_binding(manifest.check),
        },
        .dev4_schema = std::string(
            fq4_dev_coverage_census::kSchema),
        .dev4_capacity = dev4_capacity,
        .selection = {
            .representative_rank_domain =
                std::string(kRepresentativeRankDomain),
            .game_rank_domain =
                std::string(kGameRankDomain),
            .selected_order_domain =
                std::string(kSelectedOrderDomain),
            .rows_per_split = kRowsPerSplit,
            .rows_per_deck_and_split =
                kRowsPerDeckAndSplit,
            .rows_per_block = kRowsPerBlock,
            .rows_per_opponent = kRowsPerOpponent,
            .rows_per_quadrant = kRowsPerQuadrant,
        },
    };
    validate_contract(contract);
    return contract;
}

namespace {

std::size_t read_size(
    Reader& input, std::string_view context,
    std::size_t maximum) {
    const std::uint64_t value = input.u64(context);
    if (value > maximum ||
        value >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        fail(std::string(context) + " exceeds its bound");
    }
    return static_cast<std::size_t>(value);
}

void write_file_identity(
    Writer& output, const FileIdentity& identity) {
    output.u64(identity.bytes);
    output.hash(
        fq4_dev_bundle::parse_sha256(identity.sha256));
}

FileIdentity read_file_identity(
    Reader& input, std::string_view context) {
    return {
        .bytes =
            input.u64(std::string(context) + " bytes"),
        .sha256 =
            fq4_dev_bundle::format_sha256(
                input.hash(
                    std::string(context) + " hash")),
    };
}

void write_components(
    Writer& output,
    const fq4_dev_bundle::ComponentFingerprints& components) {
    output.hash(components.critic);
    output.hash(components.priority);
    output.hash(components.attack);
    output.hash(components.block);
    output.hash(components.damage_order);
}

fq4_dev_bundle::ComponentFingerprints read_components(
    Reader& input) {
    return {
        .critic = input.hash("parent critic"),
        .priority = input.hash("parent Priority"),
        .attack = input.hash("parent Attack"),
        .block = input.hash("parent Block"),
        .damage_order =
            input.hash("parent damage order"),
    };
}

void write_split_binding(
    Writer& output,
    const ScientificSplitBinding& split) {
    output.u64(split.source_seed_base);
    output.hash(split.schedule_sha256);
    output.hash(split.trajectory_sha256);
    output.hash(split.retained_sha256);
    output.hash(split.dominance_sha256);
    output.hash(split.selection_sha256);
    output.hash(split.scored_sha256);
}

ScientificSplitBinding read_split_binding(
    Reader& input, std::string_view context) {
    return {
        .source_seed_base =
            input.u64(std::string(context) + " seed base"),
        .schedule_sha256 =
            input.hash(std::string(context) + " schedule"),
        .trajectory_sha256 =
            input.hash(std::string(context) + " trajectory"),
        .retained_sha256 =
            input.hash(std::string(context) + " retained"),
        .dominance_sha256 =
            input.hash(std::string(context) + " dominance"),
        .selection_sha256 =
            input.hash(std::string(context) + " selection"),
        .scored_sha256 =
            input.hash(std::string(context) + " scored"),
    };
}

void write_count(
    Writer& output,
    const fq4_dev_coverage_census::Count& count) {
    output.u64(count.roots);
    output.u64(count.options);
    output.u64(count.distinct_games);
}

fq4_dev_coverage_census::Count read_count(
    Reader& input, std::string_view context) {
    constexpr std::size_t kMaximumRoots = 100'000;
    constexpr std::size_t kMaximumOptions = 3'200'000;
    return {
        .roots = read_size(
            input, std::string(context) + " roots",
            kMaximumRoots),
        .options = read_size(
            input, std::string(context) + " options",
            kMaximumOptions),
        .distinct_games = read_size(
            input, std::string(context) + " games",
            kExpectedSourceGames),
    };
}

void write_stack_counts(
    Writer& output,
    const fq4_dev_coverage_census::StackCounts& counts) {
    write_count(output, counts.empty);
    write_count(output, counts.active);
}

fq4_dev_coverage_census::StackCounts read_stack_counts(
    Reader& input, std::string_view context) {
    return {
        .empty =
            read_count(
                input, std::string(context) + " empty"),
        .active =
            read_count(
                input, std::string(context) + " active"),
    };
}

void write_eligible_phase_counts(
    Writer& output,
    const fq4_dev_coverage_census::EligiblePhaseCounts&
        counts) {
    write_count(output, counts.all);
    write_count(output, counts.first_or_second_main);
    write_count(output, counts.other);
}

fq4_dev_coverage_census::EligiblePhaseCounts
read_eligible_phase_counts(
    Reader& input, std::string_view context) {
    return {
        .all =
            read_count(
                input, std::string(context) + " all"),
        .first_or_second_main =
            read_count(
                input, std::string(context) + " main"),
        .other =
            read_count(
                input, std::string(context) + " other"),
    };
}

void write_block_eligibility(
    Writer& output,
    const fq4_dev_coverage_census::BlockEligibility& block) {
    for (const auto& opponent :
         block.games_by_opponent_quadrant) {
        for (std::size_t count : opponent) {
            output.u64(count);
        }
    }
    output.u64(block.distinct_games);
    output.boolean(block.balanced_eight_feasible);
}

fq4_dev_coverage_census::BlockEligibility
read_block_eligibility(
    Reader& input, std::string_view context) {
    fq4_dev_coverage_census::BlockEligibility block;
    for (std::size_t opponent = 0;
         opponent < kDeckCount; ++opponent) {
        for (std::size_t game_quadrant = 0;
             game_quadrant < kQuadrantCount;
             ++game_quadrant) {
            block.games_by_opponent_quadrant
                [opponent][game_quadrant] =
                read_size(
                    input,
                    std::string(context) + " matrix",
                    1);
        }
    }
    block.distinct_games = read_size(
        input, std::string(context) + " distinct games",
        16);
    block.balanced_eight_feasible =
        input.boolean(
            std::string(context) + " feasibility");
    return block;
}

void write_deck_census(
    Writer& output,
    const fq4_dev_coverage_census::DeckCensus& deck) {
    write_count(output, deck.retained);
    write_stack_counts(output, deck.dominance_positive);
    write_stack_counts(output, deck.selected_background);
    write_stack_counts(
        output, deck.unselected_dominance_negative);
    write_eligible_phase_counts(
        output, deck.eligible_stack_empty);
    for (const auto& block : deck.eligible_blocks) {
        write_block_eligibility(output, block);
    }
    output.boolean(deck.capacity_met);
}

fq4_dev_coverage_census::DeckCensus read_deck_census(
    Reader& input, std::string_view context) {
    fq4_dev_coverage_census::DeckCensus deck;
    deck.retained =
        read_count(input, std::string(context) + " retained");
    deck.dominance_positive =
        read_stack_counts(
            input, std::string(context) + " positive");
    deck.selected_background =
        read_stack_counts(
            input, std::string(context) + " background");
    deck.unselected_dominance_negative =
        read_stack_counts(
            input, std::string(context) + " negative");
    deck.eligible_stack_empty =
        read_eligible_phase_counts(
            input, std::string(context) + " eligible");
    for (std::size_t block = 0;
         block < kBlockCount; ++block) {
        deck.eligible_blocks[block] =
            read_block_eligibility(
                input,
                std::string(context) + " block " +
                    std::to_string(block));
    }
    deck.capacity_met =
        input.boolean(
            std::string(context) + " capacity");
    return deck;
}

void write_coverage(
    Writer& output,
    const fq4_dev_coverage_census::CoverageCensus& census) {
    for (const auto& deck : census.fit.decks) {
        write_deck_census(output, deck);
    }
    for (const auto& deck : census.check.decks) {
        write_deck_census(output, deck);
    }
    output.u64(census.retained_rows);
    output.u64(census.retained_options);
    output.u64(census.selected_rows);
    output.u64(census.selected_positive_rows);
    output.u64(census.selected_background_rows);
    output.u64(census.action_invariant_stack_rows);
    output.u64(census.exact_public_stack_rows);
    output.u64(census.valid_public_phase_rows);
    output.boolean(census.capacity_licensed);
}

fq4_dev_coverage_census::CoverageCensus read_coverage(
    Reader& input) {
    fq4_dev_coverage_census::CoverageCensus census;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        census.fit.decks[deck] =
            read_deck_census(
                input,
                "FIT deck " + std::to_string(deck));
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        census.check.decks[deck] =
            read_deck_census(
                input,
                "CHECK deck " + std::to_string(deck));
    }
    census.retained_rows =
        read_size(
            input, "coverage retained rows", 100'000);
    census.retained_options =
        read_size(
            input, "coverage retained options",
            3'200'000);
    census.selected_rows =
        read_size(input, "coverage selected rows", 1'000);
    census.selected_positive_rows =
        read_size(input, "coverage positive rows", 1'000);
    census.selected_background_rows =
        read_size(input, "coverage background rows", 1'000);
    census.action_invariant_stack_rows =
        read_size(input, "coverage invariant rows", 100'000);
    census.exact_public_stack_rows =
        read_size(input, "coverage exact-stack rows", 100'000);
    census.valid_public_phase_rows =
        read_size(input, "coverage phase rows", 100'000);
    census.capacity_licensed =
        input.boolean("coverage licensed");
    return census;
}

void write_contract(Writer& output, const Contract& contract) {
    write_file_identity(output, contract.dev1.bundle);
    write_file_identity(
        output, contract.dev1.parent_artifact);
    output.hash(contract.dev1.parent_model_fingerprint);
    write_components(output, contract.dev1.parent_components);
    output.u64(contract.dev1.generation_namespace);
    output.u64(contract.dev1.hidden_namespace);
    output.u64(contract.dev1.dominance_namespace);
    output.hash(contract.dev1.collection_spec_sha256);
    output.text(contract.dev1.production_recipe);
    output.text(contract.dev1.feature_schema);
    output.text(contract.dev1.stable_root_schema);
    output.u16(contract.dev1.feature_count);
    output.hash(contract.dev1.feature_contract_sha256);
    write_split_binding(output, contract.dev1.fit);
    write_split_binding(output, contract.dev1.check);
    output.text(contract.dev4_schema);
    write_coverage(output, contract.dev4_capacity);
    output.text(
        contract.selection.representative_rank_domain);
    output.text(contract.selection.game_rank_domain);
    output.text(contract.selection.selected_order_domain);
    output.u16(contract.selection.rows_per_split);
    output.u16(
        contract.selection.rows_per_deck_and_split);
    output.u8(contract.selection.rows_per_block);
    output.u8(contract.selection.rows_per_opponent);
    output.u8(contract.selection.rows_per_quadrant);
}

Contract read_contract(Reader& input) {
    Contract contract;
    contract.dev1.bundle =
        read_file_identity(input, "DEV1 bundle");
    contract.dev1.parent_artifact =
        read_file_identity(input, "parent artifact");
    contract.dev1.parent_model_fingerprint =
        input.hash("parent model");
    contract.dev1.parent_components =
        read_components(input);
    contract.dev1.generation_namespace =
        input.u64("generation namespace");
    contract.dev1.hidden_namespace =
        input.u64("hidden namespace");
    contract.dev1.dominance_namespace =
        input.u64("dominance namespace");
    contract.dev1.collection_spec_sha256 =
        input.hash("collection contract");
    contract.dev1.production_recipe =
        input.text("production recipe");
    contract.dev1.feature_schema =
        input.text("feature schema");
    contract.dev1.stable_root_schema =
        input.text("stable-root schema");
    contract.dev1.feature_count =
        input.u16("feature count");
    contract.dev1.feature_contract_sha256 =
        input.hash("feature contract");
    contract.dev1.fit =
        read_split_binding(input, "FIT");
    contract.dev1.check =
        read_split_binding(input, "CHECK");
    contract.dev4_schema = input.text("DEV4 schema");
    contract.dev4_capacity = read_coverage(input);
    contract.selection.representative_rank_domain =
        input.text("representative-rank domain");
    contract.selection.game_rank_domain =
        input.text("game-rank domain");
    contract.selection.selected_order_domain =
        input.text("selected-order domain");
    contract.selection.rows_per_split =
        input.u16("rows per split");
    contract.selection.rows_per_deck_and_split =
        input.u16("rows per deck and split");
    contract.selection.rows_per_block =
        input.u8("rows per block");
    contract.selection.rows_per_opponent =
        input.u8("rows per opponent");
    contract.selection.rows_per_quadrant =
        input.u8("rows per quadrant");
    return contract;
}

void write_score_accounting(
    Writer& output,
    const fq4_dev_bundle::ScoreAccounting& accounting) {
    output.u64(accounting.score_calls);
    output.u64(accounting.scored_actions);
    output.u64(accounting.sampled_worlds);
    output.u64(accounting.rollout_evaluations);
    output.u64(accounting.terminal_evaluations);
    output.u64(accounting.bootstrap_evaluations);
}

fq4_dev_bundle::ScoreAccounting read_score_accounting(
    Reader& input, std::string_view context) {
    return {
        .score_calls =
            input.u64(std::string(context) + " calls"),
        .scored_actions =
            input.u64(std::string(context) + " actions"),
        .sampled_worlds =
            input.u64(std::string(context) + " worlds"),
        .rollout_evaluations =
            input.u64(std::string(context) + " rollouts"),
        .terminal_evaluations =
            input.u64(std::string(context) + " terminal"),
        .bootstrap_evaluations =
            input.u64(std::string(context) + " bootstrap"),
    };
}

void write_accounting(
    Writer& output,
    const PublisherAccounting& accounting) {
    output.u64(accounting.reconstruction.source_games);
    output.u64(accounting.reconstruction.retained_roots);
    output.u64(accounting.reconstruction.retained_options);
    write_score_accounting(
        output, accounting.reconstruction.parent_scoring);
    write_score_accounting(
        output, accounting.canonical_neutral);
    write_score_accounting(
        output, accounting.hidden_clone);
    output.u64(accounting.bit_identical_actions);
    for (const auto& split :
         accounting.distinct_hidden_controls) {
        for (std::uint16_t count : split) {
            output.u16(count);
        }
    }
    for (const auto& split :
         accounting.nondistinct_hidden_controls) {
        for (std::uint16_t count : split) {
            output.u16(count);
        }
    }
    output.boolean(
        accounting.selection_frozen_before_scoring);
    output.boolean(
        accounting.dev1_scientific_sections_exact);
    output.boolean(
        accounting.canonical_hidden_bit_identical);
    output.boolean(accounting.parent_immutable);
    output.boolean(accounting.bundle_immutable);
    output.boolean(accounting.executable_immutable);
    output.u64(accounting.parent_models_loaded);
    output.u64(accounting.fits);
    output.u64(
        accounting.candidate_rollout_evaluations);
    output.u64(accounting.gameplay_evaluation_seeds);
}

PublisherAccounting read_accounting(Reader& input) {
    PublisherAccounting accounting;
    accounting.reconstruction.source_games =
        input.u64("reconstruction games");
    accounting.reconstruction.retained_roots =
        input.u64("reconstruction roots");
    accounting.reconstruction.retained_options =
        input.u64("reconstruction options");
    accounting.reconstruction.parent_scoring =
        read_score_accounting(
            input, "reconstruction scoring");
    accounting.canonical_neutral =
        read_score_accounting(input, "canonical scoring");
    accounting.hidden_clone =
        read_score_accounting(input, "hidden scoring");
    accounting.bit_identical_actions =
        input.u64("bit-identical actions");
    for (auto& split :
         accounting.distinct_hidden_controls) {
        for (std::uint16_t& count : split) {
            count = input.u16("distinct hidden controls");
        }
    }
    for (auto& split :
         accounting.nondistinct_hidden_controls) {
        for (std::uint16_t& count : split) {
            count =
                input.u16("nondistinct hidden controls");
        }
    }
    accounting.selection_frozen_before_scoring =
        input.boolean("selection frozen");
    accounting.dev1_scientific_sections_exact =
        input.boolean("DEV1 sections exact");
    accounting.canonical_hidden_bit_identical =
        input.boolean("canonical/hidden identity");
    accounting.parent_immutable =
        input.boolean("parent immutable");
    accounting.bundle_immutable =
        input.boolean("bundle immutable");
    accounting.executable_immutable =
        input.boolean("executable immutable");
    accounting.parent_models_loaded =
        input.u64("parent models loaded");
    accounting.fits = input.u64("fits");
    accounting.candidate_rollout_evaluations =
        input.u64("candidate rollouts");
    accounting.gameplay_evaluation_seeds =
        input.u64("gameplay seeds");
    return accounting;
}

void write_eligible_root(
    Writer& output, const EligibleRoot& root) {
    output.u8(split_byte(root.rank.split));
    output.u8(root.rank.owner_deck);
    output.u8(root.rank.schedule_block);
    output.hash(root.rank.physical_game_sha256);
    output.hash(root.rank.stable_root_id);
    output.u16(root.schedule_index);
    output.u8(root.owner_seat);
    output.boolean(root.owner_on_play);
    output.u8(root.opponent_deck);
    output.u32(root.trace_ordinal);
    output.u8(root.legal_action_count);
    output.boolean(root.retained_nontrivial);
    output.u8(root.public_stack_size);
    output.boolean(root.dominance_positive);
    output.u8(root.existing_selected_roles);
}

EligibleRoot read_eligible_root(Reader& input) {
    EligibleRoot root;
    root.rank.split =
        split_from_byte(input.u8("row split"));
    root.rank.owner_deck = input.u8("row owner deck");
    root.rank.schedule_block =
        input.u8("row block");
    root.rank.physical_game_sha256 =
        input.hash("row physical game");
    root.rank.stable_root_id =
        input.hash("row stable root");
    root.schedule_index = input.u16("row schedule index");
    root.owner_seat = input.u8("row owner seat");
    root.owner_on_play =
        input.boolean("row owner on play");
    root.opponent_deck = input.u8("row opponent");
    root.trace_ordinal = input.u32("row trace ordinal");
    root.legal_action_count =
        input.u8("row legal-action count");
    root.retained_nontrivial =
        input.boolean("row retained/nontrivial");
    root.public_stack_size =
        input.u8("row public stack size");
    root.dominance_positive =
        input.boolean("row dominance positive");
    root.existing_selected_roles =
        input.u8("row existing roles");
    return root;
}

void write_neutral_action(
    Writer& output, const NeutralAction& action) {
    output.boolean(action.is_pass);
    output.u8(action.dominance.complete);
    output.u8(action.dominance.strict);
    for (std::uint64_t bits : action.raw_sample_bits) {
        output.u64(bits);
    }
    for (std::uint64_t bits :
         action.shallow_prior_sample_bits) {
        output.u64(bits);
    }
    for (std::uint64_t bits :
         action.continuation_sample_bits) {
        output.u64(bits);
    }
    output.u64(action.base_score_bits);
    output.u64(action.parent_residual_bits);
    output.u64(action.features.size());
    for (const auto feature : action.features) {
        output.u16(feature.index);
        output.u64(feature.value_bits);
    }
}

NeutralAction read_neutral_action(Reader& input) {
    NeutralAction action;
    action.is_pass = input.boolean("action Pass");
    action.dominance.complete =
        input.u8("action dominance complete");
    action.dominance.strict =
        input.u8("action dominance strict");
    for (std::uint64_t& bits : action.raw_sample_bits) {
        bits = input.u64("raw sample");
    }
    for (std::uint64_t& bits :
         action.shallow_prior_sample_bits) {
        bits = input.u64("shallow sample");
    }
    for (std::uint64_t& bits :
         action.continuation_sample_bits) {
        bits = input.u64("continuation sample");
    }
    action.base_score_bits = input.u64("base score");
    action.parent_residual_bits =
        input.u64("parent residual");
    const std::size_t feature_count =
        read_size(
            input, "sparse-feature count",
            kMaximumFeatures);
    action.features.resize(feature_count);
    for (auto& feature : action.features) {
        feature.index =
            input.u16("sparse-feature index");
        feature.value_bits =
            input.u64("sparse-feature value");
    }
    return action;
}

void write_neutral_row(
    Writer& output, const NeutralRow& row) {
    write_eligible_root(output, row.locator.root);
    output.hash(row.locator.representative_rank);
    output.hash(row.locator.game_rank);
    output.hash(row.information_action_sha256);
    output.hash(row.descriptor_set_sha256);
    output.u8(row.pass_index);
    output.u64(row.production_seed);
    output.boolean(row.hidden_clone_eligible);
    output.boolean(row.hidden_clone_distinct);
    write_score_accounting(output, row.accounting);
    output.u64(row.actions.size());
    for (const NeutralAction& action : row.actions) {
        write_neutral_action(output, action);
    }
}

NeutralRow read_neutral_row(Reader& input) {
    NeutralRow row;
    row.locator.root = read_eligible_root(input);
    row.locator.representative_rank =
        input.hash("representative rank");
    row.locator.game_rank = input.hash("game rank");
    row.information_action_sha256 =
        input.hash("information/action digest");
    row.descriptor_set_sha256 =
        input.hash("descriptor-set digest");
    row.pass_index = input.u8("Pass index");
    row.production_seed = input.u64("production seed");
    row.hidden_clone_eligible =
        input.boolean("hidden clone eligible");
    row.hidden_clone_distinct =
        input.boolean("hidden clone distinct");
    row.accounting =
        read_score_accounting(input, "row accounting");
    const std::size_t action_count =
        read_size(input, "action count", kMaximumActions);
    row.actions.reserve(action_count);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        row.actions.push_back(read_neutral_action(input));
    }
    return row;
}

std::string encode_payload_unchecked(
    const Artifact& artifact) {
    Writer output;
    write_contract(output, artifact.manifest.contract);
    output.text(artifact.manifest.producer_commit);
    output.hash(
        artifact.manifest.producer_executable_sha256);
    output.hash(
        artifact.manifest.selected_order_sha256);
    write_accounting(output, artifact.manifest.accounting);
    output.u64(artifact.rows.size());
    for (const NeutralRow& row : artifact.rows) {
        write_neutral_row(output, row);
    }
    return output.take();
}

Artifact decode_payload(std::string_view payload) {
    Reader input(payload);
    Artifact artifact;
    artifact.manifest.contract = read_contract(input);
    artifact.manifest.producer_commit =
        input.text("producer commit");
    artifact.manifest.producer_executable_sha256 =
        input.hash("producer executable");
    artifact.manifest.selected_order_sha256 =
        input.hash("selected order");
    artifact.manifest.accounting = read_accounting(input);
    const std::size_t row_count =
        read_size(input, "neutral row count", kTotalRows);
    artifact.rows.reserve(row_count);
    for (std::size_t row = 0; row < row_count; ++row) {
        artifact.rows.push_back(read_neutral_row(input));
    }
    if (!input.empty()) {
        fail("payload has trailing bytes");
    }
    validate(artifact);
    return artifact;
}

std::string encode_file_unchecked(
    const Artifact& artifact) {
    const std::string payload =
        encode_payload_unchecked(artifact);
    Writer output;
    output.raw(std::string_view(
        kMagic.data(), kMagic.size()));
    output.u32(kWireVersion);
    output.u32(kEndianMarker);
    output.text(kSchema);
    output.u64(payload.size());
    output.hash(digest_string(payload));
    output.raw(payload);
    return output.take();
}

Artifact decode_file(std::string_view bytes) {
    if (bytes.size() > kMaximumArtifactBytes) {
        fail("file exceeds byte limit");
    }
    Reader input(bytes);
    const std::string_view magic =
        input.view(kMagic.size(), "magic");
    if (!std::equal(
            magic.begin(), magic.end(), kMagic.begin())) {
        fail("magic mismatch");
    }
    if (input.u32("wire version") != kWireVersion) {
        fail("wire version mismatch");
    }
    if (input.u32("endian marker") != kEndianMarker) {
        fail("endian marker mismatch");
    }
    if (input.text("schema") != kSchema) {
        fail("schema mismatch");
    }
    const std::size_t payload_size =
        read_size(
            input, "payload size",
            kMaximumArtifactBytes);
    const Hash256 expected_hash =
        input.hash("payload hash");
    const std::string_view payload =
        input.view(payload_size, "payload");
    if (!input.empty()) {
        fail("file has trailing bytes");
    }
    if (digest_string(payload) != expected_hash) {
        fail("payload SHA-256 mismatch");
    }
    return decode_payload(payload);
}

void require_finite_bits(
    std::uint64_t bits, std::string_view context) {
    if (!std::isfinite(std::bit_cast<double>(bits))) {
        fail(std::string(context) + " is nonfinite");
    }
}

void add_score_accounting(
    fq4_dev_bundle::ScoreAccounting& total,
    const fq4_dev_bundle::ScoreAccounting& addend) {
    const auto add_checked =
        [](std::uint64_t& target, std::uint64_t value) {
            if (value >
                std::numeric_limits<std::uint64_t>::max() -
                    target) {
                fail("score accounting overflows");
            }
            target += value;
        };
    add_checked(total.score_calls, addend.score_calls);
    add_checked(total.scored_actions, addend.scored_actions);
    add_checked(total.sampled_worlds, addend.sampled_worlds);
    add_checked(
        total.rollout_evaluations,
        addend.rollout_evaluations);
    add_checked(
        total.terminal_evaluations,
        addend.terminal_evaluations);
    add_checked(
        total.bootstrap_evaluations,
        addend.bootstrap_evaluations);
}

void validate_row(
    const NeutralRow& row,
    fq4_dev_bundle::ScoreAccounting& aggregate) {
    validate_eligible_root(row.locator.root);
    if (row.locator.representative_rank !=
            representative_rank(row.locator.root.rank) ||
        row.locator.game_rank !=
            game_rank(row.locator.root.rank)) {
        fail("stored row rank does not reproduce");
    }
    require_hash(
        row.information_action_sha256,
        "information/action digest");
    require_hash(
        row.descriptor_set_sha256,
        "descriptor-set digest");
    if (row.locator.root.rank.stable_root_id !=
            fq4_dev_bundle::expected_stable_root_sha256(
                row.locator.root.rank.split,
                row.locator.root.rank.schedule_block,
                row.locator.root.schedule_index,
                row.locator.root.owner_seat,
                row.locator.root.trace_ordinal,
                row.information_action_sha256) ||
        row.production_seed !=
            production_seed_for_stable_root(
                row.locator.root.rank.stable_root_id) ||
        row.hidden_clone_eligible !=
            row.hidden_clone_distinct ||
        row.actions.size() !=
            row.locator.root.legal_action_count ||
        row.actions.size() < 2 ||
        row.actions.size() > kMaximumActions ||
        row.pass_index >= row.actions.size()) {
        fail("neutral row shape is invalid");
    }
    std::size_t pass_count = 0;
    for (std::size_t index = 0;
         index < row.actions.size(); ++index) {
        const NeutralAction& action = row.actions[index];
        if (action.is_pass) {
            ++pass_count;
            if (index != row.pass_index) {
                fail("typed Pass disagrees with Pass index");
            }
        }
        if (action.dominance.complete >
                fq4_dev_bundle::kWorldCount ||
            action.dominance.strict >
                action.dominance.complete ||
            (index == row.pass_index &&
             (action.dominance.complete != 0 ||
              action.dominance.strict != 0)) ||
            (index != row.pass_index &&
             action.dominance.complete ==
                 fq4_dev_bundle::kWorldCount &&
             action.dominance.strict ==
                 fq4_dev_bundle::kWorldCount)) {
            fail("neutral dominance facts are invalid");
        }

        double shallow_mean = 0.0;
        for (std::size_t world = 0;
             world < fq4_dev_bundle::kWorldCount;
             ++world) {
            const std::uint64_t raw_bits =
                action.raw_sample_bits[world];
            const std::uint64_t shallow_bits =
                action.shallow_prior_sample_bits[world];
            const std::uint64_t continuation_bits =
                action.continuation_sample_bits[world];
            require_finite_bits(raw_bits, "raw sample");
            require_finite_bits(
                shallow_bits, "shallow sample");
            require_finite_bits(
                continuation_bits, "continuation sample");
            const double raw =
                std::bit_cast<double>(raw_bits);
            const double shallow =
                std::bit_cast<double>(shallow_bits);
            const double continuation =
                std::bit_cast<double>(continuation_bits);
            if (raw < 0.0 || raw > 1.0 ||
                shallow < 0.0 || shallow > 1.0 ||
                continuation < 0.0 ||
                continuation > 1.0) {
                fail("score sample is outside [0, 1]");
            }
            const double expected_raw =
                (shallow +
                 static_cast<double>(
                     fq4_dev_bundle::kWorldCount) *
                     continuation) /
                static_cast<double>(
                    fq4_dev_bundle::kWorldCount + 1U);
            if (raw_bits !=
                std::bit_cast<std::uint64_t>(
                    expected_raw)) {
                fail("raw score blend is not bit exact");
            }
            shallow_mean += shallow;
        }
        shallow_mean /=
            static_cast<double>(
                fq4_dev_bundle::kWorldCount);
        double expected_base = shallow_mean;
        for (std::uint64_t bits :
             action.continuation_sample_bits) {
            expected_base += std::bit_cast<double>(bits);
        }
        expected_base /=
            static_cast<double>(
                fq4_dev_bundle::kWorldCount + 1U);
        require_finite_bits(
            action.base_score_bits, "base score");
        require_finite_bits(
            action.parent_residual_bits,
            "parent residual");
        const double base =
            std::bit_cast<double>(action.base_score_bits);
        const double residual =
            std::bit_cast<double>(
                action.parent_residual_bits);
        if (base < 0.0 || base > 1.0 ||
            action.base_score_bits !=
                std::bit_cast<std::uint64_t>(
                    expected_base) ||
            std::abs(residual) > 0.10) {
            fail("base score or parent residual is invalid");
        }
        if (action.features.size() > kMaximumFeatures) {
            fail("sparse feature count exceeds its bound");
        }
        for (std::size_t feature = 0;
             feature < action.features.size(); ++feature) {
            const auto value = action.features[feature];
            if (value.index >=
                    fq4_dev_bundle::kFeatureCount ||
                value.index ==
                    fq4_dev_coverage_census::
                        kStackSizeFeatureIndex ||
                value.value_bits == 0 ||
                (feature != 0 &&
                 action.features[feature - 1].index >=
                     value.index)) {
                fail("sparse feature encoding is invalid");
            }
            require_finite_bits(
                value.value_bits, "sparse feature");
        }
    }
    if (pass_count != 1) {
        fail("neutral row does not contain one typed Pass");
    }
    const std::uint64_t action_count =
        static_cast<std::uint64_t>(row.actions.size());
    const std::uint64_t rollout_count =
        action_count * fq4_dev_bundle::kWorldCount;
    if (row.accounting.score_calls != 1 ||
        row.accounting.scored_actions != action_count ||
        row.accounting.sampled_worlds !=
            fq4_dev_bundle::kWorldCount ||
        row.accounting.rollout_evaluations !=
            rollout_count ||
        row.accounting.terminal_evaluations >
            rollout_count ||
        row.accounting.bootstrap_evaluations !=
            rollout_count -
                row.accounting.terminal_evaluations) {
        fail("neutral row score accounting is inconsistent");
    }
    add_score_accounting(aggregate, row.accounting);
}

void validate_reconstruction_ledger(
    const ReconstructionLedger& ledger) {
    const auto& score = ledger.parent_scoring;
    if (ledger.source_games != kExpectedSourceGames ||
        ledger.retained_roots != kExpectedRetainedRoots ||
        ledger.retained_options !=
            kExpectedRetainedOptions ||
        score.score_calls !=
            kExpectedReconstructionRows ||
        score.scored_actions !=
            kExpectedReconstructionActions ||
        score.sampled_worlds !=
            kExpectedReconstructionWorlds ||
        score.rollout_evaluations !=
            kExpectedReconstructionRollouts ||
        score.terminal_evaluations !=
            kExpectedReconstructionTerminal ||
        score.bootstrap_evaluations !=
            kExpectedReconstructionBootstrap ||
        score.terminal_evaluations +
                score.bootstrap_evaluations !=
            score.rollout_evaluations) {
        fail("reconstruction ledger disagrees with DEV4");
    }
}

void validate_publisher_accounting(
    const PublisherAccounting& accounting,
    const fq4_dev_bundle::ScoreAccounting& row_total,
    const std::array<
        std::array<std::uint16_t, kDeckCount>,
        kSplitCount>& observed_distinct,
    const std::array<
        std::array<std::uint16_t, kDeckCount>,
        kSplitCount>& observed_nondistinct) {
    validate_reconstruction_ledger(
        accounting.reconstruction);
    const auto& canonical = accounting.canonical_neutral;
    if (canonical != row_total ||
        accounting.hidden_clone != canonical ||
        accounting.distinct_hidden_controls !=
            observed_distinct ||
        accounting.nondistinct_hidden_controls !=
            observed_nondistinct ||
        canonical.score_calls != kTotalRows ||
        canonical.sampled_worlds !=
            kTotalRows * fq4_dev_bundle::kWorldCount ||
        canonical.rollout_evaluations !=
            canonical.scored_actions *
                fq4_dev_bundle::kWorldCount ||
        canonical.terminal_evaluations +
                canonical.bootstrap_evaluations !=
            canonical.rollout_evaluations ||
        accounting.bit_identical_actions !=
            canonical.scored_actions ||
        !accounting.selection_frozen_before_scoring ||
        !accounting.dev1_scientific_sections_exact ||
        !accounting.canonical_hidden_bit_identical ||
        !accounting.parent_immutable ||
        !accounting.bundle_immutable ||
        !accounting.executable_immutable ||
        accounting.parent_models_loaded != 1 ||
        accounting.fits != 0 ||
        accounting.candidate_rollout_evaluations != 0 ||
        accounting.gameplay_evaluation_seeds != 0) {
        fail("publisher accounting is inconsistent");
    }
    std::size_t hidden_controls = 0;
    for (std::size_t split = 0;
         split < kSplitCount; ++split) {
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const std::size_t cell =
                accounting.distinct_hidden_controls[split][deck] +
                accounting.nondistinct_hidden_controls[split][deck];
            if (cell != kRowsPerDeckAndSplit) {
                fail("hidden-control split/deck cross-sum is not 32");
            }
            hidden_controls += cell;
        }
    }
    if (hidden_controls != kTotalRows) {
        fail("hidden-control cross-sum is not 320");
    }
}

} // namespace

void validate(const Artifact& artifact) {
    validate_contract(artifact.manifest.contract);
    if (!canonical_git_commit(
            artifact.manifest.producer_commit)) {
        fail("producer commit is not lower-case 40-hex");
    }
    require_hash(
        artifact.manifest.producer_executable_sha256,
        "producer executable digest");
    require_hash(
        artifact.manifest.selected_order_sha256,
        "selected-order digest");
    if (artifact.rows.size() != kTotalRows) {
        fail("artifact does not contain 320 rows");
    }

    std::vector<RankedLocator> locators;
    locators.reserve(artifact.rows.size());
    std::set<Hash256> stable_roots;
    std::set<std::tuple<
        std::uint8_t, std::uint8_t, std::uint16_t,
        std::uint8_t, std::uint32_t>>
        public_locators;
    std::array<
        std::array<
            std::array<std::size_t, kBlockCount>,
            kDeckCount>,
        kSplitCount>
        by_block{};
    std::array<
        std::array<
            std::array<std::set<Hash256>, kBlockCount>,
            kDeckCount>,
        kSplitCount>
        physical_games_by_block{};
    std::array<
        std::array<
            std::array<
                std::array<std::size_t, kDeckCount>,
                kBlockCount>,
            kDeckCount>,
        kSplitCount>
        by_opponent{};
    std::array<
        std::array<
            std::array<
                std::array<std::size_t, kQuadrantCount>,
                kBlockCount>,
            kDeckCount>,
        kSplitCount>
        by_quadrant{};
    fq4_dev_bundle::ScoreAccounting row_total;
    std::array<
        std::array<std::uint16_t, kDeckCount>,
        kSplitCount>
        observed_distinct{};
    std::array<
        std::array<std::uint16_t, kDeckCount>,
        kSplitCount>
        observed_nondistinct{};

    for (std::size_t index = 0;
         index < artifact.rows.size(); ++index) {
        const NeutralRow& row = artifact.rows[index];
        validate_row(row, row_total);
        if (index != 0 &&
            !locator_order(
                artifact.rows[index - 1].locator,
                row.locator)) {
            fail("artifact rows are not in canonical order");
        }
        if (!stable_roots
                 .insert(row.locator.root.rank.stable_root_id)
                 .second ||
            !public_locators
                 .emplace(
                     split_byte(row.locator.root.rank.split),
                     row.locator.root.rank.schedule_block,
                     row.locator.root.schedule_index,
                     row.locator.root.owner_seat,
                     row.locator.root.trace_ordinal)
                 .second) {
            fail("artifact locator is duplicated");
        }
        const std::size_t split =
            split_byte(row.locator.root.rank.split);
        const std::size_t owner =
            row.locator.root.rank.owner_deck;
        const std::size_t block =
            row.locator.root.rank.schedule_block;
        std::uint16_t& hidden_count =
            row.hidden_clone_distinct
                ? observed_distinct[split][owner]
                : observed_nondistinct[split][owner];
        if (hidden_count ==
            std::numeric_limits<std::uint16_t>::max()) {
            fail("hidden-control count overflows");
        }
        ++hidden_count;
        ++by_block[split][owner][block];
        physical_games_by_block[split][owner][block]
            .insert(
                row.locator.root.rank
                    .physical_game_sha256);
        ++by_opponent[split][owner][block]
                     [row.locator.root.opponent_deck];
        ++by_quadrant[split][owner][block]
                     [quadrant(row.locator.root)];
        locators.push_back(row.locator);
    }

    for (std::size_t split = 0;
         split < kSplitCount; ++split) {
        for (std::size_t owner = 0;
             owner < kDeckCount; ++owner) {
            std::size_t deck_rows = 0;
            for (std::size_t block = 0;
                 block < kBlockCount; ++block) {
                if (by_block[split][owner][block] !=
                        kRowsPerBlock ||
                    physical_games_by_block
                            [split][owner][block]
                                .size() !=
                        kRowsPerBlock) {
                    fail(
                        "artifact block does not contain eight "
                        "distinct physical games");
                }
                deck_rows += by_block[split][owner][block];
                for (std::size_t opponent = 0;
                     opponent < kDeckCount; ++opponent) {
                    const std::size_t expected =
                        opponent == owner
                            ? 0
                            : kRowsPerOpponent;
                    if (by_opponent[split][owner][block]
                                   [opponent] != expected) {
                        fail("artifact opponent balance is invalid");
                    }
                }
                for (std::size_t game_quadrant = 0;
                     game_quadrant < kQuadrantCount;
                     ++game_quadrant) {
                    if (by_quadrant[split][owner][block]
                                   [game_quadrant] !=
                        kRowsPerQuadrant) {
                        fail("artifact quadrant balance is invalid");
                    }
                }
            }
            if (deck_rows != kRowsPerDeckAndSplit) {
                fail("artifact deck count is not 32");
            }
        }
    }
    const Hash256 selected_digest =
        selected_order_sha256(locators);
    if (selected_digest !=
        artifact.manifest.selected_order_sha256) {
        fail("selected-order digest mismatch");
    }
    validate_publisher_accounting(
        artifact.manifest.accounting, row_total,
        observed_distinct, observed_nondistinct);
}

std::string encode(const Artifact& artifact) {
    validate(artifact);
    const std::string bytes =
        encode_file_unchecked(artifact);
    if (decode_file(bytes) != artifact) {
        throw std::runtime_error(
            "FQ4 neutral encoder self-check failed");
    }
    return bytes;
}

Artifact decode(std::string_view bytes) {
    return decode_file(bytes);
}

std::string testing::encode_wire_unchecked(
    const Artifact& artifact) {
    return encode_file_unchecked(artifact);
}

namespace {

[[noreturn]] void throw_errno(
    std::string_view operation,
    const std::filesystem::path& path, int error) {
    throw std::system_error(
        error, std::generic_category(),
        std::string(operation) + " '" + path.string() + "'");
}

void require_absent(
    const std::filesystem::path& path,
    std::string_view context) {
    struct stat status {};
    if (::lstat(path.c_str(), &status) == 0) {
        throw std::runtime_error(
            std::string(context) + " already exists: '" +
            path.string() + "'");
    }
    const int error = errno;
    if (error != ENOENT) {
        throw_errno(
            std::string("cannot inspect ") +
                std::string(context),
            path, error);
    }
}

std::filesystem::path temporary_path_impl(
    const std::filesystem::path& destination) {
    if (destination.empty() ||
        destination.filename().empty()) {
        throw std::invalid_argument(
            "FQ4 neutral destination is empty");
    }
    const std::filesystem::path parent =
        destination.has_parent_path()
            ? destination.parent_path()
            : std::filesystem::path(".");
    return parent /
           ("." + destination.filename().string() +
            ".publishing.tmp");
}

class TemporaryPublication {
  public:
    explicit TemporaryPublication(
        std::filesystem::path path)
        : path_(std::move(path)) {}

    TemporaryPublication(
        const TemporaryPublication&) = delete;
    TemporaryPublication& operator=(
        const TemporaryPublication&) = delete;

    ~TemporaryPublication() {
        if (!path_.empty()) {
            static_cast<void>(::unlink(path_.c_str()));
        }
    }

    void release() {
        path_.clear();
    }

  private:
    std::filesystem::path path_;
};

void close_checked(
    int descriptor, const std::filesystem::path& path) {
    while (::close(descriptor) != 0) {
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        throw_errno("cannot close", path, error);
    }
}

void write_all(
    int descriptor, std::string_view bytes,
    const std::filesystem::path& path) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written =
            ::write(
                descriptor, bytes.data() + offset,
                bytes.size() - offset);
        if (written < 0) {
            const int error = errno;
            if (error == EINTR) {
                continue;
            }
            throw_errno("cannot write", path, error);
        }
        if (written == 0) {
            throw std::runtime_error(
                "zero-length FQ4 neutral write");
        }
        offset += static_cast<std::size_t>(written);
    }
}

FileIdentity publish_bytes(
    const std::filesystem::path& destination,
    std::string_view bytes) {
    if (bytes.empty() ||
        bytes.size() > kMaximumArtifactBytes) {
        throw std::invalid_argument(
            "FQ4 neutral publication bytes are invalid");
    }
    const std::filesystem::path temporary =
        temporary_path_impl(destination);
    const std::filesystem::path parent =
        destination.has_parent_path()
            ? destination.parent_path()
            : std::filesystem::path(".");
    std::error_code status_error;
    const auto parent_status =
        std::filesystem::symlink_status(
            parent, status_error);
    if (status_error ||
        !std::filesystem::is_directory(parent_status) ||
        std::filesystem::is_symlink(parent_status)) {
        throw std::runtime_error(
            "FQ4 neutral publication parent is not a "
            "non-symlink directory");
    }
    require_absent(destination, "destination");
    require_absent(temporary, "publication temporary");

    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int descriptor = -1;
    while (descriptor < 0) {
        descriptor =
            ::open(temporary.c_str(), flags, 0644);
        if (descriptor < 0 && errno != EINTR) {
            throw_errno(
                "cannot create publication temporary",
                temporary, errno);
        }
    }
    TemporaryPublication cleanup(temporary);
    try {
        write_all(descriptor, bytes, temporary);
        while (::fsync(descriptor) != 0) {
            const int error = errno;
            if (error == EINTR) {
                continue;
            }
            throw_errno(
                "cannot sync publication temporary",
                temporary, error);
        }
        close_checked(descriptor, temporary);
        descriptor = -1;
    } catch (...) {
        if (descriptor >= 0) {
            static_cast<void>(::close(descriptor));
        }
        throw;
    }

    if (::link(
            temporary.c_str(),
            destination.c_str()) != 0) {
        throw_errno(
            "cannot publish artifact without replacement",
            destination, errno);
    }
    if (::unlink(temporary.c_str()) != 0) {
        throw_errno(
            "cannot remove publication temporary",
            temporary, errno);
    }
    cleanup.release();

    int directory_flags = O_RDONLY;
#ifdef O_CLOEXEC
    directory_flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECTORY
    directory_flags |= O_DIRECTORY;
#endif
    const int directory_descriptor =
        ::open(parent.c_str(), directory_flags);
    if (directory_descriptor < 0) {
        throw_errno(
            "cannot open publication directory",
            parent, errno);
    }
    if (::fsync(directory_descriptor) != 0) {
        const int error = errno;
        static_cast<void>(::close(directory_descriptor));
        throw_errno(
            "cannot sync publication directory",
            parent, error);
    }
    close_checked(directory_descriptor, parent);

    const auto snapshot =
        artifact_integrity::snapshot_regular_file(
            destination);
    const FileIdentity identity{
        .bytes =
            static_cast<std::uint64_t>(
                snapshot.byte_size),
        .sha256 = snapshot.sha256,
    };
    if (identity.bytes != bytes.size() ||
        identity.sha256 !=
            artifact_integrity::sha256_string(bytes)) {
        throw std::runtime_error(
            "published FQ4 neutral identity mismatch");
    }
    return identity;
}

std::string read_exact_file(
    const std::filesystem::path& path,
    std::uint64_t byte_count) {
    if (byte_count == 0 ||
        byte_count > kMaximumArtifactBytes ||
        byte_count >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::streamsize>::max())) {
        fail("expected artifact byte count is invalid");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open FQ4 neutral artifact");
    }
    std::string bytes(
        static_cast<std::size_t>(byte_count), '\0');
    input.read(
        bytes.data(),
        static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() !=
            static_cast<std::streamsize>(bytes.size()) ||
        input.peek() !=
            std::char_traits<char>::eof()) {
        throw std::runtime_error(
            "FQ4 neutral artifact length changed");
    }
    return bytes;
}

PublicationReport publish_at(
    const std::filesystem::path& destination,
    const Artifact& artifact) {
    const std::string bytes = encode(artifact);
    const Artifact decoded = decode(bytes);
    if (decoded != artifact) {
        throw std::runtime_error(
            "FQ4 neutral publication self-check failed");
    }
    return {
        .artifact = publish_bytes(destination, bytes),
        .manifest = artifact.manifest,
    };
}

Artifact load_from_impl(
    const std::filesystem::path& path,
    const Contract& expected_contract,
    const FileIdentity& expected_identity) {
    validate_contract(expected_contract);
    validate_file_identity(
        expected_identity, "expected artifact");
    const auto before =
        artifact_integrity::snapshot_regular_file(path);
    if (before.byte_size != expected_identity.bytes ||
        before.sha256 != expected_identity.sha256) {
        throw std::runtime_error(
            "FQ4 neutral artifact identity mismatch");
    }
    const std::string bytes =
        read_exact_file(path, expected_identity.bytes);
    const auto after =
        artifact_integrity::snapshot_regular_file(path);
    if (before != after ||
        artifact_integrity::sha256_string(bytes) !=
            expected_identity.sha256) {
        throw std::runtime_error(
            "FQ4 neutral artifact changed while loading");
    }
    Artifact artifact = decode(bytes);
    if (artifact.manifest.contract != expected_contract) {
        fail("stored contract does not match expected contract");
    }
    return artifact;
}

} // namespace

std::filesystem::path production_artifact_path() {
    return std::filesystem::path(kProductionArtifactPath);
}

std::filesystem::path production_temporary_path() {
    return temporary_path_impl(production_artifact_path());
}

PublicationReport publish_atomic_no_replace(
    const Artifact& artifact) {
    return publish_at(production_artifact_path(), artifact);
}

Artifact load_published(
    const Contract& expected_contract,
    const FileIdentity& expected_identity) {
    return load_from_impl(
        production_artifact_path(),
        expected_contract, expected_identity);
}

std::filesystem::path testing::temporary_path_for(
    const std::filesystem::path& destination) {
    return temporary_path_impl(destination);
}

PublicationReport testing::publish_atomic_no_replace_at(
    const std::filesystem::path& destination,
    const Artifact& artifact) {
    return publish_at(destination, artifact);
}

Artifact testing::load_from(
    const std::filesystem::path& path,
    const Contract& expected_contract,
    const FileIdentity& expected_identity) {
    return load_from_impl(
        path, expected_contract, expected_identity);
}

} // namespace old_school::fq4_neutral_supplement
