#include "old_school/fq4_work0_cache.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/fq4_dev_schedule.hpp"
#include "old_school/fq4_neutral_supplement.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::fq4_work0_cache {
namespace {

namespace bundle = fq4_dev_bundle;
namespace collection = fq4_priority_collection;
namespace information = fq0_information_set;
namespace neutral = fq4_neutral_supplement;
namespace schedule = fq4_dev_schedule;

constexpr std::array<char, 8> kMagic{
    'O', 'S', 'F', 'Q', '4', 'W', '0', '\0',
};
constexpr std::uint32_t kWireVersion = 1;
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::size_t kMaximumTextBytes = 4096;
constexpr std::size_t kMaximumCards = 512;
constexpr std::size_t kMaximumObjects = 512;
constexpr std::size_t kMaximumTurnNumber = 1'000'000;
constexpr int kMaximumMana = 512;
constexpr int kMaximumSafeGameInteger = 1'000'000;
constexpr std::uint64_t kMaximumObjectId = 1'000'000;
constexpr std::uint64_t kValidationWorldSeed =
    0x465134574f524b30ULL;

[[noreturn]] void fail(std::string_view message) {
    throw std::invalid_argument(
        "invalid FQ4 WORK0 root cache: " +
        std::string(message));
}

bool nonzero_hash(const Hash256& digest) {
    return std::any_of(
        digest.begin(), digest.end(),
        [](std::uint8_t byte) { return byte != 0; });
}

bool canonical_text(
    std::string_view value,
    std::size_t maximum = kMaximumTextBytes) {
    return !value.empty() && value.size() <= maximum &&
           std::all_of(
               value.begin(), value.end(),
               [](unsigned char character) {
                   return character >= 0x20U &&
                          character <= 0x7eU;
               });
}

class Writer {
  public:
    explicit Writer(
        std::size_t maximum = kMaximumCacheBytes)
        : maximum_(maximum) {}

    void u8(std::uint8_t value) {
        ensure(1);
        bytes_.push_back(static_cast<char>(value));
    }

    void boolean(bool value) {
        u8(value ? 1U : 0U);
    }

    void u32(std::uint32_t value) {
        ensure(4);
        for (std::size_t offset = 0; offset < 4; ++offset) {
            bytes_.push_back(static_cast<char>(
                static_cast<std::uint8_t>(
                    value >> (offset * 8U))));
        }
    }

    void u64(std::uint64_t value) {
        ensure(8);
        for (std::size_t offset = 0; offset < 8; ++offset) {
            bytes_.push_back(static_cast<char>(
                static_cast<std::uint8_t>(
                    value >> (offset * 8U))));
        }
    }

    void i32(int value) {
        static_assert(sizeof(int) == sizeof(std::uint32_t));
        u32(static_cast<std::uint32_t>(value));
    }

    void hash(const Hash256& digest) {
        raw(std::string_view(
            reinterpret_cast<const char*>(digest.data()),
            digest.size()));
    }

    void raw(std::string_view value) {
        ensure(value.size());
        bytes_.append(value.data(), value.size());
    }

    void text(std::string_view value) {
        if (!canonical_text(value) ||
            value.size() >
                std::numeric_limits<std::uint32_t>::max()) {
            fail("text is not canonical ASCII");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        raw(value);
    }

    const std::string& bytes() const {
        return bytes_;
    }

    std::string take() {
        return std::move(bytes_);
    }

  private:
    void ensure(std::size_t extra) const {
        if (extra > maximum_ - bytes_.size()) {
            fail("wire exceeds its byte bound");
        }
    }

    std::size_t maximum_;
    std::string bytes_;
};

class Reader {
  public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    std::uint8_t u8(std::string_view context) {
        require(1, context);
        return static_cast<std::uint8_t>(
            static_cast<unsigned char>(bytes_[position_++]));
    }

    bool boolean(std::string_view context) {
        const std::uint8_t value = u8(context);
        if (value > 1) {
            fail(std::string(context) + " is not boolean");
        }
        return value != 0;
    }

    std::uint32_t u32(std::string_view context) {
        require(4, context);
        std::uint32_t result = 0;
        for (std::size_t offset = 0; offset < 4; ++offset) {
            result |=
                static_cast<std::uint32_t>(
                    static_cast<unsigned char>(
                        bytes_[position_ + offset]))
                << (offset * 8U);
        }
        position_ += 4;
        return result;
    }

    std::uint64_t u64(std::string_view context) {
        require(8, context);
        std::uint64_t result = 0;
        for (std::size_t offset = 0; offset < 8; ++offset) {
            result |=
                static_cast<std::uint64_t>(
                    static_cast<unsigned char>(
                        bytes_[position_ + offset]))
                << (offset * 8U);
        }
        position_ += 8;
        return result;
    }

    int i32(std::string_view context) {
        const std::uint32_t value = u32(context);
        if (value <=
            static_cast<std::uint32_t>(
                std::numeric_limits<int>::max())) {
            return static_cast<int>(value);
        }
        return static_cast<int>(
            static_cast<std::int64_t>(value) -
            (static_cast<std::int64_t>(1) << 32));
    }

    Hash256 hash(std::string_view context) {
        require(32, context);
        Hash256 result{};
        std::copy_n(
            reinterpret_cast<const std::uint8_t*>(
                bytes_.data() + position_),
            result.size(), result.begin());
        position_ += result.size();
        return result;
    }

    std::string text(
        std::string_view context,
        std::size_t maximum = kMaximumTextBytes) {
        const std::size_t size =
            bounded_u32(context, maximum);
        require(size, context);
        std::string result(
            bytes_.substr(position_, size));
        position_ += size;
        if (!canonical_text(result, maximum)) {
            fail(std::string(context) +
                 " is not canonical ASCII");
        }
        return result;
    }

    std::size_t bounded_u32(
        std::string_view context, std::size_t maximum) {
        const std::uint32_t value = u32(context);
        if (value > maximum) {
            fail(std::string(context) + " exceeds its bound");
        }
        return static_cast<std::size_t>(value);
    }

    std::size_t bounded_u64(
        std::string_view context, std::size_t maximum) {
        const std::uint64_t value = u64(context);
        if (value > maximum ||
            value >
                std::numeric_limits<std::size_t>::max()) {
            fail(std::string(context) + " exceeds its bound");
        }
        return static_cast<std::size_t>(value);
    }

    void finish() const {
        if (position_ != bytes_.size()) {
            fail("payload has trailing bytes");
        }
    }

  private:
    void require(
        std::size_t count, std::string_view context) const {
        if (position_ > bytes_.size() ||
            count > bytes_.size() - position_) {
            fail(std::string(context) + " is truncated");
        }
    }

