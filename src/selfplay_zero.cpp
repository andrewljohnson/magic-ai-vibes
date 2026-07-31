#include "old_school/selfplay_zero.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <istream>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace old_school::selfplay_zero {
namespace {

constexpr double kIllegalScore = -1e9;
constexpr double kImprovementMargin = 1e-9;

std::uint64_t mix_seed(std::uint64_t seed, std::uint64_t salt) {
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (salt + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

using CardCountArray = std::array<int, kCardCount>;

void add_card(CardCountArray& counts, CardId card) {
    counts[static_cast<std::size_t>(card)] += 1;
}

void subtract_card(CardCountArray& counts, CardId card) {
    counts[static_cast<std::size_t>(card)] -= 1;
}

CardCountArray count_cards(const std::vector<CardId>& cards) {
    CardCountArray counts{};
    for (const CardId card : cards) {
        add_card(counts, card);
    }
    return counts;
}

void append_counts(std::vector<float>& features,
                   const CardCountArray& counts) {
    for (const int count : counts) {
        features.push_back(
            static_cast<float>(std::max(count, 0)) / 4.0f);
    }
}

CardCountArray public_battlefield_counts(const PublicPlayerState& player) {
    CardCountArray counts{};
    for (const auto& land : player.lands) {
        add_card(counts, land.card);
    }
    for (const auto& creature : player.creatures) {
        add_card(counts, creature.card);
    }
    for (const auto& artifact : player.artifacts) {
        add_card(counts, artifact.card);
    }
    for (const CardId enchantment : player.enchantments) {
        add_card(counts, enchantment);
    }
    return counts;
}

void subtract_public_zones(CardCountArray& counts,
                           const PublicPlayerState& player) {
    const CardCountArray battlefield = public_battlefield_counts(player);
    for (std::size_t index = 0; index < kCardCount; ++index) {
        counts[index] -= battlefield[index];
    }
    for (const CardId card : player.graveyard) {
        subtract_card(counts, card);
    }
    for (const CardId card : player.exile) {
        subtract_card(counts, card);
    }
}

int mana_pool_total(const ManaCost& pool) {
    return pool.generic + pool.green + pool.red + pool.blue + pool.white;
}

void append_player_scalars(std::vector<float>& features,
                           const PublicPlayerState& player,
                           std::size_t hand_size) {
    const auto push = [&features](double value) {
        features.push_back(static_cast<float>(value));
    };
    push(player.life / 20.0);
    push(std::min(player.life, 8) / 8.0);
    push(static_cast<double>(hand_size) / 7.0);
    push(static_cast<double>(player.library_size) / 40.0);
    push(static_cast<double>(player.graveyard.size()) / 20.0);
    const std::size_t lands = player.lands.size();
    std::size_t untapped_lands = 0;
    for (const auto& land : player.lands) {
        untapped_lands += land.tapped ? 0 : 1;
    }
    push(static_cast<double>(lands) / 10.0);
    push(static_cast<double>(untapped_lands) / 10.0);
    int power = 0;
    int toughness = 0;
    int untapped_power = 0;
    int ready_power = 0;
    int flying_power = 0;
    int damage = 0;
    int bonus_power = 0;
    std::size_t untapped_creatures = 0;
    std::size_t ready_creatures = 0;
    for (const auto& creature : player.creatures) {
        const auto& definition = card_definition(creature.card);
        const int creature_power =
            definition.power + creature.temporary_power_bonus;
        power += creature_power;
        toughness +=
            definition.toughness + creature.temporary_toughness_bonus;
        damage += creature.damage;
        bonus_power += creature.temporary_power_bonus;
        if (!creature.tapped) {
            untapped_creatures += 1;
            untapped_power += creature_power;
        }
        if (!creature.tapped && !creature.summoning_sick) {
            ready_creatures += 1;
            ready_power += creature_power;
        }
        if (definition.flying) {
            flying_power += creature_power;
        }
    }
    push(static_cast<double>(player.creatures.size()) / 6.0);
    push(static_cast<double>(untapped_creatures) / 6.0);
    push(static_cast<double>(ready_creatures) / 6.0);
    push(power / 12.0);
    push(toughness / 12.0);
    push(untapped_power / 12.0);
    push(ready_power / 12.0);
    push(flying_power / 8.0);
    push(damage / 8.0);
    push(bonus_power / 6.0);
    std::size_t untapped_artifacts = 0;
    for (const auto& artifact : player.artifacts) {
        untapped_artifacts += artifact.tapped ? 0 : 1;
    }
    push(static_cast<double>(player.artifacts.size()) / 4.0);
    push(static_cast<double>(untapped_artifacts) / 4.0);
    push(static_cast<double>(player.enchantments.size()) / 2.0);
    push(mana_pool_total(player.mana_pool) / 4.0);
    push(player.land_played_this_turn ? 1.0 : 0.0);
    push(player.channel_active ? 1.0 : 0.0);
}

constexpr std::size_t kGlobalScalarCount = 12;
constexpr std::size_t kPlayerScalarCount = 23;
constexpr std::size_t kCardBlockCount = 11;

}  // namespace

const std::array<std::vector<CardId>, kSpzDeckCount>& spz_decks() {
    static const std::array<std::vector<CardId>, kSpzDeckCount> decks = {
        green_deck(), red_deck(), blue_deck(), white_control_deck(),
        ru_aggro_deck(), lotus_combo_deck(), burn_deck(),
    };
    return decks;
}

std::string_view spz_deck_name(std::size_t deck_index) {
    static constexpr std::array<std::string_view, kSpzDeckCount> names = {
        "Green", "Red", "Blue", "White", "RU Aggro", "Lotus Combo",
        "Burn",
    };
    return names.at(deck_index);
}

std::size_t spz_feature_count() {
    return kGlobalScalarCount + 2 * kPlayerScalarCount +
           kCardBlockCount * kCardCount;
}

std::vector<float> spz_features(
    const PlayerObservation& observation,
    const std::array<std::vector<CardId>, 2>& original_decks,
    TurnPhase phase) {
    const std::size_t me = observation.observer;
    const std::size_t opponent = 1 - me;
    const PublicPlayerState& my_public = observation.players[me];
    const PublicPlayerState& opponent_public = observation.players[opponent];

    std::vector<float> features;
    features.reserve(spz_feature_count());
    const auto push = [&features](double value) {
        features.push_back(static_cast<float>(value));
    };

    push(static_cast<double>(observation.turn_number) / 20.0);
    push(observation.active_player == me ? 1.0 : 0.0);
    for (std::size_t index = 0; index < kSpzPhaseCount; ++index) {
        push(static_cast<std::size_t>(phase) == index ? 1.0 : 0.0);
    }
    push(static_cast<double>(observation.stack.size()) / 3.0);
    push(static_cast<double>(observation.extra_turns_pending[me]));
    push(static_cast<double>(observation.extra_turns_pending[opponent]));

    append_player_scalars(features, my_public, observation.hand.size());
    append_player_scalars(features, opponent_public,
                          opponent_public.hand_size);

    // Stack spells are physical cards that must leave the hidden pools; the
    // extractor also exposes them directly as pending effects.
    CardCountArray my_stack{};
    CardCountArray opponent_stack{};
    CardCountArray my_stack_spells{};
    CardCountArray opponent_stack_spells{};
    for (const auto& object : observation.stack) {
        auto& exposure =
            object.controller == me ? my_stack : opponent_stack;
        add_card(exposure, object.card);
        if (object.kind == StackObjectKind::Spell) {
            auto& physical = object.controller == me
                                 ? my_stack_spells
                                 : opponent_stack_spells;
            add_card(physical, object.card);
        }
    }

    const CardCountArray my_hand = count_cards(observation.hand);

    // The observer's remaining library pool: its decklist minus every card
    // it can already account for.
    CardCountArray my_library = count_cards(original_decks[me]);
    subtract_public_zones(my_library, my_public);
    for (std::size_t index = 0; index < kCardCount; ++index) {
        my_library[index] -= my_hand[index] + my_stack_spells[index];
    }

    // The opponent's unseen pool (hand plus library combined).
    CardCountArray opponent_unseen = count_cards(original_decks[opponent]);
    subtract_public_zones(opponent_unseen, opponent_public);
    for (std::size_t index = 0; index < kCardCount; ++index) {
        opponent_unseen[index] -= opponent_stack_spells[index];
    }

    CardCountArray my_untapped_creatures{};
    for (const auto& creature : my_public.creatures) {
        if (!creature.tapped) {
            add_card(my_untapped_creatures, creature.card);
        }
    }
    CardCountArray opponent_untapped_creatures{};
    for (const auto& creature : opponent_public.creatures) {
        if (!creature.tapped) {
            add_card(opponent_untapped_creatures, creature.card);
        }
    }

    append_counts(features, my_hand);
    append_counts(features, public_battlefield_counts(my_public));
    append_counts(features, public_battlefield_counts(opponent_public));
    append_counts(features, my_untapped_creatures);
    append_counts(features, opponent_untapped_creatures);
    append_counts(features, count_cards(my_public.graveyard));
    append_counts(features, count_cards(opponent_public.graveyard));
    append_counts(features, my_library);
    append_counts(features, opponent_unseen);
    append_counts(features, my_stack);
    append_counts(features, opponent_stack);

    if (features.size() != spz_feature_count()) {
        throw std::logic_error("spz feature schema size mismatch");
    }
    return features;
}

namespace {

constexpr std::size_t kCreatureSlots = 8;
// Present flag + card one-hot + nine combat/state scalars.
constexpr std::size_t kCreatureSlotFeatures = kCardCount + 10;
constexpr std::size_t kStackSlots = 3;
// Present flag + card one-hot + controller/ability + five-way target class
// + target-card one-hot + X value.
constexpr std::size_t kStackSlotFeatures = 2 * kCardCount + 9;
constexpr std::size_t kCastabilityFeatures = kCardCount + 2;
constexpr std::size_t kRaceFeatures = 10;

int creature_current_power(const CreaturePermanent& creature) {
    return card_definition(creature.card).power +
           creature.temporary_power_bonus;
}

int creature_current_toughness(const CreaturePermanent& creature) {
    return card_definition(creature.card).toughness +
           creature.temporary_toughness_bonus;
}

void append_creature_slots_range(std::vector<float>& features,
                                 const std::vector<CreaturePermanent>& raw,
                                 std::size_t first_slot,
                                 std::size_t slot_count);

void append_creature_slots(std::vector<float>& features,
                           const std::vector<CreaturePermanent>& raw) {
    append_creature_slots_range(features, raw, 0, kCreatureSlots);
}

void append_creature_slots_range(std::vector<float>& features,
                                 const std::vector<CreaturePermanent>& raw,
                                 std::size_t first_slot,
                                 std::size_t slot_count) {
    std::vector<const CreaturePermanent*> ordered;
    ordered.reserve(raw.size());
    for (const auto& creature : raw) {
        ordered.push_back(&creature);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const CreaturePermanent* left,
                 const CreaturePermanent* right) {
                  const int lp = creature_current_power(*left);
                  const int rp = creature_current_power(*right);
                  if (lp != rp) {
                      return lp > rp;
                  }
                  const int lt = creature_current_toughness(*left);
                  const int rt = creature_current_toughness(*right);
                  if (lt != rt) {
                      return lt > rt;
                  }
                  if (left->card != right->card) {
                      return left->card < right->card;
                  }
                  return left->id < right->id;
              });
    const auto push = [&features](double value) {
        features.push_back(static_cast<float>(value));
    };
    for (std::size_t slot = first_slot;
         slot < first_slot + slot_count; ++slot) {
        if (slot >= ordered.size()) {
            for (std::size_t f = 0; f < kCreatureSlotFeatures; ++f) {
                push(0.0);
            }
            continue;
        }
        const CreaturePermanent& creature = *ordered[slot];
        const auto& definition = card_definition(creature.card);
        push(1.0);
        for (std::size_t card = 0; card < kCardCount; ++card) {
            push(card == static_cast<std::size_t>(creature.card) ? 1.0
                                                                 : 0.0);
        }
        push(creature_current_power(creature) / 12.0);
        push(creature_current_toughness(creature) / 12.0);
        push(std::max(0, creature_current_toughness(creature) -
                             creature.damage) /
             12.0);
        push(creature.damage / 8.0);
        push(creature.temporary_power_bonus / 6.0);
        push(creature.temporary_toughness_bonus / 6.0);
        push(creature.tapped ? 1.0 : 0.0);
        push(creature.summoning_sick ? 1.0 : 0.0);
        push(definition.flying ? 1.0 : 0.0);
    }
}

// Approximate castability against the observer's untapped producers: lands
// give their color, Mox Sapphire blue, Sol Ring two generic. Colored costs
// draw from matching producers first, generic from what remains.
struct ManaAvailable {
    int green = 0, red = 0, blue = 0, white = 0, generic = 0;
    // Mana usable as any single color (Black Lotus, Channel life).
    int wild = 0;

    int total() const {
        return green + red + blue + white + generic + wild;
    }
};

ManaAvailable available_mana(const PublicPlayerState& player) {
    ManaAvailable mana;
    for (const auto& land : player.lands) {
        if (land.tapped) {
            continue;
        }
        switch (land.card) {
            case CardId::Forest: mana.green += 1; break;
            case CardId::Mountain: mana.red += 1; break;
            case CardId::Island: mana.blue += 1; break;
            case CardId::Plains: mana.white += 1; break;
            default: mana.generic += 1; break;
        }
    }
    for (const auto& artifact : player.artifacts) {
        if (artifact.tapped) {
            continue;
        }
        if (artifact.card == CardId::MoxSapphire) {
            mana.blue += 1;
        } else if (artifact.card == CardId::SolRing) {
            mana.generic += 2;
        } else if (artifact.card == CardId::BlackLotus) {
            mana.wild += 3;
        }
    }
    if (player.channel_active) {
        mana.wild += std::max(0, player.life - 1);
    }
    mana.green += player.mana_pool.green;
    mana.red += player.mana_pool.red;
    mana.blue += player.mana_pool.blue;
    mana.white += player.mana_pool.white;
    mana.generic += player.mana_pool.generic;
    return mana;
}

bool roughly_castable(const CardDefinition& definition,
                      const ManaAvailable& mana, bool land_played) {
    if (definition.type == CardType::Land) {
        return !land_played;
    }
    const ManaCost& cost = definition.cost;
    int wild = mana.wild;
    const auto shortfall = [&wild](int have, int need) {
        const int missing = std::max(0, need - have);
        if (missing > wild) {
            return true;
        }
        wild -= missing;
        return false;
    };
    if (shortfall(mana.green, cost.green) ||
        shortfall(mana.red, cost.red) ||
        shortfall(mana.blue, cost.blue) ||
        shortfall(mana.white, cost.white)) {
        return false;
    }
    const int leftover = mana.total() - cost.green - cost.red -
                         cost.blue - cost.white;
    return leftover >= cost.generic;
}

}  // namespace

std::size_t spz_feature_count_v2() {
    return spz_feature_count() +
           2 * kCreatureSlots * kCreatureSlotFeatures +
           kStackSlots * kStackSlotFeatures + kCastabilityFeatures +
           kRaceFeatures;
}

std::vector<float> spz_features_v2(
    const PlayerObservation& observation,
    const std::array<std::vector<CardId>, 2>& original_decks,
    TurnPhase phase) {
    std::vector<float> features =
        spz_features(observation, original_decks, phase);
    features.reserve(spz_feature_count_v2());
    const std::size_t me = observation.observer;
    const std::size_t opponent = 1 - me;
    const PublicPlayerState& my_public = observation.players[me];
    const PublicPlayerState& opponent_public =
        observation.players[opponent];
    const auto push = [&features](double value) {
        features.push_back(static_cast<float>(value));
    };

    append_creature_slots(features, my_public.creatures);
    append_creature_slots(features, opponent_public.creatures);

    // Top of stack first: the objects that resolve next.
    for (std::size_t slot = 0; slot < kStackSlots; ++slot) {
        if (slot >= observation.stack.size()) {
            for (std::size_t f = 0; f < kStackSlotFeatures; ++f) {
                push(0.0);
            }
            continue;
        }
        const StackObject& object =
            observation.stack[observation.stack.size() - 1 - slot];
        push(1.0);
        for (std::size_t card = 0; card < kCardCount; ++card) {
            push(card == static_cast<std::size_t>(object.card) ? 1.0
                                                               : 0.0);
        }
        push(object.controller == me ? 1.0 : 0.0);
        push(object.kind == StackObjectKind::ActivatedAbility ? 1.0
                                                              : 0.0);
        std::array<double, 5> target_class{};
        std::array<double, kCardCount> target_card{};
        if (!object.target.has_value()) {
            target_class[0] = 1.0;
        } else if (!object.target->creature.has_value()) {
            target_class[object.target->player == me ? 1 : 2] = 1.0;
        } else {
            target_class[object.target->player == me ? 3 : 4] = 1.0;
            const auto& creatures =
                observation.players[object.target->player].creatures;
            for (const auto& creature : creatures) {
                if (creature.id == *object.target->creature) {
                    target_card[static_cast<std::size_t>(
                        creature.card)] = 1.0;
                    break;
                }
            }
        }
        for (const double value : target_class) {
            push(value);
        }
        for (const double value : target_card) {
            push(value);
        }
        push(object.x_value / 4.0);
    }

    const ManaAvailable mana = available_mana(my_public);
    std::array<int, kCardCount> castable{};
    int castable_total = 0;
    for (const CardId card : observation.hand) {
        if (roughly_castable(card_definition(card), mana,
                             my_public.land_played_this_turn)) {
            castable[static_cast<std::size_t>(card)] += 1;
            castable_total += 1;
        }
    }
    for (const int count : castable) {
        push(count / 4.0);
    }
    push(castable_total / 7.0);
    push(mana.total() / 8.0);

    int my_ready_power = 0;
    int my_untapped_power = 0;
    for (const auto& creature : my_public.creatures) {
        const int power = creature_current_power(creature);
        if (!creature.tapped) {
            my_untapped_power += power;
            if (!creature.summoning_sick) {
                my_ready_power += power;
            }
        }
    }
    int opponent_ready_power = 0;
    int opponent_untapped_power = 0;
    for (const auto& creature : opponent_public.creatures) {
        const int power = creature_current_power(creature);
        if (!creature.tapped) {
            opponent_untapped_power += power;
            if (!creature.summoning_sick) {
                opponent_ready_power += power;
            }
        }
    }
    const auto clipped_ratio = [](double numerator,
                                  double denominator) {
        return std::min(2.0, numerator / std::max(1.0, denominator));
    };
    push(clipped_ratio(my_ready_power, opponent_public.life));
    push(clipped_ratio(opponent_ready_power, my_public.life));
    push(my_ready_power >= opponent_public.life ? 1.0 : 0.0);
    push(opponent_ready_power >= my_public.life ? 1.0 : 0.0);
    push(std::min(10.0, opponent_public.life /
                            std::max(1.0, double(my_ready_power))) /
         10.0);
    push(std::min(10.0, my_public.life /
                            std::max(1.0,
                                     double(opponent_ready_power))) /
         10.0);
    push(clipped_ratio(my_untapped_power, opponent_untapped_power));
    push((my_ready_power - opponent_ready_power) / 12.0);
    push((static_cast<double>(my_public.creatures.size()) -
          static_cast<double>(opponent_public.creatures.size())) /
         6.0);
    push(clipped_ratio(opponent_untapped_power, my_public.life));

    if (features.size() != spz_feature_count_v2()) {
        throw std::logic_error("spz v2 feature schema size mismatch");
    }
    return features;
}

namespace {

constexpr std::size_t kCreatureSlotsV3 = 18;
constexpr std::size_t kStackSlotsV3 = 6;

void append_stack_slot(std::vector<float>& features,
                       const PlayerObservation& observation,
                       std::size_t me, std::size_t slot) {
    const auto push = [&features](double value) {
        features.push_back(static_cast<float>(value));
    };
    if (slot >= observation.stack.size()) {
        for (std::size_t f = 0; f < kStackSlotFeatures; ++f) {
            push(0.0);
        }
        return;
    }
    const StackObject& object =
        observation.stack[observation.stack.size() - 1 - slot];
    push(1.0);
    for (std::size_t card = 0; card < kCardCount; ++card) {
        push(card == static_cast<std::size_t>(object.card) ? 1.0 : 0.0);
    }
    push(object.controller == me ? 1.0 : 0.0);
    push(object.kind == StackObjectKind::ActivatedAbility ? 1.0 : 0.0);
    std::array<double, 5> target_class{};
    std::array<double, kCardCount> target_card{};
    if (!object.target.has_value()) {
        target_class[0] = 1.0;
    } else if (!object.target->creature.has_value()) {
        target_class[object.target->player == me ? 1 : 2] = 1.0;
    } else {
        target_class[object.target->player == me ? 3 : 4] = 1.0;
        for (const auto& creature :
             observation.players[object.target->player].creatures) {
            if (creature.id == *object.target->creature) {
                target_card[static_cast<std::size_t>(creature.card)] =
                    1.0;
                break;
            }
        }
    }
    for (const double value : target_class) {
        push(value);
    }
    for (const double value : target_card) {
        push(value);
    }
    push(object.x_value / 4.0);
}

void append_untapped_permanent_counts(
    std::vector<float>& features, const PublicPlayerState& player) {
    std::array<int, kCardCount> counts{};
    for (const auto& land : player.lands) {
        if (!land.tapped) {
            counts[static_cast<std::size_t>(land.card)] += 1;
        }
    }
    for (const auto& creature : player.creatures) {
        if (!creature.tapped) {
            counts[static_cast<std::size_t>(creature.card)] += 1;
        }
    }
    for (const auto& artifact : player.artifacts) {
        if (!artifact.tapped) {
            counts[static_cast<std::size_t>(artifact.card)] += 1;
        }
    }
    for (const CardId enchantment : player.enchantments) {
        counts[static_cast<std::size_t>(enchantment)] += 1;
    }
    for (const int count : counts) {
        features.push_back(static_cast<float>(count) / 4.0f);
    }
}

}  // namespace

// Schema v3: lossless play-zone representation. v2 as an exact prefix,
// plus creature slots nine through eighteen per player (no metagame deck
// can field more, so individual creatures never aggregate), untapped
// counts by card for every permanent type, per-color mana pools for both
// players, and stack objects four through six.
std::size_t spz_feature_count_v3() {
    return spz_feature_count_v2() +
           2 * (kCreatureSlotsV3 - kCreatureSlots) *
               kCreatureSlotFeatures +
           2 * kCardCount + 10 +
           (kStackSlotsV3 - kStackSlots) * kStackSlotFeatures;
}

std::vector<float> spz_features_v3(
    const PlayerObservation& observation,
    const std::array<std::vector<CardId>, 2>& original_decks,
    TurnPhase phase) {
    std::vector<float> features =
        spz_features_v2(observation, original_decks, phase);
    features.reserve(spz_feature_count_v3());
    const std::size_t me = observation.observer;
    const std::size_t opponent = 1 - me;
    append_creature_slots_range(features,
                                observation.players[me].creatures,
                                kCreatureSlots,
                                kCreatureSlotsV3 - kCreatureSlots);
    append_creature_slots_range(features,
                                observation.players[opponent].creatures,
                                kCreatureSlots,
                                kCreatureSlotsV3 - kCreatureSlots);
    append_untapped_permanent_counts(features, observation.players[me]);
    append_untapped_permanent_counts(features,
                                     observation.players[opponent]);
    for (const std::size_t player : {me, opponent}) {
        const ManaCost& pool = observation.players[player].mana_pool;
        features.push_back(static_cast<float>(pool.green) / 4.0f);
        features.push_back(static_cast<float>(pool.red) / 4.0f);
        features.push_back(static_cast<float>(pool.blue) / 4.0f);
        features.push_back(static_cast<float>(pool.white) / 4.0f);
        features.push_back(static_cast<float>(pool.generic) / 4.0f);
    }
    for (std::size_t slot = kStackSlots; slot < kStackSlotsV3; ++slot) {
        append_stack_slot(features, observation, me, slot);
    }
    if (features.size() != spz_feature_count_v3()) {
        throw std::logic_error("spz v3 feature schema size mismatch");
    }
    return features;
}

std::vector<float> spz_features_for(
    std::size_t input_count, const PlayerObservation& observation,
    const std::array<std::vector<CardId>, 2>& original_decks,
    TurnPhase phase) {
    if (input_count == spz_feature_count()) {
        return spz_features(observation, original_decks, phase);
    }
    if (input_count == spz_feature_count_v2()) {
        return spz_features_v2(observation, original_decks, phase);
    }
    if (input_count == spz_feature_count_v3()) {
        return spz_features_v3(observation, original_decks, phase);
    }
    throw std::invalid_argument(
        "unknown SPZ feature schema for input count " +
        std::to_string(input_count));
}

GameState reconstruct_observed_state(const PlayerObservation& observation) {
    const std::size_t me = observation.observer;
    const std::size_t opponent = 1 - me;

    GameState state;
    for (std::size_t player = 0; player < 2; ++player) {
        const PublicPlayerState& public_state = observation.players[player];
        PlayerState& reconstructed = state.players[player];
        reconstructed.life = public_state.life;
        reconstructed.graveyard = public_state.graveyard;
        reconstructed.exile = public_state.exile;
        reconstructed.lands = public_state.lands;
        reconstructed.creatures = public_state.creatures;
        reconstructed.artifacts = public_state.artifacts;
        reconstructed.enchantments = public_state.enchantments;
        reconstructed.mana_pool = public_state.mana_pool;
        reconstructed.land_played_this_turn =
            public_state.land_played_this_turn;
        reconstructed.channel_active = public_state.channel_active;
        reconstructed.library.assign(public_state.library_size, CardId{});
    }
    state.players[me].hand = observation.hand;
    state.players[opponent].hand.assign(
        observation.players[opponent].hand_size, CardId{});
    state.stack = observation.stack;
    state.extra_turns_pending = observation.extra_turns_pending;
    state.active_player = observation.active_player;
    state.starting_player = observation.starting_player;
    state.turn_number = observation.turn_number;

    PermanentId maximum_permanent = 0;
    for (const PlayerState& player : state.players) {
        for (const auto& creature : player.creatures) {
            maximum_permanent = std::max(maximum_permanent, creature.id);
        }
        for (const auto& artifact : player.artifacts) {
            maximum_permanent = std::max(maximum_permanent, artifact.id);
        }
    }
    state.next_permanent_id = maximum_permanent + 1;
    StackObjectId maximum_stack_object = 0;
    for (const auto& object : state.stack) {
        maximum_stack_object = std::max(maximum_stack_object, object.id);
    }
    state.next_stack_object_id = maximum_stack_object + 1;
    return state;
}

// ---------------------------------------------------------------------------
// Value network

SpzNet::SpzNet(std::size_t inputs, std::size_t hidden, std::uint64_t seed)
    : inputs_(inputs), hidden_(hidden) {
    if (inputs == 0 || hidden == 0) {
        throw std::invalid_argument("SpzNet requires nonzero dimensions");
    }
    std::mt19937_64 random(seed);
    const double scale =
        1.0 / std::sqrt(static_cast<double>(inputs));
    std::uniform_real_distribution<double> hidden_init(-scale, scale);
    hidden_weights_.resize(hidden * inputs);
    for (double& weight : hidden_weights_) {
        weight = hidden_init(random);
    }
    hidden_bias_.assign(hidden, 0.0);
    const double output_scale =
        1.0 / std::sqrt(static_cast<double>(hidden));
    std::uniform_real_distribution<double> output_init(-output_scale,
                                                       output_scale);
    output_weights_.resize(hidden);
    for (double& weight : output_weights_) {
        weight = output_init(random);
    }
    output_bias_ = 0.0;
    momentum_.assign(hidden * inputs + hidden + hidden + 1, 0.0);
}

namespace {

double sigmoid(double value) {
    return 1.0 / (1.0 + std::exp(-value));
}

}  // namespace

double SpzNet::value(const std::vector<float>& features) const {
    if (features.size() != inputs_) {
        throw std::invalid_argument("SpzNet feature size mismatch");
    }
    double output = output_bias_;
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        double activation = hidden_bias_[unit];
        const double* row = hidden_weights_.data() + unit * inputs_;
        for (std::size_t input = 0; input < inputs_; ++input) {
            activation += row[input] * features[input];
        }
        output += output_weights_[unit] * std::tanh(activation);
    }
    return sigmoid(output);
}

