#include "old_school/fq0_dominance_transition.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::fq0_dominance_transition {
namespace {

std::optional<int> terminal_winner(const GameState& state) {
    const bool player_zero_lost =
        state.players[0].life <= 0 || state.failed_draw[0];
    const bool player_one_lost =
        state.players[1].life <= 0 || state.failed_draw[1];
    if (!player_zero_lost && !player_one_lost) {
        return std::nullopt;
    }
    if (player_zero_lost == player_one_lost) {
        return -1;
    }
    return player_zero_lost ? 1 : 0;
}

bool phase_has_future_combat(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::BeginCombat;
}

bool creature_choice_state_changed(
    const GameState& root, const GameState& settled) {
    for (std::size_t player = 0; player < root.players.size();
         ++player) {
        for (const CreaturePermanent& after :
             settled.players[player].creatures) {
            const auto before = std::find_if(
                root.players[player].creatures.begin(),
                root.players[player].creatures.end(),
                [&](const CreaturePermanent& candidate) {
                    return candidate.id == after.id;
                });
            if (before == root.players[player].creatures.end()) {
                continue;
            }
            if (std::tie(
                    before->card, before->tapped,
                    before->summoning_sick, before->damage,
                    before->temporary_power_bonus,
                    before->temporary_toughness_bonus,
                    before->exile_on_death_this_turn) !=
                std::tie(
                    after.card, after.tapped,
                    after.summoning_sick, after.damage,
                    after.temporary_power_bonus,
                    after.temporary_toughness_bonus,
                    after.exile_on_death_this_turn)) {
                return true;
            }
        }
    }
    return false;
}

void pass_empty_priority_window(GameState& state) {
    if (!state.stack.empty() ||
        state.active_player >= state.players.size()) {
        throw std::invalid_argument(
            "neutral FQ0 window requires an empty stack and active player");
    }
    PriorityState priority{
        .player = state.active_player,
        .consecutive_passes = 0,
    };
    if (pass_priority(state, priority) !=
            PriorityPassResult::Passed ||
        pass_priority(state, priority) !=
            PriorityPassResult::WindowEnded ||
        !state.stack.empty()) {
        throw std::logic_error(
            "neutral FQ0 empty Priority window did not close exactly");
    }
}

std::vector<std::size_t> canonical_cleanup_discards(
    const PlayerState& player) {
    const std::size_t excess =
        player.hand.size() > kMaximumHandSize
            ? player.hand.size() - kMaximumHandSize
            : 0;
    std::vector<std::pair<CardId, std::size_t>> ordered;
    ordered.reserve(player.hand.size());
    for (std::size_t index = 0; index < player.hand.size();
         ++index) {
        ordered.emplace_back(player.hand[index], index);
    }
    std::sort(
        ordered.begin(), ordered.end(),
        [](const auto& first, const auto& second) {
            return std::tie(first.first, first.second) <
                   std::tie(second.first, second.second);
        });
    std::vector<std::size_t> selected;
    selected.reserve(excess);
    for (std::size_t index = 0; index < excess; ++index) {
        selected.push_back(ordered[index].second);
    }
    std::sort(selected.begin(), selected.end());
    return selected;
}

fq0_dominance::PlayerResourceCost convert_cost(
    const probes::Dc1PlayerResourceCost& source) {
    fq0_dominance::PlayerResourceCost result{
        .hand_cards_consumed =
            source.hand_cards_consumed,
        .mana_depleted = source.mana_depleted,
        .land_play_entitlement_consumed =
            source.land_play_entitlement_consumed,
    };
    result.preexisting_sources_newly_tapped.reserve(
        source.preexisting_sources_newly_tapped.size());
    for (const probes::Dc1ManaSource& mana_source :
         source.preexisting_sources_newly_tapped) {
        fq0_dominance::SourceKind kind;
        switch (mana_source.kind) {
        case probes::Dc1ManaSourceKind::Land:
            kind = fq0_dominance::SourceKind::Land;
            break;
        case probes::Dc1ManaSourceKind::Artifact:
            kind = fq0_dominance::SourceKind::Artifact;
            break;
        default:
            throw std::invalid_argument(
                "DC1 resource source has an invalid kind");
        }
        result.preexisting_sources_newly_tapped.push_back({
            .kind = kind,
            .key = mana_source.key,
            .card = mana_source.card,
        });
    }
    return result;
}

void finish_remaining_turn(GameState& state, TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
        pass_empty_priority_window(state);
        if (!resolve_combat(state, state.active_player, {}, {})) {
            throw std::logic_error(
                "neutral FQ0 no-attacker combat was rejected");
        }
        pass_empty_priority_window(state);
        pass_empty_priority_window(state);
        break;
    case TurnPhase::BeginCombat:
        if (!resolve_combat(state, state.active_player, {}, {})) {
            throw std::logic_error(
                "neutral FQ0 no-attacker combat was rejected");
        }
        pass_empty_priority_window(state);
        pass_empty_priority_window(state);
        break;
    case TurnPhase::EndCombat:
        pass_empty_priority_window(state);
        break;
    case TurnPhase::SecondMain:
        break;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        throw std::invalid_argument(
            "neutral FQ0 transition cannot start in a declaration phase");
    default:
        throw std::invalid_argument(
            "neutral FQ0 transition received an invalid phase");
    }

