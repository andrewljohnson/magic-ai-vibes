#include "old_school/fq4_d1_field_gate.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_dominance_transition.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/oc1_action_scoring.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::fq4_d1_field_gate {
namespace {

namespace integrity = artifact_integrity;
namespace dominance = fq0_dominance;
namespace transition = fq0_dominance_transition;
namespace information = fq0_information_set;
namespace scoring = oc1_action_scoring;
namespace probe_data = probes;
namespace collection = fq4_priority_collection;

constexpr std::size_t kPlayerCount = 2;
constexpr std::string_view kOwnerInformationSchema =
    "old-school-fq4-d1-p0-owner-information-action-v1";
constexpr std::string_view kStableRootSchema =
    "old-school-fq4-d1-p0-stable-root-v1";

[[maybe_unused]] const collection::CollectionSpec&
collection_spec() {
    static constexpr collection::CollectionSpec spec{
        .owner_information_schema =
            kOwnerInformationSchema,
        .stable_root_schema = kStableRootSchema,
        .hidden_seed_namespace = kHiddenSeedNamespace,
        .hidden_seed_scope =
            "old-school-fq4-d1-p0-hidden-v1",
        .dominance_seed_namespace =
            kDominanceSeedNamespace,
        .dominance_seed_scope =
            "old-school-fq4-d1-p0-dominance-v1",
        .maximum_legal_actions = kMaximumLegalActions,
        .maximum_roots_per_owner_game =
            kMaximumRootsPerOwnerGame,
        .dominance_worlds = kDominanceWorlds,
    };
    return spec;
}

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "FQ4-D1-P0 deck is outside the five-deck field");
    }
    return result;
}

std::vector<CardId> cards_for_deck(DeckId deck) {
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
    throw std::invalid_argument("unknown FQ4-D1-P0 deck");
}

bool same_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool bit_identical(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index) {
        if (!same_double(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool tensor_bits_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t action = 0;
         action < first.size(); ++action) {
        if (!bit_identical(
                first[action], second[action])) {
            return false;
        }
    }
    return true;
}

void append_field(std::string& output, std::uint64_t value) {
    output += std::to_string(value);
}

template <typename Value>
void append_tab_field(std::string& output, const Value& value) {
    if (!output.empty() && output.back() != '\n') {
        output.push_back('\t');
    }
    append_field(output, value);
}

void digest_u64(
    integrity::Sha256Accumulator& digest,
    std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >>
             (8U * static_cast<unsigned>(
                        bytes.size() - 1 - index))) &
            0xffU);
    }
    digest.update(bytes);
}

void digest_bool(
    integrity::Sha256Accumulator& digest, bool value) {
    digest_u64(digest, value ? 1U : 0U);
}

void digest_string(
    integrity::Sha256Accumulator& digest,
    std::string_view value) {
    digest_u64(digest, value.size());
    digest.update(value);
}

void digest_double(
    integrity::Sha256Accumulator& digest, double value) {
    digest_u64(
        digest, std::bit_cast<std::uint64_t>(value));
}

void digest_string_vector(
    integrity::Sha256Accumulator& digest,
    const std::vector<std::string>& values) {
    digest_u64(digest, values.size());
    for (const std::string& value : values) {
        digest_string(digest, value);
    }
}

void digest_double_vector(
    integrity::Sha256Accumulator& digest,
    const std::vector<double>& values) {
    digest_u64(digest, values.size());
    for (const double value : values) {
        digest_double(digest, value);
    }
}

class ByteWriter {
  public:
    template <typename Integer>
    void integer(Integer value) {
        using Unsigned = std::make_unsigned_t<Integer>;
        const Unsigned converted =
            static_cast<Unsigned>(value);
        for (std::size_t byte = 0;
             byte < sizeof(Unsigned); ++byte) {
            data_.push_back(static_cast<char>(
                static_cast<unsigned char>(
                    converted >> (byte * 8U))));
        }
    }

    void boolean(bool value) {
        data_.push_back(value ? '\1' : '\0');
    }

    void text(std::string_view value) {
        integer<std::uint64_t>(value.size());
        data_.append(value);
    }

    template <typename Values, typename Append>
    void sequence(const Values& values, Append append) {
        integer<std::uint64_t>(values.size());
        for (const auto& value : values) {
            append(*this, value);
        }
    }

    std::string take() {
        return std::move(data_);
    }

  private:
    std::string data_;
};

void append_card(ByteWriter& writer, CardId card) {
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(card));
}

void append_mana(ByteWriter& writer, const ManaCost& mana) {
    writer.integer(mana.generic);
    writer.integer(mana.green);
    writer.integer(mana.red);
    writer.integer(mana.blue);
    writer.integer(mana.white);
}

void append_target(ByteWriter& writer, const Target& target) {
    writer.integer<std::uint64_t>(target.player);
    writer.boolean(target.creature.has_value());
    if (target.creature.has_value()) {
        writer.integer<std::uint64_t>(
            *target.creature);
    }
}

void append_public_player(
    ByteWriter& writer, const PublicPlayerState& player) {
    writer.integer(player.life);
    writer.integer<std::uint64_t>(player.library_size);
    writer.integer<std::uint64_t>(player.hand_size);
    const auto append_cards =
        [](ByteWriter& output,
           const std::vector<CardId>& cards) {
            output.sequence(
                cards,
                [](ByteWriter& inner, CardId card) {
                    append_card(inner, card);
                });
        };
    append_cards(writer, player.graveyard);
    append_cards(writer, player.exile);
    writer.sequence(
        player.lands,
        [](ByteWriter& output,
           const LandPermanent& land) {
            append_card(output, land.card);
            output.boolean(land.tapped);
        });
    writer.sequence(
        player.creatures,
        [](ByteWriter& output,
           const CreaturePermanent& creature) {
            output.integer<std::uint64_t>(creature.id);
            append_card(output, creature.card);
            output.boolean(creature.tapped);
            output.boolean(creature.summoning_sick);
            output.integer(creature.damage);
            output.integer(
                creature.temporary_power_bonus);
            output.integer(
                creature.temporary_toughness_bonus);
            output.boolean(
                creature.exile_on_death_this_turn);
        });
    writer.sequence(
        player.artifacts,
        [](ByteWriter& output,
           const ArtifactPermanent& artifact) {
            output.integer<std::uint64_t>(artifact.id);
            append_card(output, artifact.card);
            output.boolean(artifact.tapped);
        });
    append_cards(writer, player.enchantments);
    append_mana(writer, player.mana_pool);
    writer.boolean(player.land_played_this_turn);
}

void append_priority_action(
    ByteWriter& writer, const PriorityAction& action) {
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(action.kind));
    append_card(writer, action.card);
    writer.boolean(action.target.has_value());
    if (action.target.has_value()) {
        append_target(writer, *action.target);
    }
    writer.boolean(action.spell_target.has_value());
    if (action.spell_target.has_value()) {
        writer.integer<std::uint64_t>(
            *action.spell_target);
    }
    writer.boolean(
        action.source_permanent.has_value());
    if (action.source_permanent.has_value()) {
        writer.integer<std::uint64_t>(
            *action.source_permanent);
    }
    writer.integer(action.x_value);
}

std::string owner_information_action_bytes(
    const GameState& state,
    const LearnedDecisionContext& context,
    const std::vector<PriorityAction>& legal_actions) {
    if (!context.valid ||
        context.decision_player >= kPlayerCount) {
        throw std::invalid_argument(
            "invalid owner-safe information/action context");
    }
    const PlayerObservation observation =
        observe_game_state(
            state, context.decision_player);
    if (observation.revealed_opponent_hand.has_value()) {
        throw std::logic_error(
            "owner-safe observation exposed opponent hand");
    }
    ByteWriter writer;
    writer.text(
        "old-school-fq4-d1-p0-owner-information-action-v1");
    writer.integer<std::uint64_t>(observation.observer);
    for (const PublicPlayerState& player :
         observation.players) {
        append_public_player(writer, player);
    }
    writer.sequence(
        observation.hand,
        [](ByteWriter& output, CardId card) {
            append_card(output, card);
        });
    writer.sequence(
        observation.stack,
        [](ByteWriter& output,
           const StackObject& object) {
            output.integer<std::uint64_t>(
                static_cast<std::uint64_t>(
                    object.kind));
            output.integer<std::uint64_t>(object.id);
            append_card(output, object.card);
            output.integer<std::uint64_t>(
                object.controller);
            output.boolean(object.target.has_value());
            if (object.target.has_value()) {
                append_target(output, *object.target);
            }
            output.boolean(
                object.spell_target.has_value());
            if (object.spell_target.has_value()) {
                output.integer<std::uint64_t>(
                    *object.spell_target);
            }
            output.integer(object.x_value);
        });
    for (const std::size_t extra :
         observation.extra_turns_pending) {
        writer.integer<std::uint64_t>(extra);
    }
    writer.integer<std::uint64_t>(
        observation.active_player);
    writer.integer<std::uint64_t>(
        observation.starting_player);
    writer.integer<std::uint64_t>(
        observation.turn_number);
    writer.boolean(context.valid);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(context.phase));
    writer.integer<std::uint64_t>(
        context.decision_player);
    writer.integer(context.consecutive_passes);
    writer.boolean(context.sorcery_actions);
    writer.sequence(
        legal_actions,
        [](ByteWriter& output,
           const PriorityAction& action) {
            append_priority_action(output, action);
        });
    return writer.take();
}

bool sorcery_actions_for(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
    case TurnPhase::SecondMain:
        return true;
    case TurnPhase::BeginCombat:
    case TurnPhase::EndCombat:
        return false;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        return false;
    }
    return false;
}

bool priority_phase(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::BeginCombat ||
           phase == TurnPhase::EndCombat ||
           phase == TurnPhase::SecondMain;
}

