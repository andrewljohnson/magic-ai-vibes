#include "alpha/game.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace alpha {
namespace {

constexpr std::array<CardDefinition, 13> kCardDefinitions = {{
    {CardId::Forest, "Forest", CardType::Land, {}, 0, 0, 0},
    {CardId::Mountain, "Mountain", CardType::Land, {}, 0, 0, 0},
    {CardId::GrizzlyBears,
     "Grizzly Bears",
     CardType::Creature,
     {.generic = 1, .green = 1, .red = 0},
     2,
     2,
     0},
    {CardId::LightningBolt,
     "Lightning Bolt",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 1},
     0,
     0,
     3},
    {CardId::IronrootTreefolk,
     "Ironroot Treefolk",
     CardType::Creature,
     {.generic = 4, .green = 1, .red = 0},
     3,
     5,
     0},
    {CardId::FireElemental,
     "Fire Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 2},
     5,
     4,
     0},
    {CardId::Island, "Island", CardType::Land, {}, 0, 0, 0},
    {CardId::Counterspell,
     "Counterspell",
     CardType::Instant,
     {.generic = 0, .green = 0, .red = 0, .blue = 2},
     0,
     0,
     0},
    {CardId::WaterElemental,
     "Water Elemental",
     CardType::Creature,
     {.generic = 3, .green = 0, .red = 0, .blue = 2},
     5,
     4,
     0},
    {CardId::Tsunami,
     "Tsunami",
     CardType::Sorcery,
     {.generic = 3, .green = 1, .red = 0, .blue = 0},
     0,
     0,
     0},
    {CardId::Plains, "Plains", CardType::Land, {}, 0, 0, 0},
    {CardId::Millstone,
     "Millstone",
     CardType::Artifact,
     {.generic = 2},
     0,
     0,
     0},
    {CardId::Moat,
     "Moat",
     CardType::Enchantment,
     {.generic = 2,
      .green = 0,
      .red = 0,
      .blue = 0,
      .white = 2},
     0,
     0,
     0},
}};

constexpr std::array<CardId, 4> kCreatureCards = {
    CardId::GrizzlyBears,
    CardId::IronrootTreefolk,
    CardId::FireElemental,
    CardId::WaterElemental,
};

constexpr std::array<CardId, 1> kSorceryCards = {
    CardId::Tsunami,
};

constexpr std::array<CardId, 1> kArtifactCards = {
    CardId::Millstone,
};

constexpr std::array<CardId, 1> kEnchantmentCards = {
    CardId::Moat,
};

constexpr ManaCost kMillstoneActivationCost = {.generic = 2};

bool has_card(const std::vector<CardId>& cards, CardId wanted) {
    return std::find(cards.begin(), cards.end(), wanted) != cards.end();
}

bool remove_card(std::vector<CardId>& cards, CardId wanted) {
    const auto position = std::find(cards.begin(), cards.end(), wanted);
    if (position == cards.end()) {
        return false;
    }
    cards.erase(position);
    return true;
}

bool is_land(CardId card) {
    return card == CardId::Forest || card == CardId::Mountain ||
           card == CardId::Island || card == CardId::Plains;
}

bool can_pay(const PlayerState& player, const ManaCost& cost) {
    int forests = 0;
    int mountains = 0;
    int islands = 0;
    int plains = 0;
    int total = 0;
    for (const auto& land : player.lands) {
        if (land.tapped) {
            continue;
        }
        ++total;
        if (land.card == CardId::Forest) {
            ++forests;
        } else if (land.card == CardId::Mountain) {
            ++mountains;
        } else if (land.card == CardId::Island) {
            ++islands;
        } else if (land.card == CardId::Plains) {
            ++plains;
        }
    }

    if (forests < cost.green || mountains < cost.red ||
        islands < cost.blue || plains < cost.white) {
        return false;
    }
    return total >= cost.green + cost.red + cost.blue + cost.white +
                        cost.generic;
}

bool pay_mana(PlayerState& player, const ManaCost& cost) {
    if (!can_pay(player, cost)) {
        return false;
    }

    std::vector<bool> selected(player.lands.size(), false);
    auto select_colored = [&](CardId land_type, int amount) {
        for (std::size_t index = 0;
             index < player.lands.size() && amount > 0; ++index) {
            if (!player.lands[index].tapped &&
                player.lands[index].card == land_type) {
                selected[index] = true;
                --amount;
            }
        }
    };

    select_colored(CardId::Forest, cost.green);
    select_colored(CardId::Mountain, cost.red);
    select_colored(CardId::Island, cost.blue);
    select_colored(CardId::Plains, cost.white);

    int generic_remaining = cost.generic;
    for (std::size_t index = 0;
         index < player.lands.size() && generic_remaining > 0; ++index) {
        if (!player.lands[index].tapped && !selected[index]) {
            selected[index] = true;
            --generic_remaining;
        }
    }

    for (std::size_t index = 0; index < player.lands.size(); ++index) {
        if (selected[index]) {
            player.lands[index].tapped = true;
        }
    }
    return true;
}

CreaturePermanent* find_creature(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [id](const CreaturePermanent& creature) { return creature.id == id; });
    return position == player.creatures.end() ? nullptr : &*position;
}

ArtifactPermanent* find_artifact(PlayerState& player, PermanentId id) {
    const auto position = std::find_if(
        player.artifacts.begin(), player.artifacts.end(),
        [id](const ArtifactPermanent& artifact) {
            return artifact.id == id;
        });
    return position == player.artifacts.end() ? nullptr : &*position;
}

bool moat_on_battlefield(const GameState& state) {
    return std::any_of(
        state.players.begin(), state.players.end(),
        [](const PlayerState& player) {
            return std::find(player.enchantments.begin(),
                             player.enchantments.end(),
                             CardId::Moat) != player.enchantments.end();
        });
}

bool can_attack_through_moat(const GameState& state,
                             const CreaturePermanent& creature) {
    return !moat_on_battlefield(state) ||
           card_definition(creature.card).flying;
}

