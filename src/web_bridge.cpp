#include "old_school/web_bridge.hpp"
#include "old_school/artifact_integrity.hpp"
#include "old_school/selfplay_zero.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::web {
namespace {

std::vector<CardId> deck_cards(DeckId deck) {
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
    case DeckId::LotusCombo:
        return lotus_combo_deck();
    case DeckId::Burn:
        return burn_deck();
    case DeckId::UWR:
        return uwr_deck();
    case DeckId::Robots:
        return robots_deck();
    case DeckId::WhiteWeenie:
        return white_weenie_deck();
    case DeckId::BRMidrange:
        return br_midrange_deck();
    }
    throw std::out_of_range("unknown deck");
}

void validate_exact_deck_cards(
    const std::vector<CardId>& cards,
    std::string_view option_name) {
    if (cards.size() != 40) {
        throw std::invalid_argument(
            std::string(option_name) +
            " requires exactly 40 card IDs");
    }
    for (const CardId card : cards) {
        if (static_cast<std::size_t>(card) >= kCardCount) {
            throw std::invalid_argument(
                std::string(option_name) +
                " contains an unknown card ID");
        }
    }
}

std::vector<CardId> configured_deck_cards(
    DeckId fallback,
    const std::optional<std::vector<CardId>>& custom,
    std::string_view option_name) {
    if (!custom.has_value()) {
        return deck_cards(fallback);
    }
    validate_exact_deck_cards(*custom, option_name);
    return *custom;
}

std::string_view deck_id_token(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "green";
    case DeckId::Red:
        return "red";
    case DeckId::Blue:
        return "blue";
    case DeckId::White:
        return "white";
    case DeckId::RUAggro:
        return "ru-aggro";
    case DeckId::LotusCombo:
        return "lotus-combo";
    case DeckId::Burn:
        return "burn";
    case DeckId::UWR:
        return "uwr";
    case DeckId::Robots:
        return "robots";
    case DeckId::WhiteWeenie:
        return "white-weenie";
    case DeckId::BRMidrange:
        return "br-midrange";
    }
    throw std::out_of_range("unknown deck");
}

std::string_view evolution_pilot_token(EvolutionPilot pilot) {
    switch (pilot) {
    case EvolutionPilot::Random:
        return "random";
    case EvolutionPilot::MonteCarlo:
        return "monte-carlo";
    case EvolutionPilot::DeepMonteCarlo:
        return "deep-monte-carlo";
    case EvolutionPilot::Handcrafted:
        return "handcrafted";
    case EvolutionPilot::SelfPlayZero:
        return "spz";
    }
    throw std::out_of_range("unknown evolution pilot");
}

std::string_view card_type_name(CardType type) {
    switch (type) {
    case CardType::Land:
        return "land";
    case CardType::Creature:
        return "creature";
    case CardType::Instant:
        return "instant";
    case CardType::Sorcery:
        return "sorcery";
    case CardType::Artifact:
        return "artifact";
    case CardType::Enchantment:
        return "enchantment";
    }
    throw std::logic_error("unknown card type");
}

std::string_view phase_name(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
        return "first_main";
    case TurnPhase::BeginCombat:
        return "begin_combat";
    case TurnPhase::DeclareAttackers:
        return "declare_attackers";
    case TurnPhase::DeclareBlockers:
        return "declare_blockers";
    case TurnPhase::DamageOrder:
        return "damage_order";
    case TurnPhase::EndCombat:
        return "end_combat";
    case TurnPhase::SecondMain:
        return "second_main";
    }
    throw std::logic_error("unknown turn phase");
}

std::string_view event_kind_name(GameEventKind kind) {
    switch (kind) {
    case GameEventKind::TurnStarted:
        return "turn_started";
    case GameEventKind::PriorityActionSelected:
        return "priority_action";
    case GameEventKind::StackObjectResolved:
        return "stack_resolved";
    case GameEventKind::AttackersDeclared:
        return "attackers_declared";
    case GameEventKind::BlockersDeclared:
        return "blockers_declared";
    case GameEventKind::DamageOrderChosen:
        return "damage_order";
    case GameEventKind::CombatResolved:
        return "combat_resolved";
    case GameEventKind::CardsDiscarded:
        return "cards_discarded";
    case GameEventKind::MulliganTaken:
        return "mulligan_taken";
    }
    throw std::logic_error("unknown game event");
}

std::string_view action_kind_name(PriorityActionKind kind) {
    switch (kind) {
    case PriorityActionKind::Pass:
        return "pass";
    case PriorityActionKind::PlayLand:
        return "play_land";
    case PriorityActionKind::CastCreature:
        return "cast_creature";
    case PriorityActionKind::CastSorcery:
        return "cast_sorcery";
    case PriorityActionKind::CastArtifact:
        return "cast_artifact";
    case PriorityActionKind::CastEnchantment:
        return "cast_enchantment";
    case PriorityActionKind::CastLightningBolt:
        return "cast_lightning_bolt";
    case PriorityActionKind::CastDisintegrate:
        return "cast_disintegrate";
    case PriorityActionKind::CastGiantGrowth:
        return "cast_giant_growth";
    case PriorityActionKind::CastCounterspell:
        return "cast_counterspell";
    case PriorityActionKind::CastAncestralRecall:
        return "cast_ancestral_recall";
    case PriorityActionKind::CastBraingeyser:
        return "cast_braingeyser";
    case PriorityActionKind::CastForceSpike:
        return "cast_force_spike";
    case PriorityActionKind::CastManaDrain:
        return "cast_mana_drain";
    case PriorityActionKind::CastMindTwist:
        return "cast_mind_twist";
    case PriorityActionKind::CastRecall:
        return "cast_recall";
    case PriorityActionKind::CastDemonicTutor:
        return "cast_demonic_tutor";
    case PriorityActionKind::CastCopyArtifact:
        return "cast_copy_artifact";
    case PriorityActionKind::ActivateSage:
        return "activate_sage";
    case PriorityActionKind::TapLandForMana:
        return "tap_land_for_mana";
    case PriorityActionKind::ActivateChaosOrb:
        return "activate_chaos_orb";
    case PriorityActionKind::ActivateJalumTome:
        return "activate_jalum_tome";
    case PriorityActionKind::CastDarkRitual:
        return "cast_dark_ritual";
    case PriorityActionKind::CastShatter:
        return "cast_shatter";
    case PriorityActionKind::ActivateTriskelion:
        return "activate_triskelion";
    case PriorityActionKind::ActivateMillstone:
        return "activate_millstone";
    case PriorityActionKind::CastPsionicBlast:
        return "cast_psionic_blast";
    case PriorityActionKind::CastSwordsToPlowshares:
        return "cast_swords_to_plowshares";
    case PriorityActionKind::CastDisenchant:
        return "cast_disenchant";
    case PriorityActionKind::ActivateLibrary:
        return "activate_library";
    case PriorityActionKind::ActivateMishrasFactory:
        return "activate_mishras_factory";
    case PriorityActionKind::ActivateStripMine:
        return "activate_strip_mine";
    }
    throw std::logic_error("unknown priority action");
}