    std::string_view bytes_;
    std::size_t position_ = 0;
};

std::uint8_t card_byte(CardId card) {
    const std::size_t value =
        static_cast<std::size_t>(card);
    if (value >= kCardCount) {
        fail("card ID is invalid");
    }
    return static_cast<std::uint8_t>(value);
}

CardId read_card(Reader& input, std::string_view context) {
    const std::uint8_t value = input.u8(context);
    if (value >= kCardCount) {
        fail(std::string(context) + " card ID is invalid");
    }
    return static_cast<CardId>(value);
}

void write_cards(
    Writer& output, const std::vector<CardId>& cards) {
    if (cards.size() > kMaximumCards) {
        fail("card vector exceeds its bound");
    }
    output.u32(static_cast<std::uint32_t>(cards.size()));
    for (const CardId card : cards) {
        output.u8(card_byte(card));
    }
}

std::vector<CardId> read_cards(
    Reader& input, std::string_view context) {
    const std::size_t count =
        input.bounded_u32(context, kMaximumCards);
    std::vector<CardId> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(read_card(input, context));
    }
    return result;
}

void write_mana(Writer& output, const ManaCost& mana) {
    output.i32(mana.generic);
    output.i32(mana.green);
    output.i32(mana.red);
    output.i32(mana.blue);
    output.i32(mana.white);
}

ManaCost read_mana(Reader& input) {
    return {
        .generic = input.i32("mana generic"),
        .green = input.i32("mana green"),
        .red = input.i32("mana red"),
        .blue = input.i32("mana blue"),
        .white = input.i32("mana white"),
    };
}

void write_target(
    Writer& output,
    const std::optional<Target>& target) {
    output.boolean(target.has_value());
    if (!target.has_value()) {
        return;
    }
    output.u64(target->player);
    output.boolean(target->creature.has_value());
    if (target->creature.has_value()) {
        output.u64(*target->creature);
    }
}

std::optional<Target> read_target(Reader& input) {
    if (!input.boolean("target present")) {
        return std::nullopt;
    }
    Target result;
    result.player =
        input.bounded_u64("target player", 1);
    if (input.boolean("target creature present")) {
        result.creature =
            input.u64("target creature");
    }
    return result;
}

void write_optional_id(
    Writer& output,
    const std::optional<std::uint64_t>& value) {
    output.boolean(value.has_value());
    if (value.has_value()) {
        output.u64(*value);
    }
}

std::optional<std::uint64_t> read_optional_id(
    Reader& input, std::string_view context) {
    if (!input.boolean(
            std::string(context) + " present")) {
        return std::nullopt;
    }
    return input.u64(context);
}

void write_priority_action(
    Writer& output, const PriorityAction& action) {
    output.u8(
        static_cast<std::uint8_t>(action.kind));
    output.u8(card_byte(action.card));
    write_target(output, action.target);
    write_optional_id(output, action.spell_target);
    write_optional_id(output, action.source_permanent);
    output.i32(action.x_value);
}

PriorityAction read_priority_action(Reader& input) {
    const std::uint8_t kind =
        input.u8("Priority action kind");
    if (kind >
        static_cast<std::uint8_t>(
            PriorityActionKind::ActivateMillstone)) {
        fail("Priority action kind is invalid");
    }
    PriorityAction result;
    result.kind =
        static_cast<PriorityActionKind>(kind);
    result.card =
        read_card(input, "Priority action card");
    result.target = read_target(input);
    result.spell_target =
        read_optional_id(input, "spell target");
    result.source_permanent =
        read_optional_id(input, "source permanent");
    result.x_value =
        input.i32("Priority action X");
    return result;
}

void write_public_player(
    Writer& output, const PublicPlayerState& player) {
    output.i32(player.life);
    output.u64(player.library_size);
    output.u64(player.hand_size);
    write_cards(output, player.graveyard);
    write_cards(output, player.exile);
    if (player.lands.size() > kMaximumObjects ||
        player.creatures.size() > kMaximumObjects ||
        player.artifacts.size() > kMaximumObjects) {
        fail("public permanent vector exceeds its bound");
    }
    output.u32(
        static_cast<std::uint32_t>(
            player.lands.size()));
    for (const LandPermanent& land : player.lands) {
        output.u8(card_byte(land.card));
        output.boolean(land.tapped);
    }
    output.u32(
        static_cast<std::uint32_t>(
            player.creatures.size()));
    for (const CreaturePermanent& creature :
         player.creatures) {
        output.u64(creature.id);
        output.u8(card_byte(creature.card));
        output.boolean(creature.tapped);
        output.boolean(creature.summoning_sick);
        output.i32(creature.damage);
        output.i32(creature.temporary_power_bonus);
        output.i32(creature.temporary_toughness_bonus);
        output.boolean(
            creature.exile_on_death_this_turn);
    }
    output.u32(
        static_cast<std::uint32_t>(
            player.artifacts.size()));
    for (const ArtifactPermanent& artifact :
         player.artifacts) {
        output.u64(artifact.id);
        output.u8(card_byte(artifact.card));
        output.boolean(artifact.tapped);
    }
    write_cards(output, player.enchantments);
    write_mana(output, player.mana_pool);
    output.boolean(player.land_played_this_turn);
}

PublicPlayerState read_public_player(Reader& input) {
    PublicPlayerState result;
    result.life = input.i32("player life");
    result.library_size =
        input.bounded_u64("library size", kMaximumCards);
    result.hand_size =
        input.bounded_u64("hand size", kMaximumCards);
    result.graveyard =
        read_cards(input, "graveyard");
    result.exile = read_cards(input, "exile");
    const std::size_t land_count =
        input.bounded_u32("land count", kMaximumObjects);
    result.lands.reserve(land_count);
    for (std::size_t index = 0; index < land_count; ++index) {
        result.lands.push_back({
            .card = read_card(input, "land card"),
            .tapped = input.boolean("land tapped"),
        });
    }
    const std::size_t creature_count =
        input.bounded_u32(
            "creature count", kMaximumObjects);
    result.creatures.reserve(creature_count);
    for (std::size_t index = 0;
         index < creature_count; ++index) {
        result.creatures.push_back({
            .id = input.u64("creature ID"),
            .card =
                read_card(input, "creature card"),
            .tapped =
                input.boolean("creature tapped"),
            .summoning_sick =
                input.boolean(
                    "creature summoning sickness"),
            .damage =
                input.i32("creature damage"),
            .temporary_power_bonus =
                input.i32(
                    "creature temporary power"),
            .temporary_toughness_bonus =
                input.i32(
                    "creature temporary toughness"),
            .exile_on_death_this_turn =
                input.boolean(
                    "creature exile-on-death"),
        });
    }
    const std::size_t artifact_count =
        input.bounded_u32(
            "artifact count", kMaximumObjects);
    result.artifacts.reserve(artifact_count);
    for (std::size_t index = 0;
         index < artifact_count; ++index) {
        result.artifacts.push_back({
            .id = input.u64("artifact ID"),
            .card =
                read_card(input, "artifact card"),
            .tapped =
                input.boolean("artifact tapped"),
        });
    }
    result.enchantments =
        read_cards(input, "enchantments");
    result.mana_pool = read_mana(input);
    result.land_played_this_turn =
        input.boolean("land played");
    return result;
}

void write_stack_object(
    Writer& output, const StackObject& object) {
    output.u8(
        static_cast<std::uint8_t>(object.kind));
    output.u64(object.id);
    output.u8(card_byte(object.card));
    output.u64(object.controller);
    write_target(output, object.target);
    write_optional_id(output, object.spell_target);
    output.i32(object.x_value);
}

StackObject read_stack_object(Reader& input) {
    const std::uint8_t kind =
        input.u8("stack object kind");
    if (kind >
        static_cast<std::uint8_t>(
            StackObjectKind::ActivatedAbility)) {
        fail("stack object kind is invalid");
    }
    return {
        .kind = static_cast<StackObjectKind>(kind),
        .id = input.u64("stack object ID"),
        .card = read_card(input, "stack object card"),
        .controller =
            input.bounded_u64(
                "stack object controller", 1),
        .target = read_target(input),
        .spell_target =
            read_optional_id(
                input, "stack spell target"),
        .x_value = input.i32("stack object X"),
    };
}

void write_owner_state(
    Writer& output, const OwnerVisibleState& state) {
    output.u64(state.observer);
    for (const PublicPlayerState& player :
         state.players) {
        write_public_player(output, player);
    }
    write_cards(output, state.owner_hand);
    if (state.stack.size() > kMaximumObjects) {
        fail("stack exceeds its bound");
    }
    output.u32(
        static_cast<std::uint32_t>(
            state.stack.size()));
    for (const StackObject& object : state.stack) {
        write_stack_object(output, object);
    }
    for (const std::size_t extra :
         state.extra_turns_pending) {
        output.u64(extra);
    }
    output.u64(state.active_player);
    output.u64(state.starting_player);
    output.u64(state.turn_number);
    for (const bool failed : state.failed_draw) {
        output.boolean(failed);
    }
    output.u64(state.next_permanent_id);
    output.u64(state.next_stack_object_id);
}

OwnerVisibleState read_owner_state(Reader& input) {
    OwnerVisibleState result;
    result.observer =
        input.bounded_u64("observer", 1);
    for (PublicPlayerState& player : result.players) {
        player = read_public_player(input);
    }
    result.owner_hand =
        read_cards(input, "owner hand");
    const std::size_t stack_count =
        input.bounded_u32("stack count", kMaximumObjects);
    result.stack.reserve(stack_count);
    for (std::size_t index = 0;
         index < stack_count; ++index) {
        result.stack.push_back(
            read_stack_object(input));
    }
    for (std::size_t& extra :
         result.extra_turns_pending) {
        extra =
            input.bounded_u64(
                "extra turns", kMaximumTurnNumber);
    }
    result.active_player =
        input.bounded_u64("active player", 1);
    result.starting_player =
        input.bounded_u64("starting player", 1);
    result.turn_number =
        input.bounded_u64(
            "turn number", kMaximumTurnNumber);
    for (bool& failed : result.failed_draw) {
        failed = input.boolean("failed draw");
    }
    result.next_permanent_id =
        input.u64("next permanent ID");
    result.next_stack_object_id =
        input.u64("next stack object ID");
    return result;
}

PlayerObservation observation_for(const Root& root) {
    return {
        .observer = root.state.observer,
        .players = root.state.players,
        .hand = root.state.owner_hand,
        .revealed_opponent_hand = std::nullopt,
        .stack = root.state.stack,
        .extra_turns_pending =
            root.state.extra_turns_pending,
        .active_player = root.state.active_player,
        .starting_player =
            root.state.starting_player,
        .turn_number = root.state.turn_number,
    };
}

std::vector<CardId> expand_composition(
    const std::array<std::uint8_t, kCardCount>& counts) {
    std::vector<CardId> result;
    for (std::size_t card = 0;
         card < kCardCount; ++card) {
        result.insert(
            result.end(), counts[card],
            static_cast<CardId>(card));
    }
    return result;
}

std::array<std::vector<CardId>, 2> decks_for(
    const Root& root) {
    return {
        expand_composition(root.deck_compositions[0]),
        expand_composition(root.deck_compositions[1]),
    };
}

GameState carrier_for(const Root& root) {
    const OwnerVisibleState& source = root.state;
    if (source.observer >= 2 ||
        source.owner_hand.size() !=
            source.players[source.observer].hand_size) {
        fail("owner-visible hand boundary drifted");
    }
    GameState result;
    for (std::size_t player = 0; player < 2; ++player) {
        const PublicPlayerState& visible =
            source.players[player];
        PlayerState& state = result.players[player];
        state.life = visible.life;
        state.graveyard = visible.graveyard;
        state.exile = visible.exile;
        state.lands = visible.lands;
        state.creatures = visible.creatures;
        state.artifacts = visible.artifacts;
        state.enchantments = visible.enchantments;
        state.mana_pool = visible.mana_pool;
        state.land_played_this_turn =
            visible.land_played_this_turn;
        state.library.assign(
            visible.library_size, CardId::Forest);
        if (player == source.observer) {
            state.hand = source.owner_hand;
        } else {
            state.hand.assign(
                visible.hand_size, CardId::Forest);
        }
    }
    result.stats = {};
    result.stack = source.stack;
    result.extra_turns_pending =
        source.extra_turns_pending;
    result.failed_draw = source.failed_draw;
    result.active_player = source.active_player;
    result.starting_player =
        source.starting_player;
    result.turn_number = source.turn_number;
    result.next_permanent_id =
        source.next_permanent_id;
    result.next_stack_object_id =
        source.next_stack_object_id;
    return result;
}

void write_locator(
    Writer& output,
    const collection::RootLocator& locator) {
    output.u64(locator.source_block);
    output.u64(locator.source_seed_base);
    output.u64(locator.schedule_index);
    output.u64(locator.game_seed);
    output.u64(locator.owner_seat);
    output.u64(locator.trace_ordinal);
}

collection::RootLocator read_locator(Reader& input) {
    return {
        .source_block =
            input.bounded_u64(
                "source block",
                schedule::kScheduleBlocks - 1),
        .source_seed_base =
            input.u64("source seed base"),
        .schedule_index =
            input.bounded_u64(
                "schedule index",
                schedule::kPhysicalGamesPerSplit - 1),
        .game_seed = input.u64("game seed"),
        .owner_seat =
            input.bounded_u64("owner seat", 1),
        .trace_ordinal =
            input.bounded_u64(
                "trace ordinal",
                std::numeric_limits<
                    std::uint32_t>::max()),
    };
}

void write_ranked_locator(
    Writer& output, const NeutralLocator& root) {
    output.u8(
        static_cast<std::uint8_t>(root.split));
    output.u8(root.owner_deck);
    output.u8(root.schedule_block);
    output.hash(root.physical_game_sha256);
    output.hash(root.stable_root_id);
    output.u32(root.schedule_index);
    output.u8(root.owner_seat);
    output.boolean(root.owner_on_play);
    output.u8(root.opponent_deck);
    output.u32(root.trace_ordinal);
    output.u8(root.legal_action_count);
    output.boolean(root.retained_nontrivial);
    output.u8(root.public_stack_size);
    output.boolean(root.dominance_positive);
    output.u8(root.existing_selected_roles);
    output.hash(root.representative_rank);
    output.hash(root.game_rank);
}

NeutralLocator read_ranked_locator(Reader& input) {
    const std::uint8_t split =
        input.u8("neutral split");
    if (split >
        static_cast<std::uint8_t>(bundle::Split::Check)) {
        fail("neutral split is invalid");
    }
    NeutralLocator result;
    result.split =
        static_cast<bundle::Split>(split);
    result.owner_deck =
        input.u8("neutral owner deck");
    result.schedule_block =
        input.u8("neutral schedule block");
    result.physical_game_sha256 =
        input.hash("neutral physical game");
    result.stable_root_id =
        input.hash("neutral stable root");
    result.schedule_index =
        static_cast<std::uint16_t>(
            input.bounded_u32(
                "neutral schedule index",
                schedule::kPhysicalGamesPerSplit - 1));
    result.owner_seat =
        input.u8("neutral owner seat");
    result.owner_on_play =
        input.boolean("neutral owner on play");
    result.opponent_deck =
        input.u8("neutral opponent deck");
    result.trace_ordinal =
        input.u32("neutral trace ordinal");
    result.legal_action_count =
        input.u8("neutral action count");
    result.retained_nontrivial =
        input.boolean("neutral retained");
    result.public_stack_size =
        input.u8("neutral stack size");
    result.dominance_positive =
        input.boolean("neutral dominance");
    result.existing_selected_roles =
        input.u8("neutral roles");
    result.representative_rank =
        input.hash("representative rank");
    result.game_rank =
        input.hash("game rank");
    return result;
}

neutral::RankedLocator source_ranked_locator(
    const NeutralLocator& locator) {
    return {
        .root = {
            .rank = {
                .split = locator.split,
                .owner_deck = locator.owner_deck,
                .schedule_block =
                    locator.schedule_block,
                .physical_game_sha256 =
                    locator.physical_game_sha256,
                .stable_root_id =
                    locator.stable_root_id,
            },
            .schedule_index = locator.schedule_index,
            .owner_seat = locator.owner_seat,
            .owner_on_play = locator.owner_on_play,
            .opponent_deck =
                locator.opponent_deck,
            .trace_ordinal = locator.trace_ordinal,
            .legal_action_count =
                locator.legal_action_count,
            .retained_nontrivial =
                locator.retained_nontrivial,
            .public_stack_size =
                locator.public_stack_size,
            .dominance_positive =
                locator.dominance_positive,
            .existing_selected_roles =
                locator.existing_selected_roles,
        },
        .representative_rank =
            locator.representative_rank,
        .game_rank = locator.game_rank,
    };
}

void write_file_identity(
    Writer& output, const FileIdentity& identity) {
    output.u64(identity.bytes);
    output.hash(identity.sha256);
}

FileIdentity read_file_identity(Reader& input) {
    return {
        .bytes = input.u64("file byte count"),
        .sha256 = input.hash("file SHA-256"),
    };
}

void write_bindings(
    Writer& output, const SourceBindings& sources) {
    write_file_identity(
        output, sources.parent_artifact);
    output.hash(sources.parent_model_fingerprint);
    write_file_identity(output, sources.dev1_artifact);
    output.hash(
        sources.dev1_fit_selection_sha256);
    output.hash(sources.dev1_fit_scored_sha256);
    output.hash(
        sources.dev1_check_selection_sha256);
    output.hash(sources.dev1_check_scored_sha256);
    write_file_identity(
        output, sources.neutral_artifact);
    output.hash(
        sources.neutral_selected_order_sha256);
}

SourceBindings read_bindings(Reader& input) {
    SourceBindings result;
    result.parent_artifact =
        read_file_identity(input);
    result.parent_model_fingerprint =
        input.hash("parent model fingerprint");
    result.dev1_artifact =
        read_file_identity(input);
    result.dev1_fit_selection_sha256 =
        input.hash("DEV1 FIT selection");
    result.dev1_fit_scored_sha256 =
        input.hash("DEV1 FIT scored");
    result.dev1_check_selection_sha256 =
        input.hash("DEV1 CHECK selection");
    result.dev1_check_scored_sha256 =
        input.hash("DEV1 CHECK scored");
    result.neutral_artifact =
        read_file_identity(input);
    result.neutral_selected_order_sha256 =
        input.hash("neutral selected order");
    return result;
}

void write_context(
    Writer& output,
    const LearnedDecisionContext& context) {
    output.boolean(context.valid);
    output.u8(
        static_cast<std::uint8_t>(context.phase));
    output.u64(context.decision_player);
    output.i32(context.consecutive_passes);
    output.boolean(context.sorcery_actions);
}

LearnedDecisionContext read_context(Reader& input) {
    LearnedDecisionContext result;
    result.valid = input.boolean("context valid");
    const std::uint8_t phase =
        input.u8("context phase");
    if (phase >
        static_cast<std::uint8_t>(
            TurnPhase::SecondMain)) {
        fail("context phase is invalid");
    }
    result.phase =
        static_cast<TurnPhase>(phase);
    result.decision_player =
        input.bounded_u64("decision player", 1);
    result.consecutive_passes =
        input.i32("consecutive passes");
    result.sorcery_actions =
        input.boolean("sorcery actions");
    return result;
}

void write_root(Writer& output, const Root& root) {
    output.u8(
        static_cast<std::uint8_t>(root.source));
    output.u8(
        static_cast<std::uint8_t>(root.split));
    output.u32(root.source_row);
    output.u8(root.source_roles);
    output.u64(root.production_seed);
    write_locator(output, root.locator);
    output.u8(root.owner_deck);
    output.u8(root.opponent_deck);
    output.hash(root.stable_root_id);
    output.hash(root.physical_game_sha256);
    output.hash(root.information_action_sha256);
    output.hash(root.descriptor_set_sha256);
    output.hash(root.raw_actions_sha256);
    output.hash(root.canonical_actions_sha256);
    output.boolean(root.has_neutral_locator);
    if (root.has_neutral_locator) {
        write_ranked_locator(
            output, root.neutral_locator);
    }
    write_owner_state(output, root.state);
    for (const auto& deck : root.deck_compositions) {
        for (const std::uint8_t count : deck) {
            output.u8(count);
        }
    }
    write_context(output, root.context);
    output.u32(
        static_cast<std::uint32_t>(
            root.raw_actions.size()));
    for (const PriorityAction& action :
         root.raw_actions) {
        write_priority_action(output, action);
    }
    output.u32(
        static_cast<std::uint32_t>(
            root.canonical_actions.size()));
    for (const CanonicalAction& action :
         root.canonical_actions) {
        output.text(action.descriptor);
        output.u8(action.raw_index);
    }
    output.u8(root.pass_index);
}

Root read_root(Reader& input) {
    Root result;
    const std::uint8_t family =
        input.u8("source family");
    if (family >
        static_cast<std::uint8_t>(
            SourceFamily::Dev5Neutral)) {
        fail("source family is invalid");
    }
    result.source =
        static_cast<SourceFamily>(family);
    const std::uint8_t split =
        input.u8("root split");
    if (split >
        static_cast<std::uint8_t>(
            bundle::Split::Check)) {
        fail("root split is invalid");
    }
    result.split =
        static_cast<bundle::Split>(split);
    result.source_row =
        input.u32("source row");
    result.source_roles =
        input.u8("source roles");
    result.production_seed =
        input.u64("production seed");
    result.locator = read_locator(input);
    result.owner_deck =
        input.u8("owner deck");
    result.opponent_deck =
        input.u8("opponent deck");
    result.stable_root_id =
        input.hash("stable root ID");
    result.physical_game_sha256 =
        input.hash("physical game digest");
    result.information_action_sha256 =
        input.hash("information/action digest");
    result.descriptor_set_sha256 =
        input.hash("descriptor-set digest");
    result.raw_actions_sha256 =
        input.hash("raw-actions digest");
    result.canonical_actions_sha256 =
        input.hash("canonical-actions digest");
    result.has_neutral_locator =
        input.boolean("neutral locator present");
    if (result.has_neutral_locator) {
        result.neutral_locator =
            read_ranked_locator(input);
    }
    result.state = read_owner_state(input);
    for (auto& deck : result.deck_compositions) {
        for (std::uint8_t& count : deck) {
            count = input.u8("deck card count");
        }
    }
    result.context = read_context(input);
    const std::size_t raw_count =
        input.bounded_u32(
            "raw action count",
            bundle::kMaximumActions);
    result.raw_actions.reserve(raw_count);
    for (std::size_t index = 0;
         index < raw_count; ++index) {
        result.raw_actions.push_back(
            read_priority_action(input));
    }
    const std::size_t canonical_count =
        input.bounded_u32(
            "canonical action count",
            bundle::kMaximumActions);
    result.canonical_actions.reserve(canonical_count);
    for (std::size_t index = 0;
         index < canonical_count; ++index) {
        result.canonical_actions.push_back({
            .descriptor =
                input.text("canonical descriptor"),
            .raw_index =
                input.u8("canonical raw index"),
        });
    }
    result.pass_index =
        input.u8("Pass index");
    return result;
}

void append_root_order_fields(
    Writer& output, const Root& root) {
    output.u8(
        static_cast<std::uint8_t>(root.source));
    output.u8(
        static_cast<std::uint8_t>(root.split));
    output.u32(root.source_row);
    output.u8(root.source_roles);
    output.u64(root.production_seed);
    write_locator(output, root.locator);
    output.u8(root.owner_deck);
    output.u8(root.opponent_deck);
    output.hash(root.stable_root_id);
    output.hash(root.physical_game_sha256);
    output.hash(root.information_action_sha256);
    output.hash(root.descriptor_set_sha256);
    output.hash(root.raw_actions_sha256);
    output.hash(root.canonical_actions_sha256);
    output.boolean(root.has_neutral_locator);
    if (root.has_neutral_locator) {
        write_ranked_locator(
            output, root.neutral_locator);
    }
}

void require_source_bindings(
    const SourceBindings& sources) {
    if (sources.parent_artifact !=
            FileIdentity{
                .bytes = kParentArtifactBytes,
                .sha256 =
                    bundle::parse_sha256(
                        kParentArtifactSha256),
            } ||
        sources.parent_model_fingerprint !=
            bundle::parse_sha256(
                kParentModelFingerprint) ||
        sources.dev1_artifact !=
            FileIdentity{
                .bytes = kDev1ArtifactBytes,
                .sha256 =
                    bundle::parse_sha256(
                        kDev1ArtifactSha256),
            } ||
        sources.neutral_artifact !=
            FileIdentity{
                .bytes = kNeutralArtifactBytes,
                .sha256 =
                    bundle::parse_sha256(
                        kNeutralArtifactSha256),
            }) {
        fail("immutable source identity drifted");
    }
    for (const Hash256* digest :
         std::array{
             &sources.dev1_fit_selection_sha256,
             &sources.dev1_fit_scored_sha256,
             &sources.dev1_check_selection_sha256,
             &sources.dev1_check_scored_sha256,
             &sources.neutral_selected_order_sha256,
         }) {
        if (!nonzero_hash(*digest)) {
            fail("source selection binding is zero");
        }
    }
}

std::vector<CardId> expected_deck(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return green_deck();
    case DeckId::Red:
        return red_deck();
    case DeckId::Blue:
        return blue_deck();
    case DeckId::White:
        return white_control_deck();
    case DeckId::RUAggro:
        return ru_aggro_deck();
    }
    fail("deck ID is invalid");
}

