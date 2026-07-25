#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace old_school::probes {
namespace {

constexpr std::size_t kPlayerCount = 2;
constexpr std::size_t kProbeCardCount = kCardCount;
constexpr std::size_t kCategoryCount =
    static_cast<std::size_t>(Category::WhiteAvoidRedundantMoat) + 1;
using CardCounts = std::array<std::size_t, kProbeCardCount>;

std::vector<CardId> deck_for(DeckId deck) {
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
        throw std::invalid_argument(
            "RU Aggro decision probes have not been authored");
    }
    throw std::invalid_argument("unknown probe deck");
}

std::size_t card_index(CardId card) {
    const std::size_t index = static_cast<std::size_t>(card);
    if (index >= kCardCount) {
        throw std::invalid_argument("probe contains an unknown card");
    }
    return index;
}

CardCounts counts_of(const std::vector<CardId>& cards) {
    CardCounts counts{};
    for (const CardId card : cards) {
        ++counts[card_index(card)];
    }
    return counts;
}

void subtract_card(CardCounts& counts, CardId card) {
    std::size_t& count = counts[card_index(card)];
    if (count == 0) {
        throw std::invalid_argument(
            "probe zones use a card absent from the original deck");
    }
    --count;
}

void subtract_public_zones(CardCounts& counts,
                           const PlayerState& player) {
    for (const CardId card : player.graveyard) {
        subtract_card(counts, card);
    }
    for (const CardId card : player.exile) {
        subtract_card(counts, card);
    }
    for (const LandPermanent& land : player.lands) {
        subtract_card(counts, land.card);
    }
    for (const CreaturePermanent& creature : player.creatures) {
        subtract_card(counts, creature.card);
    }
    for (const ArtifactPermanent& artifact : player.artifacts) {
        subtract_card(counts, artifact.card);
    }
    for (const CardId card : player.enchantments) {
        subtract_card(counts, card);
    }
}

std::vector<CardId> expand_counts(const CardCounts& counts) {
    std::vector<CardId> cards;
    for (std::size_t index = 0; index < counts.size(); ++index) {
        cards.insert(cards.end(), counts[index],
                     static_cast<CardId>(index));
    }
    return cards;
}

DecisionProbe make_base(std::string stable_id, Category category,
                        DecisionKind decision_kind, DeckId root_deck,
                        DeckId opponent_deck, TurnPhase phase,
                        std::size_t turn_number,
                        std::size_t active_player = 0) {
    DecisionProbe probe;
    probe.stable_id = std::move(stable_id);
    probe.category = category;
    probe.decision_kind = decision_kind;
    probe.root_deck = root_deck;
    probe.opponent_deck = opponent_deck;
    probe.root_player = 0;
    probe.phase = phase;
    probe.consecutive_passes = 0;
    probe.state.active_player = active_player;
    probe.state.starting_player = 0;
    probe.state.turn_number = turn_number;
    probe.original_decks = {
        deck_for(root_deck),
        deck_for(opponent_deck),
    };
    return probe;
}

void finish_hidden_zones(DecisionProbe& probe,
                         std::size_t opponent_hand_size = 5) {
    for (std::size_t player = 0; player < kPlayerCount; ++player) {
        CardCounts remaining = counts_of(probe.original_decks[player]);
        subtract_public_zones(remaining, probe.state.players[player]);
        for (const StackObject& object : probe.state.stack) {
            if (object.controller == player &&
                object.kind == StackObjectKind::Spell) {
                subtract_card(remaining, object.card);
            }
        }

        PlayerState& player_state = probe.state.players[player];
        if (player == probe.root_player) {
            for (const CardId card : player_state.hand) {
                subtract_card(remaining, card);
            }
            player_state.library = expand_counts(remaining);
        } else {
            const std::vector<CardId> hidden = expand_counts(remaining);
            if (hidden.size() < opponent_hand_size) {
                throw std::invalid_argument(
                    "probe opponent hand is larger than its hidden pool");
            }
            const auto hand_end =
                hidden.begin() +
                static_cast<std::ptrdiff_t>(opponent_hand_size);
            player_state.hand.assign(hidden.begin(), hand_end);
            player_state.library.assign(hand_end, hidden.end());
        }
    }

    PermanentId largest_permanent = 0;
    StackObjectId largest_stack_object = 0;
    for (const PlayerState& player : probe.state.players) {
        for (const CreaturePermanent& creature : player.creatures) {
            largest_permanent = std::max(largest_permanent, creature.id);
        }
        for (const ArtifactPermanent& artifact : player.artifacts) {
            largest_permanent = std::max(largest_permanent, artifact.id);
        }
    }
    for (const StackObject& object : probe.state.stack) {
        largest_stack_object =
            std::max(largest_stack_object, object.id);
    }
    probe.state.next_permanent_id = largest_permanent + 1;
    probe.state.next_stack_object_id = largest_stack_object + 1;
}

Candidate priority_candidate(std::string descriptor,
                             PriorityAction action) {
    return {
        .descriptor = std::move(descriptor),
        .action = std::move(action),
    };
}

Candidate attack_candidate(std::string descriptor, PermanentId attacker,
                           bool include) {
    return {
        .descriptor = std::move(descriptor),
        .action = BinaryAttackDecision{
            .attacker = attacker,
            .include = include,
        },
    };
}