std::string_view end_reason_name(EndReason reason) {
    switch (reason) {
    case EndReason::LifeTotal:
        return "life_total";
    case EndReason::EmptyLibrary:
        return "empty_library";
    case EndReason::TurnLimit:
        return "turn_limit";
    }
    throw std::logic_error("unknown end reason");
}

void write_json_string(std::ostream& output,
                       std::string_view value) {
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20U) {
                constexpr char kHex[] = "0123456789abcdef";
                output << "\\u00"
                       << kHex[(character >> 4U) & 0x0fU]
                       << kHex[character & 0x0fU];
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    output << '"';
}

void write_json_real(std::ostream& output, double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "evolution result contains a non-finite number");
    }
    const std::streamsize old_precision = output.precision();
    output << std::setprecision(17) << value;
    output.precision(old_precision);
}

void write_evolution_stats(std::ostream& output,
                           const DeckSimulationStats& stats) {
    output << "{\"games\":" << stats.games
           << ",\"wins\":" << stats.wins
           << ",\"losses\":" << stats.losses
           << ",\"draws\":" << stats.draws
           << ",\"winRate\":";
    write_json_real(output, stats.win_rate());
    output << '}';
}

void write_model_status(
    std::ostream& output, std::string_view message,
    std::string_view family, std::size_t generation,
    std::size_t search_worlds, std::size_t horizon_turns,
    std::string_view source,
    std::string_view fingerprint) {
    output << "{\"type\":\"status\",\"message\":";
    write_json_string(output, message);
    output << ",\"model\":{\"family\":";
    write_json_string(output, family);
    output << ",\"generation\":" << generation
           << ",\"searchWorlds\":" << search_worlds
           << ",\"horizonTurns\":" << horizon_turns
           << ",\"source\":";
    write_json_string(output, source);
    output << ",\"fingerprint\":";
    write_json_string(output, fingerprint);
    output << "}}\n" << std::flush;
}

void write_mana(std::ostream& output, const ManaCost& mana) {
    output << "{\"generic\":" << mana.generic
           << ",\"green\":" << mana.green
           << ",\"red\":" << mana.red
           << ",\"blue\":" << mana.blue
           << ",\"white\":" << mana.white
           << ",\"black\":" << mana.black << '}';
}

std::string mana_label(const ManaCost& mana) {
    std::string result;
    if (mana.generic != 0) {
        result += std::to_string(mana.generic);
    }
    result.append(static_cast<std::size_t>(mana.green), 'G');
    result.append(static_cast<std::size_t>(mana.red), 'R');
    result.append(static_cast<std::size_t>(mana.blue), 'U');
    result.append(static_cast<std::size_t>(mana.white), 'W');
    result.append(static_cast<std::size_t>(mana.black), 'B');
    return result;
}

void write_card(std::ostream& output, CardId card) {
    const auto& definition = card_definition(card);
    output << "{\"id\":\"card-"
           << static_cast<unsigned int>(card) << "\",\"name\":";
    write_json_string(output, definition.name);
    output << ",\"type\":";
    write_json_string(output, card_type_name(definition.type));
    output << ",\"cost\":";
    write_mana(output, definition.cost);
    output << ",\"costLabel\":";
    write_json_string(output, mana_label(definition.cost));
    output << ",\"power\":" << definition.power
           << ",\"toughness\":" << definition.toughness
           << ",\"flying\":"
           << (definition.flying ? "true" : "false") << '}';
}

void write_cards(std::ostream& output,
                 const std::vector<CardId>& cards) {
    output << '[';
    for (std::size_t index = 0; index < cards.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_card(output, cards[index]);
    }
    output << ']';
}

std::optional<std::pair<std::size_t, const CreaturePermanent*>>
find_creature(const PlayerObservation& observation,
              PermanentId permanent) {
    for (std::size_t player = 0;
         player < observation.players.size(); ++player) {
        const auto& creatures =
            observation.players[player].creatures;
        const auto found = std::find_if(
            creatures.begin(), creatures.end(),
            [permanent](const CreaturePermanent& creature) {
                return creature.id == permanent;
            });
        if (found != creatures.end()) {
            return std::pair<std::size_t,
                             const CreaturePermanent*>{
                player, &*found};
        }
    }
    return std::nullopt;
}

std::string creature_label(const PlayerObservation& observation,
                           PermanentId permanent) {
    const auto found = find_creature(observation, permanent);
    if (!found.has_value()) {
        for (const auto& player : observation.players) {
            for (const auto& land : player.lands) {
                if (land.id == permanent) {
                    return std::string(
                               card_definition(land.card).name) +
                           " #" + std::to_string(permanent);
                }
            }
        }
        return "permanent #" + std::to_string(permanent);
    }
    return std::string(
               card_definition(found->second->card).name) +
           " #" + std::to_string(permanent);
}

std::string target_label(const PlayerObservation& observation,
                         const Target& target) {
    if (target.creature.has_value()) {
        return creature_label(observation, *target.creature);
    }
    return target.player == observation.observer
               ? "You"
               : "Opponent";
}

std::string action_label(const PlayerObservation& observation,
                         const PriorityAction& action) {
    if (action.kind == PriorityActionKind::Pass) {
        return "Pass priority";
    }
    if (action.kind == PriorityActionKind::TapLandForMana) {
        static constexpr std::array<const char*, 6> kFaceNames = {
            "{1}", "{G}", "{R}", "{U}", "{W}", "{B}",
        };
        const int face = action.x_value >= 0 && action.x_value < 6
                             ? action.x_value
                             : 0;
        std::string source_name = "permanent";
        const auto& mine =
            observation.players[observation.observer];
        if (action.source_permanent.has_value()) {
            for (const auto& land : mine.lands) {
                if (land.id == *action.source_permanent) {
                    source_name = std::string(
                        card_definition(land.card).name);
                }
            }
            for (const auto& artifact : mine.artifacts) {
                if (artifact.id == *action.source_permanent) {
                    source_name = std::string(
                        card_definition(artifact.is_copy
                                            ? artifact.copy_of
                                            : artifact.card)
                            .name);
                }
            }
            for (const auto& creature : mine.creatures) {
                if (creature.id == *action.source_permanent) {
                    source_name = std::string(
                        card_definition(creature.card).name);
                }
            }
        }
        return "Tap " + source_name + " for " +
               kFaceNames[static_cast<std::size_t>(face)];
    }
    const std::string card(card_definition(action.card).name);
    const bool activation =
        action.kind == PriorityActionKind::ActivateMillstone ||
        action.kind == PriorityActionKind::ActivateLibrary ||
        action.kind == PriorityActionKind::ActivateStripMine;
    std::string result =
        action.kind == PriorityActionKind::PlayLand
            ? "Play " + card
            : action.kind ==
                      PriorityActionKind::ActivateMishrasFactory
                  ? "Animate " + card
                  : activation ? "Activate " + card
                               : "Cast " + card;
    if (action.kind == PriorityActionKind::CastDisintegrate ||
        action.kind == PriorityActionKind::CastBraingeyser) {
        result += " (X=" + std::to_string(action.x_value) + ")";
    }
    if (action.target.has_value()) {
        result += " → " +
                  target_label(observation, *action.target);
    }
    if (action.spell_target.has_value()) {
        result += " → stack #" +
                  std::to_string(*action.spell_target);
    }
    return result;
}

