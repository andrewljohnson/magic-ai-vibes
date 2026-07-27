#include "old_school/fq0_dominance.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::fq0_dominance {
namespace {

bool valid_card(CardId card) {
    return static_cast<std::size_t>(card) < kCardCount;
}

bool valid_mana(const ManaCost& mana) {
    return mana.generic >= 0 && mana.green >= 0 &&
           mana.red >= 0 && mana.blue >= 0 &&
           mana.white >= 0;
}

std::size_t card_index(CardId card) {
    return static_cast<std::size_t>(card);
}

std::array<std::size_t, kCardCount> card_counts(
    const std::vector<CardId>& cards, bool& valid) {
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : cards) {
        if (!valid_card(card)) {
            valid = false;
            return {};
        }
        ++counts[card_index(card)];
    }
    return counts;
}

bool checked_add(std::size_t& destination,
                 std::size_t value) {
    if (destination >
        std::numeric_limits<std::size_t>::max() - value) {
        return false;
    }
    destination += value;
    return true;
}

bool remove_card_copies(std::vector<CardId>& cards,
                        CardId card, std::size_t count) {
    while (count > 0) {
        const auto found =
            std::find(cards.rbegin(), cards.rend(), card);
        if (found == cards.rend()) {
            return false;
        }
        cards.erase(std::next(found).base());
        --count;
    }
    return true;
}

std::optional<CanonicalPlayerResourceCost>
canonical_cost(const GameState& root, std::size_t player,
               const PlayerResourceCost& cost) {
    if (player >= root.players.size() ||
        !valid_mana(cost.mana_depleted)) {
        return std::nullopt;
    }

    CanonicalPlayerResourceCost canonical{
        .hand_cards_consumed =
            cost.hand_cards_consumed,
        .mana_depleted = cost.mana_depleted,
        .land_play_entitlement_consumed =
            cost.land_play_entitlement_consumed,
    };
    bool hand_valid = true;
    const auto root_hand_counts =
        card_counts(root.players[player].hand, hand_valid);
    if (!hand_valid) {
        return std::nullopt;
    }
    for (std::size_t card = 0; card < kCardCount; ++card) {
        if (cost.hand_cards_consumed[card] >
            root_hand_counts[card]) {
            return std::nullopt;
        }
    }
    std::vector<ResourceSource> seen;
    seen.reserve(
        cost.preexisting_sources_newly_tapped.size());
    const PlayerState& root_player = root.players[player];
    for (const ResourceSource& source :
         cost.preexisting_sources_newly_tapped) {
        if (!valid_card(source.card) ||
            std::find(seen.begin(), seen.end(), source) !=
                seen.end()) {
            return std::nullopt;
        }
        seen.push_back(source);
        if (source.kind == SourceKind::Land) {
            const std::size_t index =
                static_cast<std::size_t>(source.key);
            if (index >= root_player.lands.size() ||
                root_player.lands[index].card != source.card ||
                root_player.lands[index].tapped ||
                !checked_add(
                    canonical.lands_newly_tapped[
                        card_index(source.card)],
                    1)) {
                return std::nullopt;
            }
            continue;
        }
        if (source.kind != SourceKind::Artifact) {
            return std::nullopt;
        }
        const auto artifact = std::find_if(
            root_player.artifacts.begin(),
            root_player.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == source.key &&
                       candidate.card == source.card;
            });
        if (artifact == root_player.artifacts.end() ||
            artifact->tapped ||
            !checked_add(
                canonical.artifacts_newly_tapped[
                    card_index(source.card)],
                1)) {
            return std::nullopt;
        }
    }
    if (cost.land_play_entitlement_consumed &&
        root_player.land_played_this_turn) {
        return std::nullopt;
    }
    return canonical;
}

bool restore_consumed_cards(
    const PlayerState& root, PlayerState& boundary,
    const CanonicalPlayerResourceCost& cost) {
    bool valid = true;
    const auto root_graveyard =
        card_counts(root.graveyard, valid);
    const auto root_exile = card_counts(root.exile, valid);
    const auto boundary_graveyard =
        card_counts(boundary.graveyard, valid);
    const auto boundary_exile =
        card_counts(boundary.exile, valid);
    if (!valid) {
        return false;
    }

    for (std::size_t index = 0; index < kCardCount;
         ++index) {
        const std::size_t consumed =
            cost.hand_cards_consumed[index];
        if (consumed == 0) {
            continue;
        }
        // A decrease in either public destination or more excess copies
        // than the ledger accounts for makes correspondence ambiguous.
        if (boundary_graveyard[index] <
                root_graveyard[index] ||
            boundary_exile[index] < root_exile[index]) {
            return false;
        }
        const std::size_t graveyard_excess =
            boundary_graveyard[index] -
            root_graveyard[index];
        const std::size_t exile_excess =
            boundary_exile[index] - root_exile[index];
        if (graveyard_excess >
                std::numeric_limits<std::size_t>::max() -
                    exile_excess ||
            graveyard_excess + exile_excess != consumed ||
            boundary.hand.size() >
                std::numeric_limits<std::size_t>::max() -
                    consumed) {
            return false;
        }
        const CardId card = static_cast<CardId>(index);
        if (!remove_card_copies(
                boundary.graveyard, card,
                graveyard_excess) ||
            !remove_card_copies(
                boundary.exile, card, exile_excess)) {
            return false;
        }
        boundary.hand.insert(
            boundary.hand.end(), consumed, card);
    }
    return true;
}