std::array<std::uint8_t, kCardCount> composition_of(
    const std::vector<CardId>& deck) {
    std::array<std::uint8_t, kCardCount> result{};
    for (const CardId card : deck) {
        std::uint8_t& count =
            result[static_cast<std::size_t>(card)];
        if (count ==
            std::numeric_limits<std::uint8_t>::max()) {
            fail("deck composition overflows");
        }
        ++count;
    }
    return result;
}

void require_locator_source(const Root& row) {
    const schedule::Split split =
        row.split == bundle::Split::Fit
            ? schedule::Split::Fit
            : schedule::Split::Check;
    const auto games =
        schedule::source_schedule(split);
    if (row.locator.schedule_index >= games.size()) {
        fail("root schedule index is out of range");
    }
    const schedule::SourceGame& source =
        games[row.locator.schedule_index];
    if (row.locator.source_block !=
            source.schedule_block ||
        row.locator.source_seed_base !=
            source.source_seed_base ||
        row.locator.game_seed != source.game_seed ||
        row.locator.owner_seat >= 2 ||
        row.owner_deck !=
            static_cast<std::uint8_t>(
                source.seat_decks[
                    row.locator.owner_seat]) ||
        row.opponent_deck !=
            static_cast<std::uint8_t>(
                source.seat_decks[
                    1 - row.locator.owner_seat])) {
        fail("root locator disagrees with frozen schedule");
    }
}