std::string stack_label(const PlayerObservation& observation,
                        const StackObject& object) {
    std::string result =
        object.controller == observation.observer
            ? "Your "
            : "Opponent's ";
    result += card_definition(object.card).name;
    if (object.card == CardId::Disintegrate ||
        object.card == CardId::Braingeyser) {
        result += " (X=" + std::to_string(object.x_value) + ")";
    }
    if (object.target.has_value()) {
        result += " → " +
                  target_label(observation, *object.target);
    }
    if (object.spell_target.has_value()) {
        result += " → stack #" +
                  std::to_string(*object.spell_target);
    }
    return result;
}

void write_target(std::ostream& output,
                  const PlayerObservation& observation,
                  const Target& target) {
    output << "{\"player\":" << target.player;
    if (target.creature.has_value()) {
        output << ",\"creature\":"
               << *target.creature;
    }
    output << ",\"label\":";
    write_json_string(output, target_label(observation, target));
    output << '}';
}

void write_land(std::ostream& output,
                const LandPermanent& land,
                std::size_t index) {
    // Engine permanent ids let the client wire lands as ability
    // origins and targets (Strip Mine, Factory); the index-keyed
    // "land-N" form survives only for id-less snapshots.
    if (land.id != 0) {
        output << "{\"permanentId\":" << land.id << ",\"card\":";
    } else {
        output << "{\"permanentId\":\"land-" << index
               << "\",\"card\":";
    }
    write_card(output, land.card);
    if (land.is_copy) {
        output << ",\"copyOf\":";
        write_card(output, land.copy_of);
    }
    output << ",\"tapped\":"
           << (land.tapped ? "true" : "false") << '}';
}

void write_creature(std::ostream& output,
                    const CreaturePermanent& creature) {
    const auto& definition = card_definition(
        creature.is_copy ? creature.copy_of : creature.card);
    output << "{\"permanentId\":" << creature.id
           << ",\"card\":";
    write_card(output, creature.card);
    output << ",\"tapped\":"
           << (creature.tapped ? "true" : "false")
           << ",\"summoningSick\":"
           << (creature.summoning_sick ? "true" : "false")
           << ",\"damage\":" << creature.damage
           << ",\"power\":"
           << definition.power + creature.temporary_power_bonus +
                  creature.plus_counters + creature.crusade_bonus
           << ",\"toughness\":"
           << definition.toughness +
                  creature.temporary_toughness_bonus +
                  creature.plus_counters + creature.crusade_bonus
           << ",\"plusCounters\":" << creature.plus_counters;
    if (creature.is_copy) {
        output << ",\"copyOf\":";
        write_card(output, creature.copy_of);
    }
    output << '}';
}

void write_artifact(std::ostream& output,
                    const ArtifactPermanent& artifact) {
    output << "{\"permanentId\":" << artifact.id
           << ",\"card\":";
    write_card(output, artifact.card);
    if (artifact.is_copy) {
        output << ",\"copyOf\":";
        write_card(output, artifact.copy_of);
    }
    output << ",\"tapped\":"
           << (artifact.tapped ? "true" : "false") << '}';
}

void write_enchantment(std::ostream& output, CardId card,
                       std::size_t index) {
    output << "{\"permanentId\":\"enchantment-" << index
           << "\",\"card\":";
    write_card(output, card);
    output << ",\"tapped\":false}";
}

template <typename Value, typename Writer>
void write_array(std::ostream& output,
                 const std::vector<Value>& values,
                 Writer writer) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        writer(output, values[index], index);
    }
    output << ']';
}

void write_public_player(
    std::ostream& output, const PlayerObservation& observation,
    std::size_t player) {
    const auto& state = observation.players[player];
    output << "{\"life\":" << state.life
           << ",\"librarySize\":" << state.library_size
           << ",\"handSize\":" << state.hand_size
           << ",\"graveyard\":";
    write_cards(output, state.graveyard);
    output << ",\"exile\":";
    write_cards(output, state.exile);
    output << ",\"lands\":";
    write_array(
        output, state.lands,
        [](std::ostream& target, const LandPermanent& land,
           std::size_t index) {
            write_land(target, land, index);
        });
    output << ",\"creatures\":";
    write_array(
        output, state.creatures,
        [](std::ostream& target,
           const CreaturePermanent& creature, std::size_t) {
            write_creature(target, creature);
        });
    output << ",\"artifacts\":";
    write_array(
        output, state.artifacts,
        [](std::ostream& target,
           const ArtifactPermanent& artifact, std::size_t) {
            write_artifact(target, artifact);
        });
    output << ",\"enchantments\":";
    write_array(
        output, state.enchantments,
        [](std::ostream& target, CardId card,
           std::size_t index) {
            write_enchantment(target, card, index);
        });
    output << ",\"manaPool\":";
    write_mana(output, state.mana_pool);
    output << ",\"landPlayedThisTurn\":"
           << (state.land_played_this_turn ? "true" : "false")
           << ",\"extraTurns\":"
           << observation.extra_turns_pending[player];
    if (player == observation.observer) {
        output << ",\"hand\":";
        write_cards(output, observation.hand);
    } else if (observation.revealed_opponent_hand.has_value()) {
        output << ",\"revealedHand\":";
        write_cards(
            output, *observation.revealed_opponent_hand);
    }
    output << '}';
}

void write_stack_object(
    std::ostream& output,
    const PlayerObservation& observation,
    const StackObject& object) {
    output << "{\"stackId\":" << object.id
           << ",\"kind\":";
    write_json_string(
        output, object.kind == StackObjectKind::Spell
                    ? "spell"
                    : "activated_ability");
    output << ",\"controller\":" << object.controller
           << ",\"card\":";
    write_card(output, object.card);
    output << ",\"xValue\":" << object.x_value
           << ",\"label\":";
    write_json_string(output, stack_label(observation, object));
    if (object.target.has_value()) {
        output << ",\"target\":";
        write_target(output, observation, *object.target);
    }
    if (object.spell_target.has_value()) {
        output << ",\"spellTarget\":"
               << *object.spell_target;
    }
    output << '}';
}

void write_state(std::ostream& output,
                 const PlayerObservation& observation,
                 TurnPhase phase) {
    output << "{\"turnNumber\":" << observation.turn_number
           << ",\"activePlayer\":" << observation.active_player
           << ",\"startingPlayer\":"
           << observation.starting_player
           << ",\"phase\":";
    write_json_string(output, phase_name(phase));
    output << ",\"observer\":" << observation.observer
           << ",\"players\":[";
    write_public_player(output, observation, 0);
    output << ',';
    write_public_player(output, observation, 1);
    output << "],\"stack\":[";
    for (std::size_t index = 0;
         index < observation.stack.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_stack_object(
            output, observation, observation.stack[index]);
    }
    output << "]}";
}

