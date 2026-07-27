#include "old_school/fq0_information_set.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace old_school::fq0_information_set {
namespace {

inline constexpr std::string_view kInformationSetSchema =
    "old-school-fq0-owner-information-set-v1";
inline constexpr std::string_view kLeafConsequenceSchema =
    "old-school-fq0-redacted-leaf-consequence-v2";
inline constexpr std::string_view kPriorityConsequenceSchema =
    "old-school-fq0-canonical-priority-consequence-v1";
inline constexpr std::string_view kIndexedSeedSchema =
    "old-school-fq0-indexed-seed-v1";

class ByteWriter {
  public:
    void boolean(bool value) {
        data_.push_back(value ? '\1' : '\0');
    }

    template <typename Integer>
    void integer(Integer value) {
        using Unsigned = std::make_unsigned_t<Integer>;
        const Unsigned converted =
            static_cast<Unsigned>(value);
        for (std::size_t byte = 0; byte < sizeof(Unsigned);
             ++byte) {
            data_.push_back(static_cast<char>(
                static_cast<unsigned char>(
                    converted >> (byte * 8U))));
        }
    }

    void text(std::string_view value) {
        integer<std::uint64_t>(value.size());
        data_.append(value);
    }

    template <typename Value, typename Append>
    void sequence(
        const std::vector<Value>& values, Append append) {
        integer<std::uint64_t>(values.size());
        for (const Value& value : values) {
            append(*this, value);
        }
    }

