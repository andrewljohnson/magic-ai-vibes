#include "old_school/fq4_priority_collection.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_dominance_transition.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace old_school::fq4_priority_collection {
namespace {

namespace integrity = artifact_integrity;
namespace dominance = fq0_dominance;
namespace transition = fq0_dominance_transition;
namespace information = fq0_information_set;
namespace probe_data = probes;

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "FQ4 collection deck is outside the five-deck field");
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
    throw std::invalid_argument("unknown FQ4 collection deck");
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
        writer.integer<std::uint64_t>(*target.creature);
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
            output.integer(creature.temporary_power_bonus);
            output.integer(creature.temporary_toughness_bonus);
            output.boolean(creature.exile_on_death_this_turn);
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
        writer.integer<std::uint64_t>(*action.spell_target);
    }
    writer.boolean(action.source_permanent.has_value());
    if (action.source_permanent.has_value()) {
        writer.integer<std::uint64_t>(*action.source_permanent);
    }
    writer.integer(action.x_value);
}

std::string owner_information_action_bytes(
    const GameState& state,
    const LearnedDecisionContext& context,
    const std::vector<PriorityAction>& legal_actions,
    std::string_view schema) {
    if (schema.empty() || !context.valid ||
        context.decision_player >= kPlayerCount) {
        throw std::invalid_argument(
            "invalid owner-safe information/action context");
    }
    const PlayerObservation observation =
        observe_game_state(state, context.decision_player);
    if (observation.revealed_opponent_hand.has_value()) {
        throw std::logic_error(
            "owner-safe observation exposed opponent hand");
    }
    ByteWriter writer;
    writer.text(schema);
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
                static_cast<std::uint64_t>(object.kind));
            output.integer<std::uint64_t>(object.id);
            append_card(output, object.card);
            output.integer<std::uint64_t>(object.controller);
            output.boolean(object.target.has_value());
            if (object.target.has_value()) {
                append_target(output, *object.target);
            }
            output.boolean(object.spell_target.has_value());
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
    writer.integer<std::uint64_t>(observation.active_player);
    writer.integer<std::uint64_t>(observation.starting_player);
    writer.integer<std::uint64_t>(observation.turn_number);
    writer.boolean(context.valid);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(context.phase));
    writer.integer<std::uint64_t>(context.decision_player);
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
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

bool priority_phase(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::BeginCombat ||
           phase == TurnPhase::EndCombat ||
           phase == TurnPhase::SecondMain;
}

std::uint64_t hidden_seed(
    const RootLocator& locator,
    const CollectionSpec& spec) {
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
        spec.hidden_seed_namespace,
        {
            .domain =
                information::SeedDomain::
                    RootDeterminization,
            .scope = std::string(
                spec.hidden_seed_scope),
            .group = group,
            .bank = information::SeedBank::Root,
            .block = 0,
            .world = 0,
        });
}