std::size_t find_field_value(std::string_view line,
                             std::string_view field) {
    const std::string quoted =
        "\"" + std::string(field) + "\"";
    const std::size_t key = line.find(quoted);
    if (key == std::string_view::npos) {
        throw std::invalid_argument(
            "response is missing field " + std::string(field));
    }
    const std::size_t colon =
        line.find(':', key + quoted.size());
    if (colon == std::string_view::npos) {
        throw std::invalid_argument(
            "response has malformed field " + std::string(field));
    }
    return colon + 1;
}

std::uint64_t parse_unsigned_at(std::string_view line,
                                std::size_t offset,
                                std::size_t& end) {
    while (offset < line.size() &&
           std::isspace(
               static_cast<unsigned char>(line[offset])) != 0) {
        ++offset;
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(
        line.data() + offset, line.data() + line.size(), value);
    if (parsed.ec != std::errc{}) {
        throw std::invalid_argument(
            "response contains a non-integer value");
    }
    end = static_cast<std::size_t>(
        parsed.ptr - line.data());
    return value;
}

std::uint64_t parse_unsigned_field(std::string_view line,
                                   std::string_view field) {
    std::size_t end = 0;
    return parse_unsigned_at(
        line, find_field_value(line, field), end);
}

std::vector<std::uint64_t>
parse_unsigned_array_field(std::string_view line,
                           std::string_view field) {
    const std::size_t value = find_field_value(line, field);
    const std::size_t begin = line.find('[', value);
    if (begin == std::string_view::npos) {
        throw std::invalid_argument(
            "response field " + std::string(field) +
            " is not an array");
    }
    std::size_t depth = 0;
    std::size_t end = begin;
    for (; end < line.size(); ++end) {
        if (line[end] == '[') {
            ++depth;
        } else if (line[end] == ']') {
            if (--depth == 0) {
                break;
            }
        }
    }
    if (end == line.size()) {
        throw std::invalid_argument(
            "response has an unterminated array");
    }
    std::vector<std::uint64_t> values;
    for (std::size_t cursor = begin + 1; cursor < end;) {
        if (std::isdigit(
                static_cast<unsigned char>(line[cursor])) != 0) {
            std::size_t parsed_end = cursor;
            values.push_back(
                parse_unsigned_at(line, cursor, parsed_end));
            cursor = parsed_end;
        } else {
            ++cursor;
        }
    }
    return values;
}

std::vector<std::uint64_t>
parse_strict_unsigned_array_field(std::string_view line,
                                  std::string_view field) {
    std::size_t cursor = find_field_value(line, field);
    const auto skip_whitespace = [&line](std::size_t& position) {
        while (position < line.size() &&
               std::isspace(static_cast<unsigned char>(
                   line[position])) != 0) {
            ++position;
        }
    };
    const auto malformed = [field]() {
        throw std::invalid_argument(
            "response field " + std::string(field) +
            " must be an array of unsigned integers");
    };

    skip_whitespace(cursor);
    if (cursor >= line.size() || line[cursor] != '[') {
        malformed();
    }
    ++cursor;
    skip_whitespace(cursor);

    std::vector<std::uint64_t> values;
    if (cursor < line.size() && line[cursor] == ']') {
        ++cursor;
    } else {
        while (true) {
            if (cursor >= line.size() ||
                std::isdigit(static_cast<unsigned char>(
                    line[cursor])) == 0) {
                malformed();
            }
            std::uint64_t value = 0;
            const auto parsed = std::from_chars(
                line.data() + cursor, line.data() + line.size(),
                value);
            if (parsed.ec != std::errc{}) {
                malformed();
            }
            cursor = static_cast<std::size_t>(
                parsed.ptr - line.data());
            values.push_back(value);
            skip_whitespace(cursor);
            if (cursor >= line.size()) {
                malformed();
            }
            if (line[cursor] == ']') {
                ++cursor;
                break;
            }
            if (line[cursor] != ',') {
                malformed();
            }
            ++cursor;
            skip_whitespace(cursor);
        }
    }

    skip_whitespace(cursor);
    if (cursor < line.size() &&
        line[cursor] != ',' && line[cursor] != '}') {
        malformed();
    }
    return values;
}

template <typename Value>
bool unique_values(const std::vector<Value>& values) {
    return std::set<Value>(values.begin(), values.end()).size() ==
           values.size();
}

class JsonController {
  public:
    JsonController(std::istream& input, std::ostream& output,
                   bool reveal_opponent_hand, bool bluff_mode)
        : input_(input), output_(output),
          reveal_opponent_hand_(reveal_opponent_hand),
          bluff_mode_(bluff_mode) {}

    HumanController controller() {
        return {
            .choose_priority_action =
                [this](const PlayerObservation& observation,
                       TurnPhase phase,
                       const std::vector<PriorityAction>& actions) {
                    return choose_priority(
                        observation, phase, actions);
                },
            .choose_attackers =
                [this](const PlayerObservation& observation,
                       const std::vector<PermanentId>& attackers) {
                    return choose_attackers(
                        observation, attackers);
                },
            .choose_blockers =
                [this](
                    const PlayerObservation& observation,
                    const std::vector<PermanentId>& attackers,
                    const std::vector<LegalBlockerChoice>& choices) {
                    return choose_blockers(
                        observation, attackers, choices);
                },
            .choose_damage_order =
                [this](const PlayerObservation& observation,
                       PermanentId attacker,
                       const std::vector<PermanentId>& blockers) {
                    return choose_damage_order(
                        observation, attacker, blockers);
                },
            .choose_cleanup_discards =
                [this](const PlayerObservation& observation,
                       std::size_t excess) {
                    return choose_cleanup_discards(
                        observation, excess);
                },
            .choose_mulligan =
                [this](const PlayerObservation& observation) {
                    return choose_mulligan(observation);
                },
            .observe =
                [this](const PlayerObservation& observation,
                       const GameEvent& event) {
                    observe(observation, event);
                },
            .reveal_opponent_hand = reveal_opponent_hand_,
            .bluff_mode = bluff_mode_,
            .offers_mana_taps = true,
        };
    }

    void emit_game_over(const PlayerObservation& observation,
                        const GameResult& result) {
        output_ << "{\"type\":\"game_over\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"result\":{\"winner\":"
                << result.winner << ",\"reason\":";
        write_json_string(output_, end_reason_name(result.reason));
        output_ << ",\"turns\":" << result.turns
                << ",\"startingPlayer\":"
                << result.starting_player
                << ",\"endingLife\":["
                << result.ending_life[0] << ','
                << result.ending_life[1] << "]}}\n"
                << std::flush;
    }

  private:
    std::uint64_t next_decision_id() {
        return ++decision_id_;
    }

    std::string read_response(std::uint64_t expected_id) {
        std::string line;
        if (!std::getline(input_, line)) {
            throw std::runtime_error(
                "web client disconnected during a decision");
        }
        if (parse_unsigned_field(line, "decisionId") !=
            expected_id) {
            throw std::invalid_argument(
                "response decisionId is stale");
        }
        return line;
    }

    std::size_t choose_priority(
        const PlayerObservation& observation, TurnPhase phase,
        const std::vector<PriorityAction>& actions) {
        phase_ = phase;
        const std::uint64_t id = next_decision_id();
        output_ << "{\"type\":\"decision\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"decision\":{\"id\":" << id
                << ",\"kind\":\"priority\",\"phase\":";
        write_json_string(output_, phase_name(phase));
        output_ << ",\"options\":[";
        for (std::size_t index = 0; index < actions.size();
             ++index) {
            if (index != 0) {
                output_ << ',';
            }
            const auto& action = actions[index];
            output_ << "{\"index\":" << index
                    << ",\"label\":";
            write_json_string(
                output_, action_label(observation, action));
            output_ << ",\"kind\":";
            write_json_string(
                output_, action_kind_name(action.kind));
            if (action.kind != PriorityActionKind::Pass) {
                output_ << ",\"card\":";
                write_card(output_, action.card);
            }
            if (action.target.has_value()) {
                output_ << ",\"target\":";
                write_target(
                    output_, observation, *action.target);
            }
            if (action.spell_target.has_value()) {
                output_ << ",\"spellTarget\":"
                        << *action.spell_target;
            }
            if (action.source_permanent.has_value()) {
                output_ << ",\"sourcePermanent\":"
                        << *action.source_permanent;
            }
            if (action.x_value != 0) {
                output_ << ",\"xValue\":"
                        << action.x_value;
            }
            output_ << '}';
        }
        output_ << "]}}\n" << std::flush;

        const std::string response = read_response(id);
        const std::uint64_t chosen =
            parse_unsigned_field(response, "index");
        if (chosen >= actions.size()) {
            throw std::invalid_argument(
                "priority response selects an illegal option");
        }
        return static_cast<std::size_t>(chosen);
    }

    std::vector<PermanentId> choose_attackers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& eligible) {
        phase_ = TurnPhase::DeclareAttackers;
        const std::uint64_t id = next_decision_id();
        output_ << "{\"type\":\"decision\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"decision\":{\"id\":" << id
                << ",\"kind\":\"attackers\",\"eligible\":[";
        for (std::size_t index = 0; index < eligible.size();
             ++index) {
            if (index != 0) {
                output_ << ',';
            }
            output_ << eligible[index];
        }
        output_ << "]}}\n" << std::flush;

        const auto selected = parse_unsigned_array_field(
            read_response(id), "ids");
        if (!unique_values(selected)) {
            throw std::invalid_argument(
                "attacker response contains duplicates");
        }
        std::vector<PermanentId> result;
        result.reserve(selected.size());
        for (const std::uint64_t attacker : selected) {
            if (std::find(eligible.begin(), eligible.end(),
                          attacker) == eligible.end()) {
                throw std::invalid_argument(
                    "attacker response selects an illegal creature");
            }
            result.push_back(attacker);
        }
        return result;
    }

    std::vector<std::pair<PermanentId, PermanentId>>
    choose_blockers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& attackers,
        const std::vector<LegalBlockerChoice>& choices) {
        phase_ = TurnPhase::DeclareBlockers;
        const std::uint64_t id = next_decision_id();
        output_ << "{\"type\":\"decision\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"decision\":{\"id\":" << id
                << ",\"kind\":\"blockers\",\"attackers\":[";
        for (std::size_t index = 0; index < attackers.size();
             ++index) {
            if (index != 0) {
                output_ << ',';
            }
            output_ << attackers[index];
        }
        output_ << "],\"choices\":[";
        for (std::size_t index = 0; index < choices.size();
             ++index) {
            if (index != 0) {
                output_ << ',';
            }
            output_ << "{\"blocker\":" << choices[index].blocker
                    << ",\"legalAttackers\":[";
            for (std::size_t attacker = 0;
                 attacker <
                 choices[index].legal_attackers.size();
                 ++attacker) {
                if (attacker != 0) {
                    output_ << ',';
                }
                output_
                    << choices[index].legal_attackers[attacker];
            }
            output_ << "]}";
        }
        output_ << "]}}\n" << std::flush;

        const auto values = parse_unsigned_array_field(
            read_response(id), "pairs");
        if (values.size() % 2 != 0) {
            throw std::invalid_argument(
                "block response pairs are malformed");
        }
        std::set<PermanentId> used_blockers;
        std::vector<std::pair<PermanentId, PermanentId>> result;
        for (std::size_t index = 0; index < values.size();
             index += 2) {
            const PermanentId attacker = values[index];
            const PermanentId blocker = values[index + 1];
            const auto choice = std::find_if(
                choices.begin(), choices.end(),
                [blocker](const LegalBlockerChoice& candidate) {
                    return candidate.blocker == blocker;
                });
            if (choice == choices.end() ||
                std::find(
                    choice->legal_attackers.begin(),
                    choice->legal_attackers.end(),
                    attacker) ==
                    choice->legal_attackers.end() ||
                !used_blockers.insert(blocker).second) {
                throw std::invalid_argument(
                    "block response contains an illegal assignment");
            }
            result.emplace_back(attacker, blocker);
        }
        return result;
    }

    std::vector<PermanentId> choose_damage_order(
        const PlayerObservation& observation,
        PermanentId attacker,
        const std::vector<PermanentId>& blockers) {
        phase_ = TurnPhase::DamageOrder;
        const std::uint64_t id = next_decision_id();
        output_ << "{\"type\":\"decision\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"decision\":{\"id\":" << id
                << ",\"kind\":\"damage_order\",\"attacker\":"
                << attacker << ",\"blockers\":[";
        for (std::size_t index = 0; index < blockers.size();
             ++index) {
            if (index != 0) {
                output_ << ',';
            }
            output_ << blockers[index];
        }
        output_ << "]}}\n" << std::flush;

        auto selected = parse_unsigned_array_field(
            read_response(id), "ids");
        auto expected = std::vector<std::uint64_t>(
            blockers.begin(), blockers.end());
        auto sorted_selected = selected;
        std::sort(sorted_selected.begin(), sorted_selected.end());
        std::sort(expected.begin(), expected.end());
        if (sorted_selected != expected) {
            throw std::invalid_argument(
                "damage order must be an exact blocker permutation");
        }
        return std::vector<PermanentId>(
            selected.begin(), selected.end());
    }

    std::vector<std::size_t> choose_cleanup_discards(
        const PlayerObservation& observation,
        std::size_t excess) {
        phase_ = TurnPhase::SecondMain;
        const std::uint64_t id = next_decision_id();
        output_ << "{\"type\":\"decision\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"decision\":{\"id\":" << id
                << ",\"kind\":\"cleanup_discard\",\"count\":"
                << excess << ",\"options\":[";
        for (std::size_t index = 0;
             index < observation.hand.size(); ++index) {
            if (index != 0) {
                output_ << ',';
            }
            output_ << "{\"index\":" << index
                    << ",\"card\":";
            write_card(output_, observation.hand[index]);
            output_ << '}';
        }
        output_ << "]}}\n" << std::flush;

        const auto selected = parse_strict_unsigned_array_field(
            read_response(id), "indices");
        if (selected.size() != excess) {
            throw std::invalid_argument(
                "cleanup response must select exactly the excess");
        }
        if (!unique_values(selected)) {
            throw std::invalid_argument(
                "cleanup response contains duplicate positions");
        }
        std::vector<std::size_t> result;
        result.reserve(selected.size());
        for (const std::uint64_t position : selected) {
            if (position >= observation.hand.size()) {
                throw std::invalid_argument(
                    "cleanup response selects an illegal position");
            }
            result.push_back(
                static_cast<std::size_t>(position));
        }
        return result;
    }

    bool choose_mulligan(const PlayerObservation& observation) {
        const std::uint64_t id = next_decision_id();
        output_ << "{\"type\":\"decision\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"decision\":{\"id\":" << id
                << ",\"kind\":\"mulligan\",\"handSize\":"
                << observation.hand.size() << ",\"options\":["
                << "{\"index\":0,\"label\":\"Keep this hand\"},"
                << "{\"index\":1,\"label\":\"Mulligan to "
                << (observation.hand.size() - 1)
                << "\"}]}}\n"
                << std::flush;
        const auto selected = parse_unsigned_field(
            read_response(id), "index");
        if (selected > 1) {
            throw std::invalid_argument(
                "mulligan response must choose option 0 or 1");
        }
        return selected == 1;
    }

    static std::string combat_creature_label(
        const PlayerObservation& observation, PermanentId id) {
        for (const auto& player : observation.players) {
            for (const auto& creature : player.creatures) {
                if (creature.id == id) {
                    return std::string(
                               card_definition(creature.card).name) +
                           " #" + std::to_string(id);
                }
            }
        }
        return "creature #" + std::to_string(id);
    }

    std::string event_label(
        const PlayerObservation& observation,
        const GameEvent& event) const {
        const std::string actor =
            event.player == observation.observer
                ? "You"
                : "Opponent";
        switch (event.kind) {
        case GameEventKind::TurnStarted:
            return actor + " started turn " +
                   std::to_string(observation.turn_number);
        case GameEventKind::PriorityActionSelected:
            if (!event.priority_action.has_value()) {
                return actor + " acted";
            }
            return actor + ": " +
                   action_label(
                       observation, *event.priority_action);
        case GameEventKind::StackObjectResolved:
            if (!event.stack_object.has_value()) {
                return "The top object resolved";
            }
            return "Resolved " +
                   stack_label(
                       observation, *event.stack_object);
        case GameEventKind::AttackersDeclared: {
            if (event.attackers.empty()) {
                return actor + " declared no attackers";
            }
            std::string text = actor + " attacked with ";
            for (std::size_t i = 0; i < event.attackers.size(); ++i) {
                if (i != 0) {
                    text += ", ";
                }
                text += combat_creature_label(observation,
                                              event.attackers[i]);
            }
            return text;
        }
        case GameEventKind::BlockersDeclared: {
            if (event.blocks.empty()) {
                return actor + " declared no blockers";
            }
            std::string text = actor + " blocked ";
            for (std::size_t i = 0; i < event.blocks.size(); ++i) {
                if (i != 0) {
                    text += "; ";
                }
                text += combat_creature_label(observation,
                                              event.blocks[i].first) +
                        " with " +
                        combat_creature_label(observation,
                                              event.blocks[i].second);
            }
            return text;
        }
        case GameEventKind::DamageOrderChosen:
            return actor + " ordered combat damage";
        case GameEventKind::CombatResolved:
            return "Combat damage resolved";
        case GameEventKind::CardsDiscarded:
            return actor + " discarded " +
                   std::to_string(event.cards.size()) +
                   (event.cards.size() == 1
                        ? " card"
                        : " cards");
        case GameEventKind::MulliganTaken:
            return actor + " took a mulligan";
        }
        throw std::logic_error("unknown event");
    }

    void observe(const PlayerObservation& observation,
                 const GameEvent& event) {
        phase_ = event.phase;
        output_ << "{\"type\":\"event\",\"state\":";
        write_state(output_, observation, phase_);
        output_ << ",\"event\":{\"kind\":";
        write_json_string(output_, event_kind_name(event.kind));
        output_ << ",\"player\":" << event.player
                << ",\"phase\":";
        write_json_string(output_, phase_name(event.phase));
        if (event.kind ==
                GameEventKind::PriorityActionSelected &&
            event.priority_action.has_value()) {
            output_ << ",\"actionKind\":";
            write_json_string(
                output_,
                action_kind_name(
                    event.priority_action->kind));
        }
        output_ << ",\"label\":";
        write_json_string(
            output_, event_label(observation, event));
        if (event.kind == GameEventKind::CardsDiscarded) {
            output_ << ",\"cards\":";
            write_cards(output_, event.cards);
        }
        // Combat structure for client visuals: attacker ids and
        // (attacker, blocker) pairs accompany every combat event.
        if (event.kind == GameEventKind::AttackersDeclared ||
            event.kind == GameEventKind::BlockersDeclared ||
            event.kind == GameEventKind::DamageOrderChosen ||
            event.kind == GameEventKind::CombatResolved) {
            output_ << ",\"attackers\":[";
            for (std::size_t index = 0;
                 index < event.attackers.size(); ++index) {
                output_ << (index == 0 ? "" : ",")
                        << event.attackers[index];
            }
            output_ << "],\"blocks\":[";
            for (std::size_t index = 0; index < event.blocks.size();
                 ++index) {
                output_ << (index == 0 ? "" : ",") << '['
                        << event.blocks[index].first << ','
                        << event.blocks[index].second << ']';
            }
            output_ << ']';
        }
        output_ << "}}\n" << std::flush;
    }

    std::istream& input_;
    std::ostream& output_;
    bool reveal_opponent_hand_ = false;
    bool bluff_mode_ = false;
    std::uint64_t decision_id_ = 0;
    TurnPhase phase_ = TurnPhase::FirstMain;
};

} // namespace