std::uint64_t hidden_seed(
    const RootLocator& locator, std::size_t repartition) {
    if (repartition != 0) {
        throw std::invalid_argument(
            "canonical hidden seed has no repartition knob");
    }
    const std::string group =
        "source_seed_base=" +
        std::to_string(locator.source_seed_base) +
        "\nschedule=" +
        std::to_string(locator.schedule_index) +
        "\nowner=" +
        std::to_string(locator.owner_seat) +
        "\ntrace=" +
        std::to_string(locator.trace_ordinal) +
        "\n";
    return information::derive_indexed_seed(
        kHiddenSeedNamespace,
        {
            .domain =
                information::SeedDomain::
                    RootDeterminization,
            .scope =
                "old-school-fq4-d1-p0-hidden-v1",
            .group = group,
            .bank = information::SeedBank::Root,
            .block = 0,
            .world = 0,
        });
}

std::uint64_t dominance_world_seed(
    const ReplayRootManifest& manifest,
    std::size_t world) {
    return information::derive_indexed_seed(
        kDominanceSeedNamespace,
        {
            .domain =
                information::SeedDomain::
                    RootDeterminization,
            .scope =
                "old-school-fq4-d1-p0-dominance-v1",
            .group = manifest.stable_id,
            .bank = information::SeedBank::Root,
            .block = 0,
            .world = world,
        });
}

std::array<std::vector<CardId>, 2> original_decks(
    const SourceGame& source) {
    return {
        cards_for_deck(source.seat_decks[0]),
        cards_for_deck(source.seat_decks[1]),
    };
}

struct HiddenClone {
    probe_data::DecisionProbe probe;
    bool eligible = false;
    bool distinct = false;
};

bool hidden_zones_differ(
    const GameState& first, const GameState& second,
    std::size_t observer) {
    return first.players[observer].library !=
               second.players[observer].library ||
           first.players[1 - observer].hand !=
               second.players[1 - observer].hand ||
           first.players[1 - observer].library !=
               second.players[1 - observer].library;
}

HiddenClone make_hidden_clone(
    const probe_data::DecisionProbe& source,
    const RootLocator& locator) {
    static_cast<void>(locator);
    HiddenClone result{.probe = source};
    const std::size_t owner = source.root_player;
    if (owner >= kPlayerCount) {
        throw std::invalid_argument(
            "hidden clone owner must be a player");
    }
    PlayerState& opponent =
        result.probe.state.players[1 - owner];
    PlayerState& owner_state =
        result.probe.state.players[owner];

    // Frozen first-distinct-pair order from the declaration.
    for (std::size_t hand = 0;
         hand < opponent.hand.size(); ++hand) {
        for (std::size_t library = 0;
             library < opponent.library.size();
             ++library) {
            if (opponent.hand[hand] !=
                opponent.library[library]) {
                result.eligible = true;
                std::swap(
                    opponent.hand[hand],
                    opponent.library[library]);
                result.distinct = true;
                return result;
            }
        }
    }
    const auto swap_within =
        [&](std::vector<CardId>& cards) {
            for (std::size_t first = 0;
                 first < cards.size(); ++first) {
                for (std::size_t second = first + 1;
                     second < cards.size(); ++second) {
                    if (cards[first] != cards[second]) {
                        result.eligible = true;
                        std::swap(
                            cards[first], cards[second]);
                        result.distinct = true;
                        return true;
                    }
                }
            }
            return false;
        };
    if (swap_within(opponent.library) ||
        swap_within(owner_state.library) ||
        swap_within(opponent.hand)) {
        return result;
    }
    if (hidden_zones_differ(
            source.state, result.probe.state,
            owner)) {
        throw std::logic_error(
            "vacuous hidden clone changed hidden state");
    }
    result.eligible = false;
    result.distinct = false;
    return result;
}

struct CanonicalRoot {
    probe_data::DecisionProbe probe;
    ReplayRootManifest manifest;
    std::string information_action_bytes;
    std::string selected_descriptor;
};

struct RootBuildAttempt {
    std::optional<CanonicalRoot> root;
    enum class Category : std::uint8_t {
        Trivial,
        Nontrivial,
        Malformed,
        OverCap,
    } category = Category::Malformed;
    std::string trajectory_row;
    std::string error;
};

RootBuildAttempt build_safe_root(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal) {
    RootBuildAttempt result;
    if (!point.context.valid ||
        point.context.decision_player != owner_seat ||
        owner_seat >= kPlayerCount ||
        point.context.consecutive_passes < 0 ||
        point.context.consecutive_passes > 1 ||
        !priority_phase(point.context.phase) ||
        point.context.sorcery_actions !=
            sorcery_actions_for(point.context.phase)) {
        result.error = "invalid Priority trace context";
        return result;
    }
    const RootLocator locator{
        .source_block = source.source_block,
        .source_seed_base = source.source_seed_base,
        .schedule_index = source.schedule_index,
        .game_seed = source.game_seed,
        .owner_seat = owner_seat,
        .trace_ordinal = trace_ordinal,
    };
    const auto decks = original_decks(source);
    GameState safe;
    try {
        // This is deliberately the first operation that can expose a root
        // state to later hashing or selection. Both libraries and the
        // opponent hand are reconstructed from the owner information set;
        // the owner's hand is preserved by sample_determinization.
        safe = sample_determinization(
            point.state, decks, owner_seat,
            hidden_seed(locator, 0));
        safe.stats = {};
        if (safe.players[owner_seat].hand !=
            point.state.players[owner_seat].hand) {
            throw std::logic_error(
                "owner hand changed during canonical "
                "determinization");
        }
    } catch (const std::exception& error) {
        result.error =
            std::string("owner-safe determinization failed: ") +
            error.what();
        return result;
    }
    const auto legal = legal_priority_actions(
        safe, owner_seat, point.context.sorcery_actions);
    if (!point.selected_priority_action.has_value()) {
        result.error = "trace root has no selected action";
        return result;
    }
    const std::size_t selected_matches =
        static_cast<std::size_t>(std::count(
            legal.begin(), legal.end(),
            *point.selected_priority_action));
    if (selected_matches != 1) {
        result.error =
            "selected action does not occur exactly once";
        return result;
    }
    if (legal.size() < 2) {
        if (legal.size() != 1 ||
            legal.front().kind !=
                PriorityActionKind::Pass) {
            result.error =
                "trivial legal set is not exactly one Pass";
            return result;
        }
        try {
            const std::string descriptor =
                probe_data::
                    stable_priority_action_descriptor(
                        legal.front());
            if (descriptor.empty()) {
                throw std::logic_error(
                    "trivial Pass descriptor is empty");
            }
            const std::string information_bytes =
                owner_information_action_bytes(
                    safe, point.context, legal);
            const std::string information_fingerprint =
                integrity::sha256_string(
                    information_bytes);
            result.category =
                RootBuildAttempt::Category::Trivial;
            result.trajectory_row =
                fq4_d1_field_gate::physical_game_id(
                    locator) + "\t" +
                std::to_string(trace_ordinal) + "\t" +
                information_fingerprint + "\t" +
                descriptor + "\ttrivial\n";
        } catch (const std::exception& error) {
            result.error =
                std::string(
                    "trivial root canonicalization failed: ") +
                error.what();
        }
        return result;
    }

    std::vector<information::CanonicalActionRow> rows;
    try {
        const information::InformationSetKey key =
            information::make_information_set_key(
                safe, point.context, legal);
        rows =
            information::descriptor_canonical_action_rows(
                key);
    } catch (const std::exception& error) {
        result.error =
            std::string("canonical action set failed: ") +
            error.what();
        return result;
    }
    if (rows.size() != legal.size()) {
        result.error =
            "canonical action set is incomplete";
        return result;
    }
    std::vector<PriorityAction> canonical_actions;
    canonical_actions.reserve(rows.size());
    for (const auto& row : rows) {
        canonical_actions.push_back(row.action);
    }
    std::set<std::string> descriptor_set;
    for (const auto& row : rows) {
        if (!descriptor_set.insert(row.descriptor).second) {
            result.error =
                "canonical action descriptors are not unique";
            return result;
        }
    }
    const std::string information_bytes =
        owner_information_action_bytes(
            safe, point.context, canonical_actions);
    const std::string information_fingerprint =
        integrity::sha256_string(information_bytes);

    std::size_t pass_count = 0;
    std::size_t pass_index = 0;
    std::string selected_descriptor;
    probe_data::DecisionProbe probe{
        .stable_id = {},
        .category = probe_data::Category::GreenDevelop,
        .decision_kind =
            probe_data::DecisionKind::Priority,
        .root_deck = source.seat_decks[owner_seat],
        .opponent_deck =
            source.seat_decks[1 - owner_seat],
        .root_player = owner_seat,
        .phase = point.context.phase,
        .consecutive_passes =
            point.context.consecutive_passes,
        .state = std::move(safe),
        .original_decks = decks,
    };
    std::vector<std::string> descriptors;
    descriptors.reserve(rows.size());
    probe.candidates.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size();
         ++index) {
        const auto& row = rows[index];
        descriptors.push_back(row.descriptor);
        probe.candidates.push_back({
            .descriptor = row.descriptor,
            .action = row.action,
        });
        if (row.action.kind == PriorityActionKind::Pass) {
            ++pass_count;
            pass_index = index;
        }
        if (row.action == *point.selected_priority_action) {
            selected_descriptor = row.descriptor;
        }
    }
    if (pass_count != 1 || selected_descriptor.empty()) {
        result.error =
            "canonical root lacks one Pass or selected descriptor";
        return result;
    }
    if (legal.size() > kMaximumLegalActions) {
        result.category =
            RootBuildAttempt::Category::OverCap;
        result.trajectory_row =
            fq4_d1_field_gate::physical_game_id(
                locator) + "\t" +
            std::to_string(trace_ordinal) + "\t" +
            information_fingerprint +
            "\t" + selected_descriptor +
            "\tover-cap\n";
        return result;
    }
    const std::string root_id =
        fq4_d1_field_gate::stable_root_id(
            locator, information_fingerprint);
    probe.stable_id = root_id;
    result.root = CanonicalRoot{
        .probe = std::move(probe),
        .manifest = {
            .locator = locator,
            .owner_deck =
                source.seat_decks[owner_seat],
            .opponent_deck =
                source.seat_decks[1 - owner_seat],
            .stable_id = root_id,
            .information_action_fingerprint =
                information_fingerprint,
            .canonical_descriptors =
                std::move(descriptors),
            .pass_index = pass_index,
        },
        .information_action_bytes =
            information_bytes,
        .selected_descriptor =
            std::move(selected_descriptor),
    };
    result.category =
        RootBuildAttempt::Category::Nontrivial;
    result.trajectory_row =
        fq4_d1_field_gate::physical_game_id(
            locator) + "\t" +
        std::to_string(trace_ordinal) + "\t" +
        information_fingerprint + "\t" +
        result.root->selected_descriptor + "\n";
    return result;
}