std::uint64_t dominance_world_seed(
    const ReplayRootManifest& manifest,
    const CollectionSpec& spec, std::size_t world) {
    return information::derive_indexed_seed(
        spec.dominance_seed_namespace,
        {
            .domain =
                information::SeedDomain::
                    RootDeterminization,
            .scope = std::string(
                spec.dominance_seed_scope),
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

std::vector<PriorityAction> priority_actions(
    const probe_data::DecisionProbe& probe) {
    std::vector<PriorityAction> result;
    result.reserve(probe.candidates.size());
    for (const auto& candidate : probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr) {
            throw std::logic_error(
                "FQ4 collection contains a non-Priority action");
        }
        result.push_back(*action);
    }
    return result;
}

} // namespace

bool CollectionSpec::valid() const {
    return !owner_information_schema.empty() &&
           !stable_root_schema.empty() &&
           hidden_seed_namespace != 0 &&
           !hidden_seed_scope.empty() &&
           dominance_seed_namespace != 0 &&
           !dominance_seed_scope.empty() &&
           maximum_legal_actions >= 2 &&
           maximum_legal_actions <=
               std::numeric_limits<std::uint16_t>::max() &&
           maximum_roots_per_owner_game > 0 &&
           dominance_worlds >= 2;
}

std::string physical_game_id(
    const RootLocator& locator) {
    return "source_seed_base=" +
           std::to_string(locator.source_seed_base) +
           "\nschedule_index=" +
           std::to_string(locator.schedule_index) +
           "\n";
}

std::string stable_root_id(
    const RootLocator& locator,
    std::string_view information_action_fingerprint,
    std::string_view stable_root_schema) {
    if (locator.owner_seat >= kPlayerCount ||
        information_action_fingerprint.empty() ||
        stable_root_schema.empty()) {
        throw std::invalid_argument(
            "invalid FQ4 stable-root coordinate");
    }
    std::string key(stable_root_schema);
    key.push_back('\n');
    key += "source_seed_base_index=" +
           std::to_string(locator.source_block) + "\n";
    key += "source_seed_base=" +
           std::to_string(locator.source_seed_base) + "\n";
    key += "schedule_index=" +
           std::to_string(locator.schedule_index) + "\n";
    key += "game_seed=" +
           std::to_string(locator.game_seed) + "\n";
    key += "owner=" +
           std::to_string(locator.owner_seat) + "\n";
    key += "trace=" +
           std::to_string(locator.trace_ordinal) + "\n";
    key += "information_action_sha256=";
    key.append(information_action_fingerprint);
    key.push_back('\n');
    return integrity::sha256_string(key);
}

RetentionResult retain_owner_game_roots(
    const std::vector<RetentionCandidate>& candidates,
    std::size_t cap) {
    RetentionResult result;
    if (cap == 0) {
        return result;
    }
    std::map<std::string, std::string> fingerprints;
    std::set<std::string> stable_ids;
    std::size_t previous_ordinal = 0;
    bool first = true;
    for (std::size_t index = 0;
         index < candidates.size(); ++index) {
        const RetentionCandidate& candidate =
            candidates[index];
        if (candidate.information_action_fingerprint.empty() ||
            candidate.information_action_bytes.empty() ||
            candidate.stable_id.empty() ||
            (!first &&
             candidate.trace_ordinal <= previous_ordinal) ||
            !stable_ids.insert(candidate.stable_id).second) {
            return result;
        }
        first = false;
        previous_ordinal = candidate.trace_ordinal;
        const auto [found, inserted] =
            fingerprints.emplace(
                candidate.information_action_fingerprint,
                candidate.information_action_bytes);
        if (!inserted &&
            found->second !=
                candidate.information_action_bytes) {
            ++result.hash_collision_count;
            return result;
        }
        if (!inserted) {
            ++result.duplicate_count;
            continue;
        }
        result.unique_input_indices.push_back(index);
    }
    const auto positions =
        learned_iteration::evenly_spaced_retained_indices(
            result.unique_input_indices.size(), cap);
    for (const std::size_t position : positions) {
        result.retained_input_indices.push_back(
            result.unique_input_indices[position]);
    }
    result.valid =
        std::is_sorted(
            result.retained_input_indices.begin(),
            result.retained_input_indices.end()) &&
        result.unique_input_indices.size() +
                result.duplicate_count ==
            candidates.size() &&
        result.hash_collision_count == 0;
    return result;
}

bool validate_replay_manifest(
    const std::vector<ReplayRootManifest>& roots,
    std::string_view stable_root_schema,
    std::size_t maximum_legal_actions) {
    if (stable_root_schema.empty() ||
        maximum_legal_actions < 2) {
        return false;
    }
    std::set<std::string> stable_ids;
    for (const ReplayRootManifest& root : roots) {
        const std::size_t owner =
            static_cast<std::size_t>(root.owner_deck);
        const std::size_t opponent =
            static_cast<std::size_t>(root.opponent_deck);
        if (owner >= kDeckCount || opponent >= kDeckCount ||
            root.owner_deck == root.opponent_deck ||
            root.locator.owner_seat >= kPlayerCount ||
            root.stable_id.empty() ||
            root.information_action_fingerprint.empty() ||
            root.canonical_descriptors.size() < 2 ||
            root.canonical_descriptors.size() >
                maximum_legal_actions ||
            root.pass_index >=
                root.canonical_descriptors.size() ||
            !std::is_sorted(
                root.canonical_descriptors.begin(),
                root.canonical_descriptors.end()) ||
            std::adjacent_find(
                root.canonical_descriptors.begin(),
                root.canonical_descriptors.end()) !=
                root.canonical_descriptors.end() ||
            root.stable_id != stable_root_id(
                root.locator,
                root.information_action_fingerprint,
                stable_root_schema) ||
            !stable_ids.insert(root.stable_id).second) {
            return false;
        }
    }
    return true;
}

std::string serialize_replay_manifest(
    const std::vector<ReplayRootManifest>& roots,
    std::string_view manifest_schema,
    std::string_view stable_root_schema,
    std::size_t maximum_legal_actions) {
    if (manifest_schema.empty() ||
        !validate_replay_manifest(
            roots, stable_root_schema,
            maximum_legal_actions)) {
        throw std::invalid_argument(
            "invalid FQ4 replay manifest");
    }
    std::string output(manifest_schema);
    output += "\nretained-replay-manifest\n";
    for (const ReplayRootManifest& root : roots) {
        output +=
            std::to_string(root.locator.source_block) + "\t" +
            std::to_string(root.locator.source_seed_base) + "\t" +
            std::to_string(root.locator.schedule_index) + "\t" +
            std::to_string(root.locator.game_seed) + "\t" +
            std::to_string(root.locator.owner_seat) + "\t" +
            std::to_string(root.locator.trace_ordinal) + "\t" +
            std::to_string(static_cast<std::size_t>(
                root.owner_deck)) + "\t" +
            std::to_string(static_cast<std::size_t>(
                root.opponent_deck)) + "\t" +
            root.stable_id + "\t" +
            root.information_action_fingerprint + "\t" +
            std::to_string(root.pass_index) + "\t" +
            std::to_string(
                root.canonical_descriptors.size());
        for (const std::string& descriptor :
             root.canonical_descriptors) {
            output += "\t" +
                      std::to_string(descriptor.size()) +
                      ":" + descriptor;
        }
        output.push_back('\n');
    }
    return output;
}

std::string replay_manifest_sha256(
    const std::vector<ReplayRootManifest>& roots,
    std::string_view manifest_schema,
    std::string_view stable_root_schema,
    std::size_t maximum_legal_actions) {
    return integrity::sha256_string(
        serialize_replay_manifest(
            roots, manifest_schema, stable_root_schema,
            maximum_legal_actions));
}

RootBuildResult build_canonical_root(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal, const CollectionSpec& spec) {
    RootBuildResult result;
    if (!spec.valid() ||
        !point.context.valid ||
        point.context.decision_player != owner_seat ||
        owner_seat >= kPlayerCount ||
        point.context.consecutive_passes < 0 ||
        point.context.consecutive_passes > 1 ||
        !priority_phase(point.context.phase) ||
        point.context.sorcery_actions !=
            sorcery_actions_for(point.context.phase)) {
        result.error = "invalid Priority trace context or collection spec";
        return result;
    }
    if (source.starting_player >= kPlayerCount ||
        source.seat_decks[0] == source.seat_decks[1]) {
        result.error = "invalid source-game coordinate";
        return result;
    }
    try {
        static_cast<void>(deck_index(source.seat_decks[0]));
        static_cast<void>(deck_index(source.seat_decks[1]));
    } catch (const std::exception& error) {
        result.error = error.what();
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
        safe = sample_determinization(
            point.state, decks, owner_seat,
            hidden_seed(locator, spec));
        safe.stats = {};
        if (safe.players[owner_seat].hand !=
            point.state.players[owner_seat].hand) {
            throw std::logic_error(
                "owner hand changed during canonical determinization");
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
            result.selected_descriptor =
                probe_data::stable_priority_action_descriptor(
                    legal.front());
            if (result.selected_descriptor.empty()) {
                throw std::logic_error(
                    "trivial Pass descriptor is empty");
            }
            const std::string information_bytes =
                owner_information_action_bytes(
                    safe, point.context, legal,
                    spec.owner_information_schema);
            result.information_action_fingerprint =
                integrity::sha256_string(information_bytes);
            result.disposition = RootDisposition::Trivial;
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
            information::descriptor_canonical_action_rows(key);
    } catch (const std::exception& error) {
        result.error =
            std::string("canonical action set failed: ") +
            error.what();
        return result;
    }
    if (rows.size() != legal.size()) {
        result.error = "canonical action set is incomplete";
        return result;
    }
    std::vector<PriorityAction> canonical_actions;
    canonical_actions.reserve(rows.size());
    std::set<std::string> descriptor_set;
    for (const auto& row : rows) {
        canonical_actions.push_back(row.action);
        if (!descriptor_set.insert(row.descriptor).second) {
            result.error =
                "canonical action descriptors are not unique";
            return result;
        }
    }
    const std::string information_bytes =
        owner_information_action_bytes(
            safe, point.context, canonical_actions,
            spec.owner_information_schema);
    result.information_action_fingerprint =
        integrity::sha256_string(information_bytes);

    std::size_t pass_count = 0;
    std::size_t pass_index = 0;
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
            result.selected_descriptor = row.descriptor;
        }
    }
    if (pass_count != 1 ||
        result.selected_descriptor.empty()) {
        result.error =
            "canonical root lacks one Pass or selected descriptor";
        return result;
    }
    if (legal.size() > spec.maximum_legal_actions) {
        result.disposition = RootDisposition::OverCap;
        return result;
    }

    const std::string root_id =
        stable_root_id(
            locator,
            result.information_action_fingerprint,
            spec.stable_root_schema);
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
                result.information_action_fingerprint,
            .canonical_descriptors =
                std::move(descriptors),
            .pass_index = pass_index,
        },
        .information_action_bytes =
            information_bytes,
    };
    result.disposition =
        RootDisposition::RetentionCandidate;
    return result;
}