bool restore_land_sources(
    const PlayerState& root, PlayerState& boundary,
    const PlayerResourceCost& cost) {
    std::vector<std::size_t> restored;
    restored.reserve(
        cost.preexisting_sources_newly_tapped.size());
    for (const ResourceSource& source :
         cost.preexisting_sources_newly_tapped) {
        if (source.kind != SourceKind::Land) {
            continue;
        }
        const std::size_t root_count =
            static_cast<std::size_t>(std::count_if(
                root.lands.begin(), root.lands.end(),
                [&](const LandPermanent& land) {
                    return land.card == source.card;
                }));
        const std::size_t boundary_count =
            static_cast<std::size_t>(std::count_if(
                boundary.lands.begin(),
                boundary.lands.end(),
                [&](const LandPermanent& land) {
                    return land.card == source.card;
                }));
        if (boundary_count < root_count) {
            return false;
        }

        const auto available =
            [&](std::size_t index, bool require_tapped) {
                return index < boundary.lands.size() &&
                       boundary.lands[index].card ==
                           source.card &&
                       (!require_tapped ||
                        boundary.lands[index].tapped) &&
                       std::find(
                           restored.begin(), restored.end(),
                           index) == restored.end();
            };
        std::optional<std::size_t> selected;
        for (std::size_t index = 0;
             index < boundary.lands.size(); ++index) {
            if (available(index, true)) {
                selected = index;
                break;
            }
        }
        if (!selected.has_value()) {
            for (std::size_t index = 0;
                 index < boundary.lands.size(); ++index) {
                if (available(index, false)) {
                    selected = index;
                    break;
                }
            }
        }
        if (!selected.has_value()) {
            return false;
        }
        boundary.lands[*selected].tapped = false;
        restored.push_back(*selected);
    }
    return true;
}

bool restore_artifact_sources(
    const PlayerState& root, PlayerState& boundary,
    const PlayerResourceCost& cost) {
    for (const ResourceSource& source :
         cost.preexisting_sources_newly_tapped) {
        if (source.kind != SourceKind::Artifact) {
            continue;
        }
        const auto root_artifact = std::find_if(
            root.artifacts.begin(), root.artifacts.end(),
            [&](const ArtifactPermanent& artifact) {
                return artifact.id == source.key &&
                       artifact.card == source.card;
            });
        const auto boundary_artifact = std::find_if(
            boundary.artifacts.begin(),
            boundary.artifacts.end(),
            [&](const ArtifactPermanent& artifact) {
                return artifact.id == source.key &&
                       artifact.card == source.card;
            });
        if (root_artifact == root.artifacts.end() ||
            boundary_artifact == boundary.artifacts.end()) {
            return false;
        }
        boundary_artifact->tapped = root_artifact->tapped;
    }
    return true;
}

class ByteWriter {
  public:
    void byte(std::uint8_t value) {
        data_.push_back(static_cast<char>(value));
    }

    void boolean(bool value) {
        byte(value ? 1U : 0U);
    }

    void integer(std::uint64_t value) {
        for (std::size_t index = 0;
             index < sizeof(value); ++index) {
            byte(static_cast<std::uint8_t>(
                value >> (index * 8U)));
        }
    }

    void signed_integer(int value) {
        integer(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)));
    }

    void text(const char* value) {
        const std::string text_value(value);
        integer(text_value.size());
        data_.append(text_value);
    }

    const std::string& data() const {
        return data_;
    }

  private:
    std::string data_;
};

void append_card(ByteWriter& writer, CardId card,
                 bool& valid) {
    if (!valid_card(card)) {
        valid = false;
        return;
    }
    writer.integer(card_index(card));
}

void append_cards(ByteWriter& writer,
                  std::vector<CardId> cards,
                  bool& valid) {
    std::sort(cards.begin(), cards.end());
    writer.integer(cards.size());
    for (const CardId card : cards) {
        append_card(writer, card, valid);
    }
}