bool replay_exact(
    const CanonicalRoot& root, const HiddenClone& clone) {
    if (observe_game_state(
            root.probe.state,
            root.probe.root_player) !=
        observe_game_state(
            clone.probe.state,
            clone.probe.root_player)) {
        return false;
    }
    const auto legal = legal_priority_actions(
        clone.probe.state, clone.probe.root_player,
        sorcery_actions_for(clone.probe.phase));
    const LearnedDecisionContext context{
        .valid = true,
        .phase = clone.probe.phase,
        .decision_player =
            clone.probe.root_player,
        .consecutive_passes =
            clone.probe.consecutive_passes,
        .sorcery_actions =
            sorcery_actions_for(clone.probe.phase),
    };
    const auto key =
        information::make_information_set_key(
            clone.probe.state, context, legal);
    const auto rows =
        information::descriptor_canonical_action_rows(key);
    if (rows.size() != root.probe.candidates.size()) {
        return false;
    }
    std::vector<PriorityAction> canonical_actions;
    canonical_actions.reserve(rows.size());
    for (const auto& row : rows) {
        canonical_actions.push_back(row.action);
    }
    const std::string bytes =
        owner_information_action_bytes(
            clone.probe.state, context,
            canonical_actions);
    if (bytes != root.information_action_bytes ||
        integrity::sha256_string(bytes) !=
            root.manifest
                .information_action_fingerprint) {
        return false;
    }
    for (std::size_t index = 0; index < rows.size();
         ++index) {
        const auto* action =
            std::get_if<PriorityAction>(
                &root.probe.candidates[index].action);
        if (action == nullptr ||
            rows[index].descriptor !=
                root.probe.candidates[index].descriptor ||
            rows[index].action != *action) {
            return false;
        }
    }
    return true;
}

std::vector<PriorityAction> priority_actions(
    const probe_data::DecisionProbe& probe) {
    std::vector<PriorityAction> result;
    result.reserve(probe.candidates.size());
    for (const auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr) {
            throw std::logic_error(
                "FQ4-D1-P0 replay contains a non-Priority action");
        }
        result.push_back(*action);
    }
    return result;
}

std::vector<std::vector<double>> priority_option_features(
    const probe_data::DecisionProbe& probe) {
    const auto actions = priority_actions(probe);
    std::vector<std::vector<double>> result;
    result.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        result.push_back(
            learned_priority_policy_features(
                probe.state, probe.root_player, action,
                sorcery_actions_for(probe.phase),
                probe.phase,
                probe.consecutive_passes));
    }
    return result;
}

[[maybe_unused]] bool priority_feature_bits_identical(
    const probe_data::DecisionProbe& visible,
    const probe_data::DecisionProbe& hidden) {
    const auto first = priority_option_features(visible);
    const auto second = priority_option_features(hidden);
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t action = 0;
         action < first.size(); ++action) {
        if (!bit_identical(first[action], second[action])) {
            return false;
        }
    }
    return true;
}

RobustDominance evaluate_dominance(
    const CanonicalRoot& root,
    std::vector<std::string>& infrastructure_failures) {
    const std::size_t action_count =
        root.probe.candidates.size();
    std::vector<DominanceWorldRow> rows;
    rows.reserve(kDominanceWorlds);
    for (std::size_t world = 0;
         world < kDominanceWorlds; ++world) {
        DominanceWorldRow row{
            .candidate_complete =
                std::vector<bool>(action_count, false),
            .orientations =
                std::vector<dominance::Orientation>(
                    action_count,
                    dominance::Orientation::Incomparable),
        };
        std::optional<GameState> sampled;
        try {
            sampled = sample_determinization(
                root.probe.state,
                root.probe.original_decks,
                root.probe.root_player,
                dominance_world_seed(
                    root.manifest, world));
        } catch (const std::exception& error) {
            infrastructure_failures.push_back(
                root.manifest.stable_id +
                ": dominance world sampling exception: " +
                error.what());
            rows.push_back(std::move(row));
            continue;
        }
        // One sampled information-set world is reused for Pass and every
        // alternative at this coordinate. A Pass exception does not
        // short-circuit later candidate settlement attempts.
        std::optional<dominance::Settlement> pass;
        try {
            pass.emplace(
                transition::advance_to_next_first_main(
                    root.probe, *sampled,
                    root.manifest.pass_index,
                    root.manifest
                        .information_action_fingerprint));
            row.pass_complete =
                pass->complete() &&
                !pass
                     ->unresolved_transient_choice_effect();
            row.candidate_complete[
                root.manifest.pass_index] =
                row.pass_complete;
        } catch (const std::exception& error) {
            infrastructure_failures.push_back(
                root.manifest.stable_id +
                ": dominance Pass exception: " +
                error.what());
        }
        for (std::size_t action = 0;
             action < action_count; ++action) {
            if (action == root.manifest.pass_index) {
                continue;
            }
            try {
                const dominance::Settlement candidate =
                    transition::advance_to_next_first_main(
                        root.probe, *sampled, action,
                        root.manifest
                            .information_action_fingerprint);
                row.candidate_complete[action] =
                    candidate.complete() &&
                    !candidate
                         .unresolved_transient_choice_effect();
                if (row.pass_complete &&
                    row.candidate_complete[action] &&
                    pass.has_value()) {
                    row.orientations[action] =
                        dominance::compare(
                            *pass, candidate,
                            root.probe.root_player)
                            .orientation;
                }
            } catch (const std::exception& error) {
                infrastructure_failures.push_back(
                    root.manifest.stable_id +
                    ": dominance candidate exception: " +
                    error.what());
            }
        }
        rows.push_back(std::move(row));
    }
    return fq4_d1_field_gate::
        summarize_robust_dominance(
        root.manifest.pass_index,
        action_count, rows);
}

bool production_recipe_exact(
    const scoring::DecisionScore& score) {
    const auto& recipe = score.recipe;
    return
        score.decision_kind ==
            probe_data::DecisionKind::Priority &&
        score.score_mode ==
            scoring::ScoreMode::
                ProductionPrioritySearch &&
        recipe.seed_source ==
            scoring::SeedSource::Derived &&
        recipe.seed_tag == scoring::kProductionTag &&
        recipe.seed_base ==
            scoring::kProductionSeedBase &&
        recipe.resolved_seed.has_value() &&
        recipe.worlds == scoring::kProductionWorlds &&
        recipe.horizon_turns ==
            scoring::kProductionHorizonTurns &&
        recipe.rollouts_per_world ==
            scoring::kProductionRolloutsPerWorld &&
        recipe.blend_shallow_prior ==
            scoring::kProductionBlendShallowPrior &&
        recipe.evaluation_threads ==
            scoring::kProductionEvaluationThreads &&
        recipe.value_mirror &&
        same_double(
            recipe.value_continuation_epsilon, 0.0) &&
        same_double(
            recipe.value_priority_residual_weight, 0.0) &&
        !recipe.value_pass_dominance &&
        recipe.value_continuation_controller ==
            LearnedContinuationController::Legacy &&
        score.accounting.sampled_worlds ==
            scoring::kProductionWorlds &&
        score.accounting.rollout_evaluations ==
            score.actions.size() *
                scoring::kProductionWorlds *
                scoring::kProductionRolloutsPerWorld &&
        score.accounting.terminal_evaluations +
                score.accounting.bootstrapped_evaluations ==
           score.accounting.rollout_evaluations;
}

void digest_decision_score(
    integrity::Sha256Accumulator& digest,
    const scoring::DecisionScore& score) {
    digest_string(digest, score.stable_id);
    digest_u64(
        digest,
        static_cast<std::uint64_t>(
            score.decision_kind));
    digest_u64(
        digest,
        static_cast<std::uint64_t>(score.score_mode));
    const auto& recipe = score.recipe;
    digest_u64(
        digest,
        static_cast<std::uint64_t>(
            recipe.seed_source));
    digest_string(digest, recipe.seed_tag);
    digest_u64(digest, recipe.seed_base);
    digest_bool(
        digest, recipe.resolved_seed.has_value());
    if (recipe.resolved_seed.has_value()) {
        digest_u64(digest, *recipe.resolved_seed);
    }
    digest_u64(digest, recipe.worlds);
    digest_u64(digest, recipe.horizon_turns);
    digest_u64(digest, recipe.rollouts_per_world);
    digest_bool(digest, recipe.blend_shallow_prior);
    digest_u64(digest, recipe.evaluation_threads);
    digest_bool(digest, recipe.value_mirror);
    digest_double(
        digest, recipe.value_continuation_epsilon);
    digest_double(
        digest, recipe.value_priority_residual_weight);
    digest_bool(digest, recipe.value_pass_dominance);
    digest_u64(
        digest,
        static_cast<std::uint64_t>(
            recipe.value_continuation_controller));
    digest_u64(digest, score.actions.size());
    for (const auto& action : score.actions) {
        digest_string(digest, action.descriptor);
        digest_double_vector(
            digest, action.raw_samples);
        digest_double(digest, action.raw_score);
    }
    digest_string_vector(
        digest, score.selected_support);
    digest_bool(
        digest, score.deterministic_selection);
    digest_u64(
        digest, score.accounting.sampled_worlds);
    digest_u64(
        digest, score.accounting.rollout_evaluations);
    digest_u64(
        digest, score.accounting.terminal_evaluations);
    digest_u64(
        digest,
        score.accounting.bootstrapped_evaluations);
}