double SpzNet::train_batch(
    const std::vector<const std::vector<float>*>& features,
    const std::vector<float>& targets, double learning_rate) {
    if (features.size() != targets.size() || features.empty()) {
        throw std::invalid_argument("SpzNet batch size mismatch");
    }
    const std::size_t weight_count = hidden_ * inputs_;
    std::vector<double> gradient(weight_count + hidden_ + hidden_ + 1, 0.0);
    std::vector<double> activations(hidden_, 0.0);
    double total_loss = 0.0;
    for (std::size_t example = 0; example < features.size(); ++example) {
        const std::vector<float>& row = *features[example];
        if (row.size() != inputs_) {
            throw std::invalid_argument("SpzNet feature size mismatch");
        }
        double output = output_bias_;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            double activation = hidden_bias_[unit];
            const double* weights =
                hidden_weights_.data() + unit * inputs_;
            for (std::size_t input = 0; input < inputs_; ++input) {
                activation += weights[input] * row[input];
            }
            activations[unit] = std::tanh(activation);
            output += output_weights_[unit] * activations[unit];
        }
        const double prediction = sigmoid(output);
        const double target = targets[example];
        const double clamped =
            std::clamp(prediction, 1e-7, 1.0 - 1e-7);
        total_loss += -(target * std::log(clamped) +
                        (1.0 - target) * std::log(1.0 - clamped));
        const double output_delta = prediction - target;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            gradient[weight_count + hidden_ + unit] +=
                output_delta * activations[unit];
            const double hidden_delta =
                output_delta * output_weights_[unit] *
                (1.0 - activations[unit] * activations[unit]);
            gradient[weight_count + unit] += hidden_delta;
            double* row_gradient = gradient.data() + unit * inputs_;
            for (std::size_t input = 0; input < inputs_; ++input) {
                row_gradient[input] += hidden_delta * row[input];
            }
        }
        gradient[weight_count + hidden_ + hidden_] += output_delta;
    }

    const double batch_scale = 1.0 / static_cast<double>(features.size());
    constexpr double kMomentum = 0.9;
    const auto apply = [&](std::size_t offset, double* parameter) {
        momentum_[offset] = kMomentum * momentum_[offset] -
                            learning_rate * gradient[offset] * batch_scale;
        *parameter += momentum_[offset];
    };
    for (std::size_t index = 0; index < weight_count; ++index) {
        apply(index, &hidden_weights_[index]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + unit, &hidden_bias_[unit]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + hidden_ + unit, &output_weights_[unit]);
    }
    apply(weight_count + hidden_ + hidden_, &output_bias_);
    return total_loss * batch_scale;
}

void SpzNet::save(std::ostream& out) const {
    out << "spz-net-v1\n" << inputs_ << ' ' << hidden_ << '\n';
    out << std::hexfloat;
    for (const double weight : hidden_weights_) {
        out << weight << '\n';
    }
    for (const double bias : hidden_bias_) {
        out << bias << '\n';
    }
    for (const double weight : output_weights_) {
        out << weight << '\n';
    }
    out << output_bias_ << '\n';
}

SpzNet SpzNet::load(std::istream& in) {
    std::string magic;
    in >> magic;
    if (magic != "spz-net-v1") {
        throw std::runtime_error("unrecognized SPZ net artifact header");
    }
    SpzNet net;
    in >> net.inputs_ >> net.hidden_;
    if (!in || net.inputs_ == 0 || net.hidden_ == 0) {
        throw std::runtime_error("malformed SPZ net dimensions");
    }
    const auto read_value = [&in]() {
        std::string token;
        in >> token;
        if (!in) {
            throw std::runtime_error("truncated SPZ net artifact");
        }
        return std::strtod(token.c_str(), nullptr);
    };
    net.hidden_weights_.resize(net.hidden_ * net.inputs_);
    for (double& weight : net.hidden_weights_) {
        weight = read_value();
    }
    net.hidden_bias_.resize(net.hidden_);
    for (double& bias : net.hidden_bias_) {
        bias = read_value();
    }
    net.output_weights_.resize(net.hidden_);
    for (double& weight : net.output_weights_) {
        weight = read_value();
    }
    net.output_bias_ = read_value();
    net.momentum_.assign(
        net.hidden_ * net.inputs_ + net.hidden_ + net.hidden_ + 1, 0.0);
    return net;
}

void save_spz_net(const SpzNet& net, const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open SPZ artifact for writing: " +
                                 path);
    }
    net.save(out);
    if (!out) {
        throw std::runtime_error("failed writing SPZ artifact: " + path);
    }
}

SpzNet load_spz_net(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open SPZ artifact: " + path);
    }
    return SpzNet::load(in);
}

// ---------------------------------------------------------------------------
// Policy network

std::size_t spz_action_feature_count() {
    // kind one-hot + card one-hot + target class (none/self/opponent
    // player, own/enemy creature) + target creature card + countered spell
    // card + x scale + source-permanent flag.
    return 14 + kCardCount + 5 + kCardCount + kCardCount + 1 + 1;
}

std::vector<float> spz_action_features(const PriorityAction& action,
                                       std::size_t actor,
                                       const GameState& context) {
    std::vector<float> features(spz_action_feature_count(), 0.0f);
    std::size_t offset = 0;
    features[offset + static_cast<std::size_t>(action.kind)] = 1.0f;
    offset += 14;
    features[offset + static_cast<std::size_t>(action.card)] = 1.0f;
    offset += kCardCount;
    if (!action.target.has_value()) {
        features[offset + 0] = 1.0f;
    } else if (!action.target->creature.has_value()) {
        features[offset + (action.target->player == actor ? 1 : 2)] =
            1.0f;
    } else {
        features[offset + (action.target->player == actor ? 3 : 4)] =
            1.0f;
    }
    offset += 5;
    if (action.target.has_value() &&
        action.target->creature.has_value()) {
        for (const auto& creature :
             context.players[action.target->player].creatures) {
            if (creature.id == *action.target->creature) {
                features[offset +
                         static_cast<std::size_t>(creature.card)] = 1.0f;
                break;
            }
        }
    }
    offset += kCardCount;
    if (action.spell_target.has_value()) {
        for (const auto& object : context.stack) {
            if (object.id == *action.spell_target) {
                features[offset +
                         static_cast<std::size_t>(object.card)] = 1.0f;
                break;
            }
        }
    }
    offset += kCardCount;
    features[offset] = static_cast<float>(action.x_value) / 4.0f;
    offset += 1;
    features[offset] = action.source_permanent.has_value() ? 1.0f : 0.0f;
    return features;
}

SpzPolicyNet::SpzPolicyNet(std::size_t state_inputs,
                           std::size_t action_inputs, std::size_t hidden,
                           std::uint64_t seed)
    : state_inputs_(state_inputs),
      action_inputs_(action_inputs),
      hidden_(hidden) {
    if (state_inputs == 0 || action_inputs == 0 || hidden == 0) {
        throw std::invalid_argument(
            "SpzPolicyNet requires nonzero dimensions");
    }
    const std::size_t inputs = state_inputs + action_inputs;
    std::mt19937_64 random(seed);
    const double scale = 1.0 / std::sqrt(static_cast<double>(inputs));
    std::uniform_real_distribution<double> hidden_init(-scale, scale);
    hidden_weights_.resize(hidden * inputs);
    for (double& weight : hidden_weights_) {
        weight = hidden_init(random);
    }
    hidden_bias_.assign(hidden, 0.0);
    const double output_scale =
        1.0 / std::sqrt(static_cast<double>(hidden));
    std::uniform_real_distribution<double> output_init(-output_scale,
                                                       output_scale);
    output_weights_.resize(hidden);
    for (double& weight : output_weights_) {
        weight = output_init(random);
    }
    momentum_.assign(hidden * inputs + hidden + hidden + 1, 0.0);
}

double SpzPolicyNet::logit(
    const std::vector<float>& state_features,
    const std::vector<float>& action_features) const {
    if (state_features.size() != state_inputs_ ||
        action_features.size() != action_inputs_) {
        throw std::invalid_argument(
            "SpzPolicyNet feature size mismatch");
    }
    const std::size_t inputs = state_inputs_ + action_inputs_;
    double output = output_bias_;
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        const double* row = hidden_weights_.data() + unit * inputs;
        double activation = hidden_bias_[unit];
        for (std::size_t index = 0; index < state_inputs_; ++index) {
            activation += row[index] * state_features[index];
        }
        for (std::size_t index = 0; index < action_inputs_; ++index) {
            activation +=
                row[state_inputs_ + index] * action_features[index];
        }
        output += output_weights_[unit] * std::tanh(activation);
    }
    return output;
}

double SpzPolicyNet::train_batch(const std::vector<Decision>& decisions,
                                 double learning_rate) {
    if (decisions.empty()) {
        return 0.0;
    }
    const std::size_t inputs = state_inputs_ + action_inputs_;
    const std::size_t weight_count = hidden_ * inputs;
    std::vector<double> gradient(weight_count + hidden_ + hidden_ + 1,
                                 0.0);
    double total_loss = 0.0;
    std::size_t decision_count = 0;
    for (const Decision& decision : decisions) {
        const auto& actions = *decision.actions;
        const auto& target = *decision.target;
        if (actions.empty() || actions.size() != target.size()) {
            continue;
        }
        decision_count += 1;
        // Forward every action, caching activations for the backward pass.
        std::vector<std::vector<double>> activations(actions.size());
        std::vector<double> logits(actions.size());
        for (std::size_t option = 0; option < actions.size(); ++option) {
            activations[option].resize(hidden_);
            double output = output_bias_;
            for (std::size_t unit = 0; unit < hidden_; ++unit) {
                const double* row =
                    hidden_weights_.data() + unit * inputs;
                double activation = hidden_bias_[unit];
                for (std::size_t index = 0; index < state_inputs_;
                     ++index) {
                    activation += row[index] * (*decision.state)[index];
                }
                for (std::size_t index = 0; index < action_inputs_;
                     ++index) {
                    activation += row[state_inputs_ + index] *
                                  actions[option][index];
                }
                activations[option][unit] = std::tanh(activation);
                output +=
                    output_weights_[unit] * activations[option][unit];
            }
            logits[option] = output;
        }
        double best = -std::numeric_limits<double>::infinity();
        for (const double value : logits) {
            best = std::max(best, value);
        }
        double normalizer = 0.0;
        std::vector<double> probabilities(actions.size());
        for (std::size_t option = 0; option < actions.size(); ++option) {
            probabilities[option] = std::exp(logits[option] - best);
            normalizer += probabilities[option];
        }
        for (std::size_t option = 0; option < actions.size(); ++option) {
            probabilities[option] /= normalizer;
            if (target[option] > 0.0f) {
                total_loss -=
                    target[option] *
                    std::log(std::max(probabilities[option], 1e-9));
            }
        }
        for (std::size_t option = 0; option < actions.size(); ++option) {
            const double output_delta =
                probabilities[option] - target[option];
            gradient[weight_count + hidden_ + hidden_] += output_delta;
            for (std::size_t unit = 0; unit < hidden_; ++unit) {
                const double activation = activations[option][unit];
                gradient[weight_count + hidden_ + unit] +=
                    output_delta * activation;
                const double hidden_delta =
                    output_delta * output_weights_[unit] *
                    (1.0 - activation * activation);
                gradient[weight_count + unit] += hidden_delta;
                double* row_gradient =
                    gradient.data() + unit * inputs;
                for (std::size_t index = 0; index < state_inputs_;
                     ++index) {
                    row_gradient[index] +=
                        hidden_delta * (*decision.state)[index];
                }
                for (std::size_t index = 0; index < action_inputs_;
                     ++index) {
                    row_gradient[state_inputs_ + index] +=
                        hidden_delta * actions[option][index];
                }
            }
        }
    }
    if (decision_count == 0) {
        return 0.0;
    }
    const double batch_scale = 1.0 / static_cast<double>(decision_count);
    constexpr double kMomentum = 0.9;
    const auto apply = [&](std::size_t offset, double* parameter) {
        momentum_[offset] = kMomentum * momentum_[offset] -
                            learning_rate * gradient[offset] * batch_scale;
        *parameter += momentum_[offset];
    };
    for (std::size_t index = 0; index < weight_count; ++index) {
        apply(index, &hidden_weights_[index]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + unit, &hidden_bias_[unit]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + hidden_ + unit, &output_weights_[unit]);
    }
    apply(weight_count + hidden_ + hidden_, &output_bias_);
    return total_loss * batch_scale;
}