HiddenClone make_hidden_clone(const CanonicalRoot& root) {
    HiddenClone result{.probe = root.probe};
    const std::size_t owner = root.probe.root_player;
    if (owner >= kPlayerCount) {
        throw std::invalid_argument(
            "hidden clone owner must be a player");
    }
    PlayerState& opponent =
        result.probe.state.players[1 - owner];
    PlayerState& owner_state =
        result.probe.state.players[owner];

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
            root.probe.state, result.probe.state,
            owner)) {
        throw std::logic_error(
            "vacuous hidden clone changed hidden state");
    }
    return result;
}

bool replay_exact(
    const CanonicalRoot& root, const HiddenClone& clone,
    const CollectionSpec& spec) {
    if (!spec.valid() ||
        observe_game_state(
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
        .decision_player = clone.probe.root_player,
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
            canonical_actions,
            spec.owner_information_schema);
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

bool priority_feature_bits_identical(
    const probe_data::DecisionProbe& visible,
    const probe_data::DecisionProbe& hidden) {
    const auto first = priority_option_features(visible);
    const auto second = priority_option_features(hidden);
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

CanonicalHiddenDiagnostic diagnose_canonical_hidden_root(
    const LearnedDecisionTracePoint& point,
    const SourceGame& source, std::size_t owner_seat,
    std::size_t trace_ordinal, const CollectionSpec& spec) {
    CanonicalHiddenDiagnostic diagnostic;
    RootBuildResult attempt =
        build_canonical_root(
            point, source, owner_seat,
            trace_ordinal, spec);
    if (!attempt.root.has_value()) {
        return diagnostic;
    }
    CanonicalRoot& root = *attempt.root;
    HiddenClone clone = make_hidden_clone(root);
    diagnostic.materialized = true;
    diagnostic.canonical_state = root.probe.state;
    diagnostic.hidden_clone_state = clone.probe.state;
    diagnostic.canonical_descriptors =
        root.manifest.canonical_descriptors;
    diagnostic.information_action_fingerprint =
        root.manifest.information_action_fingerprint;
    diagnostic.owner_hand_preserved =
        root.probe.state.players[owner_seat].hand ==
        point.state.players[owner_seat].hand;
    diagnostic.reporting_statistics_zero =
        root.probe.state.stats ==
        std::array<PlayerGameStats, 2>{};
    diagnostic.second_replay_exact =
        replay_exact(root, clone, spec);
    diagnostic.hidden_feature_bits_identical =
        priority_feature_bits_identical(
            root.probe, clone.probe);
    diagnostic.hidden_clone_eligible = clone.eligible;
    diagnostic.hidden_clone_distinct = clone.distinct;
    return diagnostic;
}

bool RobustDominance::any_dominated() const {
    return shape_valid &&
           std::any_of(
               robustly_pass_dominated.begin(),
               robustly_pass_dominated.end(),
               [](bool value) { return value; });
}

RobustDominance summarize_robust_dominance(
    std::size_t pass_index, std::size_t action_count,
    const std::vector<DominanceWorldRow>& worlds,
    std::size_t expected_worlds,
    std::size_t maximum_legal_actions) {
    RobustDominance result{
        .pass_index = pass_index,
        .complete_world_counts =
            std::vector<std::size_t>(action_count, 0),
        .strict_world_counts =
            std::vector<std::size_t>(action_count, 0),
        .robustly_pass_dominated =
            std::vector<bool>(action_count, false),
    };
    if (action_count < 2 ||
        action_count > maximum_legal_actions ||
        pass_index >= action_count ||
        expected_worlds < 2 ||
        worlds.size() != expected_worlds) {
        return result;
    }
    for (const DominanceWorldRow& world : worlds) {
        if (world.candidate_complete.size() !=
                action_count ||
            world.orientations.size() != action_count) {
            return result;
        }
        ++result.transition_count;
        for (std::size_t action = 0;
             action < action_count; ++action) {
            if (action == pass_index) {
                continue;
            }
            ++result.transition_count;
            if (!world.pass_complete ||
                !world.candidate_complete[action]) {
                continue;
            }
            ++result.complete_world_counts[action];
            ++result.complete_comparisons;
            if (world.orientations[action] ==
                dominance::Orientation::
                    FirstDominatesSecond) {
                ++result.strict_world_counts[action];
            }
        }
    }
    for (std::size_t action = 0;
         action < action_count; ++action) {
        if (action != pass_index &&
            result.strict_world_counts[action] ==
                expected_worlds) {
            result.robustly_pass_dominated[action] =
                true;
        }
    }
    result.shape_valid = true;
    return result;
}

RobustDominance evaluate_robust_dominance(
    const CanonicalRoot& root, const CollectionSpec& spec,
    std::vector<std::string>& infrastructure_failures) {
    if (!spec.valid()) {
        infrastructure_failures.push_back(
            root.manifest.stable_id +
            ": invalid collection specification");
        return {};
    }
    const std::size_t action_count =
        root.probe.candidates.size();
    std::vector<DominanceWorldRow> rows;
    rows.reserve(spec.dominance_worlds);
    for (std::size_t world = 0;
         world < spec.dominance_worlds; ++world) {
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
                    root.manifest, spec, world));
        } catch (const std::exception& error) {
            infrastructure_failures.push_back(
                root.manifest.stable_id +
                ": dominance world sampling exception: " +
                error.what());
            rows.push_back(std::move(row));
            continue;
        }
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
    return summarize_robust_dominance(
        root.manifest.pass_index,
        action_count, rows,
        spec.dominance_worlds,
        spec.maximum_legal_actions);
}

std::string_view parent_class_name(
    ParentClass classification) {
    switch (classification) {
    case ParentClass::Safe:
        return "Safe";
    case ParentClass::Class1:
        return "Class 1";
    case ParentClass::Class2:
        return "Class 2";
    case ParentClass::Class3:
        return "Class 3";
    case ParentClass::Invalid:
        return "Invalid";
    }
    return "Invalid";
}

bool ParentClassResult::high_confidence_unsafe() const {
    return valid &&
           (classification == ParentClass::Class1 ||
            classification == ParentClass::Class2);
}

ParentClassResult classify_parent(
    const ParentClassInput& input,
    std::size_t expected_worlds) {
    ParentClassResult result;
    const std::size_t count =
        input.canonical_descriptors.size();
    if (expected_worlds < 2 ||
        count < 2 ||
        input.base_scores.size() != count ||
        input.combined_scores.size() != count ||
        input.base_samples.size() != count ||
        input.robustly_pass_dominated.size() != count ||
        !std::is_sorted(
            input.canonical_descriptors.begin(),
            input.canonical_descriptors.end()) ||
        std::adjacent_find(
            input.canonical_descriptors.begin(),
            input.canonical_descriptors.end()) !=
            input.canonical_descriptors.end()) {
        return result;
    }
    std::optional<std::size_t> best_dominated;
    std::optional<std::size_t> best_nondominated;
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(input.base_scores[index]) ||
            !std::isfinite(input.combined_scores[index]) ||
            input.base_samples[index].size() !=
                expected_worlds ||
            !std::all_of(
                input.base_samples[index].begin(),
                input.base_samples[index].end(),
                [](double value) {
                    return std::isfinite(value);
                })) {
            return result;
        }
        std::optional<std::size_t>& best =
            input.robustly_pass_dominated[index]
                ? best_dominated
                : best_nondominated;
        if (!best.has_value() ||
            input.combined_scores[index] >
                input.combined_scores[*best]) {
            best = index;
        }
    }
    if (!best_dominated.has_value() ||
        !best_nondominated.has_value()) {
        return result;
    }
    result.best_dominated_index = *best_dominated;
    result.best_nondominated_index = *best_nondominated;
    result.margin =
        input.combined_scores[*best_dominated] -
        input.combined_scores[*best_nondominated];
    if (!std::isfinite(result.margin)) {
        return ParentClassResult{};
    }
    const double residual_difference =
        (input.combined_scores[*best_dominated] -
         input.base_scores[*best_dominated]) -
        (input.combined_scores[*best_nondominated] -
         input.base_scores[*best_nondominated]);
    std::vector<double> differences(expected_worlds);
    double mean = 0.0;
    for (std::size_t world = 0;
         world < expected_worlds; ++world) {
        differences[world] =
            input.base_samples[*best_dominated][world] -
            input.base_samples[*best_nondominated][world] +
            residual_difference;
        if (!std::isfinite(differences[world])) {
            return ParentClassResult{};
        }
        mean += differences[world];
    }
    mean /= static_cast<double>(expected_worlds);
    double squared = 0.0;
    for (const double difference : differences) {
        const double centered = difference - mean;
        squared += centered * centered;
    }
    result.paired_standard_error =
        std::sqrt(
            squared /
            static_cast<double>(
                expected_worlds *
                (expected_worlds - 1)));
    if (!std::isfinite(
            result.paired_standard_error)) {
        return ParentClassResult{};
    }
    if (result.margin < 0.0) {
        result.classification = ParentClass::Safe;
        result.sigma =
            result.paired_standard_error == 0.0
                ? 0.0
                : result.margin /
                      result.paired_standard_error;
    } else if (
        result.margin > 0.0 &&
        result.paired_standard_error == 0.0) {
        result.classification = ParentClass::Class1;
        result.sigma = 0.0;
    } else if (
        result.margin > 0.0 &&
        result.paired_standard_error > 0.0 &&
        result.margin /
                result.paired_standard_error >=
            3.0) {
        result.classification = ParentClass::Class2;
        result.sigma =
            result.margin /
            result.paired_standard_error;
    } else {
        result.classification = ParentClass::Class3;
        result.sigma =
            result.paired_standard_error == 0.0
                ? 0.0
                : result.margin /
                      result.paired_standard_error;
    }
    if (!std::isfinite(result.sigma)) {
        return ParentClassResult{};
    }
    result.valid = true;
    return result;
}