ProductionAccounting accounting_for_score(
    const scoring::DecisionScore& score) {
    return {
        .score_calls = 1,
        .scored_actions = score.actions.size(),
        .sampled_worlds =
            score.accounting.sampled_worlds,
        .rollout_evaluations =
            score.accounting.rollout_evaluations,
        .terminal_evaluations =
            score.accounting.terminal_evaluations,
        .bootstrapped_evaluations =
            score.accounting.bootstrapped_evaluations,
    };
}

std::vector<double> residuals_for(
    const CanonicalRoot& root,
    const std::shared_ptr<const LearnedModel>& parent) {
    return diagnose_learned_value_priority_residual(
               root.probe.state,
               root.probe.root_player,
               sorcery_actions_for(root.probe.phase),
               root.probe.phase,
               root.probe.consecutive_passes,
               priority_actions(root.probe),
               parent, kParentResidualWeight)
        .residuals;
}

struct RootScore {
    scoring::DecisionScore base;
    std::vector<double> base_scores;
    std::vector<double> residuals;
    std::vector<double> combined;
    ParentClassResult classification;
    ProductionAccounting accounting;
};

RootScore score_parent_root(
    const CanonicalRoot& root,
    const RobustDominance& robust,
    const std::shared_ptr<const LearnedModel>& parent) {
    RootScore result{
        .base =
            scoring::score_production(
                root.probe, parent),
    };
    result.accounting =
        accounting_for_score(result.base);
    if (!production_recipe_exact(result.base) ||
        result.base.actions.size() !=
            root.manifest.canonical_descriptors.size()) {
        throw std::logic_error(
            root.manifest.stable_id +
            ": production recipe/accounting drifted");
    }
    result.residuals = residuals_for(root, parent);
    if (result.residuals.size() !=
        result.base.actions.size()) {
        throw std::logic_error(
            root.manifest.stable_id +
            ": residual row count drifted");
    }
    std::vector<std::vector<double>> samples;
    samples.reserve(result.base.actions.size());
    for (std::size_t action = 0;
         action < result.base.actions.size(); ++action) {
        const auto& row = result.base.actions[action];
        if (row.descriptor !=
            root.manifest
                .canonical_descriptors[action] ||
            row.raw_samples.size() !=
                scoring::kProductionWorlds) {
            throw std::logic_error(
                root.manifest.stable_id +
                ": production descriptor/sample order drifted");
        }
        result.base_scores.push_back(row.raw_score);
        result.combined.push_back(
            row.raw_score + result.residuals[action]);
        samples.push_back(row.raw_samples);
    }
    result.classification = classify_parent({
        .canonical_descriptors =
            root.manifest.canonical_descriptors,
        .base_scores = result.base_scores,
        .combined_scores = result.combined,
        .base_samples = std::move(samples),
        .robustly_pass_dominated =
            robust.robustly_pass_dominated,
    });
    if (!result.classification.valid) {
        throw std::logic_error(
            root.manifest.stable_id +
            ": parent classification is invalid");
    }
    return result;
}

std::string scored_corpus_digest(
    const std::vector<ScoredRoot>& roots) {
    integrity::Sha256Accumulator digest;
    digest_string(digest, kSchema);
    digest_string(digest, "scored-corpus");
    digest_u64(digest, roots.size());
    for (const ScoredRoot& root : roots) {
        digest_string(digest, root.manifest.stable_id);
        digest_string(
            digest,
            root.manifest
                .information_action_fingerprint);
        digest_string_vector(
            digest,
            root.manifest.canonical_descriptors);
        digest_u64(
            digest, root.manifest.pass_index);
        digest_u64(
            digest,
            root.dominance.strict_world_counts.size());
        for (const std::size_t count :
             root.dominance.strict_world_counts) {
            digest_u64(digest, count);
        }
        digest_u64(
            digest,
            root.dominance
                .robustly_pass_dominated.size());
        for (const bool value :
             root.dominance
                 .robustly_pass_dominated) {
            digest_bool(digest, value);
        }
        digest_u64(
            digest,
            root.dominance.complete_comparisons);
        digest_u64(
            digest,
            root.dominance.transition_count);
        digest_bool(
            digest, root.dominance.shape_valid);
        digest_decision_score(
            digest, root.base_score);
        digest_double_vector(
            digest, root.base_scores);
        digest_string_vector(
            digest, root.base_exact_support);
        digest_double_vector(
            digest, root.residuals);
        digest_double_vector(
            digest, root.combined_scores);
        digest_u64(
            digest,
            static_cast<std::uint64_t>(
                root.parent_class.classification));
        digest_u64(
            digest,
            root.parent_class
                .best_dominated_index);
        digest_u64(
            digest,
            root.parent_class
                .best_nondominated_index);
        digest_double(
            digest, root.parent_class.margin);
        digest_double(
            digest,
            root.parent_class
                .paired_standard_error);
        digest_double(
            digest, root.parent_class.sigma);
        digest_bool(
            digest, root.parent_class.valid);
        digest_u64(
            digest, root.accounting.score_calls);
        digest_u64(
            digest, root.accounting.scored_actions);
        digest_u64(
            digest, root.accounting.sampled_worlds);
        digest_u64(
            digest,
            root.accounting.rollout_evaluations);
        digest_u64(
            digest,
            root.accounting.terminal_evaluations);
        digest_u64(
            digest,
            root.accounting.bootstrapped_evaluations);
        digest_bool(
            digest,
            root.hidden_replay_bit_identical);
        digest_bool(
            digest,
            root.hidden_feature_bits_identical);
    }
    return digest.finish();
}

void digest_root_counts(
    integrity::Sha256Accumulator& digest,
    const RootCounts& counts) {
    digest_u64(digest, counts.raw);
    digest_u64(digest, counts.nontrivial);
    digest_u64(digest, counts.malformed);
    digest_u64(digest, counts.trivial);
    digest_u64(digest, counts.over_cap);
    digest_u64(digest, counts.eligible);
    digest_u64(digest, counts.duplicate);
    digest_u64(digest, counts.unique);
    digest_u64(digest, counts.retained);
    digest_u64(digest, counts.cap_dropped);
    digest_u64(
        digest, counts.dominance_positive);
    for (const std::size_t count :
         counts.parent_classes) {
        digest_u64(digest, count);
    }
}

void digest_robust_dominance(
    integrity::Sha256Accumulator& digest,
    const RobustDominance& robust) {
    digest_u64(digest, robust.pass_index);
    digest_u64(
        digest, robust.strict_world_counts.size());
    for (const std::size_t count :
         robust.strict_world_counts) {
        digest_u64(digest, count);
    }
    digest_u64(
        digest,
        robust.robustly_pass_dominated.size());
    for (const bool dominated :
         robust.robustly_pass_dominated) {
        digest_bool(digest, dominated);
    }
    digest_u64(
        digest, robust.complete_comparisons);
    digest_u64(
        digest, robust.transition_count);
    digest_bool(digest, robust.shape_valid);
}

std::size_t class_index(ParentClass value) {
    switch (value) {
    case ParentClass::Safe:
        return 0;
    case ParentClass::Class1:
        return 1;
    case ParentClass::Class2:
        return 2;
    case ParentClass::Class3:
        return 3;
    case ParentClass::Invalid:
        break;
    }
    throw std::invalid_argument(
        "invalid FQ4-D1-P0 class has no count index");
}

struct RetainedRoot {
    CanonicalRoot root;
    HiddenClone hidden;
    std::vector<std::vector<double>>
        neutral_priority_options;
    std::vector<std::vector<double>>
        hidden_neutral_priority_options;
    bool hidden_feature_bits_identical = false;
};

std::string dominance_corpus_digest(
    const std::vector<RetainedRoot>& roots,
    const std::vector<std::optional<RobustDominance>>&
        labels,
    const std::vector<GameCensus>& games) {
    integrity::Sha256Accumulator digest;
    digest_string(digest, kSchema);
    digest_string(digest, "dominance-corpus");
    digest_u64(digest, roots.size());
    digest_u64(digest, labels.size());
    for (std::size_t index = 0;
         index < roots.size(); ++index) {
        digest_string(
            digest, roots[index].root.manifest.stable_id);
        digest_bool(
            digest, labels[index].has_value());
        if (labels[index].has_value()) {
            digest_robust_dominance(
                digest, *labels[index]);
        }
    }
    digest_u64(digest, games.size());
    for (const GameCensus& game : games) {
        digest_u64(
            digest, game.source.source_block);
        digest_u64(
            digest, game.source.source_seed_base);
        digest_u64(
            digest, game.source.schedule_index);
        digest_u64(
            digest, game.source.game_seed);
        for (const RootCounts& counts :
             game.owners) {
            digest_root_counts(digest, counts);
        }
    }
    return digest.finish();
}

struct Construction {
    CensusReport report;
    std::vector<ReplayRootManifest> retained_manifest;
};

void record_failure(
    CensusReport& report, std::string message) {
    report.infrastructure_failures.push_back(
        std::move(message));
}

bool source_config_exact(
    const GameConfig& config,
    const std::shared_ptr<const LearnedModel>& parent,
    std::size_t starting_player) {
    if (config.max_turns != kSourceTurnCap ||
        config.starting_player !=
            std::optional<std::size_t>(
                starting_player) ||
        config.learned_training_seed !=
            kParentTrainingSeed ||
        config.learned_search_depth != 1 ||
        config.learned_model != parent ||
        config.learned_policy_recorder != nullptr ||
        std::any_of(
            config.human_controllers.begin(),
            config.human_controllers.end(),
            [](const auto& controller) {
                return controller.has_value();
            })) {
        return false;
    }
    for (const auto& bot : config.bots) {
        if (bot.kind != BotKind::Learned ||
            bot.learned_variant !=
                LearnedVariant::
                    ValueSearchChampion ||
            bot.rollouts_per_action !=
                scoring::kProductionWorlds ||
            !same_double(bot.exploration_rate, 0.0) ||
            !same_double(
                bot.value_continuation_epsilon, 0.0) ||
            !same_double(
                bot.value_priority_residual_weight,
                0.0) ||
            bot.value_pass_dominance ||
            bot.value_continuation_controller !=
                LearnedContinuationController::Legacy ||
            bot.training_games !=
                kParentTrainingGames ||
            bot.learned_model != parent) {
            return false;
        }
    }
    return true;
}