DeckId parse_deck_id(std::string_view value) {
    if (value == "green") {
        return DeckId::Green;
    }
    if (value == "red") {
        return DeckId::Red;
    }
    if (value == "blue") {
        return DeckId::Blue;
    }
    if (value == "white") {
        return DeckId::White;
    }
    if (value == "ru-aggro" || value == "ru") {
        return DeckId::RUAggro;
    }
    if (value == "robots") {
        return DeckId::Robots;
    }
    if (value == "white-weenie" || value == "ww") {
        return DeckId::WhiteWeenie;
    }
    if (value == "br-midrange" || value == "br") {
        return DeckId::BRMidrange;
    }
    if (value == "lotus-combo" || value == "lotus") {
        return DeckId::LotusCombo;
    }
    if (value == "burn") {
        return DeckId::Burn;
    }
    if (value == "uwr" || value == "uwr-aggro") {
        return DeckId::UWR;
    }
    throw std::invalid_argument(
        "unknown deck: " + std::string(value));
}

EvolutionPilot parse_evolution_pilot(std::string_view value) {
    if (value == "random") {
        return EvolutionPilot::Random;
    }
    if (value == "monte-carlo" || value == "mc") {
        return EvolutionPilot::MonteCarlo;
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return EvolutionPilot::DeepMonteCarlo;
    }
    if (value == "handcrafted") {
        return EvolutionPilot::Handcrafted;
    }
    if (value == "spz" || value == "self-play-zero") {
        return EvolutionPilot::SelfPlayZero;
    }
    throw std::invalid_argument(
        "unknown evolution pilot: " + std::string(value));
}