    const std::string& data() const {
        return data_;
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

void append_stack(
    ByteWriter& writer,
    const std::vector<StackObject>& stack) {
    writer.sequence(
        stack,
        [](ByteWriter& output,
           const StackObject& object) {
            output.integer<std::uint64_t>(
                static_cast<std::uint64_t>(object.kind));
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
}

void append_observation(
    ByteWriter& writer,
    const PlayerObservation& observation) {
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
    // Deliberately do not append revealed_opponent_hand. It is a debug-only
    // field and never part of a Learned information set.
    append_stack(writer, observation.stack);
    for (const std::size_t turns :
         observation.extra_turns_pending) {
        writer.integer<std::uint64_t>(turns);
    }
    writer.integer<std::uint64_t>(
        observation.active_player);
    writer.integer<std::uint64_t>(
        observation.starting_player);
    writer.integer<std::uint64_t>(
        observation.turn_number);
}

void canonicalize_leaf_observation(
    PlayerObservation& observation) {
    const std::size_t observer = observation.observer;
    if (observer >= observation.players.size()) {
        throw std::invalid_argument(
            "FQ0 leaf consequence has an invalid observer");
    }
    const auto relative_player =
        [observer](std::size_t player) {
            if (player >= 2) {
                throw std::invalid_argument(
                    "FQ0 leaf consequence has an invalid player");
            }
            return player == observer ? std::size_t{0}
                                      : std::size_t{1};
        };
    if (observer == 1) {
        std::swap(
            observation.players[0],
            observation.players[1]);
        std::swap(
            observation.extra_turns_pending[0],
            observation.extra_turns_pending[1]);
    }
    for (StackObject& object : observation.stack) {
        object.controller =
            relative_player(object.controller);
        if (object.target.has_value()) {
            object.target->player =
                relative_player(object.target->player);
        }
    }
    observation.active_player =
        relative_player(observation.active_player);
    observation.starting_player =
        relative_player(observation.starting_player);
    observation.observer = 0;

    std::set<PermanentId> permanent_ids;
    std::array<std::map<PermanentId, std::size_t>, 2>
        creature_indices;
    std::array<std::vector<std::vector<std::size_t>>, 2>
        incoming_creature_targets;
    std::map<StackObjectId, StackObjectId> stack_ids;

    for (std::size_t player = 0;
         player < observation.players.size(); ++player) {
        PublicPlayerState& public_player =
            observation.players[player];
        std::sort(
            public_player.exile.begin(),
            public_player.exile.end());
        incoming_creature_targets[player].resize(
            public_player.creatures.size());
        for (std::size_t index = 0;
             index < public_player.creatures.size(); ++index) {
            const PermanentId id =
                public_player.creatures[index].id;
            if (!permanent_ids.insert(id).second ||
                !creature_indices[player]
                     .emplace(id, index)
                     .second) {
                throw std::invalid_argument(
                    "FQ0 leaf consequence has a duplicate "
                    "permanent ID");
            }
        }

        for (const ArtifactPermanent& artifact :
             public_player.artifacts) {
            if (!permanent_ids
                     .insert(artifact.id)
                     .second) {
                throw std::invalid_argument(
                    "FQ0 leaf consequence has a duplicate "
                    "permanent ID");
            }
        }
    }

    for (std::size_t position = 0;
         position < observation.stack.size(); ++position) {
        const StackObject& object =
            observation.stack[position];
        if (!stack_ids
                 .emplace(
                     object.id,
                     static_cast<StackObjectId>(
                         position + 1))
                 .second) {
            throw std::invalid_argument(
                "FQ0 leaf consequence has a duplicate stack ID");
        }
        if (!object.target.has_value() ||
            !object.target->creature.has_value()) {
            continue;
        }
        if (object.target->player >=
            observation.players.size()) {
            throw std::invalid_argument(
                "FQ0 leaf consequence has an invalid target "
                "player");
        }
        const auto target =
            creature_indices[object.target->player].find(
                *object.target->creature);
        // A spell may legally retain a target whose object has already left
        // the battlefield. Every such target is the same rules-level
        // "missing object" consequence and therefore has no live incidence.
        if (target !=
            creature_indices[object.target->player].end()) {
            incoming_creature_targets[object.target->player]
                                     [target->second]
                                         .push_back(position);
        }
    }

    std::array<std::map<PermanentId, PermanentId>, 2>
        creature_ids;
    PermanentId next_permanent_id = 1;
    for (std::size_t player = 0;
         player < observation.players.size(); ++player) {
        PublicPlayerState& public_player =
            observation.players[player];
        std::sort(
            public_player.lands.begin(),
            public_player.lands.end(),
            [](const LandPermanent& left,
               const LandPermanent& right) {
                return std::tie(left.card, left.tapped) <
                       std::tie(right.card, right.tapped);
            });
        std::vector<std::size_t> creature_order(
            public_player.creatures.size());
        for (std::size_t index = 0;
             index < creature_order.size(); ++index) {
            creature_order[index] = index;
        }
        std::sort(
            creature_order.begin(), creature_order.end(),
            [&](std::size_t first, std::size_t second) {
                const CreaturePermanent& left =
                    public_player.creatures[first];
                const CreaturePermanent& right =
                    public_player.creatures[second];
                const auto left_fields = std::tie(
                    left.card, left.tapped,
                    left.summoning_sick, left.damage,
                    left.temporary_power_bonus,
                    left.temporary_toughness_bonus,
                    left.exile_on_death_this_turn);
                const auto right_fields = std::tie(
                    right.card, right.tapped,
                    right.summoning_sick, right.damage,
                    right.temporary_power_bonus,
                    right.temporary_toughness_bonus,
                    right.exile_on_death_this_turn);
                if (left_fields != right_fields) {
                    return left_fields < right_fields;
                }
                return incoming_creature_targets[player][first] <
                       incoming_creature_targets[player][second];
            });
        std::vector<CreaturePermanent> creatures;
        creatures.reserve(creature_order.size());
        for (const std::size_t index : creature_order) {
            CreaturePermanent creature =
                public_player.creatures[index];
            const PermanentId physical_id = creature.id;
            creature.id = next_permanent_id++;
            creature_ids[player].emplace(
                physical_id, creature.id);
            creatures.push_back(creature);
        }
        public_player.creatures = std::move(creatures);

        std::sort(
            public_player.artifacts.begin(),
            public_player.artifacts.end(),
            [](const ArtifactPermanent& left,
               const ArtifactPermanent& right) {
                return std::tie(left.card, left.tapped) <
                       std::tie(right.card, right.tapped);
            });
        for (ArtifactPermanent& artifact :
             public_player.artifacts) {
            artifact.id = next_permanent_id++;
        }
        std::sort(
            public_player.enchantments.begin(),
            public_player.enchantments.end());
    }

    for (StackObject& object : observation.stack) {
        object.id = stack_ids.at(object.id);
        if (object.target.has_value()) {
            Target& target = *object.target;
            if (target.player >= creature_ids.size()) {
                throw std::invalid_argument(
                    "FQ0 leaf consequence has an invalid target "
                    "player");
            }
            if (target.creature.has_value()) {
                const auto found =
                    creature_ids[target.player].find(
                        *target.creature);
                // Zero is a canonical missing-object sentinel. Live
                // canonical permanent IDs begin at one. Once the object is
                // missing, its former controller is no longer a
                // rules-relevant part of the target, so normalize both
                // coordinates.
                if (found == creature_ids[target.player].end()) {
                    target.player = 0;
                    target.creature = 0;
                } else {
                    target.creature = found->second;
                }
            }
        }
        if (object.spell_target.has_value()) {
            const auto found =
                stack_ids.find(*object.spell_target);
            // Zero is likewise the canonical missing-stack-object
            // sentinel.
            object.spell_target =
                found == stack_ids.end() ? 0 : found->second;
        }
    }
}

void append_canonical_leaf_observation(
    ByteWriter& writer,
    PlayerObservation observation) {
    canonicalize_leaf_observation(observation);
    append_observation(writer, observation);
}

void append_context(
    ByteWriter& writer,
    const LearnedDecisionContext& context);

std::size_t observer_relative_player(
    std::size_t player, std::size_t observer) {
    if (player >= 2 || observer >= 2) {
        throw std::invalid_argument(
            "FQ0 consequence has an invalid player coordinate");
    }
    return player == observer ? 0 : 1;
}

void append_canonical_leaf_context(
    ByteWriter& writer, LearnedDecisionContext context,
    std::size_t observer) {
    if (context.valid) {
        context.decision_player =
            observer_relative_player(
                context.decision_player, observer);
    }
    append_context(writer, context);
}

void append_context(
    ByteWriter& writer,
    const LearnedDecisionContext& context) {
    writer.boolean(context.valid);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(context.phase));
    writer.integer<std::uint64_t>(
        context.decision_player);
    writer.integer(context.consecutive_passes);
    writer.boolean(context.sorcery_actions);
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
    writer.boolean(action.source_permanent.has_value());
    if (action.source_permanent.has_value()) {
        writer.integer<std::uint64_t>(
            *action.source_permanent);
    }
    writer.integer(action.x_value);
}

void append_player_stats(
    ByteWriter& writer, const PlayerGameStats& stats) {
    writer.integer<std::uint64_t>(stats.cards_drawn);
    writer.integer<std::uint64_t>(stats.lands_played);
    writer.integer<std::uint64_t>(stats.spells_cast);
    writer.integer<std::uint64_t>(stats.spells_countered);
    writer.integer<std::uint64_t>(stats.damage_to_opponent);
    writer.integer<std::uint64_t>(stats.cards_milled);
    writer.integer<std::uint64_t>(stats.decisions);
    writer.integer<std::uint64_t>(
        stats.monte_carlo_rollouts);
}

bool priority_phase(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
    case TurnPhase::BeginCombat:
    case TurnPhase::EndCombat:
    case TurnPhase::SecondMain:
        return true;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        return false;
    }
    return false;
}

bool phase_has_sorcery_actions(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

void validate_priority_context(
    const GameState& state,
    const LearnedDecisionContext& context) {
    if (state.active_player >= state.players.size() ||
        state.starting_player >= state.players.size() ||
        !context.valid ||
        !priority_phase(context.phase) ||
        context.decision_player >= state.players.size() ||
        context.consecutive_passes < 0 ||
        context.consecutive_passes > 1 ||
        context.sorcery_actions !=
            phase_has_sorcery_actions(context.phase)) {
        throw std::invalid_argument(
            "FQ0 context is not a production Priority boundary");
    }
}

void validate_observation(
    const PlayerObservation& observation) {
    if (observation.observer >= observation.players.size() ||
        observation.active_player >=
            observation.players.size() ||
        observation.starting_player >=
            observation.players.size()) {
        throw std::invalid_argument(
            "FQ0 observation contains an invalid player");
    }
    if (observation.players[observation.observer].hand_size !=
        observation.hand.size()) {
        throw std::invalid_argument(
            "FQ0 owner hand identities do not match its public size");
    }
}

void validate_information_set_key(
    const InformationSetKey& key) {
    validate_observation(key.observation());
    if (key.observation()
            .revealed_opponent_hand.has_value()) {
        throw std::invalid_argument(
            "FQ0 information key retained a revealed opponent hand");
    }
    if (!key.context().valid ||
        !priority_phase(key.context().phase) ||
        key.context().decision_player !=
            key.observation().observer ||
        key.context().consecutive_passes < 0 ||
        key.context().consecutive_passes > 1 ||
        key.context().sorcery_actions !=
            phase_has_sorcery_actions(
                key.context().phase)) {
        throw std::invalid_argument(
            "FQ0 information key has an invalid Priority context");
    }
    if (key.ordered_actions().size() < 2) {
        throw std::invalid_argument(
            "FQ0 successor information set must have multiple actions");
    }
}

void validate_seed_domain(SeedDomain domain) {
    switch (domain) {
    case SeedDomain::RootDeterminization:
    case SeedDomain::RootMacroTransition:
    case SeedDomain::SuccessorSelectionDeterminization:
    case SeedDomain::SuccessorSelectionMacroTransition:
    case SeedDomain::SuccessorEvaluationDeterminization:
    case SeedDomain::SuccessorEvaluationMacroTransition:
    case SeedDomain::InvarianceCheck:
        return;
    }
    throw std::invalid_argument(
        "FQ0 indexed seed has an invalid domain");
}

void validate_seed_bank(SeedBank bank) {
    switch (bank) {
    case SeedBank::Root:
    case SeedBank::A:
    case SeedBank::B:
        return;
    }
    throw std::invalid_argument(
        "FQ0 indexed seed has an invalid bank");
}

struct TerminalDisposition {
    int winner = -1;
    EndReason reason = EndReason::LifeTotal;
};

std::optional<TerminalDisposition> terminal_disposition(
    const GameState& state) {
    if (state.failed_draw[0] || state.failed_draw[1]) {
        int winner = -1;
        if (state.failed_draw[0] != state.failed_draw[1]) {
            winner = state.failed_draw[0] ? 1 : 0;
        }
        return TerminalDisposition{
            .winner = winner,
            .reason = EndReason::EmptyLibrary,
        };
    }
    const bool player_zero_lost =
        state.players[0].life <= 0;
    const bool player_one_lost =
        state.players[1].life <= 0;
    if (!player_zero_lost && !player_one_lost) {
        return std::nullopt;
    }
    int winner = -1;
    if (player_zero_lost != player_one_lost) {
        winner = player_zero_lost ? 1 : 0;
    }
    return TerminalDisposition{
        .winner = winner,
        .reason = EndReason::LifeTotal,
    };
}

enum class PriorityConsequenceDisposition : std::uint8_t {
    ActionApplied,
    Passed,
    StackObjectResolved,
    WindowEnded,
    Terminal,
};

std::uint8_t hex_nibble(char digit) {
    if (digit >= '0' && digit <= '9') {
        return static_cast<std::uint8_t>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f') {
        return static_cast<std::uint8_t>(
            10 + digit - 'a');
    }
    throw std::logic_error(
        "SHA-256 implementation returned non-lowercase hex");
}

std::uint64_t first_sha256_word(std::string_view digest) {
    if (digest.size() != 64) {
        throw std::logic_error(
            "SHA-256 implementation returned the wrong width");
    }
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 16; ++index) {
        value = (value << 4U) |
                hex_nibble(digest[index]);
    }
    return value;
}

} // namespace

InformationSetKey make_information_set_key(
    const GameState& state,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> ordered_actions) {
    validate_priority_context(state, context);
    const std::vector<PriorityAction> authoritative =
        legal_priority_actions(
            state, context.decision_player,
            context.sorcery_actions);
    if (authoritative.size() < 2 ||
        !std::equal(
            authoritative.begin(), authoritative.end(),
            ordered_actions.begin(), ordered_actions.end())) {
        throw std::invalid_argument(
            "FQ0 information key actions are not the exact "
            "engine-authoritative legal set");
    }
    PlayerObservation observation =
        observe_game_state(state, context.decision_player);
    std::sort(
        observation.hand.begin(), observation.hand.end());
    InformationSetKey result(
        std::move(observation), context, authoritative);
    validate_information_set_key(result);
    static_cast<void>(
        descriptor_canonical_action_rows(result));
    return result;
}

std::string information_set_sha256(
    const InformationSetKey& key) {
    validate_information_set_key(key);
    ByteWriter writer;
    writer.text(kInformationSetSchema);
    append_observation(writer, key.observation_);
    append_context(writer, key.context_);
    writer.sequence(
        key.ordered_actions_,
        [](ByteWriter& output,
           const PriorityAction& action) {
            append_priority_action(output, action);
        });
    return artifact_integrity::sha256_string(writer.data());
}

std::vector<CanonicalActionRow>
descriptor_canonical_action_rows(
    const InformationSetKey& key) {
    validate_information_set_key(key);
    const auto& actions = key.ordered_actions();
    std::vector<CanonicalActionRow> rows;
    rows.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        const std::string descriptor =
            probes::stable_priority_action_descriptor(action);
        if (descriptor.empty()) {
            throw std::logic_error(
                "stable Priority action descriptor is empty");
        }
        rows.push_back({
            .descriptor = descriptor,
            .action = action,
        });
    }
    std::sort(
        rows.begin(), rows.end(),
        [](const CanonicalActionRow& first,
           const CanonicalActionRow& second) {
            return first.descriptor < second.descriptor;
        });
    for (std::size_t index = 1; index < rows.size();
         ++index) {
        if (rows[index - 1].descriptor ==
            rows[index].descriptor) {
            throw std::invalid_argument(
                "FQ0 action set contains a duplicate descriptor");
        }
    }
    return rows;
}

std::string redacted_leaf_consequence_sha256(
    const GameState& state, std::size_t observer,
    const LearnedDecisionContext& context,
    const std::optional<GameResult>& terminal_result) {
    PlayerObservation observation =
        observe_game_state(state, observer);
    std::sort(
        observation.hand.begin(), observation.hand.end());
    validate_observation(observation);
    if (terminal_result.has_value()) {
        if (context != LearnedDecisionContext{}) {
            throw std::invalid_argument(
                "FQ0 terminal leaf must have an absent context");
        }
    } else {
        validate_priority_context(state, context);
    }
    if (terminal_result.has_value() &&
        terminal_result->winner != -1 &&
        terminal_result->winner != 0 &&
        terminal_result->winner != 1) {
        throw std::invalid_argument(
            "FQ0 leaf consequence has an invalid terminal winner");
    }

    ByteWriter writer;
    writer.text(kLeafConsequenceSchema);
    append_canonical_leaf_observation(
        writer, observation);
    append_canonical_leaf_context(
        writer, context, observer);
    writer.boolean(terminal_result.has_value());
    if (terminal_result.has_value()) {
        const int winner =
            terminal_result->winner < 0
                ? -1
                : static_cast<int>(
                      observer_relative_player(
                          static_cast<std::size_t>(
                              terminal_result->winner),
                          observer));
        writer.integer(winner);
        writer.integer<std::uint64_t>(
            static_cast<std::uint64_t>(
                terminal_result->reason));
        writer.integer<std::uint64_t>(
            terminal_result->turns);
        writer.integer<std::uint64_t>(
            observer_relative_player(
                terminal_result->starting_player,
                observer));
        writer.integer(
            terminal_result
                ->ending_life[observer]);
        writer.integer(
            terminal_result
                ->ending_life[1 - observer]);
        append_player_stats(
            writer,
            terminal_result
                ->player_stats[observer]);
        append_player_stats(
            writer,
            terminal_result
                ->player_stats[1 - observer]);
    }
    return artifact_integrity::sha256_string(writer.data());
}

std::string canonical_priority_consequence_sha256(
    const GameState& state, std::size_t observer,
    const LearnedDecisionContext& context,
    const PriorityAction& action) {
    validate_priority_context(state, context);
    if (observer != context.decision_player) {
        throw std::invalid_argument(
            "FQ0 Priority consequence observer must own the "
            "information set");
    }
    if (terminal_disposition(state).has_value()) {
        throw std::invalid_argument(
            "FQ0 Priority consequence root is already terminal");
    }
    const std::vector<PriorityAction> authoritative =
        legal_priority_actions(
            state, context.decision_player,
            context.sorcery_actions);
    if (std::find(
            authoritative.begin(), authoritative.end(),
            action) == authoritative.end()) {
        throw std::invalid_argument(
            "FQ0 Priority consequence action is not exactly legal");
    }

    GameState successor = state;
    LearnedDecisionContext successor_context = context;
    PriorityConsequenceDisposition disposition =
        PriorityConsequenceDisposition::ActionApplied;
    std::optional<TerminalDisposition> terminal;
    if (action.kind == PriorityActionKind::Pass) {
        PriorityState priority{
            .player = context.decision_player,
            .consecutive_passes =
                context.consecutive_passes,
        };
        const PriorityPassResult pass =
            pass_priority(successor, priority);
        switch (pass) {
        case PriorityPassResult::Passed:
            disposition =
                PriorityConsequenceDisposition::Passed;
            successor_context.decision_player =
                priority.player;
            successor_context.consecutive_passes =
                priority.consecutive_passes;
            break;
        case PriorityPassResult::StackObjectResolved:
            terminal = terminal_disposition(successor);
            if (terminal.has_value()) {
                disposition =
                    PriorityConsequenceDisposition::Terminal;
                successor_context = {};
            } else {
                disposition = PriorityConsequenceDisposition::
                    StackObjectResolved;
                successor_context.decision_player =
                    priority.player;
                successor_context.consecutive_passes =
                    priority.consecutive_passes;
            }
            break;
        case PriorityPassResult::WindowEnded:
            disposition =
                PriorityConsequenceDisposition::WindowEnded;
            successor_context = {};
            break;
        }
    } else {
        if (!apply_priority_action(
                successor, context.decision_player, action,
                context.sorcery_actions)) {
            throw std::logic_error(
                "validated FQ0 Priority action became illegal");
        }
        successor_context.consecutive_passes = 0;
    }

    PlayerObservation observation =
        observe_game_state(successor, observer);
    std::sort(
        observation.hand.begin(), observation.hand.end());
    validate_observation(observation);
    if (disposition ==
            PriorityConsequenceDisposition::ActionApplied ||
        disposition ==
            PriorityConsequenceDisposition::Passed ||
        disposition ==
            PriorityConsequenceDisposition::
                StackObjectResolved) {
        validate_priority_context(
            successor, successor_context);
    } else if (successor_context !=
               LearnedDecisionContext{}) {
        throw std::logic_error(
            "closed FQ0 Priority consequence retained context");
    }

    ByteWriter writer;
    writer.text(kPriorityConsequenceSchema);
    append_canonical_leaf_observation(
        writer, observation);
    append_canonical_leaf_context(
        writer, successor_context, observer);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(disposition));
    writer.boolean(terminal.has_value());
    if (terminal.has_value()) {
        const int winner =
            terminal->winner < 0
                ? -1
                : static_cast<int>(
                      observer_relative_player(
                          static_cast<std::size_t>(
                              terminal->winner),
                          observer));
        writer.integer(winner);
        writer.integer<std::uint64_t>(
            static_cast<std::uint64_t>(
                terminal->reason));
    }
    return artifact_integrity::sha256_string(writer.data());
}

