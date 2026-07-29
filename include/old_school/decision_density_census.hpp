#pragma once

#include "old_school/game.hpp"
#include "old_school/learned_iteration.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::decision_density_census {

inline constexpr std::string_view kIdentifier =
    "AQ16-DBC6-DENSITY-CENSUS";
inline constexpr std::uint64_t kCollectionRootSeed =
    202607291801ULL;
inline constexpr std::size_t kScheduleGeneration = 15;
inline constexpr std::array<std::size_t, 2> kTrainBlocks{
    0, 1,
};
inline constexpr std::size_t kDevBlock = 2;
inline constexpr std::size_t kSourceTurnCap = 128;
inline constexpr std::size_t kSourceWorlds = 8;
inline constexpr std::size_t kSourceRolloutsPerWorld =
    kLearnedValueSearchRolloutsPerWorld;
inline constexpr std::size_t kSourceHorizonTurns =
    kLearnedValueSearchHorizonTurns;
inline constexpr std::size_t kPolicyFeatureCount = 893;
inline constexpr std::size_t kGamesPerBlock =
    learned_iteration::kBalancedScheduleGames;
inline constexpr std::size_t kTrainGames =
    kGamesPerBlock * kTrainBlocks.size();
inline constexpr std::size_t kDevGames = kGamesPerBlock;
inline constexpr std::size_t kTrainActorGames =
    kTrainGames * 2;
inline constexpr std::size_t kDevActorGames =
    kDevGames * 2;
inline constexpr std::size_t kTrainActorGamesPerDeck = 32;
inline constexpr std::size_t kDevActorGamesPerDeck = 16;
inline constexpr std::string_view kRequiredParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";

static_assert(kGamesPerBlock == 40);
static_assert(kSourceRolloutsPerWorld == 1);
static_assert(kSourceHorizonTurns == 4);
static_assert(kTrainGames == 80);
static_assert(kDevGames == 40);
static_assert(kTrainActorGamesPerDeck * kDeckCount ==
              kTrainActorGames);
static_assert(kDevActorGamesPerDeck * kDeckCount ==
              kDevActorGames);

enum class Command : std::uint8_t {
    Census,
};

enum class Split : std::uint8_t {
    Train = 0,
    Dev = 1,
};

struct RootCoordinate {
    Split split = Split::Train;
    std::size_t block_index = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t starting_player = 0;
    std::array<DeckId, 2> seat_decks{
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t actor = 0;
    std::size_t trace_ordinal = 0;
    std::size_t nontrivial_ordinal = 0;
    std::size_t actor_game_nontrivial_roots = 0;

    DeckId owner_deck() const;
    bool operator==(const RootCoordinate&) const = default;
};

// The manifest retains only actor-local/public information. Feature rows are
// validated and hashed during collection, then discarded.
struct ManifestRoot {
    RootCoordinate coordinate;
    std::size_t legal_action_count = 0;
    std::size_t potential_pairs = 0;
    std::vector<std::string> action_descriptors;
    std::string information_action_fingerprint;
    std::string stable_root_id;

    bool operator==(const ManifestRoot&) const = default;
};

struct WidthCensus {
    std::size_t legal_action_count = 0;
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t potential_pairs = 0;

    bool operator==(const WidthCensus&) const = default;
};

struct ActorGameCensus {
    Split split = Split::Train;
    std::size_t block_index = 0;
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::uint64_t game_seed = 0;
    std::size_t starting_player = 0;
    std::array<DeckId, 2> seat_decks{
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t actor = 0;
    DeckId owner_deck = DeckId::Green;
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t potential_pairs = 0;
    std::vector<WidthCensus> widths;

    bool operator==(const ActorGameCensus&) const = default;
};

struct DeckCensus {
    std::size_t actor_games = 0;
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t potential_pairs = 0;
    std::vector<WidthCensus> widths;

    bool operator==(const DeckCensus&) const = default;
};

struct SplitCensus {
    Split split = Split::Train;
    std::size_t games = 0;
    std::size_t actor_games = 0;
    std::size_t roots = 0;
    std::size_t options = 0;
    std::size_t potential_pairs = 0;
    std::array<DeckCensus, kDeckCount> decks{};
    std::vector<ActorGameCensus> actor_game_rows;
    std::vector<WidthCensus> widths;

