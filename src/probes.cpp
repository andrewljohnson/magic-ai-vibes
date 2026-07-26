#include "old_school/probes.hpp"
#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace old_school::probes {
namespace {

constexpr std::size_t kPlayerCount = 2;
constexpr std::size_t kProbeCardCount = kCardCount;
constexpr std::size_t kLegacyCategoryCount =
    static_cast<std::size_t>(Category::WhiteAvoidRedundantMoat) + 1;
constexpr std::size_t kProbeDevV3Count = 20;
constexpr std::array<Category, kProbeDevV3Count>
    kProbeDevV3Categories = {
        Category::GreenDevelop,
        Category::GreenGrowthSaveBolt,
        Category::GreenGrowthPushCombat,
        Category::GreenGrowthHold,
        Category::RedFaceLethal,
        Category::RedClearBlocker,
        Category::RedFinishDamagedThreat,
        Category::RedStackRace,
        Category::BlueCounterExpensiveSpell,
        Category::BlueForceSpike,
        Category::BlueCounterLethal,
        Category::BlueCounterWar,
        Category::WhiteEmergencyMoat,
        Category::WhiteEstablishMillstone,
        Category::WhiteMillBeforeDraw,
        Category::WhiteAvoidRedundantMoat,
        Category::RULandColor,
        Category::RUBlockerDevelopment,
        Category::RUFlyingMoatAttack,
        Category::RUDisintegrateLethal,
    };
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
        return ru_aggro_deck();
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

Candidate block_candidate(std::string descriptor, PermanentId attacker,
                          PermanentId blocker, bool include) {
    return {
        .descriptor = std::move(descriptor),
        .action = BinaryBlockDecision{
            .attacker = attacker,
            .blocker = blocker,
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
    probe.state.players[1].lands.assign(
        5, land(CardId::Mountain));
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

DecisionProbe green_growth_save_bolt_probe() {
    constexpr PermanentId kBear = 1;
    constexpr StackObjectId kBolt = 1;
    DecisionProbe probe = make_base(
        "green.bolt-on-bear-response.v3",
        Category::GreenGrowthSaveBolt, DecisionKind::Priority,
        DeckId::Green, DeckId::Red, TurnPhase::FirstMain, 10, 1);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::GiantGrowth};
    root.lands = {
        land(CardId::Forest),
        land(CardId::Forest),
    };
    root.creatures = {
        creature(kBear, CardId::GrizzlyBears),
    };
    probe.state.players[1].lands = {
        land(CardId::Mountain, true),
    };
    probe.state.stack = {
        spell(kBolt, CardId::LightningBolt, 1,
              Target::creature_target(0, kBear)),
    };
    // The Bolt caster has passed; passing now resolves it, so this is the
    // last legal opportunity to save the Bear.
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "growth-own-grizzly-bears",
            PriorityAction::cast_giant_growth(
                Target::creature_target(0, kBear))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe green_growth_push_probe() {
    constexpr PermanentId kTreefolk = 1;
    constexpr PermanentId kSickBear = 2;
    DecisionProbe probe = make_base(
        "green.begin-combat-growth-targets.v3",
        Category::GreenGrowthPushCombat, DecisionKind::Priority,
        DeckId::Green, DeckId::Red, TurnPhase::BeginCombat, 9);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::GiantGrowth};
    root.lands = {
        land(CardId::Forest, true),
        land(CardId::Forest, true),
        land(CardId::Forest),
        land(CardId::Forest),
        land(CardId::Forest),
    };
    root.creatures = {
        creature(kTreefolk, CardId::IronrootTreefolk),
        creature(kSickBear, CardId::GrizzlyBears, false, true),
    };
    root.land_played_this_turn = true;
    PlayerState& opponent = probe.state.players[1];
    opponent.life = 6;
    opponent.lands.assign(5, land(CardId::Mountain, true));
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "growth-own-ironroot-treefolk",
            PriorityAction::cast_giant_growth(
                Target::creature_target(0, kTreefolk))),
        priority_candidate(
            "growth-own-summoning-sick-grizzly-bears",
            PriorityAction::cast_giant_growth(
                Target::creature_target(0, kSickBear))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe green_growth_hold_probe() {
    constexpr PermanentId kBear = 1;
    DecisionProbe probe = make_base(
        "green.second-main-growth-options.v3",
        Category::GreenGrowthHold, DecisionKind::Priority,
        DeckId::Green, DeckId::Red, TurnPhase::SecondMain, 11);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::GiantGrowth};
    root.lands.assign(5, land(CardId::Forest));
    root.creatures = {
        creature(kBear, CardId::GrizzlyBears),
    };
    root.land_played_this_turn = true;
    probe.state.players[1].lands.assign(
        5, land(CardId::Mountain, true));
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "growth-own-grizzly-bears",
            PriorityAction::cast_giant_growth(
                Target::creature_target(0, kBear))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe field_ru_flying_men_chump_air_probe(
    int life, std::string stable_id, Category category) {
    constexpr PermanentId kFlyingMen = 1;
    constexpr PermanentId kAirElemental = 2;
    DecisionProbe probe = make_base(
        std::move(stable_id), category, DecisionKind::Block,
        DeckId::RUAggro, DeckId::Blue,
        TurnPhase::DeclareBlockers, 10, 1);
    PlayerState& defender = probe.state.players[0];
    defender.life = life;
    defender.lands = {land(CardId::Island)};
    defender.creatures = {
        creature(kFlyingMen, CardId::FlyingMen),
    };
    PlayerState& attacker = probe.state.players[1];
    attacker.lands.assign(5, land(CardId::Island, true));
    // Attackers are tapped as they enter the Declare Blockers state.
    attacker.creatures = {
        creature(kAirElemental, CardId::AirElemental, true),
    };
    probe.candidates = {
        block_candidate(
            "no-blocks", kAirElemental, kFlyingMen, false),
        block_candidate(
            "block-air-elemental-with-flying-men",
            kAirElemental, kFlyingMen, true),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe diagnostic_ru_attack_flying_into_air_probe() {
    constexpr PermanentId kFlyingAttacker = 1;
    constexpr PermanentId kFlyingBlocker = 2;
    DecisionProbe probe = make_base(
        "diagnostic.ru.life20-flying-men-attack-air.v1",
        Category::DiagnosticRUAttackFlyingIntoLargerFlyingBlocker,
        DecisionKind::Attack, DeckId::RUAggro, DeckId::Blue,
        TurnPhase::DeclareAttackers, 10);
    PlayerState& root = probe.state.players[0];
    root.life = 20;
    root.lands = {land(CardId::Island)};
    root.creatures = {
        creature(kFlyingAttacker, CardId::FlyingMen),
    };
    PlayerState& opponent = probe.state.players[1];
    opponent.life = 20;
    opponent.lands.assign(5, land(CardId::Island));
    opponent.creatures = {
        creature(kFlyingBlocker, CardId::AirElemental),
    };
    probe.candidates = {
        attack_candidate(
            "no-attack", kFlyingAttacker, false),
        attack_candidate(
            "attack-with-only-legal-attacker",
            kFlyingAttacker, true),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe field_green_second_main_sick_bear_growth_probe() {
    constexpr PermanentId kBear = 1;
    DecisionProbe probe = make_base(
        "field.green.second-main-sick-bear-growth.v1",
        Category::FieldGreenSecondMainSickBearGrowth,
        DecisionKind::Priority, DeckId::Green, DeckId::Red,
        TurnPhase::SecondMain, 9);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::GiantGrowth};
    root.lands = {
        land(CardId::Forest, true),
        land(CardId::Forest, true),
        land(CardId::Forest),
    };
    root.creatures = {
        creature(kBear, CardId::GrizzlyBears, false, true),
    };
    root.land_played_this_turn = true;
    probe.state.players[1].lands.assign(
        5, land(CardId::Mountain, true));
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "growth-own-summoning-sick-grizzly-bears",
            PriorityAction::cast_giant_growth(
                Target::creature_target(0, kBear))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe field_green_begin_combat_growth_tapped_air_probe() {
    constexpr PermanentId kTreefolk = 1;
    constexpr PermanentId kAirElemental = 2;
    DecisionProbe probe = make_base(
        "field.green.begin-combat-growth-tapped-air.v1",
        Category::FieldGreenBeginCombatGrowthTappedAir,
        DecisionKind::Priority, DeckId::Green, DeckId::Blue,
        TurnPhase::BeginCombat, 9);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::GiantGrowth};
    root.lands = {
        land(CardId::Forest, true),
        land(CardId::Forest, true),
        land(CardId::Forest, true),
        land(CardId::Forest, true),
        land(CardId::Forest),
    };
    root.creatures = {
        creature(kTreefolk, CardId::IronrootTreefolk),
    };
    root.land_played_this_turn = true;
    PlayerState& opponent = probe.state.players[1];
    opponent.life = 6;
    opponent.lands.assign(5, land(CardId::Island, true));
    opponent.creatures = {
        creature(kAirElemental, CardId::AirElemental, true),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "growth-own-ironroot-treefolk",
            PriorityAction::cast_giant_growth(
                Target::creature_target(0, kTreefolk))),
        priority_candidate(
            "growth-opponent-tapped-air-elemental",
            PriorityAction::cast_giant_growth(
                Target::creature_target(1, kAirElemental))),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe field_green_attack_after_growth_air_probe(
    bool air_tapped, std::string stable_id, Category category) {
    constexpr PermanentId kTreefolk = 1;
    constexpr PermanentId kAirElemental = 2;
    const DecisionProbe source =
        field_green_begin_combat_growth_tapped_air_probe();

    GameState successor = source.state;
    const PriorityAction growth =
        PriorityAction::cast_giant_growth(
            Target::creature_target(1, kAirElemental));
    if (!apply_priority_action(
            successor, source.root_player, growth, false) ||
        !resolve_top_of_stack(successor)) {
        throw std::logic_error(
            "field Growth fixture could not construct its linked "
            "attack successor");
    }
    CreaturePermanent* air = nullptr;
    for (CreaturePermanent& candidate :
         successor.players[1].creatures) {
        if (candidate.id == kAirElemental) {
            air = &candidate;
            break;
        }
    }
    if (air == nullptr) {
        throw std::logic_error(
            "field Growth successor lost Air Elemental");
    }
    air->tapped = air_tapped;

    DecisionProbe probe;
    probe.stable_id = std::move(stable_id);
    probe.category = category;
    probe.decision_kind = DecisionKind::Attack;
    probe.root_deck = source.root_deck;
    probe.opponent_deck = source.opponent_deck;
    probe.root_player = source.root_player;
    probe.phase = TurnPhase::DeclareAttackers;
    probe.consecutive_passes = 0;
    probe.state = std::move(successor);
    probe.original_decks = source.original_decks;
    probe.candidates = {
        attack_candidate(
            "skip-ironroot-treefolk", kTreefolk, false),
        attack_candidate(
            "include-ironroot-treefolk", kTreefolk, true),
    };
    return probe;
}

DecisionProbe red_face_lethal_probe() {
    constexpr PermanentId kAir = 1;
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
        creature(kAir, CardId::AirElemental, false, true),
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
            "bolt-opponent-air-elemental",
            PriorityAction::cast_lightning_bolt(
                Target::creature_target(1, kAir))),
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
    constexpr PermanentId kAir = 1;
    DecisionProbe probe = make_base(
        "red.finish-damaged-air.v1",
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
        creature(kAir, CardId::AirElemental, false, true, 3),
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
            "bolt-damaged-air-elemental",
            PriorityAction::cast_lightning_bolt(
                Target::creature_target(1, kAir))),
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
    probe.state.players[0].lands.assign(5, land(CardId::Island));
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

DecisionProbe blue_force_spike_probe() {
    constexpr StackObjectId kGrayOgre = 1;
    DecisionProbe probe = make_base(
        "blue.force-spike-tapped-out-gray-ogre.v1",
        Category::BlueForceSpike, DecisionKind::Priority,
        DeckId::Blue, DeckId::Red, TurnPhase::FirstMain, 6, 1);
    probe.state.players[0].hand = {CardId::ForceSpike};
    probe.state.players[0].lands = {land(CardId::Island)};
    probe.state.players[1].lands.assign(
        3, land(CardId::Mountain, true));
    probe.state.stack = {
        spell(kGrayOgre, CardId::GrayOgre, 1),
    };
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "force-spike-gray-ogre",
            PriorityAction::cast_force_spike(kGrayOgre)),
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
    probe.state.players[0].lands.assign(5, land(CardId::Island));
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
    constexpr StackObjectId kAirSpell = 1;
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
        spell(kAirSpell, CardId::AirElemental, 0),
        spell(kEnemyCounter, CardId::Counterspell, 1, std::nullopt,
              kAirSpell),
    };
    probe.consecutive_passes = 1;
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "counter-own-air-elemental",
            PriorityAction::cast_counterspell(kAirSpell)),
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
    opponent.graveyard.assign(15, CardId::Island);
    opponent.graveyard.insert(
        opponent.graveyard.end(), 8, CardId::Counterspell);
    opponent.graveyard.insert(
        opponent.graveyard.end(), 4, CardId::AirElemental);
    opponent.graveyard.insert(
        opponent.graveyard.end(), 4, CardId::FlyingMen);
    opponent.graveyard.insert(
        opponent.graveyard.end(), 2, CardId::ForceSpike);

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

DecisionProbe ru_land_color_probe() {
    constexpr PermanentId kBear = 1;
    DecisionProbe probe = make_base(
        "ru.second-main-land-colors.v3", Category::RULandColor,
        DecisionKind::Priority, DeckId::RUAggro, DeckId::Green,
        TurnPhase::SecondMain, 5);
    PlayerState& root = probe.state.players[0];
    root.life = 2;
    root.hand = {
        CardId::Mountain,
        CardId::Island,
        CardId::FlyingMen,
    };
    root.lands = {land(CardId::Mountain, true)};
    PlayerState& opponent = probe.state.players[1];
    opponent.lands.assign(2, land(CardId::Forest, true));
    opponent.creatures = {
        creature(kBear, CardId::GrizzlyBears),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "play-mountain",
            PriorityAction::play_land(CardId::Mountain)),
        priority_candidate(
            "play-island",
            PriorityAction::play_land(CardId::Island)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe ru_blocker_development_probe() {
    constexpr PermanentId kBear = 1;
    DecisionProbe probe = make_base(
        "ru.second-main-blocker-development.v3",
        Category::RUBlockerDevelopment, DecisionKind::Priority,
        DeckId::RUAggro, DeckId::Green, TurnPhase::SecondMain, 7);
    PlayerState& root = probe.state.players[0];
    root.life = 2;
    root.hand = {
        CardId::IronclawOrcs,
        CardId::GrayOgre,
    };
    root.lands = {
        land(CardId::Mountain),
        land(CardId::Mountain),
        land(CardId::Island),
    };
    root.land_played_this_turn = true;
    PlayerState& opponent = probe.state.players[1];
    opponent.lands.assign(2, land(CardId::Forest, true));
    opponent.creatures = {
        creature(kBear, CardId::GrizzlyBears),
    };
    probe.candidates = {
        priority_candidate("pass", PriorityAction::pass()),
        priority_candidate(
            "cast-ironclaw-orcs",
            PriorityAction::cast_creature(CardId::IronclawOrcs)),
        priority_candidate(
            "cast-gray-ogre",
            PriorityAction::cast_creature(CardId::GrayOgre)),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe ru_flying_moat_attack_probe() {
    constexpr PermanentId kFlyingMen = 1;
    DecisionProbe probe = make_base(
        "ru.flying-men-moat-attack.v3",
        Category::RUFlyingMoatAttack, DecisionKind::Attack,
        DeckId::RUAggro, DeckId::White, TurnPhase::DeclareAttackers, 9);
    probe.state.players[0].lands = {
        land(CardId::Island),
    };
    probe.state.players[0].creatures = {
        creature(kFlyingMen, CardId::FlyingMen),
    };
    PlayerState& opponent = probe.state.players[1];
    opponent.life = 1;
    opponent.lands.assign(4, land(CardId::Plains));
    opponent.enchantments = {CardId::Moat};
    probe.candidates = {
        attack_candidate("skip-flying-men", kFlyingMen, false),
        attack_candidate("include-flying-men", kFlyingMen, true),
    };
    finish_hidden_zones(probe);
    return probe;
}

DecisionProbe ru_disintegrate_lethal_probe() {
    DecisionProbe probe = make_base(
        "ru.disintegrate-player-x.v3",
        Category::RUDisintegrateLethal, DecisionKind::Priority,
        DeckId::RUAggro, DeckId::Blue, TurnPhase::SecondMain, 9);
    PlayerState& root = probe.state.players[0];
    root.hand = {CardId::Disintegrate};
    root.lands = {
        land(CardId::Mountain),
        land(CardId::Mountain),
        land(CardId::Mountain),
        land(CardId::Island),
    };
    root.land_played_this_turn = true;
    PlayerState& opponent = probe.state.players[1];
    opponent.life = 3;
    opponent.lands.assign(2, land(CardId::Island, true));

    probe.candidates.push_back(
        priority_candidate("pass", PriorityAction::pass()));
    for (int x_value = 0; x_value <= 3; ++x_value) {
        probe.candidates.push_back(priority_candidate(
            "disintegrate-x" + std::to_string(x_value) +
                "-self-player",
            PriorityAction::cast_disintegrate(
                x_value, Target::player_target(0))));
        probe.candidates.push_back(priority_candidate(
            "disintegrate-x" + std::to_string(x_value) +
                "-opponent-player",
            PriorityAction::cast_disintegrate(
                x_value, Target::player_target(1))));
    }
    finish_hidden_zones(probe);
    return probe;
}

std::string harvested_priority_descriptor(
    const PriorityAction& action) {
    std::string descriptor =
        "kind-" +
        std::to_string(static_cast<std::size_t>(action.kind)) +
        ".card-" +
        std::to_string(static_cast<std::size_t>(action.card)) +
        ".x-" + std::to_string(action.x_value);
    if (action.target.has_value()) {
        descriptor +=
            ".target-player-" +
            std::to_string(action.target->player);
        if (action.target->creature.has_value()) {
            descriptor +=
                ".creature-" +
                std::to_string(*action.target->creature);
        }
    }
    if (action.spell_target.has_value()) {
        descriptor +=
            ".spell-" + std::to_string(*action.spell_target);
    }
    if (action.source_permanent.has_value()) {
        descriptor +=
            ".source-" + std::to_string(*action.source_permanent);
    }
    return descriptor;
}

struct PriorityCallbackHarvest {
    Game* game = nullptr;
    std::size_t next_priority_decision_ordinal = 0;
    std::optional<GameState> state;
    std::vector<PriorityAction> actions;
    std::size_t captured_priority_decision_ordinal = 0;
    TurnPhase phase = TurnPhase::FirstMain;
};

struct PriorityCallbackHarvestComplete {};

HumanController land_then_pass_controller(
    std::size_t controlled_player, PriorityCallbackHarvest& harvest) {
    HumanController controller;
    controller.choose_priority_action =
        [controlled_player, &harvest](
            const PlayerObservation&, TurnPhase phase,
            const std::vector<PriorityAction>& actions) {
            const std::size_t ordinal =
                harvest.next_priority_decision_ordinal++;
            if (harvest.game == nullptr) {
                throw std::logic_error(
                    "priority harvest callback has no running game");
            }

            constexpr std::size_t kRootPlayer = 0;
            constexpr std::size_t kOpponent = 1;
            const GameState& state = harvest.game->state();
            bool has_x_zero = false;
            bool has_affordable_lethal_x = false;
            for (const PriorityAction& action : actions) {
                if (action.kind !=
                        PriorityActionKind::CastDisintegrate ||
                    !action.target.has_value() ||
                    action.target->creature.has_value() ||
                    action.target->player != kOpponent) {
                    continue;
                }
                has_x_zero = has_x_zero || action.x_value == 0;
                has_affordable_lethal_x =
                    has_affordable_lethal_x ||
                    action.x_value >=
                        state.players[kOpponent].life;
            }

            if (controlled_player == kRootPlayer &&
                phase == TurnPhase::SecondMain &&
                state.active_player == kRootPlayer &&
                state.stack.empty() &&
                state.players[kRootPlayer].land_played_this_turn &&
                has_x_zero &&
                !has_affordable_lethal_x) {
                harvest.state = state;
                harvest.actions = actions;
                harvest.captured_priority_decision_ordinal =
                    ordinal;
                harvest.phase = phase;
                throw PriorityCallbackHarvestComplete{};
            }

            const auto choose_action =
                [&actions](const auto& predicate)
                    -> std::optional<std::size_t> {
                    const auto found = std::find_if(
                        actions.begin(), actions.end(), predicate);
                    if (found == actions.end()) {
                        return std::nullopt;
                    }
                    return static_cast<std::size_t>(
                        std::distance(actions.begin(), found));
                };

            // The fixed trajectory develops mana but deliberately casts no
            // spells. Prefer a Mountain for RU, then any legal land.
            if (controlled_player == kRootPlayer) {
                const auto mountain = choose_action(
                    [](const PriorityAction& action) {
                        return action.kind ==
                                   PriorityActionKind::PlayLand &&
                               action.card == CardId::Mountain;
                    });
                if (mountain.has_value()) {
                    return *mountain;
                }
            }
            const auto land_action = choose_action(
                [](const PriorityAction& action) {
                    return action.kind ==
                           PriorityActionKind::PlayLand;
                });
            if (land_action.has_value()) {
                return *land_action;
            }
            const auto pass = choose_action(
                [](const PriorityAction& action) {
                    return action.kind == PriorityActionKind::Pass;
                });
            if (!pass.has_value()) {
                throw std::logic_error(
                    "priority harvest decision has no Pass");
            }
            return *pass;
        };
    controller.choose_attackers =
        [](const PlayerObservation&,
           const std::vector<PermanentId>&) {
            return std::vector<PermanentId>{};
        };
    controller.choose_blockers =
        [](const PlayerObservation&,
           const std::vector<PermanentId>&,
           const std::vector<LegalBlockerChoice>&) {
            return std::vector<
                std::pair<PermanentId, PermanentId>>{};
        };
    controller.choose_damage_order =
        [](const PlayerObservation&, PermanentId,
           const std::vector<PermanentId>& blockers) {
            return blockers;
        };
    controller.choose_cleanup_discards =
        [](const PlayerObservation&, std::size_t excess) {
            std::vector<std::size_t> indices(excess);
            std::iota(indices.begin(), indices.end(), 0);
            return indices;
        };
    return controller;
}

DecisionProbe harvest_ru_disintegrate_hold_probe() {
    constexpr std::size_t kRootPlayer = 0;
    constexpr std::size_t kOpponent = 1;
    PriorityCallbackHarvest harvest;

    GameConfig config;
    config.max_turns = 70;
    config.starting_player = kRootPlayer;
    config.human_controllers[kRootPlayer] =
        land_then_pass_controller(kRootPlayer, harvest);
    config.human_controllers[kOpponent] =
        land_then_pass_controller(kOpponent, harvest);

    const std::array<std::vector<CardId>, 2> decks = {
        ru_aggro_deck(),
        green_deck(),
    };
    Game game(decks[0], decks[1], kProbeValidationV1GameSeed,
              config);
    harvest.game = &game;
    try {
        static_cast<void>(game.run());
    } catch (const PriorityCallbackHarvestComplete&) {
        // The callback snapshots the exact pre-action state and legal list.
    }
    if (!harvest.state.has_value()) {
        throw std::logic_error(
            "fixed validation trajectory did not reach the RU X=0 state");
    }

    DecisionProbe probe;
    probe.stable_id =
        "validation.ru.disintegrate-hold-x0.v1";
    probe.category = Category::RUDisintegrateHoldValidation;
    probe.decision_kind = DecisionKind::Priority;
    probe.root_deck = DeckId::RUAggro;
    probe.opponent_deck = DeckId::Green;
    probe.root_player = kRootPlayer;
    probe.phase = harvest.phase;
    probe.consecutive_passes = 0;
    probe.state = std::move(*harvest.state);
    probe.original_decks = decks;
    probe.candidates.reserve(harvest.actions.size());
    for (const PriorityAction& action : harvest.actions) {
        probe.candidates.push_back(priority_candidate(
            harvested_priority_descriptor(action), action));
    }
    probe.harvest = HarvestProvenance{
        .collector =
            std::string(kProbePriorityCallbackCollector),
        .trajectory_script =
            std::string(kProbeLandThenPassScript),
        .game_seed = kProbeValidationV1GameSeed,
        .starting_player = kRootPlayer,
        .priority_decision_ordinal =
            harvest.captured_priority_decision_ordinal,
        .turn_number = probe.state.turn_number,
        .phase = probe.phase,
    };
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

    if (probe.decision_kind == DecisionKind::Block) {
        std::vector<BinaryBlockDecision> declared;
        for (const Candidate& candidate : probe.candidates) {
            const auto* action =
                std::get_if<BinaryBlockDecision>(&candidate.action);
            if (action == nullptr) {
                errors.push_back(
                    "block probe contains a non-block candidate");
                valid = false;
                continue;
            }
            if (std::find(declared.begin(), declared.end(), *action) !=
                declared.end()) {
                errors.push_back(
                    "block probe contains a duplicate candidate action");
                valid = false;
            }
            declared.push_back(*action);

            GameState settled = probe.state;
            if (!settle_binary_block_decision(
                    settled, probe.state.active_player, *action)) {
                errors.push_back(
                    "block probe contains an illegal combat branch");
                valid = false;
            }
        }
        if (declared.size() != 2 ||
            declared[0].attacker != declared[1].attacker ||
            declared[0].blocker != declared[1].blocker ||
            declared[0].include == declared[1].include) {
            errors.push_back(
                "block probe must offer no-block/block for one "
                "attacker-blocker pair");
            valid = false;
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
    // Time Walk means turn-number parity no longer determines the active
    // player. The full state records active player and queued future turns.
    if (probe.state.turn_number == 0) {
        errors.push_back("probe turn number must be positive");
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
    } else if (probe.decision_kind == DecisionKind::Block) {
        if (probe.phase != TurnPhase::DeclareBlockers ||
            probe.state.active_player == probe.root_player ||
            probe.consecutive_passes != 0 ||
            !probe.state.stack.empty()) {
            errors.push_back(
                "block probe is outside a reachable declaration window");
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
    } else if (probe.decision_kind == DecisionKind::Block) {
        if (probe.candidates.empty()) {
            errors.push_back("block probe has no blocking subject");
            return false;
        }
        const auto* first = std::get_if<BinaryBlockDecision>(
            &probe.candidates.front().action);
        if (first == nullptr) {
            errors.push_back("block probe has no blocking subject");
            return false;
        }
        const CreaturePermanent* attacker = find_creature(
            probe.state, probe.state.active_player, first->attacker);
        const CreaturePermanent* blocker = find_creature(
            probe.state, probe.root_player, first->blocker);
        if (attacker == nullptr || !attacker->tapped ||
            blocker == nullptr || blocker->tapped) {
            errors.push_back(
                "binary block subject is outside a legal combat state");
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

bool settle_binary_block_decision(
    GameState& state, std::size_t attacking_player,
    const BinaryBlockDecision& decision) {
    if (attacking_player >= state.players.size()) {
        return false;
    }
    GameState settled = state;
    auto& attackers = settled.players[attacking_player].creatures;
    const auto attacker = std::find_if(
        attackers.begin(), attackers.end(),
        [&](const CreaturePermanent& creature) {
            return creature.id == decision.attacker;
        });
    if (attacker == attackers.end() || !attacker->tapped) {
        return false;
    }
    // resolve_combat() owns both declaration and settlement and therefore
    // expects a ready attacker. This corpus captures the next decision point,
    // where declaration has already tapped it.
    attacker->tapped = false;
    const std::vector<std::pair<PermanentId, PermanentId>> blocks =
        decision.include
            ? std::vector<std::pair<PermanentId, PermanentId>>{
                  {decision.attacker, decision.blocker}}
            : std::vector<std::pair<PermanentId, PermanentId>>{};
    if (!resolve_combat(
            settled, attacking_player, {decision.attacker}, blocks)) {
        return false;
    }
    state = std::move(settled);
    return true;
}

bool Validation::ok() const {
    return exact_card_conservation &&
           candidates_legal_and_complete && reachable_state &&
           hidden_clone_invariant && errors.empty();
}

std::vector<DecisionProbe> make_probe_dev_v1() {
    std::vector<DecisionProbe> probes;
    probes.reserve(kLegacyCategoryCount);
    probes.push_back(green_develop_probe());
    probes.push_back(green_tsunami_probe());
    probes.push_back(green_favorable_attack_probe());
    probes.push_back(green_unfavorable_attack_probe());
    probes.push_back(red_face_lethal_probe());
    probes.push_back(red_clear_blocker_probe());
    probes.push_back(red_finish_damaged_probe());
    probes.push_back(red_stack_race_probe());
    probes.push_back(blue_counter_expensive_probe());
    probes.push_back(blue_force_spike_probe());
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
            // Waiting gives the visible Air Elemental a lethal attack
            // before Red receives another main phase.
            probe.state.players[0].life = 4;
            break;
        case Category::BlueCounterLethal:
            // Countering now exposes a concrete winning continuation rather
            // than a branch whose correct defense is eventually scored as
            // the same terminal loss at long horizons.
            probe.state.players[1].life = 4;
            expose_creature(
                0, CardId::AirElemental,
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

std::vector<DecisionProbe> make_probe_dev_v3() {
    const std::vector<DecisionProbe> legacy = make_probe_dev_v2();
    const auto v3_legacy_probe =
        [&legacy](Category category) {
            const auto found = std::find_if(
                legacy.begin(), legacy.end(),
                [category](const DecisionProbe& probe) {
                    return probe.category == category;
                });
            if (found == legacy.end()) {
                throw std::logic_error(
                    "probe-dev-v3 legacy source fixture is missing");
            }
            DecisionProbe probe = *found;
            const std::size_t suffix = probe.stable_id.rfind(".v2");
            if (suffix == std::string::npos ||
                suffix + 3 != probe.stable_id.size()) {
                throw std::logic_error(
                    "probe-dev-v3 source has an invalid stable ID");
            }
            probe.stable_id.replace(suffix, 3, ".v3");
            return probe;
        };

    std::vector<DecisionProbe> probes;
    probes.reserve(kProbeDevV3Count);
    probes.push_back(v3_legacy_probe(Category::GreenDevelop));
    probes.push_back(green_growth_save_bolt_probe());
    probes.push_back(green_growth_push_probe());
    probes.push_back(green_growth_hold_probe());

    for (const Category category :
         {Category::RedFaceLethal, Category::RedClearBlocker,
          Category::RedFinishDamagedThreat, Category::RedStackRace}) {
        DecisionProbe probe = v3_legacy_probe(category);
        if (category == Category::RedClearBlocker) {
            // Begin Combat is the final modeled priority window before the
            // Bear can block the Fire Elemental. Green must be tapped out:
            // otherwise a hidden Giant Growth can reopen priority and let
            // Red heal the Pass branch by Bolting later in this window.
            probe.phase = TurnPhase::BeginCombat;
            for (LandPermanent& land :
                 probe.state.players[1].lands) {
                land.tapped = true;
            }
        } else if (category ==
                   Category::RedFinishDamagedThreat) {
            // The opponent has passed in its final main phase. Passing again
            // ends the turn and cleanup removes the Air Elemental's marked
            // damage, so the Bolt decision cannot heal later.
            probe.phase = TurnPhase::SecondMain;
            probe.state.active_player = 1;
            probe.state.turn_number = 10;
            probe.consecutive_passes = 1;
        }
        probes.push_back(std::move(probe));
    }

    for (const Category category :
         {Category::BlueCounterExpensiveSpell,
          Category::BlueForceSpike,
          Category::BlueCounterLethal, Category::BlueCounterWar,
          Category::WhiteEmergencyMoat,
          Category::WhiteEstablishMillstone,
          Category::WhiteMillBeforeDraw,
          Category::WhiteAvoidRedundantMoat}) {
        DecisionProbe probe = v3_legacy_probe(category);
        if (category == Category::WhiteEmergencyMoat) {
            // Passing now ends White's turn; it cannot heal the branch by
            // casting the same Moat after its otherwise-empty combat.
            probe.phase = TurnPhase::SecondMain;
        } else if (
            category == Category::WhiteAvoidRedundantMoat) {
            // A hidden Giant Growth must not reopen priority after White
            // passes and permit the delayed Millstone/Moat cast.
            for (LandPermanent& land :
                 probe.state.players[1].lands) {
                land.tapped = true;
            }
        }
        probes.push_back(std::move(probe));
    }

    probes.push_back(ru_land_color_probe());
    probes.push_back(ru_blocker_development_probe());
    probes.push_back(ru_flying_moat_attack_probe());
    probes.push_back(ru_disintegrate_lethal_probe());
    return probes;
}

std::vector<DecisionProbe> make_probe_validation_v1() {
    return {harvest_ru_disintegrate_hold_probe()};
}

std::vector<DecisionProbe> make_force_spike_policy_controls_v1() {
    DecisionProbe live = blue_force_spike_probe();
    live.stable_id =
        "control.blue.force-spike-live-gray-ogre.v1";

    DecisionProbe payable = live;
    payable.stable_id =
        "control.blue.force-spike-payable-gray-ogre.v1";
    // The live state has exactly the three tapped Mountains used to cast
    // Gray Ogre. The natural payable control exposes one additional,
    // untapped Mountain, moving that physical card out of Red's hidden
    // library so conservation and declared-deck reachability remain exact.
    auto& red_library = payable.state.players[1].library;
    const auto mountain =
        std::find(red_library.begin(), red_library.end(),
                  CardId::Mountain);
    if (mountain == red_library.end()) {
        throw std::logic_error(
            "Force Spike payable control has no hidden Mountain");
    }
    red_library.erase(mountain);
    payable.state.players[1].lands.push_back(
        land(CardId::Mountain));
    return {std::move(live), std::move(payable)};
}

std::vector<DecisionProbe> make_field_regressions_v1() {
    std::vector<DecisionProbe> probes;
    probes.reserve(6);
    probes.push_back(field_ru_flying_men_chump_air_probe(
        20, "field.ru.life20-flying-men-chump-air.v1",
        Category::FieldRULife20FlyingMenChumpAir));
    probes.push_back(field_ru_flying_men_chump_air_probe(
        4, "field.ru.life4-flying-men-chump-air.v1",
        Category::FieldRULife4FlyingMenChumpAir));
    probes.push_back(
        field_green_second_main_sick_bear_growth_probe());
    probes.push_back(
        field_green_begin_combat_growth_tapped_air_probe());
    probes.push_back(field_green_attack_after_growth_air_probe(
        true,
        "field.green.attack-after-growth-opponent-air.v1",
        Category::FieldGreenAttackAfterGrowthTappedAir));
    probes.push_back(field_green_attack_after_growth_air_probe(
        false,
        "field.green.attack-after-growth-opponent-air-untapped-"
        "control.v1",
        Category::FieldGreenAttackAfterGrowthUntappedAirControl));
    return probes;
}

std::vector<DecisionProbe> make_attack_regression_v1() {
    return {diagnostic_ru_attack_flying_into_air_probe()};
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

std::vector<std::string> validate_probe_dev_v3(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed) {
    std::vector<std::string> errors;
    if (probes.size() != kProbeDevV3Count) {
        errors.push_back(
            "probe-dev-v3 must contain exactly 20 probes");
    }

    std::unordered_set<std::string> stable_ids;
    std::unordered_set<std::size_t> categories;
    std::array<std::size_t, kDeckCount> root_deck_counts{};
    for (const DecisionProbe& probe : probes) {
        if (probe.stable_id.empty() ||
            !stable_ids.insert(probe.stable_id).second) {
            errors.push_back(
                "probe stable IDs must be nonempty and unique");
        }
        if (!probe.stable_id.ends_with(".v3")) {
            errors.push_back(
                "probe-dev-v3 stable IDs must end in .v3");
        }

        const bool category_is_active =
            std::find(kProbeDevV3Categories.begin(),
                      kProbeDevV3Categories.end(),
                      probe.category) !=
            kProbeDevV3Categories.end();
        const std::size_t category =
            static_cast<std::size_t>(probe.category);
        if (!category_is_active ||
            !categories.insert(category).second) {
            errors.push_back(
                "probe-dev-v3 categories must be active and unique");
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

    for (const Category category : kProbeDevV3Categories) {
        if (!categories.contains(
                static_cast<std::size_t>(category))) {
            errors.push_back("probe-dev-v3 omits a category");
        }
    }
    for (const std::size_t count : root_deck_counts) {
        if (count != 4) {
            errors.push_back(
                "probe-dev-v3 requires four probes per root deck");
        }
    }
    return errors;
}

std::vector<std::string> validate_probe_validation_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed) {
    std::vector<std::string> errors;
    if (probes.size() != 1) {
        errors.push_back(
            "probe-validation-v1 must contain exactly one probe");
    }

    for (const DecisionProbe& probe : probes) {
        if (probe.stable_id !=
            "validation.ru.disintegrate-hold-x0.v1") {
            errors.push_back(
                "probe-validation-v1 has an unknown stable ID");
        }
        if (probe.category !=
                Category::RUDisintegrateHoldValidation ||
            probe.decision_kind != DecisionKind::Priority ||
            probe.root_deck != DeckId::RUAggro ||
            probe.opponent_deck != DeckId::Green ||
            probe.root_player != 0 ||
            probe.phase != TurnPhase::SecondMain ||
            probe.consecutive_passes != 0 ||
            probe.state.active_player != probe.root_player ||
            !probe.state.players[probe.root_player]
                 .land_played_this_turn) {
            errors.push_back(
                "probe-validation-v1 lost its harvested decision context");
        }

        if (!probe.harvest.has_value()) {
            errors.push_back(
                "probe-validation-v1 is missing harvest provenance");
        } else {
            const HarvestProvenance& harvest = *probe.harvest;
            if (harvest.collector !=
                    kProbePriorityCallbackCollector ||
                harvest.trajectory_script !=
                    kProbeLandThenPassScript ||
                harvest.game_seed !=
                    kProbeValidationV1GameSeed ||
                harvest.starting_player !=
                    probe.state.starting_player ||
                harvest.turn_number != probe.state.turn_number ||
                harvest.phase != probe.phase) {
                errors.push_back(
                    "probe-validation-v1 harvest provenance disagrees "
                    "with the state");
            }
        }

        const PriorityAction x_zero =
            PriorityAction::cast_disintegrate(
                0, Target::player_target(1));
        bool has_pass = false;
        bool has_x_zero = false;
        bool has_affordable_lethal_x = false;
        bool has_land_play = false;
        for (const Candidate& candidate : probe.candidates) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            if (action == nullptr) {
                continue;
            }
            has_pass =
                has_pass ||
                action->kind == PriorityActionKind::Pass;
            has_land_play =
                has_land_play ||
                action->kind == PriorityActionKind::PlayLand;
            has_x_zero = has_x_zero || *action == x_zero;
            if (action->kind ==
                    PriorityActionKind::CastDisintegrate &&
                action->target.has_value() &&
                !action->target->creature.has_value() &&
                action->target->player == 1 &&
                action->x_value >= probe.state.players[1].life) {
                has_affordable_lethal_x = true;
            }
        }
        if (!has_pass || !has_x_zero || has_land_play ||
            has_affordable_lethal_x) {
            errors.push_back(
                "probe-validation-v1 is not a nonlethal "
                "Pass-versus-X=0 decision");
        }

        const Validation validation =
            validate_probe(probe, hidden_seed);
        for (const std::string& error : validation.errors) {
            errors.push_back(probe.stable_id + ": " + error);
        }
    }
    return errors;
}

std::vector<std::string> validate_force_spike_policy_controls_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed) {
    constexpr std::array<std::string_view, 2> kExpectedIds = {
        "control.blue.force-spike-live-gray-ogre.v1",
        "control.blue.force-spike-payable-gray-ogre.v1",
    };
    std::vector<std::string> errors;
    if (probes.size() != kExpectedIds.size()) {
        errors.push_back(
            "Force Spike controls must contain exactly two probes");
    }

    const std::size_t checked =
        std::min(probes.size(), kExpectedIds.size());
    for (std::size_t index = 0; index < checked; ++index) {
        const DecisionProbe& probe = probes[index];
        if (probe.stable_id != kExpectedIds[index]) {
            errors.push_back(
                "Force Spike controls have an unknown or reordered "
                "stable ID");
        }
        const bool shared_context =
            probe.category == Category::BlueForceSpike &&
            probe.decision_kind == DecisionKind::Priority &&
            probe.root_deck == DeckId::Blue &&
            probe.opponent_deck == DeckId::Red &&
            probe.root_player == 0 &&
            probe.phase == TurnPhase::FirstMain &&
            probe.consecutive_passes == 1 &&
            probe.state.stack.size() == 1 &&
            probe.state.stack.back().kind ==
                StackObjectKind::Spell &&
            probe.state.stack.back().card == CardId::GrayOgre &&
            probe.state.stack.back().controller == 1 &&
            probe.state.players[0].hand ==
                std::vector<CardId>{CardId::ForceSpike};
        if (!shared_context) {
            errors.push_back(
                probe.stable_id +
                ": Force Spike control lost its shared live-spell "
                "decision context");
        }

        bool has_pass = false;
        bool has_force_spike = false;
        if (!probe.state.stack.empty()) {
            const PriorityAction force_spike =
                PriorityAction::cast_force_spike(
                    probe.state.stack.back().id);
            for (const Candidate& candidate : probe.candidates) {
                const auto* action =
                    std::get_if<PriorityAction>(&candidate.action);
                if (action == nullptr) {
                    continue;
                }
                has_pass =
                    has_pass ||
                    (candidate.descriptor == "pass" &&
                     *action == PriorityAction::pass());
                has_force_spike =
                    has_force_spike ||
                    (candidate.descriptor ==
                         "force-spike-gray-ogre" &&
                     *action == force_spike);
            }
        }
        if (probe.candidates.size() != 2 || !has_pass ||
            !has_force_spike) {
            errors.push_back(
                probe.stable_id +
                ": Force Spike control must expose exactly Pass and "
                "Force Spike");
        }

        const PlayerState& opponent = probe.state.players[1];
        const std::size_t expected_lands = index == 0 ? 3 : 4;
        const std::size_t expected_untapped = index;
        const std::size_t untapped_mountains =
            static_cast<std::size_t>(std::count_if(
                opponent.lands.begin(), opponent.lands.end(),
                [](const LandPermanent& permanent) {
                    return permanent.card == CardId::Mountain &&
                           !permanent.tapped;
                }));
        if (opponent.mana_pool != ManaCost{} ||
            opponent.lands.size() != expected_lands ||
            !std::all_of(
                opponent.lands.begin(), opponent.lands.end(),
                [](const LandPermanent& permanent) {
                    return permanent.card == CardId::Mountain;
                }) ||
            untapped_mountains != expected_untapped) {
            errors.push_back(
                probe.stable_id +
                ": opponent public mana sources do not match the "
                "control");
        }

        const Validation validation =
            validate_probe(probe, hidden_seed + index);
        for (const std::string& error : validation.errors) {
            errors.push_back(probe.stable_id + ": " + error);
        }
    }

    if (probes.size() >= 2) {
        GameState live = probes[0].state;
        GameState payable = probes[1].state;
        auto& payable_lands = payable.players[1].lands;
        const auto extra_mountain = std::find_if(
            payable_lands.begin(), payable_lands.end(),
            [](const LandPermanent& permanent) {
                return permanent.card == CardId::Mountain &&
                       !permanent.tapped;
            });
        if (extra_mountain != payable_lands.end()) {
            payable_lands.erase(extra_mountain);
            payable.players[1].library.push_back(CardId::Mountain);
        }
        for (std::size_t player = 0; player < kPlayerCount; ++player) {
            std::sort(live.players[player].library.begin(),
                      live.players[player].library.end());
            std::sort(payable.players[player].library.begin(),
                      payable.players[player].library.end());
        }
        if (live != payable ||
            probes[0].original_decks !=
                probes[1].original_decks ||
            probes[0].candidates.size() !=
                probes[1].candidates.size()) {
            errors.push_back(
                "Force Spike controls differ by more than the "
                "additional public Mountain");
        } else {
            for (std::size_t candidate = 0;
                 candidate < probes[0].candidates.size();
                 ++candidate) {
                if (probes[0].candidates[candidate].descriptor !=
                        probes[1].candidates[candidate].descriptor ||
                    probes[0].candidates[candidate].action !=
                        probes[1].candidates[candidate].action) {
                    errors.push_back(
                        "Force Spike controls changed candidate "
                        "identity or order");
                    break;
                }
            }
        }
    }
    return errors;
}

std::vector<std::string> validate_field_regressions_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed) {
    constexpr std::array<std::string_view, 6> kExpectedIds = {
        "field.ru.life20-flying-men-chump-air.v1",
        "field.ru.life4-flying-men-chump-air.v1",
        "field.green.second-main-sick-bear-growth.v1",
        "field.green.begin-combat-growth-tapped-air.v1",
        "field.green.attack-after-growth-opponent-air.v1",
        "field.green.attack-after-growth-opponent-air-untapped-"
        "control.v1",
    };
    constexpr std::array<Category, 6> kExpectedCategories = {
        Category::FieldRULife20FlyingMenChumpAir,
        Category::FieldRULife4FlyingMenChumpAir,
        Category::FieldGreenSecondMainSickBearGrowth,
        Category::FieldGreenBeginCombatGrowthTappedAir,
        Category::FieldGreenAttackAfterGrowthTappedAir,
        Category::FieldGreenAttackAfterGrowthUntappedAirControl,
    };
    constexpr std::array<DecisionKind, 6> kExpectedKinds = {
        DecisionKind::Block,
        DecisionKind::Block,
        DecisionKind::Priority,
        DecisionKind::Priority,
        DecisionKind::Attack,
        DecisionKind::Attack,
    };

    std::vector<std::string> errors;
    if (probes.size() != kExpectedIds.size()) {
        errors.push_back(
            "field-regressions-v1 must contain exactly six probes");
    }
    std::unordered_set<std::string> stable_ids;
    const std::size_t checked =
        std::min(probes.size(), kExpectedIds.size());
    for (std::size_t index = 0; index < checked; ++index) {
        const DecisionProbe& probe = probes[index];
        if (probe.stable_id != kExpectedIds[index] ||
            !stable_ids.insert(probe.stable_id).second) {
            errors.push_back(
                "field-regressions-v1 has an unknown, reordered, or "
                "duplicate stable ID");
        }
        if (probe.category != kExpectedCategories[index] ||
            probe.decision_kind != kExpectedKinds[index] ||
            probe.harvest.has_value()) {
            errors.push_back(
                probe.stable_id +
                ": field decision context or authored provenance "
                "changed");
        }
        const Validation validation =
            validate_probe(probe, hidden_seed + index);
        for (const std::string& error : validation.errors) {
            errors.push_back(probe.stable_id + ": " + error);
        }
    }

    if (probes.size() >= 2) {
        GameState life20 = probes[0].state;
        life20.players[probes[0].root_player].life = 4;
        if (life20 != probes[1].state ||
            probes[0].original_decks != probes[1].original_decks ||
            probes[0].candidates != probes[1].candidates) {
            errors.push_back(
                "RU chump-block controls differ by more than defender "
                "life");
        }
    }

    if (probes.size() >= 3) {
        const DecisionProbe& sick_bear = probes[2];
        GameState predecessor = sick_bear.state;
        PlayerState& root =
            predecessor.players[sick_bear.root_player];
        const auto bear = std::find_if(
            root.creatures.begin(), root.creatures.end(),
            [](const CreaturePermanent& creature) {
                return creature.card == CardId::GrizzlyBears;
            });
        if (bear == root.creatures.end() ||
            root.lands.size() < 2) {
            errors.push_back(
                "sick-Bear Growth probe has no exact cast "
                "predecessor");
        } else {
            const PermanentId bear_id = bear->id;
            root.hand.push_back(CardId::GrizzlyBears);
            root.creatures.erase(bear);
            root.lands[0].tapped = false;
            root.lands[1].tapped = false;
            predecessor.next_permanent_id = bear_id;

            const std::size_t frozen_spells_cast =
                sick_bear.state.stats[sick_bear.root_player]
                    .spells_cast;
            const StackObjectId frozen_next_stack_object_id =
                sick_bear.state.next_stack_object_id;
            if (!apply_priority_action(
                    predecessor, sick_bear.root_player,
                    PriorityAction::cast_creature(
                        CardId::GrizzlyBears),
                    true) ||
                !resolve_top_of_stack(predecessor)) {
                errors.push_back(
                    "sick-Bear Growth probe could not replay its "
                    "Bear cast predecessor");
            } else {
                const bool history_counters_exact =
                    predecessor
                            .stats[sick_bear.root_player]
                            .spells_cast ==
                        frozen_spells_cast + 1 &&
                    predecessor.next_stack_object_id ==
                        frozen_next_stack_object_id + 1;
                predecessor
                    .stats[sick_bear.root_player]
                    .spells_cast = frozen_spells_cast;
                predecessor.next_stack_object_id =
                    frozen_next_stack_object_id;
                if (!history_counters_exact ||
                    predecessor != sick_bear.state) {
                    errors.push_back(
                        "sick-Bear Growth probe is not the exact "
                        "rules successor of casting Grizzly Bears");
                }
            }
        }
    }

    if (probes.size() >= 5) {
        GameState expected = probes[3].state;
        const PriorityAction growth =
            PriorityAction::cast_giant_growth(
                Target::creature_target(1, 2));
        if (!apply_priority_action(
                expected, probes[3].root_player, growth, false) ||
            !resolve_top_of_stack(expected) ||
            expected != probes[4].state) {
            errors.push_back(
                "Growth-on-tapped-Air attack probe is not the exact "
                "resolved successor of its linked priority probe");
        }
    }

    if (probes.size() >= 6) {
        GameState tapped = probes[4].state;
        CreaturePermanent* air = nullptr;
        for (CreaturePermanent& creature :
             tapped.players[1].creatures) {
            if (creature.id == 2) {
                air = &creature;
                break;
            }
        }
        if (air == nullptr) {
            errors.push_back(
                "linked attack probe is missing Air Elemental");
        } else {
            air->tapped = false;
            if (tapped != probes[5].state ||
                probes[4].original_decks !=
                    probes[5].original_decks ||
                probes[4].candidates != probes[5].candidates) {
                errors.push_back(
                    "untapped-Air control differs by more than Air "
                    "Elemental's tapped status");
            }
        }
    }
    return errors;
}

std::vector<std::string> validate_attack_regression_v1(
    const std::vector<DecisionProbe>& probes,
    std::uint64_t hidden_seed) {
    std::vector<std::string> errors;
    if (probes.size() != 1) {
        errors.push_back(
            "attack-regression-v1 must contain exactly one probe");
        return errors;
    }

    const DecisionProbe& probe = probes.front();
    if (probe.stable_id !=
            "diagnostic.ru.life20-flying-men-attack-air.v1" ||
        probe.category !=
            Category::
                DiagnosticRUAttackFlyingIntoLargerFlyingBlocker ||
        probe.decision_kind != DecisionKind::Attack ||
        probe.root_deck != DeckId::RUAggro ||
        probe.opponent_deck != DeckId::Blue ||
        probe.harvest.has_value()) {
        errors.push_back(
            "attack-regression-v1 identity or authored context "
            "changed");
    }

    const Validation validation =
        validate_probe(probe, hidden_seed);
    for (const std::string& error : validation.errors) {
        errors.push_back(probe.stable_id + ": " + error);
    }

    const std::size_t opponent = 1 - probe.root_player;
    if (probe.root_player >= kPlayerCount ||
        probe.state.players[probe.root_player].life != 20 ||
        probe.state.players[opponent].life != 20 ||
        probe.state.players[probe.root_player].creatures.size() !=
            1 ||
        probe.state.players[opponent].creatures.size() != 1) {
        errors.push_back(
            "attack-regression-v1 must retain the healthy-life "
            "one-attacker/one-blocker public board");
        return errors;
    }

    const CreaturePermanent& attacker =
        probe.state.players[probe.root_player].creatures.front();
    const CreaturePermanent& blocker =
        probe.state.players[opponent].creatures.front();
    const CardDefinition& attacker_definition =
        card_definition(attacker.card);
    const CardDefinition& blocker_definition =
        card_definition(blocker.card);
    if (attacker_definition.power != 1 ||
        attacker_definition.toughness != 1 ||
        !attacker_definition.flying || attacker.tapped ||
        attacker.summoning_sick ||
        blocker_definition.power != 4 ||
        blocker_definition.toughness != 4 ||
        !blocker_definition.flying || blocker.tapped) {
        errors.push_back(
            "attack-regression-v1 public creatures are not an "
            "eligible 1/1 flyer and untapped 4/4 flying blocker");
    }

    if (probe.candidates.size() != 2) {
        errors.push_back(
            "attack-regression-v1 must enumerate both legal "
            "attacker sets");
        return errors;
    }
    const auto* no_attack =
        std::get_if<BinaryAttackDecision>(
            &probe.candidates[0].action);
    const auto* attack =
        std::get_if<BinaryAttackDecision>(
            &probe.candidates[1].action);
    if (probe.candidates[0].descriptor != "no-attack" ||
        probe.candidates[1].descriptor !=
            "attack-with-only-legal-attacker" ||
        no_attack == nullptr || attack == nullptr ||
        no_attack->attacker != attacker.id ||
        attack->attacker != attacker.id ||
        no_attack->include || !attack->include) {
        errors.push_back(
            "attack-regression-v1 does not enumerate No Attack "
            "then the only nonempty legal attacker set");
    }

    GameState passed = probe.state;
    if (!resolve_combat(
            passed, probe.root_player, {}, {}) ||
        passed.players[probe.root_player].creatures.size() != 1 ||
        passed.players[opponent].creatures.size() != 1) {
        errors.push_back(
            "attack-regression-v1 No Attack branch is not a legal "
            "engine combat declaration");
    }
    GameState blocked = probe.state;
    if (!resolve_combat(
            blocked, probe.root_player, {attacker.id},
            {{attacker.id, blocker.id}}) ||
        !blocked.players[probe.root_player].creatures.empty() ||
        blocked.players[opponent].creatures.size() != 1) {
        errors.push_back(
            "attack-regression-v1 engine block does not destroy "
            "only the 1/1 attacker");
    }
    return errors;
}

namespace {

std::uint64_t dc1_mix_seed(std::uint64_t seed,
                           std::string_view stable_id,
                           std::size_t world_index) {
    constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    std::uint64_t hash = kFnvOffset;
    const auto add = [&](std::uint8_t byte) {
        hash ^= byte;
        hash *= kFnvPrime;
    };
    for (const char character : stable_id) {
        add(static_cast<std::uint8_t>(
            static_cast<unsigned char>(character)));
    }
    for (std::size_t byte = 0; byte < sizeof(seed); ++byte) {
        add(static_cast<std::uint8_t>(
            seed >> (byte * 8U)));
    }
    const std::uint64_t index =
        static_cast<std::uint64_t>(world_index);
    for (std::size_t byte = 0; byte < sizeof(index); ++byte) {
        add(static_cast<std::uint8_t>(
            index >> (byte * 8U)));
    }
    return hash;
}

GameState dc1_hidden_repartition_clone(
    const DecisionProbe& probe) {
    if (probe.root_player >= kPlayerCount) {
        throw std::invalid_argument(
            "DC1 root player must be 0 or 1");
    }
    GameState clone = probe.state;
    std::reverse(
        clone.players[probe.root_player].library.begin(),
        clone.players[probe.root_player].library.end());

    const std::size_t opponent = 1 - probe.root_player;
    PlayerState& opponent_clone = clone.players[opponent];
    const std::size_t hand_size = opponent_clone.hand.size();
    std::vector<CardId> hidden = opponent_clone.hand;
    hidden.insert(hidden.end(), opponent_clone.library.begin(),
                  opponent_clone.library.end());
    if (hidden.size() > 1) {
        std::rotate(hidden.begin(), hidden.begin() + 1,
                    hidden.end());
        std::reverse(hidden.begin(), hidden.end());
    }
    const auto hand_end =
        hidden.begin() + static_cast<std::ptrdiff_t>(hand_size);
    opponent_clone.hand.assign(hidden.begin(), hand_end);
    opponent_clone.library.assign(hand_end, hidden.end());
    return clone;
}

void add_mana(ManaCost& destination, const ManaCost& value) {
    destination.generic += value.generic;
    destination.green += value.green;
    destination.red += value.red;
    destination.blue += value.blue;
    destination.white += value.white;
}

ManaCost dc1_available_mana(const PlayerState& player) {
    ManaCost available = player.mana_pool;
    for (const LandPermanent& land : player.lands) {
        if (land.tapped) {
            continue;
        }
        switch (land.card) {
        case CardId::Forest:
            ++available.green;
            break;
        case CardId::Mountain:
            ++available.red;
            break;
        case CardId::Island:
            ++available.blue;
            break;
        case CardId::Plains:
            ++available.white;
            break;
        default:
            break;
        }
    }
    for (const ArtifactPermanent& artifact : player.artifacts) {
        if (artifact.tapped) {
            continue;
        }
        if (artifact.card == CardId::MoxSapphire) {
            ++available.blue;
        } else if (artifact.card == CardId::SolRing) {
            available.generic += 2;
        }
    }
    return available;
}

ManaCost positive_mana_difference(const ManaCost& before,
                                  const ManaCost& after) {
    return {
        .generic = std::max(0, before.generic - after.generic),
        .green = std::max(0, before.green - after.green),
        .red = std::max(0, before.red - after.red),
        .blue = std::max(0, before.blue - after.blue),
        .white = std::max(0, before.white - after.white),
    };
}

std::array<std::size_t, kCardCount> dc1_hand_counts(
    const PlayerState& player) {
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : player.hand) {
        ++counts[card_index(card)];
    }
    return counts;
}

void add_newly_tapped_sources(
    const GameState& root, const GameState& before,
    const GameState& after, std::size_t player,
    std::vector<Dc1ManaSource>& destination) {
    const auto add_unique = [&](Dc1ManaSource source) {
        if (std::find(destination.begin(), destination.end(),
                      source) == destination.end()) {
            destination.push_back(source);
        }
    };

    const auto& root_player = root.players[player];
    const auto& before_player = before.players[player];
    const auto& after_player = after.players[player];
    const std::size_t land_count =
        std::min({root_player.lands.size(),
                  before_player.lands.size(),
                  after_player.lands.size()});
    for (std::size_t index = 0; index < land_count; ++index) {
        if (root_player.lands[index].card ==
                before_player.lands[index].card &&
            root_player.lands[index].card ==
                after_player.lands[index].card &&
            !root_player.lands[index].tapped &&
            !before_player.lands[index].tapped &&
            after_player.lands[index].tapped) {
            add_unique({
                .kind = Dc1ManaSourceKind::Land,
                .key = static_cast<std::uint64_t>(index),
                .card = root_player.lands[index].card,
            });
        }
    }

    for (const ArtifactPermanent& root_artifact :
         root_player.artifacts) {
        if (root_artifact.tapped) {
            continue;
        }
        const auto before_artifact = std::find_if(
            before_player.artifacts.begin(),
            before_player.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == root_artifact.id &&
                       candidate.card == root_artifact.card;
            });
        const auto after_artifact = std::find_if(
            after_player.artifacts.begin(),
            after_player.artifacts.end(),
            [&](const ArtifactPermanent& candidate) {
                return candidate.id == root_artifact.id &&
                       candidate.card == root_artifact.card;
            });
        if (before_artifact != before_player.artifacts.end() &&
            after_artifact != after_player.artifacts.end() &&
            !before_artifact->tapped && after_artifact->tapped) {
            add_unique({
                .kind = Dc1ManaSourceKind::Artifact,
                .key = root_artifact.id,
                .card = root_artifact.card,
            });
        }
    }
}

void accumulate_dc1_operation(
    const GameState& root, const GameState& before,
    const GameState& after,
    std::array<Dc1PlayerResourceCost, 2>& resources,
    bool include_hand_and_land_costs) {
    for (std::size_t player = 0; player < kPlayerCount; ++player) {
        const ManaCost before_mana =
            dc1_available_mana(before.players[player]);
        const ManaCost after_mana =
            dc1_available_mana(after.players[player]);
        add_mana(
            resources[player].mana_depleted,
            positive_mana_difference(before_mana, after_mana));
        add_newly_tapped_sources(
            root, before, after, player,
            resources[player].preexisting_sources_newly_tapped);

        if (!include_hand_and_land_costs) {
            continue;
        }
        const auto before_hand =
            dc1_hand_counts(before.players[player]);
        const auto after_hand =
            dc1_hand_counts(after.players[player]);
        for (std::size_t card = 0; card < kCardCount; ++card) {
            if (before_hand[card] > after_hand[card]) {
                resources[player].hand_cards_consumed[card] +=
                    before_hand[card] - after_hand[card];
            }
        }
        if (!before.players[player].land_played_this_turn &&
            after.players[player].land_played_this_turn) {
            resources[player].land_play_entitlement_consumed = true;
        }
    }
}

bool dc1_terminal(const GameState& state) {
    return state.players[0].life <= 0 ||
           state.players[1].life <= 0 ||
           state.failed_draw[0] || state.failed_draw[1];
}

void remove_normalized_graveyard_cost(
    std::vector<CardId>& graveyard,
    const std::vector<CardId>& root_graveyard, CardId card,
    std::size_t maximum) {
    const std::size_t root_count =
        static_cast<std::size_t>(std::count(
            root_graveyard.begin(), root_graveyard.end(), card));
    std::size_t current_count =
        static_cast<std::size_t>(std::count(
            graveyard.begin(), graveyard.end(), card));
    std::size_t removable =
        std::min(maximum, current_count > root_count
                              ? current_count - root_count
                              : std::size_t{0});
    while (removable > 0) {
        const auto found =
            std::find(graveyard.rbegin(), graveyard.rend(), card);
        if (found == graveyard.rend()) {
            break;
        }
        graveyard.erase(std::next(found).base());
        --removable;
        --current_count;
    }
}

void restore_dc1_source(
    PlayerState& player, const PlayerState& root,
    const Dc1ManaSource& source) {
    if (source.kind == Dc1ManaSourceKind::Land) {
        const std::size_t index =
            static_cast<std::size_t>(source.key);
        if (index < player.lands.size() &&
            index < root.lands.size() &&
            player.lands[index].card == source.card &&
            root.lands[index].card == source.card) {
            player.lands[index].tapped = root.lands[index].tapped;
        }
        return;
    }

    const auto root_artifact = std::find_if(
        root.artifacts.begin(), root.artifacts.end(),
        [&](const ArtifactPermanent& candidate) {
            return candidate.id == source.key &&
                   candidate.card == source.card;
        });
    const auto artifact = std::find_if(
        player.artifacts.begin(), player.artifacts.end(),
        [&](const ArtifactPermanent& candidate) {
            return candidate.id == source.key &&
                   candidate.card == source.card;
        });
    if (root_artifact != root.artifacts.end() &&
        artifact != player.artifacts.end()) {
        artifact->tapped = root_artifact->tapped;
    }
}

GameState normalized_dc1_effect(
    const GameState& root,
    const Dc1CanonicalSettlement& settlement) {
    GameState normalized = settlement.settled_state;
    for (std::size_t player = 0; player < kPlayerCount; ++player) {
        auto& state = normalized.players[player];
        const auto& root_state = root.players[player];
        const auto& resources = settlement.resources[player];
        for (std::size_t card = 0; card < kCardCount; ++card) {
            const CardId card_id = static_cast<CardId>(card);
            const std::size_t consumed =
                resources.hand_cards_consumed[card];
            state.hand.insert(state.hand.end(), consumed, card_id);
            remove_normalized_graveyard_cost(
                state.graveyard, root_state.graveyard, card_id,
                consumed);
        }
        for (const Dc1ManaSource& source :
             resources.preexisting_sources_newly_tapped) {
            restore_dc1_source(state, root_state, source);
        }
        state.mana_pool = root_state.mana_pool;
        state.land_played_this_turn =
            root_state.land_played_this_turn;
        std::sort(state.hand.begin(), state.hand.end());
        std::sort(state.graveyard.begin(), state.graveyard.end());
        std::sort(state.exile.begin(), state.exile.end());
        std::sort(state.enchantments.begin(),
                  state.enchantments.end());
        std::sort(
            state.lands.begin(), state.lands.end(),
            [](const LandPermanent& left,
               const LandPermanent& right) {
                return std::tie(left.card, left.tapped) <
                       std::tie(right.card, right.tapped);
            });
        std::sort(
            state.creatures.begin(), state.creatures.end(),
            [](const CreaturePermanent& left,
               const CreaturePermanent& right) {
                return left.id < right.id;
            });
        std::sort(
            state.artifacts.begin(), state.artifacts.end(),
            [](const ArtifactPermanent& left,
               const ArtifactPermanent& right) {
                return left.id < right.id;
            });
    }
    normalized.stats = {};
    normalized.next_permanent_id = 0;
    normalized.next_stack_object_id = 0;
    return normalized;
}

bool mana_cost_leq(const ManaCost& left, const ManaCost& right) {
    return left.generic <= right.generic &&
           left.green <= right.green &&
           left.red <= right.red &&
           left.blue <= right.blue &&
           left.white <= right.white;
}

bool source_cost_leq(std::vector<Dc1ManaSource> left,
                     std::vector<Dc1ManaSource> right) {
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    return std::includes(
        right.begin(), right.end(), left.begin(), left.end());
}

bool player_cost_leq(const Dc1PlayerResourceCost& left,
                     const Dc1PlayerResourceCost& right) {
    for (std::size_t card = 0; card < kCardCount; ++card) {
        if (left.hand_cards_consumed[card] >
            right.hand_cards_consumed[card]) {
            return false;
        }
    }
    return mana_cost_leq(left.mana_depleted,
                         right.mana_depleted) &&
           source_cost_leq(
               left.preexisting_sources_newly_tapped,
               right.preexisting_sources_newly_tapped) &&
           (!left.land_play_entitlement_consumed ||
            right.land_play_entitlement_consumed);
}

Dc1Dominance compare_dc1_settlements(
    const DecisionProbe& probe,
    const Dc1CanonicalSettlement& first,
    const Dc1CanonicalSettlement& second) {
    if (!dc1_settlements_have_equal_normalized_effect(
            probe, first, second)) {
        return Dc1Dominance::Incomparable;
    }

    const std::size_t actor = probe.root_player;
    const std::size_t opponent = 1 - actor;
    const auto dominates =
        [&](const Dc1CanonicalSettlement& better,
            const Dc1CanonicalSettlement& worse) {
            const bool actor_no_more =
                player_cost_leq(better.resources[actor],
                                worse.resources[actor]);
            const bool opponent_no_fewer =
                player_cost_leq(worse.resources[opponent],
                                better.resources[opponent]);
            const bool strict =
                better.resources[actor] !=
                    worse.resources[actor] ||
                better.resources[opponent] !=
                    worse.resources[opponent];
            return actor_no_more && opponent_no_fewer && strict;
        };
    const bool first_dominates = dominates(first, second);
    const bool second_dominates = dominates(second, first);
    if (first_dominates == second_dominates) {
        return Dc1Dominance::Incomparable;
    }
    return first_dominates
               ? Dc1Dominance::FirstDominatesSecond
               : Dc1Dominance::SecondDominatesFirst;
}

std::string dc1_pair_key(
    const DecisionProbe& probe, std::size_t first,
    std::size_t second);

} // namespace

Dc1CanonicalSettlement settle_dc1_priority_candidate(
    const DecisionProbe& probe,
    const GameState& information_set_world,
    std::size_t candidate_index) {
    if (probe.decision_kind != DecisionKind::Priority ||
        probe.root_player >= kPlayerCount ||
        probe.consecutive_passes < 0 ||
        probe.consecutive_passes > 1 ||
        candidate_index >= probe.candidates.size()) {
        throw std::invalid_argument(
            "invalid DC1 Priority settlement root");
    }
    const auto* action = std::get_if<PriorityAction>(
        &probe.candidates[candidate_index].action);
    if (action == nullptr) {
        throw std::invalid_argument(
            "DC1 candidate is not a Priority action");
    }

    Dc1CanonicalSettlement settlement;
    settlement.settled_state = information_set_world;
    settlement.phase = probe.phase;
    PriorityState priority = {
        .player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
    };

    const auto take_pass = [&]() {
        const GameState before = settlement.settled_state;
        const PriorityPassResult result =
            pass_priority(settlement.settled_state, priority);
        if (result == PriorityPassResult::StackObjectResolved) {
            accumulate_dc1_operation(
                information_set_world, before,
                settlement.settled_state, settlement.resources,
                false);
            settlement.terminal =
                dc1_terminal(settlement.settled_state);
        } else if (result == PriorityPassResult::WindowEnded) {
            settlement.window_ended = true;
        }
        return result;
    };

    if (action->kind == PriorityActionKind::Pass) {
        static_cast<void>(take_pass());
    } else {
        const GameState before = settlement.settled_state;
        if (!apply_priority_action(
                settlement.settled_state, probe.root_player,
                *action, sorcery_actions_for(probe.phase))) {
            throw std::invalid_argument(
                "DC1 candidate is illegal in sampled world");
        }
        accumulate_dc1_operation(
            information_set_world, before,
            settlement.settled_state, settlement.resources, true);
        priority = {
            .player = probe.root_player,
            .consecutive_passes = 0,
        };
    }

    constexpr std::size_t kMaximumPassSteps = 1024;
    std::size_t steps = 0;
    while (!settlement.terminal && !settlement.window_ended) {
        if (++steps > kMaximumPassSteps) {
            throw std::logic_error(
                "DC1 canonical settlement exceeded pass bound");
        }
        static_cast<void>(take_pass());
    }
    for (auto& player : settlement.resources) {
        std::sort(
            player.preexisting_sources_newly_tapped.begin(),
            player.preexisting_sources_newly_tapped.end());
    }
    settlement.final_priority_player = priority.player;
    settlement.final_consecutive_passes =
        priority.consecutive_passes;
    return settlement;
}

bool dc1_settlements_have_equal_normalized_effect(
    const DecisionProbe& probe,
    const Dc1CanonicalSettlement& first,
    const Dc1CanonicalSettlement& second) {
    return first.phase == second.phase &&
           first.final_priority_player ==
               second.final_priority_player &&
           first.final_consecutive_passes ==
               second.final_consecutive_passes &&
           first.terminal == second.terminal &&
           first.window_ended == second.window_ended &&
           normalized_dc1_effect(probe.state, first) ==
               normalized_dc1_effect(probe.state, second);
}

Dc1PairComparison compare_dc1_priority_pair(
    const DecisionProbe& probe,
    std::size_t first_candidate_index,
    std::size_t second_candidate_index, std::size_t worlds,
    std::uint64_t seed) {
    if (worlds == 0 ||
        first_candidate_index == second_candidate_index ||
        first_candidate_index >= probe.candidates.size() ||
        second_candidate_index >= probe.candidates.size()) {
        throw std::invalid_argument(
            "invalid DC1 candidate pair");
    }
    const GameState hidden_clone =
        dc1_hidden_repartition_clone(probe);
    Dc1PairComparison comparison{
        .first_descriptor =
            probe.candidates[first_candidate_index].descriptor,
        .second_descriptor =
            probe.candidates[second_candidate_index].descriptor,
        .world_seeds = {},
        .world_orientations = {},
        .unanimous_orientation = Dc1Dominance::Incomparable,
        .hidden_repartition_bit_identical = true,
    };
    comparison.world_seeds.reserve(worlds);
    comparison.world_orientations.reserve(worlds);
    const std::string pair_key =
        dc1_pair_key(
            probe, first_candidate_index,
            second_candidate_index);
    std::optional<Dc1Dominance> unanimous;
    for (std::size_t world_index = 0; world_index < worlds;
         ++world_index) {
        const std::uint64_t world_seed =
            dc1_mix_seed(seed, pair_key, world_index);
        comparison.world_seeds.push_back(world_seed);
        const GameState world = sample_determinization(
            probe.state, probe.original_decks, probe.root_player,
            world_seed);
        const GameState clone_world = sample_determinization(
            hidden_clone, probe.original_decks, probe.root_player,
            world_seed);
        const Dc1CanonicalSettlement first =
            settle_dc1_priority_candidate(
                probe, world, first_candidate_index);
        const Dc1CanonicalSettlement second =
            settle_dc1_priority_candidate(
                probe, world, second_candidate_index);
        const Dc1CanonicalSettlement clone_first =
            settle_dc1_priority_candidate(
                probe, clone_world, first_candidate_index);
        const Dc1CanonicalSettlement clone_second =
            settle_dc1_priority_candidate(
                probe, clone_world, second_candidate_index);
        const Dc1Dominance orientation =
            compare_dc1_settlements(probe, first, second);
        const Dc1Dominance clone_orientation =
            compare_dc1_settlements(
                probe, clone_first, clone_second);
        comparison.world_orientations.push_back(orientation);
        comparison.hidden_repartition_bit_identical =
            comparison.hidden_repartition_bit_identical &&
            first == clone_first && second == clone_second &&
            orientation == clone_orientation;
        if (!unanimous.has_value()) {
            unanimous = orientation;
        } else if (*unanimous != orientation) {
            unanimous = Dc1Dominance::Incomparable;
        }
    }
    if (comparison.hidden_repartition_bit_identical &&
        unanimous.has_value() &&
        *unanimous != Dc1Dominance::Incomparable &&
        std::all_of(
            comparison.world_orientations.begin(),
            comparison.world_orientations.end(),
            [&](Dc1Dominance orientation) {
                return orientation == *unanimous;
            })) {
        comparison.unanimous_orientation = *unanimous;
    }
    return comparison;
}

namespace {

void dc1_append_u64(std::string& key, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        key.push_back(static_cast<char>(
            static_cast<std::uint8_t>(
                value >> (byte * 8U))));
    }
}

void dc1_append_bool(std::string& key, bool value) {
    key.push_back(value ? '\1' : '\0');
}

void dc1_append_cards(std::string& key,
                      std::vector<CardId> cards,
                      bool order_matters) {
    if (!order_matters) {
        std::sort(cards.begin(), cards.end());
    }
    dc1_append_u64(key, cards.size());
    for (const CardId card : cards) {
        dc1_append_u64(
            key, static_cast<std::uint64_t>(card));
    }
}

void dc1_append_mana(std::string& key, const ManaCost& mana) {
    dc1_append_u64(
        key, static_cast<std::uint64_t>(mana.generic));
    dc1_append_u64(
        key, static_cast<std::uint64_t>(mana.green));
    dc1_append_u64(
        key, static_cast<std::uint64_t>(mana.red));
    dc1_append_u64(
        key, static_cast<std::uint64_t>(mana.blue));
    dc1_append_u64(
        key, static_cast<std::uint64_t>(mana.white));
}

void dc1_append_target(std::string& key,
                       const std::optional<Target>& target) {
    dc1_append_bool(key, target.has_value());
    if (!target.has_value()) {
        return;
    }
    dc1_append_u64(key, target->player);
    dc1_append_bool(key, target->creature.has_value());
    if (target->creature.has_value()) {
        dc1_append_u64(key, *target->creature);
    }
}

std::string dc1_information_set_key(
    const DecisionProbe& probe) {
    const PlayerObservation observation =
        observe_game_state(probe.state, probe.root_player);
    std::string key;
    key.reserve(1024);
    dc1_append_u64(key, probe.root_player);
    dc1_append_u64(
        key, static_cast<std::uint64_t>(probe.root_deck));
    dc1_append_u64(
        key, static_cast<std::uint64_t>(probe.opponent_deck));
    dc1_append_u64(
        key, static_cast<std::uint64_t>(probe.phase));
    dc1_append_u64(
        key, static_cast<std::uint64_t>(
                 probe.consecutive_passes));
    dc1_append_u64(key, observation.active_player);
    dc1_append_u64(key, observation.starting_player);
    dc1_append_u64(key, observation.turn_number);
    for (const std::size_t turns :
         observation.extra_turns_pending) {
        dc1_append_u64(key, turns);
    }
    for (const PublicPlayerState& player :
         observation.players) {
        dc1_append_u64(
            key, static_cast<std::uint64_t>(player.life));
        dc1_append_u64(key, player.library_size);
        dc1_append_u64(key, player.hand_size);
        dc1_append_cards(key, player.graveyard, false);
        dc1_append_cards(key, player.exile, false);

        std::vector<LandPermanent> lands = player.lands;
        std::sort(
            lands.begin(), lands.end(),
            [](const LandPermanent& left,
               const LandPermanent& right) {
                return std::tie(left.card, left.tapped) <
                       std::tie(right.card, right.tapped);
            });
        dc1_append_u64(key, lands.size());
        for (const LandPermanent& land : lands) {
            dc1_append_u64(
                key, static_cast<std::uint64_t>(land.card));
            dc1_append_bool(key, land.tapped);
        }

        std::vector<CreaturePermanent> creatures =
            player.creatures;
        std::sort(
            creatures.begin(), creatures.end(),
            [](const CreaturePermanent& left,
               const CreaturePermanent& right) {
                return left.id < right.id;
            });
        dc1_append_u64(key, creatures.size());
        for (const CreaturePermanent& creature : creatures) {
            dc1_append_u64(key, creature.id);
            dc1_append_u64(
                key, static_cast<std::uint64_t>(creature.card));
            dc1_append_bool(key, creature.tapped);
            dc1_append_bool(key, creature.summoning_sick);
            dc1_append_u64(
                key, static_cast<std::uint64_t>(creature.damage));
            dc1_append_u64(
                key, static_cast<std::uint64_t>(
                         creature.temporary_power_bonus));
            dc1_append_u64(
                key, static_cast<std::uint64_t>(
                         creature.temporary_toughness_bonus));
            dc1_append_bool(
                key, creature.exile_on_death_this_turn);
        }

        std::vector<ArtifactPermanent> artifacts =
            player.artifacts;
        std::sort(
            artifacts.begin(), artifacts.end(),
            [](const ArtifactPermanent& left,
               const ArtifactPermanent& right) {
                return left.id < right.id;
            });
        dc1_append_u64(key, artifacts.size());
        for (const ArtifactPermanent& artifact : artifacts) {
            dc1_append_u64(key, artifact.id);
            dc1_append_u64(
                key, static_cast<std::uint64_t>(artifact.card));
            dc1_append_bool(key, artifact.tapped);
        }
        dc1_append_cards(key, player.enchantments, false);
        dc1_append_mana(key, player.mana_pool);
        dc1_append_bool(key, player.land_played_this_turn);
    }
    dc1_append_cards(key, observation.hand, false);
    dc1_append_u64(key, observation.stack.size());
    for (const StackObject& object : observation.stack) {
        dc1_append_u64(
            key, static_cast<std::uint64_t>(object.kind));
        dc1_append_u64(key, object.id);
        dc1_append_u64(
            key, static_cast<std::uint64_t>(object.card));
        dc1_append_u64(key, object.controller);
        dc1_append_target(key, object.target);
        dc1_append_bool(key, object.spell_target.has_value());
        if (object.spell_target.has_value()) {
            dc1_append_u64(key, *object.spell_target);
        }
        dc1_append_u64(
            key, static_cast<std::uint64_t>(object.x_value));
    }
    return key;
}

std::uint64_t dc1_stable_hash(std::string_view value) {
    constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    std::uint64_t hash = kFnvOffset;
    for (const char character : value) {
        hash ^= static_cast<std::uint8_t>(
            static_cast<unsigned char>(character));
        hash *= kFnvPrime;
    }
    return hash;
}

Dc1LegalActionSetSummary dc1_summarize_action_set(
    const std::vector<PriorityAction>& actions,
    std::size_t diagnostic_ceiling) {
    if (actions.empty() || diagnostic_ceiling == 0) {
        throw std::invalid_argument(
            "DC1 action summary requires a nonempty action set and "
            "positive ceiling");
    }
    if (actions.size() > diagnostic_ceiling) {
        throw std::logic_error(
            "DC1 legal-action set exceeds diagnostic ceiling");
    }

    Dc1LegalActionSetSummary summary;
    summary.legal_actions = actions.size();
    std::vector<std::string> descriptors;
    descriptors.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        const std::size_t kind =
            static_cast<std::size_t>(action.kind);
        if (kind >= summary.action_kinds.size()) {
            throw std::logic_error(
                "DC1 action set contains an invalid action kind");
        }
        ++summary.action_kinds[kind];
        descriptors.push_back(
            harvested_priority_descriptor(action));
    }
    std::sort(descriptors.begin(), descriptors.end());
    summary.descriptors_distinct =
        std::adjacent_find(
            descriptors.begin(), descriptors.end()) ==
        descriptors.end();
    std::string digest_input;
    for (const std::string& descriptor : descriptors) {
        dc1_append_u64(digest_input, descriptor.size());
        digest_input += descriptor;
    }
    summary.sorted_descriptor_fnv1a64 =
        dc1_stable_hash(digest_input);
    return summary;
}

std::string dc1_pair_key(
    const DecisionProbe& probe, std::size_t first,
    std::size_t second) {
    std::string first_descriptor =
        probe.candidates[first].descriptor;
    std::string second_descriptor =
        probe.candidates[second].descriptor;
    if (second_descriptor < first_descriptor) {
        std::swap(first_descriptor, second_descriptor);
    }
    std::string key = dc1_information_set_key(probe);
    dc1_append_u64(key, first_descriptor.size());
    key += first_descriptor;
    dc1_append_u64(key, second_descriptor.size());
    key += second_descriptor;
    return key;
}

using Dc1PairExample = Dc1PairLabelObservation;

struct Dc1PairCandidate {
    std::uint64_t hash = 0;
    std::string first_descriptor;
    std::string second_descriptor;
    std::size_t first = 0;
    std::size_t second = 0;
};

std::vector<Dc1PairCandidate> dc1_retained_pairs(
    const DecisionProbe& probe, std::size_t maximum) {
    std::vector<Dc1PairCandidate> pairs;
    for (std::size_t first = 0;
         first < probe.candidates.size(); ++first) {
        for (std::size_t second = first + 1;
             second < probe.candidates.size(); ++second) {
            std::string first_descriptor =
                probe.candidates[first].descriptor;
            std::string second_descriptor =
                probe.candidates[second].descriptor;
            if (second_descriptor < first_descriptor) {
                std::swap(first_descriptor, second_descriptor);
            }
            const std::string key =
                dc1_pair_key(probe, first, second);
            pairs.push_back({
                .hash = dc1_stable_hash(key),
                .first_descriptor =
                    std::move(first_descriptor),
                .second_descriptor =
                    std::move(second_descriptor),
                .first = first,
                .second = second,
            });
        }
    }
    std::sort(
        pairs.begin(), pairs.end(),
        [](const Dc1PairCandidate& left,
           const Dc1PairCandidate& right) {
            return std::tie(
                       left.hash, left.first_descriptor,
                       left.second_descriptor) <
                   std::tie(
                       right.hash, right.first_descriptor,
                       right.second_descriptor);
        });
    if (pairs.size() > maximum) {
        pairs.resize(maximum);
    }
    return pairs;
}

const DecisionProbe& dc1_find_probe(
    const std::vector<DecisionProbe>& probes, Category category) {
    const auto found = std::find_if(
        probes.begin(), probes.end(),
        [category](const DecisionProbe& probe) {
            return probe.category == category;
        });
    if (found == probes.end()) {
        throw std::logic_error(
            "DC1 fixture corpus is missing a category");
    }
    return *found;
}

std::size_t dc1_candidate_index(
    const DecisionProbe& probe, std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [descriptor](const Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::logic_error(
            "DC1 fixture is missing a candidate");
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

bool dc1_fixture_gate() {
    const auto validation = make_probe_validation_v1();
    const auto controls = make_force_spike_policy_controls_v1();
    const auto dev = make_probe_dev_v3();
    if (validation.size() != 1 || controls.size() != 2) {
        return false;
    }
    const DecisionProbe& x_zero_probe = validation.front();
    const std::size_t pass =
        dc1_candidate_index(
            x_zero_probe,
            harvested_priority_descriptor(
                PriorityAction::pass()));
    const auto x_zero = std::find_if(
        x_zero_probe.candidates.begin(),
        x_zero_probe.candidates.end(),
        [](const Candidate& candidate) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate.action);
            return action != nullptr &&
                   action->kind ==
                       PriorityActionKind::CastDisintegrate &&
                   action->x_value == 0;
        });
    if (x_zero == x_zero_probe.candidates.end()) {
        return false;
    }
    const std::size_t x_zero_index =
        static_cast<std::size_t>(
            std::distance(
                x_zero_probe.candidates.begin(), x_zero));
    const auto x_zero_comparison =
        compare_dc1_priority_pair(
            x_zero_probe, pass, x_zero_index, kDc1Worlds,
            kDc1TrainingMiningSeed);
    if (x_zero_comparison.unanimous_orientation !=
            Dc1Dominance::FirstDominatesSecond ||
        !x_zero_comparison.hidden_repartition_bit_identical) {
        return false;
    }

    const auto expect_incomparable =
        [](const DecisionProbe& probe,
           std::string_view first_descriptor,
           std::string_view second_descriptor) {
            const auto comparison =
                compare_dc1_priority_pair(
                    probe,
                    dc1_candidate_index(
                        probe, first_descriptor),
                    dc1_candidate_index(
                        probe, second_descriptor),
                    kDc1Worlds, kDc1TrainingMiningSeed);
            return comparison.unanimous_orientation ==
                       Dc1Dominance::Incomparable &&
                   comparison.hidden_repartition_bit_identical;
        };
    return
        expect_incomparable(
            controls[0], "pass",
            "force-spike-gray-ogre") &&
        expect_incomparable(
            controls[1], "pass",
            "force-spike-gray-ogre") &&
        expect_incomparable(
            dc1_find_probe(dev, Category::GreenGrowthSaveBolt),
            "pass", "growth-own-grizzly-bears") &&
        expect_incomparable(
            dc1_find_probe(dev, Category::GreenDevelop),
            "pass", "cast-grizzly-bears") &&
        expect_incomparable(
            dc1_find_probe(dev, Category::RULandColor),
            "pass", "play-mountain") &&
        expect_incomparable(
            dc1_find_probe(
                dev, Category::WhiteEstablishMillstone),
            "pass", "cast-millstone") &&
        expect_incomparable(
            dc1_find_probe(
                dev, Category::RUDisintegrateLethal),
            "pass", "disintegrate-x3-opponent-player");
}

void dc1_finalize_deck_examples(
    std::vector<Dc1PairExample> examples,
    std::size_t minimum_examples,
    std::size_t minimum_seat_games,
    Dc1DeckMiningSummary& summary) {
    auto deduped =
        dedupe_dc1_pair_labels(std::move(examples));
    summary.conflicting_pair_keys =
        deduped.conflicting_pair_keys;
    std::vector<Dc1PairExample> positives;
    std::vector<Dc1PairExample> controls;
    for (Dc1PairExample& example : deduped.retained) {
        (example.positive ? positives : controls)
            .push_back(std::move(example));
    }
    summary.unique_positive_pairs = positives.size();
    summary.unique_incomparable_pairs = controls.size();

    const std::size_t matched =
        std::min(positives.size(), controls.size());
    controls.resize(matched);

    std::set<std::string> positive_seat_games;
    for (const auto& example : positives) {
        positive_seat_games.insert(example.seat_game_key);
    }
    std::set<std::string> control_seat_games;
    for (const auto& example : controls) {
        control_seat_games.insert(example.seat_game_key);
    }
    summary.unique_matched_incomparable_controls =
        controls.size();
    summary.positive_seat_games = positive_seat_games.size();
    summary.incomparable_control_seat_games =
        control_seat_games.size();
    summary.density_passed =
        positives.size() >= minimum_examples &&
        controls.size() >= minimum_examples &&
        positive_seat_games.size() >= minimum_seat_games &&
        control_seat_games.size() >= minimum_seat_games;
}

Dc1MiningSplitReport dc1_mine_split(
    std::shared_ptr<const LearnedModel> model,
    const Dc1MiningConfig& config, std::uint64_t split_seed,
    bool training_split) {
    Dc1MiningSplitReport report;
    report.seed = split_seed;
    report.hidden_repartition_passed = true;
    std::array<std::vector<Dc1PairExample>, kDeckCount>
        examples_by_deck;

    for (std::size_t block = 0;
         block < config.blocks_per_split; ++block) {
        const auto schedule =
            learned_iteration::balanced_schedule(
                split_seed, 1, block);
        for (const auto& scheduled : schedule) {
            GameConfig game_config;
            game_config.max_turns = config.max_game_turns;
            game_config.starting_player =
                scheduled.starting_player;
            game_config.learned_model = model;
            game_config.bots = {
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action = config.worlds,
                    .exploration_rate =
                        training_split
                            ? config.training_exploration_rate
                            : 0.0,
                    .learned_model = model,
                },
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action = config.worlds,
                    .exploration_rate =
                        training_split
                            ? config.training_exploration_rate
                            : 0.0,
                    .learned_model = model,
                },
            };
            const std::array<std::vector<CardId>, 2> decks = {
                deck_for(scheduled.seat_decks[0]),
                deck_for(scheduled.seat_decks[1]),
            };
            Game game(
                decks[0], decks[1], scheduled.seed,
                game_config);
            std::vector<LearnedDecisionTracePoint> trace;
            static_cast<void>(
                game.run_with_priority_root_trace(trace));
            ++report.games;
            report.seat_games += 2;
            report.raw_priority_roots += trace.size();

            for (std::size_t player = 0; player < kPlayerCount;
                 ++player) {
                const DeckId actor_deck =
                    scheduled.seat_decks[player];
                const std::size_t deck =
                    static_cast<std::size_t>(actor_deck);
                if (deck >= kDeckCount) {
                    throw std::logic_error(
                        "DC1 schedule contains invalid deck");
                }
                ++report.decks[deck].seat_games;
                std::vector<DecisionProbe> roots;
                for (std::size_t ordinal = 0;
                     ordinal < trace.size(); ++ordinal) {
                    const auto& point = trace[ordinal];
                    if (!point.context.valid ||
                        point.context.decision_player != player) {
                        continue;
                    }
                    ++report.decks[deck].raw_priority_roots;
                    const auto actions = legal_priority_actions(
                        point.state, player,
                        point.context.sorcery_actions);
                    if (actions.size() <= 1) {
                        continue;
                    }
                    ++report.raw_multi_action_roots;
                    ++report.decks[deck]
                          .raw_multi_action_roots;
                    if (actions.size() >
                        config.max_legal_actions) {
                        throw std::logic_error(
                            "DC1 root exceeds legal-action bound");
                    }
                    DecisionProbe probe;
                    probe.stable_id =
                        std::string(training_split
                                        ? "dc1.train"
                                        : "dc1.heldout") +
                        ".b" + std::to_string(block) +
                        ".g" +
                        std::to_string(
                            scheduled.schedule_index) +
                        ".p" + std::to_string(player) +
                        ".r" + std::to_string(ordinal);
                    probe.category = Category::GreenDevelop;
                    probe.decision_kind =
                        DecisionKind::Priority;
                    probe.root_deck = actor_deck;
                    probe.opponent_deck =
                        scheduled.seat_decks[1 - player];
                    probe.root_player = player;
                    probe.phase = point.context.phase;
                    probe.consecutive_passes =
                        point.context.consecutive_passes;
                    probe.state = point.state;
                    probe.original_decks = decks;
                    probe.candidates.reserve(actions.size());
                    for (const PriorityAction& action : actions) {
                        probe.candidates.push_back(
                            priority_candidate(
                                harvested_priority_descriptor(
                                    action),
                                action));
                    }
                    roots.push_back(std::move(probe));
                }

                const auto retained =
                    learned_iteration::
                        evenly_spaced_retained_indices(
                            roots.size(),
                            config.max_roots_per_seat_game);
                report.retained_roots += retained.size();
                report.decks[deck].retained_roots +=
                    retained.size();
                const std::string seat_game_key =
                    std::string(training_split
                                    ? "train"
                                    : "heldout") +
                    ".b" + std::to_string(block) +
                    ".g" +
                    std::to_string(scheduled.schedule_index) +
                    ".p" + std::to_string(player);
                for (const std::size_t retained_index :
                     retained) {
                    const DecisionProbe& probe =
                        roots.at(retained_index);
                    const auto pairs = dc1_retained_pairs(
                        probe, config.max_pairs_per_root);
                    for (std::size_t pair_index = 0;
                         pair_index < pairs.size();
                         ++pair_index) {
                        const Dc1PairCandidate& pair =
                            pairs[pair_index];
                        const auto comparison =
                            compare_dc1_priority_pair(
                                probe, pair.first, pair.second,
                                config.worlds, split_seed);
                        ++report.pair_groups;
                        report.paired_world_cells +=
                            config.worlds;
                        report.settlement_operations +=
                            config.worlds * 4;
                        ++report.decks[deck].pair_groups;
                        report.decks[deck]
                            .paired_world_cells += config.worlds;
                        report.decks[deck]
                            .settlement_operations +=
                            config.worlds * 4;
                        report.hidden_repartition_passed =
                            report.hidden_repartition_passed &&
                            comparison
                                .hidden_repartition_bit_identical;
                        examples_by_deck[deck].push_back({
                            .exact_pair_key =
                                dc1_pair_key(
                                    probe, pair.first,
                                    pair.second),
                            .seat_game_key = seat_game_key,
                            .positive =
                                comparison
                                    .unanimous_orientation !=
                                Dc1Dominance::Incomparable,
                        });
                    }
                }
            }
        }
    }

    report.density_passed = true;
    const std::size_t minimum_examples =
        training_split
            ? config.training_minimum_examples_per_deck
            : config.heldout_minimum_examples_per_deck;
    const std::size_t minimum_seat_games =
        training_split
            ? config
                  .training_minimum_seat_games_per_deck
            : config
                  .heldout_minimum_seat_games_per_deck;
    for (std::size_t deck = 0; deck < kDeckCount; ++deck) {
        dc1_finalize_deck_examples(
            std::move(examples_by_deck[deck]),
            minimum_examples, minimum_seat_games,
            report.decks[deck]);
        report.density_passed =
            report.density_passed &&
            report.decks[deck].density_passed;
    }
    report.density_passed =
        report.density_passed &&
        report.hidden_repartition_passed;

    const std::size_t expected_games =
        config.blocks_per_split *
        learned_iteration::kBalancedScheduleGames;
    const std::size_t expected_seat_games =
        expected_games * kPlayerCount;
    const std::size_t expected_seat_games_per_deck =
        expected_seat_games / kDeckCount;
    std::size_t deck_seat_games = 0;
    std::size_t deck_raw_priority_roots = 0;
    std::size_t deck_raw_multi_action_roots = 0;
    std::size_t deck_retained_roots = 0;
    std::size_t deck_pair_groups = 0;
    std::size_t deck_paired_world_cells = 0;
    std::size_t deck_settlement_operations = 0;
    bool deck_caps_passed =
        expected_seat_games % kDeckCount == 0;
    for (const Dc1DeckMiningSummary& deck : report.decks) {
        deck_seat_games += deck.seat_games;
        deck_raw_priority_roots += deck.raw_priority_roots;
        deck_raw_multi_action_roots +=
            deck.raw_multi_action_roots;
        deck_retained_roots += deck.retained_roots;
        deck_pair_groups += deck.pair_groups;
        deck_paired_world_cells +=
            deck.paired_world_cells;
        deck_settlement_operations +=
            deck.settlement_operations;
        deck_caps_passed =
            deck_caps_passed &&
            deck.seat_games ==
                expected_seat_games_per_deck &&
            deck.raw_multi_action_roots <=
                deck.raw_priority_roots &&
            deck.retained_roots <=
                deck.raw_multi_action_roots &&
            deck.retained_roots <=
                deck.seat_games *
                    config.max_roots_per_seat_game &&
            deck.pair_groups <=
                deck.retained_roots *
                    config.max_pairs_per_root &&
            deck.paired_world_cells ==
                deck.pair_groups * config.worlds &&
            deck.settlement_operations ==
                deck.paired_world_cells * 4;
    }
    report.accounting_passed =
        report.games == expected_games &&
        report.seat_games == expected_seat_games &&
        report.raw_multi_action_roots <=
            report.raw_priority_roots &&
        report.retained_roots <=
            report.raw_multi_action_roots &&
        report.retained_roots <=
            report.seat_games *
                config.max_roots_per_seat_game &&
        report.pair_groups <=
            report.retained_roots *
                config.max_pairs_per_root &&
        report.paired_world_cells ==
            report.pair_groups * config.worlds &&
        report.settlement_operations ==
            report.paired_world_cells * 4 &&
        deck_caps_passed &&
        deck_seat_games == report.seat_games &&
        deck_raw_priority_roots ==
            report.raw_priority_roots &&
        deck_raw_multi_action_roots ==
            report.raw_multi_action_roots &&
        deck_retained_roots == report.retained_roots &&
        deck_pair_groups == report.pair_groups &&
        deck_paired_world_cells ==
            report.paired_world_cells &&
        deck_settlement_operations ==
            report.settlement_operations;
    return report;
}

Dc1ActionCensusSplitReport dc1_census_split(
    std::shared_ptr<const LearnedModel> model,
    const Dc1ActionCensusConfig& config,
    std::uint64_t split_seed, bool training_split) {
    Dc1ActionCensusSplitReport report;
    report.seed = split_seed;
    report.descriptors_distinct = true;
    report.legal_action_histogram.assign(
        config.diagnostic_ceiling + 1, 0);
    for (auto& deck : report.decks) {
        deck.legal_action_histogram.assign(
            config.diagnostic_ceiling + 1, 0);
    }

    for (std::size_t block = 0;
         block < config.blocks_per_split; ++block) {
        const auto schedule =
            learned_iteration::balanced_schedule(
                split_seed, 1, block);
        for (const auto& scheduled : schedule) {
            GameConfig game_config;
            game_config.max_turns = config.max_game_turns;
            game_config.starting_player =
                scheduled.starting_player;
            game_config.learned_model = model;
            game_config.bots = {
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action = config.worlds,
                    .exploration_rate =
                        training_split
                            ? config.training_exploration_rate
                            : 0.0,
                    .learned_model = model,
                },
                BotConfig{
                    .kind = BotKind::Learned,
                    .learned_variant =
                        LearnedVariant::ValueSearchChampion,
                    .rollouts_per_action = config.worlds,
                    .exploration_rate =
                        training_split
                            ? config.training_exploration_rate
                            : 0.0,
                    .learned_model = model,
                },
            };
            const std::array<std::vector<CardId>, 2> decks = {
                deck_for(scheduled.seat_decks[0]),
                deck_for(scheduled.seat_decks[1]),
            };
            Game game(
                decks[0], decks[1], scheduled.seed,
                game_config);
            std::vector<LearnedDecisionTracePoint> trace;
            static_cast<void>(
                game.run_with_priority_root_trace(trace));
            ++report.games;
            report.seat_games += kPlayerCount;

            for (std::size_t player = 0;
                 player < kPlayerCount; ++player) {
                const DeckId root_deck =
                    scheduled.seat_decks[player];
                const DeckId opponent_deck =
                    scheduled.seat_decks[1 - player];
                const std::size_t deck_index =
                    static_cast<std::size_t>(root_deck);
                if (deck_index >= kDeckCount) {
                    throw std::logic_error(
                        "DC1 census schedule contains invalid deck");
                }
                auto& deck = report.decks[deck_index];
                ++deck.seat_games;
                for (std::size_t ordinal = 0;
                     ordinal < trace.size(); ++ordinal) {
                    const auto& point = trace[ordinal];
                    if (!point.context.valid ||
                        point.context.decision_player != player) {
                        continue;
                    }
                    const auto actions = legal_priority_actions(
                        point.state, player,
                        point.context.sorcery_actions);
                    const Dc1LegalActionSetSummary summary =
                        dc1_summarize_action_set(
                            actions,
                            config.diagnostic_ceiling);
                    ++report.priority_roots;
                    ++deck.priority_roots;
                    ++report.legal_action_histogram.at(
                        summary.legal_actions);
                    ++deck.legal_action_histogram.at(
                        summary.legal_actions);
                    report.maximum_legal_actions =
                        std::max(
                            report.maximum_legal_actions,
                            summary.legal_actions);
                    deck.maximum_legal_actions =
                        std::max(
                            deck.maximum_legal_actions,
                            summary.legal_actions);
                    report.descriptors_distinct =
                        report.descriptors_distinct &&
                        summary.descriptors_distinct;

                    if (summary.legal_actions >
                        config.threshold) {
                        ++report.over_threshold_roots;
                        ++deck.over_threshold_roots;
                        report.over_threshold_contexts.push_back({
                            .training_split = training_split,
                            .block = block,
                            .schedule_index =
                                scheduled.schedule_index,
                            .seat = player,
                            .root_deck = root_deck,
                            .opponent_deck = opponent_deck,
                            .trace_ordinal = ordinal,
                            .turn_number =
                                point.state.turn_number,
                            .phase = point.context.phase,
                            .consecutive_passes =
                                point.context.consecutive_passes,
                            .stack_size =
                                point.state.stack.size(),
                            .actions = summary,
                        });
                    }
                }
            }
        }
    }

    std::sort(
        report.over_threshold_contexts.begin(),
        report.over_threshold_contexts.end(),
        [](const Dc1ActionCensusContext& left,
           const Dc1ActionCensusContext& right) {
            return std::tie(
                       left.block, left.schedule_index,
                       left.seat, left.trace_ordinal) <
                   std::tie(
                       right.block, right.schedule_index,
                       right.seat, right.trace_ordinal);
        });

    const std::size_t expected_games =
        config.blocks_per_split *
        learned_iteration::kBalancedScheduleGames;
    const std::size_t expected_seat_games =
        expected_games * kPlayerCount;
    const std::size_t expected_seat_games_per_deck =
        expected_seat_games / kDeckCount;
    const auto histogram_total =
        [](const std::vector<std::size_t>& histogram) {
            return std::accumulate(
                histogram.begin(), histogram.end(),
                std::size_t{0});
        };
    const auto histogram_over_threshold =
        [&](const std::vector<std::size_t>& histogram) {
            if (config.threshold + 1 >= histogram.size()) {
                return std::size_t{0};
            }
            return std::accumulate(
                histogram.begin() +
                    static_cast<std::ptrdiff_t>(
                        config.threshold + 1),
                histogram.end(), std::size_t{0});
        };
    std::vector<std::size_t> deck_histogram_sum(
        config.diagnostic_ceiling + 1, 0);
    std::size_t deck_seat_games = 0;
    std::size_t deck_priority_roots = 0;
    std::size_t deck_over_threshold_roots = 0;
    bool deck_accounting =
        expected_seat_games % kDeckCount == 0;
    for (const auto& deck : report.decks) {
        deck_seat_games += deck.seat_games;
        deck_priority_roots += deck.priority_roots;
        deck_over_threshold_roots +=
            deck.over_threshold_roots;
        if (deck.legal_action_histogram.size() !=
            deck_histogram_sum.size()) {
            deck_accounting = false;
            continue;
        }
        for (std::size_t actions = 0;
             actions < deck_histogram_sum.size(); ++actions) {
            deck_histogram_sum[actions] +=
                deck.legal_action_histogram[actions];
        }
        const auto maximum = std::find_if(
            deck.legal_action_histogram.rbegin(),
            deck.legal_action_histogram.rend(),
            [](std::size_t count) { return count != 0; });
        const std::size_t histogram_maximum =
            maximum == deck.legal_action_histogram.rend()
                ? 0
                : deck.legal_action_histogram.size() - 1 -
                      static_cast<std::size_t>(
                          std::distance(
                              deck.legal_action_histogram.rbegin(),
                              maximum));
        deck_accounting =
            deck_accounting &&
            deck.seat_games ==
                expected_seat_games_per_deck &&
            histogram_total(
                deck.legal_action_histogram) ==
                deck.priority_roots &&
            histogram_over_threshold(
                deck.legal_action_histogram) ==
                deck.over_threshold_roots &&
            histogram_maximum ==
                deck.maximum_legal_actions;
    }
    const auto global_max = std::find_if(
        report.legal_action_histogram.rbegin(),
        report.legal_action_histogram.rend(),
        [](std::size_t count) { return count != 0; });
    const std::size_t global_histogram_maximum =
        global_max == report.legal_action_histogram.rend()
            ? 0
            : report.legal_action_histogram.size() - 1 -
                  static_cast<std::size_t>(
                      std::distance(
                          report.legal_action_histogram.rbegin(),
                          global_max));
    const bool contexts_exact =
        report.over_threshold_contexts.size() ==
            report.over_threshold_roots &&
        std::all_of(
            report.over_threshold_contexts.begin(),
            report.over_threshold_contexts.end(),
            [&](const Dc1ActionCensusContext& context) {
                return context.training_split ==
                           training_split &&
                       context.actions.legal_actions >
                           config.threshold &&
                       context.actions.legal_actions <=
                           config.diagnostic_ceiling &&
                       context.actions.descriptors_distinct;
            });
    report.accounting_passed =
        report.games == expected_games &&
        report.seat_games == expected_seat_games &&
        report.legal_action_histogram.size() ==
            config.diagnostic_ceiling + 1 &&
        histogram_total(report.legal_action_histogram) ==
            report.priority_roots &&
        histogram_over_threshold(
            report.legal_action_histogram) ==
            report.over_threshold_roots &&
        global_histogram_maximum ==
            report.maximum_legal_actions &&
        deck_accounting &&
        deck_seat_games == report.seat_games &&
        deck_priority_roots == report.priority_roots &&
        deck_over_threshold_roots ==
            report.over_threshold_roots &&
        deck_histogram_sum ==
            report.legal_action_histogram &&
        contexts_exact && report.descriptors_distinct;
    return report;
}

} // namespace

Dc1PairLabelDedupeResult dedupe_dc1_pair_labels(
    std::vector<Dc1PairLabelObservation> observations) {
    std::sort(
        observations.begin(), observations.end(),
        [](const Dc1PairLabelObservation& left,
           const Dc1PairLabelObservation& right) {
            return std::tie(
                       left.exact_pair_key,
                       left.seat_game_key, left.positive) <
                   std::tie(
                       right.exact_pair_key,
                       right.seat_game_key, right.positive);
        });

    Dc1PairLabelDedupeResult result;
    result.retained.reserve(observations.size());
    for (std::size_t begin = 0;
         begin < observations.size();) {
        std::size_t end = begin + 1;
        bool saw_positive = observations[begin].positive;
        bool saw_incomparable = !observations[begin].positive;
        while (end < observations.size() &&
               observations[end].exact_pair_key ==
                   observations[begin].exact_pair_key) {
            saw_positive =
                saw_positive || observations[end].positive;
            saw_incomparable =
                saw_incomparable ||
                !observations[end].positive;
            ++end;
        }
        if (saw_positive && saw_incomparable) {
            ++result.conflicting_pair_keys;
        } else {
            Dc1PairLabelObservation representative =
                observations[begin];
            representative.positive = saw_positive;
            result.retained.push_back(
                std::move(representative));
        }
        begin = end;
    }
    return result;
}

Dc1LegalActionSetSummary summarize_dc1_legal_actions(
    const GameState& state, std::size_t player,
    bool sorcery_actions, std::size_t diagnostic_ceiling) {
    if (player >= kPlayerCount) {
        throw std::invalid_argument(
            "DC1 action summary player must be 0 or 1");
    }
    return dc1_summarize_action_set(
        legal_priority_actions(
            state, player, sorcery_actions),
        diagnostic_ceiling);
}

Dc1DominanceAuditReport audit_dc1_dominance_mining(
    std::shared_ptr<const LearnedModel> frozen_parent,
    Dc1MiningConfig config) {
    if (!frozen_parent ||
        config.required_model_fingerprint.empty() ||
        config.training_seed == config.heldout_seed ||
        config.blocks_per_split == 0 || config.worlds == 0 ||
        config.max_roots_per_seat_game == 0 ||
        config.max_pairs_per_root == 0 ||
        config.max_legal_actions < 2 ||
        config.max_game_turns == 0 ||
        !std::isfinite(config.training_exploration_rate) ||
        config.training_exploration_rate < 0.0 ||
        config.training_exploration_rate > 1.0) {
        throw std::invalid_argument(
            "invalid DC1 mining configuration");
    }
    const std::string fingerprint =
        learned_model_fingerprint(frozen_parent);
    if (fingerprint != config.required_model_fingerprint) {
        throw std::invalid_argument(
            "DC1 frozen-parent fingerprint mismatch");
    }

    Dc1DominanceAuditReport report;
    report.model_fingerprint = fingerprint;
    report.config = config;
    report.fixture_gate_passed = dc1_fixture_gate();
    if (!report.fixture_gate_passed) {
        return report;
    }
    report.training = dc1_mine_split(
        frozen_parent, config, config.training_seed, true);
    report.heldout = dc1_mine_split(
        frozen_parent, config, config.heldout_seed, false);

    report.accounting_passed =
        report.training.accounting_passed &&
        report.heldout.accounting_passed;
    report.gate_passed =
        report.fixture_gate_passed &&
        report.accounting_passed &&
        report.training.density_passed &&
        report.heldout.density_passed;
    return report;
}

Dc1ActionCensusReport audit_dc1_action_census(
    std::shared_ptr<const LearnedModel> frozen_parent,
    Dc1ActionCensusConfig config) {
    if (!frozen_parent ||
        config.required_model_fingerprint.empty() ||
        config.training_seed == config.heldout_seed ||
        config.blocks_per_split == 0 ||
        config.worlds == 0 ||
        config.max_game_turns == 0 ||
        !std::isfinite(config.training_exploration_rate) ||
        config.training_exploration_rate < 0.0 ||
        config.training_exploration_rate > 1.0 ||
        config.threshold == 0 ||
        config.diagnostic_ceiling <= config.threshold) {
        throw std::invalid_argument(
            "invalid DC1 action-census configuration");
    }
    const std::string fingerprint =
        learned_model_fingerprint(frozen_parent);
    if (fingerprint != config.required_model_fingerprint) {
        throw std::invalid_argument(
            "DC1 action-census frozen-parent fingerprint mismatch");
    }

    Dc1ActionCensusReport report;
    report.model_fingerprint = fingerprint;
    report.config = config;
    report.training = dc1_census_split(
        frozen_parent, config, config.training_seed, true);
    report.heldout = dc1_census_split(
        frozen_parent, config, config.heldout_seed, false);
    report.accounting_passed =
        report.training.accounting_passed &&
        report.heldout.accounting_passed &&
        report.pair_comparisons == 0 &&
        report.density_examples == 0;
    report.reproduced_over_threshold_root =
        report.training.over_threshold_roots +
            report.heldout.over_threshold_roots >
        0;
    report.ceiling_passed =
        report.training.maximum_legal_actions <=
            config.diagnostic_ceiling &&
        report.heldout.maximum_legal_actions <=
            config.diagnostic_ceiling;
    report.gate_passed =
        report.accounting_passed &&
        report.reproduced_over_threshold_root &&
        report.ceiling_passed;
    return report;
}

std::vector<BsrSourceGame> bsr_source_schedule(
    std::uint64_t seed, std::size_t blocks) {
    if (blocks == 0) {
        throw std::invalid_argument(
            "BSR source schedule requires a positive block count");
    }
    std::vector<BsrSourceGame> schedule;
    schedule.reserve(blocks * kBsrSourceGamesPerBlock);
    for (std::size_t block = 0; block < blocks; ++block) {
        std::size_t schedule_index = 0;
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            for (std::size_t tracked_seat = 0;
                 tracked_seat < kPlayerCount; ++tracked_seat) {
                for (std::size_t play_draw = 0;
                     play_draw < 2; ++play_draw) {
                    const bool tracked_starts =
                        play_draw == 1;
                    schedule.push_back({
                        .block = block,
                        .schedule_index = schedule_index,
                        .opponent_deck =
                            static_cast<DeckId>(opponent),
                        .tracked_seat = tracked_seat,
                        .tracked_starts = tracked_starts,
                        .starting_player =
                            tracked_starts
                                ? tracked_seat
                                : 1 - tracked_seat,
                        .seed = learned_iteration::derive_seed(
                            seed,
                            learned_iteration::SeedDomain::
                                SelfPlayGame,
                            0, block, schedule_index),
                    });
                    ++schedule_index;
                }
            }
        }
        if (schedule_index != kBsrSourceGamesPerBlock) {
            throw std::logic_error(
                "BSR source block has the wrong size");
        }
    }
    return schedule;
}

BsrRootClassification classify_bsr_trace_root(
    const LearnedDecisionTracePoint& point,
    std::size_t tracked_player,
    std::size_t maximum_legal_actions) {
    if (tracked_player >= kPlayerCount ||
        maximum_legal_actions < 2) {
        throw std::invalid_argument(
            "invalid BSR root-classification configuration");
    }

    BsrRootClassification result;
    if (!point.context.valid ||
        point.context.decision_player >= kPlayerCount) {
        result.eligibility =
            BsrRootEligibility::InvalidContext;
        return result;
    }
    if (point.context.decision_player != tracked_player) {
        result.eligibility =
            BsrRootEligibility::WrongDecisionOwner;
        return result;
    }
    if (point.state.stack.empty()) {
        result.eligibility = BsrRootEligibility::EmptyStack;
        return result;
    }
    const std::size_t stack_controller =
        point.state.stack.back().controller;
    if (stack_controller >= kPlayerCount) {
        result.eligibility =
            BsrRootEligibility::InvalidContext;
        return result;
    }
    if (stack_controller == tracked_player) {
        result.eligibility =
            BsrRootEligibility::TrackedSpellOnTop;
        return result;
    }

    result.legal_actions = legal_priority_actions(
        point.state, tracked_player,
        point.context.sorcery_actions);
    result.descriptors.reserve(result.legal_actions.size());
    for (const PriorityAction& action : result.legal_actions) {
        result.descriptors.push_back(
            harvested_priority_descriptor(action));
    }
    if (result.legal_actions.size() < 2 ||
        result.legal_actions.size() > maximum_legal_actions) {
        result.eligibility =
            BsrRootEligibility::ActionCountOutsideBounds;
        return result;
    }
    if (!point.selected_priority_action.has_value()) {
        result.eligibility =
            BsrRootEligibility::MissingSelectedAction;
        return result;
    }

    for (std::size_t index = 0;
         index < result.legal_actions.size(); ++index) {
        if (result.legal_actions[index] ==
            *point.selected_priority_action) {
            ++result.selected_action_matches;
            result.selected_action_index = index;
        }
    }
    if (result.selected_action_matches == 0) {
        result.eligibility =
            BsrRootEligibility::SelectedActionNotLegal;
        result.selected_action_index.reset();
        return result;
    }
    if (result.selected_action_matches != 1) {
        result.eligibility =
            BsrRootEligibility::SelectedActionAmbiguous;
        result.selected_action_index.reset();
        return result;
    }
    std::vector<std::string> sorted_descriptors =
        result.descriptors;
    std::sort(
        sorted_descriptors.begin(), sorted_descriptors.end());
    if (std::adjacent_find(
            sorted_descriptors.begin(),
            sorted_descriptors.end()) !=
        sorted_descriptors.end()) {
        result.eligibility =
            BsrRootEligibility::DuplicateActionDescriptor;
        return result;
    }
    result.eligibility = BsrRootEligibility::Eligible;
    return result;
}

std::vector<std::size_t> select_bsr_retained_candidate_indices(
    const std::vector<BsrRetentionCandidate>& candidates,
    std::size_t roots_per_loss,
    std::size_t roots_per_opponent) {
    if (roots_per_loss == 0 || roots_per_opponent == 0 ||
        roots_per_opponent >
            std::numeric_limits<std::size_t>::max() /
                kDeckCount) {
        throw std::invalid_argument(
            "invalid BSR retention configuration");
    }

    std::array<
        std::map<std::string, std::vector<std::size_t>>,
        kDeckCount>
        candidates_by_loss;
    for (std::size_t index = 0;
         index < candidates.size(); ++index) {
        const BsrRetentionCandidate& candidate =
            candidates[index];
        const std::size_t opponent =
            static_cast<std::size_t>(
                candidate.opponent_deck);
        if (opponent >= kDeckCount ||
            candidate.source_loss_key.empty() ||
            candidate.provenance_key.empty() ||
            candidate.stable_selection_key.empty()) {
            throw std::invalid_argument(
                "invalid BSR retention candidate");
        }
        candidates_by_loss[opponent]
            [candidate.source_loss_key]
                .push_back(index);
    }

    std::vector<std::size_t> selected;
    selected.reserve(
        std::min(
            candidates.size(),
            roots_per_opponent * kDeckCount));
    for (std::size_t opponent = 0;
         opponent < kDeckCount; ++opponent) {
        std::vector<std::size_t> stratum;
        for (auto& [loss_key, loss_indices] :
             candidates_by_loss[opponent]) {
            static_cast<void>(loss_key);
            std::sort(
                loss_indices.begin(), loss_indices.end(),
                [&](std::size_t left, std::size_t right) {
                    return std::tie(
                               candidates[left]
                                   .stable_selection_key,
                               candidates[left]
                                   .provenance_key,
                               left) <
                           std::tie(
                               candidates[right]
                                   .stable_selection_key,
                               candidates[right]
                                   .provenance_key,
                               right);
                });
            if (loss_indices.size() > roots_per_loss) {
                loss_indices.resize(roots_per_loss);
            }
            stratum.insert(
                stratum.end(), loss_indices.begin(),
                loss_indices.end());
        }
        std::sort(
            stratum.begin(), stratum.end(),
            [&](std::size_t left, std::size_t right) {
                return std::tie(
                           candidates[left].provenance_key,
                           candidates[left]
                               .stable_selection_key,
                           candidates[left].source_loss_key,
                           left) <
                       std::tie(
                           candidates[right].provenance_key,
                           candidates[right]
                               .stable_selection_key,
                           candidates[right].source_loss_key,
                           right);
            });
        if (stratum.size() > roots_per_opponent) {
            stratum.resize(roots_per_opponent);
        }
        selected.insert(
            selected.end(), stratum.begin(), stratum.end());
    }
    return selected;
}

bool bsr_retention_requirements_met(
    const std::vector<BsrRetentionCandidate>& retained,
    std::size_t roots_per_loss,
    std::size_t roots_per_opponent,
    std::size_t minimum_losses_per_opponent) {
    if (roots_per_loss == 0 || roots_per_opponent == 0 ||
        minimum_losses_per_opponent == 0 ||
        minimum_losses_per_opponent > roots_per_opponent) {
        throw std::invalid_argument(
            "invalid BSR retention requirement");
    }
    std::array<std::size_t, kDeckCount> roots_by_opponent{};
    std::array<std::map<std::string, std::size_t>, kDeckCount>
        roots_by_loss;
    for (const BsrRetentionCandidate& candidate : retained) {
        const std::size_t opponent =
            static_cast<std::size_t>(
                candidate.opponent_deck);
        if (opponent >= kDeckCount ||
            candidate.source_loss_key.empty() ||
            candidate.provenance_key.empty() ||
            candidate.stable_selection_key.empty()) {
            throw std::invalid_argument(
                "invalid retained BSR candidate");
        }
        ++roots_by_opponent[opponent];
        const std::size_t roots_in_loss =
            ++roots_by_loss[opponent]
                [candidate.source_loss_key];
        if (roots_in_loss > roots_per_loss) {
            return false;
        }
    }
    for (std::size_t opponent = 0;
         opponent < kDeckCount; ++opponent) {
        if (roots_by_opponent[opponent] !=
                roots_per_opponent ||
            roots_by_loss[opponent].size() <
                minimum_losses_per_opponent) {
            return false;
        }
    }
    return true;
}

namespace {

constexpr double kBsrNormal95 = 1.96;

std::string bsr_hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0')
           << std::setw(16) << value;
    return output.str();
}

std::string bsr_information_action_key(
    const DecisionProbe& probe) {
    std::string key = dc1_information_set_key(probe);
    std::vector<std::string> descriptors;
    descriptors.reserve(probe.candidates.size());
    for (const Candidate& candidate : probe.candidates) {
        descriptors.push_back(candidate.descriptor);
    }
    std::sort(descriptors.begin(), descriptors.end());
    dc1_append_u64(key, descriptors.size());
    for (const std::string& descriptor : descriptors) {
        dc1_append_u64(key, descriptor.size());
        key += descriptor;
    }
    return key;
}

void bsr_append_text(std::string& key,
                     std::string_view value) {
    dc1_append_u64(key, value.size());
    key.append(value);
}

std::string bsr_stable_root_key(
    const DecisionProbe& probe,
    std::string_view actual_action_descriptor,
    std::string_view model_fingerprint,
    const BsrRootKeyContext& provenance) {
    if (model_fingerprint.empty() ||
        provenance.tracked_seat >= kPlayerCount ||
        probe.root_player != provenance.tracked_seat) {
        throw std::invalid_argument(
            "invalid BSR stable-root identity input");
    }
    const std::size_t actual_matches =
        static_cast<std::size_t>(std::count_if(
            probe.candidates.begin(),
            probe.candidates.end(),
            [&](const Candidate& candidate) {
                return candidate.descriptor ==
                       actual_action_descriptor;
            }));
    if (actual_matches != 1) {
        throw std::invalid_argument(
            "BSR stable-root identity requires one actual "
            "descriptor");
    }

    std::string key;
    key.reserve(1536);
    bsr_append_text(key, kBsrEnvironmentRevision);
    bsr_append_text(key, model_fingerprint);
    const std::string information_action_key =
        bsr_information_action_key(probe);
    bsr_append_text(key, information_action_key);
    bsr_append_text(key, actual_action_descriptor);
    dc1_append_u64(key, provenance.game_seed);
    dc1_append_u64(key, provenance.block);
    dc1_append_u64(key, provenance.schedule_index);
    dc1_append_u64(key, provenance.tracked_seat);
    dc1_append_bool(key, provenance.tracked_starts);
    dc1_append_u64(key, provenance.trace_ordinal);
    return key;
}

std::vector<PriorityAction> bsr_priority_actions(
    const DecisionProbe& probe) {
    std::vector<PriorityAction> actions;
    actions.reserve(probe.candidates.size());
    for (const Candidate& candidate : probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr) {
            throw std::invalid_argument(
                "BSR Priority probe contains an attack candidate");
        }
        actions.push_back(*action);
    }
    return actions;
}

bool bsr_sorcery_actions(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

bool bsr_complete_action_set(
    const DecisionProbe& probe,
    const std::vector<PriorityAction>& candidates) {
    const auto legal = legal_priority_actions(
        probe.state, probe.root_player,
        bsr_sorcery_actions(probe.phase));
    if (legal.size() != candidates.size()) {
        return false;
    }
    std::vector<bool> matched(candidates.size(), false);
    for (const PriorityAction& action : legal) {
        std::size_t matches = 0;
        std::size_t matched_index = 0;
        for (std::size_t index = 0;
             index < candidates.size(); ++index) {
            if (candidates[index] == action) {
                ++matches;
                matched_index = index;
            }
        }
        if (matches != 1 || matched[matched_index]) {
            return false;
        }
        matched[matched_index] = true;
    }
    return std::all_of(
        matched.begin(), matched.end(),
        [](bool value) { return value; });
}

bool bsr_samples_bit_identical(
    const LearnedActionSamples& first,
    const LearnedActionSamples& second) {
    if (first.sampled_worlds != second.sampled_worlds ||
        first.rollout_evaluations !=
            second.rollout_evaluations ||
        first.terminal_evaluations !=
            second.terminal_evaluations ||
        first.bootstrapped_evaluations !=
            second.bootstrapped_evaluations ||
        first.q_samples.size() != second.q_samples.size()) {
        return false;
    }
    for (std::size_t action = 0;
         action < first.q_samples.size(); ++action) {
        if (first.q_samples[action].size() !=
            second.q_samples[action].size()) {
            return false;
        }
        for (std::size_t sample = 0;
             sample < first.q_samples[action].size(); ++sample) {
            if (std::bit_cast<std::uint64_t>(
                    first.q_samples[action][sample]) !=
                std::bit_cast<std::uint64_t>(
                    second.q_samples[action][sample])) {
                return false;
            }
        }
    }
    return true;
}

double bsr_mean(const std::vector<double>& values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "BSR mean requires at least one sample");
    }
    for (const double value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "BSR samples must be finite");
        }
    }
    return std::accumulate(
               values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

struct BsrBestSet {
    double mean = 0.0;
    std::vector<std::size_t> indices;
    std::vector<std::string> descriptors;
};

BsrBestSet bsr_best_set(
    const std::vector<std::string>& descriptors,
    const LearnedActionSamples& samples) {
    if (samples.q_samples.size() != descriptors.size() ||
        descriptors.empty()) {
        throw std::invalid_argument(
            "BSR samples do not match descriptors");
    }
    std::vector<double> means;
    means.reserve(samples.q_samples.size());
    for (const auto& row : samples.q_samples) {
        means.push_back(bsr_mean(row));
    }
    const double best =
        *std::max_element(means.begin(), means.end());
    BsrBestSet result;
    result.mean = best;
    for (std::size_t index = 0;
         index < means.size(); ++index) {
        if (means[index] == best) {
            result.indices.push_back(index);
            result.descriptors.push_back(
                descriptors[index]);
        }
    }
    return result;
}

std::pair<double, double> bsr_paired_regret_and_se(
    const std::vector<double>& best,
    const std::vector<double>& actual) {
    if (best.size() != actual.size() || best.empty()) {
        throw std::invalid_argument(
            "BSR paired rows must have equal nonzero size");
    }
    std::vector<double> differences;
    differences.reserve(best.size());
    for (std::size_t sample = 0;
         sample < best.size(); ++sample) {
        differences.push_back(best[sample] - actual[sample]);
    }
    const double mean = bsr_mean(differences);
    if (differences.size() == 1) {
        return {mean, 0.0};
    }
    double squared_deviations = 0.0;
    for (const double difference : differences) {
        const double centered = difference - mean;
        squared_deviations += centered * centered;
    }
    const double sample_variance =
        squared_deviations /
        static_cast<double>(differences.size() - 1);
    return {
        mean,
        std::sqrt(
            sample_variance /
            static_cast<double>(differences.size())),
    };
}

std::size_t bsr_expected_evaluations(
    std::size_t actions, std::size_t worlds,
    std::size_t rollouts_per_world) {
    if (actions == 0 || worlds == 0 ||
        rollouts_per_world == 0 ||
        actions >
            std::numeric_limits<std::size_t>::max() / worlds ||
        actions * worlds >
            std::numeric_limits<std::size_t>::max() /
                rollouts_per_world) {
        throw std::overflow_error(
            "BSR reference evaluation count overflow");
    }
    return actions * worlds * rollouts_per_world;
}

bool bsr_sample_accounting(
    const LearnedActionSamples& samples,
    std::size_t actions, std::size_t worlds,
    std::size_t rollouts_per_world) {
    const std::size_t samples_per_action =
        bsr_expected_evaluations(
            1, worlds, rollouts_per_world);
    return
        samples.sampled_worlds == worlds &&
        samples.rollout_evaluations ==
            bsr_expected_evaluations(
                actions, worlds, rollouts_per_world) &&
        samples.terminal_evaluations <=
            samples.rollout_evaluations &&
        samples.bootstrapped_evaluations ==
            samples.rollout_evaluations -
                samples.terminal_evaluations &&
        samples.q_samples.size() == actions &&
        std::all_of(
            samples.q_samples.begin(),
            samples.q_samples.end(),
            [&](const std::vector<double>& row) {
                return row.size() == samples_per_action;
            });
}

LearnedActionSamples bsr_score_pass(
    const DecisionProbe& probe,
    const std::vector<PriorityAction>& candidates,
    std::shared_ptr<const LearnedModel> model,
    const BsrReferenceConfig& config,
    std::uint64_t seed, std::size_t worlds) {
    return learned_priority_action_samples(
        probe.state, probe.original_decks, probe.root_player,
        bsr_sorcery_actions(probe.phase), probe.phase,
        probe.consecutive_passes,
        candidates, std::move(model),
        {
            .seed = seed,
            .worlds = worlds,
            .rollouts_per_world =
                config.rollouts_per_world,
            .horizon_turns = config.horizon_turns,
            .continuation_variant =
                LearnedVariant::ValueSearchChampion,
            .value_continuation_epsilon = 0.0,
            .blend_shallow_prior = false,
            .value_priority_residual_weight = 0.0,
            .evaluation_threads =
                config.evaluation_threads,
        });
}

struct BsrHarvestedRoot {
    DecisionProbe probe;
    std::string information_action_key;
    std::string information_action_fingerprint;
    std::string stable_root_key;
    std::string stable_root_fingerprint;
    std::string actual_action_descriptor;
    std::string provenance_key;
    std::string source_game_key;
    std::size_t source_cell = 0;
    std::size_t block = 0;
    std::size_t schedule_index = 0;
    std::size_t tracked_seat = 0;
    bool tracked_starts = false;
    std::uint64_t game_seed = 0;
    std::size_t trace_ordinal = 0;
};

std::string bsr_fixed_width(std::uint64_t value,
                            std::size_t width) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(
                  static_cast<int>(width))
           << value;
    return output.str();
}

std::string bsr_source_game_key(
    const BsrSourceGame& scheduled) {
    return "b" + bsr_fixed_width(scheduled.block, 4) +
           ".g" +
           bsr_fixed_width(scheduled.schedule_index, 3) +
           ".p" + std::to_string(scheduled.tracked_seat) +
           ".d" + (scheduled.tracked_starts ? "p" : "d") +
           ".s" + bsr_fixed_width(scheduled.seed, 20);
}

std::string bsr_provenance_key(
    const BsrSourceGame& scheduled,
    std::size_t trace_ordinal) {
    return bsr_source_game_key(scheduled) +
           ".r" + bsr_fixed_width(trace_ordinal, 6);
}

void bsr_add_cell_to_deck(
    const BsrSourceCellSummary& cell,
    BsrDeckSummary& deck) {
    deck.games += cell.games;
    deck.tracked_losses += cell.tracked_losses;
    deck.draws += cell.draws;
    deck.turn_limit_draws += cell.turn_limit_draws;
    deck.trace_roots += cell.trace_roots;
    deck.tracked_held_opponent_stack_roots +=
        cell.tracked_held_opponent_stack_roots;
    deck.opponent_held_opponent_stack_roots +=
        cell.opponent_held_opponent_stack_roots;
    deck.eligible_loss_roots += cell.eligible_loss_roots;
    deck.loss_games_with_eligible_roots +=
        cell.loss_games_with_eligible_roots;
    deck.retained_roots += cell.retained_roots;
}

} // namespace

std::string bsr_stable_root_fingerprint(
    const DecisionProbe& probe,
    std::string_view actual_action_descriptor,
    std::string_view model_fingerprint,
    const BsrRootKeyContext& provenance) {
    return bsr_hex64(dc1_stable_hash(
        bsr_stable_root_key(
            probe, actual_action_descriptor,
            model_fingerprint, provenance)));
}

BsrPairedRegretEstimate bsr_paired_regret_estimate(
    const std::vector<double>& best_samples,
    const std::vector<double>& actual_samples) {
    const auto [regret, standard_error] =
        bsr_paired_regret_and_se(
            best_samples, actual_samples);
    return {
        .regret = regret,
        .standard_error = standard_error,
        .lower_95 =
            regret - kBsrNormal95 * standard_error,
    };
}

bool bsr_diagnostic_stable_mistake(
    bool scout_confirmation_best_set_stable,
    bool actual_outside_best_sets,
    const BsrPairedRegretEstimate& estimate) {
    return scout_confirmation_best_set_stable &&
           actual_outside_best_sets &&
           estimate.regret >=
               kBsrDiagnosticRegretThreshold &&
           estimate.lower_95 > 0.0;
}

bool bsr_practical_high_cost_mistake(
    bool scout_confirmation_best_set_stable,
    bool actual_outside_best_sets,
    const BsrPairedRegretEstimate& estimate) {
    return scout_confirmation_best_set_stable &&
           actual_outside_best_sets &&
           estimate.regret >=
               kBsrPracticalRegretThreshold &&
           estimate.lower_95 >
               kBsrPracticalLower95Threshold;
}

bool bsr_practical_audit_gate(
    bool audit_valid,
    std::size_t practical_high_cost_mistakes) {
    return audit_valid && practical_high_cost_mistakes != 0;
}

BsrRootScore score_bsr_priority_probe(
    const DecisionProbe& probe,
    std::string_view actual_action_descriptor,
    std::shared_ptr<const LearnedModel> frozen_model,
    BsrReferenceConfig config) {
    if (!frozen_model ||
        probe.decision_kind != DecisionKind::Priority ||
        probe.root_player >= kPlayerCount ||
        probe.state.stack.empty() ||
        probe.candidates.size() < 2 ||
        probe.candidates.size() >
            kBsrMaximumLegalActions ||
        config.seed == 0 ||
        config.scout_worlds == 0 ||
        config.confirmation_worlds == 0 ||
        config.rollouts_per_world == 0 ||
        config.evaluation_threads == 0) {
        throw std::invalid_argument(
            "invalid BSR reference-scoring input");
    }

    DecisionProbe canonical = probe;
    std::sort(
        canonical.candidates.begin(),
        canonical.candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.descriptor < right.descriptor;
        });
    if (std::adjacent_find(
            canonical.candidates.begin(),
            canonical.candidates.end(),
            [](const Candidate& left, const Candidate& right) {
                return left.descriptor == right.descriptor;
            }) != canonical.candidates.end()) {
        throw std::invalid_argument(
            "BSR candidate descriptors must be distinct");
    }

    const std::vector<PriorityAction> candidates =
        bsr_priority_actions(canonical);
    if (!bsr_complete_action_set(canonical, candidates)) {
        throw std::invalid_argument(
            "BSR candidate list is not the complete legal "
            "Priority action set");
    }
    const auto actual = std::find_if(
        canonical.candidates.begin(),
        canonical.candidates.end(),
        [actual_action_descriptor](const Candidate& candidate) {
            return candidate.descriptor ==
                   actual_action_descriptor;
        });
    if (actual == canonical.candidates.end()) {
        throw std::invalid_argument(
            "BSR actual-action descriptor is absent");
    }
    const std::size_t actual_index =
        static_cast<std::size_t>(
            std::distance(
                canonical.candidates.begin(), actual));
    std::vector<std::string> descriptors;
    descriptors.reserve(canonical.candidates.size());
    for (const Candidate& candidate : canonical.candidates) {
        descriptors.push_back(candidate.descriptor);
    }

    const std::string information_action_key =
        bsr_information_action_key(canonical);
    const std::uint64_t scout_seed = dc1_mix_seed(
        config.seed, information_action_key, 0);
    const std::uint64_t confirmation_seed = dc1_mix_seed(
        config.seed, information_action_key, 1);
    if (scout_seed == confirmation_seed) {
        throw std::logic_error(
            "BSR scout and confirmation seeds collided");
    }

    const LearnedActionSamples scout = bsr_score_pass(
        canonical, candidates, frozen_model, config,
        scout_seed, config.scout_worlds);
    const LearnedActionSamples confirmation = bsr_score_pass(
        canonical, candidates, frozen_model, config,
        confirmation_seed, config.confirmation_worlds);

    DecisionProbe hidden = canonical;
    hidden.state = dc1_hidden_repartition_clone(canonical);
    LearnedDecisionTracePoint hidden_point{
        .state = hidden.state,
        .context = {
            .valid = true,
            .phase = hidden.phase,
            .decision_player = hidden.root_player,
            .consecutive_passes =
                hidden.consecutive_passes,
            .sorcery_actions =
                bsr_sorcery_actions(hidden.phase),
        },
        .selected_priority_action =
            candidates[actual_index],
    };
    const BsrRootClassification hidden_classification =
        classify_bsr_trace_root(
            hidden_point, hidden.root_player,
            kBsrMaximumLegalActions);
    const LearnedActionSamples hidden_scout = bsr_score_pass(
        hidden, candidates, frozen_model, config,
        scout_seed, config.scout_worlds);
    const LearnedActionSamples hidden_confirmation =
        bsr_score_pass(
            hidden, candidates, frozen_model, config,
            confirmation_seed,
            config.confirmation_worlds);

    const BsrBestSet scout_best =
        bsr_best_set(descriptors, scout);
    const BsrBestSet confirmation_best =
        bsr_best_set(descriptors, confirmation);
    const BsrPairedRegretEstimate paired =
        bsr_paired_regret_estimate(
            confirmation.q_samples[
                confirmation_best.indices.front()],
            confirmation.q_samples[actual_index]);
    const bool stable_best =
        scout_best.descriptors ==
        confirmation_best.descriptors;
    const auto outside =
        [&](const std::vector<std::string>& best) {
            return std::find(
                       best.begin(), best.end(),
                       actual_action_descriptor) ==
                   best.end();
        };
    const bool actual_outside =
        outside(scout_best.descriptors) &&
        outside(confirmation_best.descriptors);

    const bool scout_accounting = bsr_sample_accounting(
        scout, candidates.size(), config.scout_worlds,
        config.rollouts_per_world);
    const bool confirmation_accounting =
        bsr_sample_accounting(
            confirmation, candidates.size(),
            config.confirmation_worlds,
            config.rollouts_per_world);
    const bool hidden_scout_accounting =
        bsr_sample_accounting(
            hidden_scout, candidates.size(),
            config.scout_worlds,
            config.rollouts_per_world);
    const bool hidden_confirmation_accounting =
        bsr_sample_accounting(
            hidden_confirmation, candidates.size(),
            config.confirmation_worlds,
            config.rollouts_per_world);

    BsrRootScore result;
    result.stable_id = canonical.stable_id;
    result.information_action_fingerprint =
        bsr_hex64(
            dc1_stable_hash(information_action_key));
    result.action_count = candidates.size();
    result.actual_action_index = actual_index;
    result.actual_action_descriptor =
        std::string(actual_action_descriptor);
    result.scout_seed = scout_seed;
    result.confirmation_seed = confirmation_seed;
    result.scout_best_actions = scout_best.descriptors;
    result.confirmation_best_actions =
        confirmation_best.descriptors;
    result.scout_actual_mean =
        bsr_mean(scout.q_samples[actual_index]);
    result.scout_best_mean = scout_best.mean;
    result.confirmation_actual_mean =
        bsr_mean(confirmation.q_samples[actual_index]);
    result.confirmation_best_mean =
        confirmation_best.mean;
    result.confirmation_regret = paired.regret;
    result.paired_standard_error =
        paired.standard_error;
    result.paired_lower_95 = paired.lower_95;
    result.sampled_worlds =
        scout.sampled_worlds +
        confirmation.sampled_worlds +
        hidden_scout.sampled_worlds +
        hidden_confirmation.sampled_worlds;
    result.rollout_evaluations =
        scout.rollout_evaluations +
        confirmation.rollout_evaluations +
        hidden_scout.rollout_evaluations +
        hidden_confirmation.rollout_evaluations;
    result.terminal_evaluations =
        scout.terminal_evaluations +
        confirmation.terminal_evaluations +
        hidden_scout.terminal_evaluations +
        hidden_confirmation.terminal_evaluations;
    result.bootstrapped_evaluations =
        scout.bootstrapped_evaluations +
        confirmation.bootstrapped_evaluations +
        hidden_scout.bootstrapped_evaluations +
        hidden_confirmation.bootstrapped_evaluations;
    result.scout_confirmation_best_set_stable =
        stable_best;
    result.actual_outside_best_sets = actual_outside;
    result.diagnostic_stable_mistake =
        bsr_diagnostic_stable_mistake(
            stable_best, actual_outside, paired);
    result.practical_high_cost_mistake =
        bsr_practical_high_cost_mistake(
            stable_best, actual_outside, paired);
    result.descriptor_order_invariant =
        std::is_sorted(
            canonical.candidates.begin(),
            canonical.candidates.end(),
            [](const Candidate& left,
               const Candidate& right) {
                return left.descriptor <
                       right.descriptor;
            });
    result.hidden_repartition_eligible =
        hidden_classification.eligible() &&
        hidden_classification.legal_actions.size() ==
            candidates.size() &&
        std::all_of(
            candidates.begin(), candidates.end(),
            [&](const PriorityAction& action) {
                return static_cast<std::size_t>(
                           std::count(
                               hidden_classification
                                   .legal_actions.begin(),
                               hidden_classification
                                   .legal_actions.end(),
                               action)) == 1;
            });
    result.hidden_repartition_bit_identical =
        bsr_samples_bit_identical(
            scout, hidden_scout) &&
        bsr_samples_bit_identical(
            confirmation, hidden_confirmation);
    result.accounting_passed =
        scout_accounting && confirmation_accounting &&
        hidden_scout_accounting &&
        hidden_confirmation_accounting &&
        result.terminal_evaluations <=
            result.rollout_evaluations &&
        result.bootstrapped_evaluations ==
            result.rollout_evaluations -
                result.terminal_evaluations;
    return result;
}

BsrAuditReport audit_bsr_blue_stack_regret(
    std::shared_ptr<const LearnedModel> frozen_model,
    BsrAuditConfig config) {
    if (!frozen_model ||
        config.required_model_fingerprint.empty() ||
        config.source_seed == 0 ||
        config.reference_seed == 0 ||
        config.source_seed == config.reference_seed ||
        config.source_blocks == 0 ||
        config.source_max_turns == 0 ||
        config.production_worlds == 0 ||
        config.roots_per_loss == 0 ||
        config.roots_per_opponent == 0 ||
        config.minimum_losses_per_opponent == 0 ||
        config.maximum_legal_actions < 2 ||
        config.reference.seed != config.reference_seed ||
        config.reference.scout_worlds == 0 ||
        config.reference.confirmation_worlds == 0 ||
        config.reference.rollouts_per_world == 0 ||
        config.reference.evaluation_threads == 0 ||
        config.roots_per_opponent >
            config.source_blocks * 4 *
                config.roots_per_loss ||
        config.minimum_losses_per_opponent >
            config.roots_per_opponent) {
        throw std::invalid_argument(
            "invalid BSR audit configuration");
    }
    const std::string fingerprint =
        learned_model_fingerprint(frozen_model);
    if (fingerprint != config.required_model_fingerprint) {
        throw std::invalid_argument(
            "BSR frozen-model fingerprint mismatch");
    }

    BsrAuditReport report;
    report.model_fingerprint = fingerprint;
    report.config = config;
    report.schedule = bsr_source_schedule(
        config.source_seed, config.source_blocks);
    report.descriptor_order_invariant = true;
    report.hidden_repartition_passed = true;
    report.scout_confirmation_seeds_disjoint = true;
    bool traced_actions_valid = true;
    bool observed_action_bound_passed = true;
    std::vector<BsrHarvestedRoot> harvested;

    for (std::size_t cell_index = 0;
         cell_index < report.source_cells.size();
         ++cell_index) {
        const std::size_t opponent = cell_index / 4;
        const std::size_t within_opponent = cell_index % 4;
        report.source_cells[cell_index].opponent_deck =
            static_cast<DeckId>(opponent);
        report.source_cells[cell_index].tracked_seat =
            within_opponent / 2;
        report.source_cells[cell_index].tracked_starts =
            within_opponent % 2 == 1;
    }

    for (const BsrSourceGame& scheduled :
         report.schedule) {
        if (scheduled.schedule_index >=
                report.source_cells.size() ||
            scheduled.tracked_seat >= kPlayerCount) {
            throw std::logic_error(
                "BSR source schedule contains an invalid cell");
        }
        BsrSourceCellSummary& cell =
            report.source_cells[scheduled.schedule_index];
        if (cell.opponent_deck !=
                scheduled.opponent_deck ||
            cell.tracked_seat != scheduled.tracked_seat ||
            cell.tracked_starts !=
                scheduled.tracked_starts) {
            throw std::logic_error(
                "BSR source schedule cell metadata changed");
        }

        const std::size_t tracked = scheduled.tracked_seat;
        const std::size_t opponent = 1 - tracked;
        const std::array<std::vector<CardId>, 2> decks = {
            tracked == 0
                ? deck_for(DeckId::Blue)
                : deck_for(scheduled.opponent_deck),
            tracked == 1
                ? deck_for(DeckId::Blue)
                : deck_for(scheduled.opponent_deck),
        };
        GameConfig game_config;
        game_config.max_turns = config.source_max_turns;
        game_config.starting_player =
            scheduled.starting_player;
        game_config.learned_model = frozen_model;
        game_config.learned_search_depth = 1;
        game_config.bots[tracked] = {
            .kind = BotKind::Learned,
            .learned_variant =
                LearnedVariant::ValueSearchChampion,
            .rollouts_per_action =
                config.production_worlds,
            .exploration_rate = 0.0,
            .value_continuation_epsilon = 0.0,
            .value_priority_residual_weight = 0.0,
            .learned_model = frozen_model,
        };
        game_config.bots[opponent] = {
            .kind = BotKind::Handcrafted,
        };

        Game game(
            decks[0], decks[1], scheduled.seed,
            game_config);
        std::vector<LearnedDecisionTracePoint> trace;
        const GameResult result =
            game.run_with_priority_root_trace(trace);
        ++cell.games;
        cell.trace_roots += trace.size();
        const bool tracked_lost =
            result.winner == static_cast<int>(opponent);
        if (tracked_lost) {
            ++cell.tracked_losses;
        } else if (result.winner < 0) {
            ++cell.draws;
            if (result.reason == EndReason::TurnLimit) {
                ++cell.turn_limit_draws;
            }
        }

        std::vector<BsrHarvestedRoot> game_roots;
        for (std::size_t ordinal = 0;
             ordinal < trace.size(); ++ordinal) {
            const LearnedDecisionTracePoint& point =
                trace[ordinal];
            if (!point.context.valid ||
                point.context.decision_player >=
                    kPlayerCount) {
                traced_actions_valid = false;
                continue;
            }
            const auto trace_actions = legal_priority_actions(
                point.state,
                point.context.decision_player,
                point.context.sorcery_actions);
            const std::size_t trace_action_matches =
                point.selected_priority_action.has_value()
                    ? static_cast<std::size_t>(
                          std::count(
                              trace_actions.begin(),
                              trace_actions.end(),
                              *point.selected_priority_action))
                    : 0;
            traced_actions_valid =
                traced_actions_valid &&
                trace_action_matches == 1;

            if (!point.state.stack.empty() &&
                point.state.stack.back().controller <
                    kPlayerCount) {
                const std::size_t controller =
                    point.state.stack.back().controller;
                if (point.context.decision_player == tracked &&
                    controller == opponent) {
                    ++cell
                          .tracked_held_opponent_stack_roots;
                } else if (
                    point.context.decision_player == opponent &&
                    controller == opponent) {
                    ++cell
                          .opponent_held_opponent_stack_roots;
                }
            }

            const BsrRootClassification classification =
                classify_bsr_trace_root(
                    point, tracked,
                    config.maximum_legal_actions);
            if (classification.eligibility ==
                    BsrRootEligibility::
                        ActionCountOutsideBounds &&
                classification.legal_actions.size() >
                    config.maximum_legal_actions) {
                observed_action_bound_passed = false;
            }
            if (!tracked_lost ||
                !classification.eligible()) {
                continue;
            }
            ++cell.eligible_loss_roots;

            DecisionProbe probe;
            probe.category = Category::BlueCounterWar;
            probe.decision_kind = DecisionKind::Priority;
            probe.root_deck = DeckId::Blue;
            probe.opponent_deck =
                scheduled.opponent_deck;
            probe.root_player = tracked;
            probe.phase = point.context.phase;
            probe.consecutive_passes =
                point.context.consecutive_passes;
            probe.state = point.state;
            probe.original_decks = decks;
            probe.candidates.reserve(
                classification.legal_actions.size());
            for (std::size_t action = 0;
                 action <
                 classification.legal_actions.size();
                 ++action) {
                probe.candidates.push_back(
                    priority_candidate(
                        classification.descriptors[action],
                        classification
                            .legal_actions[action]));
            }

            const std::string information_action_key =
                bsr_information_action_key(probe);
            const std::string provenance_key =
                bsr_provenance_key(
                    scheduled, ordinal);
            const std::string actual_action_descriptor =
                classification.descriptors.at(
                    *classification.selected_action_index);
            const BsrRootKeyContext root_key_context{
                .game_seed = scheduled.seed,
                .block = scheduled.block,
                .schedule_index =
                    scheduled.schedule_index,
                .tracked_seat =
                    scheduled.tracked_seat,
                .tracked_starts =
                    scheduled.tracked_starts,
                .trace_ordinal = ordinal,
            };
            const std::string stable_root_key =
                bsr_stable_root_key(
                    probe, actual_action_descriptor,
                    fingerprint, root_key_context);
            const std::string stable_root_fingerprint =
                bsr_hex64(
                    dc1_stable_hash(stable_root_key));
            probe.stable_id =
                "bsr0." +
                std::string(
                    deck_name(
                        scheduled.opponent_deck)) +
                "." + provenance_key + ".k" +
                stable_root_fingerprint;
            probe.harvest = HarvestProvenance{
                .collector =
                    "Game::run_with_priority_root_trace",
                .trajectory_script =
                    "bsr0-blue-vs-handcrafted-source-v1",
                .game_seed = scheduled.seed,
                .starting_player =
                    scheduled.starting_player,
                .priority_decision_ordinal = ordinal,
                .turn_number =
                    point.state.turn_number,
                .phase = point.context.phase,
            };
            game_roots.push_back({
                .probe = std::move(probe),
                .information_action_key =
                    information_action_key,
                .information_action_fingerprint =
                    bsr_hex64(
                        dc1_stable_hash(
                            information_action_key)),
                .stable_root_key =
                    stable_root_key,
                .stable_root_fingerprint =
                    stable_root_fingerprint,
                .actual_action_descriptor =
                    actual_action_descriptor,
                .provenance_key = provenance_key,
                .source_game_key =
                    bsr_source_game_key(scheduled),
                .source_cell =
                    scheduled.schedule_index,
                .block = scheduled.block,
                .schedule_index =
                    scheduled.schedule_index,
                .tracked_seat =
                    scheduled.tracked_seat,
                .tracked_starts =
                    scheduled.tracked_starts,
                .game_seed = scheduled.seed,
                .trace_ordinal = ordinal,
            });
        }

        if (tracked_lost && !game_roots.empty()) {
            ++cell.loss_games_with_eligible_roots;
        }
        harvested.insert(
            harvested.end(),
            std::make_move_iterator(game_roots.begin()),
            std::make_move_iterator(game_roots.end()));
    }

    std::array<std::set<std::string>, kDeckCount>
        retained_loss_keys;
    std::vector<BsrRetentionCandidate> retention_candidates;
    retention_candidates.reserve(harvested.size());
    for (const BsrHarvestedRoot& root : harvested) {
        retention_candidates.push_back({
            .opponent_deck = root.probe.opponent_deck,
            .source_loss_key = root.source_game_key,
            .provenance_key = root.provenance_key,
            .stable_selection_key =
                root.information_action_key,
        });
    }
    const std::vector<std::size_t> retained_indices =
        select_bsr_retained_candidate_indices(
            retention_candidates, config.roots_per_loss,
            config.roots_per_opponent);
    std::vector<BsrHarvestedRoot> retained;
    std::vector<BsrRetentionCandidate>
        retained_contract_candidates;
    retained.reserve(retained_indices.size());
    retained_contract_candidates.reserve(
        retained_indices.size());
    for (const std::size_t index : retained_indices) {
        BsrHarvestedRoot& root = harvested.at(index);
        const std::size_t opponent =
            static_cast<std::size_t>(
                root.probe.opponent_deck);
        ++report.source_cells.at(root.source_cell)
              .retained_roots;
        retained_loss_keys.at(opponent).insert(
            root.source_game_key);
        retained_contract_candidates.push_back(
            retention_candidates.at(index));
        retained.push_back(std::move(root));
    }
    std::sort(
        retained.begin(), retained.end(),
        [](const BsrHarvestedRoot& left,
           const BsrHarvestedRoot& right) {
            return std::tie(
                       left.probe.opponent_deck,
                       left.provenance_key,
                       left.stable_root_key) <
                   std::tie(
                       right.probe.opponent_deck,
                       right.provenance_key,
                       right.stable_root_key);
        });

    for (std::size_t deck_index = 0;
         deck_index < kDeckCount; ++deck_index) {
        report.decks[deck_index].opponent_deck =
            static_cast<DeckId>(deck_index);
    }
    for (const BsrSourceCellSummary& cell :
         report.source_cells) {
        const std::size_t deck_index =
            static_cast<std::size_t>(
                cell.opponent_deck);
        bsr_add_cell_to_deck(
            cell, report.decks.at(deck_index));
    }
    for (std::size_t deck_index = 0;
         deck_index < kDeckCount; ++deck_index) {
        report.decks[deck_index]
            .retained_distinct_losses =
            retained_loss_keys[deck_index].size();
    }

    report.roots.reserve(retained.size());
    for (BsrHarvestedRoot& root : retained) {
        BsrRootScore score = score_bsr_priority_probe(
            root.probe, root.actual_action_descriptor,
            frozen_model, config.reference);
        if (score.information_action_fingerprint !=
            root.information_action_fingerprint) {
            throw std::logic_error(
                "BSR reference changed the information/action key");
        }
        const std::size_t deck_index =
            static_cast<std::size_t>(
                root.probe.opponent_deck);
        BsrDeckSummary& deck =
            report.decks.at(deck_index);
        if (score.diagnostic_stable_mistake) {
            ++deck.diagnostic_stable_mistakes;
        }
        if (score.practical_high_cost_mistake) {
            ++deck.practical_high_cost_mistakes;
        }
        deck.reference_rollout_evaluations +=
            score.rollout_evaluations;
        deck.reference_terminal_evaluations +=
            score.terminal_evaluations;
        deck.reference_bootstrapped_evaluations +=
            score.bootstrapped_evaluations;
        report.descriptor_order_invariant =
            report.descriptor_order_invariant &&
            score.descriptor_order_invariant;
        report.hidden_repartition_passed =
            report.hidden_repartition_passed &&
            score.hidden_repartition_eligible &&
            score.hidden_repartition_bit_identical;
        report.scout_confirmation_seeds_disjoint =
            report.scout_confirmation_seeds_disjoint &&
            score.scout_seed != score.confirmation_seed;
        report.reference_rollout_evaluations +=
            score.rollout_evaluations;
        report.reference_terminal_evaluations +=
            score.terminal_evaluations;
        report.reference_bootstrapped_evaluations +=
            score.bootstrapped_evaluations;
        report.roots.push_back({
            .stable_id = root.probe.stable_id,
            .stable_root_fingerprint =
                root.stable_root_fingerprint,
            .information_action_fingerprint =
                root.information_action_fingerprint,
            .opponent_deck =
                root.probe.opponent_deck,
            .block = root.block,
            .schedule_index = root.schedule_index,
            .tracked_seat = root.tracked_seat,
            .tracked_starts = root.tracked_starts,
            .game_seed = root.game_seed,
            .trace_ordinal = root.trace_ordinal,
            .turn_number =
                root.probe.state.turn_number,
            .phase = root.probe.phase,
            .action_count =
                root.probe.candidates.size(),
            .actual_action_descriptor =
                root.actual_action_descriptor,
            .score = std::move(score),
        });
    }

    for (const BsrSourceCellSummary& cell :
         report.source_cells) {
        report.source_games += cell.games;
        report.tracked_losses += cell.tracked_losses;
        report.draws += cell.draws;
        report.turn_limit_draws +=
            cell.turn_limit_draws;
        report.trace_roots += cell.trace_roots;
        report.tracked_held_opponent_stack_roots +=
            cell.tracked_held_opponent_stack_roots;
        report.opponent_held_opponent_stack_roots +=
            cell.opponent_held_opponent_stack_roots;
        report.eligible_loss_roots +=
            cell.eligible_loss_roots;
        report.loss_games_with_eligible_roots +=
            cell.loss_games_with_eligible_roots;
    }
    for (const BsrDeckSummary& deck : report.decks) {
        report.retained_distinct_losses +=
            deck.retained_distinct_losses;
        report.diagnostic_stable_mistakes +=
            deck.diagnostic_stable_mistakes;
        report.practical_high_cost_mistakes +=
            deck.practical_high_cost_mistakes;
        if (deck.practical_high_cost_mistakes != 0) {
            ++report.mistake_opponent_strata;
        }
    }

    const std::size_t expected_source_games =
        config.source_blocks *
        kBsrSourceGamesPerBlock;
    report.source_balance_passed =
        report.schedule.size() == expected_source_games &&
        report.source_games == expected_source_games &&
        std::all_of(
            report.source_cells.begin(),
            report.source_cells.end(),
            [&](const BsrSourceCellSummary& cell) {
                return cell.games ==
                    config.source_blocks;
            }) &&
        std::all_of(
            report.decks.begin(), report.decks.end(),
            [&](const BsrDeckSummary& deck) {
                return deck.games ==
                    config.source_blocks * 4;
            });

    const std::size_t expected_retained =
        config.roots_per_opponent * kDeckCount;
    report.retention_passed =
        retained.size() == expected_retained &&
        report.roots.size() == expected_retained &&
        bsr_retention_requirements_met(
            retained_contract_candidates,
            config.roots_per_loss,
            config.roots_per_opponent,
            config.minimum_losses_per_opponent) &&
        std::all_of(
            report.decks.begin(), report.decks.end(),
            [&](const BsrDeckSummary& deck) {
                return
                    deck.retained_roots ==
                        config.roots_per_opponent &&
                    deck.retained_distinct_losses >=
                        config
                            .minimum_losses_per_opponent;
            });

    std::size_t deck_games = 0;
    std::size_t deck_losses = 0;
    std::size_t deck_draws = 0;
    std::size_t deck_turn_limit_draws = 0;
    std::size_t deck_trace_roots = 0;
    std::size_t deck_tracked_held = 0;
    std::size_t deck_opponent_held = 0;
    std::size_t deck_eligible = 0;
    std::size_t deck_eligible_games = 0;
    std::size_t deck_retained = 0;
    std::size_t deck_diagnostic = 0;
    std::size_t deck_practical = 0;
    std::size_t deck_reference_rollouts = 0;
    std::size_t deck_reference_terminal = 0;
    std::size_t deck_reference_bootstrapped = 0;
    for (const BsrDeckSummary& deck : report.decks) {
        deck_games += deck.games;
        deck_losses += deck.tracked_losses;
        deck_draws += deck.draws;
        deck_turn_limit_draws +=
            deck.turn_limit_draws;
        deck_trace_roots += deck.trace_roots;
        deck_tracked_held +=
            deck.tracked_held_opponent_stack_roots;
        deck_opponent_held +=
            deck.opponent_held_opponent_stack_roots;
        deck_eligible += deck.eligible_loss_roots;
        deck_eligible_games +=
            deck.loss_games_with_eligible_roots;
        deck_retained += deck.retained_roots;
        deck_diagnostic +=
            deck.diagnostic_stable_mistakes;
        deck_practical +=
            deck.practical_high_cost_mistakes;
        deck_reference_rollouts +=
            deck.reference_rollout_evaluations;
        deck_reference_terminal +=
            deck.reference_terminal_evaluations;
        deck_reference_bootstrapped +=
            deck.reference_bootstrapped_evaluations;
    }
    std::size_t root_rollouts = 0;
    std::size_t root_terminal = 0;
    std::size_t root_bootstrapped = 0;
    bool root_accounting = true;
    for (const BsrRetainedRoot& root : report.roots) {
        root_rollouts += root.score.rollout_evaluations;
        root_terminal += root.score.terminal_evaluations;
        root_bootstrapped +=
            root.score.bootstrapped_evaluations;
        root_accounting =
            root_accounting &&
            root.score.accounting_passed &&
            root.action_count ==
                root.score.action_count &&
            root.actual_action_descriptor ==
                root.score.actual_action_descriptor;
    }
    report.traced_actions_valid = traced_actions_valid;
    report.accounting_passed =
        deck_games == report.source_games &&
        deck_losses == report.tracked_losses &&
        deck_draws == report.draws &&
        deck_turn_limit_draws ==
            report.turn_limit_draws &&
        deck_trace_roots == report.trace_roots &&
        deck_tracked_held ==
            report.tracked_held_opponent_stack_roots &&
        deck_opponent_held ==
            report.opponent_held_opponent_stack_roots &&
        deck_eligible == report.eligible_loss_roots &&
        deck_eligible_games ==
            report.loss_games_with_eligible_roots &&
        deck_retained == report.roots.size() &&
        deck_diagnostic ==
            report.diagnostic_stable_mistakes &&
        deck_practical ==
            report.practical_high_cost_mistakes &&
        deck_reference_rollouts ==
            report.reference_rollout_evaluations &&
        deck_reference_terminal ==
            report.reference_terminal_evaluations &&
        deck_reference_bootstrapped ==
            report.reference_bootstrapped_evaluations &&
        root_rollouts ==
            report.reference_rollout_evaluations &&
        root_terminal ==
            report.reference_terminal_evaluations &&
        root_bootstrapped ==
            report.reference_bootstrapped_evaluations &&
        report.reference_terminal_evaluations <=
            report.reference_rollout_evaluations &&
        report.reference_bootstrapped_evaluations ==
            report.reference_rollout_evaluations -
                report.reference_terminal_evaluations &&
        root_accounting;

    const std::size_t reference_worlds =
        config.reference.scout_worlds +
        config.reference.confirmation_worlds;
    const std::size_t maximum_per_original_pass =
        bsr_expected_evaluations(
            config.maximum_legal_actions,
            reference_worlds,
            config.reference.rollouts_per_world);
    if (maximum_per_original_pass >
        std::numeric_limits<std::size_t>::max() / 2) {
        throw std::overflow_error(
            "BSR hidden-clone evaluation bound overflow");
    }
    const std::size_t maximum_reference_evaluations =
        expected_retained >
                std::numeric_limits<std::size_t>::max() /
                    (maximum_per_original_pass * 2)
            ? std::numeric_limits<std::size_t>::max()
            : expected_retained *
                  maximum_per_original_pass * 2;
    report.bounds_passed =
        observed_action_bound_passed &&
        report.roots.size() <= expected_retained &&
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [&](const BsrRetainedRoot& root) {
                return root.action_count >= 2 &&
                       root.action_count <=
                           config.maximum_legal_actions;
            }) &&
        report.reference_rollout_evaluations <=
            maximum_reference_evaluations;
    report.audit_valid =
        report.source_balance_passed &&
        report.retention_passed &&
        report.traced_actions_valid &&
        report.descriptor_order_invariant &&
        report.hidden_repartition_passed &&
        report.scout_confirmation_seeds_disjoint &&
        report.accounting_passed &&
        report.bounds_passed;
    report.diagnostic_replication_found =
        report.audit_valid &&
        report.diagnostic_stable_mistakes != 0;
    report.gate_passed = bsr_practical_audit_gate(
        report.audit_valid,
        report.practical_high_cost_mistakes);
    return report;
}

} // namespace old_school::probes