GameConfig source_game_config(
    const std::shared_ptr<const LearnedModel>& parent,
    std::size_t starting_player) {
    const BotConfig bot{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action =
            scoring::kProductionWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = kParentTrainingGames,
        .learned_model = parent,
    };
    return {
        .max_turns = kSourceTurnCap,
        .starting_player = starting_player,
        .bots = {bot, bot},
        .learned_training_seed =
            kParentTrainingSeed,
        .learned_model = parent,
        .learned_search_depth = 1,
    };
}

bool evidence_equal(
    const CensusReport& first,
    const CensusReport& second) {
    bool scored_roots_equal =
        first.scored_roots.size() ==
        second.scored_roots.size();
    for (std::size_t index = 0;
         scored_roots_equal &&
         index < first.scored_roots.size();
         ++index) {
        const ScoredRoot& left =
            first.scored_roots[index];
        const ScoredRoot& right =
            second.scored_roots[index];
        scored_roots_equal =
            left == right &&
            tensor_bits_identical(
                left.neutral_priority_options,
                right.neutral_priority_options) &&
            tensor_bits_identical(
                left.hidden_neutral_priority_options,
                right.hidden_neutral_priority_options);
    }
    return
        first.parent_fingerprint ==
            second.parent_fingerprint &&
        first.parent_components ==
            second.parent_components &&
        first.schedule_sha256 ==
            second.schedule_sha256 &&
        first.trajectory_sha256 ==
            second.trajectory_sha256 &&
        first.retained_corpus_sha256 ==
            second.retained_corpus_sha256 &&
        first.dominance_corpus_sha256 ==
            second.dominance_corpus_sha256 &&
        first.scored_corpus_sha256 ==
            second.scored_corpus_sha256 &&
        first.audit_scores_sha256 ==
            second.audit_scores_sha256 &&
        first.schedule_balance ==
            second.schedule_balance &&
        first.games == second.games &&
        first.decks == second.decks &&
        first.deck_game_coverage ==
            second.deck_game_coverage &&
        first.pooled == second.pooled &&
        scored_roots_equal &&
        first.raw_base_dominated_support_by_deck ==
            second.raw_base_dominated_support_by_deck &&
        first.raw_base_mixed_tie_support_by_deck ==
            second.raw_base_mixed_tie_support_by_deck &&
        first.primary_accounting ==
            second.primary_accounting &&
        first.hidden_control_accounting ==
            second.hidden_control_accounting &&
        first.reverse_control_accounting ==
            second.reverse_control_accounting &&
        first.accounting == second.accounting &&
        same_double(
            first.class2_sigma_mass,
            second.class2_sigma_mass) &&
        first.distinct_high_confidence_games ==
            second.distinct_high_confidence_games &&
        first.distinct_high_confidence_decks ==
            second.distinct_high_confidence_decks &&
        first.hidden_replay_roots ==
            second.hidden_replay_roots &&
        first.distinct_hidden_clones ==
            second.distinct_hidden_clones &&
        first.vacuous_hidden_clones ==
            second.vacuous_hidden_clones &&
        first.schedule_preflight_before_games ==
            second.schedule_preflight_before_games &&
        first.parent_contract_exact ==
            second.parent_contract_exact &&
        first.source_config_exact ==
            second.source_config_exact &&
        first.retention_score_blind ==
            second.retention_score_blind &&
        first.all_replays_exact ==
            second.all_replays_exact &&
        first.all_hidden_feature_bits_identical ==
            second.all_hidden_feature_bits_identical &&
        first.first_deck_controls_bit_identical ==
            second.first_deck_controls_bit_identical &&
        first.recipe_and_accounting_exact ==
            second.recipe_and_accounting_exact &&
        first.count_cross_sums_exact ==
            second.count_cross_sums_exact &&
        first.infrastructure_failures ==
            second.infrastructure_failures;
}