void SpzPolicyNet::save(std::ostream& out) const {
    out << "spz-policy-v1\n"
        << state_inputs_ << ' ' << action_inputs_ << ' ' << hidden_
        << '\n';
    out << std::hexfloat;
    for (const double weight : hidden_weights_) {
        out << weight << '\n';
    }
    for (const double bias : hidden_bias_) {
        out << bias << '\n';
    }
    for (const double weight : output_weights_) {
        out << weight << '\n';
    }
    out << output_bias_ << '\n';
}

SpzPolicyNet SpzPolicyNet::load(std::istream& in) {
    std::string magic;
    in >> magic;
    if (magic != "spz-policy-v1") {
        throw std::runtime_error(
            "unrecognized SPZ policy artifact header");
    }
    SpzPolicyNet net;
    in >> net.state_inputs_ >> net.action_inputs_ >> net.hidden_;
    if (!in || net.state_inputs_ == 0 || net.action_inputs_ == 0 ||
        net.hidden_ == 0) {
        throw std::runtime_error("malformed SPZ policy dimensions");
    }
    const auto read_value = [&in]() {
        std::string token;
        in >> token;
        if (!in) {
            throw std::runtime_error("truncated SPZ policy artifact");
        }
        return std::strtod(token.c_str(), nullptr);
    };
    const std::size_t inputs =
        net.state_inputs_ + net.action_inputs_;
    net.hidden_weights_.resize(net.hidden_ * inputs);
    for (double& weight : net.hidden_weights_) {
        weight = read_value();
    }
    net.hidden_bias_.resize(net.hidden_);
    for (double& bias : net.hidden_bias_) {
        bias = read_value();
    }
    net.output_weights_.resize(net.hidden_);
    for (double& weight : net.output_weights_) {
        weight = read_value();
    }
    net.output_bias_ = read_value();
    net.momentum_.assign(net.hidden_ * inputs + net.hidden_ * 2 + 1,
                         0.0);
    return net;
}

void save_spz_policy_net(const SpzPolicyNet& net,
                         const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error(
            "cannot open SPZ policy artifact for writing: " + path);
    }
    net.save(out);
    if (!out) {
        throw std::runtime_error("failed writing SPZ policy artifact: " +
                                 path);
    }
}

SpzPolicyNet load_spz_policy_net(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open SPZ policy artifact: " +
                                 path);
    }
    return SpzPolicyNet::load(in);
}

// ---------------------------------------------------------------------------
// Advantage head

SpzAdvantageNet::SpzAdvantageNet(std::size_t state_inputs,
                                 std::size_t action_inputs,
                                 std::size_t hidden, std::uint64_t seed)
    : state_inputs_(state_inputs),
      action_inputs_(action_inputs),
      hidden_(hidden) {
    if (state_inputs == 0 || action_inputs == 0 || hidden == 0) {
        throw std::invalid_argument(
            "SpzAdvantageNet requires nonzero dimensions");
    }
    const std::size_t inputs = state_inputs + action_inputs;
    std::mt19937_64 random(seed);
    const double scale = 1.0 / std::sqrt(static_cast<double>(inputs));
    std::uniform_real_distribution<double> hidden_init(-scale, scale);
    hidden_weights_.resize(hidden * inputs);
    for (double& weight : hidden_weights_) {
        weight = hidden_init(random);
    }
    hidden_bias_.assign(hidden, 0.0);
    const double output_scale =
        0.1 / std::sqrt(static_cast<double>(hidden));
    std::uniform_real_distribution<double> output_init(-output_scale,
                                                       output_scale);
    output_weights_.resize(hidden);
    for (double& weight : output_weights_) {
        weight = output_init(random);
    }
    momentum_.assign(hidden * inputs + hidden + hidden + 1, 0.0);
}

double SpzAdvantageNet::delta(
    const std::vector<float>& state_features,
    const std::vector<float>& action_features) const {
    if (state_features.size() != state_inputs_ ||
        action_features.size() != action_inputs_) {
        throw std::invalid_argument(
            "SpzAdvantageNet feature size mismatch");
    }
    const std::size_t inputs = state_inputs_ + action_inputs_;
    double output = output_bias_;
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        const double* row = hidden_weights_.data() + unit * inputs;
        double activation = hidden_bias_[unit];
        for (std::size_t index = 0; index < state_inputs_; ++index) {
            activation += row[index] * state_features[index];
        }
        for (std::size_t index = 0; index < action_inputs_; ++index) {
            activation +=
                row[state_inputs_ + index] * action_features[index];
        }
        output += output_weights_[unit] * std::tanh(activation);
    }
    return output;
}

double SpzAdvantageNet::train_batch(const std::vector<Sample>& samples,
                                    double learning_rate) {
    if (samples.empty()) {
        return 0.0;
    }
    const std::size_t inputs = state_inputs_ + action_inputs_;
    const std::size_t weight_count = hidden_ * inputs;
    std::vector<double> gradient(weight_count + hidden_ + hidden_ + 1,
                                 0.0);
    std::vector<double> activations(hidden_);
    double total_loss = 0.0;
    for (const Sample& sample : samples) {
        double output = output_bias_;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            const double* row = hidden_weights_.data() + unit * inputs;
            double activation = hidden_bias_[unit];
            for (std::size_t index = 0; index < state_inputs_;
                 ++index) {
                activation += row[index] * (*sample.state)[index];
            }
            for (std::size_t index = 0; index < action_inputs_;
                 ++index) {
                activation += row[state_inputs_ + index] *
                              (*sample.action)[index];
            }
            activations[unit] = std::tanh(activation);
            output += output_weights_[unit] * activations[unit];
        }
        const double error = output - sample.target;
        total_loss += error * error;
        const double output_delta = 2.0 * error;
        gradient[weight_count + hidden_ + hidden_] += output_delta;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            gradient[weight_count + hidden_ + unit] +=
                output_delta * activations[unit];
            const double hidden_delta =
                output_delta * output_weights_[unit] *
                (1.0 - activations[unit] * activations[unit]);
            gradient[weight_count + unit] += hidden_delta;
            double* row_gradient = gradient.data() + unit * inputs;
            for (std::size_t index = 0; index < state_inputs_;
                 ++index) {
                row_gradient[index] +=
                    hidden_delta * (*sample.state)[index];
            }
            for (std::size_t index = 0; index < action_inputs_;
                 ++index) {
                row_gradient[state_inputs_ + index] +=
                    hidden_delta * (*sample.action)[index];
            }
        }
    }
    const double batch_scale = 1.0 / static_cast<double>(samples.size());
    constexpr double kMomentum = 0.9;
    const auto apply = [&](std::size_t offset, double* parameter) {
        momentum_[offset] = kMomentum * momentum_[offset] -
                            learning_rate * gradient[offset] * batch_scale;
        *parameter += momentum_[offset];
    };
    for (std::size_t index = 0; index < weight_count; ++index) {
        apply(index, &hidden_weights_[index]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + unit, &hidden_bias_[unit]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + hidden_ + unit, &output_weights_[unit]);
    }
    apply(weight_count + hidden_ + hidden_, &output_bias_);
    return total_loss * batch_scale;
}

double SpzAdvantageNet::train_ranking_batch(
    const std::vector<RankedPair>& pairs, double learning_rate) {
    if (pairs.empty()) {
        return 0.0;
    }
    const std::size_t inputs = state_inputs_ + action_inputs_;
    const std::size_t weight_count = hidden_ * inputs;
    std::vector<double> gradient(weight_count + hidden_ + hidden_ + 1,
                                 0.0);
    std::vector<double> better_activations(hidden_);
    std::vector<double> worse_activations(hidden_);
    double total_loss = 0.0;
    const auto forward = [&](const std::vector<float>& state,
                             const std::vector<float>& action,
                             std::vector<double>& activations) {
        double output = output_bias_;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            const double* row = hidden_weights_.data() + unit * inputs;
            double activation = hidden_bias_[unit];
            for (std::size_t index = 0; index < state_inputs_;
                 ++index) {
                activation += row[index] * state[index];
            }
            for (std::size_t index = 0; index < action_inputs_;
                 ++index) {
                activation += row[state_inputs_ + index] * action[index];
            }
            activations[unit] = std::tanh(activation);
            output += output_weights_[unit] * activations[unit];
        }
        return output;
    };
    const auto backward = [&](const std::vector<float>& state,
                              const std::vector<float>& action,
                              const std::vector<double>& activations,
                              double output_delta) {
        gradient[weight_count + hidden_ + hidden_] += output_delta;
        for (std::size_t unit = 0; unit < hidden_; ++unit) {
            gradient[weight_count + hidden_ + unit] +=
                output_delta * activations[unit];
            const double hidden_delta =
                output_delta * output_weights_[unit] *
                (1.0 - activations[unit] * activations[unit]);
            gradient[weight_count + unit] += hidden_delta;
            double* row_gradient = gradient.data() + unit * inputs;
            for (std::size_t index = 0; index < state_inputs_;
                 ++index) {
                row_gradient[index] += hidden_delta * state[index];
            }
            for (std::size_t index = 0; index < action_inputs_;
                 ++index) {
                row_gradient[state_inputs_ + index] +=
                    hidden_delta * action[index];
            }
        }
    };
    for (const RankedPair& pair : pairs) {
        const double better_output =
            forward(*pair.state, *pair.better, better_activations);
        const double worse_output =
            forward(*pair.state, *pair.worse, worse_activations);
        const double margin = better_output - worse_output;
        // Logistic ranking loss ln(1 + e^-margin); its gradient
        // magnitude is the misordering probability.
        total_loss += margin > 30.0
                          ? 0.0
                          : std::log1p(std::exp(-margin));
        const double misordered = 1.0 / (1.0 + std::exp(margin));
        backward(*pair.state, *pair.better, better_activations,
                 -misordered);
        backward(*pair.state, *pair.worse, worse_activations,
                 misordered);
    }
    const double batch_scale = 1.0 / static_cast<double>(pairs.size());
    constexpr double kMomentum = 0.9;
    const auto apply = [&](std::size_t offset, double* parameter) {
        momentum_[offset] = kMomentum * momentum_[offset] -
                            learning_rate * gradient[offset] *
                                batch_scale;
        *parameter += momentum_[offset];
    };
    for (std::size_t index = 0; index < weight_count; ++index) {
        apply(index, &hidden_weights_[index]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + unit, &hidden_bias_[unit]);
    }
    for (std::size_t unit = 0; unit < hidden_; ++unit) {
        apply(weight_count + hidden_ + unit, &output_weights_[unit]);
    }
    apply(weight_count + hidden_ + hidden_, &output_bias_);
    return total_loss * batch_scale;
}

void SpzAdvantageNet::save(std::ostream& out) const {
    out << "spz-advantage-v1\n"
        << state_inputs_ << ' ' << action_inputs_ << ' ' << hidden_
        << '\n';
    out << std::hexfloat;
    for (const double weight : hidden_weights_) {
        out << weight << '\n';
    }
    for (const double bias : hidden_bias_) {
        out << bias << '\n';
    }
    for (const double weight : output_weights_) {
        out << weight << '\n';
    }
    out << output_bias_ << '\n';
}

SpzAdvantageNet SpzAdvantageNet::load(std::istream& in) {
    std::string magic;
    in >> magic;
    if (magic != "spz-advantage-v1") {
        throw std::runtime_error(
            "unrecognized SPZ advantage artifact header");
    }
    SpzAdvantageNet net;
    in >> net.state_inputs_ >> net.action_inputs_ >> net.hidden_;
    if (!in || net.state_inputs_ == 0 || net.action_inputs_ == 0 ||
        net.hidden_ == 0) {
        throw std::runtime_error(
            "malformed SPZ advantage dimensions");
    }
    const auto read_value = [&in]() {
        std::string token;
        in >> token;
        if (!in) {
            throw std::runtime_error(
                "truncated SPZ advantage artifact");
        }
        return std::strtod(token.c_str(), nullptr);
    };
    const std::size_t inputs = net.state_inputs_ + net.action_inputs_;
    net.hidden_weights_.resize(net.hidden_ * inputs);
    for (double& weight : net.hidden_weights_) {
        weight = read_value();
    }
    net.hidden_bias_.resize(net.hidden_);
    for (double& bias : net.hidden_bias_) {
        bias = read_value();
    }
    net.output_weights_.resize(net.hidden_);
    for (double& weight : net.output_weights_) {
        weight = read_value();
    }
    net.output_bias_ = read_value();
    net.momentum_.assign(net.hidden_ * inputs + net.hidden_ * 2 + 1,
                         0.0);
    return net;
}

void save_spz_advantage_net(const SpzAdvantageNet& net,
                            const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error(
            "cannot open SPZ advantage artifact for writing: " + path);
    }
    net.save(out);
    if (!out) {
        throw std::runtime_error(
            "failed writing SPZ advantage artifact: " + path);
    }
}

SpzAdvantageNet load_spz_advantage_net(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open SPZ advantage artifact: " +
                                 path);
    }
    return SpzAdvantageNet::load(in);
}

// ---------------------------------------------------------------------------
// Agent

namespace {

struct SpzAgent {
    std::shared_ptr<const SpzNet> net;
    std::shared_ptr<const SpzPolicyNet> policy_net;
    std::shared_ptr<const SpzAdvantageNet> advantage_net;
    std::array<std::vector<CardId>, 2> decks;
    std::size_t seat = 0;
    SpzPolicyConfig config;
    SpzRecorder* recorder = nullptr;
    std::mt19937_64 rng;

    SpzAgent(std::shared_ptr<const SpzNet> shared_net,
             const std::array<std::vector<CardId>, 2>& original_decks,
             std::size_t player_seat, const SpzPolicyConfig& policy,
             SpzRecorder* sample_recorder,
             std::shared_ptr<const SpzPolicyNet> shared_policy,
             std::shared_ptr<const SpzAdvantageNet> shared_advantage)
        : net(std::move(shared_net)),
          policy_net(std::move(shared_policy)),
          advantage_net(std::move(shared_advantage)),
          decks(original_decks),
          seat(player_seat),
          config(policy),
          recorder(sample_recorder),
          rng(mix_seed(policy.seed, player_seat)) {}

    bool explore() {
        if (config.epsilon <= 0.0) {
            return false;
        }
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        return unit(rng) < config.epsilon;
    }

    // Declared-but-unresolved combat, learned from the public game events
    // every seat receives. It supplies post-blockers response windows with
    // the combat context the observation itself does not carry.
    struct PendingCombat {
        std::vector<PermanentId> attackers;
        std::vector<std::pair<PermanentId, PermanentId>> blocks;
    };
    std::optional<PendingCombat> pending_combat;

    void on_event(const GameEvent& event) {
        switch (event.kind) {
            case GameEventKind::AttackersDeclared:
            case GameEventKind::BlockersDeclared:
            case GameEventKind::DamageOrderChosen:
                pending_combat =
                    PendingCombat{event.attackers, event.blocks};
                break;
            case GameEventKind::CombatResolved:
            case GameEventKind::TurnStarted:
                pending_combat.reset();
                break;
            default:
                break;
        }
    }

    // ------------------------------------------------------------------
    // No-upside prune: a rules-only refusal of actions that cannot help.
    //
    // Compared with passing in the same window, an action has no upside
    // when its settled consequence leaves the opponent equal or better
    // (only their creatures' temporary bonuses may have risen), leaves the
    // actor equal or worse (life no higher; own permanents unchanged apart
    // from extra taps; hand a sub-multiset; graveyard a super-multiset;
    // library no larger; mana no higher), and changes nothing else. This
    // catches pumping an enemy creature, burning one's own face, milling
    // oneself, and every pay-for-a-no-op, without any card knowledge.

    static std::map<CardId, int> counted(const std::vector<CardId>& cards) {
        std::map<CardId, int> counts;
        for (const CardId card : cards) {
            counts[card] += 1;
        }
        return counts;
    }

    static bool multiset_subset(const std::map<CardId, int>& small,
                                const std::map<CardId, int>& large) {
        for (const auto& [card, count] : small) {
            const auto entry = large.find(card);
            if (entry == large.end() || entry->second < count) {
                return false;
            }
        }
        return true;
    }

    static bool mana_leq(const ManaCost& left, const ManaCost& right) {
        return left.generic <= right.generic &&
               left.green <= right.green && left.red <= right.red &&
               left.blue <= right.blue && left.white <= right.white;
    }

    // (card -> total, tapped) census over lands or artifacts.
    template <typename Permanent>
    static std::map<CardId, std::pair<int, int>> tap_census(
        const std::vector<Permanent>& permanents) {
        std::map<CardId, std::pair<int, int>> census;
        for (const auto& permanent : permanents) {
            auto& entry = census[permanent.card];
            entry.first += 1;
            entry.second += permanent.tapped ? 1 : 0;
        }
        return census;
    }

    template <typename Permanent>
    static bool same_permanents_allowing_extra_taps(
        const std::vector<Permanent>& action_side,
        const std::vector<Permanent>& pass_side, bool allow_extra_taps) {
        const auto action_census = tap_census(action_side);
        const auto pass_census = tap_census(pass_side);
        if (action_census.size() != pass_census.size()) {
            return false;
        }
        for (const auto& [card, totals] : action_census) {
            const auto entry = pass_census.find(card);
            if (entry == pass_census.end() ||
                entry->second.first != totals.first) {
                return false;
            }
            if (allow_extra_taps ? totals.second < entry->second.second
                                 : totals.second != entry->second.second) {
                return false;
            }
        }
        return true;
    }

    static const CreaturePermanent* creature_by_id(
        const std::vector<CreaturePermanent>& creatures, PermanentId id) {
        for (const auto& creature : creatures) {
            if (creature.id == id) {
                return &creature;
            }
        }
        return nullptr;
    }

    // Creature-list comparison keyed by permanent id. `bonus_rule`:
    // 0 = temporary bonuses must be equal, +1 = action side may be higher.
    static bool same_creatures(
        const std::vector<CreaturePermanent>& action_side,
        const std::vector<CreaturePermanent>& pass_side, int bonus_rule) {
        if (action_side.size() != pass_side.size()) {
            return false;
        }
        for (const auto& creature : action_side) {
            const auto* other = creature_by_id(pass_side, creature.id);
            if (other == nullptr || other->card != creature.card ||
                other->tapped != creature.tapped ||
                other->summoning_sick != creature.summoning_sick ||
                other->damage != creature.damage ||
                other->exile_on_death_this_turn !=
                    creature.exile_on_death_this_turn) {
                return false;
            }
            if (bonus_rule == 0) {
                if (creature.temporary_power_bonus !=
                        other->temporary_power_bonus ||
                    creature.temporary_toughness_bonus !=
                        other->temporary_toughness_bonus) {
                    return false;
                }
            } else {
                if (creature.temporary_power_bonus <
                        other->temporary_power_bonus ||
                    creature.temporary_toughness_bonus <
                        other->temporary_toughness_bonus) {
                    return false;
                }
            }
        }
        return true;
    }