void remove_dead_creatures(PlayerState& player) {
    auto creature = player.creatures.begin();
    while (creature != player.creatures.end()) {
        const auto& definition = card_definition(creature->card);
        if (creature->damage >= definition.toughness) {
            player.graveyard.push_back(creature->card);
            creature = player.creatures.erase(creature);
        } else {
            ++creature;
        }
    }
}

bool contains_action(const std::vector<PriorityAction>& actions,
                     const PriorityAction& wanted) {
    return std::find(actions.begin(), actions.end(), wanted) != actions.end();
}

std::size_t opponent_of(std::size_t player) {
    return 1 - player;
}

} // namespace

const CardDefinition& card_definition(CardId card) {
    const auto index = static_cast<std::size_t>(card);
    if (index >= kCardDefinitions.size()) {
        throw std::out_of_range("unknown card ID");
    }
    return kCardDefinitions[index];
}

std::vector<CardId> green_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Forest);
    deck.insert(deck.end(), 9, CardId::GrizzlyBears);
    deck.insert(deck.end(), 12, CardId::IronrootTreefolk);
    deck.insert(deck.end(), 1, CardId::Tsunami);
    return deck;
}

std::vector<CardId> red_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Mountain);
    deck.insert(deck.end(), 10, CardId::LightningBolt);
    deck.insert(deck.end(), 12, CardId::FireElemental);
    return deck;
}

std::vector<CardId> blue_alpha_deck() {
    std::vector<CardId> deck(18, CardId::Island);
    deck.insert(deck.end(), 14, CardId::Counterspell);
    deck.insert(deck.end(), 8, CardId::WaterElemental);
    return deck;
}

std::vector<CardId> white_control_deck() {
    std::vector<CardId> deck(22, CardId::Plains);
    deck.insert(deck.end(), 3, CardId::Millstone);
    deck.insert(deck.end(), 15, CardId::Moat);
    return deck;
}

Target Target::player_target(std::size_t player_index) {
    return {.player = player_index, .creature = std::nullopt};
}

Target Target::creature_target(std::size_t controller,
                               PermanentId creature_id) {
    return {.player = controller, .creature = creature_id};
}

PriorityAction PriorityAction::pass() {
    return {};
}

PriorityAction PriorityAction::play_land(CardId land) {
    return {.kind = PriorityActionKind::PlayLand, .card = land};
}

PriorityAction PriorityAction::cast_creature(CardId creature) {
    return {.kind = PriorityActionKind::CastCreature, .card = creature};
}

PriorityAction PriorityAction::cast_sorcery(CardId sorcery) {
    return {.kind = PriorityActionKind::CastSorcery, .card = sorcery};
}

PriorityAction PriorityAction::cast_artifact(CardId artifact) {
    return {.kind = PriorityActionKind::CastArtifact, .card = artifact};
}

PriorityAction
PriorityAction::cast_enchantment(CardId enchantment) {
    return {
        .kind = PriorityActionKind::CastEnchantment,
        .card = enchantment,
    };
}

PriorityAction PriorityAction::cast_lightning_bolt(Target bolt_target) {
    return {.kind = PriorityActionKind::CastLightningBolt,
            .card = CardId::LightningBolt,
            .target = bolt_target};
}

PriorityAction
PriorityAction::cast_counterspell(StackObjectId target_spell) {
    return {
        .kind = PriorityActionKind::CastCounterspell,
        .card = CardId::Counterspell,
        .target = std::nullopt,
        .spell_target = target_spell,
    };
}

PriorityAction
PriorityAction::activate_millstone(PermanentId millstone,
                                   Target mill_target) {
    return {
        .kind = PriorityActionKind::ActivateMillstone,
        .card = CardId::Millstone,
        .target = mill_target,
        .spell_target = std::nullopt,
        .source_permanent = millstone,
    };
}

std::vector<PriorityAction>
legal_priority_actions(const GameState& state, std::size_t player,
                       bool sorcery_actions) {
    if (player >= state.players.size()) {
        return {};
    }

    const auto& player_state = state.players[player];
    std::vector<PriorityAction> actions = {PriorityAction::pass()};
    const bool has_sorcery_timing =
        sorcery_actions && player == state.active_player &&
        state.stack.empty();

    if (has_sorcery_timing && !player_state.land_played_this_turn) {
        for (const CardId land :
             {CardId::Forest, CardId::Mountain, CardId::Island,
              CardId::Plains}) {
            if (has_card(player_state.hand, land)) {
                actions.push_back(PriorityAction::play_land(land));
            }
        }
    }

    if (has_sorcery_timing) {
        for (const CardId creature : kCreatureCards) {
            const auto& definition = card_definition(creature);
            if (has_card(player_state.hand, creature) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_creature(creature));
            }
        }
        for (const CardId sorcery : kSorceryCards) {
            const auto& definition = card_definition(sorcery);
            if (has_card(player_state.hand, sorcery) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_sorcery(sorcery));
            }
        }
        for (const CardId artifact : kArtifactCards) {
            const auto& definition = card_definition(artifact);
            if (has_card(player_state.hand, artifact) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_artifact(artifact));
            }
        }
        for (const CardId enchantment : kEnchantmentCards) {
            const auto& definition = card_definition(enchantment);
            if (has_card(player_state.hand, enchantment) &&
                can_pay(player_state, definition.cost)) {
                actions.push_back(
                    PriorityAction::cast_enchantment(enchantment));
            }
        }
    }

    const auto& bolt = card_definition(CardId::LightningBolt);
    if (has_card(player_state.hand, CardId::LightningBolt) &&
        can_pay(player_state, bolt.cost)) {
        for (std::size_t controller = 0; controller < state.players.size();
             ++controller) {
            actions.push_back(PriorityAction::cast_lightning_bolt(
                Target::player_target(controller)));
            for (const auto& creature : state.players[controller].creatures) {
                actions.push_back(PriorityAction::cast_lightning_bolt(
                    Target::creature_target(controller, creature.id)));
            }
        }
    }

    const auto& counterspell = card_definition(CardId::Counterspell);
    if (has_card(player_state.hand, CardId::Counterspell) &&
        can_pay(player_state, counterspell.cost)) {
        for (const auto& spell : state.stack) {
            if (spell.kind == StackObjectKind::Spell) {
                actions.push_back(
                    PriorityAction::cast_counterspell(spell.id));
            }
        }
    }

    if (can_pay(player_state, kMillstoneActivationCost)) {
        for (const auto& artifact : player_state.artifacts) {
            if (artifact.card != CardId::Millstone ||
                artifact.tapped) {
                continue;
            }
            for (std::size_t target = 0;
                 target < state.players.size(); ++target) {
                actions.push_back(PriorityAction::activate_millstone(
                    artifact.id, Target::player_target(target)));
            }
        }
    }

    return actions;
}