    bool operator==(const SplitCensus&) const = default;
};

struct Census {
    std::uint64_t root_seed = kCollectionRootSeed;
    std::string parent_fingerprint;
    std::array<SplitCensus, 2> splits{
        SplitCensus{.split = Split::Train},
        SplitCensus{.split = Split::Dev},
    };
    std::vector<ManifestRoot> roots;
    std::string manifest_hash;

    bool operator==(const Census&) const = default;
};

struct Collection {
    Census census;
    bool hidden_repartition_witness = false;
    std::string hidden_witness_root_id;

    bool operator==(const Collection&) const = default;
};

// These references and spans are non-owning and remain valid only for the
// duration of an AuthenticatedRootVisitor call. The callback is a research
// replay seam; Collection and the frozen AQ16 manifest continue to retain no
// actions, feature rows, GameState, or hidden cards.
struct AuthenticatedRootView {
    const ManifestRoot& manifest;
    std::span<const double> observation;
    std::span<const PriorityAction> actions;
    std::span<const std::vector<double>> option_rows;
    bool hidden_repartition_witness = false;
};

using AuthenticatedRootVisitor =
    std::function<void(const AuthenticatedRootView&)>;

// A production-only replay seam for consumers that must evaluate an already
// frozen source root. Every reference remains valid only for the duration of
// the callback. The frozen Census and returned Collection continue to retain
// no GameState, trace, original deck vector, or hidden-zone payload.
struct AuthenticatedReplayRootView {
    const ManifestRoot& manifest;
    std::span<const double> observation;
    std::span<const PriorityAction> actions;
    std::span<const std::vector<double>> option_rows;
    const LearnedDecisionTracePoint& trace_point;
    const std::array<std::vector<CardId>, 2>& original_decks;
    bool hidden_repartition_witness = false;
};

using AuthenticatedReplayRootVisitor =
    std::function<void(const AuthenticatedReplayRootView&)>;

struct RunReport {
    Census census;
    bool repeated_collection_bit_identical = false;
    bool hidden_repartition_witness = false;
    std::string hidden_witness_root_id;
    std::size_t source_collections = 0;
};

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments);
void print_usage(std::ostream& output);

std::size_t split_index(Split split);
std::size_t potential_pair_count(std::size_t legal_actions);
GameConfig source_game_config(
    std::shared_ptr<const LearnedModel> parent,
    std::size_t starting_player);

std::string canonical_information_action_fingerprint(
    std::span<const PriorityAction> actions,
    std::span<const std::string> descriptors,
    std::span<const std::vector<double>> option_rows);
std::string stable_root_id(
    const RootCoordinate& coordinate,
    std::span<const std::string> action_descriptors,
    std::string_view information_action_fingerprint,
    std::size_t legal_action_count,
    std::size_t potential_pairs);
std::string canonical_manifest_hash(const Census& census);

void validate_manifest_root(const ManifestRoot& root);
void validate_census(const Census& census);

Collection collect_census(
    std::shared_ptr<const LearnedModel> parent);
Collection collect_census(
    std::shared_ptr<const LearnedModel> parent,
    const AuthenticatedRootVisitor& visitor);
Collection replay_frozen_census(
    std::shared_ptr<const LearnedModel> parent,
    const Census& frozen,
    const AuthenticatedReplayRootVisitor& visitor);

// Evaluation-only hidden-information witness. The returned state differs
// solely by a repartition of the opponent's hand/library and is guaranteed
// to preserve the observer's complete game observation.
std::optional<GameState> make_actor_local_hidden_repartition(
    const GameState& state, std::size_t observer);
RunReport run(std::shared_ptr<const LearnedModel> parent);
void print_report(
    std::ostream& output, const RunReport& report);

namespace testing {

void validate_schedule_block(
    std::size_t block_index,
    std::span<const learned_iteration::ScheduledGame> games);
ManifestRoot make_manifest_root(
    const RootCoordinate& coordinate,
    std::vector<PriorityAction> actions,
    std::vector<std::vector<double>> option_rows);
Census make_census(
    std::string parent_fingerprint,
    std::vector<ManifestRoot> roots);
void validate_frozen_replay_root(
    const Census& frozen, std::size_t position,
    const ManifestRoot& replayed);
std::optional<GameState> hidden_repartition(
    const GameState& state, std::size_t observer);
ManifestRoot make_live_manifest_root(
    const LearnedDecisionTracePoint& point,
    const RootCoordinate& coordinate);

} // namespace testing

} // namespace old_school::decision_density_census