RootCounts& RootCounts::operator+=(
    const RootCounts& other) {
    raw += other.raw;
    trivial += other.trivial;
    nontrivial += other.nontrivial;
    malformed += other.malformed;
    over_cap += other.over_cap;
    eligible += other.eligible;
    duplicate += other.duplicate;
    unique += other.unique;
    retained += other.retained;
    cap_dropped += other.cap_dropped;
    dominance_positive += other.dominance_positive;
    for (std::size_t index = 0;
         index < parent_classes.size(); ++index) {
        parent_classes[index] +=
            other.parent_classes[index];
    }
    return *this;
}

bool RootCounts::terminal_cross_sums_valid() const {
    const std::size_t class_count =
        std::accumulate(
            parent_classes.begin(),
            parent_classes.end(),
            std::size_t{0});
    return raw ==
               malformed + trivial + over_cap + eligible &&
           nontrivial == over_cap + eligible &&
           eligible == duplicate + unique &&
           unique == retained + cap_dropped &&
           dominance_positive <= retained &&
           class_count == dominance_positive;
}

ProductionAccounting&
ProductionAccounting::operator+=(
    const ProductionAccounting& other) {
    score_calls += other.score_calls;
    scored_actions += other.scored_actions;
    sampled_worlds += other.sampled_worlds;
    rollout_evaluations +=
        other.rollout_evaluations;
    terminal_evaluations +=
        other.terminal_evaluations;
    bootstrapped_evaluations +=
        other.bootstrapped_evaluations;
    dominance_transitions +=
        other.dominance_transitions;
    return *this;
}

