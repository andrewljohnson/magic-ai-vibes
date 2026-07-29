#include "old_school/action_q_multiscale_teacher.hpp"

#include "old_school/learned_iteration.hpp"
#include "old_school/probes.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace old_school::action_q_multiscale_teacher {
namespace {

namespace bellman_teacher = action_q_bellman_teacher;

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

double mean_probability(
    std::span<const ResolvedSample> samples) {
    if (samples.empty()) {
        throw std::invalid_argument(
            "AQ2-MS0 cannot average an empty sample bank");
    }
    double total = 0.0;
    for (const ResolvedSample& sample : samples) {
        if (!probability(sample.value)) {
            throw std::invalid_argument(
                "AQ2-MS0 resolved sample is not a probability");
        }
        total += sample.value;
    }
    const double result =
        total / static_cast<double>(samples.size());
    if (!probability(result)) {
        throw std::logic_error(
            "AQ2-MS0 resolved mean is not a probability");
    }
    return result;
}

double composite_value(
    double bellman_value, double resolved_value) {
    if (!probability(bellman_value) ||
        !probability(resolved_value)) {
        throw std::invalid_argument(
            "AQ2-MS0 blend input is not a probability");
    }
    const double result =
        kBellmanWeight * bellman_value +
        kResolvedWeight * resolved_value;
    if (!probability(result)) {
        throw std::logic_error(
            "AQ2-MS0 composite is not a probability");
    }
    return result;
}

void add_accounting(
    ResolvedAccounting& destination,
    const ResolvedActionAccounting& source) {
    destination.evaluations += source.evaluations;
    destination.terminal_evaluations +=
        source.terminal_evaluations;
    destination.critic_evaluations +=
        source.critic_evaluations;
    destination.window_ended_evaluations +=
        source.window_ended_evaluations;
    destination.priority_passes += source.priority_passes;
    destination.stack_resolutions +=
        source.stack_resolutions;
}

} // namespace

RootTargets score_priority_root(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    std::span<const PriorityAction> candidates,
    std::shared_ptr<const LearnedModel> legacy_c16,
    std::uint64_t root_seed) {
    RootTargets result{
        .bellman = bellman_teacher::score_priority_root(
            state, original_decks, context, candidates,
            legacy_c16, root_seed),
    };
    bellman_teacher::validate_root_targets(result.bellman);

    std::vector<GameState> sampled_worlds;
    sampled_worlds.reserve(bellman_teacher::kRootWorlds);
    for (const bellman_teacher::WorldSeeds& world :
         result.bellman.root_worlds) {
        sampled_worlds.push_back(
            sample_determinization(
                state, original_decks,
                result.bellman.root_owner,
                world.determinization_seed));
    }

    result.actions.reserve(result.bellman.actions.size());
    for (const bellman_teacher::RootActionTarget& bellman_action :
         result.bellman.actions) {
        ActionTarget target{
            .descriptor = bellman_action.descriptor,
            .action = bellman_action.action,
            .bellman_value = bellman_action.value,
        };
        target.resolved_samples.reserve(
            bellman_teacher::kRootWorlds);
        for (std::size_t world = 0;
             world < sampled_worlds.size(); ++world) {
            const auto consequence =
                resolve_priority_action_consequence(
                    sampled_worlds[world],
                    result.bellman.root_owner,
                    context.sorcery_actions,
                    context.consecutive_passes,
                    target.action);
            if (!consequence.has_value()) {
                throw std::logic_error(
                    "AQ2-MS0 legal action had no resolved "
                    "consequence");
            }

            double value = 0.5;
            if (consequence->terminal) {
                value =
                    learned_iteration::
                        terminal_value_for_perspective(
                            consequence->winner,
                            result.bellman.root_owner);
                ++target.resolved_accounting
                      .terminal_evaluations;
            } else {
                value = learned_critic_value(
                    consequence->state,
                    result.bellman.root_owner,
                    legacy_c16);
                ++target.resolved_accounting
                      .critic_evaluations;
            }
            if (!probability(value)) {
                throw std::logic_error(
                    "AQ2-MS0 resolved leaf is not a probability");
            }
            ++target.resolved_accounting.evaluations;
            target.resolved_accounting
                    .window_ended_evaluations +=
                static_cast<std::size_t>(
                    consequence->window_ended);
            target.resolved_accounting.priority_passes +=
                consequence->priority_passes;
            target.resolved_accounting.stack_resolutions +=
                consequence->stack_resolutions;
            target.resolved_samples.push_back({
                .source_world =
                    result.bellman.root_worlds[world],
                .value = value,
                .terminal = consequence->terminal,
                .critic_leaf = !consequence->terminal,
                .window_ended =
                    consequence->window_ended,
                .priority_passes =
                    consequence->priority_passes,
                .stack_resolutions =
                    consequence->stack_resolutions,
            });
        }
        target.resolved_value =
            mean_probability(target.resolved_samples);
        target.value = composite_value(
            target.bellman_value, target.resolved_value);
        result.actions.push_back(std::move(target));
    }

    result.resolved_accounting.root_actions =
        result.actions.size();
    result.resolved_accounting.root_determinizations =
        result.bellman.root_worlds.size();
    for (const ActionTarget& target : result.actions) {
        add_accounting(
            result.resolved_accounting,
            target.resolved_accounting);
    }

    validate_root_targets(result);
    return result;
}

