#include "old_school/exact_combat_subgame.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace old_school::exact_combat_subgame {
namespace {

constexpr std::size_t kPlayerCount = 2;

std::size_t opponent_of(std::size_t player) {
    if (player >= kPlayerCount) {
        throw std::out_of_range(
            "exact combat attacking player must be 0 or 1");
    }
    return 1 - player;
}

CreaturePermanent* find_creature(
    PlayerState& player, PermanentId permanent) {
    const auto found = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [permanent](const CreaturePermanent& creature) {
            return creature.id == permanent;
        });
    return found == player.creatures.end() ? nullptr : &*found;
}

const CreaturePermanent* find_creature(
    const PlayerState& player, PermanentId permanent) {
    const auto found = std::find_if(
        player.creatures.begin(), player.creatures.end(),
        [permanent](const CreaturePermanent& creature) {
            return creature.id == permanent;
        });
    return found == player.creatures.end() ? nullptr : &*found;
}

bool contains_creature(
    const PlayerState& player, PermanentId permanent) {
    return find_creature(player, permanent) != nullptr;
}

std::size_t checked_product(
    std::size_t left, std::size_t right,
    std::size_t limit, const char* message) {
    if (left != 0 &&
        right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::overflow_error(message);
    }
    const std::size_t product = left * right;
    if (product > limit) {
        throw std::length_error(message);
    }
    return product;
}

GameState normalized_attack_state(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers) {
    GameState normalized = state;
    std::unordered_set<PermanentId> seen;
    seen.reserve(attackers.size());
    for (const PermanentId attacker : attackers) {
        if (!seen.insert(attacker).second) {
            throw std::invalid_argument(
                "exact combat attackers must be unique");
        }
        CreaturePermanent* creature = find_creature(
            normalized.players[attacking_player], attacker);
        if (creature == nullptr) {
            throw std::invalid_argument(
                "exact combat attacker is missing");
        }
        // Declare Blockers snapshots already contain tapped attackers.
        creature->tapped = false;
    }

    GameState legality = normalized;
    if (!resolve_combat(
            legality, attacking_player, attackers, {})) {
        throw std::invalid_argument(
            "exact combat fixed attack is illegal");
    }
    return normalized;
}

std::vector<BlockerOptions> blocker_options(
    const GameState& normalized,
    std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    std::size_t maximum_blockers) {
    const std::size_t defender = opponent_of(attacking_player);
    std::vector<BlockerOptions> options;
    std::unordered_set<PermanentId> seen;
    for (const CreaturePermanent& blocker :
         normalized.players[defender].creatures) {
        if (blocker.tapped) {
            continue;
        }
        if (options.size() == maximum_blockers) {
            throw std::length_error(
                "exact combat blocker bound exceeded");
        }
        if (!seen.insert(blocker.id).second) {
            throw std::invalid_argument(
                "exact combat blocker ids must be unique");
        }
        BlockerOptions current{
            .blocker = blocker.id,
        };
        for (const PermanentId attacker : attackers) {
            GameState legality = normalized;
            if (resolve_combat(
                    legality, attacking_player, attackers,
                    {{attacker, blocker.id}})) {
                current.legal_attackers.push_back(attacker);
            }
        }
        options.push_back(std::move(current));
    }
    return options;
}

std::vector<PermanentId> pure_chump_blockers(
    const GameState& result, std::size_t attacking_player,
    const std::vector<
        std::pair<PermanentId, PermanentId>>& declared_blocks) {
    const std::size_t defender = opponent_of(attacking_player);
    std::vector<PermanentId> chumps;
    for (const auto& [attacker, blocker] : declared_blocks) {
        const bool attacker_survived = contains_creature(
            result.players[attacking_player], attacker);
        const bool blocker_survived = contains_creature(
            result.players[defender], blocker);
        if (attacker_survived && !blocker_survived) {
            chumps.push_back(blocker);
        }
    }
    return chumps;
}

struct Enumerator {
    const GameState& normalized;
    std::size_t attacking_player = 0;
    const std::vector<PermanentId>& attackers;
    const std::vector<BlockerOptions>& options;
    const Bounds& bounds;
    Enumeration& result;
    std::vector<std::pair<PermanentId, PermanentId>>
        declared_blocks;