    const std::size_t active_player = state.active_player;
    const auto discards =
        canonical_cleanup_discards(state.players[active_player]);
    static_cast<void>(
        cleanup_turn(state, active_player, discards));
}

} // namespace

fq0_dominance::Settlement advance_to_next_first_main(
    const probes::DecisionProbe& probe,
    const GameState& information_set_world,
    std::size_t candidate_index,
    std::string root_information_fingerprint) {
    if (root_information_fingerprint.empty() ||
        probe.decision_kind != probes::DecisionKind::Priority ||
        probe.root_player >= information_set_world.players.size() ||
        candidate_index >= probe.candidates.size()) {
        throw std::invalid_argument(
            "invalid FQ0 dominance transition root");
    }
    const auto* action = std::get_if<PriorityAction>(
        &probe.candidates[candidate_index].action);
    if (action == nullptr) {
        throw std::invalid_argument(
            "FQ0 dominance candidate is not a Priority action");
    }
    static_cast<void>(action);

    const probes::Dc1CanonicalSettlement current =
        probes::settle_dc1_priority_candidate(
            probe, information_set_world, candidate_index);
    fq0_dominance::Settlement result{
        .root_information_fingerprint =
            std::move(root_information_fingerprint),
        .root_state = information_set_world,
        .boundary_state = current.settled_state,
        .costs = {
            convert_cost(current.resources[0]),
            convert_cost(current.resources[1]),
        },
    };

    if (const auto winner =
            terminal_winner(result.boundary_state);
        winner.has_value()) {
        result.terminal = true;
        result.terminal_winner = *winner;
        result.complete = true;
        return result;
    }
    if (!current.window_ended ||
        !result.boundary_state.stack.empty()) {
        throw std::logic_error(
            "FQ0 dominance root window did not settle");
    }

    result.unresolved_transient_choice_effect =
        phase_has_future_combat(probe.phase) &&
        creature_choice_state_changed(
            information_set_world, result.boundary_state);

    finish_remaining_turn(
        result.boundary_state, probe.phase);
    if (const auto winner =
            terminal_winner(result.boundary_state);
        winner.has_value()) {
        result.terminal = true;
        result.terminal_winner = *winner;
        result.complete = true;
        return result;
    }

    if (result.boundary_state.turn_number ==
        std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error(
            "FQ0 dominance turn number overflow");
    }
    ++result.boundary_state.turn_number;
    advance_turn_player(result.boundary_state);
    begin_turn(
        result.boundary_state,
        result.boundary_state.active_player);

    const bool starting_player_first_turn =
        result.boundary_state.turn_number == 1 &&
        result.boundary_state.active_player ==
            result.boundary_state.starting_player;
    if (!starting_player_first_turn) {
        PlayerState& active =
            result.boundary_state.players[
                result.boundary_state.active_player];
        if (active.library.empty()) {
            result.terminal = true;
            result.terminal_winner = static_cast<int>(
                1 - result.boundary_state.active_player);
            result.complete = true;
            return result;
        }
        active.hand.push_back(active.library.back());
        active.library.pop_back();
        ++result.boundary_state
              .stats[result.boundary_state.active_player]
              .cards_drawn;
    }

    result.boundary_context = {
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player =
            result.boundary_state.active_player,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    result.complete = true;
    return result;
}

} // namespace old_school::fq0_dominance_transition