std::vector<CardId> parse_exact_deck_cards(
    std::string_view value) {
    std::vector<CardId> cards;
    std::size_t cursor = 0;
    while (cursor < value.size()) {
        const std::size_t comma = value.find(',', cursor);
        const std::size_t end =
            comma == std::string_view::npos
                ? value.size()
                : comma;
        const std::string_view token =
            value.substr(cursor, end - cursor);
        std::uint64_t parsed = 0;
        const auto result = std::from_chars(
            token.data(), token.data() + token.size(), parsed);
        if (token.empty() || result.ec != std::errc{} ||
            result.ptr != token.data() + token.size()) {
            throw std::invalid_argument(
                "custom deck requires comma-separated decimal "
                "card IDs");
        }
        if (parsed >= kCardCount) {
            throw std::invalid_argument(
                "custom deck contains an unknown card ID");
        }
        cards.push_back(static_cast<CardId>(parsed));
        if (comma == std::string_view::npos) {
            cursor = value.size();
        } else {
            cursor = comma + 1;
            if (cursor == value.size()) {
                throw std::invalid_argument(
                    "custom deck requires comma-separated decimal "
                    "card IDs");
            }
        }
    }
    validate_exact_deck_cards(cards, "custom deck");
    return cards;
}

void write_evolution_json(
    std::ostream& output,
    const DeckEvolutionSummary& summary,
    const EvolutionJsonConfig& config) {
    validate_exact_deck_cards(
        summary.best.cards, "evolved deck");
    if (summary.generation_best_win_rates.size() !=
        config.generations) {
        throw std::invalid_argument(
            "evolution result generation trace length does not "
            "match its configuration");
    }
    const auto validate_stats =
        [](const DeckSimulationStats& stats) {
            if (stats.wins + stats.losses + stats.draws !=
                stats.games) {
                throw std::invalid_argument(
                    "evolution result contains inconsistent stats");
            }
        };
    validate_stats(summary.best.total);
    DeckSimulationStats opponent_total;
    for (const auto& stats : summary.best.by_opponent) {
        validate_stats(stats);
        opponent_total.games += stats.games;
        opponent_total.wins += stats.wins;
        opponent_total.losses += stats.losses;
        opponent_total.draws += stats.draws;
    }
    if (opponent_total.games != summary.best.total.games ||
        opponent_total.wins != summary.best.total.wins ||
        opponent_total.losses != summary.best.total.losses ||
        opponent_total.draws != summary.best.total.draws) {
        throw std::invalid_argument(
            "evolution result aggregate stats do not equal the "
            "sum of opponent rows");
    }

    output << "{\"type\":\"evolution_result\","
              "\"schemaVersion\":1,\"seed\":";
    write_json_string(output, std::to_string(config.seed));
    output << ",\"pilot\":";
    write_json_string(output, evolution_pilot_token(config.pilot));
    output << ",\"parameters\":{\"generations\":"
           << config.generations
           << ",\"population\":" << config.population
           << ",\"gamesPerOpponent\":"
           << config.games_per_opponent
           << "},\"deck\":{\"size\":40,\"cardIds\":[";
    for (std::size_t index = 0;
         index < summary.best.cards.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << static_cast<unsigned int>(
            summary.best.cards[index]);
    }
    output << "],\"cards\":[";
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : summary.best.cards) {
        ++counts[static_cast<std::size_t>(card)];
    }
    bool wrote_card = false;
    for (std::size_t card = 0; card < counts.size(); ++card) {
        if (counts[card] == 0) {
            continue;
        }
        if (wrote_card) {
            output << ',';
        }
        wrote_card = true;
        const auto id = static_cast<CardId>(card);
        output << "{\"id\":" << card << ",\"name\":";
        write_json_string(output, card_definition(id).name);
        output << ",\"count\":" << counts[card] << '}';
    }
    output << "]},\"fitness\":";
    write_evolution_stats(output, summary.best.total);
    output << ",\"byOpponent\":[";
    for (std::size_t opponent = 0;
         opponent < summary.best.by_opponent.size();
         ++opponent) {
        if (opponent != 0) {
            output << ',';
        }
        const auto deck = static_cast<DeckId>(opponent);
        output << "{\"deck\":";
        write_json_string(output, deck_id_token(deck));
        output << ",\"name\":";
        write_json_string(output, deck_name(deck));
        output << ",\"fitness\":";
        write_evolution_stats(
            output, summary.best.by_opponent[opponent]);
        output << '}';
    }
    output << "],\"top\":[";
    for (std::size_t rank = 0; rank < summary.top.size(); ++rank) {
        const EvolvedDeck& entry = summary.top[rank];
        validate_exact_deck_cards(entry.cards, "top evolved deck");
        validate_stats(entry.total);
        if (rank != 0) {
            output << ',';
        }
        output << "{\"cards\":[";
        std::array<std::size_t, kCardCount> entry_counts{};
        for (const CardId card : entry.cards) {
            ++entry_counts[static_cast<std::size_t>(card)];
        }
        bool wrote_entry_card = false;
        for (std::size_t card = 0; card < entry_counts.size();
             ++card) {
            if (entry_counts[card] == 0) {
                continue;
            }
            if (wrote_entry_card) {
                output << ',';
            }
            wrote_entry_card = true;
            const auto id = static_cast<CardId>(card);
            output << "{\"id\":" << card << ",\"name\":";
            write_json_string(output, card_definition(id).name);
            output << ",\"count\":" << entry_counts[card] << '}';
        }
        output << "],\"fitness\":";
        write_evolution_stats(output, entry.total);
        output << '}';
    }
    output << "],\"generationBestWinRates\":[";
    for (std::size_t generation = 0;
         generation <
         summary.generation_best_win_rates.size();
         ++generation) {
        if (generation != 0) {
            output << ',';
        }
        write_json_real(
            output,
            summary.generation_best_win_rates[generation]);
    }
    output << "]}\n";
}