    static bool no_upside_versus_pass(
        std::size_t actor,
        const ResolvedPriorityActionConsequence& action_settled,
        const ResolvedPriorityActionConsequence& pass_settled) {
        if (action_settled.terminal || pass_settled.terminal) {
            return false;
        }
        const GameState& acted = action_settled.state;
        const GameState& passed = pass_settled.state;
        if (acted.stack != passed.stack ||
            acted.extra_turns_pending != passed.extra_turns_pending ||
            acted.failed_draw != passed.failed_draw) {
            return false;
        }
        const std::size_t opponent = 1 - actor;
        const PlayerState& acted_self = acted.players[actor];
        const PlayerState& passed_self = passed.players[actor];
        const PlayerState& acted_opponent = acted.players[opponent];
        const PlayerState& passed_opponent = passed.players[opponent];

        // Opponent: identical apart from possibly raised temporary
        // bonuses on their creatures.
        if (acted_self.channel_active != passed_self.channel_active ||
            acted_opponent.channel_active !=
                passed_opponent.channel_active ||
            acted_opponent.life != passed_opponent.life ||
            acted_opponent.hand.size() != passed_opponent.hand.size() ||
            acted_opponent.library.size() !=
                passed_opponent.library.size() ||
            counted(acted_opponent.graveyard) !=
                counted(passed_opponent.graveyard) ||
            counted(acted_opponent.exile) !=
                counted(passed_opponent.exile) ||
            counted(acted_opponent.enchantments) !=
                counted(passed_opponent.enchantments) ||
            acted_opponent.mana_pool != passed_opponent.mana_pool ||
            acted_opponent.land_played_this_turn !=
                passed_opponent.land_played_this_turn ||
            !same_permanents_allowing_extra_taps(
                acted_opponent.lands, passed_opponent.lands, false) ||
            !same_permanents_allowing_extra_taps(
                acted_opponent.artifacts, passed_opponent.artifacts,
                false) ||
            !same_creatures(acted_opponent.creatures,
                            passed_opponent.creatures, +1)) {
            return false;
        }

        // Actor: equal or strictly worse in every dimension.
        return acted_self.life <= passed_self.life &&
               acted_self.library.size() <= passed_self.library.size() &&
               acted_self.land_played_this_turn ==
                   passed_self.land_played_this_turn &&
               counted(acted_self.enchantments) ==
                   counted(passed_self.enchantments) &&
               counted(acted_self.exile) == counted(passed_self.exile) &&
               multiset_subset(counted(acted_self.hand),
                               counted(passed_self.hand)) &&
               multiset_subset(counted(passed_self.graveyard),
                               counted(acted_self.graveyard)) &&
               mana_leq(acted_self.mana_pool, passed_self.mana_pool) &&
               same_permanents_allowing_extra_taps(
                   acted_self.lands, passed_self.lands, true) &&
               same_permanents_allowing_extra_taps(
                   acted_self.artifacts, passed_self.artifacts, true) &&
               same_creatures(acted_self.creatures, passed_self.creatures,
                              0);
    }

    // True when the action's settled consequence differs from passing only
    // by raised temporary bonuses on the actor's own creatures (plus the
    // actor's spent resources). Such a pump has no upside when it provably
    // expires unused: in the actor's second main, or when none of the
    // pumped creatures could legally attack this turn. Bonuses vanish at
    // the end of the actor's own turn, so they can never help defense.
    static bool own_pump_only_versus_pass(
        std::size_t actor,
        const ResolvedPriorityActionConsequence& action_settled,
        const ResolvedPriorityActionConsequence& pass_settled,
        std::vector<PermanentId>* pumped) {
        if (action_settled.terminal || pass_settled.terminal) {
            return false;
        }
        const GameState& acted = action_settled.state;
        const GameState& passed = pass_settled.state;
        if (acted.stack != passed.stack ||
            acted.players[1 - actor] != passed.players[1 - actor]) {
            return false;
        }
        const PlayerState& acted_self = acted.players[actor];
        const PlayerState& passed_self = passed.players[actor];
        if (acted_self.life != passed_self.life ||
            acted_self.library.size() != passed_self.library.size() ||
            acted_self.creatures.size() != passed_self.creatures.size()) {
            return false;
        }
        for (const auto& creature : acted_self.creatures) {
            const auto* other =
                creature_by_id(passed_self.creatures, creature.id);
            if (other == nullptr || other->card != creature.card ||
                other->tapped != creature.tapped ||
                other->summoning_sick != creature.summoning_sick ||
                other->damage != creature.damage) {
                return false;
            }
            if (creature.temporary_power_bonus <
                    other->temporary_power_bonus ||
                creature.temporary_toughness_bonus <
                    other->temporary_toughness_bonus) {
                return false;
            }
            if (creature.temporary_power_bonus >
                    other->temporary_power_bonus ||
                creature.temporary_toughness_bonus >
                    other->temporary_toughness_bonus) {
                pumped->push_back(creature.id);
            }
        }
        return !pumped->empty();
    }

    static bool creature_on_battlefield(const GameState& state,
                                        std::size_t player,
                                        PermanentId id,
                                        bool require_untapped) {
        for (const auto& creature : state.players[player].creatures) {
            if (creature.id == id) {
                return !require_untapped || !creature.tapped;
            }
        }
        return false;
    }

    // Resolves the pending combat on `state` the way the engine will after
    // the response window: dead participants leave combat and a block the
    // engine would reject is dropped rather than failing the combat.
    void resolve_pending_combat(GameState& state) const {
        if (!pending_combat.has_value()) {
            return;
        }
        resolve_declared_combat(state, *pending_combat);
    }

    static void resolve_declared_combat(GameState& state,
                                        const PendingCombat& declared) {
        const std::size_t attacking_player = state.active_player;
        const std::size_t defender = 1 - attacking_player;
        std::vector<PermanentId> attackers;
        for (const PermanentId attacker : declared.attackers) {
            if (creature_on_battlefield(state, attacking_player, attacker,
                                        false)) {
                attackers.push_back(attacker);
            }
        }
        if (attackers.empty()) {
            return;
        }
        std::vector<std::pair<PermanentId, PermanentId>> blocks;
        for (const auto& block : declared.blocks) {
            const bool attacker_fighting =
                std::find(attackers.begin(), attackers.end(),
                          block.first) != attackers.end();
            if (attacker_fighting &&
                creature_on_battlefield(state, defender, block.second,
                                        true)) {
                blocks.push_back(block);
            }
        }
        while (true) {
            GameState trial = state;
            if (resolve_combat(trial, attacking_player, attackers,
                               blocks)) {
                state = std::move(trial);
                return;
            }
            if (blocks.empty()) {
                return;
            }
            blocks.pop_back();
        }
    }

    // Terminal-aware value of a full state from `perspective`. Terminal
    // detection mirrors the engine's life/failed-draw rules so combat and
    // consequence simulations score wins exactly.
    double value_for(const GameState& state, std::size_t perspective,
                     TurnPhase phase) const {
        const bool self_dead = state.players[perspective].life <= 0 ||
                               state.failed_draw[perspective];
        const bool opponent_dead =
            state.players[1 - perspective].life <= 0 ||
            state.failed_draw[1 - perspective];
        if (self_dead || opponent_dead) {
            if (self_dead && opponent_dead) {
                return 0.5;
            }
            return self_dead ? 0.0 : 1.0;
        }
        return net->value(spz_features_for(
            net->input_count(),
            observe_game_state(state, perspective), decks, phase));
    }

    // ------------------------------------------------------------------
    // Rollout lookahead: deterministic greedy forward play on a
    // determinized world until the start of `seat`'s next turn.

    static constexpr int kRolloutDecisionBudget = 300;

    // Discounts an exact terminal outcome by the turns elapsed since the
    // lookahead root; the identity at gamma 1.0.
    double discount_outcome(double value, std::size_t root_turn,
                            std::size_t end_turn) const {
        if (config.gamma_per_turn >= 1.0 || value == 0.5) {
            return value;
        }
        const double elapsed = end_turn > root_turn
                                   ? static_cast<double>(end_turn -
                                                         root_turn)
                                   : 0.0;
        return 0.5 + (value - 0.5) *
                         std::pow(config.gamma_per_turn, elapsed);
    }

    std::optional<double> terminal_value(const GameState& state) const {
        const bool self_dead = state.players[seat].life <= 0 ||
                               state.failed_draw[seat];
        const bool opponent_dead = state.players[1 - seat].life <= 0 ||
                                   state.failed_draw[1 - seat];
        if (!self_dead && !opponent_dead) {
            return std::nullopt;
        }
        if (self_dead && opponent_dead) {
            return 0.5;
        }
        return self_dead ? 0.0 : 1.0;
    }

    PriorityAction choose_rollout_priority(
        const GameState& state, const PriorityState& priority,
        bool sorcery, TurnPhase phase,
        const PendingCombat* pending = nullptr) const {
        const auto actions =
            legal_priority_actions(state, priority.player, sorcery);
        if (actions.size() <= 1) {
            return actions.empty() ? PriorityAction::pass() : actions[0];
        }
        const auto pass_settled = resolve_priority_action_consequence(
            state, priority.player, sorcery,
            std::min(priority.consecutive_passes, 1),
            PriorityAction::pass());
        double best_value = -std::numeric_limits<double>::infinity();
        PriorityAction chosen = actions[0];
        for (const PriorityAction& action : actions) {
            const auto consequence = resolve_priority_action_consequence(
                state, priority.player, sorcery,
                std::min(priority.consecutive_passes, 1), action);
            if (!consequence.has_value()) {
                continue;
            }
            if (action.kind != PriorityActionKind::Pass &&
                pass_settled.has_value() &&
                no_upside_versus_pass(priority.player, *consequence,
                                      *pass_settled)) {
                continue;
            }
            double value = 0.0;
            if (consequence->terminal) {
                value = consequence->winner == -1
                            ? 0.5
                            : (static_cast<std::size_t>(
                                   consequence->winner) ==
                                       priority.player
                                   ? 1.0
                                   : 0.0);
            } else if (pending != nullptr) {
                // Post-blockers response: judge each candidate through
                // the declared combat, exactly as the root does, so a
                // held trick's payoff (flipping the declared trade) is
                // visible inside imagined futures too.
                GameState post_combat = consequence->state;
                resolve_declared_combat(post_combat, *pending);
                if (const auto terminal = terminal_value(post_combat)) {
                    value = priority.player == seat
                                ? *terminal
                                : 1.0 - *terminal;
                } else {
                    value = value_for(post_combat, priority.player,
                                      TurnPhase::SecondMain);
                }
            } else {
                value = value_for(consequence->state, priority.player,
                                  phase);
            }
            if (value > best_value) {
                best_value = value;
                chosen = action;
            }
        }
        return chosen;
    }

    // Plays one priority window to completion with the greedy mirror
    // policy. Returns the terminal value when the game ends inside it.
    std::optional<double> rollout_window(
        GameState& state, TurnPhase phase, bool sorcery,
        PriorityState priority, int& budget,
        const PendingCombat* pending = nullptr) const {
        while (true) {
            PriorityAction action = PriorityAction::pass();
            if (budget > 0) {
                budget -= 1;
                action = choose_rollout_priority(state, priority, sorcery,
                                                 phase, pending);
            }
            if (action.kind != PriorityActionKind::Pass &&
                !apply_priority_action(state, priority.player, action,
                                       sorcery)) {
                action = PriorityAction::pass();
            }
            if (action.kind == PriorityActionKind::Pass) {
                const PriorityPassResult pass =
                    pass_priority(state, priority);
                if (pass == PriorityPassResult::Passed) {
                    continue;
                }
                if (pass == PriorityPassResult::WindowEnded) {
                    return std::nullopt;
                }
                if (const auto terminal = terminal_value(state)) {
                    return terminal;
                }
                continue;
            }
            priority.consecutive_passes = 0;
        }
    }

    // Myopic greedy attack subset for the current world; shared by rollout
    // combat and root candidate generation.
    std::vector<PermanentId> greedy_attack_set(
        const GameState& state, std::size_t attacking_player,
        const std::vector<PermanentId>& eligible) const {
        const auto evaluate_set =
            [&](const std::vector<PermanentId>& attack_set) -> double {
            if (attack_set.empty()) {
                return value_for(state, attacking_player,
                                 TurnPhase::SecondMain);
            }
            const auto blocks = greedy_blocks(state, attacking_player,
                                              attack_set, nullptr);
            GameState simulation = state;
            if (!resolve_combat(simulation, attacking_player, attack_set,
                                blocks)) {
                return kIllegalScore;
            }
            return value_for(simulation, attacking_player,
                             TurnPhase::SecondMain);
        };
        std::vector<PermanentId> chosen;
        std::vector<PermanentId> remaining = eligible;
        double current = evaluate_set(chosen);
        while (!remaining.empty()) {
            double best_value = current + kImprovementMargin;
            std::size_t best_index = remaining.size();
            for (std::size_t index = 0; index < remaining.size();
                 ++index) {
                auto trial = chosen;
                trial.push_back(remaining[index]);
                const double trial_value = evaluate_set(trial);
                if (trial_value > best_value) {
                    best_value = trial_value;
                    best_index = index;
                }
            }
            if (best_index == remaining.size()) {
                break;
            }
            chosen.push_back(remaining[best_index]);
            remaining.erase(remaining.begin() +
                            static_cast<std::ptrdiff_t>(best_index));
            current = best_value;
        }
        return chosen;
    }

    std::optional<double> rollout_combat_after_beginning(
        GameState& state, int& budget) const {
        const std::size_t attacking_player = state.active_player;
        const auto eligible =
            old_school::legal_attackers(state, attacking_player);
        if (!eligible.empty()) {
            const auto attack_set =
                greedy_attack_set(state, attacking_player, eligible);
            if (!attack_set.empty()) {
                const auto blocks = greedy_blocks(
                    state, attacking_player, attack_set, nullptr);
                // Mirror the engine: both players may respond after the
                // blocks are declared, then combat is sanitized and
                // resolved with whatever survived. Responders see the
                // declaration, so held tricks price in the real trade.
                const PendingCombat declared{attack_set, blocks};
                if (const auto terminal = rollout_window(
                        state, TurnPhase::DeclareBlockers, false,
                        {attacking_player, 0}, budget, &declared)) {
                    return terminal;
                }
                resolve_declared_combat(state, declared);
                if (const auto terminal = terminal_value(state)) {
                    return terminal;
                }
            }
        }
        return rollout_window(state, TurnPhase::EndCombat, false,
                              {state.active_player, 0}, budget);
    }

    std::optional<double> rollout_combat(GameState& state,
                                         int& budget) const {
        if (const auto terminal =
                rollout_window(state, TurnPhase::BeginCombat, false,
                               {state.active_player, 0}, budget)) {
            return terminal;
        }
        return rollout_combat_after_beginning(state, budget);
    }

    // Greedy cleanup discards evaluated on the full state for `player`.
    std::vector<std::size_t> greedy_discards(const GameState& state,
                                             std::size_t player,
                                             std::size_t excess) const {
        const auto& hand = state.players[player].hand;
        std::vector<std::size_t> chosen;
        std::vector<bool> discarded(hand.size(), false);
        for (std::size_t round = 0; round < excess && round < hand.size();
             ++round) {
            double best_value = -std::numeric_limits<double>::infinity();
            std::size_t best_index = hand.size();
            for (std::size_t index = 0; index < hand.size(); ++index) {
                if (discarded[index]) {
                    continue;
                }
                GameState trial = state;
                std::vector<CardId> remaining;
                for (std::size_t position = 0; position < hand.size();
                     ++position) {
                    if (!discarded[position] && position != index) {
                        remaining.push_back(hand[position]);
                    }
                }
                trial.players[player].hand = std::move(remaining);
                trial.players[player].graveyard.push_back(hand[index]);
                const double value =
                    value_for(trial, player, TurnPhase::SecondMain);
                if (value > best_value) {
                    best_value = value;
                    best_index = index;
                }
            }
            if (best_index == hand.size()) {
                break;
            }
            discarded[best_index] = true;
            chosen.push_back(best_index);
        }
        std::sort(chosen.begin(), chosen.end());
        return chosen;
    }

    void rollout_cleanup(GameState& state) const {
        const auto& hand = state.players[state.active_player].hand;
        std::vector<std::size_t> discards;
        if (hand.size() > kMaximumHandSize) {
            discards = greedy_discards(state, state.active_player,
                                       hand.size() - kMaximumHandSize);
        }
        cleanup_turn(state, state.active_player, discards);
    }

    // Completes the current turn from `entry_phase` (resuming its priority
    // window when applicable), then plays full turns until `seat` would
    // receive its next turn; evaluates the network at that boundary.
    // Combat-decision roots pass a combat phase and a state where combat
    // has already been resolved.
    double finish_turn_and_rollout(GameState state, TurnPhase entry_phase,
                                   PriorityState resume) const {
        const std::size_t root_turn = state.turn_number;
        const auto settle = [&](double value) {
            return discount_outcome(value, root_turn,
                                    state.turn_number);
        };
        if (const auto terminal = terminal_value(state)) {
            return settle(*terminal);
        }
        int budget = kRolloutDecisionBudget *
                     static_cast<int>(std::max<std::size_t>(
                         1, config.rollout_turn_cycles));
        const auto run_second_main =
            [&](GameState& current) -> std::optional<double> {
            return rollout_window(current, TurnPhase::SecondMain, true,
                                  {current.active_player, 0}, budget);
        };
        std::optional<double> terminal;
        switch (entry_phase) {
            case TurnPhase::FirstMain:
                terminal = rollout_window(state, TurnPhase::FirstMain,
                                          true, resume, budget);
                if (!terminal) {
                    terminal = rollout_combat(state, budget);
                }
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
            case TurnPhase::BeginCombat:
                terminal = rollout_window(state, TurnPhase::BeginCombat,
                                          false, resume, budget);
                if (!terminal) {
                    terminal =
                        rollout_combat_after_beginning(state, budget);
                }
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
            case TurnPhase::EndCombat:
                terminal = rollout_window(state, TurnPhase::EndCombat,
                                          false, resume, budget);
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
            case TurnPhase::SecondMain:
                terminal = rollout_window(state, TurnPhase::SecondMain,
                                          true, resume, budget);
                break;
            default:
                // Combat decision roots: combat already resolved.
                terminal = rollout_window(state, TurnPhase::EndCombat,
                                          false,
                                          {state.active_player, 0},
                                          budget);
                if (!terminal) {
                    terminal = run_second_main(state);
                }
                break;
        }
        if (terminal) {
            return settle(*terminal);
        }
        rollout_cleanup(state);

        // Play forward until `seat` has reached `rollout_turn_cycles` of
        // its own turn starts; deeper settings play the seat's turns too.
        // The guard bound absorbs Time Walk chains.
        std::size_t seat_turn_starts_remaining =
            std::max<std::size_t>(1, config.rollout_turn_cycles);
        const int guard_limit =
            static_cast<int>(6 * seat_turn_starts_remaining);
        for (int guard = 0; guard < guard_limit; ++guard) {
            if (state.turn_number >= 500) {
                break;
            }
            state.turn_number += 1;
            advance_turn_player(state);
            begin_turn(state, state.active_player);
            auto& active = state.players[state.active_player];
            if (active.library.empty()) {
                return settle(state.active_player == seat ? 0.0 : 1.0);
            }
            active.hand.push_back(active.library.back());
            active.library.pop_back();
            if (state.active_player == seat) {
                seat_turn_starts_remaining -= 1;
                if (seat_turn_starts_remaining == 0) {
                    return settle(
                        value_for(state, seat, TurnPhase::FirstMain));
                }
            }
            terminal = rollout_window(state, TurnPhase::FirstMain, true,
                                      {state.active_player, 0}, budget);
            if (!terminal) {
                terminal = rollout_combat(state, budget);
            }
            if (!terminal) {
                terminal = run_second_main(state);
            }
            if (terminal) {
                return settle(*terminal);
            }
            rollout_cleanup(state);
        }
        return settle(value_for(state, seat, TurnPhase::FirstMain));
    }

    // ------------------------------------------------------------------
    // ISMCTS: per-world PUCT trees over both players' priority decisions.
    //
    // Tree moves are priority actions (including Pass); combat declarations,
    // damage, discards, and turn structure advance through the same greedy
    // machinery the rollout uses. Every tree position is normalized to a
    // real decision point (two or more legal actions), a terminal, or the
    // value-net horizon at the deciding seat's future turn start.

    enum class TreeStage : std::uint8_t {
        Main1,
        BeginCombatWindow,
        DeclareBlockersWindow,
        EndCombatWindow,
        Main2,
    };

