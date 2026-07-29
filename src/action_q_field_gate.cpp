#include "old_school/action_q_field_gate.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::action_q_field_gate {
namespace {

constexpr std::size_t kPlayerCount = 2;

void remove_one(
    std::vector<CardId>& cards, CardId wanted,
    std::string_view zone_description) {
    const auto found = std::find(
        cards.begin(), cards.end(), wanted);
    if (found == cards.end()) {
        throw std::logic_error(
            "Ancestral field root cannot remove " +
            std::string(card_definition(wanted).name) +
            " for " + std::string(zone_description));
    }
    cards.erase(found);
}

void remove_cards(
    std::vector<CardId>& cards,
    const std::vector<CardId>& removed,
    std::string_view zone_description) {
    for (const CardId card : removed) {
        remove_one(cards, card, zone_description);
    }
}

void remove_public_permanents(
    std::vector<CardId>& cards,
    const PlayerState& player) {
    for (const LandPermanent& land : player.lands) {
        remove_one(cards, land.card, "land");
    }
    for (const CreaturePermanent& creature :
         player.creatures) {
        remove_one(cards, creature.card, "creature");
    }
    for (const ArtifactPermanent& artifact :
         player.artifacts) {
        remove_one(cards, artifact.card, "artifact");
    }
    remove_cards(cards, player.enchantments, "enchantment");
    remove_cards(cards, player.graveyard, "graveyard");
    remove_cards(cards, player.exile, "exile");
}

std::size_t unique_action_index(
    const std::vector<PriorityAction>& actions,
    const PriorityAction& wanted,
    std::string_view description) {
    const auto first = std::find(
        actions.begin(), actions.end(), wanted);
    if (first == actions.end() ||
        std::find(first + 1, actions.end(), wanted) !=
            actions.end()) {
        throw std::logic_error(
            "Ancestral field root requires one " +
            std::string(description) + " action");
    }
    return static_cast<std::size_t>(
        std::distance(actions.begin(), first));
}

bool index_matches(
    const AncestralFieldRoot& root, std::size_t index,
    const PriorityAction& wanted) {
    return index < root.legal_actions.size() &&
           root.legal_actions[index] == wanted;
}

void validate_root(const AncestralFieldRoot& root) {
    if (root.actor >= kPlayerCount ||
        !root.context.valid ||
        root.context.decision_player != root.actor ||
        root.context.phase != TurnPhase::FirstMain ||
        !root.context.sorcery_actions ||
        root.context.consecutive_passes != 0 ||
        root.state.active_player != root.actor ||
        root.state.turn_number != kCaptureTurn ||
        root.original_decks[0] != blue_deck() ||
        root.original_decks[1] != red_deck()) {
        throw std::logic_error(
            "Ancestral field root metadata drifted");
    }

    const auto actual = legal_priority_actions(
        root.state, root.actor,
        root.context.sorcery_actions);
    if (actual != root.legal_actions ||
        !has_required_action_identities(root)) {
        throw std::logic_error(
            "Ancestral field root legal actions drifted");
    }

    // Determinization is also a strict card-conservation check. It must be
    // possible without consulting the canonical hidden identities below.
    static_cast<void>(sample_determinization(
        root.state, root.original_decks, root.actor,
        kReferenceSearchSeed));
}

} // namespace