void require_neutral_locator(const Root& row) {
    if (!row.has_neutral_locator) {
        fail("neutral row lacks ranked locator");
    }
    const NeutralLocator& eligible =
        row.neutral_locator;
    const neutral::RankKey rank{
        .split = eligible.split,
        .owner_deck = eligible.owner_deck,
        .schedule_block =
            eligible.schedule_block,
        .physical_game_sha256 =
            eligible.physical_game_sha256,
        .stable_root_id =
            eligible.stable_root_id,
    };
    if (eligible.split != row.split ||
        eligible.owner_deck != row.owner_deck ||
        eligible.schedule_block !=
            row.locator.source_block ||
        eligible.physical_game_sha256 !=
            row.physical_game_sha256 ||
        eligible.stable_root_id !=
            row.stable_root_id ||
        eligible.schedule_index !=
            row.locator.schedule_index ||
        eligible.owner_seat != row.locator.owner_seat ||
        eligible.opponent_deck != row.opponent_deck ||
        eligible.trace_ordinal !=
            row.locator.trace_ordinal ||
        eligible.legal_action_count !=
            row.canonical_actions.size() ||
        !eligible.retained_nontrivial ||
        eligible.public_stack_size != 0 ||
        eligible.dominance_positive ||
        eligible.existing_selected_roles != 0 ||
        row.neutral_locator.representative_rank !=
            neutral::representative_rank(
                rank) ||
        row.neutral_locator.game_rank !=
            neutral::game_rank(rank) ||
        row.production_seed !=
            neutral::production_seed_for_stable_root(
                row.stable_root_id)) {
        fail("neutral ranked locator drifted");
    }
    const schedule::Split split =
        row.split == bundle::Split::Fit
            ? schedule::Split::Fit
            : schedule::Split::Check;
    const auto games =
        schedule::source_schedule(split);
    if (eligible.owner_on_play !=
        (games[row.locator.schedule_index]
             .starting_player ==
         row.locator.owner_seat)) {
        fail("neutral play/draw locator drifted");
    }
}