    struct TreePosition {
        GameState state;
        TreeStage stage = TreeStage::Main1;
        TurnPhase phase = TurnPhase::FirstMain;
        bool sorcery = false;
        PriorityState priority;
        std::optional<PendingCombat> declared;
        std::size_t seat_turn_starts_remaining = 1;
        std::optional<double> terminal;  // seat perspective
        bool horizon = false;
    };

    static TurnPhase stage_phase(TreeStage stage) {
        switch (stage) {
            case TreeStage::Main1:
                return TurnPhase::FirstMain;
            case TreeStage::BeginCombatWindow:
                return TurnPhase::BeginCombat;
            case TreeStage::DeclareBlockersWindow:
                return TurnPhase::DeclareBlockers;
            case TreeStage::EndCombatWindow:
                return TurnPhase::EndCombat;
            case TreeStage::Main2:
                return TurnPhase::SecondMain;
        }
        return TurnPhase::FirstMain;
    }

    void open_window(TreePosition& position, TreeStage stage) const {
        position.stage = stage;
        position.phase = stage_phase(stage);
        position.sorcery = stage == TreeStage::Main1 ||
                           stage == TreeStage::Main2;
        position.priority = {position.state.active_player, 0};
    }

    void set_terminal_from_state(TreePosition& position) const {
        position.terminal = terminal_value(position.state);
    }

    // Advances past the end of the current window to the next window,
    // terminal, or horizon, mirroring the engine turn structure.
    void advance_after_window_end(TreePosition& position) const {
        switch (position.stage) {
            case TreeStage::Main1: {
                open_window(position, TreeStage::BeginCombatWindow);
                return;
            }
            case TreeStage::BeginCombatWindow: {
                const std::size_t attacker =
                    position.state.active_player;
                const auto eligible = old_school::legal_attackers(
                    position.state, attacker);
                std::vector<PermanentId> attack_set;
                if (!eligible.empty()) {
                    attack_set = greedy_attack_set(position.state,
                                                   attacker, eligible);
                }
                if (attack_set.empty()) {
                    position.declared.reset();
                    open_window(position, TreeStage::EndCombatWindow);
                    return;
                }
                const auto blocks = greedy_blocks(
                    position.state, attacker, attack_set, nullptr);
                position.declared = PendingCombat{attack_set, blocks};
                open_window(position, TreeStage::DeclareBlockersWindow);
                return;
            }
            case TreeStage::DeclareBlockersWindow: {
                if (position.declared.has_value()) {
                    resolve_declared_combat(position.state,
                                            *position.declared);
                    position.declared.reset();
                    set_terminal_from_state(position);
                    if (position.terminal.has_value()) {
                        return;
                    }
                }
                open_window(position, TreeStage::EndCombatWindow);
                return;
            }
            case TreeStage::EndCombatWindow: {
                open_window(position, TreeStage::Main2);
                return;
            }
            case TreeStage::Main2: {
                rollout_cleanup(position.state);
                if (position.state.turn_number >= 500) {
                    position.horizon = true;
                    return;
                }
                position.state.turn_number += 1;
                advance_turn_player(position.state);
                begin_turn(position.state,
                           position.state.active_player);
                auto& active = position.state.players[
                    position.state.active_player];
                if (active.library.empty()) {
                    position.terminal =
                        position.state.active_player == seat ? 0.0 : 1.0;
                    return;
                }
                active.hand.push_back(active.library.back());
                active.library.pop_back();
                if (position.state.active_player == seat) {
                    position.seat_turn_starts_remaining -= 1;
                    if (position.seat_turn_starts_remaining == 0) {
                        position.phase = TurnPhase::FirstMain;
                        position.horizon = true;
                        return;
                    }
                }
                open_window(position, TreeStage::Main1);
                return;
            }
        }
    }

    // Auto-plays forced passes until the position is a decision point with
    // at least two legal actions, a terminal, or the horizon.
    // Normalizes to the deciding seat's next real choice. Forced passes
    // auto-play, and the opponent's decisions are played by the greedy
    // policy rather than branching the tree: a determinized tree that lets
    // the opponent respond optimally per world plays them as clairvoyant,
    // which measurably poisons the search.
    void normalize_position(TreePosition& position) const {
        for (int guard = 0; guard < 400; ++guard) {
            if (position.terminal.has_value() || position.horizon) {
                return;
            }
            const auto actions = legal_priority_actions(
                position.state, position.priority.player,
                position.sorcery);
            PriorityAction forced = PriorityAction::pass();
            if (actions.size() > 1) {
                if (position.priority.player == seat) {
                    return;
                }
                forced = choose_rollout_priority(
                    position.state, position.priority,
                    position.sorcery, position.phase);
            }
            if (forced.kind != PriorityActionKind::Pass &&
                apply_priority_action(position.state,
                                      position.priority.player, forced,
                                      position.sorcery)) {
                position.priority.consecutive_passes = 0;
                continue;
            }
            const PriorityPassResult pass =
                pass_priority(position.state, position.priority);
            if (pass == PriorityPassResult::WindowEnded) {
                advance_after_window_end(position);
            } else if (pass ==
                       PriorityPassResult::StackObjectResolved) {
                set_terminal_from_state(position);
            }
        }
        position.horizon = true;
    }

    TreePosition advance_with_action(const TreePosition& parent,
                                     const PriorityAction& action) const {
        TreePosition next = parent;
        if (action.kind == PriorityActionKind::Pass ||
            !apply_priority_action(next.state, next.priority.player,
                                   action, next.sorcery)) {
            const PriorityPassResult pass =
                pass_priority(next.state, next.priority);
            if (pass == PriorityPassResult::WindowEnded) {
                advance_after_window_end(next);
            } else if (pass ==
                       PriorityPassResult::StackObjectResolved) {
                set_terminal_from_state(next);
            }
        } else {
            next.priority.consecutive_passes = 0;
        }
        normalize_position(next);
        return next;
    }

    // Champion-strength leaf evaluation: greedy playout from a normalized
    // tree position to the horizon, using the same window/combat machinery
    // as the deployed rollout.
    double playout_from_position(TreePosition position) const {
        int budget = kRolloutDecisionBudget;
        for (int guard = 0; guard < 40; ++guard) {
            if (position.terminal.has_value()) {
                return *position.terminal;
            }
            if (position.horizon) {
                return value_for(position.state, seat, position.phase);
            }
            const auto terminal = rollout_window(
                position.state, position.phase, position.sorcery,
                position.priority, budget);
            if (terminal.has_value()) {
                return *terminal;
            }
            advance_after_window_end(position);
        }
        return value_for(position.state, seat, position.phase);
    }

    struct IsmctsNode {
        TreePosition position;
        bool expanded = false;
        double state_value = 0.5;  // seat perspective, net estimate
        std::vector<PriorityAction> actions;
        std::vector<double> priors;
        std::vector<int> child_visits;
        std::vector<double> child_value_sums;  // seat perspective
        std::vector<std::unique_ptr<IsmctsNode>> children;
    };

    static void softmax_priors(std::vector<double>& scores) {
        constexpr double kTemperature = 0.04;
        double best = -std::numeric_limits<double>::infinity();
        for (const double score : scores) {
            best = std::max(best, score);
        }
        double total = 0.0;
        for (double& score : scores) {
            score = std::exp((score - best) / kTemperature);
            total += score;
        }
        for (double& score : scores) {
            score /= total;
        }
    }

    // Learned priors: softmax over policy-net logits for the actor at the
    // given position. Empty when no policy net is attached.
    std::vector<double> learned_priors(
        const GameState& state, TurnPhase phase, std::size_t actor,
        const std::vector<PriorityAction>& actions) const {
        if (policy_net == nullptr) {
            return {};
        }
        const auto state_row = spz_features_for(
            net->input_count(),
            observe_game_state(state, actor), decks, phase);
        std::vector<double> logits;
        logits.reserve(actions.size());
        for (const PriorityAction& action : actions) {
            logits.push_back(policy_net->logit(
                state_row, spz_action_features(action, actor, state)));
        }
        double best = -std::numeric_limits<double>::infinity();
        for (const double value : logits) {
            best = std::max(best, value);
        }
        double total = 0.0;
        for (double& value : logits) {
            value = std::exp(value - best);
            total += value;
        }
        for (double& value : logits) {
            value /= total;
        }
        return logits;
    }

    // Expands `node`: legal actions minus no-upside waste, myopic priors
    // from the acting player's perspective, and the node's own net value.
    // Returns the seat-perspective estimate for backpropagation.
    double expand_node(IsmctsNode& node) const {
        const TreePosition& position = node.position;
        node.expanded = true;
        node.state_value = playout_from_position(position);
        auto actions = legal_priority_actions(
            position.state, position.priority.player,
            position.sorcery);
        const auto pass_settled = resolve_priority_action_consequence(
            position.state, position.priority.player, position.sorcery,
            std::min(position.priority.consecutive_passes, 1),
            PriorityAction::pass());
        std::vector<double> scores;
        for (const PriorityAction& action : actions) {
            const auto consequence = resolve_priority_action_consequence(
                position.state, position.priority.player,
                position.sorcery,
                std::min(position.priority.consecutive_passes, 1),
                action);
            if (!consequence.has_value()) {
                continue;
            }
            if (action.kind != PriorityActionKind::Pass &&
                pass_settled.has_value() &&
                no_upside_versus_pass(position.priority.player,
                                      *consequence, *pass_settled)) {
                continue;
            }
            double score = 0.0;
            if (consequence->terminal) {
                score = consequence->winner == -1
                            ? 0.5
                            : (static_cast<std::size_t>(
                                   consequence->winner) ==
                                       position.priority.player
                                   ? 1.0
                                   : 0.0);
            } else {
                score = value_for(consequence->state,
                                  position.priority.player,
                                  position.phase);
            }
            node.actions.push_back(action);
            scores.push_back(score);
        }
        if (node.actions.empty()) {
            node.actions.push_back(PriorityAction::pass());
            scores.push_back(0.5);
        }
        auto priors = learned_priors(position.state, position.phase,
                                     position.priority.player,
                                     node.actions);
        if (priors.empty()) {
            softmax_priors(scores);
            priors = std::move(scores);
        }
        node.priors = std::move(priors);
        node.child_visits.assign(node.actions.size(), 0);
        node.child_value_sums.assign(node.actions.size(), 0.0);
        node.children.resize(node.actions.size());
        return node.state_value;
    }

    std::size_t puct_select(const IsmctsNode& node) const {
        constexpr double kPuct = 1.5;
        constexpr double kFirstPlayPenalty = 0.1;
        const std::size_t actor = node.position.priority.player;
        int total_visits = 0;
        for (const int visits : node.child_visits) {
            total_visits += visits;
        }
        const double parent_actor_value =
            actor == seat ? node.state_value : 1.0 - node.state_value;
        const double exploration_scale =
            std::sqrt(static_cast<double>(total_visits) + 1.0);
        double best = -std::numeric_limits<double>::infinity();
        std::size_t best_index = 0;
        for (std::size_t index = 0; index < node.actions.size();
             ++index) {
            const int visits = node.child_visits[index];
            double actor_q = parent_actor_value - kFirstPlayPenalty;
            if (visits > 0) {
                const double seat_q =
                    node.child_value_sums[index] / visits;
                actor_q = actor == seat ? seat_q : 1.0 - seat_q;
            }
            const double bound =
                actor_q + kPuct * node.priors[index] *
                              exploration_scale / (1.0 + visits);
            if (bound > best) {
                best = bound;
                best_index = index;
            }
        }
        return best_index;
    }

    // One tree simulation; returns a seat-perspective value.
    double ismcts_simulate(IsmctsNode& node) const {
        if (node.position.terminal.has_value()) {
            return *node.position.terminal;
        }
        if (node.position.horizon) {
            if (!node.expanded) {
                node.expanded = true;
                node.state_value = value_for(node.position.state, seat,
                                             node.position.phase);
            }
            return node.state_value;
        }
        if (!node.expanded) {
            return expand_node(node);
        }
        const std::size_t choice = puct_select(node);
        if (node.children[choice] == nullptr) {
            node.children[choice] = std::make_unique<IsmctsNode>();
            node.children[choice]->position = advance_with_action(
                node.position, node.actions[choice]);
        }
        const double value = ismcts_simulate(*node.children[choice]);
        node.child_visits[choice] += 1;
        node.child_value_sums[choice] += value;
        return value;
    }

    // Root search: one tree per determinized world with the engine's real
    // action list at the root; visits are pooled across worlds.
    std::size_t ismcts_choose(
        const std::vector<PriorityAction>& actions,
        const std::vector<bool>& dominated,
        const std::vector<double>& myopic_totals,
        const std::vector<GameState>& worlds, TurnPhase phase,
        bool sorcery_actions) const {
        std::vector<double> root_scores;
        std::vector<std::size_t> root_action_indices;
        for (std::size_t index = 0; index < actions.size(); ++index) {
            if (!dominated[index]) {
                root_action_indices.push_back(index);
                root_scores.push_back(
                    myopic_totals[index] /
                    static_cast<double>(worlds.size()));
            }
        }
        if (root_action_indices.size() <= 1) {
            return root_action_indices.empty()
                       ? 0
                       : root_action_indices.front();
        }
        std::vector<PriorityAction> root_actions;
        for (const std::size_t index : root_action_indices) {
            root_actions.push_back(actions[index]);
        }
        std::vector<double> priors = learned_priors(
            worlds.front(), phase, seat, root_actions);
        if (priors.empty()) {
            priors = root_scores;
            softmax_priors(priors);
        }

        std::vector<std::unique_ptr<IsmctsNode>> roots;
        for (const GameState& world : worlds) {
            auto root = std::make_unique<IsmctsNode>();
            root->position.state = world;
            root->position.seat_turn_starts_remaining =
                std::max<std::size_t>(1, config.rollout_turn_cycles);
            switch (phase) {
                case TurnPhase::FirstMain:
                    root->position.stage = TreeStage::Main1;
                    break;
                case TurnPhase::BeginCombat:
                    root->position.stage =
                        TreeStage::BeginCombatWindow;
                    break;
                case TurnPhase::EndCombat:
                    root->position.stage = TreeStage::EndCombatWindow;
                    break;
                default:
                    root->position.stage = TreeStage::Main2;
                    break;
            }
            root->position.phase = phase;
            root->position.sorcery = sorcery_actions;
            root->position.priority = {seat, 0};
            root->expanded = true;
            root->state_value =
                value_for(world, seat, phase);
            for (const std::size_t index : root_action_indices) {
                root->actions.push_back(actions[index]);
            }
            root->priors = priors;
            root->child_visits.assign(root->actions.size(), 0);
            root->child_value_sums.assign(root->actions.size(), 0.0);
            root->children.resize(root->actions.size());
            roots.push_back(std::move(root));
        }

        const std::size_t iterations =
            std::max<std::size_t>(config.ismcts_iterations,
                                  root_action_indices.size() * 2);
        for (std::size_t iteration = 0; iteration < iterations;
             ++iteration) {
            ismcts_simulate(*roots[iteration % roots.size()]);
        }

        if (recorder != nullptr) {
            SpzPolicySample sample;
            sample.state = spz_features_for(
                net->input_count(),
                observe_game_state(worlds.front(), seat), decks, phase);
            float visit_total = 0.0f;
            for (std::size_t slot = 0;
                 slot < root_action_indices.size(); ++slot) {
                sample.actions.push_back(spz_action_features(
                    root_actions[slot], seat, worlds.front()));
                float visits = 0.0f;
                for (const auto& root : roots) {
                    visits += static_cast<float>(
                        root->child_visits[slot]);
                }
                sample.visits.push_back(visits);
                visit_total += visits;
            }
            if (visit_total > 0.0f) {
                for (float& visits : sample.visits) {
                    visits /= visit_total;
                }
                recorder->policy_samples.push_back(std::move(sample));
            }
        }

        std::size_t best_root_slot = 0;
        long best_visits = -1;
        double best_value = -std::numeric_limits<double>::infinity();
        for (std::size_t slot = 0; slot < root_action_indices.size();
             ++slot) {
            long visits = 0;
            double value_sum = 0.0;
            for (const auto& root : roots) {
                visits += root->child_visits[slot];
                value_sum += root->child_value_sums[slot];
            }
            const double mean_value =
                visits > 0 ? value_sum / visits
                           : -std::numeric_limits<double>::infinity();
            if (visits > best_visits ||
                (visits == best_visits && mean_value > best_value)) {
                best_visits = visits;
                best_value = mean_value;
                best_root_slot = slot;
            }
        }
        return root_action_indices[best_root_slot];
    }

    void record(const PlayerObservation& observation, TurnPhase phase) {
        if (recorder != nullptr) {
            recorder->feature_rows.push_back(spz_features_for(
                net->input_count(), observation, decks, phase));
            recorder->feature_turns.push_back(observation.turn_number);
        }
    }

    void record_state(const GameState& state, TurnPhase phase) {
        if (recorder != nullptr &&
            state.players[0].life > 0 && state.players[1].life > 0) {
            recorder->feature_rows.push_back(spz_features_for(
                net->input_count(),
                observe_game_state(state, seat), decks, phase));
            recorder->feature_turns.push_back(state.turn_number);
        }
    }