void append_mana(ByteWriter& writer,
                 const ManaCost& mana, bool& valid) {
    if (!valid_mana(mana)) {
        valid = false;
        return;
    }
    writer.signed_integer(mana.generic);
    writer.signed_integer(mana.green);
    writer.signed_integer(mana.red);
    writer.signed_integer(mana.blue);
    writer.signed_integer(mana.white);
}

std::string owner_observable_projection(
    const Settlement& settlement, const GameState& state,
    std::size_t observer, bool& valid) {
    if (observer >= state.players.size() ||
        state.active_player >= state.players.size() ||
        state.starting_player >= state.players.size() ||
        !state.stack.empty()) {
        valid = false;
        return {};
    }
    if (settlement.terminal) {
        if (settlement.terminal_winner < -1 ||
            settlement.terminal_winner > 1 ||
            settlement.boundary_context.valid) {
            valid = false;
            return {};
        }
    } else if (
        settlement.terminal_winner != -2 ||
        !settlement.boundary_context.valid ||
        settlement.boundary_context.phase !=
            TurnPhase::FirstMain ||
        settlement.boundary_context.decision_player !=
            state.active_player ||
        settlement.boundary_context.consecutive_passes != 0 ||
        !settlement.boundary_context.sorcery_actions) {
        valid = false;
        return {};
    }

    ByteWriter writer;
    writer.text("old-school-fq0-dominance-consequence-v1");
    writer.integer(observer);
    writer.boolean(settlement.terminal);
    writer.signed_integer(settlement.terminal_winner);
    writer.boolean(settlement.boundary_context.valid);
    writer.integer(static_cast<std::uint64_t>(
        settlement.boundary_context.phase));
    writer.integer(
        settlement.boundary_context.decision_player);
    writer.signed_integer(
        settlement.boundary_context.consecutive_passes);
    writer.boolean(
        settlement.boundary_context.sorcery_actions);

    for (std::size_t player = 0;
         player < state.players.size(); ++player) {
        const PlayerState& source = state.players[player];
        writer.signed_integer(source.life);
        writer.integer(source.library.size());
        writer.integer(source.hand.size());
        if (player == observer) {
            append_cards(writer, source.hand, valid);
        }
        append_cards(writer, source.graveyard, valid);
        append_cards(writer, source.exile, valid);

        std::vector<LandPermanent> lands = source.lands;
        std::sort(
            lands.begin(), lands.end(),
            [](const LandPermanent& first,
               const LandPermanent& second) {
                return std::tie(first.card, first.tapped) <
                       std::tie(second.card, second.tapped);
            });
        writer.integer(lands.size());
        for (const LandPermanent& land : lands) {
            append_card(writer, land.card, valid);
            writer.boolean(land.tapped);
        }

        std::vector<CreaturePermanent> creatures =
            source.creatures;
        std::sort(
            creatures.begin(), creatures.end(),
            [](const CreaturePermanent& first,
               const CreaturePermanent& second) {
                return std::tie(
                           first.card, first.tapped,
                           first.summoning_sick, first.damage,
                           first.temporary_power_bonus,
                           first.temporary_toughness_bonus,
                           first.exile_on_death_this_turn) <
                       std::tie(
                           second.card, second.tapped,
                           second.summoning_sick, second.damage,
                           second.temporary_power_bonus,
                           second.temporary_toughness_bonus,
                           second.exile_on_death_this_turn);
            });
        writer.integer(creatures.size());
        for (const CreaturePermanent& creature : creatures) {
            append_card(writer, creature.card, valid);
            writer.boolean(creature.tapped);
            writer.boolean(creature.summoning_sick);
            writer.signed_integer(creature.damage);
            writer.signed_integer(
                creature.temporary_power_bonus);
            writer.signed_integer(
                creature.temporary_toughness_bonus);
            writer.boolean(
                creature.exile_on_death_this_turn);
        }

        std::vector<ArtifactPermanent> artifacts =
            source.artifacts;
        std::sort(
            artifacts.begin(), artifacts.end(),
            [](const ArtifactPermanent& first,
               const ArtifactPermanent& second) {
                return std::tie(first.card, first.tapped) <
                       std::tie(second.card, second.tapped);
            });
        writer.integer(artifacts.size());
        for (const ArtifactPermanent& artifact : artifacts) {
            append_card(writer, artifact.card, valid);
            writer.boolean(artifact.tapped);
        }
        append_cards(writer, source.enchantments, valid);
        append_mana(writer, source.mana_pool, valid);
        writer.boolean(source.land_played_this_turn);
    }

    for (const std::size_t turns :
         state.extra_turns_pending) {
        writer.integer(turns);
    }
    for (const bool failed : state.failed_draw) {
        writer.boolean(failed);
    }
    writer.integer(state.active_player);
    writer.integer(state.starting_player);
    writer.integer(state.turn_number);
    return valid ? writer.data() : std::string{};
}