Construction construct_once(
    const std::shared_ptr<const LearnedModel>& parent,
    const std::vector<SourceGame>& schedule,
    const std::string& schedule_hash) {
    Construction output;
    CensusReport& report = output.report;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    report.parent_components =
        learned_model_component_fingerprints(parent);
    report.schedule_sha256 = schedule_hash;
    report.schedule_balance =
        audit_schedule_balance(schedule);
    report.schedule_preflight_before_games =
        schedule.size() == kExpectedPhysicalGames &&
        report.schedule_balance.exact &&
        serialize_source_schedule(schedule).size() ==
            kExpectedScheduleBytes &&
        schedule_hash == kExpectedScheduleSha256;
    report.parent_contract_exact =
        report.parent_fingerprint ==
            kRequiredParentFingerprint;
    report.source_config_exact = true;
    report.retention_score_blind = true;
    report.all_replays_exact = true;
    report.all_hidden_feature_bits_identical = true;
    report.first_deck_controls_bit_identical = true;
    report.recipe_and_accounting_exact = true;

    if (!report.schedule_preflight_before_games) {
        record_failure(
            report,
            "schedule preflight digest/size/balance mismatch");
        return output;
    }
    if (!report.parent_contract_exact) {
        record_failure(
            report,
            "parent is not the exact immutable C16 artifact");
        return output;
    }

    std::string trajectory;
    std::vector<RetainedRoot> retained;
    retained.reserve(
        kExpectedOwnerPerspectives *
        kMaximumRootsPerOwnerGame);
    std::map<std::string, std::string>
        global_information_bytes;

    for (const SourceGame& source : schedule) {
        GameCensus game_report{.source = source};
        const auto decks = original_decks(source);
        const GameConfig config =
            source_game_config(
                parent, source.starting_player);
        report.source_config_exact =
            report.source_config_exact &&
            source_config_exact(
                config, parent,
                source.starting_player);
        std::vector<LearnedDecisionTracePoint> trace;
        try {
            Game game(
                decks[0], decks[1],
                source.game_seed, config);
            static_cast<void>(
                game.run_with_priority_root_trace(
                    trace));
        } catch (const std::exception& error) {
            record_failure(
                report,
                "source game " +
                    std::to_string(
                        source.source_block) +
                    "/" +
                    std::to_string(
                        source.schedule_index) +
                    " threw: " + error.what());
            report.games.push_back(
                std::move(game_report));
            continue;
        }

        trajectory +=
            "game\t" +
            std::to_string(source.source_block) + "\t" +
            std::to_string(source.source_seed_base) + "\t" +
            std::to_string(source.schedule_index) + "\t" +
            std::to_string(source.game_seed) + "\n";
        for (std::size_t owner = 0;
             owner < kPlayerCount; ++owner) {
            RootCounts& counts =
                game_report.owners[owner];
            std::vector<CanonicalRoot> candidates;
            std::vector<RetentionCandidate>
                retention_candidates;
            for (std::size_t ordinal = 0;
                 ordinal < trace.size(); ++ordinal) {
                const auto& point = trace[ordinal];
                if (point.context.decision_player !=
                    owner) {
                    continue;
                }
                ++counts.raw;
                RootBuildAttempt attempt =
                    build_safe_root(
                        point, source, owner, ordinal);
                if (attempt.trajectory_row.empty()) {
                    attempt.trajectory_row =
                        "b" +
                        std::to_string(
                            source.source_block) +
                        ".g" +
                        std::to_string(
                            source.schedule_index) +
                        ".p" +
                        std::to_string(owner) +
                        "\t" +
                        std::to_string(ordinal) +
                        "\tmalformed\n";
                }
                trajectory += attempt.trajectory_row;
                switch (attempt.category) {
                case RootBuildAttempt::Category::Trivial:
                    ++counts.trivial;
                    continue;
                case RootBuildAttempt::Category::Malformed:
                    ++counts.malformed;
                    if (!attempt.error.empty()) {
                        record_failure(
                            report,
                            physical_game_id({
                                .source_block =
                                    source.source_block,
                                .source_seed_base =
                                    source.source_seed_base,
                                .schedule_index =
                                    source.schedule_index,
                                .game_seed =
                                    source.game_seed,
                                .owner_seat = owner,
                                .trace_ordinal = ordinal,
                            }) +
                                ": " +
                                attempt.error);
                    }
                    continue;
                case RootBuildAttempt::Category::OverCap:
                    ++counts.over_cap;
                    ++counts.nontrivial;
                    continue;
                case RootBuildAttempt::Category::Nontrivial:
                    break;
                }
                ++counts.nontrivial;
                ++counts.eligible;
                if (!attempt.root.has_value()) {
                    ++counts.malformed;
                    --counts.nontrivial;
                    --counts.eligible;
                    record_failure(
                        report,
                        "nontrivial root was not materialized");
                    continue;
                }
                const auto [known, inserted] =
                    global_information_bytes.emplace(
                        attempt.root->manifest
                            .information_action_fingerprint,
                        attempt.root
                            ->information_action_bytes);
                if (!inserted &&
                    known->second !=
                        attempt.root
                            ->information_action_bytes) {
                    record_failure(
                        report,
                        attempt.root->manifest.stable_id +
                            ": global information/action "
                            "SHA-256 collision");
                }
                retention_candidates.push_back({
                    .trace_ordinal = ordinal,
                    .information_action_fingerprint =
                        attempt.root->manifest
                            .information_action_fingerprint,
                    .information_action_bytes =
                        attempt.root
                            ->information_action_bytes,
                    .stable_id =
                        attempt.root->manifest.stable_id,
                });
                candidates.push_back(
                    std::move(*attempt.root));
            }

            const RetentionResult selection =
                fq4_d1_field_gate::
                    retain_owner_game_roots(
                    retention_candidates);
            if (!selection.valid) {
                record_failure(
                    report,
                    "owner-game retention failed structural "
                    "validation");
            }
            counts.duplicate =
                selection.duplicate_count;
            counts.unique =
                selection.unique_input_indices.size();
            counts.retained =
                selection.retained_input_indices.size();
            counts.cap_dropped =
                counts.unique -
                counts.retained;
            for (const std::size_t index :
                 selection.retained_input_indices) {
                CanonicalRoot root =
                    std::move(candidates[index]);
                HiddenClone hidden =
                    make_hidden_clone(
                        root.probe,
                        root.manifest.locator);
                ++report.hidden_replay_roots;
                if (hidden.distinct) {
                    ++report.distinct_hidden_clones;
                } else {
                    ++report.vacuous_hidden_clones;
                }
                const bool distinct_accounting_exact =
                    hidden.distinct == hidden.eligible;
                const bool replay =
                    distinct_accounting_exact &&
                    replay_exact(root, hidden);
                report.all_replays_exact =
                    report.all_replays_exact && replay;
                if (!replay) {
                    record_failure(
                        report,
                        root.manifest.stable_id +
                            ": hidden replay or distinct/vacuous "
                            "accounting failed");
                }
                retained.push_back({
                    .root = std::move(root),
                    .hidden = std::move(hidden),
                });
            }
        }
        report.games.push_back(
            std::move(game_report));
    }

    if (!report.source_config_exact) {
        record_failure(
            report,
            "source C16 K8/H4/R1 configuration drifted");
    }
    report.trajectory_sha256 =
        integrity::sha256_string(trajectory);

    output.retained_manifest.reserve(retained.size());
    for (const RetainedRoot& root : retained) {
        output.retained_manifest.push_back(
            root.root.manifest);
    }
    if (!validate_replay_manifest(
            output.retained_manifest)) {
        record_failure(
            report,
            "retained replay manifest is invalid");
    }
    report.retained_corpus_sha256 =
        replay_manifest_sha256(
            output.retained_manifest);

    // Phase one freezes every retained root and every rules-owned dominance
    // label before the first parent score or residual is evaluated.
    std::vector<std::optional<RobustDominance>>
        frozen_dominance(retained.size());
    for (std::size_t index = 0;
         index < retained.size(); ++index) {
        RobustDominance robust =
            evaluate_dominance(
                retained[index].root,
                report.infrastructure_failures);
        report.accounting.dominance_transitions +=
            robust.transition_count;
        if (!robust.shape_valid) {
            record_failure(
                report,
                retained[index].root.manifest.stable_id +
                    ": dominance evidence shape is invalid");
            continue;
        }
        if (robust.any_dominated()) {
            const RootLocator& locator =
                retained[index].root.manifest.locator;
            const std::size_t game_index =
                locator.source_block *
                    learned_iteration::
                        kBalancedScheduleGames +
                locator.schedule_index;
            if (game_index >= report.games.size() ||
                locator.owner_seat >= kPlayerCount) {
                record_failure(
                    report,
                    retained[index].root.manifest.stable_id +
                        ": dominance game locator is invalid");
            } else {
                ++report.games[game_index]
                      .owners[locator.owner_seat]
                      .dominance_positive;
            }
        }
        frozen_dominance[index] = std::move(robust);
    }

    report.dominance_corpus_sha256 =
        dominance_corpus_digest(
            retained, frozen_dominance, report.games);

    // Phase two begins only after the complete retained/dominance corpus is
    // immutable. This owner-boundary control compares the neutral input
    // tensors directly and does not invoke the parent model.
    for (std::size_t retained_index = 0;
         retained_index < retained.size();
         ++retained_index) {
        RetainedRoot& retained_root =
            retained[retained_index];
        bool feature_exact = false;
        try {
            auto visible_options =
                priority_option_features(
                    retained_root.root.probe);
            auto hidden_options =
                priority_option_features(
                    retained_root.hidden.probe);
            feature_exact =
                visible_options.size() ==
                    hidden_options.size();
            for (std::size_t action = 0;
                 feature_exact &&
                 action <
                     visible_options.size();
                 ++action) {
                feature_exact =
                    bit_identical(
                        visible_options[action],
                        hidden_options[action]);
            }
            if (frozen_dominance[retained_index]
                    .has_value() &&
                frozen_dominance[retained_index]
                    ->any_dominated()) {
                retained_root.neutral_priority_options =
                    std::move(visible_options);
                retained_root
                    .hidden_neutral_priority_options =
                    std::move(hidden_options);
            }
        } catch (const std::exception& error) {
            record_failure(
                report,
                retained_root.root.manifest.stable_id +
                    ": hidden feature-bit check threw: " +
                    error.what());
        }
        retained_root.hidden_feature_bits_identical =
            feature_exact;
        report.all_hidden_feature_bits_identical =
            report.all_hidden_feature_bits_identical &&
            feature_exact;
    }

    std::array<bool, kDeckCount> deck_control_seen{};
    integrity::Sha256Accumulator audit_scores;
    digest_string(audit_scores, kSchema);
    digest_string(
        audit_scores, "first-retained-score-audits");
    std::set<std::string> high_confidence_games;
    std::set<std::size_t> high_confidence_decks;
    for (std::size_t retained_index = 0;
         retained_index < retained.size();
         ++retained_index) {
        RetainedRoot& retained_root =
            retained[retained_index];
        CanonicalRoot& root = retained_root.root;
        const std::size_t deck =
            deck_index(root.manifest.owner_deck);
        if (!frozen_dominance[retained_index]
                 .has_value()) {
            continue;
        }
        RobustDominance robust =
            *frozen_dominance[retained_index];
        const bool census_score = robust.any_dominated();
        const bool control_score =
            !deck_control_seen[deck];
        if (control_score) {
            deck_control_seen[deck] = true;
        }
        if (!census_score && !control_score) {
            continue;
        }

        std::optional<RootScore> parent_score;
        std::optional<scoring::DecisionScore>
            visible_control_score;
        try {
            if (census_score) {
                parent_score =
                    score_parent_root(
                        root, robust, parent);
                report.primary_accounting +=
                    parent_score->accounting;
            } else {
                visible_control_score =
                    scoring::score_production(
                        root.probe, parent);
                const ProductionAccounting primary =
                    accounting_for_score(
                        *visible_control_score);
                report.primary_accounting += primary;
                if (!production_recipe_exact(
                        *visible_control_score)) {
                    throw std::logic_error(
                        "control production recipe drifted");
                }
            }
        } catch (const std::exception& error) {
            record_failure(
                report,
                root.manifest.stable_id +
                    ": parent scoring threw: " +
                    error.what());
            continue;
        }

        if (control_score) {
            try {
                const auto visible =
                    parent_score.has_value()
                        ? parent_score->base
                        : *visible_control_score;
                const auto hidden =
                    scoring::
                        score_production_hidden_clone(
                            root.probe,
                            retained_root.hidden.probe,
                            parent);
                report.hidden_control_accounting +=
                    accounting_for_score(hidden);
                auto reversed_probe = root.probe;
                std::reverse(
                    reversed_probe.candidates.begin(),
                    reversed_probe.candidates.end());
                const auto reversed =
                    scoring::score_production(
                        reversed_probe, parent);
                report.reverse_control_accounting +=
                    accounting_for_score(reversed);
                const bool exact =
                    production_recipe_exact(visible) &&
                    production_recipe_exact(hidden) &&
                    production_recipe_exact(reversed) &&
                    scoring::bit_identical(
                        visible, hidden) &&
                    scoring::bit_identical(
                        visible, reversed);
                digest_u64(audit_scores, deck);
                digest_decision_score(
                    audit_scores, visible);
                digest_decision_score(
                    audit_scores, hidden);
                digest_decision_score(
                    audit_scores, reversed);
                report.first_deck_controls_bit_identical =
                    report
                        .first_deck_controls_bit_identical &&
                    exact;
                if (!exact) {
                    record_failure(
                        report,
                        root.manifest.stable_id +
                            ": first-deck hidden/reversed scorer "
                            "control drifted");
                }
            } catch (const std::exception& error) {
                report.first_deck_controls_bit_identical =
                    false;
                record_failure(
                    report,
                    root.manifest.stable_id +
                        ": first-deck scorer control threw: " +
                        error.what());
            }
        }
        if (!census_score ||
            !parent_score.has_value()) {
            continue;
        }

        const ParentClassResult classification =
            parent_score->classification;
        bool raw_support_has_dominated = false;
        bool raw_support_has_nondominated = false;
        for (std::size_t action = 0;
             action <
             parent_score->base.actions.size();
             ++action) {
            const std::string& descriptor =
                parent_score->base
                    .actions[action].descriptor;
            if (!std::binary_search(
                    parent_score->base
                        .selected_support.begin(),
                    parent_score->base
                        .selected_support.end(),
                    descriptor)) {
                continue;
            }
            if (robust
                    .robustly_pass_dominated[action]) {
                raw_support_has_dominated = true;
            } else {
                raw_support_has_nondominated = true;
            }
        }
        if (raw_support_has_dominated) {
            ++report
                  .raw_base_dominated_support_by_deck[
                      deck];
        }
        if (raw_support_has_dominated &&
            raw_support_has_nondominated) {
            ++report
                  .raw_base_mixed_tie_support_by_deck[
                      deck];
        }
        ScoredRoot scored{
            .manifest = root.manifest,
            .dominance = std::move(robust),
            .base_score =
                parent_score->base,
            .base_scores =
                parent_score->base_scores,
            .base_exact_support =
                parent_score->base.selected_support,
            .neutral_priority_options =
                std::move(
                    retained_root
                        .neutral_priority_options),
            .hidden_neutral_priority_options =
                std::move(
                    retained_root
                        .hidden_neutral_priority_options),
            .residuals =
                parent_score->residuals,
            .combined_scores =
                parent_score->combined,
            .parent_class = classification,
            .accounting =
                parent_score->accounting,
            .hidden_replay_bit_identical =
                replay_exact(
                    root, retained_root.hidden),
            .hidden_feature_bits_identical =
                retained_root
                    .hidden_feature_bits_identical,
        };
        report.scored_roots.push_back(
            std::move(scored));
        if (classification.classification ==
            ParentClass::Class2) {
            report.class2_sigma_mass +=
                classification.sigma;
            if (!std::isfinite(
                    report.class2_sigma_mass)) {
                record_failure(
                    report,
                    root.manifest.stable_id +
                        ": Class-2 sigma mass became nonfinite");
            }
        }
        if (classification.high_confidence_unsafe()) {
            high_confidence_games.insert(
                fq4_d1_field_gate::physical_game_id(
                    root.manifest.locator));
            high_confidence_decks.insert(deck);
        }
        const std::size_t game_index =
            root.manifest.locator.source_block *
                learned_iteration::
                    kBalancedScheduleGames +
            root.manifest.locator.schedule_index;
        if (game_index >= report.games.size() ||
            root.manifest.locator.owner_seat >=
                kPlayerCount) {
            record_failure(
                report,
                root.manifest.stable_id +
                    ": census game locator is invalid");
        } else {
            RootCounts& counts =
                report.games[game_index]
                    .owners[
                        root.manifest.locator.owner_seat];
            ++counts.parent_classes[
                class_index(
                    classification.classification)];
        }
    }

    report.distinct_high_confidence_games =
        high_confidence_games.size();
    report.distinct_high_confidence_decks =
        high_confidence_decks.size();
    for (std::size_t deck = 0;
         deck < deck_control_seen.size(); ++deck) {
        if (!deck_control_seen[deck]) {
            record_failure(
                report,
                "missing first-retained scorer audit for deck " +
                    std::string(deck_name(
                        static_cast<DeckId>(deck))));
        }
    }
    report.first_deck_controls_bit_identical =
        report.first_deck_controls_bit_identical &&
        std::all_of(
            deck_control_seen.begin(),
            deck_control_seen.end(),
            [](bool seen) { return seen; });
    report.audit_scores_sha256 =
        audit_scores.finish();
    report.scored_corpus_sha256 =
        scored_corpus_digest(report.scored_roots);

    for (std::size_t game = 0;
         game < report.games.size(); ++game) {
        for (std::size_t owner = 0;
             owner < kPlayerCount; ++owner) {
            const std::size_t deck = deck_index(
                report.games[game]
                    .source.seat_decks[owner]);
            const RootCounts& counts =
                report.games[game].owners[owner];
            report.decks[deck] += counts;
            DeckGameCoverage& coverage =
                report.deck_game_coverage[deck];
            ++coverage.owner_games;
            coverage.games_with_raw +=
                counts.raw > 0 ? 1U : 0U;
            coverage.games_with_retained +=
                counts.retained > 0 ? 1U : 0U;
            coverage
                .games_with_dominance_positive +=
                counts.dominance_positive > 0
                    ? 1U
                    : 0U;
        }
    }
    for (const RootCounts& deck : report.decks) {
        report.pooled += deck;
    }
    const std::size_t dominance_transitions =
        report.accounting.dominance_transitions;
    report.accounting =
        report.primary_accounting;
    report.accounting +=
        report.hidden_control_accounting;
    report.accounting +=
        report.reverse_control_accounting;
    report.accounting.dominance_transitions =
        dominance_transitions;
    report.count_cross_sums_exact =
        report.games.size() ==
            kExpectedPhysicalGames &&
        std::all_of(
            report.games.begin(), report.games.end(),
            [](const GameCensus& game) {
                return std::all_of(
                    game.owners.begin(),
                    game.owners.end(),
                    [](const RootCounts& counts) {
                        return counts
                            .terminal_cross_sums_valid();
                    });
            }) &&
        report.pooled.terminal_cross_sums_valid() &&
        std::all_of(
            report.decks.begin(), report.decks.end(),
            [](const RootCounts& counts) {
                return counts
                    .terminal_cross_sums_valid();
            }) &&
        std::all_of(
            report.deck_game_coverage.begin(),
            report.deck_game_coverage.end(),
            [](const DeckGameCoverage& coverage) {
                return coverage.owner_games ==
                           kExpectedPerspectivesPerDeck &&
                       coverage.games_with_raw <=
                           coverage.owner_games &&
                       coverage.games_with_retained <=
                           coverage.owner_games &&
                       coverage
                               .games_with_dominance_positive <=
                           coverage.games_with_retained;
            });
    std::size_t expected_transitions = 0;
    for (const ReplayRootManifest& root :
         output.retained_manifest) {
        const std::size_t actions =
            root.canonical_descriptors.size();
        if (actions >
            std::numeric_limits<std::size_t>::max() /
                kDominanceWorlds ||
            expected_transitions >
                std::numeric_limits<std::size_t>::max() -
                    actions * kDominanceWorlds) {
            record_failure(
                report,
                "dominance transition expectation overflowed");
            expected_transitions = 0;
            break;
        }
        expected_transitions +=
            actions * kDominanceWorlds;
    }
    report.recipe_and_accounting_exact =
        report.recipe_and_accounting_exact &&
        report.primary_accounting.valid() &&
        report.hidden_control_accounting.valid() &&
        report.reverse_control_accounting.valid() &&
        report.accounting.valid() &&
        report.accounting.dominance_transitions ==
            expected_transitions &&
        report.scored_roots.size() ==
            report.pooled.dominance_positive;
    if (!report.count_cross_sums_exact) {
        record_failure(
            report,
            "root accounting cross-sums are inconsistent");
    }
    if (!report.recipe_and_accounting_exact) {
        record_failure(
            report,
            "production recipe/accounting is inconsistent");
    }
    if (!report.all_replays_exact ||
        !report.all_hidden_feature_bits_identical ||
        !report.first_deck_controls_bit_identical) {
        record_failure(
            report,
            "one or more replay/invariance controls failed");
    }
    return output;
}

} // namespace