std::vector<PriorityAction> canonical_typed_actions(
    const Root& root) {
    std::vector<PriorityAction> result;
    result.reserve(root.canonical_actions.size());
    for (const CanonicalAction& action :
         root.canonical_actions) {
        if (action.raw_index >=
            root.raw_actions.size()) {
            fail("canonical raw-action index is invalid");
        }
        result.push_back(
            root.raw_actions[action.raw_index]);
    }
    return result;
}

bool bounded_mana(const ManaCost& mana) {
    const std::array<int, 5> components{
        mana.generic,
        mana.green,
        mana.red,
        mana.blue,
        mana.white,
    };
    std::int64_t total = 0;
    for (const int component : components) {
        if (component < 0 ||
            component > kMaximumMana) {
            return false;
        }
        total += component;
    }
    return total <= kMaximumMana;
}

bool bounded_target(const Target& target) {
    return target.player < 2 &&
           (!target.creature.has_value() ||
            (*target.creature != 0 &&
             *target.creature <= kMaximumObjectId));
}

bool valid_card(const CardId card) {
    return static_cast<std::size_t>(card) < kCardCount;
}

bool bounded_priority_action(
    const PriorityAction& action) {
    return
        static_cast<std::uint8_t>(action.kind) <=
            static_cast<std::uint8_t>(
                PriorityActionKind::
                    ActivateMillstone) &&
        valid_card(action.card) &&
        (!action.target.has_value() ||
         bounded_target(*action.target)) &&
        (!action.spell_target.has_value() ||
         (*action.spell_target != 0 &&
          *action.spell_target <=
              kMaximumObjectId)) &&
        (!action.source_permanent.has_value() ||
         (*action.source_permanent != 0 &&
          *action.source_permanent <=
              kMaximumObjectId)) &&
        action.x_value >= 0 &&
        action.x_value <=
            kMaximumSafeGameInteger;
}

bool bounded_public_player(
    const PublicPlayerState& player) {
    if (player.life <= 0 ||
        player.life > kMaximumSafeGameInteger ||
        player.library_size > kMaximumCards ||
        player.hand_size > kMaximumCards ||
        player.graveyard.size() > kMaximumCards ||
        player.exile.size() > kMaximumCards ||
        player.lands.size() > kMaximumObjects ||
        player.creatures.size() > kMaximumObjects ||
        player.artifacts.size() > kMaximumObjects ||
        player.enchantments.size() > kMaximumCards ||
        !bounded_mana(player.mana_pool)) {
        return false;
    }
    if (!std::all_of(
            player.graveyard.begin(),
            player.graveyard.end(),
            valid_card) ||
        !std::all_of(
            player.exile.begin(),
            player.exile.end(),
            valid_card) ||
        !std::all_of(
            player.enchantments.begin(),
            player.enchantments.end(),
            valid_card) ||
        !std::all_of(
            player.lands.begin(),
            player.lands.end(),
            [](const LandPermanent& land) {
                return valid_card(land.card);
            })) {
        return false;
    }
    for (const CreaturePermanent& creature :
         player.creatures) {
        if (!valid_card(creature.card) ||
            creature.id == 0 ||
            creature.id > kMaximumObjectId ||
            creature.damage < 0 ||
            creature.damage >
                kMaximumSafeGameInteger ||
            creature.temporary_power_bonus < 0 ||
            creature.temporary_power_bonus >
                kMaximumSafeGameInteger ||
            creature.temporary_toughness_bonus < 0 ||
            creature.temporary_toughness_bonus >
                kMaximumSafeGameInteger) {
            return false;
        }
    }
    return std::all_of(
        player.artifacts.begin(),
        player.artifacts.end(),
        [](const ArtifactPermanent& artifact) {
            return valid_card(artifact.card) &&
                   artifact.id != 0 &&
                   artifact.id <=
                       kMaximumObjectId;
        });
}

bool bounded_stack_object(const StackObject& object) {
    return
        static_cast<std::uint8_t>(object.kind) <=
            static_cast<std::uint8_t>(
                StackObjectKind::
                    ActivatedAbility) &&
        object.id != 0 &&
        object.id <= kMaximumObjectId &&
        static_cast<std::size_t>(object.card) <
            kCardCount &&
        object.controller < 2 &&
        (!object.target.has_value() ||
         bounded_target(*object.target)) &&
        (!object.spell_target.has_value() ||
         (*object.spell_target != 0 &&
          *object.spell_target <=
              kMaximumObjectId)) &&
        object.x_value >= 0 &&
        object.x_value <=
            kMaximumSafeGameInteger;
}