void validate_root_targets(const RootTargets& targets) {
    bellman_teacher::validate_root_targets(targets.bellman);
    if (targets.actions.size() !=
            targets.bellman.actions.size() ||
        targets.actions.size() < 2 ||
        targets.bellman.root_worlds.size() !=
            bellman_teacher::kRootWorlds) {
        throw std::invalid_argument(
            "AQ2-MS0 root dimensions drifted");
    }

    ResolvedAccounting observed{
        .root_actions = targets.actions.size(),
        .root_determinizations =
            targets.bellman.root_worlds.size(),
    };
    for (std::size_t index = 0;
         index < targets.actions.size(); ++index) {
        const ActionTarget& target = targets.actions[index];
        const bellman_teacher::RootActionTarget& bellman =
            targets.bellman.actions[index];
        if (target.descriptor.empty() ||
            target.descriptor != bellman.descriptor ||
            target.descriptor !=
                probes::stable_priority_action_descriptor(
                    target.action) ||
            target.action != bellman.action ||
            !same_bits(
                target.bellman_value, bellman.value) ||
            !probability(target.bellman_value) ||
            !probability(target.resolved_value) ||
            !probability(target.value) ||
            target.resolved_samples.size() !=
                bellman_teacher::kRootWorlds) {
            throw std::invalid_argument(
                "AQ2-MS0 action identity or dimensions drifted");
        }

        ResolvedActionAccounting observed_action;
        for (std::size_t world = 0;
             world < bellman_teacher::kRootWorlds; ++world) {
            const ResolvedSample& sample =
                target.resolved_samples[world];
            if (sample.source_world !=
                    targets.bellman.root_worlds[world] ||
                !probability(sample.value) ||
                sample.terminal == sample.critic_leaf ||
                sample.stack_resolutions >
                    sample.priority_passes) {
                throw std::invalid_argument(
                    "AQ2-MS0 resolved common-world coordinate "
                    "drifted");
            }
            ++observed_action.evaluations;
            observed_action.terminal_evaluations +=
                static_cast<std::size_t>(sample.terminal);
            observed_action.critic_evaluations +=
                static_cast<std::size_t>(
                    sample.critic_leaf);
            observed_action.window_ended_evaluations +=
                static_cast<std::size_t>(
                    sample.window_ended);
            observed_action.priority_passes +=
                sample.priority_passes;
            observed_action.stack_resolutions +=
                sample.stack_resolutions;
        }
        if (!same_bits(
                target.resolved_value,
                mean_probability(target.resolved_samples)) ||
            !same_bits(
                target.value,
                composite_value(
                    target.bellman_value,
                    target.resolved_value))) {
            throw std::invalid_argument(
                "AQ2-MS0 resolved mean or fixed blend drifted");
        }

        const ResolvedActionAccounting& accounting =
            target.resolved_accounting;
        if (accounting != observed_action ||
            accounting.evaluations !=
                bellman_teacher::kRootWorlds ||
            accounting.terminal_evaluations +
                    accounting.critic_evaluations !=
                accounting.evaluations ||
            accounting.window_ended_evaluations >
                accounting.evaluations ||
            accounting.stack_resolutions >
                accounting.priority_passes) {
            throw std::invalid_argument(
                "AQ2-MS0 action accounting does not cross-sum");
        }
        add_accounting(observed, accounting);
    }

    if (observed != targets.resolved_accounting ||
        observed.evaluations !=
            observed.root_actions *
                observed.root_determinizations ||
        observed.terminal_evaluations +
                observed.critic_evaluations !=
            observed.evaluations ||
        observed.window_ended_evaluations >
            observed.evaluations ||
        observed.stack_resolutions >
            observed.priority_passes) {
        throw std::invalid_argument(
            "AQ2-MS0 aggregate accounting does not cross-sum");
    }
}

} // namespace old_school::action_q_multiscale_teacher