    std::size_t choose_priority_action(
        const PlayerObservation& observation, TurnPhase phase,
        const std::vector<PriorityAction>& actions) {
        record(observation, phase);
        if (actions.size() <= 1) {
            return 0;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        const bool sorcery_actions =
            (phase == TurnPhase::FirstMain ||
             phase == TurnPhase::SecondMain) &&
            observation.active_player == observation.observer;
        // Rules-only prune: never take an action whose settled consequence
        // is the Pass consequence minus resources. Placeholder hidden zones
        // are safe here — any action that reveals hidden cards settles to a
        // different observation and is therefore retained.
        std::vector<bool> dominated(actions.size(), false);
        if (config.pass_dominance_prune) {
            const auto dominance = diagnose_value_pass_dominance(
                reconstructed, seat, sorcery_actions, phase, 0);
            for (const auto& entry : dominance.actions) {
                if (!entry.strictly_dominated_by_pass) {
                    continue;
                }
                for (std::size_t index = 0; index < actions.size();
                     ++index) {
                    if (actions[index] == entry.action) {
                        dominated[index] = true;
                    }
                }
            }
            const auto pass_settled = resolve_priority_action_consequence(
                reconstructed, seat, sorcery_actions, 0,
                PriorityAction::pass());
            if (pass_settled.has_value()) {
                for (std::size_t index = 0; index < actions.size();
                     ++index) {
                    if (dominated[index] ||
                        actions[index].kind ==
                            PriorityActionKind::Pass) {
                        continue;
                    }
                    const auto settled =
                        resolve_priority_action_consequence(
                            reconstructed, seat, sorcery_actions, 0,
                            actions[index]);
                    if (!settled.has_value()) {
                        continue;
                    }
                    if (no_upside_versus_pass(seat, *settled,
                                              *pass_settled)) {
                        dominated[index] = true;
                        continue;
                    }
                    std::vector<PermanentId> pumped;
                    if (own_pump_only_versus_pass(seat, *settled,
                                                  *pass_settled,
                                                  &pumped)) {
                        const bool my_turn =
                            observation.active_player == seat;
                        if (!my_turn) {
                            continue;  // defensive pre-pump stays legal
                        }
                        if (phase == TurnPhase::SecondMain) {
                            dominated[index] = true;
                            continue;
                        }
                        // Before combat on the actor's own turn the pump
                        // only matters if a pumped creature could attack.
                        const auto eligible = old_school::legal_attackers(
                            settled->state, seat);
                        bool pumped_can_attack = false;
                        for (const PermanentId id : pumped) {
                            if (std::find(eligible.begin(),
                                          eligible.end(),
                                          id) != eligible.end()) {
                                pumped_can_attack = true;
                            }
                        }
                        if (!pumped_can_attack &&
                            phase != TurnPhase::DeclareBlockers) {
                            dominated[index] = true;
                        }
                    }
                }
            }
        }
        if (explore()) {
            std::vector<std::size_t> retained;
            for (std::size_t index = 0; index < actions.size();
                 ++index) {
                if (!dominated[index]) {
                    retained.push_back(index);
                }
            }
            if (retained.empty()) {
                return 0;
            }
            std::uniform_int_distribution<std::size_t> pick(
                0, retained.size() - 1);
            return retained[pick(rng)];
        }
        const std::size_t worlds = std::max<std::size_t>(1, config.worlds);
        std::vector<GameState> sampled_worlds;
        sampled_worlds.reserve(worlds);
        for (std::size_t world = 0; world < worlds; ++world) {
            sampled_worlds.push_back(sample_determinization(
                reconstructed, decks, seat, rng()));
        }
        const bool combat_response_window =
            phase == TurnPhase::DeclareBlockers &&
            pending_combat.has_value();
        const auto score_action =
            [&](const GameState& sampled, const PriorityAction& action,
                bool with_rollout) -> double {
            const auto consequence = resolve_priority_action_consequence(
                sampled, seat, sorcery_actions, 0, action);
            if (!consequence.has_value()) {
                return kIllegalScore;
            }
            if (consequence->terminal) {
                return consequence->winner == -1
                           ? 0.5
                           : (static_cast<std::size_t>(
                                  consequence->winner) == seat
                                  ? 1.0
                                  : 0.0);
            }
            if (combat_response_window) {
                // Post-blockers response: play the declared combat out on
                // the resolved consequence before judging the position.
                GameState post_combat = consequence->state;
                resolve_pending_combat(post_combat);
                if (with_rollout) {
                    const std::size_t combat_active =
                        post_combat.active_player;
                    return finish_turn_and_rollout(
                        std::move(post_combat), TurnPhase::DamageOrder,
                        {combat_active, 0});
                }
                return value_for(post_combat, seat,
                                 TurnPhase::SecondMain);
            }
            if (with_rollout) {
                return finish_turn_and_rollout(consequence->state, phase,
                                               consequence->priority);
            }
            return value_for(consequence->state, seat, phase);
        };
        std::vector<double> totals(actions.size(), 0.0);
        for (std::size_t index = 0; index < actions.size(); ++index) {
            if (dominated[index]) {
                totals[index] =
                    kIllegalScore * static_cast<double>(worlds);
            }
        }
        for (const GameState& sampled : sampled_worlds) {
            for (std::size_t index = 0; index < actions.size(); ++index) {
                if (dominated[index]) {
                    continue;
                }
                totals[index] += score_action(sampled, actions[index],
                                              false);
            }
        }
        if (config.search == SpzPolicyConfig::Search::Ismcts &&
            !combat_response_window) {
            const std::size_t best_index = ismcts_choose(
                actions, dominated, totals, sampled_worlds, phase,
                sorcery_actions);
            if (recorder != nullptr) {
                const auto consequence =
                    resolve_priority_action_consequence(
                        sampled_worlds.front(), seat, sorcery_actions,
                        0, actions[best_index]);
                if (consequence.has_value() && !consequence->terminal) {
                    record_state(consequence->state, phase);
                }
            }
            return best_index;
        }
        if (config.rollout) {
            // Rollout-rescore the myopically strongest candidates. X-value
            // and target variants of one play score near-identically and
            // would flood the shortlist, crowding out genuinely different
            // plays (the setup spell that wins a turn later loses its slot
            // to the fifth-best X). Keep only the myopically best variant
            // per (kind, card, target class), then rank plays.
            std::vector<std::size_t> order;
            {
                std::map<std::tuple<int, int, int>, std::size_t> best;
                for (std::size_t index = 0; index < actions.size();
                     ++index) {
                    const PriorityAction& action = actions[index];
                    int target_class = 0;
                    if (action.target.has_value()) {
                        target_class =
                            1 +
                            (action.target->player == seat ? 0 : 2) +
                            (action.target->creature.has_value() ? 1
                                                                 : 0);
                    }
                    const auto key = std::make_tuple(
                        static_cast<int>(action.kind),
                        static_cast<int>(action.card), target_class);
                    const auto found = best.find(key);
                    if (found == best.end() ||
                        totals[index] > totals[found->second]) {
                        best[key] = index;
                    }
                }
                for (const auto& [key, index] : best) {
                    order.push_back(index);
                }
            }
            std::stable_sort(order.begin(), order.end(),
                             [&](std::size_t left, std::size_t right) {
                                 return totals[left] > totals[right];
                             });
            const std::size_t candidates = std::min(
                std::max<std::size_t>(config.rollout_top_k, 2),
                order.size());
            std::vector<double> rollout_totals(actions.size(),
                                               kIllegalScore);
            for (std::size_t rank = 0; rank < candidates; ++rank) {
                const std::size_t index = order[rank];
                if (totals[index] <=
                    kIllegalScore * static_cast<double>(worlds) / 2.0) {
                    continue;
                }
                double total = 0.0;
                for (const GameState& sampled : sampled_worlds) {
                    total += score_action(sampled, actions[index], true);
                }
                rollout_totals[index] = total;
            }
            totals = std::move(rollout_totals);
        }
        if (advantage_net != nullptr || 
            (config.record_advantage && recorder != nullptr &&
             config.rollout)) {
            const auto state_row = spz_features_for(
                net->input_count(),
                observe_game_state(sampled_worlds.front(), seat), decks,
                phase);
            std::vector<std::vector<float>> action_rows;
            action_rows.reserve(actions.size());
            for (const PriorityAction& action : actions) {
                action_rows.push_back(spz_action_features(
                    action, seat, sampled_worlds.front()));
            }
            if (config.record_advantage && recorder != nullptr &&
                config.rollout) {
                // Paired deltas versus pass over the SAME worlds, for
                // every action including guarded ones: the head must see
                // waste's true cost.
                double pass_total = 0.0;
                for (const GameState& sampled : sampled_worlds) {
                    pass_total += score_action(
                        sampled, PriorityAction::pass(), true);
                }
                SpzAdvantageSample sample;
                sample.state = state_row;
                for (std::size_t index = 0; index < actions.size();
                     ++index) {
                    double total = 0.0;
                    bool legal = true;
                    for (const GameState& sampled : sampled_worlds) {
                        const double value = score_action(
                            sampled, actions[index], true);
                        if (value <= kIllegalScore / 2.0) {
                            legal = false;
                            break;
                        }
                        total += value;
                    }
                    if (!legal) {
                        continue;
                    }
                    sample.actions.push_back(action_rows[index]);
                    sample.deltas.push_back(static_cast<float>(
                        (total - pass_total) /
                        static_cast<double>(worlds)));
                }
                if (sample.actions.size() > 1) {
                    recorder->advantage_samples.push_back(
                        std::move(sample));
                }
            }
            if (advantage_net != nullptr) {
                // The head is trained to ORDER actions, not to move
                // scores: search alone settles contested decisions, and
                // the head re-ranks only the actions whose rollout
                // totals tie the best within the evaluation noise band
                // (where waste otherwise wins by coin flip).
                const double band =
                    config.advantage_tie_band *
                    static_cast<double>(worlds);
                const double top =
                    *std::max_element(totals.begin(), totals.end());
                // Candidates for refinement use a wider net than the
                // arbitration band: an action a little outside the band
                // may be there on noise alone.
                const double candidate_band = 3.0 * band;
                std::vector<std::size_t> finalists;
                for (std::size_t index = 0; index < actions.size();
                     ++index) {
                    if (totals[index] > kIllegalScore / 2.0 &&
                        totals[index] >= top - candidate_band) {
                        finalists.push_back(index);
                    }
                }
                // Adaptive resampling: a tie may be genuine or may be
                // rollout noise exceeding the band. Before the head
                // arbitrates, re-judge the finalists alone on extra
                // determinized worlds - thinking harder exactly where
                // the cheap estimate is uncertain.
                if (finalists.size() > 1 &&
                    config.tie_break_worlds > worlds) {
                    const std::size_t extra_count =
                        config.tie_break_worlds - worlds;
                    std::vector<double> refined(totals);
                    for (std::size_t extra = 0; extra < extra_count;
                         ++extra) {
                        const GameState world = sample_determinization(
                            reconstructed, decks, seat, rng());
                        for (const std::size_t index : finalists) {
                            refined[index] += score_action(
                                world, actions[index], config.rollout);
                        }
                    }
                    const double refined_band =
                        config.advantage_tie_band *
                        static_cast<double>(config.tie_break_worlds);
                    double refined_top =
                        -std::numeric_limits<double>::infinity();
                    for (const std::size_t index : finalists) {
                        refined_top =
                            std::max(refined_top, refined[index]);
                    }
                    std::vector<std::size_t> still_tied;
                    for (const std::size_t index : finalists) {
                        if (refined[index] >=
                            refined_top - refined_band) {
                            still_tied.push_back(index);
                        }
                    }
                    // The refined estimate separated the field: trust
                    // it. Otherwise the survivors are genuinely tied
                    // and the head arbitrates below.
                    if (still_tied.size() == 1) {
                        totals[still_tied.front()] = top + band;
                        finalists.clear();
                    } else {
                        finalists = std::move(still_tied);
                    }
                } else {
                    // No refinement ran: arbitrate only genuine ties.
                    std::vector<std::size_t> tight;
                    for (const std::size_t index : finalists) {
                        if (totals[index] >= top - band) {
                            tight.push_back(index);
                        }
                    }
                    finalists = std::move(tight);
                }
                double best_delta =
                    -std::numeric_limits<double>::infinity();
                std::size_t best_tied = actions.size();
                for (const std::size_t index : finalists) {
                    const double delta = advantage_net->delta(
                        state_row, action_rows[index]);
                    if (delta > best_delta) {
                        best_delta = delta;
                        best_tied = index;
                    }
                }
                if (best_tied < actions.size()) {
                    totals[best_tied] = top + band;
                }
            }
        }
        std::size_t best = static_cast<std::size_t>(
            std::max_element(totals.begin(), totals.end()) -
            totals.begin());
        // A free land drop is strictly resource-positive; when its score
        // ties the best within noise, take it rather than passing on it.
        // (Guardrail: retired when the advantage head carries the signal.)
        if (config.pass_dominance_prune &&
            actions[best].kind != PriorityActionKind::PlayLand) {
            constexpr double kTieMargin = 0.004;
            for (std::size_t index = 0; index < actions.size();
                 ++index) {
                if (!dominated[index] &&
                    actions[index].kind ==
                        PriorityActionKind::PlayLand &&
                    totals[index] >=
                        totals[best] -
                            kTieMargin *
                                static_cast<double>(worlds)) {
                    best = index;
                    break;
                }
            }
        }
        if (recorder != nullptr) {
            const GameState sampled = sample_determinization(
                reconstructed, decks, seat, rng());
            const auto consequence = resolve_priority_action_consequence(
                sampled, seat, sorcery_actions, 0, actions[best]);
            if (consequence.has_value() && !consequence->terminal) {
                record_state(consequence->state, phase);
            }
        }
        return best;
    }

    // Greedy block assignment for `defender`, evaluated by resolving combat
    // on a copy of `state`. `restrictions` optionally limits each blocker to
    // its engine-provided legal attackers; resolve_combat itself rejects any
    // remaining illegal pairing.
    std::vector<std::pair<PermanentId, PermanentId>> greedy_blocks(
        const GameState& state, std::size_t attacking_player,
        const std::vector<PermanentId>& attackers,
        const std::vector<LegalBlockerChoice>* restrictions) const {
        const std::size_t defender = 1 - attacking_player;
        std::vector<std::pair<PermanentId, PermanentId>> blocks;

        std::vector<std::pair<PermanentId, std::vector<PermanentId>>>
            candidates;
        if (restrictions != nullptr) {
            for (const auto& choice : *restrictions) {
                candidates.emplace_back(choice.blocker,
                                        choice.legal_attackers);
            }
        } else {
            for (const auto& creature :
                 state.players[defender].creatures) {
                if (!creature.tapped) {
                    candidates.emplace_back(creature.id, attackers);
                }
            }
        }

        const auto combat_value =
            [&](const std::vector<std::pair<PermanentId, PermanentId>>&
                    trial) -> double {
            GameState simulation = state;
            if (!resolve_combat(simulation, attacking_player, attackers,
                                trial)) {
                return kIllegalScore;
            }
            return value_for(simulation, defender, TurnPhase::SecondMain);
        };

        double current = combat_value(blocks);
        std::vector<bool> assigned(candidates.size(), false);
        while (true) {
            double best_value = current + kImprovementMargin;
            std::size_t best_candidate = candidates.size();
            PermanentId best_attacker = 0;
            for (std::size_t index = 0; index < candidates.size();
                 ++index) {
                if (assigned[index]) {
                    continue;
                }
                for (const PermanentId attacker :
                     candidates[index].second) {
                    auto trial = blocks;
                    trial.emplace_back(attacker,
                                       candidates[index].first);
                    const double trial_value = combat_value(trial);
                    if (trial_value > best_value) {
                        best_value = trial_value;
                        best_candidate = index;
                        best_attacker = attacker;
                    }
                }
            }
            if (best_candidate == candidates.size()) {
                break;
            }
            blocks.emplace_back(best_attacker,
                                candidates[best_candidate].first);
            assigned[best_candidate] = true;
            current = best_value;
        }
        return blocks;
    }

    // Defense prediction for root attack sizing: the defender picks the
    // best-for-them assignment among the greedy hill-climb result and its
    // structured variants (decline, single-block removals, gang-block
    // additions the one-at-a-time climb cannot reach). Attacks are then
    // judged against the strongest visible defense instead of a single
    // greedy guess - assuming a sharper opponent, not a known one.
    std::vector<std::pair<PermanentId, PermanentId>> predicted_defense(
        const GameState& state, std::size_t attacking_player,
        const std::vector<PermanentId>& attackers) const {
        const std::size_t defender = 1 - attacking_player;
        const auto base =
            greedy_blocks(state, attacking_player, attackers, nullptr);

        std::vector<std::vector<std::pair<PermanentId, PermanentId>>>
            candidates;
        candidates.push_back(base);
        if (!base.empty()) {
            candidates.push_back({});
        }
        for (const auto& removed : base) {
            std::vector<std::pair<PermanentId, PermanentId>> reduced;
            for (const auto& kept : base) {
                if (kept != removed) {
                    reduced.push_back(kept);
                }
            }
            candidates.push_back(std::move(reduced));
        }
        std::vector<PermanentId> unassigned;
        for (const auto& creature : state.players[defender].creatures) {
            if (creature.tapped) {
                continue;
            }
            const bool used = std::any_of(
                base.begin(), base.end(), [&](const auto& block) {
                    return block.second == creature.id;
                });
            if (!used) {
                unassigned.push_back(creature.id);
            }
        }
        constexpr std::size_t kGangLimit = 8;
        std::size_t gangs = 0;
        for (const PermanentId attacker : attackers) {
            for (std::size_t first = 0;
                 first < unassigned.size() && gangs < kGangLimit;
                 ++first) {
                for (std::size_t second = first + 1;
                     second < unassigned.size() && gangs < kGangLimit;
                     ++second) {
                    auto gang = base;
                    gang.emplace_back(attacker, unassigned[first]);
                    gang.emplace_back(attacker, unassigned[second]);
                    candidates.push_back(std::move(gang));
                    gangs += 1;
                }
            }
        }

        double best_value = -std::numeric_limits<double>::infinity();
        std::size_t best_candidate = 0;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            GameState simulation = state;
            if (!resolve_combat(simulation, attacking_player, attackers,
                                candidates[index])) {
                continue;
            }
            const double value =
                value_for(simulation, defender, TurnPhase::SecondMain);
            if (value > best_value) {
                best_value = value;
                best_candidate = index;
            }
        }
        return candidates[best_candidate];
    }