bool ProductionAccounting::valid(
    std::size_t worlds_per_call,
    std::size_t rollouts_per_world) const {
    if (worlds_per_call == 0 ||
        rollouts_per_world == 0 ||
        score_calls >
            std::numeric_limits<std::size_t>::max() /
                worlds_per_call ||
        scored_actions >
            std::numeric_limits<std::size_t>::max() /
                worlds_per_call ||
        scored_actions * worlds_per_call >
            std::numeric_limits<std::size_t>::max() /
                rollouts_per_world) {
        return false;
    }
    return terminal_evaluations <=
               rollout_evaluations &&
           bootstrapped_evaluations ==
               rollout_evaluations -
                   terminal_evaluations &&
           sampled_worlds ==
               score_calls * worlds_per_call &&
           rollout_evaluations ==
               scored_actions *
                   worlds_per_call *
                   rollouts_per_world;
}

BlindSelection select_development_rows(
    const std::vector<BlindSelectionInput>&
        chronological_rows) {
    BlindSelection result;
    std::set<std::string> stable_ids;
    std::array<std::vector<std::size_t>, kDeckCount>
        by_deck;
    for (std::size_t index = 0;
         index < chronological_rows.size(); ++index) {
        const BlindSelectionInput& row =
            chronological_rows[index];
        const std::size_t deck =
            static_cast<std::size_t>(row.owner_deck);
        if (deck >= kDeckCount || row.stable_id.empty() ||
            !stable_ids.insert(row.stable_id).second) {
            return result;
        }
        by_deck[deck].push_back(index);
    }

    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (by_deck[deck].empty()) {
            return result;
        }
        const std::size_t background = by_deck[deck].front();
        std::vector<std::size_t> positive_candidates;
        for (const std::size_t index : by_deck[deck]) {
            if (index != background &&
                chronological_rows[index]
                    .dominance_positive) {
                positive_candidates.push_back(index);
            }
        }
        const auto retained_positions =
            learned_iteration::
                evenly_spaced_retained_indices(
                    positive_candidates.size(), 15);
        std::vector<BlindSelectionRow> deck_rows;
        deck_rows.reserve(1 + retained_positions.size());
        deck_rows.push_back({
            .input_index = background,
            .roles = static_cast<std::uint8_t>(
                DevelopmentRoleBackground |
                (chronological_rows[background]
                         .dominance_positive
                     ? DevelopmentRolePositive
                     : DevelopmentRoleNone)),
        });
        for (const std::size_t position :
             retained_positions) {
            deck_rows.push_back({
                .input_index =
                    positive_candidates[position],
                .roles = DevelopmentRolePositive,
            });
        }
        std::sort(
            deck_rows.begin(), deck_rows.end(),
            [](const BlindSelectionRow& first,
               const BlindSelectionRow& second) {
                return first.input_index <
                       second.input_index;
            });
        if (deck_rows.size() >
            kMaximumRootsPerOwnerGame) {
            return result;
        }
        result.rows_by_deck[deck] =
            deck_rows.size();
        result.positives_by_deck[deck] =
            static_cast<std::size_t>(std::count_if(
                deck_rows.begin(), deck_rows.end(),
                [](const BlindSelectionRow& row) {
                    return
                        (row.roles &
                         DevelopmentRolePositive) != 0;
                }));
        result.rows.insert(
            result.rows.end(),
            deck_rows.begin(), deck_rows.end());
    }
    result.valid =
        result.rows.size() <=
            kDeckCount * kMaximumRootsPerOwnerGame &&
        std::is_sorted(
            result.rows.begin(), result.rows.end(),
            [&](const BlindSelectionRow& first,
                const BlindSelectionRow& second) {
                const std::size_t first_deck =
                    static_cast<std::size_t>(
                        chronological_rows[first.input_index]
                            .owner_deck);
                const std::size_t second_deck =
                    static_cast<std::size_t>(
                        chronological_rows[second.input_index]
                            .owner_deck);
                return first_deck < second_deck ||
                       (first_deck == second_deck &&
                        first.input_index <
                            second.input_index);
            });
    return result;
}

} // namespace old_school::fq4_priority_collection