bool apply_priority_action(GameState& state, std::size_t player,
                           const PriorityAction& action,
                           bool sorcery_actions) {
    const auto actions =
        legal_priority_actions(state, player, sorcery_actions);
    if (!contains_action(actions, action)) {
        return false;
    }

    auto& player_state = state.players[player];
    switch (action.kind) {
    case PriorityActionKind::Pass:
        return true;

    case PriorityActionKind::PlayLand:
        if (!is_land(action.card) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        player_state.lands.push_back({.card = action.card, .tapped = false});
        player_state.land_played_this_turn = true;
        ++state.stats[player].lands_played;
        return true;

    case PriorityActionKind::CastCreature: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Creature) {
            return false;
        }
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastSorcery: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Sorcery ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastArtifact: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Artifact ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastEnchantment: {
        const auto& definition = card_definition(action.card);
        if (definition.type != CardType::Enchantment ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, action.card)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = action.card,
            .controller = player,
            .target = std::nullopt,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastLightningBolt: {
        if (!action.target.has_value()) {
            return false;
        }
        const auto& definition = card_definition(CardId::LightningBolt);
        if (!pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::LightningBolt)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::LightningBolt,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::CastCounterspell: {
        const auto& definition = card_definition(CardId::Counterspell);
        if (!action.spell_target.has_value() ||
            !pay_mana(player_state, definition.cost) ||
            !remove_card(player_state.hand, CardId::Counterspell)) {
            return false;
        }
        state.stack.push_back({
            .id = state.next_stack_object_id++,
            .card = CardId::Counterspell,
            .controller = player,
            .target = std::nullopt,
            .spell_target = action.spell_target,
        });
        ++state.stats[player].spells_cast;
        return true;
    }

    case PriorityActionKind::ActivateMillstone: {
        if (!action.source_permanent.has_value() ||
            !action.target.has_value() ||
            action.target->creature.has_value()) {
            return false;
        }
        auto* millstone =
            find_artifact(player_state, *action.source_permanent);
        if (millstone == nullptr || millstone->tapped ||
            millstone->card != CardId::Millstone ||
            !pay_mana(player_state, kMillstoneActivationCost)) {
            return false;
        }
        millstone->tapped = true;
        state.stack.push_back({
            .kind = StackObjectKind::ActivatedAbility,
            .id = state.next_stack_object_id++,
            .card = CardId::Millstone,
            .controller = player,
            .target = action.target,
            .spell_target = std::nullopt,
        });
        return true;
    }
    }

    return false;
}

bool resolve_top_of_stack(GameState& state) {
    if (state.stack.empty()) {
        return false;
    }

    const StackObject spell = state.stack.back();
    state.stack.pop_back();
    auto& controller = state.players[spell.controller];
    const auto& definition = card_definition(spell.card);

    if (spell.kind == StackObjectKind::ActivatedAbility) {
        if (spell.card != CardId::Millstone ||
            !spell.target.has_value() ||
            spell.target->creature.has_value()) {
            return false;
        }
        auto& target = state.players[spell.target->player];
        for (int card = 0; card < 2 && !target.library.empty();
             ++card) {
            target.graveyard.push_back(target.library.back());
            target.library.pop_back();
            ++state.stats[spell.controller].cards_milled;
        }
        return true;
    }

    if (definition.type == CardType::Creature) {
        controller.creatures.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false,
             .summoning_sick = true,
             .damage = 0});
        return true;
    }

    if (definition.type == CardType::Artifact) {
        controller.artifacts.push_back(
            {.id = state.next_permanent_id++,
             .card = spell.card,
             .tapped = false});
        return true;
    }

    if (definition.type == CardType::Enchantment) {
        controller.enchantments.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::LightningBolt) {
        if (spell.target.has_value()) {
            const Target& target = *spell.target;
            if (target.creature.has_value()) {
                auto* creature = find_creature(
                    state.players[target.player], *target.creature);
                if (creature != nullptr) {
                    creature->damage += definition.effect_damage;
                    remove_dead_creatures(state.players[target.player]);
                }
            } else {
                state.players[target.player].life -=
                    definition.effect_damage;
                if (target.player == opponent_of(spell.controller)) {
                    state.stats[spell.controller].damage_to_opponent +=
                        static_cast<std::size_t>(
                            definition.effect_damage);
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Counterspell) {
        if (spell.spell_target.has_value()) {
            const auto target = std::find_if(
                state.stack.begin(), state.stack.end(),
                [&](const StackObject& candidate) {
                    return candidate.id == *spell.spell_target;
                });
            if (target != state.stack.end()) {
                state.players[target->controller].graveyard.push_back(
                    target->card);
                state.stack.erase(target);
                ++state.stats[spell.controller].spells_countered;
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    if (spell.card == CardId::Tsunami) {
        for (auto& player : state.players) {
            auto land = player.lands.begin();
            while (land != player.lands.end()) {
                if (land->card == CardId::Island) {
                    player.graveyard.push_back(land->card);
                    land = player.lands.erase(land);
                } else {
                    ++land;
                }
            }
        }
        controller.graveyard.push_back(spell.card);
        return true;
    }

    return false;
}

PriorityPassResult pass_priority(GameState& state,
                                 PriorityState& priority) {
    if (priority.player >= state.players.size()) {
        throw std::out_of_range("priority player must be 0 or 1");
    }

    ++priority.consecutive_passes;
    if (priority.consecutive_passes < 2) {
        priority.player = opponent_of(priority.player);
        return PriorityPassResult::Passed;
    }

    if (state.stack.empty()) {
        return PriorityPassResult::WindowEnded;
    }
    if (!resolve_top_of_stack(state)) {
        throw std::logic_error("failed to resolve the stack");
    }

    priority.player = state.active_player;
    priority.consecutive_passes = 0;
    return PriorityPassResult::StackObjectResolved;
}

bool resolve_combat(
    GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const std::vector<std::pair<PermanentId, PermanentId>>& blocks) {
    if (attacking_player >= state.players.size()) {
        return false;
    }
    const std::size_t defending_player = opponent_of(attacking_player);

    std::unordered_set<PermanentId> attacker_ids;
    for (const PermanentId attacker_id : attackers) {
        const auto* creature =
            find_creature(state.players[attacking_player], attacker_id);
        if (creature == nullptr || creature->tapped ||
            creature->summoning_sick ||
            !can_attack_through_moat(state, *creature) ||
            !attacker_ids.insert(attacker_id).second) {
            return false;
        }
    }

    std::unordered_set<PermanentId> blocker_ids;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        if (!attacker_ids.contains(attacker_id)) {
            return false;
        }
        const auto* blocker =
            find_creature(state.players[defending_player], blocker_id);
        if (blocker == nullptr || blocker->tapped ||
            !blocker_ids.insert(blocker_id).second) {
            return false;
        }
    }

    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    for (const auto& [attacker_id, blocker_id] : blocks) {
        blockers_by_attacker[attacker_id].push_back(blocker_id);
    }

    for (const PermanentId attacker_id : attackers) {
        auto* attacker =
            find_creature(state.players[attacking_player], attacker_id);
        attacker->tapped = true;
        const auto& attacker_definition = card_definition(attacker->card);
        const auto blocker_group = blockers_by_attacker.find(attacker_id);

        if (blocker_group == blockers_by_attacker.end()) {
            state.players[defending_player].life -=
                attacker_definition.power;
            state.stats[attacking_player].damage_to_opponent +=
                static_cast<std::size_t>(attacker_definition.power);
            continue;
        }

        int attacker_damage = 0;
        for (const PermanentId blocker_id : blocker_group->second) {
            const auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            attacker_damage += card_definition(blocker->card).power;
        }
        attacker->damage += attacker_damage;

        int damage_remaining = attacker_definition.power;
        for (const PermanentId blocker_id : blocker_group->second) {
            auto* blocker =
                find_creature(state.players[defending_player], blocker_id);
            const int lethal_damage =
                std::max(0, card_definition(blocker->card).toughness -
                                blocker->damage);
            const int assigned_damage =
                std::min(damage_remaining, lethal_damage);
            blocker->damage += assigned_damage;
            damage_remaining -= assigned_damage;
        }
    }

    remove_dead_creatures(state.players[attacking_player]);
    remove_dead_creatures(state.players[defending_player]);
    return true;
}

void begin_turn(GameState& state, std::size_t player) {
    auto& player_state = state.players.at(player);
    player_state.land_played_this_turn = false;
    for (auto& land : player_state.lands) {
        land.tapped = false;
    }
    for (auto& creature : player_state.creatures) {
        creature.tapped = false;
        creature.summoning_sick = false;
    }
    for (auto& artifact : player_state.artifacts) {
        artifact.tapped = false;
    }
}

void cleanup_turn(GameState& state) {
    for (auto& player : state.players) {
        for (auto& creature : player.creatures) {
            creature.damage = 0;
        }
    }
}

Game::Game(std::vector<CardId> player_zero_deck,
           std::vector<CardId> player_one_deck, std::uint64_t seed,
           GameConfig config)
    : decks_({std::move(player_zero_deck), std::move(player_one_deck)}),
      random_(seed), config_(config) {
    if (config_.starting_player.has_value() &&
        *config_.starting_player >= state_.players.size()) {
        throw std::invalid_argument("starting player must be 0 or 1");
    }
    if (config_.max_turns == 0) {
        throw std::invalid_argument("maximum turns must be positive");
    }
    for (const auto& bot : config_.bots) {
        if (bot.kind != BotKind::Random &&
            bot.rollouts_per_action == 0) {
            throw std::invalid_argument(
                "Monte Carlo rollouts per action must be positive");
        }
    }
}

void Game::initialize() {
    state_ = GameState{};
    setup_result_.reset();

    if (config_.starting_player.has_value()) {
        state_.starting_player = *config_.starting_player;
    } else {
        std::uniform_int_distribution<std::size_t> choose_player(0, 1);
        state_.starting_player = choose_player(random_);
    }

    for (std::size_t player = 0; player < state_.players.size(); ++player) {
        state_.players[player].library = decks_[player];
        std::shuffle(state_.players[player].library.begin(),
                     state_.players[player].library.end(), random_);
    }

    for (int card = 0; card < 7; ++card) {
        for (std::size_t player = 0; player < state_.players.size(); ++player) {
            if (!draw_card(player)) {
                setup_result_ =
                    make_result(static_cast<int>(opponent_of(player)),
                                EndReason::EmptyLibrary);
                return;
            }
        }
    }
}

bool Game::draw_card(std::size_t player) {
    auto& player_state = state_.players[player];
    if (player_state.library.empty()) {
        return false;
    }
    player_state.hand.push_back(player_state.library.back());
    player_state.library.pop_back();
    ++state_.stats[player].cards_drawn;
    return true;
}

GameResult Game::make_result(int winner, EndReason reason) const {
    return {
        .winner = winner,
        .reason = reason,
        .turns = state_.turn_number,
        .starting_player = state_.starting_player,
        .ending_life = {
            state_.players[0].life,
            state_.players[1].life,
        },
        .player_stats = state_.stats,
        .bots = {
            config_.bots[0].kind,
            config_.bots[1].kind,
        },
    };
}

std::optional<GameResult> Game::life_total_result() const {
    const bool player_zero_lost = state_.players[0].life <= 0;
    const bool player_one_lost = state_.players[1].life <= 0;
    if (!player_zero_lost && !player_one_lost) {
        return std::nullopt;
    }

    int winner = -1;
    if (player_zero_lost != player_one_lost) {
        winner = player_zero_lost ? 1 : 0;
    }
    return make_result(winner, EndReason::LifeTotal);
}

std::optional<GameResult>
Game::play_priority_window(bool sorcery_actions) {
    PriorityState priority = {
        .player = state_.active_player,
        .consecutive_passes = 0,
    };

    while (true) {
        const auto actions =
            legal_priority_actions(state_, priority.player,
                                   sorcery_actions);
        const PriorityAction action =
            choose_priority_action(actions, priority.player,
                                   sorcery_actions);

        if (action.kind == PriorityActionKind::Pass) {
            const PriorityPassResult pass =
                pass_priority(state_, priority);
            if (pass == PriorityPassResult::Passed) {
                continue;
            }
            if (pass == PriorityPassResult::WindowEnded) {
                return std::nullopt;
            }
            if (const auto result = life_total_result();
                result.has_value()) {
                return result;
            }
            continue;
        }

        if (!apply_priority_action(state_, priority.player, action,
                                   sorcery_actions)) {
            throw std::logic_error("bot policy selected an illegal action");
        }
        // The player who acted receives priority again.
        priority.consecutive_passes = 0;
    }
}

PriorityAction Game::choose_priority_action(
    const std::vector<PriorityAction>& actions, std::size_t player,
    bool sorcery_actions) {
    if (actions.empty()) {
        throw std::logic_error("priority window has no pass action");
    }
    if (actions.size() == 1) {
        return actions.front();
    }

    ++state_.stats[player].decisions;
    const auto& bot = config_.bots[player];
    if (bot.kind == BotKind::Random) {
        std::uniform_int_distribution<std::size_t> choose_action(
            0, actions.size() - 1);
        return actions[choose_action(random_)];
    }

    std::vector<double> scores(actions.size(), 0.0);
    for (std::size_t action_index = 0; action_index < actions.size();
         ++action_index) {
        for (std::size_t rollout = 0;
             rollout < bot.rollouts_per_action; ++rollout) {
            scores[action_index] +=
                rollout_action(actions[action_index], player,
                               sorcery_actions, random_());
        }
    }
    state_.stats[player].monte_carlo_rollouts +=
        actions.size() * bot.rollouts_per_action;

    const double best_score =
        *std::max_element(scores.begin(), scores.end());
    std::vector<std::size_t> best_actions;
    for (std::size_t action_index = 0; action_index < scores.size();
         ++action_index) {
        if (scores[action_index] == best_score) {
            best_actions.push_back(action_index);
        }
    }
    std::uniform_int_distribution<std::size_t> break_tie(
        0, best_actions.size() - 1);
    return actions[best_actions[break_tie(random_)]];
}

double Game::rollout_action(const PriorityAction& action,
                            std::size_t player, bool sorcery_actions,
                            std::uint64_t seed) const {
    Game rollout = *this;
    rollout.random_.seed(seed);
    rollout.config_.bots = {
        BotConfig{.kind = BotKind::Random},
        BotConfig{.kind = BotKind::Random},
    };

    // The order of each library is hidden information. Re-randomizing it makes
    // each rollout a separate determinization instead of letting the bot peek
    // at the already-shuffled future.
    for (auto& player_state : rollout.state_.players) {
        std::shuffle(player_state.library.begin(),
                     player_state.library.end(), rollout.random_);
    }

    if (action.kind != PriorityActionKind::Pass &&
        !apply_priority_action(rollout.state_, player, action,
                               sorcery_actions)) {
        return -std::numeric_limits<double>::infinity();
    }

    // Resolve the candidate and anything already below it, then use a complete
    // random continuation from the following turn. This keeps rollout cost
    // bounded while the real game still uses normal stack priority.
    while (!rollout.state_.stack.empty()) {
        if (!resolve_top_of_stack(rollout.state_)) {
            throw std::logic_error("rollout failed to resolve the stack");
        }
        if (const auto result = rollout.life_total_result();
            result.has_value()) {
            if (result->winner < 0) {
                return 0.5;
            }
            return result->winner == static_cast<int>(player) ? 1.0 : 0.0;
        }
    }

    cleanup_turn(rollout.state_);
    const GameResult result =
        rollout.run_from_turn(rollout.state_.turn_number + 1);
    if (result.winner < 0) {
        return 0.5;
    }
    return result.winner == static_cast<int>(player) ? 1.0 : 0.0;
}

std::optional<GameResult> Game::play_random_combat() {
    if (const auto result = play_priority_window(false);
        result.has_value()) {
        return result;
    }

    auto& attacking_state = state_.players[state_.active_player];
    const std::size_t defending_player = opponent_of(state_.active_player);
    auto& defending_state = state_.players[defending_player];

    std::vector<PermanentId> attackers;
    std::uniform_int_distribution<int> attack_or_not(0, 1);
    for (const auto& creature : attacking_state.creatures) {
        if (!creature.tapped && !creature.summoning_sick &&
            can_attack_through_moat(state_, creature) &&
            attack_or_not(random_) == 1) {
            attackers.push_back(creature.id);
        }
    }

    if (attackers.empty()) {
        return play_priority_window(false);
    }

    std::vector<PermanentId> available_blockers;
    for (const auto& creature : defending_state.creatures) {
        if (!creature.tapped) {
            available_blockers.push_back(creature.id);
        }
    }
    std::shuffle(available_blockers.begin(), available_blockers.end(),
                 random_);

    std::unordered_map<PermanentId, std::vector<PermanentId>>
        blockers_by_attacker;
    for (const PermanentId blocker : available_blockers) {
        // Zero means no block; other values select an attacker. Multiple
        // blockers may legally select the same attacker.
        std::uniform_int_distribution<std::size_t> choose_block(
            0, attackers.size());
        const std::size_t choice = choose_block(random_);
        if (choice != 0) {
            blockers_by_attacker[attackers[choice - 1]].push_back(blocker);
        }
    }

    std::vector<std::pair<PermanentId, PermanentId>> blocks;
    for (const PermanentId attacker : attackers) {
        auto& blockers = blockers_by_attacker[attacker];
        // The attacking player chooses damage assignment order.
        std::shuffle(blockers.begin(), blockers.end(), random_);
        for (const PermanentId blocker : blockers) {
            blocks.emplace_back(attacker, blocker);
        }
    }

    if (!resolve_combat(state_, state_.active_player, attackers, blocks)) {
        throw std::logic_error("random policy declared illegal combat");
    }
    if (const auto result = life_total_result(); result.has_value()) {
        return result;
    }
    return play_priority_window(false);
}

GameResult Game::run() {
    initialize();
    if (setup_result_.has_value()) {
        return *setup_result_;
    }
    return run_from_turn(1);
}

GameResult Game::run_from_turn(std::size_t first_turn) {
    for (std::size_t turn = first_turn; turn <= config_.max_turns;
         ++turn) {
        state_.turn_number = turn;
        state_.active_player = (state_.starting_player + turn - 1) % 2;
        begin_turn(state_, state_.active_player);

        const bool starting_player_first_turn =
            turn == 1 && state_.active_player == state_.starting_player;
        if (!starting_player_first_turn &&
            !draw_card(state_.active_player)) {
            return make_result(
                static_cast<int>(opponent_of(state_.active_player)),
                EndReason::EmptyLibrary);
        }

        if (const auto result = play_priority_window(true);
            result.has_value()) {
            return *result;
        }
        if (const auto result = play_random_combat(); result.has_value()) {
            return *result;
        }
        if (const auto result = play_priority_window(true);
            result.has_value()) {
            return *result;
        }
        cleanup_turn(state_);
    }

    return make_result(-1, EndReason::TurnLimit);
}

const GameState& Game::state() const {
    return state_;
}

double SimulationSummary::average_turns() const {
    return games == 0 ? 0.0
                      : static_cast<double>(total_turns) /
                            static_cast<double>(games);
}

namespace {

double percentage(std::size_t numerator, std::size_t denominator) {
    return denominator == 0
               ? 0.0
               : 100.0 * static_cast<double>(numerator) /
                     static_cast<double>(denominator);
}

double average(std::int64_t total, std::size_t count) {
    return count == 0
               ? 0.0
               : static_cast<double>(total) / static_cast<double>(count);
}

} // namespace

double DeckSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double DeckSimulationStats::on_play_win_rate() const {
    return percentage(on_play_wins, on_play_games);
}

double DeckSimulationStats::on_draw_win_rate() const {
    return percentage(on_draw_wins, on_draw_games);
}

double DeckSimulationStats::average_ending_life() const {
    return average(total_ending_life, games);
}

double DeckSimulationStats::average_cards_drawn() const {
    return average(static_cast<std::int64_t>(total_cards_drawn), games);
}

double DeckSimulationStats::average_lands_played() const {
    return average(static_cast<std::int64_t>(total_lands_played), games);
}

double DeckSimulationStats::average_spells_cast() const {
    return average(static_cast<std::int64_t>(total_spells_cast), games);
}

double DeckSimulationStats::average_spells_countered() const {
    return average(static_cast<std::int64_t>(total_spells_countered),
                   games);
}

double DeckSimulationStats::average_damage_to_opponent() const {
    return average(static_cast<std::int64_t>(total_damage_to_opponent),
                   games);
}

double DeckSimulationStats::average_cards_milled() const {
    return average(static_cast<std::int64_t>(total_cards_milled), games);
}

double BotSimulationStats::win_rate() const {
    return percentage(wins, games);
}

double BotSimulationStats::average_decisions() const {
    return average(static_cast<std::int64_t>(total_decisions), games);
}

double BotSimulationStats::average_rollouts() const {
    return average(static_cast<std::int64_t>(total_rollouts), games);
}

double BotSimulationStats::average_rollouts_per_decision() const {
    return average(static_cast<std::int64_t>(total_rollouts),
                   total_decisions);
}

double BotMatchupStats::first_win_rate() const {
    return percentage(first_wins, games);
}

double BotMatchupStats::second_win_rate() const {
    return percentage(second_wins, games);
}

namespace {

std::array<BotMatchupStats, 3> empty_bot_matchups() {
    return {{
        {
            .first_bot = BotKind::Random,
            .second_bot = BotKind::MonteCarlo,
        },
        {
            .first_bot = BotKind::Random,
            .second_bot = BotKind::DeepMonteCarlo,
        },
        {
            .first_bot = BotKind::MonteCarlo,
            .second_bot = BotKind::DeepMonteCarlo,
        },
    }};
}

std::size_t bot_matchup_index(BotKind first, BotKind second) {
    const auto low = std::min(static_cast<std::size_t>(first),
                              static_cast<std::size_t>(second));
    const auto high = std::max(static_cast<std::size_t>(first),
                               static_cast<std::size_t>(second));
    if (low == static_cast<std::size_t>(BotKind::Random) &&
        high == static_cast<std::size_t>(BotKind::MonteCarlo)) {
        return 0;
    }
    if (low == static_cast<std::size_t>(BotKind::Random) &&
        high == static_cast<std::size_t>(BotKind::DeepMonteCarlo)) {
        return 1;
    }
    if (low == static_cast<std::size_t>(BotKind::MonteCarlo) &&
        high == static_cast<std::size_t>(BotKind::DeepMonteCarlo)) {
        return 2;
    }
    throw std::logic_error("bot matchup requires two different bots");
}

void configure_bots(GameConfig& game_config, std::size_t game_index,
                    const TournamentConfig& tournament_config) {
    const BotConfig random = {
        .kind = BotKind::Random,
        .rollouts_per_action =
            tournament_config.monte_carlo_rollouts,
    };
    const BotConfig monte_carlo = {
        .kind = BotKind::MonteCarlo,
        .rollouts_per_action =
            tournament_config.monte_carlo_rollouts,
    };
    const BotConfig deep_monte_carlo = {
        .kind = BotKind::DeepMonteCarlo,
        .rollouts_per_action =
            tournament_config.deep_monte_carlo_rollouts,
    };

    switch (tournament_config.bot_field) {
    case BotField::Random:
        game_config.bots = {random, random};
        break;
    case BotField::MonteCarlo:
        game_config.bots = {monte_carlo, monte_carlo};
        break;
    case BotField::DeepMonteCarlo:
        game_config.bots = {deep_monte_carlo, deep_monte_carlo};
        break;
    case BotField::Mixed:
        // A nine-game rotation covers every ordered cross-policy pairing and
        // every same-policy pairing. Each bot occupies six of the 18 seats.
        switch (game_index % 9) {
        case 0:
            game_config.bots = {random, random};
            break;
        case 1:
            game_config.bots = {monte_carlo, random};
            break;
        case 2:
            game_config.bots = {random, monte_carlo};
            break;
        case 3:
            game_config.bots = {deep_monte_carlo, random};
            break;
        case 4:
            game_config.bots = {random, deep_monte_carlo};
            break;
        case 5:
            game_config.bots = {monte_carlo, monte_carlo};
            break;
        case 6:
            game_config.bots = {deep_monte_carlo, monte_carlo};
            break;
        case 7:
            game_config.bots = {monte_carlo, deep_monte_carlo};
            break;
        default:
            game_config.bots = {
                deep_monte_carlo,
                deep_monte_carlo,
            };
            break;
        }
        break;
    }
}

void record_deck_result(DeckSimulationStats& deck,
                        const GameResult& result,
                        std::size_t player) {
    ++deck.games;
    if (result.winner < 0) {
        ++deck.draws;
    } else if (result.winner == static_cast<int>(player)) {
        ++deck.wins;
    } else {
        ++deck.losses;
    }

    if (result.starting_player == player) {
        ++deck.on_play_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_play_wins;
        }
    } else {
        ++deck.on_draw_games;
        if (result.winner == static_cast<int>(player)) {
            ++deck.on_draw_wins;
        }
    }

    deck.total_ending_life += result.ending_life[player];
    deck.total_cards_drawn += result.player_stats[player].cards_drawn;
    deck.total_lands_played += result.player_stats[player].lands_played;
    deck.total_spells_cast += result.player_stats[player].spells_cast;
    deck.total_spells_countered +=
        result.player_stats[player].spells_countered;
    deck.total_damage_to_opponent +=
        result.player_stats[player].damage_to_opponent;
    deck.total_cards_milled += result.player_stats[player].cards_milled;
}

SimulationSummary run_matchup(const std::vector<CardId>& first_deck,
                              const std::vector<CardId>& second_deck,
                              std::size_t games, std::uint64_t seed,
                              GameConfig game_config,
                              std::optional<TournamentConfig>
                                  tournament_config = std::nullopt,
                              std::size_t schedule_offset = 0) {
    SimulationSummary summary;
    summary.games = games;
    summary.bot_matchups = empty_bot_matchups();
    std::mt19937_64 seed_generator(seed);

    for (std::size_t game_index = 0; game_index < games; ++game_index) {
        GameConfig current_game_config = game_config;
        if (tournament_config.has_value()) {
            configure_bots(current_game_config,
                           schedule_offset + game_index,
                           *tournament_config);
        }
        Game game(first_deck, second_deck, seed_generator(),
                  current_game_config);
        const GameResult result = game.run();
        summary.total_turns += result.turns;

        for (std::size_t player = 0; player < summary.decks.size();
             ++player) {
            auto& deck = summary.decks[player];
            const auto bot_index =
                static_cast<std::size_t>(result.bots[player]);
            record_deck_result(deck, result, player);
            record_deck_result(
                summary.deck_bots[player][bot_index], result, player);

            auto& bot = summary.bots[bot_index];
            ++bot.games;
            if (result.winner < 0) {
                ++bot.draws;
            } else if (result.winner == static_cast<int>(player)) {
                ++bot.wins;
            } else {
                ++bot.losses;
            }
            bot.total_decisions +=
                result.player_stats[player].decisions;
            bot.total_rollouts +=
                result.player_stats[player].monte_carlo_rollouts;
        }

        if (result.bots[0] != result.bots[1]) {
            auto& matchup = summary.bot_matchups[bot_matchup_index(
                result.bots[0], result.bots[1])];
            ++matchup.games;
            if (result.winner < 0) {
                ++matchup.draws;
            } else if (result.bots[static_cast<std::size_t>(
                           result.winner)] == matchup.first_bot) {
                ++matchup.first_wins;
            } else {
                ++matchup.second_wins;
            }
        }

        if (result.winner < 0) {
            ++summary.draws;
        }

        switch (result.reason) {
        case EndReason::LifeTotal:
            ++summary.life_total_finishes;
            break;
        case EndReason::EmptyLibrary:
            ++summary.empty_library_finishes;
            break;
        case EndReason::TurnLimit:
            ++summary.turn_limit_draws;
            break;
        }
    }

    return summary;
}

std::vector<CardId> deck_cards(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return green_alpha_deck();
    case DeckId::Red:
        return red_alpha_deck();
    case DeckId::Blue:
        return blue_alpha_deck();
    case DeckId::White:
        return white_control_deck();
    }
    throw std::out_of_range("unknown deck ID");
}

void merge_deck_stats(DeckSimulationStats& destination,
                      const DeckSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
    destination.on_play_games += source.on_play_games;
    destination.on_play_wins += source.on_play_wins;
    destination.on_draw_games += source.on_draw_games;
    destination.on_draw_wins += source.on_draw_wins;
    destination.total_ending_life += source.total_ending_life;
    destination.total_cards_drawn += source.total_cards_drawn;
    destination.total_lands_played += source.total_lands_played;
    destination.total_spells_cast += source.total_spells_cast;
    destination.total_spells_countered +=
        source.total_spells_countered;
    destination.total_damage_to_opponent +=
        source.total_damage_to_opponent;
    destination.total_cards_milled += source.total_cards_milled;
}

void merge_bot_stats(BotSimulationStats& destination,
                     const BotSimulationStats& source) {
    destination.games += source.games;
    destination.wins += source.wins;
    destination.losses += source.losses;
    destination.draws += source.draws;
    destination.total_decisions += source.total_decisions;
    destination.total_rollouts += source.total_rollouts;
}

void merge_bot_matchup_stats(BotMatchupStats& destination,
                             const BotMatchupStats& source) {
    if (destination.first_bot != source.first_bot ||
        destination.second_bot != source.second_bot) {
        throw std::logic_error("cannot merge different bot matchups");
    }
    destination.games += source.games;
    destination.first_wins += source.first_wins;
    destination.second_wins += source.second_wins;
    destination.draws += source.draws;
}

} // namespace

SimulationSummary run_simulation(std::size_t games, std::uint64_t seed,
                                 GameConfig game_config) {
    return run_matchup(green_alpha_deck(), red_alpha_deck(), games, seed,
                       game_config);
}

std::string_view deck_name(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "Green";
    case DeckId::Red:
        return "Red";
    case DeckId::Blue:
        return "Blue";
    case DeckId::White:
        return "White";
    }
    return "Unknown";
}

std::string_view deck_list(DeckId deck) {
    switch (deck) {
    case DeckId::Green:
        return "18 Forest / 9 Grizzly Bears / 12 Ironroot Treefolk / "
               "1 Tsunami";
    case DeckId::Red:
        return "18 Mountain / 10 Lightning Bolt / 12 Fire Elemental";
    case DeckId::Blue:
        return "18 Island / 14 Counterspell / 8 Water Elemental";
    case DeckId::White:
        return "22 Plains / 3 Millstone / 15 Moat";
    }
    return "Unknown";
}

std::string_view bot_name(BotKind bot) {
    switch (bot) {
    case BotKind::Random:
        return "Random";
    case BotKind::MonteCarlo:
        return "Monte Carlo";
    case BotKind::DeepMonteCarlo:
        return "Deep Monte Carlo";
    }
    return "Unknown";
}

double TournamentSummary::average_turns() const {
    return total_games == 0
               ? 0.0
               : static_cast<double>(total_turns) /
                     static_cast<double>(total_games);
}

TournamentSummary run_tournament(std::size_t games_per_matchup,
                                 std::uint64_t seed,
                                 GameConfig game_config,
                                 TournamentConfig tournament_config) {
    const bool uses_monte_carlo =
        tournament_config.bot_field == BotField::MonteCarlo ||
        tournament_config.bot_field == BotField::Mixed;
    const bool uses_deep_monte_carlo =
        tournament_config.bot_field == BotField::DeepMonteCarlo ||
        tournament_config.bot_field == BotField::Mixed;
    if (uses_monte_carlo &&
        tournament_config.monte_carlo_rollouts == 0) {
        throw std::invalid_argument(
            "Monte Carlo rollouts per action must be positive");
    }
    if (uses_deep_monte_carlo &&
        tournament_config.deep_monte_carlo_rollouts == 0) {
        throw std::invalid_argument(
            "deep Monte Carlo rollouts per action must be positive");
    }
    if (tournament_config.bot_field == BotField::Mixed &&
        tournament_config.deep_monte_carlo_rollouts <=
            tournament_config.monte_carlo_rollouts) {
        throw std::invalid_argument(
            "deep Monte Carlo must use more rollouts than Monte Carlo");
    }

    TournamentSummary summary;
    summary.games_per_matchup = games_per_matchup;
    summary.total_games = games_per_matchup * summary.matchups.size();
    summary.bot_matchups = empty_bot_matchups();

    constexpr std::array<std::pair<DeckId, DeckId>, 6> pairings = {{
        {DeckId::Green, DeckId::Red},
        {DeckId::Green, DeckId::Blue},
        {DeckId::Green, DeckId::White},
        {DeckId::Red, DeckId::Blue},
        {DeckId::Red, DeckId::White},
        {DeckId::Blue, DeckId::White},
    }};
    std::mt19937_64 seed_generator(seed);

    for (std::size_t index = 0; index < pairings.size(); ++index) {
        const auto [first, second] = pairings[index];
        SimulationSummary matchup =
            run_matchup(deck_cards(first), deck_cards(second),
                        games_per_matchup, seed_generator(), game_config,
                        tournament_config,
                        index * games_per_matchup);
        summary.matchups[index] = {
            .first_deck = first,
            .second_deck = second,
            .result = matchup,
        };

        merge_deck_stats(
            summary.decks[static_cast<std::size_t>(first)],
            matchup.decks[0]);
        merge_deck_stats(
            summary.decks[static_cast<std::size_t>(second)],
            matchup.decks[1]);
        for (std::size_t bot = 0; bot < summary.bots.size(); ++bot) {
            merge_bot_stats(summary.bots[bot], matchup.bots[bot]);
            merge_deck_stats(
                summary
                    .deck_bots[static_cast<std::size_t>(first)][bot],
                matchup.deck_bots[0][bot]);
            merge_deck_stats(
                summary
                    .deck_bots[static_cast<std::size_t>(second)][bot],
                matchup.deck_bots[1][bot]);
        }
        for (std::size_t bot_matchup = 0;
             bot_matchup < summary.bot_matchups.size();
             ++bot_matchup) {
            merge_bot_matchup_stats(
                summary.bot_matchups[bot_matchup],
                matchup.bot_matchups[bot_matchup]);
        }
        summary.draws += matchup.draws;
        summary.life_total_finishes += matchup.life_total_finishes;
        summary.empty_library_finishes +=
            matchup.empty_library_finishes;
        summary.turn_limit_draws += matchup.turn_limit_draws;
        summary.total_turns += matchup.total_turns;
    }

    return summary;
}

} // namespace alpha