std::uint64_t derive_indexed_seed(
    std::uint64_t base_seed,
    const IndexedSeedCoordinates& coordinates) {
    validate_seed_domain(coordinates.domain);
    validate_seed_bank(coordinates.bank);
    if (coordinates.scope.empty() ||
        coordinates.group.empty()) {
        throw std::invalid_argument(
            "FQ0 indexed seed scope and group must be explicit");
    }
    ByteWriter writer;
    writer.text(kIndexedSeedSchema);
    writer.integer(base_seed);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(
            coordinates.domain));
    writer.text(coordinates.scope);
    writer.text(coordinates.group);
    writer.integer<std::uint64_t>(
        static_cast<std::uint64_t>(
            coordinates.bank));
    writer.integer<std::uint64_t>(
        coordinates.block);
    writer.integer<std::uint64_t>(
        coordinates.world);
    return first_sha256_word(
        artifact_integrity::sha256_string(writer.data()));
}

double terminal_root_owner_value(
    const GameResult& result, std::size_t root_owner) {
    if (root_owner >= 2) {
        throw std::invalid_argument(
            "terminal root owner must be 0 or 1");
    }
    if (result.winner == -1) {
        return 0.5;
    }
    if (result.winner != 0 && result.winner != 1) {
        throw std::invalid_argument(
            "terminal winner must be -1, 0, or 1");
    }
    return result.winner ==
                   static_cast<int>(root_owner)
               ? 1.0
               : 0.0;
}