void require_root_semantics(const Root& row) {
    if ((row.source != SourceFamily::Dev1Selected &&
         row.source != SourceFamily::Dev5Neutral) ||
        (row.split != bundle::Split::Fit &&
         row.split != bundle::Split::Check)) {
        fail("root source family or split is invalid");
    }
    require_locator_source(row);
    const bool public_players_bounded =
        std::all_of(
            row.state.players.begin(),
            row.state.players.end(),
            bounded_public_player);
    const bool stack_kinds_valid =
        std::all_of(
            row.state.stack.begin(),
            row.state.stack.end(),
            bounded_stack_object);
    const bool raw_actions_bounded =
        std::all_of(
            row.raw_actions.begin(),
            row.raw_actions.end(),
            bounded_priority_action);
    if (row.owner_deck >= kDeckCount ||
        row.opponent_deck >= kDeckCount ||
        row.owner_deck == row.opponent_deck ||
        row.state.observer != row.locator.owner_seat ||
        row.context.decision_player !=
            row.locator.owner_seat ||
        !row.context.valid ||
        static_cast<std::uint8_t>(
            row.context.phase) >
            static_cast<std::uint8_t>(
                TurnPhase::SecondMain) ||
        row.context.phase ==
            TurnPhase::DeclareAttackers ||
        row.context.phase ==
            TurnPhase::DeclareBlockers ||
        row.context.phase ==
            TurnPhase::DamageOrder ||
        row.context.consecutive_passes < 0 ||
        row.context.consecutive_passes > 1 ||
        row.context.sorcery_actions !=
            (row.context.phase ==
                 TurnPhase::FirstMain ||
             row.context.phase ==
                 TurnPhase::SecondMain) ||
        !public_players_bounded ||
        row.state.stack.size() > kMaximumObjects ||
        !stack_kinds_valid ||
        !raw_actions_bounded ||
        row.locator.trace_ordinal >
            std::numeric_limits<std::uint32_t>::max() ||
        row.raw_actions.size() < 2 ||
        row.raw_actions.size() >
            bundle::kMaximumActions ||
        row.canonical_actions.size() !=
            row.raw_actions.size() ||
        row.pass_index >=
            row.canonical_actions.size()) {
        fail("root rules context is invalid");
    }
    for (const Hash256* digest :
         std::array{
             &row.stable_root_id,
             &row.physical_game_sha256,
             &row.information_action_sha256,
             &row.descriptor_set_sha256,
             &row.raw_actions_sha256,
             &row.canonical_actions_sha256,
         }) {
        if (!nonzero_hash(*digest)) {
            fail("root identity digest is zero");
        }
    }
    if (row.production_seed !=
        neutral::production_seed_for_stable_root(
            row.stable_root_id)) {
        fail("root production seed drifted");
    }
    if (row.state.players[row.state.observer].hand_size !=
            row.state.owner_hand.size() ||
        !std::all_of(
            row.state.owner_hand.begin(),
            row.state.owner_hand.end(),
            valid_card) ||
        row.state.active_player >= 2 ||
        row.state.starting_player >= 2 ||
        row.state.turn_number == 0 ||
        row.state.turn_number > kMaximumTurnNumber ||
        std::any_of(
            row.state.extra_turns_pending.begin(),
            row.state.extra_turns_pending.end(),
            [](std::size_t extra) {
                return extra > kMaximumTurnNumber;
            }) ||
        row.state.failed_draw !=
            std::array<bool, 2>{false, false} ||
        row.state.next_permanent_id == 0 ||
        row.state.next_permanent_id >
            kMaximumObjectId ||
        row.state.next_stack_object_id == 0 ||
        row.state.next_stack_object_id >
            kMaximumObjectId) {
        fail("owner-visible state is malformed");
    }

    const auto schedules =
        schedule::source_schedule(
            row.split == bundle::Split::Fit
                ? schedule::Split::Fit
                : schedule::Split::Check);
    const auto& source =
        schedules[row.locator.schedule_index];
    if (row.state.starting_player !=
        source.starting_player) {
        fail("root state disagrees with source play/draw");
    }
    for (std::size_t player = 0; player < 2; ++player) {
        if (row.deck_compositions[player] !=
            composition_of(
                expected_deck(
                    source.seat_decks[player]))) {
            fail("deck composition drifted");
        }
    }

    std::vector<bool> seen_raw(
        row.raw_actions.size(), false);
    std::vector<std::string> descriptors;
    descriptors.reserve(
        row.canonical_actions.size());
    std::size_t pass_count = 0;
    for (std::size_t index = 0;
         index < row.canonical_actions.size(); ++index) {
        const CanonicalAction& canonical =
            row.canonical_actions[index];
        if (canonical.raw_index >=
                row.raw_actions.size() ||
            seen_raw[canonical.raw_index] ||
            !canonical_text(canonical.descriptor) ||
            canonical.descriptor !=
                probes::stable_priority_action_descriptor(
                    row.raw_actions[
                        canonical.raw_index]) ||
            (index != 0 &&
             !(row.canonical_actions[index - 1]
                   .descriptor <
               canonical.descriptor))) {
            fail("canonical/raw action bijection drifted");
        }
        seen_raw[canonical.raw_index] = true;
        descriptors.push_back(canonical.descriptor);
        if (row.raw_actions[canonical.raw_index].kind ==
            PriorityActionKind::Pass) {
            ++pass_count;
            if (index != row.pass_index) {
                fail("typed Pass index drifted");
            }
        }
    }
    if (pass_count != 1 ||
        std::find(seen_raw.begin(), seen_raw.end(), false) !=
            seen_raw.end() ||
        raw_actions_sha256(row.raw_actions) !=
            row.raw_actions_sha256 ||
        canonical_actions_sha256(
            row.canonical_actions) !=
            row.canonical_actions_sha256 ||
        bundle::descriptor_set_sha256(descriptors) !=
            row.descriptor_set_sha256) {
        fail("action order/hash contract drifted");
    }

    const GameState world =
        sample_world(row, kValidationWorldSeed);
    const PlayerObservation observation =
        observation_for(row);
    const std::vector<PriorityAction>
        canonical_actions =
            canonical_typed_actions(row);
    if (collection::owner_information_action_fingerprint(
            observation, row.context,
            canonical_actions,
            kOwnerInformationSchema) !=
        bundle::format_sha256(
            row.information_action_sha256)) {
        fail("owner-information/action digest drifted");
    }
    if (collection::block_bound_stable_root_id(
            row.locator,
            bundle::format_sha256(
                row.information_action_sha256),
            bundle::kStableRootSchema) !=
            bundle::format_sha256(
                row.stable_root_id) ||
        bundle::sha256(
            collection::block_bound_physical_game_id(
                row.locator)) !=
            row.physical_game_sha256) {
        fail("source locator digest drifted");
    }

    probes::DecisionProbe probe{
        .stable_id =
            bundle::format_sha256(
                row.stable_root_id),
        .category = probes::Category::GreenDevelop,
        .decision_kind =
            probes::DecisionKind::Priority,
        .root_deck =
            static_cast<DeckId>(row.owner_deck),
        .opponent_deck =
            static_cast<DeckId>(
                row.opponent_deck),
        .root_player = row.state.observer,
        .phase = row.context.phase,
        .consecutive_passes =
            row.context.consecutive_passes,
        .state = world,
        .original_decks = decks_for(row),
    };
    probe.candidates.reserve(canonical_actions.size());
    for (std::size_t index = 0;
         index < canonical_actions.size(); ++index) {
        probe.candidates.push_back({
            .descriptor =
                row.canonical_actions[index]
                    .descriptor,
            .action = canonical_actions[index],
        });
    }
    const probes::Validation probe_validation =
        probes::validate_probe(
            probe, kValidationWorldSeed + 1);
    if (!probe_validation.ok()) {
        fail(
            probe_validation.errors.empty()
                ? "rehydrated probe is invalid"
                : probe_validation.errors.front());
    }

    if (row.source ==
        SourceFamily::Dev1Selected) {
        constexpr std::uint8_t kKnownRoles =
            static_cast<std::uint8_t>(
                bundle::Role::DominancePositive) |
            static_cast<std::uint8_t>(
                bundle::Role::BackgroundControl);
        if (row.source_roles == 0 ||
            (row.source_roles & ~kKnownRoles) != 0 ||
            row.has_neutral_locator ||
            row.neutral_locator !=
                NeutralLocator{}) {
            fail("DEV1 source binding is invalid");
        }
    } else {
        if (row.source_roles != 0) {
            fail("neutral row carries DEV1 roles");
        }
        require_neutral_locator(row);
    }
}

std::string encode_payload(const Artifact& artifact) {
    Writer output;
    output.text(artifact.manifest.schema);
    output.text(artifact.manifest.environment);
    write_bindings(output, artifact.manifest.sources);
    output.u32(artifact.manifest.dev1_options);
    output.u32(artifact.manifest.neutral_options);
    output.hash(
        artifact.manifest.root_order_sha256);
    output.u32(
        static_cast<std::uint32_t>(
            artifact.roots.size()));
    for (const Root& root : artifact.roots) {
        write_root(output, root);
    }
    return output.take();
}

Artifact decode_payload(std::string_view payload) {
    Reader input(payload);
    Artifact result;
    result.manifest.schema =
        input.text("schema", 128);
    result.manifest.environment =
        input.text("environment", 256);
    result.manifest.sources =
        read_bindings(input);
    result.manifest.dev1_options =
        input.u32("DEV1 option count");
    result.manifest.neutral_options =
        input.u32("neutral option count");
    result.manifest.root_order_sha256 =
        input.hash("root-order digest");
    const std::size_t count =
        input.bounded_u32("root count", kRootCount);
    result.roots.reserve(count);
    for (std::size_t index = 0;
         index < count; ++index) {
        result.roots.push_back(read_root(input));
    }
    input.finish();
    return result;
}