    void emit_damage_orders(
        std::size_t assignment_index) {
        std::vector<std::vector<PermanentId>> groups(
            attackers.size());
        for (const auto& [attacker, blocker] :
             declared_blocks) {
            const auto found = std::find(
                attackers.begin(), attackers.end(), attacker);
            if (found == attackers.end()) {
                throw std::logic_error(
                    "exact combat assignment lost its attacker");
            }
            groups[static_cast<std::size_t>(
                       std::distance(attackers.begin(), found))]
                .push_back(blocker);
        }
        std::size_t order_count = 1;
        for (auto& group : groups) {
            std::sort(group.begin(), group.end());
            for (std::size_t factor = 2;
                 factor <= group.size(); ++factor) {
                order_count = checked_product(
                    order_count, factor,
                    bounds.maximum_damage_orders_per_assignment,
                    "exact combat damage-order bound exceeded");
            }
        }

        std::size_t emitted = 0;
        std::vector<std::pair<PermanentId, PermanentId>>
            ordered_blocks;
        const auto recurse =
            [&](const auto& self, std::size_t group_index)
                -> void {
            if (group_index == groups.size()) {
                if (result.plans.size() ==
                    bounds.maximum_completed_plans) {
                    throw std::length_error(
                        "exact combat completed-plan bound "
                        "exceeded");
                }
                GameState successor = normalized;
                if (!resolve_combat(
                        successor, attacking_player, attackers,
                        ordered_blocks)) {
                    throw std::logic_error(
                        "exact combat enumerated an illegal "
                        "completed plan");
                }
                result.plans.push_back({
                    .block_assignment_index =
                        assignment_index,
                    .damage_order_index = emitted,
                    .declared_blocks = declared_blocks,
                    .damage_ordered_blocks = ordered_blocks,
                    .resulting_state = successor,
                    .pure_chump_blockers =
                        pure_chump_blockers(
                            successor, attacking_player,
                            declared_blocks),
                });
                ++emitted;
                return;
            }

            auto& group = groups[group_index];
            do {
                const std::size_t previous_size =
                    ordered_blocks.size();
                for (const PermanentId blocker : group) {
                    ordered_blocks.emplace_back(
                        attackers[group_index], blocker);
                }
                self(self, group_index + 1);
                ordered_blocks.resize(previous_size);
            } while (std::next_permutation(
                group.begin(), group.end()));
        };
        recurse(recurse, 0);
        if (emitted != order_count) {
            throw std::logic_error(
                "exact combat damage-order census mismatch");
        }
    }

    void enumerate_assignments(std::size_t blocker_index) {
        if (blocker_index == options.size()) {
            const std::size_t assignment_index =
                result.legal_block_assignments;
            if (assignment_index ==
                bounds.maximum_block_assignments) {
                throw std::length_error(
                    "exact combat block-assignment bound "
                    "exceeded");
            }
            ++result.legal_block_assignments;
            emit_damage_orders(assignment_index);
            return;
        }

        // Canonical choice zero is No Block.
        enumerate_assignments(blocker_index + 1);
        for (const PermanentId attacker :
             options[blocker_index].legal_attackers) {
            declared_blocks.emplace_back(
                attacker, options[blocker_index].blocker);
            enumerate_assignments(blocker_index + 1);
            declared_blocks.pop_back();
        }
    }
};

bool better_attacker_response(
    double attacker_score,
    const AttackerBestResponse& incumbent) {
    return attacker_score > incumbent.attacker_score;
}

bool better_defender_choice(
    const AttackerBestResponse& candidate,
    const AttackerBestResponse& incumbent) {
    return candidate.defender_score >
           incumbent.defender_score;
}

} // namespace

const AttackerBestResponse& Selection::selected_response() const {
    if (attacker_best_responses.empty() ||
        defender_selected_assignment >=
            attacker_best_responses.size()) {
        throw std::logic_error(
            "exact combat selection has no selected response");
    }
    return attacker_best_responses[
        defender_selected_assignment];
}