    std::vector<PermanentId> choose_attackers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& eligible) {
        record(observation, TurnPhase::DeclareAttackers);
        if (eligible.empty()) {
            return {};
        }
        if (explore()) {
            std::vector<PermanentId> random_set;
            std::uniform_int_distribution<int> coin(0, 1);
            for (const PermanentId attacker : eligible) {
                if (coin(rng) == 1) {
                    random_set.push_back(attacker);
                }
            }
            return random_set;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        const std::size_t worlds =
            std::max<std::size_t>(1, config.block_prediction_worlds);
        std::vector<GameState> sampled_worlds;
        sampled_worlds.reserve(worlds);
        for (std::size_t world = 0; world < worlds; ++world) {
            sampled_worlds.push_back(sample_determinization(
                reconstructed, decks, seat, rng()));
        }

        const auto evaluate_set =
            [&](const std::vector<PermanentId>& attack_set,
                bool with_rollout) -> double {
            double total = 0.0;
            for (const GameState& world : sampled_worlds) {
                GameState simulation = world;
                if (!attack_set.empty()) {
                    const auto predicted_blocks =
                        predicted_defense(world, seat, attack_set);
                    if (!resolve_combat(simulation, seat, attack_set,
                                        predicted_blocks)) {
                        return kIllegalScore;
                    }
                }
                if (with_rollout) {
                    total += finish_turn_and_rollout(
                        std::move(simulation),
                        TurnPhase::DeclareAttackers, {seat, 0});
                } else if (attack_set.empty()) {
                    total += value_for(world, seat,
                                       TurnPhase::SecondMain);
                } else {
                    total += value_for(simulation, seat,
                                       TurnPhase::SecondMain);
                }
            }
            return total / static_cast<double>(worlds);
        };

        if (config.rollout) {
            // Candidate sets around the myopic greedy set: none, greedy,
            // all-in, and single-creature edits of the greedy set.
            std::vector<std::vector<PermanentId>> candidates;
            const auto add_candidate =
                [&candidates](std::vector<PermanentId> candidate) {
                    std::sort(candidate.begin(), candidate.end());
                    if (std::find(candidates.begin(), candidates.end(),
                                  candidate) == candidates.end()) {
                        candidates.push_back(std::move(candidate));
                    }
                };
            add_candidate({});
            const auto greedy_set =
                greedy_attack_set(sampled_worlds.front(), seat, eligible);
            add_candidate(greedy_set);
            add_candidate(eligible);
            constexpr std::size_t kEditLimit = 3;
            std::size_t additions = 0;
            for (const PermanentId attacker : eligible) {
                if (additions >= kEditLimit) {
                    break;
                }
                if (std::find(greedy_set.begin(), greedy_set.end(),
                              attacker) == greedy_set.end()) {
                    auto extended = greedy_set;
                    extended.push_back(attacker);
                    add_candidate(std::move(extended));
                    additions += 1;
                }
            }
            std::size_t removals = 0;
            for (const PermanentId attacker : greedy_set) {
                if (removals >= kEditLimit) {
                    break;
                }
                std::vector<PermanentId> reduced;
                for (const PermanentId kept : greedy_set) {
                    if (kept != attacker) {
                        reduced.push_back(kept);
                    }
                }
                add_candidate(std::move(reduced));
                removals += 1;
            }
            double best_value =
                -std::numeric_limits<double>::infinity();
            std::size_t best_candidate = 0;
            constexpr double kExactTie = 5e-4;
            for (std::size_t index = 0; index < candidates.size();
                 ++index) {
                const double value =
                    evaluate_set(candidates[index], true);
                // In effectively decided positions every plan evaluates
                // as won (or lost) and ties are arbitrary; press the win
                // by attacking with more creatures. In contested games
                // the values differ and the tie-break never fires.
                const bool decided_tie =
                    config.gamma_per_turn >= 1.0 &&
                    value >= best_value - kExactTie &&
                    value >= 0.99 &&
                    candidates[index].size() >
                        candidates[best_candidate].size();
                if (value > best_value + kExactTie || decided_tie) {
                    best_value = std::max(best_value, value);
                    best_candidate = index;
                }
            }
            return candidates[best_candidate];
        }

        std::vector<PermanentId> chosen;
        std::vector<PermanentId> remaining = eligible;
        double current = evaluate_set(chosen, false);
        while (!remaining.empty()) {
            double best_value = current + kImprovementMargin;
            std::size_t best_index = remaining.size();
            for (std::size_t index = 0; index < remaining.size();
                 ++index) {
                auto trial = chosen;
                trial.push_back(remaining[index]);
                const double trial_value = evaluate_set(trial, false);
                if (trial_value > best_value) {
                    best_value = trial_value;
                    best_index = index;
                }
            }
            if (best_index == remaining.size()) {
                break;
            }
            chosen.push_back(remaining[best_index]);
            remaining.erase(remaining.begin() +
                            static_cast<std::ptrdiff_t>(best_index));
            current = best_value;
        }
        return chosen;
    }

    std::vector<std::pair<PermanentId, PermanentId>> choose_blockers(
        const PlayerObservation& observation,
        const std::vector<PermanentId>& attackers,
        const std::vector<LegalBlockerChoice>& choices) {
        record(observation, TurnPhase::DeclareBlockers);
        if (attackers.empty() || choices.empty()) {
            return {};
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        const std::size_t attacking_player = observation.active_player;
        if (explore()) {
            std::vector<std::pair<PermanentId, PermanentId>> random_blocks;
            for (const auto& choice : choices) {
                std::uniform_int_distribution<std::size_t> pick(
                    0, choice.legal_attackers.size());
                const std::size_t selection = pick(rng);
                if (selection < choice.legal_attackers.size()) {
                    random_blocks.emplace_back(
                        choice.legal_attackers[selection], choice.blocker);
                }
            }
            GameState simulation = reconstructed;
            if (resolve_combat(simulation, attacking_player, attackers,
                               random_blocks)) {
                return random_blocks;
            }
            return {};
        }
        if (config.rollout) {
            // Compare the myopic greedy assignment against declining to
            // block, judged by where the whole turn cycle actually lands.
            const auto greedy = greedy_blocks(
                reconstructed, attacking_player, attackers, &choices);
            std::vector<std::vector<std::pair<PermanentId, PermanentId>>>
                candidates;
            candidates.push_back(greedy);
            if (!greedy.empty()) {
                candidates.push_back({});
            }
            // Single-edit variants of the greedy assignment.
            constexpr std::size_t kBlockEditLimit = 3;
            std::size_t block_removals = 0;
            for (const auto& removed : greedy) {
                if (block_removals >= kBlockEditLimit) {
                    break;
                }
                std::vector<std::pair<PermanentId, PermanentId>> reduced;
                for (const auto& kept : greedy) {
                    if (kept != removed) {
                        reduced.push_back(kept);
                    }
                }
                candidates.push_back(std::move(reduced));
                block_removals += 1;
            }
            std::size_t block_additions = 0;
            for (const auto& choice : choices) {
                if (block_additions >= kBlockEditLimit) {
                    break;
                }
                const bool already_blocking = std::any_of(
                    greedy.begin(), greedy.end(),
                    [&choice](const auto& block) {
                        return block.second == choice.blocker;
                    });
                if (already_blocking || choice.legal_attackers.empty()) {
                    continue;
                }
                // Myopically best attacker for this extra blocker.
                double best_value =
                    -std::numeric_limits<double>::infinity();
                std::optional<PermanentId> best_attacker;
                for (const PermanentId attacker :
                     choice.legal_attackers) {
                    auto trial = greedy;
                    trial.emplace_back(attacker, choice.blocker);
                    GameState simulation = reconstructed;
                    if (!resolve_combat(simulation, attacking_player,
                                        attackers, trial)) {
                        continue;
                    }
                    const double value = value_for(
                        simulation, seat, TurnPhase::SecondMain);
                    if (value > best_value) {
                        best_value = value;
                        best_attacker = attacker;
                    }
                }
                if (best_attacker.has_value()) {
                    auto extended = greedy;
                    extended.emplace_back(*best_attacker, choice.blocker);
                    candidates.push_back(std::move(extended));
                    block_additions += 1;
                }
            }
            const std::size_t worlds = std::max<std::size_t>(
                1, config.block_prediction_worlds);
            std::vector<GameState> sampled_worlds;
            sampled_worlds.reserve(worlds);
            for (std::size_t world = 0; world < worlds; ++world) {
                sampled_worlds.push_back(sample_determinization(
                    reconstructed, decks, seat, rng()));
            }
            double best_value =
                -std::numeric_limits<double>::infinity();
            std::size_t best_candidate = 0;
            for (std::size_t index = 0; index < candidates.size();
                 ++index) {
                double total = 0.0;
                bool legal = true;
                for (const GameState& world : sampled_worlds) {
                    GameState simulation = world;
                    if (!resolve_combat(simulation, attacking_player,
                                        attackers,
                                        candidates[index])) {
                        legal = false;
                        break;
                    }
                    total += finish_turn_and_rollout(
                        std::move(simulation),
                        TurnPhase::DeclareBlockers,
                        {attacking_player, 0});
                }
                if (legal && total > best_value) {
                    best_value = total;
                    best_candidate = index;
                }
            }
            return candidates[best_candidate];
        }
        // Block evaluation reads only public zones and the observer's own
        // hand, so a single reconstructed world is exact.
        return greedy_blocks(reconstructed, attacking_player, attackers,
                             &choices);
    }

    std::vector<PermanentId> choose_damage_order(
        const PlayerObservation& observation, PermanentId attacker,
        const std::vector<PermanentId>& blockers) {
        record(observation, TurnPhase::DamageOrder);
        if (blockers.size() <= 1) {
            return blockers;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        std::vector<PermanentId> order = blockers;
        std::sort(order.begin(), order.end());
        std::vector<PermanentId> best_order = blockers;
        double best_value = -std::numeric_limits<double>::infinity();
        std::size_t permutations = 0;
        constexpr std::size_t kPermutationLimit = 24;
        do {
            // Damage assignment is independent per attacker, so ranking by
            // this attacker's isolated combat is exact up to net curvature.
            GameState simulation = reconstructed;
            std::vector<std::pair<PermanentId, PermanentId>> blocks;
            blocks.reserve(order.size());
            for (const PermanentId blocker : order) {
                blocks.emplace_back(attacker, blocker);
            }
            if (resolve_combat(simulation, seat, {attacker}, blocks)) {
                const double value =
                    value_for(simulation, seat, TurnPhase::EndCombat);
                if (value > best_value) {
                    best_value = value;
                    best_order = order;
                }
            }
            permutations += 1;
        } while (permutations < kPermutationLimit &&
                 std::next_permutation(order.begin(), order.end()));
        return best_order;
    }

    std::vector<std::size_t> choose_cleanup_discards(
        const PlayerObservation& observation, std::size_t excess) {
        record(observation, TurnPhase::SecondMain);
        std::vector<std::size_t> chosen;
        if (excess == 0 || observation.hand.empty()) {
            return chosen;
        }
        const GameState reconstructed =
            reconstruct_observed_state(observation);
        std::vector<bool> discarded(observation.hand.size(), false);
        for (std::size_t round = 0;
             round < excess && round < observation.hand.size(); ++round) {
            double best_value =
                -std::numeric_limits<double>::infinity();
            std::size_t best_index = observation.hand.size();
            for (std::size_t index = 0; index < observation.hand.size();
                 ++index) {
                if (discarded[index]) {
                    continue;
                }
                GameState trial = reconstructed;
                std::vector<CardId> remaining_hand;
                for (std::size_t position = 0;
                     position < observation.hand.size(); ++position) {
                    if (!discarded[position] && position != index) {
                        remaining_hand.push_back(
                            observation.hand[position]);
                    }
                }
                trial.players[seat].hand = std::move(remaining_hand);
                trial.players[seat].graveyard.push_back(
                    observation.hand[index]);
                const double value =
                    value_for(trial, seat, TurnPhase::SecondMain);
                if (value > best_value) {
                    best_value = value;
                    best_index = index;
                }
            }
            if (best_index == observation.hand.size()) {
                break;
            }
            discarded[best_index] = true;
            chosen.push_back(best_index);
        }
        std::sort(chosen.begin(), chosen.end());
        return chosen;
    }
};

}  // namespace

HumanController make_spz_controller(
    std::shared_ptr<const SpzNet> net,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t seat, const SpzPolicyConfig& config,
    SpzRecorder* recorder,
    std::shared_ptr<const SpzPolicyNet> policy_net,
    std::shared_ptr<const SpzAdvantageNet> advantage_net) {
    auto agent = std::make_shared<SpzAgent>(
        std::move(net), original_decks, seat, config, recorder,
        std::move(policy_net), std::move(advantage_net));
    HumanController controller;
    controller.choose_priority_action =
        [agent](const PlayerObservation& observation, TurnPhase phase,
                const std::vector<PriorityAction>& actions) {
            return agent->choose_priority_action(observation, phase,
                                                 actions);
        };
    controller.choose_attackers =
        [agent](const PlayerObservation& observation,
                const std::vector<PermanentId>& eligible) {
            return agent->choose_attackers(observation, eligible);
        };
    controller.choose_blockers =
        [agent](const PlayerObservation& observation,
                const std::vector<PermanentId>& attackers,
                const std::vector<LegalBlockerChoice>& choices) {
            return agent->choose_blockers(observation, attackers, choices);
        };
    controller.choose_damage_order =
        [agent](const PlayerObservation& observation, PermanentId attacker,
                const std::vector<PermanentId>& blockers) {
            return agent->choose_damage_order(observation, attacker,
                                              blockers);
        };
    controller.choose_cleanup_discards =
        [agent](const PlayerObservation& observation, std::size_t excess) {
            return agent->choose_cleanup_discards(observation, excess);
        };
    // Public-event feed: keeps the agent's declared-combat context current
    // for post-blockers response windows.
    controller.observe = [agent](const PlayerObservation&,
                                 const GameEvent& event) {
        agent->on_event(event);
    };
    return controller;
}

// ---------------------------------------------------------------------------
// Training

namespace {

void run_indexed_jobs(std::size_t job_count, std::size_t threads,
                      const std::function<void(std::size_t)>& job) {
    const std::size_t worker_count =
        std::max<std::size_t>(1, std::min(threads, job_count));
    if (worker_count <= 1) {
        for (std::size_t index = 0; index < job_count; ++index) {
            job(index);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const std::size_t index =
                    next.fetch_add(1, std::memory_order_relaxed);
                if (index >= job_count) {
                    return;
                }
                job(index);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
}

float outcome_target(const GameResult& result, std::size_t seat,
                     bool discounted, double gamma,
                     std::size_t sample_turn) {
    if (gamma < 1.0) {
        if (result.winner == -1) {
            return 0.5f;
        }
        const double z =
            static_cast<std::size_t>(result.winner) == seat ? 1.0 : 0.0;
        const double elapsed =
            result.turns > sample_turn
                ? static_cast<double>(result.turns - sample_turn)
                : 0.0;
        return static_cast<float>(0.5 +
                                  (z - 0.5) * std::pow(gamma, elapsed));
    }
    if (discounted) {
        return static_cast<float>(
            discounted_terminal_target(result, seat));
    }
    if (result.winner == -1) {
        return 0.5f;
    }
    return static_cast<std::size_t>(result.winner) == seat ? 1.0f : 0.0f;
}

}  // namespace

SpzTrainingCoordinate spz_training_coordinate(
    std::size_t iteration, std::size_t games_per_iteration,
    std::size_t game_index) {
    if (game_index >= games_per_iteration) {
        throw std::out_of_range(
            "SPZ training game index is outside its iteration");
    }
    if (games_per_iteration != 0 &&
        iteration >
            (std::numeric_limits<std::size_t>::max() - game_index) /
                games_per_iteration) {
        throw std::overflow_error(
            "SPZ training coordinate exceeds size_t");
    }
    constexpr std::size_t pairing_count =
        kSpzDeckCount * kSpzDeckCount;
    const std::size_t global_game =
        iteration * games_per_iteration + game_index;
    const std::size_t pairing = global_game % pairing_count;
    const std::size_t repetition = global_game / pairing_count;
    return {
        .deck_zero = pairing / kSpzDeckCount,
        .deck_one = pairing % kSpzDeckCount,
        .pairing_repetition = repetition,
        .starting_player = repetition % 2,
    };
}

SpzTrainOutput train_spz(const SpzTrainConfig& config) {
    const auto& decks = spz_decks();
    const std::size_t state_inputs =
        config.schema == 3   ? spz_feature_count_v3()
        : config.schema == 2 ? spz_feature_count_v2()
                             : spz_feature_count();
    auto net = config.initial_net != nullptr
                   ? std::make_shared<SpzNet>(*config.initial_net)
                   : std::make_shared<SpzNet>(state_inputs,
                                              config.hidden, config.seed);
    std::shared_ptr<SpzAdvantageNet> advantage_net;
    if (config.train_advantage) {
        if (!config.rollout) {
            throw std::invalid_argument(
                "advantage training requires rollout self-play: paired "
                "action deltas are only measured during rollout scoring");
        }
        advantage_net =
            config.initial_advantage != nullptr
                ? std::make_shared<SpzAdvantageNet>(
                      *config.initial_advantage)
                : std::make_shared<SpzAdvantageNet>(
                      net->input_count(), spz_action_feature_count(),
                      config.advantage_hidden,
                      mix_seed(config.seed, 99));
    }
    std::vector<SpzAdvantageSample> advantage_replay;
    std::size_t advantage_cursor = 0;
    std::shared_ptr<SpzPolicyNet> policy_net;
    if (config.train_policy) {
        policy_net =
            config.initial_policy != nullptr
                ? std::make_shared<SpzPolicyNet>(*config.initial_policy)
                : std::make_shared<SpzPolicyNet>(
                      net->input_count(), spz_action_feature_count(),
                      config.policy_hidden, mix_seed(config.seed, 77));
    }
    std::vector<SpzPolicySample> policy_replay;
    std::size_t policy_cursor = 0;
    std::vector<SpzSample> replay;
    replay.reserve(std::min<std::size_t>(config.replay_capacity, 1 << 20));
    std::size_t replay_cursor = 0;
    std::mt19937_64 trainer_rng(mix_seed(config.seed, 0xABCDEF));
    std::vector<std::shared_ptr<const SpzNet>> league_pool;

    struct GameRecord {
        SpzRecorder recorders[2];
        GameResult result;
        std::size_t turns = 0;
    };

    for (std::size_t iteration = 0; iteration < config.iterations;
         ++iteration) {
        const double progress =
            config.iterations <= 1
                ? 1.0
                : static_cast<double>(iteration) /
                      static_cast<double>(config.iterations - 1);
        const double epsilon =
            config.epsilon_start +
            (config.epsilon_final - config.epsilon_start) * progress;

        auto frozen = std::make_shared<const SpzNet>(*net);
        const auto frozen_policy =
            policy_net != nullptr
                ? std::make_shared<const SpzPolicyNet>(*policy_net)
                : std::shared_ptr<const SpzPolicyNet>{};
        const auto frozen_advantage =
            advantage_net != nullptr
                ? std::make_shared<const SpzAdvantageNet>(*advantage_net)
                : std::shared_ptr<const SpzAdvantageNet>{};
        if (config.league_snapshot_interval > 0 &&
            iteration % config.league_snapshot_interval == 0) {
            league_pool.push_back(frozen);
            if (league_pool.size() > config.league_pool_size) {
                league_pool.erase(league_pool.begin());
            }
        }
        std::vector<GameRecord> records(config.games_per_iteration);
        run_indexed_jobs(
            config.games_per_iteration, config.threads,
            [&](std::size_t game_index) {
                std::mt19937_64 game_rng(mix_seed(
                    mix_seed(config.seed, iteration + 1), game_index));
                const SpzTrainingCoordinate coordinate =
                    spz_training_coordinate(
                        iteration, config.games_per_iteration,
                        game_index);
                const std::array<std::vector<CardId>, 2> game_decks = {
                    decks[coordinate.deck_zero],
                    decks[coordinate.deck_one]};
                GameRecord& record = records[game_index];
                // League play: sometimes seat an earlier snapshot so the
                // learner keeps beating past selves, not only its mirror.
                std::array<std::shared_ptr<const SpzNet>, 2> seat_nets = {
                    frozen, frozen};
                std::array<bool, 2> champion_seat = {false, false};
                std::array<bool, 2> rules_seat = {false, false};
                std::array<bool, 2> record_seat = {true, true};
                std::uniform_real_distribution<double> unit(0.0, 1.0);
                const auto sparring_net = config.spar_net != nullptr
                                              ? config.spar_net
                                              : config.initial_net;
                if (unit(game_rng) < config.rules_spar_probability) {
                    // A rules-bot seat diversifies the league: the learner
                    // sees disciplined attack/block futures its own mirror
                    // never produces. The rules seat has no observations
                    // to record; the learner's seat keeps its true
                    // outcome-labeled trajectory.
                    const std::size_t spar_seat = game_rng() % 2;
                    rules_seat[spar_seat] = true;
                    record_seat[spar_seat] = false;
                } else if (sparring_net != nullptr &&
                    unit(game_rng) < config.champion_spar_probability) {
                    // Frozen champion sparring partner under the deployed
                    // greedy-rollout configuration.
                    const std::size_t spar_seat = game_rng() % 2;
                    seat_nets[spar_seat] = sparring_net;
                    champion_seat[spar_seat] = true;
                    // A sparring partner on a different feature schema
                    // must not record: its rows cannot train this net.
                    record_seat[spar_seat] =
                        sparring_net->input_count() ==
                        frozen->input_count();
                } else if (!league_pool.empty() &&
                           unit(game_rng) <
                               config.league_probability) {
                    const std::size_t snapshot =
                        game_rng() % league_pool.size();
                    const std::size_t league_seat = game_rng() % 2;
                    seat_nets[league_seat] = league_pool[snapshot];
                    // Snapshot trajectories are still true outcome-labeled
                    // observations, so both seats keep recording.
                }
                GameConfig game_config;
                game_config.max_turns = config.max_turns;
                game_config.starting_player =
                    coordinate.starting_player;
                for (std::size_t seat = 0; seat < 2; ++seat) {
                    if (rules_seat[seat]) {
                        game_config.bots[seat] = {
                            .kind = BotKind::Handcrafted,
                            .rollouts_per_action = 1,
                        };
                        continue;
                    }
                    SpzPolicyConfig policy;
                    policy.worlds = config.training_worlds;
                    policy.block_prediction_worlds = config.training_worlds;
                    policy.epsilon = epsilon;
                    policy.rollout = config.rollout;
                    policy.gamma_per_turn =
                        config.gamma < 1.0 ? config.gamma : 1.0;
                    policy.record_advantage =
                        config.train_advantage &&
                        !champion_seat[seat] && record_seat[seat];
                    if (config.ismcts && !champion_seat[seat]) {
                        policy.search = SpzPolicyConfig::Search::Ismcts;
                        policy.ismcts_iterations =
                            config.ismcts_iterations;
                    }
                    policy.seed = game_rng();
                    game_config.human_controllers[seat] =
                        make_spz_controller(
                            seat_nets[seat], game_decks, seat, policy,
                            record_seat[seat] ? &record.recorders[seat]
                                              : nullptr,
                            champion_seat[seat] ? nullptr
                                                : frozen_policy,
                            champion_seat[seat] ? nullptr
                                                : frozen_advantage);
                }
                Game game(game_decks[0], game_decks[1], game_rng(),
                          game_config);
                record.result = game.run();
                record.turns = record.result.turns;
            });

        std::size_t new_samples = 0;
        std::size_t decisive = 0;
        std::size_t total_turns = 0;
        for (const GameRecord& record : records) {
            decisive += record.result.winner == -1 ? 0 : 1;
            total_turns += record.turns;
            for (std::size_t seat = 0; seat < 2; ++seat) {
                auto& seat_recorder = record.recorders[seat];
                for (std::size_t row_index = 0;
                     row_index < seat_recorder.feature_rows.size();
                     ++row_index) {
                    const auto& row =
                        seat_recorder.feature_rows[row_index];
                    const std::size_t sample_turn =
                        row_index < seat_recorder.feature_turns.size()
                            ? seat_recorder.feature_turns[row_index]
                            : 0;
                    const float target = outcome_target(
                        record.result, seat, config.discounted_targets,
                        config.gamma, sample_turn);
                    SpzSample sample{row, target};
                    if (replay.size() < config.replay_capacity) {
                        replay.push_back(std::move(sample));
                    } else {
                        replay[replay_cursor] = std::move(sample);
                        replay_cursor =
                            (replay_cursor + 1) % config.replay_capacity;
                    }
                    new_samples += 1;
                }
            }
        }

        std::size_t new_policy_samples = 0;
        if (policy_net != nullptr) {
            for (GameRecord& record : records) {
                for (auto& seat_recorder : record.recorders) {
                    for (auto& sample : seat_recorder.policy_samples) {
                        if (policy_replay.size() <
                            config.policy_replay_capacity) {
                            policy_replay.push_back(std::move(sample));
                        } else {
                            policy_replay[policy_cursor] =
                                std::move(sample);
                            policy_cursor = (policy_cursor + 1) %
                                            config.policy_replay_capacity;
                        }
                        new_policy_samples += 1;
                    }
                }
            }
        }

        std::size_t new_advantage_samples = 0;
        double advantage_agreement = -1.0;
        if (advantage_net != nullptr) {
            std::size_t agreements = 0;
            for (GameRecord& record : records) {
                for (auto& seat_recorder : record.recorders) {
                    for (auto& sample :
                         seat_recorder.advantage_samples) {
                        std::size_t predicted_best = 0;
                        std::size_t measured_best = 0;
                        for (std::size_t option = 1;
                             option < sample.actions.size(); ++option) {
                            if (advantage_net->delta(
                                    sample.state,
                                    sample.actions[option]) >
                                advantage_net->delta(
                                    sample.state,
                                    sample.actions[predicted_best])) {
                                predicted_best = option;
                            }
                            if (sample.deltas[option] >
                                sample.deltas[measured_best]) {
                                measured_best = option;
                            }
                        }
                        agreements +=
                            predicted_best == measured_best ? 1 : 0;
                        if (advantage_replay.size() <
                            config.advantage_replay_capacity) {
                            advantage_replay.push_back(
                                std::move(sample));
                        } else {
                            advantage_replay[advantage_cursor] =
                                std::move(sample);
                            advantage_cursor =
                                (advantage_cursor + 1) %
                                config.advantage_replay_capacity;
                        }
                        new_advantage_samples += 1;
                    }
                }
            }
            if (new_advantage_samples > 0) {
                advantage_agreement =
                    static_cast<double>(agreements) /
                    static_cast<double>(new_advantage_samples);
            }
        }
        double advantage_loss = 0.0;
        double advantage_rank_loss = 0.0;
        std::size_t advantage_rank_steps = 0;
        if (advantage_net != nullptr && !advantage_replay.empty() &&
            new_advantage_samples > 0) {
            const std::size_t batch = 64;
            const std::size_t advantage_steps = std::max<std::size_t>(
                1, (new_advantage_samples * 4) / batch);
            std::uniform_int_distribution<std::size_t> pick(
                0, advantage_replay.size() - 1);
            std::vector<SpzAdvantageNet::Sample> batch_samples;
            for (std::size_t step = 0; step < advantage_steps; ++step) {
                batch_samples.clear();
                while (batch_samples.size() < batch) {
                    const SpzAdvantageSample& decision =
                        advantage_replay[pick(trainer_rng)];
                    for (std::size_t option = 0;
                         option < decision.actions.size() &&
                         batch_samples.size() < batch;
                         ++option) {
                        batch_samples.push_back(
                            {&decision.state,
                             &decision.actions[option],
                             decision.deltas[option]});
                    }
                }
                advantage_loss += advantage_net->train_batch(
                    batch_samples, config.advantage_learning_rate);
            }
            advantage_loss /= static_cast<double>(advantage_steps);

            // Ranking pass: same replay, pairs within one decision whose
            // measured deltas disagree. Order is the deployed use of the
            // head, and consistent tiny deltas (a wasted pump, a skipped
            // land) survive here where they drown in squared error.
            // Pairs need a real measured gap: below this the deltas'
            // own pricing error dominates (a land drop's value at the
            // rollout horizon reads near zero), and training on such
            // pairs teaches confident wrong orderings - the v5 head
            // doubled land skips at its best-ever agreement.
            constexpr double kPairMargin = 1e-3;
            std::vector<SpzAdvantageNet::RankedPair> pair_batch;
            for (std::size_t step = 0; step < advantage_steps; ++step) {
                pair_batch.clear();
                std::size_t attempts = 0;
                while (pair_batch.size() < batch &&
                       attempts < batch * 8) {
                    attempts += 1;
                    const SpzAdvantageSample& decision =
                        advantage_replay[pick(trainer_rng)];
                    if (decision.actions.size() < 2) {
                        continue;
                    }
                    std::uniform_int_distribution<std::size_t> option(
                        0, decision.actions.size() - 1);
                    const std::size_t first = option(trainer_rng);
                    const std::size_t second = option(trainer_rng);
                    const double gap = decision.deltas[first] -
                                       decision.deltas[second];
                    if (std::abs(gap) < kPairMargin) {
                        continue;
                    }
                    const std::size_t better =
                        gap > 0.0 ? first : second;
                    const std::size_t worse =
                        gap > 0.0 ? second : first;
                    pair_batch.push_back(
                        {&decision.state, &decision.actions[better],
                         &decision.actions[worse]});
                }
                if (!pair_batch.empty()) {
                    advantage_rank_loss +=
                        advantage_net->train_ranking_batch(
                            pair_batch,
                            config.advantage_learning_rate);
                    advantage_rank_steps += 1;
                }
            }
            if (advantage_rank_steps > 0) {
                advantage_rank_loss /=
                    static_cast<double>(advantage_rank_steps);
            }
        }

        double mean_loss = 0.0;
        double loss_floor = 0.0;
        std::size_t floor_samples = 0;
        std::size_t steps = 0;
        if (config.train_value && !replay.empty()) {
            const std::size_t batch =
                std::max<std::size_t>(1, config.batch_size);
            steps = static_cast<std::size_t>(
                config.replay_passes *
                static_cast<double>(new_samples) /
                static_cast<double>(batch));
            steps = std::max<std::size_t>(steps, 1);
            std::uniform_int_distribution<std::size_t> pick(
                0, replay.size() - 1);
            std::vector<const std::vector<float>*> batch_features(batch);
            std::vector<float> batch_targets(batch);
            for (std::size_t step = 0; step < steps; ++step) {
                for (std::size_t slot = 0; slot < batch; ++slot) {
                    const SpzSample& sample = replay[pick(trainer_rng)];
                    batch_features[slot] = &sample.features;
                    batch_targets[slot] = sample.target;
                    // Soft targets carry irreducible entropy; report the
                    // floor so the monitor can show excess loss.
                    const double t = std::clamp(
                        static_cast<double>(sample.target), 1e-6,
                        1.0 - 1e-6);
                    loss_floor += -(t * std::log(t) +
                                    (1.0 - t) * std::log(1.0 - t));
                    floor_samples += 1;
                }
                mean_loss += net->train_batch(batch_features,
                                              batch_targets,
                                              config.learning_rate);
            }
            mean_loss /= static_cast<double>(steps);
        }

        double policy_loss = 0.0;
        if (policy_net != nullptr && !policy_replay.empty() &&
            new_policy_samples > 0) {
            const std::size_t batch = 32;
            const std::size_t policy_steps = std::max<std::size_t>(
                1, new_policy_samples / batch);
            std::uniform_int_distribution<std::size_t> pick(
                0, policy_replay.size() - 1);
            std::vector<SpzPolicyNet::Decision> batch_decisions(batch);
            for (std::size_t step = 0; step < policy_steps; ++step) {
                for (std::size_t slot = 0; slot < batch; ++slot) {
                    const SpzPolicySample& sample =
                        policy_replay[pick(trainer_rng)];
                    batch_decisions[slot] = {
                        &sample.state, &sample.actions, &sample.visits};
                }
                policy_loss += policy_net->train_batch(
                    batch_decisions, config.policy_learning_rate);
            }
            policy_loss /= static_cast<double>(policy_steps);
        }

        if (config.log) {
            std::ostringstream line;
            line << "iteration " << (iteration + 1) << '/'
                 << config.iterations << " epsilon " << std::fixed
                 << std::setprecision(3) << epsilon << " games "
                 << config.games_per_iteration << " decisive "
                 << decisive << " avg-turns "
                 << (config.games_per_iteration == 0
                         ? 0.0
                         : static_cast<double>(total_turns) /
                               static_cast<double>(
                                   config.games_per_iteration))
                 << " new-samples " << new_samples
                 << " adv-samples " << new_advantage_samples
                 << " adv-loss " << std::setprecision(5)
                 << advantage_loss << " adv-rank " << advantage_rank_loss
                 << " adv-agree "
                 << (advantage_agreement >= 0.0 ? advantage_agreement
                                                : 0.0)
                 << " replay "
                 << replay.size() << " steps " << steps << " loss "
                 << std::setprecision(4) << mean_loss
                 << " policy-samples " << new_policy_samples
                 << " policy-loss " << std::setprecision(4)
                 << policy_loss;
            config.log(line.str());
        }
        if (!config.telemetry_path.empty()) {
            double vs_handcrafted = -1.0;
            double vs_baseline = -1.0;
            double vs_random = -1.0;
            std::array<double, kSpzDeckCount> deck_lift{};
            bool have_deck_lift = false;
            if (config.probe_interval > 0 &&
                (iteration + 1) % config.probe_interval == 0) {
                const auto probe_net =
                    std::make_shared<const SpzNet>(*net);
                SpzPolicyConfig probe_policy;
                probe_policy.worlds = 4;
                probe_policy.block_prediction_worlds = 4;
                probe_policy.rollout = true;
                const auto random_probe = run_spz_benchmark(
                    probe_net, BotKind::Random, config.probe_reps,
                    mix_seed(config.seed, 9300 + iteration),
                    probe_policy, 200, config.threads);
                vs_random = random_probe.aggregate.win_rate();
                // Classic deck lift: on identical pairings, how much
                // better each deck wins piloted by the current net than
                // piloted by Random.
                for (std::size_t deck = 0; deck < kSpzDeckCount;
                     ++deck) {
                    deck_lift[deck] =
                        random_probe.per_deck[deck].win_rate() -
                        random_probe.baseline_deck_win_rate(deck);
                }
                have_deck_lift = true;
                vs_handcrafted =
                    run_spz_benchmark(
                        probe_net, BotKind::Handcrafted,
                        config.probe_reps,
                        mix_seed(config.seed, 9100 + iteration),
                        probe_policy, 200, config.threads)
                        .aggregate.win_rate();
                vs_baseline =
                    run_spz_benchmark(
                        probe_net, BotKind::DeepMonteCarlo,
                        config.probe_reps,
                        mix_seed(config.seed, 9200 + iteration),
                        probe_policy, 200, config.threads)
                        .aggregate.win_rate();
                if (config.log) {
                    std::ostringstream probe_line;
                    probe_line << "probe iteration " << (iteration + 1)
                               << " vs-random " << std::fixed
                               << std::setprecision(3) << vs_random
                               << " vs-handcrafted " << vs_handcrafted;
                    if (vs_baseline >= 0.0) {
                        probe_line << " vs-deep-mc " << vs_baseline;
                    }
                    config.log(probe_line.str());
                }
            }
            std::ofstream telemetry(config.telemetry_path,
                                    std::ios::app);
            if (telemetry) {
                if (floor_samples > 0) {
                    loss_floor /= static_cast<double>(floor_samples);
                }
                telemetry << "{\"iteration\":" << (iteration + 1)
                          << ",\"games\":"
                          << (iteration + 1) *
                                 config.games_per_iteration
                          << ",\"loss\":" << std::setprecision(6)
                          << mean_loss << ",\"loss_floor\":"
                          << loss_floor << ",\"advantage_loss\":"
                          << advantage_loss
                          << ",\"advantage_rank_loss\":"
                          << advantage_rank_loss << ",\"policy_loss\":"
                          << policy_loss << ",\"avg_turns\":"
                          << (config.games_per_iteration == 0
                                  ? 0.0
                                  : static_cast<double>(total_turns) /
                                        static_cast<double>(
                                            config
                                                .games_per_iteration));
                if (advantage_agreement >= 0.0) {
                    telemetry << ",\"advantage_agreement\":"
                              << advantage_agreement;
                }
                if (vs_random >= 0.0) {
                    telemetry << ",\"vs_random\":" << vs_random;
                }
                if (vs_handcrafted >= 0.0) {
                    telemetry << ",\"vs_handcrafted\":"
                              << vs_handcrafted;
                }
                if (vs_baseline >= 0.0) {
                    telemetry << ",\"vs_deep_monte_carlo\":"
                              << vs_baseline;
                }
                if (have_deck_lift) {
                    telemetry << ",\"deck_lift\":{";
                    for (std::size_t deck = 0; deck < kSpzDeckCount;
                         ++deck) {
                        telemetry
                            << (deck == 0 ? "\"" : ",\"")
                            << spz_deck_name(deck) << "\":"
                            << deck_lift[deck];
                    }
                    telemetry << '}';
                }
                telemetry << "}\n";
            }
        }
        if (!config.checkpoint_prefix.empty() &&
            config.checkpoint_interval > 0 &&
            (iteration + 1) % config.checkpoint_interval == 0) {
            save_spz_net(*net, config.checkpoint_prefix +
                                   std::to_string(iteration + 1) +
                                   ".txt");
        }
    }
    return {net, policy_net, advantage_net};
}

// ---------------------------------------------------------------------------
// Benchmark

double SpzDeckStats::win_rate() const {
    if (games == 0) {
        return 0.0;
    }
    return (static_cast<double>(wins) + 0.5 * static_cast<double>(draws)) /
           static_cast<double>(games);
}

double SpzBenchmarkResult::baseline_deck_win_rate(std::size_t deck) const {
    std::size_t games = 0;
    double baseline_score = 0.0;
    for (std::size_t spz_deck = 0; spz_deck < kSpzDeckCount; ++spz_deck) {
        const SpzDeckStats& stats = matchups[spz_deck][deck];
        games += stats.games;
        baseline_score += static_cast<double>(stats.losses) +
                          0.5 * static_cast<double>(stats.draws);
    }
    return games == 0 ? 0.0
                      : baseline_score / static_cast<double>(games);
}

namespace {

double wilson_lower_bound(double successes, double games) {
    if (games <= 0.0) {
        return 0.0;
    }
    constexpr double z = 1.959963984540054;
    const double phat = successes / games;
    const double z2 = z * z;
    const double denominator = 1.0 + z2 / games;
    const double center = phat + z2 / (2.0 * games);
    const double margin =
        z * std::sqrt(phat * (1.0 - phat) / games +
                      z2 / (4.0 * games * games));
    return (center - margin) / denominator;
}

}  // namespace

SpzBenchmarkResult run_spz_benchmark(
    std::shared_ptr<const SpzNet> net, BotKind baseline,
    std::size_t repetitions_per_pairing, std::uint64_t seed,
    const SpzPolicyConfig& policy, std::size_t max_turns,
    std::size_t threads,
    const std::function<void(const std::string&)>& log,
    const SpzPolicyConfig* baseline_spz_policy,
    std::shared_ptr<const SpzNet> baseline_net,
    std::shared_ptr<const SpzPolicyNet> policy_net,
    std::shared_ptr<const SpzPolicyNet> baseline_policy_net,
    std::shared_ptr<const SpzAdvantageNet> advantage_net,
    std::shared_ptr<const SpzAdvantageNet> baseline_advantage_net) {
    const auto& decks = spz_decks();

    struct Job {
        std::size_t spz_deck = 0;
        std::size_t opponent_deck = 0;
        std::size_t repetition = 0;
    };
    std::vector<Job> jobs;
    for (std::size_t spz_deck = 0; spz_deck < kSpzDeckCount; ++spz_deck) {
        for (std::size_t opponent_deck = 0;
             opponent_deck < kSpzDeckCount; ++opponent_deck) {
            for (std::size_t repetition = 0;
                 repetition < repetitions_per_pairing; ++repetition) {
                jobs.push_back({spz_deck, opponent_deck, repetition});
            }
        }
    }

    struct JobOutcome {
        // Two seat-swapped games: 1 win, 0 loss, draws counted separately.
        std::array<int, 2> spz_wins = {0, 0};
        std::array<int, 2> draws = {0, 0};
    };
    std::vector<JobOutcome> outcomes(jobs.size());
    std::atomic<std::size_t> completed{0};

    run_indexed_jobs(jobs.size(), threads, [&](std::size_t job_index) {
        const Job& job = jobs[job_index];
        const std::uint64_t pairing_seed = mix_seed(
            mix_seed(mix_seed(seed, job.spz_deck * kSpzDeckCount +
                                        job.opponent_deck),
                     job.repetition),
            0x51AB);
        const bool spz_on_play = job.repetition % 2 == 0;
        for (std::size_t game_index = 0; game_index < 2; ++game_index) {
            const std::size_t spz_seat = game_index;
            const std::size_t baseline_seat = 1 - spz_seat;
            GameConfig game_config;
            game_config.max_turns = max_turns;
            game_config.starting_player =
                spz_on_play ? spz_seat : baseline_seat;
            game_config.bots[baseline_seat].kind = baseline;
            if (baseline_spz_policy != nullptr) {
                game_config.bots[baseline_seat].kind = BotKind::Random;
            }
            std::array<std::vector<CardId>, 2> game_decks;
            game_decks[spz_seat] = decks[job.spz_deck];
            game_decks[baseline_seat] = decks[job.opponent_deck];
            SpzPolicyConfig game_policy = policy;
            game_policy.epsilon = 0.0;
            game_policy.seed =
                mix_seed(pairing_seed, 100 + game_index);
            game_config.human_controllers[spz_seat] =
                make_spz_controller(net, game_decks, spz_seat,
                                    game_policy, nullptr, policy_net,
                                    advantage_net);
            if (baseline_spz_policy != nullptr) {
                SpzPolicyConfig opponent_policy = *baseline_spz_policy;
                opponent_policy.epsilon = 0.0;
                opponent_policy.seed =
                    mix_seed(pairing_seed, 200 + game_index);
                game_config.human_controllers[baseline_seat] =
                    make_spz_controller(
                        baseline_net != nullptr ? baseline_net : net,
                        game_decks, baseline_seat, opponent_policy,
                        nullptr, baseline_policy_net,
                        baseline_advantage_net);
            }
            Game game(game_decks[0], game_decks[1], pairing_seed,
                      game_config);
            const GameResult result = game.run();
            if (result.winner == -1) {
                outcomes[job_index].draws[game_index] = 1;
            } else if (static_cast<std::size_t>(result.winner) ==
                       spz_seat) {
                outcomes[job_index].spz_wins[game_index] = 1;
            }
        }
        const std::size_t done =
            completed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (log && done % 25 == 0) {
            std::ostringstream line;
            line << "benchmark " << done << '/' << jobs.size()
                 << " pairings";
            log(line.str());
        }
    });

    SpzBenchmarkResult result;
    for (std::size_t job_index = 0; job_index < jobs.size(); ++job_index) {
        const Job& job = jobs[job_index];
        const JobOutcome& outcome = outcomes[job_index];
        for (std::size_t game_index = 0; game_index < 2; ++game_index) {
            SpzDeckStats& deck_stats = result.per_deck[job.spz_deck];
            SpzDeckStats& matchup_stats =
                result.matchups[job.spz_deck][job.opponent_deck];
            deck_stats.games += 1;
            matchup_stats.games += 1;
            result.aggregate.games += 1;
            if (outcome.spz_wins[game_index] == 1) {
                deck_stats.wins += 1;
                matchup_stats.wins += 1;
                result.aggregate.wins += 1;
            } else if (outcome.draws[game_index] == 1) {
                deck_stats.draws += 1;
                matchup_stats.draws += 1;
                result.aggregate.draws += 1;
            } else {
                deck_stats.losses += 1;
                matchup_stats.losses += 1;
                result.aggregate.losses += 1;
            }
        }
    }
    result.wilson_lower_bound_95 = wilson_lower_bound(
        static_cast<double>(result.aggregate.wins) +
            0.5 * static_cast<double>(result.aggregate.draws),
        static_cast<double>(result.aggregate.games));
    return result;
}

}  // namespace old_school::selfplay_zero