std::vector<SourceGame> source_schedule() {
    std::vector<SourceGame> result;
    result.reserve(kExpectedPhysicalGames);
    for (std::size_t block = 0;
         block < kSourceSeedBases.size(); ++block) {
        const std::uint64_t seed_base =
            kSourceSeedBases[block];
        const auto schedule =
            learned_iteration::balanced_schedule(
                seed_base,
                kSourceGenerationNamespace, 0);
        for (const auto& game : schedule) {
            result.push_back({
                .source_block = block,
                .source_seed_base = seed_base,
                .schedule_index =
                    game.schedule_index,
                .pairing_index =
                    game.pairing_index,
                .seat_decks = game.seat_decks,
                .starting_player =
                    game.starting_player,
                .game_seed = game.seed,
            });
        }
    }
    if (result.size() != kExpectedPhysicalGames) {
        throw std::logic_error(
            "FQ4-D1-P0 source schedule has wrong size");
    }
    return result;
}

std::string serialize_source_schedule(
    const std::vector<SourceGame>& schedule) {
    std::string output;
    output.reserve(kExpectedScheduleBytes);
    output.append(kScheduleSchema);
    output.push_back('\n');
    for (const SourceGame& game : schedule) {
        // Frozen ten-field contract: seed base, seed-base index, generation,
        // balanced block, schedule index, pairing index, game seed, starting
        // player, seat-zero deck, and seat-one deck.
        append_tab_field(output, game.source_seed_base);
        append_tab_field(output, game.source_block);
        append_tab_field(output, kSourceGenerationNamespace);
        append_tab_field(output, std::uint64_t{0});
        append_tab_field(output, game.schedule_index);
        append_tab_field(output, game.pairing_index);
        append_tab_field(output, game.game_seed);
        append_tab_field(output, game.starting_player);
        append_tab_field(
            output,
            static_cast<std::uint64_t>(
                game.seat_decks[0]));
        append_tab_field(
            output,
            static_cast<std::uint64_t>(
                game.seat_decks[1]));
        output.push_back('\n');
    }
    return output;
}

std::string source_schedule_sha256() {
    return integrity::sha256_string(
        serialize_source_schedule(source_schedule()));
}

ScheduleBalance audit_schedule_balance(
    const std::vector<SourceGame>& schedule) {
    ScheduleBalance result{
        .physical_games = schedule.size(),
        .owner_perspectives =
            schedule.size() * kPlayerCount,
    };
    bool rows_valid = true;
    std::set<std::tuple<std::size_t, std::size_t>>
        coordinates;
    for (const SourceGame& game : schedule) {
        rows_valid =
            rows_valid &&
            game.source_block <
                kSourceSeedBases.size() &&
            game.source_seed_base ==
                kSourceSeedBases[game.source_block] &&
            game.schedule_index <
                learned_iteration::
                    kBalancedScheduleGames &&
            game.pairing_index <
                learned_iteration::kBalancedPairings &&
            game.starting_player < kPlayerCount &&
            game.seat_decks[0] !=
                game.seat_decks[1] &&
            coordinates
                .insert({
                    game.source_block,
                    game.schedule_index,
                })
                .second;
        for (std::size_t seat = 0;
             seat < kPlayerCount; ++seat) {
            const std::size_t deck =
                deck_index(game.seat_decks[seat]);
            ++result.perspectives_by_deck[deck];
            if (seat == 0) {
                ++result.seat_zero_by_deck[deck];
            }
            if (seat == game.starting_player) {
                ++result.on_play_by_deck[deck];
            }
        }
    }
    result.exact =
        rows_valid &&
        result.physical_games ==
            kExpectedPhysicalGames &&
        result.owner_perspectives ==
            kExpectedOwnerPerspectives &&
        std::all_of(
            result.perspectives_by_deck.begin(),
            result.perspectives_by_deck.end(),
            [](std::size_t count) {
                return count ==
                    kExpectedPerspectivesPerDeck;
            }) &&
        std::all_of(
            result.seat_zero_by_deck.begin(),
            result.seat_zero_by_deck.end(),
            [](std::size_t count) {
                return count ==
                    kExpectedSeatZeroPerspectivesPerDeck;
            }) &&
        std::all_of(
            result.on_play_by_deck.begin(),
            result.on_play_by_deck.end(),
            [](std::size_t count) {
                return count ==
                    kExpectedOnPlayPerspectivesPerDeck;
            });
    return result;
}

std::string stable_root_id(
    const RootLocator& locator,
    std::string_view information_action_fingerprint) {
    return collection::stable_root_id(
        locator, information_action_fingerprint,
        kStableRootSchema);
}

