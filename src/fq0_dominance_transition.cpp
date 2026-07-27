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
    if (state.failed_draw[0] || state.failed_draw[1]) {
        if (state.failed_draw[0] == state.failed_draw[1]) {
            return -1;
        }
        return state.failed_draw[0] ? 1 : 0;
    }
    const bool player_zero_lost =
        state.players[0].life <= 0;
    const bool player_one_lost =
        state.players[1].life <= 0;
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

struct ResourceTrace {
    GameState settled_state;
    std::vector<fq0_dominance::ResourceOperation> operations;
    bool terminal = false;
    bool window_ended = false;
};

bool sorcery_actions_for(TurnPhase phase) {
    switch (phase) {
    case TurnPhase::FirstMain:
    case TurnPhase::SecondMain:
        return true;
    case TurnPhase::BeginCombat:
    case TurnPhase::EndCombat:
        return false;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        throw std::invalid_argument(
            "FQ0 resource trace cannot start in a declaration phase");
    }
    throw std::invalid_argument(
        "FQ0 resource trace received an invalid phase");
}

ResourceTrace trace_resource_operations(
    const probes::DecisionProbe& probe,
    const GameState& information_set_world,
    const PriorityAction& action) {
    ResourceTrace trace{
        .settled_state = information_set_world,
    };
    PriorityState priority{
        .player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
    };
    const auto take_pass = [&]() {
        const GameState before = trace.settled_state;
        const PriorityPassResult result =
            pass_priority(trace.settled_state, priority);
        if (result ==
            PriorityPassResult::StackObjectResolved) {
            trace.operations.push_back({
                .before = before,
                .after = trace.settled_state,
                .include_hand_and_land_costs = false,
            });
            trace.terminal =
                terminal_winner(trace.settled_state)
                    .has_value();
        } else if (
            result == PriorityPassResult::WindowEnded) {
            trace.window_ended = true;
        }
    };

    if (action.kind == PriorityActionKind::Pass) {
        take_pass();
    } else {
        const GameState before = trace.settled_state;
        if (!apply_priority_action(
                trace.settled_state, probe.root_player,
                action, sorcery_actions_for(probe.phase))) {
            throw std::invalid_argument(
                "FQ0 resource trace root action is illegal");
        }
        trace.operations.push_back({
            .before = before,
            .after = trace.settled_state,
            .include_hand_and_land_costs = true,
        });
        priority = {
            .player = probe.root_player,
            .consecutive_passes = 0,
        };
    }

    constexpr std::size_t kMaximumPassSteps = 1024;
    std::size_t steps = 0;
    while (!trace.terminal && !trace.window_ended) {
        if (++steps > kMaximumPassSteps) {
            throw std::logic_error(
                "FQ0 resource trace exceeded its pass bound");
        }
        take_pass();
    }
    return trace;
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
    const ResourceTrace resource_trace =
        trace_resource_operations(
            probe, information_set_world, *action);
    if (resource_trace.settled_state !=
            current.settled_state ||
        resource_trace.terminal != current.terminal ||
        resource_trace.window_ended !=
            current.window_ended) {
        throw std::logic_error(
            "FQ0 resource trace disagrees with DC1 settlement");
    }
    const auto& resource_operations =
        resource_trace.operations;
    const std::array<fq0_dominance::PlayerResourceCost, 2>
        costs = {
            convert_cost(current.resources[0]),
            convert_cost(current.resources[1]),
        };
    GameState boundary_state = current.settled_state;
    bool unresolved_transient_choice_effect = false;
    const auto make_result =
        [&](LearnedDecisionContext boundary_context,
            bool terminal, int winner) {
            return fq0_dominance::Settlement(
                std::move(root_information_fingerprint),
                information_set_world, current.settled_state,
                resource_operations, std::move(boundary_state),
                boundary_context, costs, terminal, winner,
                true, unresolved_transient_choice_effect);
        };

    if (const auto winner =
            terminal_winner(boundary_state);
        winner.has_value()) {
        return make_result({}, true, *winner);
    }
    if (!current.window_ended ||
        !boundary_state.stack.empty()) {
        throw std::logic_error(
            "FQ0 dominance root window did not settle");
    }

    unresolved_transient_choice_effect =
        phase_has_future_combat(probe.phase) &&
        creature_choice_state_changed(
            information_set_world, boundary_state);

    finish_remaining_turn(
        boundary_state, probe.phase);
    if (const auto winner =
            terminal_winner(boundary_state);
        winner.has_value()) {
        return make_result({}, true, *winner);
    }

    if (boundary_state.turn_number ==
        std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error(
            "FQ0 dominance turn number overflow");
    }
    ++boundary_state.turn_number;
    advance_turn_player(boundary_state);
    begin_turn(
        boundary_state,
        boundary_state.active_player);

    const bool starting_player_first_turn =
        boundary_state.turn_number == 1 &&
        boundary_state.active_player ==
            boundary_state.starting_player;
    if (!starting_player_first_turn) {
        PlayerState& active =
            boundary_state.players[
                boundary_state.active_player];
        if (active.library.empty()) {
            boundary_state.failed_draw[
                boundary_state.active_player] = true;
            return make_result(
                {}, true,
                static_cast<int>(
                    1 - boundary_state.active_player));
        }
        active.hand.push_back(active.library.back());
        active.library.pop_back();
        ++boundary_state
              .stats[boundary_state.active_player]
              .cards_drawn;
    }

    const LearnedDecisionContext boundary_context = {
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player =
            boundary_state.active_player,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    return make_result(boundary_context, false, -2);
}

} // namespace old_school::fq0_dominance_transition