void require_frozen_census(
    std::span<const Root> roots) {
    if (roots.size() != kRootCount) {
        fail("frozen root census has the wrong size");
    }
    constexpr std::uint8_t kPositiveRole =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive);
    constexpr std::uint8_t kBackgroundRole =
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    std::size_t dev1_options = 0;
    std::size_t neutral_options = 0;
    std::size_t positive = 0;
    std::size_t background = 0;
    std::array<std::size_t, kDeckCount>
        owner_deck_roots{};
    std::set<Hash256> stable_roots;
    std::set<std::tuple<
        std::size_t, std::uint64_t, std::size_t,
        std::uint64_t, std::size_t, std::size_t>>
        locators;
    for (std::size_t index = 0;
         index < roots.size(); ++index) {
        const Root& row = roots[index];
        const bool dev1 = index < kDev1RootCount;
        const std::size_t family_row =
            dev1 ? index : index - kDev1RootCount;
        const bundle::Split expected_split =
            (dev1
                 ? family_row < kDev1FitRootCount
                 : family_row <
                       kNeutralFitRootCount)
                ? bundle::Split::Fit
                : bundle::Split::Check;
        if (row.source !=
                (dev1
                     ? SourceFamily::Dev1Selected
                     : SourceFamily::Dev5Neutral) ||
            row.source_row != family_row ||
            row.split != expected_split ||
            row.owner_deck >= kDeckCount ||
            row.raw_actions.size() < 2 ||
            row.raw_actions.size() >
                bundle::kMaximumActions ||
            (dev1
                 ? row.source_roles != kPositiveRole &&
                       row.source_roles !=
                           kBackgroundRole
                 : row.source_roles != 0)) {
            fail("frozen source row/order shape drifted");
        }
        ++owner_deck_roots[row.owner_deck];
        if (dev1) {
            dev1_options += row.raw_actions.size();
            positive +=
                row.source_roles == kPositiveRole
                    ? 1U
                    : 0U;
            background +=
                row.source_roles == kBackgroundRole
                    ? 1U
                    : 0U;
        } else {
            neutral_options +=
                row.raw_actions.size();
        }
        if (!nonzero_hash(row.stable_root_id) ||
            !stable_roots.insert(
                 row.stable_root_id)
                 .second ||
            !locators
                 .emplace(
                     row.locator.source_block,
                     row.locator.source_seed_base,
                     row.locator.schedule_index,
                     row.locator.game_seed,
                     row.locator.owner_seat,
                     row.locator.trace_ordinal)
                 .second) {
            fail("selected root overlaps or duplicates");
        }
    }
    if (dev1_options != kDev1OptionCount ||
        neutral_options != kNeutralOptionCount ||
        dev1_options + neutral_options !=
            kOptionCount ||
        positive != kDev1PositiveRootCount ||
        background !=
            kDev1BackgroundRootCount ||
        owner_deck_roots != kOwnerDeckRootCounts) {
        fail("root/action/role census drifted");
    }
}

} // namespace

GameState sample_world(const Root& root, std::uint64_t seed) {
    const auto decks = decks_for(root);
    GameState result =
        sample_determinization(
            carrier_for(root), decks,
            root.state.observer, seed);
    result.stats = {};
    if (observe_game_state(
            result, root.state.observer) !=
            observation_for(root) ||
        legal_priority_actions(
            result, root.context.decision_player,
            root.context.sorcery_actions) !=
            root.raw_actions) {
        fail("sampled world changed owner information or legal actions");
    }
    return result;
}

Hash256 raw_actions_sha256(
    std::span<const PriorityAction> actions) {
    Writer output(1U << 20U);
    output.text(kSchema);
    output.text("raw-actions-v1");
    output.u32(
        static_cast<std::uint32_t>(actions.size()));
    for (const PriorityAction& action : actions) {
        write_priority_action(output, action);
    }
    return bundle::sha256(output.bytes());
}

Hash256 canonical_actions_sha256(
    std::span<const CanonicalAction> actions) {
    Writer output(1U << 20U);
    output.text(kSchema);
    output.text("canonical-actions-v1");
    output.u32(
        static_cast<std::uint32_t>(actions.size()));
    for (const CanonicalAction& action : actions) {
        output.text(action.descriptor);
        output.u8(action.raw_index);
    }
    return bundle::sha256(output.bytes());
}

std::vector<CanonicalAction> bind_canonical_actions(
    std::span<const PriorityAction> raw_actions,
    std::span<const PriorityAction>
        descriptor_canonical_actions) {
    if (raw_actions.size() < 2 ||
        raw_actions.size() >
            bundle::kMaximumActions ||
        descriptor_canonical_actions.size() !=
            raw_actions.size()) {
        fail("canonical/raw action binding shape is invalid");
    }
    std::vector<bool> seen(raw_actions.size(), false);
    std::vector<CanonicalAction> result;
    result.reserve(raw_actions.size());
    std::string prior_descriptor;
    for (const PriorityAction& action :
         descriptor_canonical_actions) {
        const std::string descriptor =
            probes::stable_priority_action_descriptor(
                action);
        if (!canonical_text(descriptor) ||
            (!prior_descriptor.empty() &&
             !(prior_descriptor < descriptor))) {
            fail("descriptor-canonical action order drifted");
        }
        prior_descriptor = descriptor;

        std::size_t raw_index = raw_actions.size();
        std::size_t matches = 0;
        for (std::size_t candidate = 0;
             candidate < raw_actions.size(); ++candidate) {
            if (raw_actions[candidate] == action) {
                raw_index = candidate;
                ++matches;
            }
        }
        if (matches != 1 || seen[raw_index]) {
            fail("canonical action lacks a unique raw match");
        }
        seen[raw_index] = true;
        result.push_back({
            .descriptor = descriptor,
            .raw_index =
                static_cast<std::uint8_t>(
                    raw_index),
        });
    }
    if (std::find(seen.begin(), seen.end(), false) !=
        seen.end()) {
        fail("canonical/raw action binding is not bijective");
    }
    return result;
}

Hash256 root_order_sha256(std::span<const Root> roots) {
    Writer output(4U << 20U);
    output.text(kSchema);
    output.text("root-order-v1");
    output.u32(
        static_cast<std::uint32_t>(roots.size()));
    for (const Root& root : roots) {
        append_root_order_fields(output, root);
    }
    return bundle::sha256(output.bytes());
}

void validate(const Artifact& artifact) {
    if (artifact.manifest.schema != kSchema ||
        artifact.manifest.environment != kEnvironment ||
        artifact.manifest.dev1_options !=
            kDev1OptionCount ||
        artifact.manifest.neutral_options !=
            kNeutralOptionCount) {
        fail("manifest schema/count contract drifted");
    }
    require_source_bindings(artifact.manifest.sources);
    require_frozen_census(artifact.roots);
    for (const Root& row : artifact.roots) {
        require_root_semantics(row);
    }

    std::vector<neutral::RankedLocator> neutral_rows;
    neutral_rows.reserve(kNeutralRootCount);
    for (std::size_t index = kDev1RootCount;
         index < artifact.roots.size(); ++index) {
        neutral_rows.push_back(
            source_ranked_locator(
                artifact.roots[index]
                    .neutral_locator));
    }
    if (neutral::selected_order_sha256(neutral_rows) !=
            artifact.manifest.sources
                .neutral_selected_order_sha256 ||
        root_order_sha256(artifact.roots) !=
            artifact.manifest.root_order_sha256) {
        fail("selected/root order digest drifted");
    }
}