bool validate_replay_manifest(
    const std::vector<ReplayRootManifest>& roots) {
    return collection::validate_replay_manifest(
        roots, kStableRootSchema,
        kMaximumLegalActions);
}

std::string serialize_replay_manifest(
    const std::vector<ReplayRootManifest>& roots) {
    return collection::serialize_replay_manifest(
        roots, kSchema, kStableRootSchema,
        kMaximumLegalActions);
}

std::string replay_manifest_sha256(
    const std::vector<ReplayRootManifest>& roots) {
    return collection::replay_manifest_sha256(
        roots, kSchema, kStableRootSchema,
        kMaximumLegalActions);
}

bool CensusReport::infrastructure_valid() const {
    ProductionAccounting repeat_sum =
        repeat_primary_accounting;
    repeat_sum += repeat_hidden_control_accounting;
    repeat_sum += repeat_reverse_control_accounting;
    repeat_sum.dominance_transitions =
        repeat_accounting.dominance_transitions;
    bool descriptive_counts_valid = true;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        descriptive_counts_valid =
            descriptive_counts_valid &&
            raw_base_mixed_tie_support_by_deck[deck] <=
                raw_base_dominated_support_by_deck[deck] &&
            raw_base_dominated_support_by_deck[deck] <=
                decks[deck].dominance_positive;
    }
    return infrastructure_failures.empty() &&
           schedule_preflight_before_games &&
           parent_contract_exact &&
           source_config_exact &&
           retention_score_blind &&
           all_replays_exact &&
           all_hidden_feature_bits_identical &&
           first_deck_controls_bit_identical &&
           recipe_and_accounting_exact &&
           count_cross_sums_exact &&
           repeated_construction_bit_identical &&
           watchdog_ok &&
           schedule_balance.exact &&
           games.size() == kExpectedPhysicalGames &&
           parent_fingerprint ==
               kRequiredParentFingerprint &&
           parent_components.critic.size() == 64 &&
           parent_components.priority.size() == 64 &&
           parent_components.attack.size() == 64 &&
           parent_components.block.size() == 64 &&
           parent_components.damage_order.size() == 64 &&
           schedule_sha256 ==
               kExpectedScheduleSha256 &&
           trajectory_sha256.size() == 64 &&
           retained_corpus_sha256.size() == 64 &&
           dominance_corpus_sha256.size() == 64 &&
           scored_corpus_sha256.size() == 64 &&
           audit_scores_sha256.size() == 64 &&
           std::isfinite(class2_sigma_mass) &&
           repeat_primary_accounting.valid() &&
           repeat_hidden_control_accounting.valid() &&
           repeat_reverse_control_accounting.valid() &&
           repeat_accounting.valid() &&
           repeat_sum == repeat_accounting &&
           repeat_accounting == accounting &&
           descriptive_counts_valid &&
           hidden_replay_roots ==
               distinct_hidden_clones +
                   vacuous_hidden_clones;
}

bool CensusReport::support_floor_met() const {
    const std::size_t high_confidence =
        pooled.parent_classes[1] +
        pooled.parent_classes[2];
    return high_confidence >=
               kMinimumHighConfidenceRoots &&
           distinct_high_confidence_games >=
               kMinimumHighConfidenceGames &&
           distinct_high_confidence_decks >=
               kMinimumHighConfidenceDecks;
}

bool CensusReport::passed() const {
    return infrastructure_valid() &&
           support_floor_met();
}

ExitClassification classify_exit(
    const CensusReport& report) {
    if (!report.infrastructure_valid()) {
        return ExitClassification::
            InfrastructureFailure;
    }
    return report.support_floor_met()
               ? ExitClassification::Pass
               : ExitClassification::Underpowered;
}

CensusReport run_parent_census(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    const auto start =
        std::chrono::steady_clock::now();
    if (!frozen_c16) {
        CensusReport report;
        report.infrastructure_failures.push_back(
            "FQ4-D1-P0 requires a frozen C16 parent");
        return report;
    }
    const auto schedule = source_schedule();
    const std::string schedule_hash =
        integrity::sha256_string(
            serialize_source_schedule(schedule));
    Construction first =
        construct_once(
            frozen_c16, schedule, schedule_hash);
    Construction second =
        construct_once(
            frozen_c16, schedule, schedule_hash);
    CensusReport result = std::move(first.report);
    result.repeat_primary_accounting =
        second.report.primary_accounting;
    result.repeat_hidden_control_accounting =
        second.report.hidden_control_accounting;
    result.repeat_reverse_control_accounting =
        second.report.reverse_control_accounting;
    result.repeat_accounting =
        second.report.accounting;
    result.repeated_construction_bit_identical =
        evidence_equal(result, second.report) &&
        first.retained_manifest ==
            second.retained_manifest;
    if (!result.repeated_construction_bit_identical) {
        result.infrastructure_failures.push_back(
            "repeated complete P0 construction drifted");
    }
    result.runtime_seconds =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start)
            .count();
    // The no-argument executable supplies the hard process supervisor. This
    // field records that its worker completed inside the declared wall time.
    result.watchdog_ok =
        result.runtime_seconds <=
            static_cast<double>(kWatchdogSeconds);
    if (!result.watchdog_ok) {
        result.infrastructure_failures.push_back(
            "P0 worker exceeded the 180-second watchdog");
    }
    if (result.infrastructure_valid() &&
        !result.support_floor_met()) {
        const std::size_t high_confidence =
            result.pooled.parent_classes[1] +
            result.pooled.parent_classes[2];
        result.underpowered_reasons.push_back(
            "high-confidence roots=" +
            std::to_string(high_confidence) +
            ", games=" +
            std::to_string(
                result.distinct_high_confidence_games) +
            ", decks=" +
            std::to_string(
                result.distinct_high_confidence_decks));
    }
    return result;
}

namespace testing {

bool neutral_tensor_bits_identical(
    const ScoredRoot& first,
    const ScoredRoot& second) {
    return
        tensor_bits_identical(
            first.neutral_priority_options,
            second.neutral_priority_options) &&
        tensor_bits_identical(
            first.hidden_neutral_priority_options,
            second.hidden_neutral_priority_options);
}

RootDisposition diagnose_root_disposition(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal) {
    const collection::RootBuildResult attempt =
        collection::build_canonical_root(
            point, source, owner_seat,
            trace_ordinal, collection_spec());
    switch (attempt.disposition) {
    case collection::RootDisposition::Malformed:
        return RootDisposition::Malformed;
    case collection::RootDisposition::Trivial:
        return RootDisposition::Trivial;
    case collection::RootDisposition::OverCap:
        return RootDisposition::OverCap;
    case collection::RootDisposition::RetentionCandidate:
        return RootDisposition::RetentionCandidate;
    }
    throw std::logic_error(
        "unknown FQ4-D1-P0 root-build disposition");
}

CanonicalHiddenDiagnostic diagnose_canonical_hidden_root(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal) {
    const collection::CanonicalHiddenDiagnostic common =
        collection::diagnose_canonical_hidden_root(
            point, source, owner_seat,
            trace_ordinal, collection_spec());
    return {
        .materialized = common.materialized,
        .canonical_state = common.canonical_state,
        .hidden_clone_state =
            common.hidden_clone_state,
        .canonical_descriptors =
            common.canonical_descriptors,
        .information_action_fingerprint =
            common.information_action_fingerprint,
        .owner_hand_preserved =
            common.owner_hand_preserved,
        .reporting_statistics_zero =
            common.reporting_statistics_zero,
        .second_replay_exact =
            common.second_replay_exact,
        .hidden_feature_bits_identical =
            common.hidden_feature_bits_identical,
        .hidden_clone_eligible =
            common.hidden_clone_eligible,
        .hidden_clone_distinct =
            common.hidden_clone_distinct,
    };
}

CensusReport complete_synthetic_report() {
    CensusReport report;
    report.parent_fingerprint =
        std::string(kRequiredParentFingerprint);
    report.parent_components = {
        .critic = std::string(64, 'a'),
        .priority = std::string(64, 'b'),
        .attack = std::string(64, 'c'),
        .block = std::string(64, 'd'),
        .damage_order = std::string(64, 'e'),
    };
    report.schedule_sha256 =
        std::string(kExpectedScheduleSha256);
    report.trajectory_sha256 =
        std::string(64, '1');
    report.retained_corpus_sha256 =
        std::string(64, '2');
    report.dominance_corpus_sha256 =
        std::string(64, '3');
    report.scored_corpus_sha256 =
        std::string(64, '4');
    report.audit_scores_sha256 =
        std::string(64, '5');
    report.schedule_balance =
        audit_schedule_balance(source_schedule());
    report.games.resize(kExpectedPhysicalGames);
    report.pooled = {
        .raw = kMinimumHighConfidenceRoots,
        .nontrivial =
            kMinimumHighConfidenceRoots,
        .eligible =
            kMinimumHighConfidenceRoots,
        .unique =
            kMinimumHighConfidenceRoots,
        .retained =
            kMinimumHighConfidenceRoots,
        .dominance_positive =
            kMinimumHighConfidenceRoots,
        .parent_classes = {
            0,
            kMinimumHighConfidenceRoots,
            0,
            0,
        },
    };
    report.distinct_high_confidence_games =
        kMinimumHighConfidenceGames;
    report.distinct_high_confidence_decks =
        kMinimumHighConfidenceDecks;
    report.hidden_replay_roots = 1;
    report.distinct_hidden_clones = 1;
    report.schedule_preflight_before_games = true;
    report.parent_contract_exact = true;
    report.source_config_exact = true;
    report.retention_score_blind = true;
    report.all_replays_exact = true;
    report.all_hidden_feature_bits_identical = true;
    report.first_deck_controls_bit_identical = true;
    report.recipe_and_accounting_exact = true;
    report.count_cross_sums_exact = true;
    report.repeated_construction_bit_identical = true;
    report.watchdog_ok = true;
    return report;
}

} // namespace testing

} // namespace old_school::fq4_d1_field_gate