AncestralFieldRoot make_ancestral_field_root() {
    AncestralFieldRoot root;
    root.original_decks = {blue_deck(), red_deck()};
    root.actor = 0;
    root.context = {
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };

    GameState& state = root.state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = kCaptureTurn;
    state.next_permanent_id = 5;
    state.next_stack_object_id = 12;

    PlayerState& blue = state.players[0];
    blue.life = 15;
    // These are the exact engine-observed cards. The EXPLORE-7 prose called
    // the third card Time Walk, but the source trace and later EXPLORE-8 gate
    // both identify it as Sol Ring (CardId 20).
    blue.hand = {
        CardId::Island,
        CardId::SolRing,
        CardId::AncestralRecall,
    };
    blue.graveyard = {
        CardId::ForceSpike,
        CardId::ForceSpike,
        CardId::Counterspell,
    };
    blue.lands.assign(
        4, LandPermanent{
               .card = CardId::Island,
               .tapped = false,
           });
    blue.creatures = {
        CreaturePermanent{
            .id = 2,
            .card = CardId::FlyingMen,
            .tapped = false,
            .summoning_sick = false,
        },
        CreaturePermanent{
            .id = 3,
            .card = CardId::AirElemental,
            .tapped = false,
            .summoning_sick = false,
        },
    };
    blue.artifacts = {
        ArtifactPermanent{
            .id = 1,
            .card = CardId::MoxSapphire,
            .tapped = false,
        },
    };
    blue.library = root.original_decks[0];
    remove_cards(blue.library, blue.hand, "known Blue hand");
    remove_public_permanents(blue.library, blue);

    PlayerState& red = state.players[1];
    red.life = 4;
    red.graveyard.assign(4, CardId::LightningBolt);
    red.lands.assign(
        3, LandPermanent{
               .card = CardId::Mountain,
               .tapped = false,
           });
    red.creatures = {
        CreaturePermanent{
            .id = 4,
            .card = CardId::IronclawOrcs,
            .tapped = false,
            .summoning_sick = false,
        },
    };

    std::vector<CardId> red_hidden = root.original_decks[1];
    remove_public_permanents(red_hidden, red);
    constexpr std::size_t kObservedRedHandSize = 5;
    if (red_hidden.size() !=
        kObservedRedHandSize + 27) {
        throw std::logic_error(
            "Ancestral field root Red hidden-zone census drifted");
    }
    const auto red_hand_end =
        red_hidden.begin() +
        static_cast<std::ptrdiff_t>(kObservedRedHandSize);
    red.hand.assign(red_hidden.begin(), red_hand_end);
    red.library.assign(red_hand_end, red_hidden.end());

    root.legal_actions = legal_priority_actions(
        state, root.actor, root.context.sorcery_actions);
    root.pass_index = unique_action_index(
        root.legal_actions, PriorityAction::pass(), "Pass");
    root.self_target_index = unique_action_index(
        root.legal_actions,
        PriorityAction::cast_ancestral_recall(
            Target::player_target(root.actor)),
        "self-target Ancestral Recall");
    root.opponent_target_index = unique_action_index(
        root.legal_actions,
        PriorityAction::cast_ancestral_recall(
            Target::player_target(1 - root.actor)),
        "opponent-target Ancestral Recall");

    validate_root(root);
    return root;
}

AncestralFieldRoot hidden_repartition_clone(
    const AncestralFieldRoot& root) {
    validate_root(root);
    AncestralFieldRoot clone = root;

    std::reverse(
        clone.state.players[root.actor].library.begin(),
        clone.state.players[root.actor].library.end());

    PlayerState& opponent =
        clone.state.players[1 - root.actor];
    const std::size_t hand_size = opponent.hand.size();
    std::vector<CardId> hidden = opponent.hand;
    hidden.insert(
        hidden.end(), opponent.library.begin(),
        opponent.library.end());
    if (hidden.size() > 1) {
        std::rotate(
            hidden.begin(), hidden.begin() + 1, hidden.end());
        std::reverse(hidden.begin(), hidden.end());
    }
    const auto hand_end =
        hidden.begin() +
        static_cast<std::ptrdiff_t>(hand_size);
    opponent.hand.assign(hidden.begin(), hand_end);
    opponent.library.assign(hand_end, hidden.end());

    validate_root(clone);
    return clone;
}

bool has_required_action_identities(
    const AncestralFieldRoot& root) {
    if (root.actor >= kPlayerCount) {
        return false;
    }
    return index_matches(
               root, root.pass_index,
               PriorityAction::pass()) &&
           index_matches(
               root, root.self_target_index,
               PriorityAction::cast_ancestral_recall(
                   Target::player_target(root.actor))) &&
           index_matches(
               root, root.opponent_target_index,
               PriorityAction::cast_ancestral_recall(
                   Target::player_target(1 - root.actor))) &&
           root.pass_index != root.self_target_index &&
           root.pass_index != root.opponent_target_index &&
           root.self_target_index !=
               root.opponent_target_index;
}

LearnedValuePriorityDiagnostic score_learned_value(
    const AncestralFieldRoot& root,
    std::shared_ptr<const LearnedModel> model,
    LearnedValueScoringRecipe recipe) {
    validate_root(root);
    if (!model) {
        throw std::invalid_argument(
            "Ancestral field-root scoring requires a model");
    }
    return diagnose_learned_value_priority(
        root.state, root.original_decks, root.actor,
        root.context.sorcery_actions, root.context.phase,
        root.context.consecutive_passes, std::move(model),
        recipe.worlds, recipe.seed,
        recipe.continuation_epsilon,
        recipe.priority_residual_weight,
        recipe.pass_dominance,
        recipe.continuation_controller,
        recipe.resolved_shallow_prior_weight);
}

} // namespace old_school::action_q_field_gate