LandPermanent land(CardId card, bool tapped = false) {
    return {.card = card, .tapped = tapped};
}

CreaturePermanent creature(PermanentId id, CardId card,
                           bool tapped = false,
                           bool summoning_sick = false,
                           int damage = 0) {
    return {
        .id = id,
        .card = card,
        .tapped = tapped,
        .summoning_sick = summoning_sick,
        .damage = damage,
    };
}

ArtifactPermanent artifact(PermanentId id, CardId card,
                           bool tapped = false) {
    return {.id = id, .card = card, .tapped = tapped};
}

StackObject spell(StackObjectId id, CardId card, std::size_t controller,
                  std::optional<Target> target = std::nullopt,
                  std::optional<StackObjectId> spell_target =
                      std::nullopt) {
    return {
        .kind = StackObjectKind::Spell,
        .id = id,
        .card = card,
        .controller = controller,
        .target = target,
        .spell_target = spell_target,
    };
}

DecisionProbe green_develop_probe() {
    DecisionProbe probe = make_base(
        "green.develop-bears.v1", Category::GreenDevelop,
        DecisionKind::Priority, DeckId::Green, DeckId::Red,
        TurnPhase::FirstMain, 5);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::GrizzlyBears, CardId::IronrootTreefolk};
    root.lands = {land(CardId::Forest), land(CardId::Forest)};
    root.land_played_this_turn = true;
    probe.state.players[1].lands = {
        land(CardId::Mountain),
        land(CardId::Mountain),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "cast-grizzly-bears",
            PriorityAction::cast_creature(CardId::GrizzlyBears)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe green_tsunami_probe() {
    DecisionProbe probe = make_base(
        "green.tsunami-timing.v1", Category::GreenTsunamiTiming,
        DecisionKind::Priority, DeckId::Green, DeckId::Blue,
        TurnPhase::FirstMain, 9);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::Tsunami, CardId::GrizzlyBears};
    root.lands.assign(4, land(CardId::Forest));
    root.land_played_this_turn = true;
    probe.state.players[1].lands.assign(4, land(CardId::Island));
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "cast-grizzly-bears",
            PriorityAction::cast_creature(CardId::GrizzlyBears)),
        priority_candidate(
            "cast-tsunami",
            PriorityAction::cast_sorcery(CardId::Tsunami)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe green_favorable_attack_probe() {
    constexpr PermanentId kTreefolk = 1;
    DecisionProbe probe = make_base(
        "green.attack-into-bear.v1", Category::GreenFavorableAttack,
        DecisionKind::Attack, DeckId::Green, DeckId::Green,
        TurnPhase::DeclareAttackers, 11);
    probe.state.players[0].lands.assign(5, land(CardId::Forest));
    probe.state.players[0].creatures = {
        creature(kTreefolk, CardId::IronrootTreefolk),
    };
    probe.state.players[1].lands.assign(5, land(CardId::Forest));
    probe.state.players[1].creatures = {
        creature(2, CardId::GrizzlyBears),
    };
    probe.candidates = {
        attack_candidate("skip-ironroot-treefolk", kTreefolk, false),
        attack_candidate("include-ironroot-treefolk", kTreefolk, true),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe green_unfavorable_attack_probe() {
    constexpr PermanentId kTreefolk = 1;
    DecisionProbe probe = make_base(
        "green.attack-into-fire-elemental.v1",
        Category::GreenUnfavorableAttack, DecisionKind::Attack,
        DeckId::Green, DeckId::Red, TurnPhase::DeclareAttackers, 11);
    probe.state.players[0].lands.assign(5, land(CardId::Forest));
    probe.state.players[0].creatures = {
        creature(kTreefolk, CardId::IronrootTreefolk),
    };
    probe.state.players[1].lands.assign(
        5, land(CardId::Mountain, true));
    probe.state.players[1].creatures = {
        creature(2, CardId::FireElemental, false, true),
    };
    probe.candidates = {
        attack_candidate("skip-ironroot-treefolk", kTreefolk, false),
        attack_candidate("include-ironroot-treefolk", kTreefolk, true),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe red_face_lethal_probe() {
    constexpr PermanentId kWater = 1;
    DecisionProbe probe = make_base(
        "red.bolt-face-lethal.v1", Category::RedFaceLethal,
        DecisionKind::Priority, DeckId::Red, DeckId::Blue,
        TurnPhase::FirstMain, 11);
    probe.state.players[0].hand = {CardId::LightningBolt};
    probe.state.players[0].lands = {land(CardId::Mountain)};
    probe.state.players[0].land_played_this_turn = true;
    probe.state.players[1].life = 3;
    probe.state.players[1].lands.assign(
        5, land(CardId::Island, true));
    probe.state.players[1].creatures = {
        creature(kWater, CardId::WaterElemental, false, true),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "bolt-self-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(0))),
        priority_candidate(
            "bolt-opponent-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(1))),
        priority_candidate(
            "bolt-opponent-water-elemental",
            PriorityAction::cast_lightning_bolt(
                Target::creature_target(1, kWater))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe red_clear_blocker_probe() {
    constexpr PermanentId kFire = 1;
    constexpr PermanentId kBear = 2;
    DecisionProbe probe = make_base(
        "red.bolt-blocker.v1", Category::RedClearBlocker,
        DecisionKind::Priority, DeckId::Red, DeckId::Green,
        TurnPhase::FirstMain, 11);
    probe.state.players[0].hand = {CardId::LightningBolt};
    probe.state.players[0].lands = {
        land(CardId::Mountain, true),
        land(CardId::Mountain, true),
        land(CardId::Mountain, true),
        land(CardId::Mountain, true),
        land(CardId::Mountain),
    };
    probe.state.players[0].creatures = {
        creature(kFire, CardId::FireElemental),
    };
    probe.state.players[0].land_played_this_turn = true;
    probe.state.players[1].life = 5;
    probe.state.players[1].lands.assign(5, land(CardId::Forest));
    probe.state.players[1].creatures = {
        creature(kBear, CardId::GrizzlyBears),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "bolt-self-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(0))),
        priority_candidate(
            "bolt-own-fire-elemental",
            PriorityAction::cast_lightning_bolt(
                Target::creature_target(0, kFire))),
        priority_candidate(
            "bolt-opponent-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(1))),
        priority_candidate(
            "bolt-opponent-grizzly-bears",
            PriorityAction::cast_lightning_bolt(
                Target::creature_target(1, kBear))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe red_finish_damaged_probe() {
    constexpr PermanentId kWater = 1;
    DecisionProbe probe = make_base(
        "red.finish-damaged-water.v1",
        Category::RedFinishDamagedThreat, DecisionKind::Priority,
        DeckId::Red, DeckId::Blue, TurnPhase::FirstMain, 11);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::LightningBolt};
    root.graveyard = {CardId::LightningBolt};
    root.lands = {
        land(CardId::Mountain, true),
        land(CardId::Mountain),
    };
    root.land_played_this_turn = true;
    probe.state.players[1].lands.assign(
        5, land(CardId::Island, true));
    probe.state.players[1].creatures = {
        creature(kWater, CardId::WaterElemental, false, true, 3),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "bolt-self-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(0))),
        priority_candidate(
            "bolt-opponent-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(1))),
        priority_candidate(
            "bolt-damaged-water-elemental",
            PriorityAction::cast_lightning_bolt(
                Target::creature_target(1, kWater))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe red_stack_race_probe() {
    constexpr StackObjectId kEnemyBolt = 1;
    DecisionProbe probe = make_base(
        "red.stack-race.v1", Category::RedStackRace,
        DecisionKind::Priority, DeckId::Red, DeckId::Red,
        TurnPhase::FirstMain, 10, 1);
    probe.state.players[0].life = 3;
    probe.state.players[0].hand = {CardId::LightningBolt};
    probe.state.players[0].lands = {land(CardId::Mountain)};
    probe.state.players[1].life = 3;
    probe.state.players[1].lands = {land(CardId::Mountain, true)};
    probe.state.stack = {
        spell(kEnemyBolt, CardId::LightningBolt, 1,
              Target::player_target(0)),
    };
    // The caster retained priority and passed before player zero received
    // this response window.
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "bolt-self-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(0))),
        priority_candidate(
            "bolt-opponent-player",
            PriorityAction::cast_lightning_bolt(
                Target::player_target(1))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe blue_counter_expensive_probe() {
    constexpr StackObjectId kFireSpell = 1;
    DecisionProbe probe = make_base(
        "blue.counter-fire-elemental.v1",
        Category::BlueCounterExpensiveSpell, DecisionKind::Priority,
        DeckId::Blue, DeckId::Red, TurnPhase::FirstMain, 10, 1);
    probe.state.players[0].hand = {CardId::Counterspell};
    probe.state.players[0].lands.assign(2, land(CardId::Island));
    probe.state.players[1].lands.assign(
        5, land(CardId::Mountain, true));
    probe.state.stack = {
        spell(kFireSpell, CardId::FireElemental, 1),
    };
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "counter-fire-elemental",
            PriorityAction::cast_counterspell(kFireSpell)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe blue_conserve_counter_probe() {
    constexpr PermanentId kWater = 1;
    constexpr StackObjectId kBolt = 1;
    DecisionProbe probe = make_base(
        "blue.conserve-counter-on-water.v1",
        Category::BlueConserveCounter, DecisionKind::Priority,
        DeckId::Blue, DeckId::Red, TurnPhase::FirstMain, 10, 1);
    probe.state.players[0].hand = {CardId::Counterspell};
    probe.state.players[0].lands.assign(2, land(CardId::Island));
    probe.state.players[0].creatures = {
        creature(kWater, CardId::WaterElemental),
    };
    probe.state.players[1].lands = {land(CardId::Mountain, true)};
    probe.state.stack = {
        spell(kBolt, CardId::LightningBolt, 1,
              Target::creature_target(0, kWater)),
    };
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "counter-lightning-bolt",
            PriorityAction::cast_counterspell(kBolt)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe blue_counter_lethal_probe() {
    constexpr StackObjectId kBolt = 1;
    DecisionProbe probe = make_base(
        "blue.counter-lethal-bolt.v1", Category::BlueCounterLethal,
        DecisionKind::Priority, DeckId::Blue, DeckId::Red,
        TurnPhase::FirstMain, 10, 1);
    probe.state.players[0].life = 3;
    probe.state.players[0].hand = {CardId::Counterspell};
    probe.state.players[0].lands.assign(2, land(CardId::Island));
    probe.state.players[1].lands = {land(CardId::Mountain, true)};
    probe.state.stack = {
        spell(kBolt, CardId::LightningBolt, 1,
              Target::player_target(0)),
    };
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "counter-lethal-lightning-bolt",
            PriorityAction::cast_counterspell(kBolt)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe blue_counter_war_probe() {
    constexpr StackObjectId kWaterSpell = 1;
    constexpr StackObjectId kEnemyCounter = 2;
    DecisionProbe probe = make_base(
        "blue.counter-war.v1", Category::BlueCounterWar,
        DecisionKind::Priority, DeckId::Blue, DeckId::Blue,
        TurnPhase::FirstMain, 9);
    probe.state.players[0].hand = {CardId::Counterspell};
    probe.state.players[0].lands = {
        land(CardId::Island, true),
        land(CardId::Island, true),
        land(CardId::Island, true),
        land(CardId::Island, true),
        land(CardId::Island, true),
        land(CardId::Island),
        land(CardId::Island),
    };
    probe.state.players[0].land_played_this_turn = true;
    probe.state.players[1].lands.assign(2, land(CardId::Island, true));
    probe.state.stack = {
        spell(kWaterSpell, CardId::WaterElemental, 0),
        spell(kEnemyCounter, CardId::Counterspell, 1, std::nullopt,
              kWaterSpell),
    };
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "counter-own-water-elemental",
            PriorityAction::cast_counterspell(kWaterSpell)),
        priority_candidate(
            "counter-opponent-counterspell",
            PriorityAction::cast_counterspell(kEnemyCounter)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe white_emergency_moat_probe() {
    constexpr PermanentId kFire = 1;
    DecisionProbe probe = make_base(
        "white.emergency-moat.v1", Category::WhiteEmergencyMoat,
        DecisionKind::Priority, DeckId::White, DeckId::Red,
        TurnPhase::FirstMain, 11);
    probe.state.players[0].life = 5;
    probe.state.players[0].hand = {CardId::Moat};
    probe.state.players[0].lands.assign(4, land(CardId::Plains));
    probe.state.players[0].land_played_this_turn = true;
    probe.state.players[1].lands.assign(
        5, land(CardId::Mountain, true));
    probe.state.players[1].creatures = {
        creature(kFire, CardId::FireElemental, false, true),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "cast-moat",
            PriorityAction::cast_enchantment(CardId::Moat)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe white_establish_millstone_probe() {
    DecisionProbe probe = make_base(
        "white.establish-millstone.v1",
        Category::WhiteEstablishMillstone, DecisionKind::Priority,
        DeckId::White, DeckId::Blue, TurnPhase::FirstMain, 5);
    probe.state.players[0].hand = {CardId::Millstone, CardId::Moat};
    probe.state.players[0].lands.assign(2, land(CardId::Plains));
    probe.state.players[0].land_played_this_turn = true;
    probe.state.players[1].lands.assign(2, land(CardId::Island));
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "cast-millstone",
            PriorityAction::cast_artifact(CardId::Millstone)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe white_mill_before_draw_probe() {
    constexpr PermanentId kMillstone = 1;
    DecisionProbe probe = make_base(
        "white.mill-before-draw.v1", Category::WhiteMillBeforeDraw,
        DecisionKind::Priority, DeckId::White, DeckId::Blue,
        TurnPhase::SecondMain, 35);
    PlayerState& root = probe.state.players[0];
    root.hand.assign(4, CardId::Moat);
    root.graveyard.assign(15, CardId::Plains);
    root.graveyard.insert(root.graveyard.end(), 2, CardId::Moat);
    root.lands.assign(2, land(CardId::Plains));
    root.artifacts = {
        artifact(kMillstone, CardId::Millstone),
    };
    root.land_played_this_turn = true;

    PlayerState& opponent = probe.state.players[1];
    opponent.graveyard.assign(16, CardId::Island);
    opponent.graveyard.insert(
        opponent.graveyard.end(), 12, CardId::Counterspell);
    opponent.graveyard.insert(
        opponent.graveyard.end(), 5, CardId::WaterElemental);

    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "mill-self",
            PriorityAction::activate_millstone(
                kMillstone, Target::player_target(0))),
        priority_candidate(
            "mill-opponent-before-draw",
            PriorityAction::activate_millstone(
                kMillstone, Target::player_target(1))),
    };
    finish_hidden_zones(probe, 5);
    return probe;
}

DecisionProbe white_avoid_redundant_moat_probe() {
    constexpr PermanentId kTreefolk = 1;
    DecisionProbe probe = make_base(
        "white.avoid-redundant-moat.v1",
        Category::WhiteAvoidRedundantMoat, DecisionKind::Priority,
        DeckId::White, DeckId::Green, TurnPhase::FirstMain, 11);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::Moat, CardId::Millstone};
    root.lands.assign(4, land(CardId::Plains));
    root.enchantments = {CardId::Moat};
    root.land_played_this_turn = true;
    probe.state.players[1].lands.assign(5, land(CardId::Forest));
    probe.state.players[1].creatures = {
        creature(kTreefolk, CardId::IronrootTreefolk),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "cast-millstone",
            PriorityAction::cast_artifact(CardId::Millstone)),
        priority_candidate(
            "cast-redundant-moat",
            PriorityAction::cast_enchantment(CardId::Moat)),
    };
    finish_hidden_zones(probe);
    return probe;
}

bool sorcery_actions_for(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

bool contains_action(const std::vector<PriorityAction>& actions,
                     const PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) !=
           actions.end();
}

bool exact_card_conservation(const DecisionProbe& probe,
                             std::vector<std::string>& errors) {
    bool valid = true;
    try {
        if (probe.root_player >= kPlayerCount) {
            errors.push_back("root player is outside the two seats");
            return false;
        }

        const std::array<DeckId, 2> declared_decks =
            probe.root_player == 0
                ? std::array<DeckId, 2>{probe.root_deck,
                                        probe.opponent_deck}
                : std::array<DeckId, 2>{probe.opponent_deck,
                                        probe.root_deck};
        for (std::size_t player = 0; player < kPlayerCount; ++player) {
            const CardCounts declared =
                counts_of(deck_for(declared_decks[player]));
            const CardCounts original =
                counts_of(probe.original_decks[player]);
            if (probe.original_decks[player].size() != 40 ||
                original != declared) {
                errors.push_back(
                    "original deck does not match its declared 40-card deck");
                valid = false;
            }

            CardCounts physical{};
            const PlayerState& state = probe.state.players[player];
            const auto add = [&](CardId card) {
                ++physical[card_index(card)];
            };
            for (const CardId card : state.library) {
                add(card);
            }
            for (const CardId card : state.hand) {
                add(card);
            }
            for (const CardId card : state.graveyard) {
                add(card);
            }
            for (const CardId card : state.exile) {
                add(card);
            }
            for (const LandPermanent& permanent : state.lands) {
                add(permanent.card);
            }
            for (const CreaturePermanent& permanent : state.creatures) {
                add(permanent.card);
            }
            for (const ArtifactPermanent& permanent : state.artifacts) {
                add(permanent.card);
            }
            for (const CardId card : state.enchantments) {
                add(card);
            }
            for (const StackObject& object : probe.state.stack) {
                if (object.controller == player &&
                    object.kind == StackObjectKind::Spell) {
                    add(object.card);
                }
            }
            std::size_t physical_total = 0;
            for (const std::size_t count : physical) {
                physical_total += count;
            }
            if (physical_total != 40 || physical != original) {
                errors.push_back(
                    "physical zones do not conserve the exact 40-card deck");
                valid = false;
            }
        }
    } catch (const std::exception& exception) {
        errors.push_back(std::string("card conservation failed: ") +
                         exception.what());
        valid = false;
    }
    return valid;
}

bool candidates_are_legal_and_complete(
    const DecisionProbe& probe, std::vector<std::string>& errors) {
    if (probe.root_player >= kPlayerCount) {
        errors.push_back("candidate validation has an invalid root player");
        return false;
    }
    bool valid = true;
    std::unordered_set<std::string> descriptors;
    for (const Candidate& candidate : probe.candidates) {
        if (candidate.descriptor.empty() ||
            !descriptors.insert(candidate.descriptor).second) {
            errors.push_back(
                "candidate descriptors must be nonempty and unique");
            valid = false;
        }
    }

    if (probe.decision_kind == DecisionKind::Priority) {
        const std::vector<PriorityAction> legal = legal_priority_actions(
            probe.state, probe.root_player,
            sorcery_actions_for(probe.phase));
        std::vector<PriorityAction> declared;
        for (const Candidate& candidate : probe.candidates) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            if (action == nullptr) {
                errors.push_back(
                    "priority probe contains an attack candidate");
                valid = false;
                continue;
            }
            if (!contains_action(legal, *action)) {
                errors.push_back(
                    "priority probe contains an illegal candidate");
                valid = false;
            }
            if (contains_action(declared, *action)) {
                errors.push_back(
                    "priority probe contains a duplicate candidate action");
                valid = false;
            }
            declared.push_back(*action);
        }
        if (declared.size() != legal.size()) {
            errors.push_back(
                "priority candidates are not the complete legal action set");
            valid = false;
        } else {
            for (const PriorityAction& action : legal) {
                if (!contains_action(declared, action)) {
                    errors.push_back(
                        "priority candidates omit a legal action");
                    valid = false;
                }
            }
        }
        return valid;
    }

    std::vector<BinaryAttackDecision> declared;
    for (const Candidate& candidate : probe.candidates) {
        const auto* action =
            std::get_if<BinaryAttackDecision>(&candidate.action);
        if (action == nullptr) {
            errors.push_back(
                "attack probe contains a priority candidate");
            valid = false;
            continue;
        }
        if (std::find(declared.begin(), declared.end(), *action) !=
            declared.end()) {
            errors.push_back(
                "attack probe contains a duplicate candidate action");
            valid = false;
        }
        declared.push_back(*action);
    }
    if (declared.size() != 2 ||
        declared[0].attacker != declared[1].attacker ||
        declared[0].include == declared[1].include) {
        errors.push_back(
            "attack probe must offer skip/include for one attacker");
        valid = false;
    }
    return valid;
}

const CreaturePermanent* find_creature(const GameState& state,
                                       std::size_t controller,
                                       PermanentId id) {
    if (controller >= kPlayerCount) {
        return nullptr;
    }
    const auto& creatures = state.players[controller].creatures;
    const auto found = std::find_if(
        creatures.begin(), creatures.end(),
        [id](const CreaturePermanent& candidate) {
            return candidate.id == id;
        });
    return found == creatures.end() ? nullptr : &*found;
}

bool has_moat(const GameState& state) {
    for (const PlayerState& player : state.players) {
        if (std::find(player.enchantments.begin(),
                      player.enchantments.end(),
                      CardId::Moat) != player.enchantments.end()) {
            return true;
        }
    }
    return false;
}

bool reachable_state(const DecisionProbe& probe,
                     std::vector<std::string>& errors) {
    bool valid = true;
    if (probe.root_player >= kPlayerCount ||
        probe.state.active_player >= kPlayerCount ||
        probe.state.starting_player >= kPlayerCount) {
        errors.push_back("probe has an invalid player index");
        return false;
    }
    if (probe.state.turn_number == 0 ||
        (probe.state.starting_player + probe.state.turn_number - 1) %
                kPlayerCount !=
            probe.state.active_player) {
        errors.push_back(
            "turn number, starting player, and active player disagree");
        valid = false;
    }
    if (probe.consecutive_passes < 0 ||
        probe.consecutive_passes > 1) {
        errors.push_back(
            "priority pass context must be zero or one prior pass");
        valid = false;
    }
    if (probe.decision_kind == DecisionKind::Attack) {
        if (probe.phase != TurnPhase::DeclareAttackers ||
            probe.state.active_player != probe.root_player ||
            probe.consecutive_passes != 0 ||
            !probe.state.stack.empty()) {
            errors.push_back(
                "attack probe is outside a reachable declaration window");
            valid = false;
        }
    } else if (probe.phase == TurnPhase::DeclareAttackers ||
               probe.phase == TurnPhase::DeclareBlockers ||
               probe.phase == TurnPhase::DamageOrder) {
        errors.push_back(
            "priority probe uses a combat-choice-only phase");
        valid = false;
    }

    std::unordered_set<PermanentId> permanent_ids;
    std::unordered_set<StackObjectId> stack_ids;
    try {
        for (const PlayerState& player : probe.state.players) {
            if (player.life <= 0) {
                errors.push_back(
                    "decision probe cannot start after a player lost");
                valid = false;
            }
            for (const LandPermanent& permanent : player.lands) {
                if (card_definition(permanent.card).type !=
                    CardType::Land) {
                    errors.push_back("nonland is in the land zone");
                    valid = false;
                }
            }
            for (const CreaturePermanent& permanent :
                 player.creatures) {
                const CardDefinition& definition =
                    card_definition(permanent.card);
                if (definition.type != CardType::Creature ||
                    permanent.damage < 0 ||
                    permanent.temporary_power_bonus < 0 ||
                    permanent.temporary_toughness_bonus < 0 ||
                    permanent.damage >=
                        definition.toughness +
                            permanent.temporary_toughness_bonus) {
                    errors.push_back(
                        "creature zone contains a dead or invalid creature");
                    valid = false;
                }
                if (permanent.id == 0 ||
                    !permanent_ids.insert(permanent.id).second) {
                    errors.push_back(
                        "permanent IDs must be nonzero and unique");
                    valid = false;
                }
            }
            for (const ArtifactPermanent& permanent :
                 player.artifacts) {
                if (card_definition(permanent.card).type !=
                    CardType::Artifact) {
                    errors.push_back(
                        "nonartifact is in the artifact zone");
                    valid = false;
                }
                if (permanent.id == 0 ||
                    !permanent_ids.insert(permanent.id).second) {
                    errors.push_back(
                        "permanent IDs must be nonzero and unique");
                    valid = false;
                }
            }
            for (const CardId card : player.enchantments) {
                if (card_definition(card).type !=
                    CardType::Enchantment) {
                    errors.push_back(
                        "nonenchantment is in the enchantment zone");
                    valid = false;
                }
            }
        }

        for (const StackObject& object : probe.state.stack) {
            if (object.controller >= kPlayerCount || object.id == 0 ||
                !stack_ids.insert(object.id).second) {
                errors.push_back(
                    "stack object has an invalid controller or ID");
                valid = false;
            }
            if (object.x_value < 0 ||
                (object.x_value != 0 &&
                 (object.kind != StackObjectKind::Spell ||
                  object.card != CardId::Disintegrate))) {
                errors.push_back(
                    "stack object has an invalid public X value");
                valid = false;
            }
            if (object.kind == StackObjectKind::Spell &&
                card_definition(object.card).type == CardType::Land) {
                errors.push_back("a land cannot be a stack spell");
                valid = false;
            }
            if (object.target.has_value()) {
                const Target& target = *object.target;
                if (target.player >= kPlayerCount ||
                    (target.creature.has_value() &&
                     find_creature(probe.state, target.player,
                                   *target.creature) == nullptr)) {
                    errors.push_back(
                        "stack object has an unreachable target");
                    valid = false;
                }
            }
            if (object.spell_target.has_value()) {
                const bool target_below = std::any_of(
                    probe.state.stack.begin(), probe.state.stack.end(),
                    [&](const StackObject& candidate) {
                        return candidate.id == *object.spell_target &&
                               candidate.id != object.id &&
                               candidate.kind ==
                                   StackObjectKind::Spell;
                    });
                if (!target_below) {
                    errors.push_back(
                        "counterspell has no valid spell target");
                    valid = false;
                }
            }
        }
    } catch (const std::exception& exception) {
        errors.push_back(std::string("state reachability failed: ") +
                         exception.what());
        valid = false;
    }

    if (!permanent_ids.empty() &&
        probe.state.next_permanent_id <=
            *std::max_element(permanent_ids.begin(),
                              permanent_ids.end())) {
        errors.push_back(
            "next permanent ID does not follow public permanents");
        valid = false;
    }
    if (!stack_ids.empty() &&
        probe.state.next_stack_object_id <=
            *std::max_element(stack_ids.begin(), stack_ids.end())) {
        errors.push_back(
            "next stack ID does not follow public stack objects");
        valid = false;
    }

    if (probe.decision_kind == DecisionKind::Attack) {
        if (probe.candidates.empty()) {
            errors.push_back("attack probe has no attack subject");
            return false;
        }
        const auto* first = std::get_if<BinaryAttackDecision>(
            &probe.candidates.front().action);
        if (first == nullptr) {
            errors.push_back("attack probe has no attack subject");
            return false;
        }
        const CreaturePermanent* attacker = find_creature(
            probe.state, probe.root_player, first->attacker);
        if (attacker == nullptr || attacker->tapped ||
            attacker->summoning_sick ||
            (has_moat(probe.state) &&
             !card_definition(attacker->card).flying)) {
            errors.push_back(
                "binary attack subject cannot legally attack");
            valid = false;
        }
    }
    return valid;
}

bool sampled_hidden_zones_equal(const GameState& left,
                                const GameState& right) {
    for (std::size_t player = 0; player < kPlayerCount; ++player) {
        if (left.players[player].hand !=
                right.players[player].hand ||
            left.players[player].library !=
                right.players[player].library) {
            return false;
        }
    }
    return true;
}

} // namespace

bool Validation::ok() const {
    return exact_card_conservation &&
           candidates_legal_and_complete && reachable_state &&
           hidden_clone_invariant && errors.empty();
}

std::vector<DecisionProbe> make_probe_dev_v1() {
    std::vector<DecisionProbe> probes;
    probes.reserve(kCategoryCount);
    probes.push_back(green_develop_probe());
    probes.push_back(green_tsunami_probe());
    probes.push_back(green_favorable_attack_probe());
    probes.push_back(green_unfavorable_attack_probe());
    probes.push_back(red_face_lethal_probe());
    probes.push_back(red_clear_blocker_probe());
    probes.push_back(red_finish_damaged_probe());
    probes.push_back(red_stack_race_probe());
    probes.push_back(blue_counter_expensive_probe());
    probes.push_back(blue_conserve_counter_probe());
    probes.push_back(blue_counter_lethal_probe());
    probes.push_back(blue_counter_war_probe());
    probes.push_back(white_emergency_moat_probe());
    probes.push_back(white_establish_millstone_probe());
    probes.push_back(white_mill_before_draw_probe());
    probes.push_back(white_avoid_redundant_moat_probe());
    return probes;
}

std::vector<DecisionProbe> make_probe_dev_v2() {
    std::vector<DecisionProbe> probes = make_probe_dev_v1();
    for (DecisionProbe& probe : probes) {
        const auto expose_creature =
            [&probe](std::size_t player, CardId card,
                     PermanentId id) {
                PlayerState& state = probe.state.players[player];
                auto found = std::find(
                    state.library.begin(), state.library.end(),
                    card);
                if (found != state.library.end()) {
                    state.library.erase(found);
                } else {
                    found = std::find(
                        state.hand.begin(), state.hand.end(), card);
                    if (found == state.hand.end()) {
                        throw std::logic_error(
                            "probe-dev-v2 cannot expose missing creature");
                    }
                    state.hand.erase(found);
                }
                state.creatures.push_back(
                    creature(id, card, false, false));
                probe.state.next_permanent_id =
                    std::max(probe.state.next_permanent_id, id + 1);
            };
        const std::size_t suffix = probe.stable_id.rfind(".v1");
        if (suffix == std::string::npos ||
            suffix + 3 != probe.stable_id.size()) {
            throw std::logic_error(
                "probe-dev-v1 contains an unversioned stable ID");
        }
        probe.stable_id.replace(suffix, 3, ".v2");

        switch (probe.category) {
        case Category::GreenDevelop:
            // Passing in the second main with the opponent tapped out ends
            // the turn, so the Actor cannot heal the branch by casting the
            // same creature in a later priority window.
            probe.phase = TurnPhase::SecondMain;
            for (LandPermanent& land :
                 probe.state.players[1].lands) {
                land.tapped = true;
            }
            // A Bear cast now is a blocker next turn; waiting exposes a
            // real tempo cost instead of two branches that inevitably
            // reconverge.
            probe.state.players[0].life = 5;
            expose_creature(
                1, CardId::FireElemental,
                probe.state.next_permanent_id);
            break;
        case Category::RedFaceLethal:
            // "Pass" now means decline the lethal Bolt this turn.
            probe.phase = TurnPhase::SecondMain;
            // Waiting gives the visible Water Elemental a lethal attack
            // before Red receives another main phase.
            probe.state.players[0].life = 5;
            break;
        case Category::BlueCounterLethal:
            // Countering now exposes a concrete winning continuation rather
            // than a branch whose correct defense is eventually scored as
            // the same terminal loss at long horizons.
            probe.state.players[1].life = 5;
            expose_creature(
                0, CardId::WaterElemental,
                probe.state.next_permanent_id);
            break;
        case Category::WhiteEmergencyMoat:
            // The attacker must actually threaten this combat. V1
            // accidentally marked it summoning-sick, making Moat optional
            // until a later turn.
            if (probe.state.players[1].creatures.size() != 1) {
                throw std::logic_error(
                    "emergency-Moat fixture lost its attacker");
            }
            probe.state.players[1]
                .creatures.front()
                .summoning_sick = false;
            break;
        case Category::WhiteEstablishMillstone:
        case Category::WhiteAvoidRedundantMoat:
            // These are "develop now versus wait a turn" probes. Ending the
            // second-main window makes the wait branch root-irreversible.
            probe.phase = TurnPhase::SecondMain;
            break;
        default:
            break;
        }
    }
    return probes;
}

bool hidden_clone_is_determinization_invariant(
    const DecisionProbe& probe, std::uint64_t seed) {
    if (probe.root_player >= kPlayerCount) {
        return false;
    }
    GameState hidden_clone = probe.state;
    std::reverse(
        hidden_clone.players[probe.root_player].library.begin(),
        hidden_clone.players[probe.root_player].library.end());

    const std::size_t opponent = 1 - probe.root_player;
    PlayerState& opponent_clone = hidden_clone.players[opponent];
    const std::size_t hand_size = opponent_clone.hand.size();
    std::vector<CardId> opponent_hidden = opponent_clone.hand;
    opponent_hidden.insert(opponent_hidden.end(),
                           opponent_clone.library.begin(),
                           opponent_clone.library.end());
    if (opponent_hidden.size() > 1) {
        std::rotate(opponent_hidden.begin(),
                    opponent_hidden.begin() + 1,
                    opponent_hidden.end());
        std::reverse(opponent_hidden.begin(), opponent_hidden.end());
    }
    const auto hand_end =
        opponent_hidden.begin() +
        static_cast<std::ptrdiff_t>(hand_size);
    opponent_clone.hand.assign(opponent_hidden.begin(), hand_end);
    opponent_clone.library.assign(hand_end, opponent_hidden.end());

    try {
        const GameState first = sample_determinization(
            probe.state, probe.original_decks, probe.root_player, seed);
        const GameState second = sample_determinization(
            hidden_clone, probe.original_decks, probe.root_player, seed);
        return sampled_hidden_zones_equal(first, second);
    } catch (const std::exception&) {
        return false;
    }
}

Validation validate_probe(const DecisionProbe& probe,
                          std::uint64_t hidden_seed) {
    Validation validation;
    validation.exact_card_conservation =
        exact_card_conservation(probe, validation.errors);
    validation.candidates_legal_and_complete =
        candidates_are_legal_and_complete(probe, validation.errors);
    validation.reachable_state =
        reachable_state(probe, validation.errors);
    validation.hidden_clone_invariant =
        hidden_clone_is_determinization_invariant(probe, hidden_seed);
    if (!validation.hidden_clone_invariant) {
        validation.errors.push_back(
            "hidden-zone clone changed fixed-seed determinization");
    }
    return validation;
}

std::vector<std::string> validate_probe_dev_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed) {
    std::vector<std::string> errors;
    if (probes.size() != kCategoryCount) {
        errors.push_back("probe-dev-v1 must contain exactly 16 probes");
    }

    std::unordered_set<std::string> stable_ids;
    std::array<bool, kCategoryCount> categories{};
    std::array<std::size_t, 4> root_deck_counts{};
    for (const DecisionProbe& probe : probes) {
        if (probe.stable_id.empty() ||
            !stable_ids.insert(probe.stable_id).second) {
            errors.push_back(
                "probe stable IDs must be nonempty and unique");
        }
        const std::size_t category =
            static_cast<std::size_t>(probe.category);
        if (category >= categories.size() || categories[category]) {
            errors.push_back(
                "probe categories must appear exactly once");
        } else {
            categories[category] = true;
        }
        const std::size_t deck =
            static_cast<std::size_t>(probe.root_deck);
        if (deck >= root_deck_counts.size()) {
            errors.push_back("probe has an unknown root deck");
        } else {
            ++root_deck_counts[deck];
        }

        const Validation validation =
            validate_probe(probe, hidden_seed);
        for (const std::string& error : validation.errors) {
            errors.push_back(probe.stable_id + ": " + error);
        }
    }
    for (const bool present : categories) {
        if (!present) {
            errors.push_back("probe-dev-v1 omits a category");
        }
    }
    for (const std::size_t count : root_deck_counts) {
        if (count != 4) {
            errors.push_back(
                "probe-dev-v1 requires four probes per root deck");
        }
    }
    return errors;
}

std::vector<std::string> validate_probe_dev_v2(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed) {
    std::vector<std::string> errors =
        validate_probe_dev_v1(probes, hidden_seed);
    for (std::string& error : errors) {
        const std::string old_name = "probe-dev-v1";
        const std::size_t position = error.find(old_name);
        if (position != std::string::npos) {
            error.replace(position, old_name.size(), "probe-dev-v2");
        }
    }
    for (const DecisionProbe& probe : probes) {
        if (!probe.stable_id.ends_with(".v2")) {
            errors.push_back(
                "probe-dev-v2 stable IDs must end in .v2");
        }
    }
    return errors;
}

} // namespace old_school::probes