LegacyLeafCriticEvaluation evaluate_legacy_leaf_critic(
    const GameState& state, std::size_t perspective,
    const LearnedDecisionContext& context,
    std::shared_ptr<const LearnedModel> model) {
    if (perspective >= state.players.size()) {
        throw std::invalid_argument(
            "FQ0 leaf critic perspective must be 0 or 1");
    }
    validate_priority_context(state, context);
    if (learned_critic_schema(model) !=
        LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "FQ0 frozen leaf critic must use LegacyStateOnly");
    }
    const double contextual =
        learned_contextual_critic_value(
            state, perspective, context, model);
    const double legacy =
        learned_critic_value(state, perspective, model);
    if (!std::isfinite(contextual) ||
        contextual < 0.0 || contextual > 1.0 ||
        !std::isfinite(legacy) ||
        legacy < 0.0 || legacy > 1.0) {
        throw std::logic_error(
            "FQ0 leaf critic returned a non-probability");
    }
    const std::uint64_t contextual_bits =
        std::bit_cast<std::uint64_t>(contextual);
    const std::uint64_t legacy_bits =
        std::bit_cast<std::uint64_t>(legacy);
    if (contextual_bits != legacy_bits) {
        throw std::logic_error(
            "FQ0 contextual/legacy critic bit identity failed");
    }
    return {
        .value = contextual,
        .contextual_bits = contextual_bits,
        .legacy_bits = legacy_bits,
        .legacy_bit_identity = true,
    };
}

} // namespace old_school::fq0_information_set