bool mana_leq(const ManaCost& first,
              const ManaCost& second) {
    return first.generic <= second.generic &&
           first.green <= second.green &&
           first.red <= second.red &&
           first.blue <= second.blue &&
           first.white <= second.white;
}

template <std::size_t Size>
bool counts_leq(const std::array<std::size_t, Size>& first,
                const std::array<std::size_t, Size>& second) {
    for (std::size_t index = 0; index < Size; ++index) {
        if (first[index] > second[index]) {
            return false;
        }
    }
    return true;
}

bool cost_leq(const CanonicalPlayerResourceCost& first,
              const CanonicalPlayerResourceCost& second) {
    return counts_leq(
               first.hand_cards_consumed,
               second.hand_cards_consumed) &&
           mana_leq(first.mana_depleted,
                    second.mana_depleted) &&
           counts_leq(first.lands_newly_tapped,
                      second.lands_newly_tapped) &&
           counts_leq(first.artifacts_newly_tapped,
                      second.artifacts_newly_tapped) &&
           (!first.land_play_entitlement_consumed ||
            second.land_play_entitlement_consumed);
}

} // namespace

CanonicalSettlement canonicalize_settlement(
    const Settlement& settlement, std::size_t observer) {
    CanonicalSettlement result;
    if (!settlement.complete ||
        settlement.unresolved_transient_choice_effect ||
        observer >= settlement.root_state.players.size() ||
        observer >= settlement.boundary_state.players.size()) {
        return result;
    }

    GameState normalized = settlement.boundary_state;
    for (std::size_t player = 0;
         player < normalized.players.size(); ++player) {
        const auto cost = canonical_cost(
            settlement.root_state, player,
            settlement.costs[player]);
        if (!cost.has_value()) {
            return result;
        }
        result.costs[player] = *cost;
        if (!restore_consumed_cards(
                settlement.root_state.players[player],
                normalized.players[player], *cost) ||
            !restore_land_sources(
                settlement.root_state.players[player],
                normalized.players[player],
                settlement.costs[player]) ||
            !restore_artifact_sources(
                settlement.root_state.players[player],
                normalized.players[player],
                settlement.costs[player])) {
            return result;
        }
        if (cost->land_play_entitlement_consumed) {
            normalized.players[player]
                .land_played_this_turn =
                settlement.root_state.players[player]
                    .land_played_this_turn;
        }
    }

    bool valid = true;
    result.owner_observable_consequence =
        owner_observable_projection(
            settlement, normalized, observer, valid);
    result.valid = valid;
    if (!valid) {
        result.owner_observable_consequence.clear();
    }
    return result;
}

Comparison compare(const Settlement& first,
                   const Settlement& second,
                   std::size_t actor) {
    Comparison result;
    result.root_information_equal =
        !first.root_information_fingerprint.empty() &&
        first.root_information_fingerprint ==
            second.root_information_fingerprint;
    result.first = canonicalize_settlement(first, actor);
    result.second = canonicalize_settlement(second, actor);
    result.first_normalized = result.first.valid;
    result.second_normalized = result.second.valid;
    result.consequences_equal =
        result.first.valid && result.second.valid &&
        result.first.owner_observable_consequence ==
            result.second.owner_observable_consequence;
    if (!result.root_information_equal ||
        !result.consequences_equal ||
        actor >= first.root_state.players.size()) {
        return result;
    }

    const std::size_t opponent = 1 - actor;
    const bool first_dominates =
        cost_leq(result.first.costs[actor],
                 result.second.costs[actor]) &&
        cost_leq(result.second.costs[opponent],
                 result.first.costs[opponent]) &&
        (result.first.costs[actor] !=
             result.second.costs[actor] ||
         result.first.costs[opponent] !=
             result.second.costs[opponent]);
    const bool second_dominates =
        cost_leq(result.second.costs[actor],
                 result.first.costs[actor]) &&
        cost_leq(result.first.costs[opponent],
                 result.second.costs[opponent]) &&
        (result.first.costs[actor] !=
             result.second.costs[actor] ||
         result.first.costs[opponent] !=
             result.second.costs[opponent]);
    if (first_dominates == second_dominates) {
        return result;
    }
    result.orientation =
        first_dominates
            ? Orientation::FirstDominatesSecond
            : Orientation::SecondDominatesFirst;
    return result;
}

} // namespace old_school::fq0_dominance