void validate_against_sources(
    const Artifact& artifact,
    const bundle::Bundle& dev1,
    const neutral::Artifact& neutral_artifact) {
    validate(artifact);
    bundle::validate(dev1);
    neutral::validate(neutral_artifact);
    const SourceBindings& sources =
        artifact.manifest.sources;
    const std::string dev1_bytes =
        bundle::encode(dev1);
    const std::string neutral_bytes =
        neutral::encode(neutral_artifact);
    const FileIdentity encoded_dev1{
        .bytes = static_cast<std::uint64_t>(
            dev1_bytes.size()),
        .sha256 = bundle::sha256(dev1_bytes),
    };
    const FileIdentity encoded_neutral{
        .bytes = static_cast<std::uint64_t>(
            neutral_bytes.size()),
        .sha256 = bundle::sha256(neutral_bytes),
    };
    if (encoded_dev1.bytes != kDev1ArtifactBytes ||
        encoded_dev1.sha256 !=
            bundle::parse_sha256(
                kDev1ArtifactSha256) ||
        encoded_dev1 != sources.dev1_artifact ||
        encoded_neutral.bytes !=
            kNeutralArtifactBytes ||
        encoded_neutral.sha256 !=
            bundle::parse_sha256(
                kNeutralArtifactSha256) ||
        encoded_neutral !=
            sources.neutral_artifact) {
        fail("source artifact encoding identity drifted");
    }
    if (dev1.fit_rows.size() != kDev1FitRootCount ||
        dev1.check_rows.size() !=
            kDev1CheckRootCount ||
        neutral_artifact.rows.size() !=
            kNeutralRootCount ||
        sources.dev1_fit_selection_sha256 !=
            dev1.manifest.fit.selection_sha256 ||
        sources.dev1_fit_scored_sha256 !=
            dev1.manifest.fit.scored_sha256 ||
        sources.dev1_check_selection_sha256 !=
            dev1.manifest.check.selection_sha256 ||
        sources.dev1_check_scored_sha256 !=
            dev1.manifest.check.scored_sha256 ||
        sources.neutral_selected_order_sha256 !=
            neutral_artifact.manifest
                .selected_order_sha256) {
        fail("source artifact manifest join drifted");
    }

    for (std::size_t index = 0;
         index < kDev1RootCount; ++index) {
        const bool fit = index < kDev1FitRootCount;
        const std::size_t local =
            fit ? index : index - kDev1FitRootCount;
        const bundle::SelectedRow& source =
            fit ? dev1.fit_rows[local]
                : dev1.check_rows[local];
        const Root& cached = artifact.roots[index];
        const bundle::CensusRow& census =
            source.census;
        if (cached.split != source.split ||
            cached.source_roles != source.roles ||
            cached.production_seed !=
                source.production_seed ||
            cached.locator.source_block !=
                census.schedule_block ||
            cached.locator.schedule_index !=
                census.schedule_index ||
            cached.locator.owner_seat !=
                census.owner_seat ||
            cached.locator.trace_ordinal !=
                census.trace_ordinal ||
            cached.owner_deck != census.owner_deck ||
            cached.opponent_deck !=
                census.opponent_deck ||
            cached.stable_root_id !=
                census.stable_root_id ||
            cached.physical_game_sha256 !=
                census.physical_game_sha256 ||
            cached.information_action_sha256 !=
                census.information_action_sha256 ||
            cached.descriptor_set_sha256 !=
                census.descriptor_set_sha256 ||
            cached.pass_index != census.pass_index ||
            cached.canonical_actions.size() !=
                source.actions.size()) {
            fail("DEV1 source-row join drifted");
        }
        for (std::size_t action = 0;
             action < source.actions.size(); ++action) {
            if (cached.canonical_actions[action]
                        .descriptor !=
                    source.actions[action].descriptor ||
                source.actions[action].is_pass !=
                    (action == cached.pass_index)) {
                fail("DEV1 source action order drifted");
            }
        }
    }
    for (std::size_t index = 0;
         index < kNeutralRootCount; ++index) {
        const neutral::NeutralRow& source =
            neutral_artifact.rows[index];
        const Root& cached =
            artifact.roots[kDev1RootCount + index];
        if (source_ranked_locator(
                cached.neutral_locator) !=
                source.locator ||
            cached.production_seed !=
                source.production_seed ||
            cached.information_action_sha256 !=
                source.information_action_sha256 ||
            cached.descriptor_set_sha256 !=
                source.descriptor_set_sha256 ||
            cached.pass_index != source.pass_index ||
            cached.canonical_actions.size() !=
                source.actions.size()) {
            fail("neutral source-row join drifted");
        }
        for (std::size_t action = 0;
             action < source.actions.size(); ++action) {
            if (source.actions[action].is_pass !=
                (action == cached.pass_index)) {
                fail("neutral source Pass order drifted");
            }
        }
    }
}

std::string encode(const Artifact& artifact) {
    validate(artifact);
    const std::string payload =
        encode_payload(artifact);
    Writer envelope;
    envelope.raw(std::string_view(
        kMagic.data(), kMagic.size()));
    envelope.u32(kWireVersion);
    envelope.u32(kEndianMarker);
    envelope.u64(payload.size());
    envelope.hash(bundle::sha256(payload));
    envelope.raw(payload);
    envelope.hash(bundle::sha256(envelope.bytes()));
    return envelope.take();
}

Artifact decode(std::string_view bytes) {
    constexpr std::size_t kEnvelopeBytes =
        kMagic.size() + 4 + 4 + 8 + 32 + 32;
    if (bytes.size() < kEnvelopeBytes ||
        bytes.size() > kMaximumCacheBytes ||
        bytes.substr(0, kMagic.size()) !=
            std::string_view(
                kMagic.data(), kMagic.size())) {
        fail("cache envelope is invalid");
    }
    Reader envelope(bytes.substr(kMagic.size()));
    if (envelope.u32("wire version") != kWireVersion ||
        envelope.u32("endian marker") != kEndianMarker) {
        fail("wire version or endian marker drifted");
    }
    const std::uint64_t payload_size =
        envelope.u64("payload size");
    if (payload_size >
            kMaximumCacheBytes ||
        payload_size >
            bytes.size() - kEnvelopeBytes) {
        fail("payload size is invalid");
    }
    const Hash256 expected_payload =
        envelope.hash("payload hash");
    const std::size_t payload_offset =
        kMagic.size() + 4 + 4 + 8 + 32;
    if (payload_offset + payload_size + 32 !=
        bytes.size()) {
        fail("cache envelope length drifted");
    }
    const std::string_view payload =
        bytes.substr(
            payload_offset,
            static_cast<std::size_t>(payload_size));
    if (bundle::sha256(payload) != expected_payload) {
        fail("payload checksum mismatch");
    }
    Hash256 complete{};
    std::copy_n(
        reinterpret_cast<const std::uint8_t*>(
            bytes.data() + bytes.size() - 32),
        complete.size(), complete.begin());
    if (bundle::sha256(
            bytes.substr(0, bytes.size() - 32)) !=
        complete) {
        fail("complete checksum mismatch");
    }
    Artifact result = decode_payload(payload);
    validate(result);
    if (encode(result) != bytes) {
        fail("cache encoding is noncanonical");
    }
    return result;
}

std::string encoded_sha256(const Artifact& artifact) {
    return artifact_integrity::sha256_string(
        encode(artifact));
}

namespace testing {

std::string encode_root(const Root& root) {
    require_root_semantics(root);
    Writer output;
    write_root(output, root);
    return output.take();
}

Root decode_root(std::string_view bytes) {
    if (bytes.empty() ||
        bytes.size() > kMaximumCacheBytes) {
        fail("test root wire size is invalid");
    }
    Reader input(bytes);
    Root result = read_root(input);
    input.finish();
    require_root_semantics(result);
    if (encode_root(result) != bytes) {
        fail("test root encoding is noncanonical");
    }
    return result;
}

std::string encode_priority_actions(
    std::span<const PriorityAction> actions) {
    if (actions.size() >
        bundle::kMaximumActions) {
        fail("test Priority action vector exceeds its bound");
    }
    Writer output(1U << 20U);
    output.u32(
        static_cast<std::uint32_t>(
            actions.size()));
    for (const PriorityAction& action : actions) {
        write_priority_action(output, action);
    }
    std::string bytes = output.take();

    Reader check(bytes);
    const std::size_t count =
        check.bounded_u32(
            "test Priority action count",
            bundle::kMaximumActions);
    std::vector<PriorityAction> decoded;
    decoded.reserve(count);
    for (std::size_t index = 0;
         index < count; ++index) {
        decoded.push_back(
            read_priority_action(check));
    }
    check.finish();
    if (!std::equal(
            decoded.begin(), decoded.end(),
            actions.begin(), actions.end())) {
        fail("test Priority action encoding is not decodable");
    }
    return bytes;
}

std::vector<PriorityAction> decode_priority_actions(
    std::string_view bytes) {
    if (bytes.size() > (1U << 20U)) {
        fail("test Priority action wire exceeds its bound");
    }
    Reader input(bytes);
    const std::size_t count =
        input.bounded_u32(
            "test Priority action count",
            bundle::kMaximumActions);
    std::vector<PriorityAction> result;
    result.reserve(count);
    for (std::size_t index = 0;
         index < count; ++index) {
        result.push_back(
            read_priority_action(input));
    }
    input.finish();
    if (encode_priority_actions(result) != bytes) {
        fail("test Priority action encoding is noncanonical");
    }
    return result;
}

void validate_census(std::span<const Root> roots) {
    require_frozen_census(roots);
}

} // namespace testing

} // namespace old_school::fq4_work0_cache