int run_evolution_json(
    std::ostream& output,
    const EvolutionJsonConfig& config) {
    if (config.generations == 0 ||
        config.games_per_opponent == 0) {
        throw std::invalid_argument(
            "evolution generations and games must be positive");
    }
    if (config.population < kDeckCount) {
        throw std::invalid_argument(
            "evolution population must cover the metagame deck count");
    }

    DeckEvolutionConfig evolution = {
        .generations = config.generations,
        .population = config.population,
        .repetitions_per_opponent = config.games_per_opponent,
    };
    switch (config.pilot) {
        case EvolutionPilot::Random:
            evolution.pilot = {.kind = BotKind::Random,
                               .rollouts_per_action = 1};
            break;
        case EvolutionPilot::MonteCarlo:
            evolution.pilot = {
                .kind = BotKind::MonteCarlo,
                .rollouts_per_action = config.monte_carlo_rollouts,
            };
            break;
        case EvolutionPilot::DeepMonteCarlo:
            evolution.pilot = {
                .kind = BotKind::DeepMonteCarlo,
                .rollouts_per_action =
                    config.deep_monte_carlo_rollouts,
            };
            break;
        case EvolutionPilot::Handcrafted:
            evolution.pilot = {.kind = BotKind::Handcrafted,
                               .rollouts_per_action = 1};
            break;
        case EvolutionPilot::SelfPlayZero: {
            // Both seats are driven by the frozen champion with a fast
            // myopic policy; fitness games stay cheap and
            // deterministic. Mid-retrain (no champion on disk) the
            // evolution degrades to the handcrafted pilot instead of
            // dying, mirroring the game-mode fallback.
            if (!std::ifstream(config.spz_artifact_path).good()) {
                evolution.pilot = {.kind = BotKind::Handcrafted,
                                   .rollouts_per_action = 1};
                break;
            }
            const auto net =
                std::make_shared<const selfplay_zero::SpzNet>(
                    selfplay_zero::load_spz_net(
                        config.spz_artifact_path));
            evolution.controller_pilot =
                [net](const std::array<std::vector<CardId>, 2>& decks,
                      std::uint64_t game_seed) {
                    std::array<std::optional<HumanController>, 2>
                        controllers;
                    for (std::size_t seat = 0; seat < 2; ++seat) {
                        selfplay_zero::SpzPolicyConfig policy;
                        policy.worlds = 1;
                        policy.block_prediction_worlds = 1;
                        policy.rollout = false;
                        policy.seed = game_seed;
                        controllers[seat] =
                            selfplay_zero::make_spz_controller(
                                net, decks, seat, policy);
                    }
                    return controllers;
                };
            break;
        }
    }
    GameConfig game_config;

    const DeckEvolutionSummary summary = evolve_deck(
        std::move(evolution), config.seed, std::move(game_config));
    write_evolution_json(output, summary, config);
    return 0;
}