Enumeration enumerate(
    const GameState& state, std::size_t attacking_player,
    const std::vector<PermanentId>& attackers,
    const Bounds& bounds) {
    static_cast<void>(opponent_of(attacking_player));
    if (bounds.maximum_block_assignments == 0 ||
        bounds.maximum_damage_orders_per_assignment == 0 ||
        bounds.maximum_completed_plans == 0) {
        throw std::invalid_argument(
            "exact combat enumeration bounds must be positive");
    }
    if (attackers.size() > bounds.maximum_attackers) {
        throw std::length_error(
            "exact combat attacker bound exceeded");
    }
    const GameState normalized =
        normalized_attack_state(
            state, attacking_player, attackers);
    const std::vector<BlockerOptions> options =
        blocker_options(
            normalized, attacking_player, attackers,
            bounds.maximum_blockers);

    std::size_t assignment_count = 1;
    for (const BlockerOptions& blocker : options) {
        assignment_count = checked_product(
            assignment_count,
            blocker.legal_attackers.size() + 1,
            bounds.maximum_block_assignments,
            "exact combat block-assignment bound exceeded");
    }

    Enumeration result{
        .attacking_player = attacking_player,
        .attackers = attackers,
        .blocker_options = options,
    };
    Enumerator enumerator{
        .normalized = normalized,
        .attacking_player = attacking_player,
        .attackers = result.attackers,
        .options = result.blocker_options,
        .bounds = bounds,
        .result = result,
    };
    enumerator.enumerate_assignments(0);
    if (result.legal_block_assignments !=
            assignment_count ||
        result.plans.empty()) {
        throw std::logic_error(
            "exact combat block-assignment census mismatch");
    }
    return result;
}

Selection select_defender_max_after_attacker_max(
    const Enumeration& enumeration,
    const StateScorer& scorer) {
    if (!scorer) {
        throw std::invalid_argument(
            "exact combat selection requires a scorer");
    }
    const std::size_t defender =
        opponent_of(enumeration.attacking_player);
    if (enumeration.legal_block_assignments == 0 ||
        enumeration.plans.empty()) {
        throw std::invalid_argument(
            "exact combat selection requires completed plans");
    }

    Selection result;
    result.attacker_best_responses.reserve(
        enumeration.legal_block_assignments);
    std::size_t plan_index = 0;
    for (std::size_t assignment = 0;
         assignment < enumeration.legal_block_assignments;
         ++assignment) {
        std::optional<AttackerBestResponse> best;
        while (plan_index < enumeration.plans.size() &&
               enumeration.plans[plan_index]
                       .block_assignment_index ==
                   assignment) {
            const CompletedPlan& plan =
                enumeration.plans[plan_index];
            const double attacker_score = scorer(
                plan.resulting_state,
                enumeration.attacking_player);
            const double defender_score = scorer(
                plan.resulting_state, defender);
            if (!std::isfinite(attacker_score) ||
                !std::isfinite(defender_score)) {
                throw std::domain_error(
                    "exact combat scorer returned a nonfinite "
                    "value");
            }
            if (!best.has_value() ||
                better_attacker_response(
                    attacker_score, *best)) {
                best = AttackerBestResponse{
                    .block_assignment_index = assignment,
                    .completed_plan_index = plan_index,
                    .attacker_score = attacker_score,
                    .defender_score = defender_score,
                };
            }
            ++plan_index;
        }
        if (!best.has_value()) {
            throw std::invalid_argument(
                "exact combat plans have a missing or unordered "
                "block assignment");
        }
        result.attacker_best_responses.push_back(*best);
    }
    if (plan_index != enumeration.plans.size()) {
        throw std::invalid_argument(
            "exact combat plans have an out-of-range block "
            "assignment");
    }

    result.defender_selected_assignment = 0;
    for (std::size_t assignment = 1;
         assignment <
         result.attacker_best_responses.size();
         ++assignment) {
        if (better_defender_choice(
                result.attacker_best_responses[assignment],
                result.attacker_best_responses[
                    result.defender_selected_assignment])) {
            result.defender_selected_assignment = assignment;
        }
    }
    return result;
}

StateScorer make_learned_end_combat_scorer(
    std::shared_ptr<const LearnedModel> model) {
    if (!model) {
        throw std::invalid_argument(
            "exact combat Learned scorer requires a model");
    }
    const LearnedCriticSchema schema =
        learned_critic_schema(model);
    return [model = std::move(model), schema](
               const GameState& state,
               std::size_t perspective) {
        if (schema == LearnedCriticSchema::DecisionContextV1) {
            return learned_contextual_critic_value(
                state, perspective,
                {
                    .valid = true,
                    .phase = TurnPhase::EndCombat,
                    .decision_player = state.active_player,
                    .consecutive_passes = 0,
                    .sorcery_actions = false,
                },
                model);
        }
        return learned_critic_value(
            state, perspective, model);
    };
}

} // namespace old_school::exact_combat_subgame