BotKind parse_opponent_bot(std::string_view value) {
    if (value == "random") {
        return BotKind::Random;
    }
    if (value == "monte-carlo" || value == "mc") {
        return BotKind::MonteCarlo;
    }
    if (value == "deep-monte-carlo" || value == "deep-mc") {
        return BotKind::DeepMonteCarlo;
    }
    if (value == "handcrafted" ||
        value == "handcoded-policy") {
        return BotKind::Handcrafted;
    }
    throw std::invalid_argument(
        "unknown opponent policy: " + std::string(value));
}

BotConfig make_opponent_bot_config(const BridgeConfig& config) {
    return {
        .kind = config.opponent_bot,
        .rollouts_per_action =
            config.opponent_bot == BotKind::MonteCarlo
                ? config.monte_carlo_rollouts
                : config.opponent_bot == BotKind::DeepMonteCarlo
                      ? config.deep_monte_carlo_rollouts
                      : 1,
    };
}

int run_bridge_session(std::istream& input, std::ostream& output,
                       const BridgeConfig& config) {
    if (config.monte_carlo_rollouts == 0 ||
        config.deep_monte_carlo_rollouts == 0) {
        throw std::invalid_argument(
            "rollout budgets must be positive");
    }
    const std::vector<CardId> human_cards =
        configured_deck_cards(
            config.human_deck, config.human_deck_cards,
            "--human-deck-cards");
    const std::vector<CardId> opponent_cards =
        configured_deck_cards(
            config.opponent_deck, config.opponent_deck_cards,
            "--opponent-deck-cards");

    output << "{\"type\":\"status\",\"message\":";
    write_json_string(output, "Preparing the battlefield");
    output << "}\n" << std::flush;

    GameConfig game_config;
    game_config.bots[0] = {
        .kind = BotKind::Random,
        .rollouts_per_action = 1,
    };
    game_config.bots[1] = make_opponent_bot_config(config);

    JsonController controller(
        input, output, config.reveal_opponent_hand,
        config.bluff_mode);
    game_config.human_controllers[0] =
        controller.controller();

    bool opponent_spz = config.opponent_spz;
    if (opponent_spz &&
        !std::ifstream(config.spz_artifact_path).good()) {
        // No champion on disk (mid-retrain window): degrade to the
        // handcrafted opponent instead of dying on load.
        opponent_spz = false;
        game_config.bots[1] = {
            .kind = BotKind::Handcrafted,
            .rollouts_per_action = 1,
        };
        write_model_status(
            output,
            "Champion artifact unavailable; handcrafted opponent",
            "handcrafted", 0, 1, 0, "fallback",
            "0000000000000000000000000000000000000000000000000000"
            "000000000000");
    }
    if (opponent_spz) {
        // Self-Play Zero drives the opponent seat through the same
        // perspective-safe controller interface a human uses; its engine
        // BotConfig is never consulted for decisions.
        const auto spz_net =
            std::make_shared<const selfplay_zero::SpzNet>(
                selfplay_zero::load_spz_net(config.spz_artifact_path));
        selfplay_zero::SpzPolicyConfig spz_policy;
        spz_policy.worlds = config.spz_worlds;
        spz_policy.block_prediction_worlds = config.spz_worlds;
        spz_policy.rollout = config.spz_rollout;
        // Discounted lookahead: sooner wins outrank later ones, so the
        // champion presses lethal from the objective itself.
        spz_policy.gamma_per_turn = 0.98;
        // When the learned advantage head ships beside the champion, it
        // carries the card-sized waste signal and the rules guardrails
        // retire.
        std::shared_ptr<const selfplay_zero::SpzAdvantageNet>
            spz_advantage;
        {
            const std::filesystem::path advantage_path =
                std::filesystem::path(config.spz_artifact_path)
                    .parent_path() /
                "spz-advantage.txt";
            std::ifstream probe(advantage_path);
            if (probe.good()) {
                spz_advantage = std::make_shared<
                    const selfplay_zero::SpzAdvantageNet>(
                    selfplay_zero::load_spz_advantage_net(
                        advantage_path.string()));
                // Exact dominance filtering stays on even with the
                // learned head: the head arbitrates ties among
                // non-dominated actions, but a provably no-upside
                // action (an X=0 self-Disintegrate) should never
                // survive on rollout noise. Live regression at seed
                // 4148072829 without it.
            }
        }
        spz_policy.seed = config.game_seed ^ 0x53505AULL;
        game_config.human_controllers[1] =
            selfplay_zero::make_spz_controller(
                spz_net, {human_cards, opponent_cards}, 1, spz_policy,
                nullptr, nullptr, spz_advantage);
        std::ifstream spz_bytes(config.spz_artifact_path,
                                std::ios::binary);
        std::ostringstream spz_contents;
        spz_contents << spz_bytes.rdbuf();
        const std::string spz_fingerprint =
            artifact_integrity::sha256_string(spz_contents.str());
        write_model_status(
            output, "Self-Play Zero champion loaded", "self-play-zero",
            0, config.spz_worlds,
            config.spz_rollout ? 1 : 0, "frozen-artifact",
            spz_fingerprint);
    }

    Game game(human_cards, opponent_cards,
              config.game_seed, game_config);
    const GameResult result = game.run();
    PlayerObservation final_observation =
        observe_game_state(game.state(), 0);
    if (config.reveal_opponent_hand) {
        final_observation.revealed_opponent_hand =
            game.state().players[1].hand;
    }
    controller.emit_game_over(final_observation, result);
    return 0;
}

} // namespace old_school::web
