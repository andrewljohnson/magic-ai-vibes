#include "old_school/action_q_multiscale_explore.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/learned_iteration.hpp"
#include "old_school/probe_runner.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace old_school::action_q_multiscale_explore {
namespace {

namespace bellman = action_q_bellman_teacher;
namespace multiscale = action_q_multiscale_teacher;

static_assert(
    kTeacherTemperature ==
    learned_iteration::kP16ExplorationTemperature);
static_assert(
    kTeacherPrimaryWeight ==
    learned_iteration::kP16ExplorationTeacherWeight);

std::size_t deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument(
            "AQ2 deck is outside the five-deck environment");
    }
    return index;
}

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

std::vector<CardId> cards_for_deck(DeckId deck) {
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
    throw std::invalid_argument("AQ2 source deck is invalid");
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ2 requires exact frozen Legacy C16");
    }
}

void add_macro(
    bellman::MacroAccounting& destination,
    const bellman::MacroAccounting& source) {
    destination.transitions += source.transitions;
    destination.terminal_transitions +=
        source.terminal_transitions;
    destination.boundary_transitions +=
        source.boundary_transitions;
    destination.critic_leaves += source.critic_leaves;
    destination.actions_applied += source.actions_applied;
    destination.priority_actions_applied +=
        source.priority_actions_applied;
    destination.phase_transitions +=
        source.phase_transitions;
    destination.turn_advances += source.turn_advances;
}

void add_bellman(
    bellman::TeacherAccounting& destination,
    const bellman::TeacherAccounting& source) {
    destination.root_actions += source.root_actions;
    destination.root_determinizations +=
        source.root_determinizations;
    destination.root_terminal_particles +=
        source.root_terminal_particles;
    destination.root_boundary_particles +=
        source.root_boundary_particles;
    destination.successor_group_occurrences +=
        source.successor_group_occurrences;
    destination.same_owner_group_occurrences +=
        source.same_owner_group_occurrences;
    destination.opponent_owner_group_occurrences +=
        source.opponent_owner_group_occurrences;
    destination.same_owner_root_particles +=
        source.same_owner_root_particles;
    destination.opponent_owner_root_particles +=
        source.opponent_owner_root_particles;
    destination.unique_successor_information_sets +=
        source.unique_successor_information_sets;
    destination.successor_actions +=
        source.successor_actions;
    destination.successor_determinizations +=
        source.successor_determinizations;
    add_macro(destination.root_macros, source.root_macros);
    add_macro(
        destination.successor_macros,
        source.successor_macros);
}

void add_resolved(
    multiscale::ResolvedAccounting& destination,
    const multiscale::ResolvedAccounting& source) {
    destination.root_actions += source.root_actions;
    destination.root_determinizations +=
        source.root_determinizations;
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

GameConfig source_game_config(
    const std::shared_ptr<const LearnedModel>& parent,
    std::size_t starting_player) {
    const BotConfig bot{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = kBaseWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_adversarial_blocks = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = 800,
        .learned_model = parent,
    };
    return {
        .max_turns = kSourceTurnCap,
        .starting_player = starting_player,
        .bots = {bot, bot},
        .learned_training_seed = 424242,
        .learned_model = parent,
        .learned_search_depth = 1,
    };
}

LearnedSearchConfig base_search_config(std::uint64_t seed) {
    return {
        .seed = seed,
        .worlds = kBaseWorlds,
        .rollouts_per_world = kBaseRolloutsPerWorld,
        .horizon_turns = kBaseHorizonTurns,
        .continuation_variant =
            LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = true,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .evaluation_threads = kBaseEvaluationThreads,
    };
}

void validate_bellman_accounting(
    const bellman::TeacherAccounting& accounting,
    std::size_t action_count) {
    if (accounting.root_actions != action_count ||
        !owner_partition_complete(accounting, action_count) ||
        accounting.root_determinizations !=
            bellman::kRootWorlds ||
        accounting.root_terminal_particles +
                accounting.root_boundary_particles !=
            action_count * bellman::kRootWorlds ||
        accounting.root_macros.transitions !=
            action_count * bellman::kRootWorlds ||
        accounting.root_macros.terminal_transitions +
                accounting.root_macros.boundary_transitions !=
            accounting.root_macros.transitions ||
        accounting.root_macros.critic_leaves != 0 ||
        accounting.successor_determinizations !=
            accounting.unique_successor_information_sets * 2 *
                bellman::kSuccessorWorlds ||
        accounting.successor_macros.transitions !=
            accounting.successor_actions * 2 *
                bellman::kSuccessorWorlds ||
        accounting.successor_macros.terminal_transitions +
                accounting.successor_macros.boundary_transitions !=
            accounting.successor_macros.transitions ||
        accounting.successor_macros.critic_leaves !=
            accounting.successor_macros.boundary_transitions ||
        accounting.unique_successor_information_sets >
            accounting.successor_group_occurrences) {
        throw std::invalid_argument(
            "AQ2 Bellman accounting cross-sum failed");
    }
}

void validate_resolved_rows(const RootExample& root) {
    const std::size_t action_count = root.actions.size();
    if (root.resolved_samples.size() != action_count ||
        root.resolved_action_accounting.size() != action_count ||
        root.resolved_accounting.root_actions != action_count ||
        root.resolved_accounting.root_determinizations !=
            bellman::kRootWorlds ||
        root.resolved_accounting.evaluations !=
            action_count * bellman::kRootWorlds ||
        root.resolved_accounting.terminal_evaluations +
                root.resolved_accounting.critic_evaluations !=
            root.resolved_accounting.evaluations ||
        root.resolved_accounting.window_ended_evaluations >
            root.resolved_accounting.evaluations) {
        throw std::invalid_argument(
            "AQ2 resolved aggregate accounting failed");
    }
    multiscale::ResolvedAccounting observed{
        .root_actions = action_count,
        .root_determinizations = bellman::kRootWorlds,
    };
    for (std::size_t action = 0;
         action < action_count; ++action) {
        const auto& samples = root.resolved_samples[action];
        const auto& expected =
            root.resolved_action_accounting[action];
        if (samples.size() != bellman::kRootWorlds ||
            expected.evaluations != bellman::kRootWorlds ||
            expected.terminal_evaluations +
                    expected.critic_evaluations !=
                expected.evaluations ||
            expected.window_ended_evaluations >
                expected.evaluations) {
            throw std::invalid_argument(
                "AQ2 resolved action accounting failed");
        }
        multiscale::ResolvedActionAccounting actual;
        double total = 0.0;
        for (std::size_t world = 0;
             world < samples.size(); ++world) {
            const auto& sample = samples[world];
            if (sample.source_world.world_index != world ||
                !probability(sample.value) ||
                sample.terminal == sample.critic_leaf) {
                throw std::invalid_argument(
                    "AQ2 resolved sample row is malformed");
            }
            ++actual.evaluations;
            actual.terminal_evaluations +=
                static_cast<std::size_t>(sample.terminal);
            actual.critic_evaluations +=
                static_cast<std::size_t>(sample.critic_leaf);
            actual.window_ended_evaluations +=
                static_cast<std::size_t>(
                    sample.window_ended);
            actual.priority_passes +=
                sample.priority_passes;
            actual.stack_resolutions +=
                sample.stack_resolutions;
            total += sample.value;
        }
        if (actual != expected ||
            root.resolved_scores[action] !=
                total /
                    static_cast<double>(
                        bellman::kRootWorlds)) {
            throw std::invalid_argument(
                "AQ2 resolved sample reduction drifted");
        }
        multiscale::ResolvedAccounting aggregate{
            .evaluations = actual.evaluations,
            .terminal_evaluations =
                actual.terminal_evaluations,
            .critic_evaluations =
                actual.critic_evaluations,
            .window_ended_evaluations =
                actual.window_ended_evaluations,
            .priority_passes = actual.priority_passes,
            .stack_resolutions =
                actual.stack_resolutions,
        };
        add_resolved(observed, aggregate);
    }
    if (observed != root.resolved_accounting) {
        throw std::invalid_argument(
            "AQ2 resolved accounting did not cross-sum");
    }
}

RootExample build_root(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& decks,
    const LearnedDecisionContext& context,
    RootCoordinate coordinate,
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!context.valid ||
        context.decision_player != coordinate.actor) {
        throw std::invalid_argument(
            "AQ2 retained root context is invalid");
    }
    const std::vector<PriorityAction> legal =
        legal_priority_actions(
            state, coordinate.actor,
            context.sorcery_actions);
    if (legal.size() < 2) {
        throw std::invalid_argument(
            "AQ2 retained root is not nontrivial");
    }
    const multiscale::RootTargets targets =
        multiscale::score_priority_root(
            state, decks, context, legal, parent,
            coordinate.search_seed);
    multiscale::validate_root_targets(targets);
    if (targets.bellman.root_seed !=
            coordinate.search_seed ||
        targets.bellman.root_owner !=
            coordinate.actor ||
        targets.actions.size() != legal.size()) {
        throw std::logic_error(
            "AQ2 teacher result drifted from retained root");
    }

    RootExample result;
    result.coordinate = std::move(coordinate);
    result.information_set_fingerprint =
        targets.bellman.root_information_set_fingerprint;
    result.bellman_accounting =
        targets.bellman.accounting;
    result.resolved_accounting =
        targets.resolved_accounting;
    for (const multiscale::ActionTarget& target :
         targets.actions) {
        result.action_descriptors.push_back(
            target.descriptor);
        result.actions.push_back(target.action);
        result.bellman_scores.push_back(
            target.bellman_value);
        result.resolved_scores.push_back(
            target.resolved_value);
        result.teacher_scores.push_back(target.value);
        result.resolved_samples.push_back(
            target.resolved_samples);
        result.resolved_action_accounting.push_back(
            target.resolved_accounting);
        result.options.push_back(
            learned_priority_policy_features(
                state, result.coordinate.actor,
                target.action, context.sorcery_actions,
                context.phase,
                context.consecutive_passes));
    }
    const LearnedActionSamples base =
        learned_priority_action_samples(
            state, decks, result.coordinate.actor,
            context.sorcery_actions, context.phase,
            context.consecutive_passes, result.actions,
            parent,
            base_search_config(
                result.coordinate.search_seed));
    result.base_scores =
        base.exact_priority_aggregate_scores;
    result.base_sampled_worlds = base.sampled_worlds;
    result.base_rollout_evaluations =
        base.rollout_evaluations;
    result.base_terminal_evaluations =
        base.terminal_evaluations;
    result.base_bootstrapped_evaluations =
        base.bootstrapped_evaluations;
    result.target_probabilities =
        action_q_explore::teacher_distribution(
            result.teacher_scores);
    result.weight = 1.0;
    return result;
}

void add_root_to_census(
    const RootExample& root, DeckCensus& deck) {
    ++deck.retained_roots;
    deck.retained_options += root.actions.size();
    ++deck.base_score_calls;
    deck.base_sampled_worlds += root.base_sampled_worlds;
    deck.base_rollout_evaluations +=
        root.base_rollout_evaluations;
    deck.base_terminal_evaluations +=
        root.base_terminal_evaluations;
    deck.base_bootstrapped_evaluations +=
        root.base_bootstrapped_evaluations;
    add_bellman(
        deck.bellman_accounting,
        root.bellman_accounting);
    add_resolved(
        deck.resolved_accounting,
        root.resolved_accounting);
    const auto [minimum, maximum] =
        std::minmax_element(
            root.teacher_scores.begin(),
            root.teacher_scores.end());
    if (*maximum > *minimum) {
        ++deck.nonzero_spread_roots;
    }
    if (deck.minimum_legal_width == 0) {
        deck.minimum_legal_width = root.actions.size();
    } else {
        deck.minimum_legal_width =
            std::min(
                deck.minimum_legal_width,
                root.actions.size());
    }
    deck.maximum_legal_width =
        std::max(
            deck.maximum_legal_width,
            root.actions.size());
}

CorpusBlock collect_block(
    std::size_t block,
    const std::shared_ptr<const LearnedModel>& parent) {
    CorpusBlock result;
    result.block = block;
    result.census.block = block;
    const auto schedule =
        learned_iteration::balanced_schedule(
            kCollectionRootSeed,
            kScheduleGeneration, block);
    for (const auto& scheduled : schedule) {
        const std::array<std::vector<CardId>, 2> decks{
            cards_for_deck(scheduled.seat_decks[0]),
            cards_for_deck(scheduled.seat_decks[1]),
        };
        Game game(
            decks[0], decks[1], scheduled.seed,
            source_game_config(
                parent, scheduled.starting_player));
        std::vector<LearnedDecisionTracePoint> trace;
        static_cast<void>(
            game.run_with_priority_root_trace(trace));
        ++result.census.games;
        for (std::size_t actor = 0; actor < 2; ++actor) {
            DeckCensus& deck =
                result.census.decks[
                    deck_index(
                        scheduled.seat_decks[actor])];
            ++deck.actor_games;
            struct Candidate {
                std::size_t trace_ordinal = 0;
                std::size_t nontrivial_ordinal = 0;
            };
            std::vector<Candidate> candidates;
            for (std::size_t ordinal = 0;
                 ordinal < trace.size(); ++ordinal) {
                const auto& point = trace[ordinal];
                if (!point.context.valid ||
                    point.context.decision_player != actor ||
                    legal_priority_actions(
                        point.state, actor,
                        point.context.sorcery_actions)
                            .size() < 2) {
                    continue;
                }
                candidates.push_back({
                    .trace_ordinal = ordinal,
                    .nontrivial_ordinal =
                        candidates.size(),
                });
            }
            deck.nontrivial_roots += candidates.size();
            const auto retained =
                learned_iteration::
                    evenly_spaced_retained_indices(
                        candidates.size(),
                        kMaximumRootsPerActorGame);
            for (std::size_t position = 0;
                 position < retained.size(); ++position) {
                const Candidate& selected =
                    candidates[retained[position]];
                const auto& point =
                    trace[selected.trace_ordinal];
                RootCoordinate coordinate{
                    .block = block,
                    .schedule_index =
                        scheduled.schedule_index,
                    .pairing_index =
                        scheduled.pairing_index,
                    .game_seed = scheduled.seed,
                    .starting_player =
                        scheduled.starting_player,
                    .actor = actor,
                    .trace_ordinal =
                        selected.trace_ordinal,
                    .nontrivial_ordinal =
                        selected.nontrivial_ordinal,
                    .actor_game_nontrivial_roots =
                        candidates.size(),
                    .retained_position = position,
                    .seat_decks = scheduled.seat_decks,
                    .search_seed =
                        root_search_seed(
                            block,
                            scheduled.schedule_index,
                            actor,
                            selected.nontrivial_ordinal),
                };
                RootExample root =
                    build_root(
                        point.state, decks,
                        point.context,
                        std::move(coordinate), parent);
                add_root_to_census(root, deck);
                result.roots.push_back(
                    std::move(root));
            }
        }
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        DeckCensus& census = result.census.decks[deck];
        if (census.retained_roots == 0) {
            throw std::logic_error(
                "AQ2 block omitted a metagame deck");
        }
        census.root_weight = 1.0;
        const double weight =
            1.0 /
            static_cast<double>(
                census.retained_roots);
        for (RootExample& root : result.roots) {
            if (deck_index(root.owner_deck()) == deck) {
                root.weight = weight;
            }
        }
    }
    validate_block(result);
    return result;
}

void require_no_probe_errors(
    std::span<const std::string> errors,
    std::string_view corpus_name) {
    if (!errors.empty()) {
        throw std::runtime_error(
            "AQ2 preflight " + std::string(corpus_name) +
            " fixture validation failed: " + errors.front());
    }
}

probes::DecisionProbe unique_probe(
    const std::vector<probes::DecisionProbe>& corpus,
    std::string_view stable_id) {
    const auto found = std::find_if(
        corpus.begin(), corpus.end(),
        [stable_id](const probes::DecisionProbe& probe) {
            return probe.stable_id == stable_id;
        });
    if (found == corpus.end() ||
        std::find_if(
            std::next(found), corpus.end(),
            [stable_id](const probes::DecisionProbe& probe) {
                return probe.stable_id == stable_id;
            }) != corpus.end()) {
        throw std::runtime_error(
            "AQ2 preflight fixture is missing or duplicated: " +
            std::string(stable_id));
    }
    return *found;
}

std::vector<probes::DecisionProbe>
load_preflight_probes() {
    const auto counter =
        probes::make_counter_composition_controls_v1();
    const auto braingeyser =
        probes::make_braingeyser_x_zero_control_v1();
    const auto field =
        probes::make_field_regressions_v1();
    const auto spike =
        probes::make_force_spike_policy_controls_v1();
    require_no_probe_errors(
        probes::validate_counter_composition_controls_v1(
            counter),
        probes::kCounterCompositionControlsV1);
    require_no_probe_errors(
        probes::validate_braingeyser_x_zero_control_v1(
            braingeyser),
        probes::kBraingeyserXZeroControlV1);
    require_no_probe_errors(
        probes::validate_field_regressions_v1(field),
        probes::kFieldRegressionsV1);
    require_no_probe_errors(
        probes::validate_force_spike_policy_controls_v1(
            spike),
        probes::kForceSpikePolicyControlsV1);
    const auto manifest = preflight_manifest();
    return {
        unique_probe(counter, manifest[0].stable_id),
        unique_probe(braingeyser, manifest[1].stable_id),
        unique_probe(field, manifest[2].stable_id),
        unique_probe(spike, manifest[3].stable_id),
    };
}

LearnedDecisionContext preflight_context(
    const probes::DecisionProbe& probe) {
    return {
        .valid = true,
        .phase = probe.phase,
        .decision_player = probe.root_player,
        .consecutive_passes =
            probe.consecutive_passes,
        .sorcery_actions =
            probe.phase == TurnPhase::FirstMain ||
            probe.phase == TurnPhase::SecondMain,
    };
}

const probes::Candidate& candidate_for_action(
    const probes::DecisionProbe& probe,
    const PriorityAction& action) {
    const probes::Candidate* found = nullptr;
    for (const probes::Candidate& candidate :
         probe.candidates) {
        if (!std::holds_alternative<PriorityAction>(
                candidate.action) ||
            std::get<PriorityAction>(candidate.action) !=
                action) {
            continue;
        }
        if (found != nullptr) {
            throw std::runtime_error(
                "AQ2 preflight probe action mapping is ambiguous");
        }
        found = &candidate;
    }
    if (found == nullptr) {
        throw std::runtime_error(
            "AQ2 preflight authoritative action has no key");
    }
    return *found;
}

std::vector<PriorityAction> authoritative_actions(
    const probes::DecisionProbe& probe) {
    if (probe.decision_kind !=
            probes::DecisionKind::Priority ||
        probe.root_player >= 2) {
        throw std::runtime_error(
            "AQ2 preflight fixture is not Priority");
    }
    const LearnedDecisionContext context =
        preflight_context(probe);
    const std::vector<PriorityAction> actions =
        legal_priority_actions(
            probe.state, probe.root_player,
            context.sorcery_actions);
    if (actions.size() != probe.candidates.size()) {
        throw std::runtime_error(
            "AQ2 preflight omits an authoritative legal action");
    }
    for (const PriorityAction& action : actions) {
        static_cast<void>(
            candidate_for_action(probe, action));
    }
    return actions;
}

void append_action(
    std::ostringstream& output,
    const PriorityAction& action) {
    output
        << static_cast<std::size_t>(action.kind)
        << ',' << static_cast<std::size_t>(action.card)
        << ',' << action.target.has_value();
    if (action.target.has_value()) {
        output
            << ',' << action.target->player
            << ',' << action.target->creature.has_value();
        if (action.target->creature.has_value()) {
            output << ',' << *action.target->creature;
        }
    }
    output << ',' << action.spell_target.has_value();
    if (action.spell_target.has_value()) {
        output << ',' << *action.spell_target;
    }
    output << ',' << action.source_permanent.has_value();
    if (action.source_permanent.has_value()) {
        output << ',' << *action.source_permanent;
    }
    output << ',' << action.x_value;
}

void append_macro(
    std::ostringstream& output,
    const bellman::MacroAccounting& accounting) {
    output
        << accounting.transitions << ','
        << accounting.terminal_transitions << ','
        << accounting.boundary_transitions << ','
        << accounting.critic_leaves << ','
        << accounting.actions_applied << ','
        << accounting.priority_actions_applied << ','
        << accounting.phase_transitions << ','
        << accounting.turn_advances;
}

void append_bellman(
    std::ostringstream& output,
    const bellman::TeacherAccounting& accounting) {
    output
        << accounting.root_actions << ','
        << accounting.root_determinizations << ','
        << accounting.root_terminal_particles << ','
        << accounting.root_boundary_particles << ','
        << accounting.successor_group_occurrences << ','
        << accounting.same_owner_group_occurrences << ','
        << accounting.opponent_owner_group_occurrences << ','
        << accounting.same_owner_root_particles << ','
        << accounting.opponent_owner_root_particles << ','
        << accounting.unique_successor_information_sets << ','
        << accounting.successor_actions << ','
        << accounting.successor_determinizations << ',';
    append_macro(output, accounting.root_macros);
    output << ',';
    append_macro(output, accounting.successor_macros);
}

void append_resolved(
    std::ostringstream& output,
    const multiscale::ResolvedAccounting& accounting) {
    output
        << accounting.root_actions << ','
        << accounting.root_determinizations << ','
        << accounting.evaluations << ','
        << accounting.terminal_evaluations << ','
        << accounting.critic_evaluations << ','
        << accounting.window_ended_evaluations << ','
        << accounting.priority_passes << ','
        << accounting.stack_resolutions;
}

void append_resolved_action(
    std::ostringstream& output,
    const multiscale::ResolvedActionAccounting& accounting) {
    output
        << accounting.evaluations << ','
        << accounting.terminal_evaluations << ','
        << accounting.critic_evaluations << ','
        << accounting.window_ended_evaluations << ','
        << accounting.priority_passes << ','
        << accounting.stack_resolutions;
}

void append_double_vector(
    std::ostringstream& output,
    std::span<const double> values) {
    output << ',' << values.size();
    for (const double value : values) {
        output << ','
               << std::bit_cast<std::uint64_t>(value);
    }
}

std::string census_payload(const Corpus& corpus) {
    std::ostringstream output;
    const LearnedValuePriorityHeadUpdateConfig optimizer =
        optimizer_config();
    const auto append_string =
        [&output](std::string_view value) {
            output << value.size() << ':' << value;
        };
    output
        << "aq2-ms0-v1|" << corpus.root_seed << '|'
        << corpus.parent_fingerprint
        << "|recipe," << kScheduleGeneration
        << ',' << kFitBlock
        << ',' << kCheckBlock
        << ',' << kSourceTurnCap
        << ',' << kBaseWorlds
        << ',' << kBaseRolloutsPerWorld
        << ',' << kBaseHorizonTurns
        << ',' << kBaseEvaluationThreads
        << ',' << kMaximumRootsPerActorGame
        << ',' << kPolicyFeatureCount
        << ',' << std::bit_cast<std::uint64_t>(
                       kTeacherTemperature)
        << ',' << std::bit_cast<std::uint64_t>(
                       kTeacherPrimaryWeight)
        << ',' << std::bit_cast<std::uint64_t>(
                       kCandidateResidualWeight)
        << ',' << std::bit_cast<std::uint64_t>(
                       multiscale::kBellmanWeight)
        << ',' << std::bit_cast<std::uint64_t>(
                       multiscale::kResolvedWeight)
        << ',' << bellman::kRootWorlds
        << ',' << bellman::kSuccessorWorlds
        << ',' << optimizer.batch_size
        << ',' << optimizer.epochs
        << ',' << std::bit_cast<std::uint64_t>(
                       optimizer.learning_rate)
        << ',' << std::bit_cast<std::uint64_t>(
                       optimizer.beta1)
        << ',' << std::bit_cast<std::uint64_t>(
                       optimizer.beta2)
        << ',' << std::bit_cast<std::uint64_t>(
                       optimizer.epsilon)
        << ',' << std::bit_cast<std::uint64_t>(
                       optimizer.global_gradient_norm_clip)
        << ',' << optimizer.seed
        << ',' << std::bit_cast<std::uint64_t>(
                       optimizer.residual_weight)
        << ',' << std::bit_cast<std::uint64_t>(
                       optimizer.policy_temperature)
        << "|source,C16-mirror,training-games=800,"
           "training-seed=424242,search-depth=1,"
           "variant=ValueSearchChampion,"
           "exploration=0,continuation-epsilon=0,"
           "base-blend-shallow=1,resolved-alpha=0,"
           "residual=0,pass-dominance=0,"
           "adversarial-blocks=0,continuation=Legacy";
    const auto append_block =
        [&output, &append_string](
            const CorpusBlock& block) {
            output << "|B," << block.block
                   << ',' << block.census.games;
            for (const DeckCensus& deck :
                 block.census.decks) {
                output
                    << "|D," << deck.actor_games
                    << ',' << deck.nontrivial_roots
                    << ',' << deck.retained_roots
                    << ',' << deck.retained_options
                    << ',' << deck.nonzero_spread_roots
                    << ',' << deck.minimum_legal_width
                    << ',' << deck.maximum_legal_width
                    << ',' << deck.base_score_calls
                    << ',' << deck.base_sampled_worlds
                    << ',' << deck.base_rollout_evaluations
                    << ',' << deck.base_terminal_evaluations
                    << ',' << deck.base_bootstrapped_evaluations
                    << ',' << std::bit_cast<std::uint64_t>(
                                   deck.root_weight)
                    << ',';
                append_bellman(
                    output, deck.bellman_accounting);
                output << ',';
                append_resolved(
                    output, deck.resolved_accounting);
            }
            for (const RootExample& root : block.roots) {
                const RootCoordinate& c = root.coordinate;
                output
                    << "|R," << c.block
                    << ',' << c.schedule_index
                    << ',' << c.pairing_index
                    << ',' << c.game_seed
                    << ',' << c.starting_player
                    << ',' << c.actor
                    << ',' << c.trace_ordinal
                    << ',' << c.nontrivial_ordinal
                    << ',' << c.actor_game_nontrivial_roots
                    << ',' << c.retained_position
                    << ',' << static_cast<std::size_t>(
                                   c.seat_decks[0])
                    << ',' << static_cast<std::size_t>(
                                   c.seat_decks[1])
                    << ',' << c.search_seed << ',';
                append_string(
                    root.information_set_fingerprint);
                output << ',' << root.actions.size();
                for (std::size_t action = 0;
                     action < root.actions.size(); ++action) {
                    output << ',';
                    append_string(
                        root.action_descriptors[action]);
                    output << ',';
                    append_action(output, root.actions[action]);
                    output << ',' << root.options[action].size();
                    for (const double feature :
                         root.options[action]) {
                        output
                            << ',' << std::bit_cast<
                                           std::uint64_t>(
                                           feature);
                    }
                    output << ',';
                    append_resolved_action(
                        output,
                        root.resolved_action_accounting[
                            action]);
                    output
                        << ','
                        << root.resolved_samples[action].size();
                    for (const auto& sample :
                         root.resolved_samples[action]) {
                        output
                            << ',' << sample.source_world.world_index
                            << ','
                            << sample.source_world
                                   .determinization_seed
                            << ','
                            << sample.source_world.macro_seed
                            << ','
                            << std::bit_cast<std::uint64_t>(
                                   sample.value)
                            << ',' << sample.terminal
                            << ',' << sample.critic_leaf
                            << ',' << sample.window_ended
                            << ',' << sample.priority_passes
                            << ',' << sample.stack_resolutions;
                    }
                }
                append_double_vector(
                    output, root.base_scores);
                append_double_vector(
                    output, root.bellman_scores);
                append_double_vector(
                    output, root.resolved_scores);
                append_double_vector(
                    output, root.teacher_scores);
                append_double_vector(
                    output, root.target_probabilities);
                output
                    << ',' << root.base_sampled_worlds
                    << ',' << root.base_rollout_evaluations
                    << ',' << root.base_terminal_evaluations
                    << ',' << root.base_bootstrapped_evaluations
                    << ',' << std::bit_cast<std::uint64_t>(
                                   root.weight)
                    << ',';
                append_bellman(
                    output, root.bellman_accounting);
                output << ',';
                append_resolved(
                    output, root.resolved_accounting);
            }
        };
    append_block(corpus.fit);
    append_block(corpus.check);
    return output.str();
}

} // namespace

DeckId RootExample::owner_deck() const {
    if (coordinate.actor >= 2) {
        throw std::out_of_range("AQ2 root actor is invalid");
    }
    return coordinate.seat_decks[coordinate.actor];
}

std::size_t BlockCensus::retained_roots() const {
    return std::accumulate(
        decks.begin(), decks.end(), std::size_t{0},
        [](std::size_t total, const DeckCensus& deck) {
            return total + deck.retained_roots;
        });
}

std::size_t BlockCensus::retained_options() const {
    return std::accumulate(
        decks.begin(), decks.end(), std::size_t{0},
        [](std::size_t total, const DeckCensus& deck) {
            return total + deck.retained_options;
        });
}

DeckCensus BlockCensus::total() const {
    DeckCensus total;
    for (const DeckCensus& deck : decks) {
        total.actor_games += deck.actor_games;
        total.nontrivial_roots += deck.nontrivial_roots;
        total.retained_roots += deck.retained_roots;
        total.retained_options += deck.retained_options;
        total.nonzero_spread_roots +=
            deck.nonzero_spread_roots;
        total.base_score_calls += deck.base_score_calls;
        total.base_sampled_worlds +=
            deck.base_sampled_worlds;
        total.base_rollout_evaluations +=
            deck.base_rollout_evaluations;
        total.base_terminal_evaluations +=
            deck.base_terminal_evaluations;
        total.base_bootstrapped_evaluations +=
            deck.base_bootstrapped_evaluations;
        add_bellman(
            total.bellman_accounting,
            deck.bellman_accounting);
        add_resolved(
            total.resolved_accounting,
            deck.resolved_accounting);
    }
    return total;
}

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() != 1) {
        return std::nullopt;
    }
    if (arguments.front() == "--census") {
        return Command::Census;
    }
    if (arguments.front() == "--run") {
        return Command::Run;
    }
    return std::nullopt;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-action-q-multiscale-explore "
           "(--census|--run)\n";
}

std::uint64_t root_search_seed(
    std::size_t block, std::size_t schedule_index,
    std::size_t actor, std::size_t nontrivial_ordinal) {
    if ((block != kFitBlock &&
         block != kCheckBlock) ||
        actor >= 2 ||
        schedule_index >=
            learned_iteration::kBalancedScheduleGames) {
        throw std::invalid_argument(
            "AQ2 root seed coordinate is invalid");
    }
    return learned_iteration::derive_seed(
        kCollectionRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        block, schedule_index * 2 + actor,
        nontrivial_ordinal);
}

std::uint64_t fit_seed() {
    const std::uint64_t derived =
        learned_iteration::derive_seed(
            kCollectionRootSeed,
            learned_iteration::SeedDomain::PolicyFit,
            0, 0, 0);
    if (derived != kFitSeed) {
        throw std::logic_error("AQ2 fit seed drifted");
    }
    return derived;
}

LearnedValuePriorityHeadUpdateConfig optimizer_config() {
    return {
        .batch_size = 64,
        .epochs = 64,
        .learning_rate = 0.003,
        .beta1 = 0.9,
        .beta2 = 0.999,
        .epsilon = 1.0e-8,
        .global_gradient_norm_clip = 5.0,
        .seed = fit_seed(),
        .residual_weight = kCandidateResidualWeight,
        .policy_temperature = kTeacherTemperature,
    };
}

bool selector_wins_advance(std::size_t challenger_wins) {
    return challenger_wins >= kSelectorWinsRequired;
}

std::uint64_t preflight_seed(std::size_t fixture_index) {
    if (fixture_index >= kPreflightFixtureCount) {
        throw std::out_of_range(
            "AQ2 preflight fixture index is invalid");
    }
    return learned_iteration::derive_seed(
        kPreflightRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        fixture_index, 0, 0);
}

std::array<PreflightSpec, kPreflightFixtureCount>
preflight_manifest() {
    const std::array<PreflightSpec, kPreflightFixtureCount>
        manifest{{
            {
                .fixture_index = 0,
                .stable_id =
                    "control.blue.counter-same-target-after-"
                    "intervening-counter.v1",
                .kind = PreflightKind::StrictPair,
                .positive_key =
                    "counter-opponent-counterspell",
                .negative_key = "pass",
                .expected_seed =
                    13755611371498319020ULL,
            },
            {
                .fixture_index = 1,
                .stable_id =
                    "control.blue.braingeyser-x0.v1",
                .kind = PreflightKind::ExcludeXZero,
                .excluded_keys = {
                    "braingeyser-x0-self",
                    "braingeyser-x0-opponent",
                },
                .expected_seed =
                    2589590173959096294ULL,
            },
            {
                .fixture_index = 2,
                .stable_id =
                    "field.green.second-main-sick-bear-growth.v1",
                .kind = PreflightKind::StrictPair,
                .positive_key = "pass",
                .negative_key =
                    "growth-own-summoning-sick-grizzly-bears",
                .expected_seed =
                    4410279927652125381ULL,
            },
            {
                .fixture_index = 3,
                .stable_id =
                    "control.blue.force-spike-live-gray-ogre.v1",
                .kind = PreflightKind::StrictPair,
                .positive_key = "force-spike-gray-ogre",
                .negative_key = "pass",
                .expected_seed =
                    118189991942941696ULL,
            },
        }};
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        if (manifest[index].fixture_index != index ||
            manifest[index].stable_id.empty() ||
            manifest[index].expected_seed !=
                preflight_seed(index)) {
            throw std::logic_error(
                "AQ2 preflight manifest drifted");
        }
    }
    return manifest;
}

DirectionSummary evaluate_direction(
    const PreflightSpec& spec,
    std::span<const PreflightAction> actions) {
    if (actions.empty()) {
        throw std::invalid_argument(
            "AQ2 direction requires legal actions");
    }
    double best = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        if (actions[index].probe_key.empty() ||
            actions[index].typed_descriptor.empty() ||
            !probability(actions[index].bellman_value) ||
            !probability(actions[index].resolved_value) ||
            !probability(actions[index].value) ||
            std::find_if(
                actions.begin(), actions.begin() + index,
                [&](const PreflightAction& earlier) {
                    return earlier.probe_key ==
                               actions[index].probe_key ||
                           earlier.action ==
                               actions[index].action;
                }) != actions.begin() + index) {
            throw std::invalid_argument(
                "AQ2 preflight action rows are invalid");
        }
        best = std::max(best, actions[index].value);
    }
    const auto value_for =
        [&actions](std::string_view key) {
            const auto found = std::find_if(
                actions.begin(), actions.end(),
                [key](const PreflightAction& action) {
                    return action.probe_key == key;
                });
            if (found == actions.end()) {
                throw std::invalid_argument(
                    "AQ2 required probe key is absent");
            }
            return found->value;
        };

    DirectionSummary result;
    for (const PreflightAction& action : actions) {
        if (action.value == best) {
            result.exact_max_support.push_back(
                action.probe_key);
        }
    }
    if (spec.kind == PreflightKind::StrictPair) {
        if (spec.positive_key.empty() ||
            spec.negative_key.empty() ||
            spec.positive_key == spec.negative_key) {
            throw std::invalid_argument(
                "AQ2 strict-pair manifest is invalid");
        }
        result.positive_value =
            value_for(spec.positive_key);
        result.negative_value =
            value_for(spec.negative_key);
        result.required_margin =
            result.positive_value -
            result.negative_value;
        result.passed = result.required_margin > 0.0;
        return result;
    }
    if (spec.kind != PreflightKind::ExcludeXZero ||
        spec.excluded_keys[0].empty() ||
        spec.excluded_keys[1].empty() ||
        spec.excluded_keys[0] ==
            spec.excluded_keys[1]) {
        throw std::invalid_argument(
            "AQ2 X=0 manifest is invalid");
    }
    const double first =
        value_for(spec.excluded_keys[0]);
    const double second =
        value_for(spec.excluded_keys[1]);
    result.positive_value = best;
    result.negative_value = std::max(first, second);
    result.excluded_margins = {
        best - first,
        best - second,
    };
    result.required_margin =
        std::min(
            result.excluded_margins[0],
            result.excluded_margins[1]);
    result.passed =
        result.excluded_margins[0] > 0.0 &&
        result.excluded_margins[1] > 0.0;
    return result;
}

bool PreflightFixtureReport::gate_passed() const {
    if (spec.fixture_index >= kPreflightFixtureCount ||
        seed != spec.expected_seed ||
        seed != preflight_seed(spec.fixture_index) ||
        information_set_fingerprint.empty() ||
        actions.empty() ||
        !direction.passed ||
        !hidden_repartition_nonvacuous ||
        !hidden_repartition_bit_identical ||
        !reversed_action_bit_identical ||
        resolved_accounting.root_actions != actions.size() ||
        resolved_accounting.root_determinizations !=
            bellman::kRootWorlds ||
        resolved_accounting.evaluations !=
            actions.size() * bellman::kRootWorlds ||
        resolved_accounting.terminal_evaluations +
                resolved_accounting.critic_evaluations !=
            resolved_accounting.evaluations) {
        return false;
    }
    try {
        validate_bellman_accounting(
            bellman_accounting, actions.size());
        if (evaluate_direction(spec, actions) != direction) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    for (const PreflightAction& action : actions) {
        const bool expected =
            std::find(
                direction.exact_max_support.begin(),
                direction.exact_max_support.end(),
                action.probe_key) !=
            direction.exact_max_support.end();
        if (action.exact_max != expected) {
            return false;
        }
    }
    return true;
}

bool PreflightReport::gate_passed() const {
    if (parent_fingerprint !=
            kRequiredParentFingerprint ||
        fixtures.size() != kPreflightFixtureCount) {
        return false;
    }
    bool all = true;
    const auto manifest = preflight_manifest();
    for (std::size_t index = 0;
         index < fixtures.size(); ++index) {
        if (fixtures[index].spec != manifest[index] ||
            direction_passed[index] !=
                fixtures[index].direction.passed ||
            !fixtures[index].gate_passed()) {
            return false;
        }
        all = all && direction_passed[index];
    }
    return hypothesis_passed && hypothesis_passed == all;
}

PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    const auto manifest = preflight_manifest();
    const auto probes = load_preflight_probes();
    if (probes.size() != manifest.size()) {
        throw std::logic_error(
            "AQ2 preflight loader changed size");
    }
    PreflightReport report;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    bool all = true;
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        const probes::DecisionProbe& probe = probes[index];
        const PreflightSpec& spec = manifest[index];
        if (probe.stable_id != spec.stable_id) {
            throw std::logic_error(
                "AQ2 preflight fixture order drifted");
        }
        const LearnedDecisionContext context =
            preflight_context(probe);
        const auto actions = authoritative_actions(probe);
        const std::uint64_t seed = preflight_seed(index);
        const multiscale::RootTargets direct =
            multiscale::score_priority_root(
                probe.state, probe.original_decks,
                context, actions, parent, seed);
        multiscale::validate_root_targets(direct);

        auto reversed_actions = actions;
        std::reverse(
            reversed_actions.begin(),
            reversed_actions.end());
        const multiscale::RootTargets reversed =
            multiscale::score_priority_root(
                probe.state, probe.original_decks,
                context, reversed_actions, parent, seed);
        multiscale::validate_root_targets(reversed);

        const GameState hidden_state =
            probe_runner::hidden_repartition_clone(probe);
        const auto hidden_actions =
            legal_priority_actions(
                hidden_state, probe.root_player,
                context.sorcery_actions);
        const multiscale::RootTargets hidden =
            multiscale::score_priority_root(
                hidden_state, probe.original_decks,
                context, hidden_actions, parent, seed);
        multiscale::validate_root_targets(hidden);

        PreflightFixtureReport fixture;
        fixture.spec = spec;
        fixture.seed = seed;
        fixture.information_set_fingerprint =
            direct.bellman
                .root_information_set_fingerprint;
        fixture.bellman_accounting =
            direct.bellman.accounting;
        fixture.resolved_accounting =
            direct.resolved_accounting;
        fixture.hidden_repartition_nonvacuous =
            hidden_state != probe.state;
        fixture.hidden_repartition_bit_identical =
            direct == hidden;
        fixture.reversed_action_bit_identical =
            direct == reversed;
        for (const multiscale::ActionTarget& target :
             direct.actions) {
            const probes::Candidate& candidate =
                candidate_for_action(
                    probe, target.action);
            fixture.actions.push_back({
                .probe_key = candidate.descriptor,
                .typed_descriptor = target.descriptor,
                .action = target.action,
                .bellman_value =
                    target.bellman_value,
                .resolved_value =
                    target.resolved_value,
                .value = target.value,
            });
        }
        fixture.direction =
            evaluate_direction(spec, fixture.actions);
        for (PreflightAction& action : fixture.actions) {
            action.exact_max =
                std::find(
                    fixture.direction
                        .exact_max_support.begin(),
                    fixture.direction
                        .exact_max_support.end(),
                    action.probe_key) !=
                fixture.direction
                    .exact_max_support.end();
        }
        report.direction_passed[index] =
            fixture.direction.passed;
        all = all && fixture.direction.passed;
        report.fixtures.push_back(std::move(fixture));
    }
    report.hypothesis_passed = all;
    return report;
}

void print_preflight_report(
    std::ostream& output,
    const PreflightReport& report) {
    output
        << std::setprecision(17)
        << "preflight schema=aq2-ms0-d0-v1"
        << " root_seed=" << kPreflightRootSeed
        << " parent_fingerprint="
        << report.parent_fingerprint
        << " fixtures=" << report.fixtures.size()
        << '\n';
    for (const PreflightFixtureReport& fixture :
         report.fixtures) {
        output
            << "preflight_fixture index="
            << fixture.spec.fixture_index
            << " id=" << fixture.spec.stable_id
            << " seed=" << fixture.seed
            << " actions=" << fixture.actions.size()
            << " margin="
            << fixture.direction.required_margin
            << " direction_passed="
            << fixture.direction.passed
            << " hidden_nonvacuous="
            << fixture.hidden_repartition_nonvacuous
            << " hidden_bit_identical="
            << fixture.hidden_repartition_bit_identical
            << " reversed_bit_identical="
            << fixture.reversed_action_bit_identical
            << " bellman_root_terminal="
            << fixture.bellman_accounting
                   .root_terminal_particles
            << " bellman_root_boundary="
            << fixture.bellman_accounting
                   .root_boundary_particles
            << " resolved_terminal="
            << fixture.resolved_accounting
                   .terminal_evaluations
            << " resolved_critic="
            << fixture.resolved_accounting
                   .critic_evaluations
            << '\n';
        for (const PreflightAction& action :
             fixture.actions) {
            output
                << "preflight_action fixture="
                << fixture.spec.fixture_index
                << " key=" << action.probe_key
                << " descriptor="
                << action.typed_descriptor
                << " bellman=" << action.bellman_value
                << " resolved=" << action.resolved_value
                << " composite=" << action.value
                << " exact_max=" << action.exact_max
                << '\n';
        }
    }
    output
        << "preflight_result="
        << (report.gate_passed() ? "PASS" : "FAIL")
        << " all_directions="
        << report.hypothesis_passed << '\n';
}

bool owner_partition_complete(
    const bellman::TeacherAccounting& accounting,
    std::size_t action_count) {
    return
        accounting.same_owner_group_occurrences +
                accounting.opponent_owner_group_occurrences ==
            accounting.successor_group_occurrences &&
        accounting.same_owner_root_particles +
                accounting.opponent_owner_root_particles ==
            accounting.root_boundary_particles &&
        accounting.root_terminal_particles +
                accounting.same_owner_root_particles +
                accounting.opponent_owner_root_particles ==
            action_count * bellman::kRootWorlds;
}

void validate_block(const CorpusBlock& block) {
    if ((block.block != kFitBlock &&
         block.block != kCheckBlock) ||
        block.census.block != block.block ||
        block.census.games !=
            learned_iteration::kBalancedScheduleGames ||
        block.census.retained_roots() !=
            block.roots.size()) {
        throw std::invalid_argument(
            "AQ2 block identity/accounting is invalid");
    }
    std::array<DeckCensus, kDeckCount> observed{};
    const auto schedule =
        learned_iteration::balanced_schedule(
            kCollectionRootSeed,
            kScheduleGeneration, block.block);
    std::array<
        std::vector<const RootExample*>,
        learned_iteration::kBalancedScheduleGames * 2>
        actor_game_roots;
    for (const RootExample& root : block.roots) {
        const std::size_t actions = root.actions.size();
        if (root.coordinate.schedule_index >=
                schedule.size()) {
            throw std::invalid_argument(
                "AQ2 schedule coordinate is invalid");
        }
        const auto& expected_game =
            schedule[root.coordinate.schedule_index];
        if (root.coordinate.block != block.block ||
            root.coordinate.actor >= 2 ||
            root.coordinate.pairing_index !=
                expected_game.pairing_index ||
            root.coordinate.game_seed !=
                expected_game.seed ||
            root.coordinate.starting_player !=
                expected_game.starting_player ||
            root.coordinate.seat_decks !=
                expected_game.seat_decks ||
            root.coordinate.actor_game_nontrivial_roots == 0 ||
            actions < 2 ||
            root.action_descriptors.size() != actions ||
            root.options.size() != actions ||
            root.base_scores.size() != actions ||
            root.bellman_scores.size() != actions ||
            root.resolved_scores.size() != actions ||
            root.teacher_scores.size() != actions ||
            root.target_probabilities.size() != actions ||
            root.information_set_fingerprint.empty() ||
            !std::is_sorted(
                root.action_descriptors.begin(),
                root.action_descriptors.end()) ||
            std::adjacent_find(
                root.action_descriptors.begin(),
                root.action_descriptors.end()) !=
                root.action_descriptors.end() ||
            root.base_sampled_worlds != kBaseWorlds ||
            root.base_rollout_evaluations !=
                actions * kBaseWorlds *
                    kBaseRolloutsPerWorld ||
            root.base_terminal_evaluations +
                    root.base_bootstrapped_evaluations !=
                root.base_rollout_evaluations ||
            root.coordinate.search_seed !=
                root_search_seed(
                    root.coordinate.block,
                    root.coordinate.schedule_index,
                    root.coordinate.actor,
                    root.coordinate.nontrivial_ordinal) ||
            !std::isfinite(root.weight) ||
            root.weight <= 0.0) {
            throw std::invalid_argument(
                "AQ2 retained root is malformed");
        }
        validate_bellman_accounting(
            root.bellman_accounting, actions);
        validate_resolved_rows(root);
        double target_total = 0.0;
        for (std::size_t action = 0;
             action < actions; ++action) {
            const double expected_composite =
                multiscale::kBellmanWeight *
                    root.bellman_scores[action] +
                multiscale::kResolvedWeight *
                    root.resolved_scores[action];
            if (root.options[action].size() !=
                    kPolicyFeatureCount ||
                !std::all_of(
                    root.options[action].begin(),
                    root.options[action].end(),
                    [](double value) {
                        return std::isfinite(value);
                    }) ||
                !probability(root.base_scores[action]) ||
                !probability(root.bellman_scores[action]) ||
                !probability(root.resolved_scores[action]) ||
                !probability(root.teacher_scores[action]) ||
                std::bit_cast<std::uint64_t>(
                    root.teacher_scores[action]) !=
                    std::bit_cast<std::uint64_t>(
                        expected_composite) ||
                !probability(
                    root.target_probabilities[action])) {
                throw std::invalid_argument(
                    "AQ2 action row is malformed");
            }
            target_total +=
                root.target_probabilities[action];
        }
        if (std::abs(target_total - 1.0) > 1.0e-9 ||
            action_q_explore::teacher_distribution(
                root.teacher_scores) !=
                root.target_probabilities) {
            throw std::invalid_argument(
                "AQ2 target distribution drifted");
        }
        actor_game_roots[
            root.coordinate.schedule_index * 2 +
            root.coordinate.actor]
            .push_back(&root);
        add_root_to_census(
            root,
            observed[deck_index(root.owner_deck())]);
    }
    for (std::size_t actor_game = 0;
         actor_game < actor_game_roots.size();
         ++actor_game) {
        const std::size_t schedule_index =
            actor_game / 2;
        const std::size_t actor = actor_game % 2;
        DeckCensus& deck =
            observed[deck_index(
                schedule[schedule_index]
                    .seat_decks[actor])];
        ++deck.actor_games;
        const auto& roots = actor_game_roots[actor_game];
        if (roots.empty()) {
            continue;
        }
        const std::size_t total =
            roots.front()
                ->coordinate
                .actor_game_nontrivial_roots;
        const auto retained =
            learned_iteration::
                evenly_spaced_retained_indices(
                    total,
                    kMaximumRootsPerActorGame);
        if (roots.size() != retained.size()) {
            throw std::invalid_argument(
                "AQ2 actor-game retention count drifted");
        }
        deck.nontrivial_roots += total;
        for (std::size_t position = 0;
             position < roots.size(); ++position) {
            const RootCoordinate& coordinate =
                roots[position]->coordinate;
            if (coordinate.schedule_index !=
                    schedule_index ||
                coordinate.actor != actor ||
                coordinate.actor_game_nontrivial_roots !=
                    total ||
                coordinate.retained_position != position ||
                coordinate.nontrivial_ordinal !=
                    retained[position] ||
                (position > 0 &&
                 (coordinate.trace_ordinal <=
                      roots[position - 1]
                          ->coordinate.trace_ordinal ||
                  coordinate.nontrivial_ordinal <=
                      roots[position - 1]
                          ->coordinate
                          .nontrivial_ordinal))) {
                throw std::invalid_argument(
                    "AQ2 retained root coordinate drifted");
            }
        }
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const DeckCensus& expected =
            block.census.decks[deck];
        DeckCensus actual = observed[deck];
        actual.root_weight = expected.root_weight;
        if (expected.actor_games != 16 ||
            expected.retained_roots == 0 ||
            expected.root_weight != 1.0 ||
            actual != expected) {
            throw std::invalid_argument(
                "AQ2 per-deck census cross-sum failed");
        }
        const double expected_weight =
            1.0 /
            static_cast<double>(
                expected.retained_roots);
        for (const RootExample& root : block.roots) {
            if (deck_index(root.owner_deck()) == deck &&
                root.weight != expected_weight) {
                throw std::invalid_argument(
                    "AQ2 equal-deck root weight drifted");
            }
        }
    }
}

void validate_corpus(const Corpus& corpus) {
    if (corpus.root_seed != kCollectionRootSeed ||
        corpus.parent_fingerprint !=
            kRequiredParentFingerprint ||
        corpus.fit.block != kFitBlock ||
        corpus.check.block != kCheckBlock) {
        throw std::invalid_argument(
            "AQ2 corpus identity is invalid");
    }
    validate_block(corpus.fit);
    validate_block(corpus.check);
}

Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    Corpus corpus;
    corpus.parent_fingerprint =
        learned_model_fingerprint(parent);
    corpus.fit = collect_block(kFitBlock, parent);
    corpus.check = collect_block(kCheckBlock, parent);
    validate_corpus(corpus);
    return corpus;
}

Metrics evaluate(
    const CorpusBlock& block,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight) {
    if (!model) {
        throw std::invalid_argument(
            "AQ2 evaluation requires a model");
    }
    validate_block(block);
    struct Accumulator {
        std::size_t roots = 0;
        std::size_t options = 0;
        double agreement = 0.0;
        double regret = 0.0;
    };
    std::array<Accumulator, kDeckCount> accumulators{};
    for (const RootExample& root : block.roots) {
        const auto logits =
            learned_policy_head_logits(
                root.options,
                LearnedPolicyDecisionKind::Priority,
                model);
        const auto scores =
            action_q_explore::combined_scores(
                root.base_scores, logits,
                residual_weight);
        const auto metrics =
            action_q_explore::evaluate_root(
                root.teacher_scores, scores);
        Accumulator& accumulator =
            accumulators[deck_index(root.owner_deck())];
        ++accumulator.roots;
        accumulator.options += metrics.action_count;
        accumulator.agreement +=
            metrics.top_one_expected_agreement;
        accumulator.regret += metrics.regret;
    }
    Metrics result;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& source = accumulators[deck];
        if (source.roots == 0) {
            throw std::logic_error(
                "AQ2 metrics omitted a deck");
        }
        auto& destination = result.decks[deck];
        destination.deck = static_cast<DeckId>(deck);
        destination.roots = source.roots;
        destination.options = source.options;
        destination.top_one_expected_agreement =
            source.agreement /
            static_cast<double>(source.roots);
        destination.mean_regret =
            source.regret /
            static_cast<double>(source.roots);
        result.roots += source.roots;
        result.options += source.options;
        result.equal_deck_top_one_expected_agreement +=
            destination.top_one_expected_agreement /
            static_cast<double>(kDeckCount);
        result.equal_deck_mean_regret +=
            destination.mean_regret /
            static_cast<double>(kDeckCount);
    }
    return result;
}

FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    validate_corpus(corpus);
    FitReport report;
    report.parent_fingerprint_before =
        learned_model_fingerprint(parent);
    report.parent_components =
        learned_model_component_fingerprints(parent);
    report.optimizer = optimizer_config();
    report.fit_examples = corpus.fit.roots.size();
    std::vector<LearnedValuePriorityTrainingExample> examples;
    examples.reserve(corpus.fit.roots.size());
    for (const RootExample& root : corpus.fit.roots) {
        report.fit_options += root.actions.size();
        examples.push_back({
            .options = root.options,
            .base_scores = root.base_scores,
            .target_probabilities =
                root.target_probabilities,
            .weight = root.weight,
        });
    }
    report.candidate =
        update_learned_value_priority_head(
            parent, examples, report.optimizer);
    const auto repeated =
        update_learned_value_priority_head(
            parent, examples, report.optimizer);
    report.candidate_fingerprint =
        learned_model_fingerprint(report.candidate);
    report.repeated_fit_bit_identical =
        report.candidate_fingerprint ==
        learned_model_fingerprint(repeated);
    report.parent_fingerprint_after =
        learned_model_fingerprint(parent);
    report.parent_immutable =
        report.parent_fingerprint_before ==
        report.parent_fingerprint_after;
    report.candidate_components =
        learned_model_component_fingerprints(
            report.candidate);
    report.only_priority_component_changed =
        report.parent_components.critic ==
            report.candidate_components.critic &&
        report.parent_components.attack ==
            report.candidate_components.attack &&
        report.parent_components.block ==
            report.candidate_components.block &&
        report.parent_components.damage_order ==
            report.candidate_components.damage_order &&
        report.parent_components.priority !=
            report.candidate_components.priority;
    report.parent_fit = evaluate(corpus.fit, parent, 0.0);
    report.candidate_fit =
        evaluate(
            corpus.fit, report.candidate,
            kCandidateResidualWeight);
    report.parent_check =
        evaluate(corpus.check, parent, 0.0);
    report.candidate_check =
        evaluate(
            corpus.check, report.candidate,
            kCandidateResidualWeight);
    return report;
}

OfflineReport evaluate_offline(
    const Corpus& corpus,
    const FitReport& fit_report,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate) {
    validate_corpus(corpus);
    OfflineReport report;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    report.candidate_fingerprint =
        learned_model_fingerprint(candidate);
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto candidate_components =
        learned_model_component_fingerprints(candidate);
    report.isolation = {
        .parent_identity_exact =
            report.parent_fingerprint ==
                kRequiredParentFingerprint &&
            corpus.parent_fingerprint ==
                report.parent_fingerprint &&
            fit_report.parent_fingerprint_before ==
                report.parent_fingerprint &&
            fit_report.parent_fingerprint_after ==
                report.parent_fingerprint &&
            fit_report.parent_components ==
                parent_components,
        .candidate_identity_exact =
            fit_report.candidate &&
            learned_model_fingerprint(
                fit_report.candidate) ==
                report.candidate_fingerprint &&
            fit_report.candidate_fingerprint ==
                report.candidate_fingerprint &&
            fit_report.candidate_components ==
                candidate_components,
        .parent_immutable =
            fit_report.parent_immutable,
        .repeated_fit_bit_identical =
            fit_report.repeated_fit_bit_identical,
        .only_priority_component_changed =
            fit_report.only_priority_component_changed &&
            parent_components.critic ==
                candidate_components.critic &&
            parent_components.attack ==
                candidate_components.attack &&
            parent_components.block ==
                candidate_components.block &&
            parent_components.damage_order ==
                candidate_components.damage_order &&
            parent_components.priority !=
                candidate_components.priority,
    };
    report.check.parent =
        evaluate(corpus.check, parent, 0.0);
    report.check.candidate =
        evaluate(
            corpus.check, candidate,
            kCandidateResidualWeight);
    report.check.metrics_match_fit_report =
        report.check.parent == fit_report.parent_check &&
        report.check.candidate ==
            fit_report.candidate_check;
    report.check.regret_strictly_improved =
        report.check.candidate.equal_deck_mean_regret <
        report.check.parent.equal_deck_mean_regret;
    report.check.top_one_not_lower =
        report.check.candidate
                .equal_deck_top_one_expected_agreement >=
            report.check.parent
                .equal_deck_top_one_expected_agreement;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        report.check.deck_regret_guard[deck] =
            report.check.candidate.decks[deck]
                    .mean_regret <=
                report.check.parent.decks[deck]
                        .mean_regret +
                    action_q_offline_gate::
                        kMaximumCheckDeckRegretIncrease;
    }
    report.model =
        action_q_offline_gate::evaluate_model_gates(
            parent, candidate);
    const auto blue_regret =
        [](const probe_eval::ProbeMetricSummary& summary) {
            for (const auto& deck : summary.by_deck) {
                if (deck.root_deck == DeckId::Blue) {
                    return deck.mean_regret;
                }
            }
            return std::numeric_limits<double>::infinity();
        };
    const double parent_blue =
        blue_regret(report.model.frozen_dev.parent);
    const double candidate_blue =
        blue_regret(report.model.frozen_dev.candidate);
    report.frozen_dev_blue_regret_no_worse =
        std::isfinite(parent_blue) &&
        std::isfinite(candidate_blue) &&
        candidate_blue <= parent_blue;
    report.corpus_accounting_complete = true;
    return report;
}

bool OfflineReport::gate_passed() const {
    return parent_fingerprint ==
               kRequiredParentFingerprint &&
           !candidate_fingerprint.empty() &&
           candidate_fingerprint != parent_fingerprint &&
           isolation.gate_passed() &&
           check.gate_passed() &&
           model.gate_passed() &&
           frozen_dev_blue_regret_no_worse &&
           corpus_accounting_complete;
}

std::vector<std::string> OfflineReport::failures() const {
    std::vector<std::string> failures;
    if (parent_fingerprint !=
            kRequiredParentFingerprint ||
        candidate_fingerprint.empty() ||
        candidate_fingerprint == parent_fingerprint) {
        failures.push_back("model identity gate failed");
    }
    if (!isolation.gate_passed()) {
        failures.push_back(
            "fit repeatability/component-isolation gate failed");
    }
    if (!check.gate_passed()) {
        failures.push_back(
            "held-out CHECK equal-deck gate failed");
    }
    for (std::string failure : model.failures()) {
        failures.push_back(std::move(failure));
    }
    if (!frozen_dev_blue_regret_no_worse) {
        failures.push_back(
            "frozen DevV3 Blue-regret guard failed");
    }
    if (!corpus_accounting_complete) {
        failures.push_back(
            "AQ2 corpus accounting gate failed");
    }
    return failures;
}

std::string canonical_census_identity(
    const Corpus& corpus) {
    validate_corpus(corpus);
    return artifact_integrity::sha256_string(
        census_payload(corpus));
}

void require_frozen_census(const Corpus& corpus) {
    const std::string identity =
        canonical_census_identity(corpus);
    if (kFrozenCensusIdentity.empty()) {
        throw std::runtime_error(
            "AQ2 census is not frozen in source: " +
            identity);
    }
    if (identity != kFrozenCensusIdentity) {
        throw std::runtime_error(
            "AQ2 census identity drifted from freeze");
    }
}

void print_census(
    std::ostream& output, const Corpus& corpus) {
    validate_corpus(corpus);
    const auto print_block =
        [&output](
            std::string_view name,
            const CorpusBlock& block) {
            const DeckCensus total =
                block.census.total();
            const auto& b = total.bellman_accounting;
            const auto& r = total.resolved_accounting;
            output
                << "block name=" << name
                << " index=" << block.block
                << " games=" << block.census.games
                << " actor_games=" << total.actor_games
                << " nontrivial_roots="
                << total.nontrivial_roots
                << " retained_roots="
                << total.retained_roots
                << " retained_options="
                << total.retained_options
                << " nonzero_spread_roots="
                << total.nonzero_spread_roots
                << " base_calls=" << total.base_score_calls
                << " base_worlds="
                << total.base_sampled_worlds
                << " base_rollouts="
                << total.base_rollout_evaluations
                << " base_terminal="
                << total.base_terminal_evaluations
                << " base_bootstrapped="
                << total.base_bootstrapped_evaluations
                << " bellman_root_actions="
                << b.root_actions
                << " bellman_root_determinizations="
                << b.root_determinizations
                << " bellman_root_terminal="
                << b.root_terminal_particles
                << " bellman_root_boundary="
                << b.root_boundary_particles
                << " bellman_successor_groups="
                << b.successor_group_occurrences
                << " bellman_same_owner_groups="
                << b.same_owner_group_occurrences
                << " bellman_opponent_owner_groups="
                << b.opponent_owner_group_occurrences
                << " bellman_same_owner_particles="
                << b.same_owner_root_particles
                << " bellman_opponent_owner_particles="
                << b.opponent_owner_root_particles
                << " bellman_successor_information_sets="
                << b.unique_successor_information_sets
                << " bellman_successor_actions="
                << b.successor_actions
                << " bellman_successor_determinizations="
                << b.successor_determinizations
                << " resolved_root_actions="
                << r.root_actions
                << " resolved_root_determinizations="
                << r.root_determinizations
                << " resolved_evaluations="
                << r.evaluations
                << " resolved_terminal="
                << r.terminal_evaluations
                << " resolved_critic="
                << r.critic_evaluations
                << " resolved_window_ended="
                << r.window_ended_evaluations
                << " resolved_priority_passes="
                << r.priority_passes
                << " resolved_stack_resolutions="
                << r.stack_resolutions
                << '\n';
            for (std::size_t index = 0;
                 index < kDeckCount; ++index) {
                const DeckCensus& deck =
                    block.census.decks[index];
                output
                    << "deck block=" << name
                    << " name="
                    << deck_name(
                           static_cast<DeckId>(index))
                    << " actor_games="
                    << deck.actor_games
                    << " nontrivial_roots="
                    << deck.nontrivial_roots
                    << " retained_roots="
                    << deck.retained_roots
                    << " retained_options="
                    << deck.retained_options
                    << " nonzero_spread_roots="
                    << deck.nonzero_spread_roots
                    << " min_width="
                    << deck.minimum_legal_width
                    << " max_width="
                    << deck.maximum_legal_width
                    << " bellman_terminal="
                    << deck.bellman_accounting
                           .root_terminal_particles
                    << " bellman_boundary="
                    << deck.bellman_accounting
                           .root_boundary_particles
                    << " resolved_evaluations="
                    << deck.resolved_accounting.evaluations
                    << " resolved_terminal="
                    << deck.resolved_accounting
                           .terminal_evaluations
                    << " resolved_critic="
                    << deck.resolved_accounting
                           .critic_evaluations
                    << '\n';
            }
        };
    output
        << "census_identity="
        << canonical_census_identity(corpus) << '\n';
    print_block("FIT", corpus.fit);
    print_block("CHECK", corpus.check);
}

void print_model_gate_report(
    std::ostream& output,
    const action_q_offline_gate::ModelGateReport& report) {
    output << std::setprecision(17);
    const auto print_dev =
        [&output](
            std::string_view policy,
            const probe_eval::ProbeMetricSummary& summary) {
            output
                << "dev_metrics policy=" << policy
                << " probes=" << summary.probe_count
                << " top1="
                << summary.top1_expected_agreement
                << " mean_regret="
                << summary.mean_regret
                << " brier=" << summary.critic_brier
                << " log_loss="
                << summary.critic_log_loss
                << " bias=" << summary.critic_bias
                << " ece=" << summary.critic_ece << '\n';
            for (const auto& deck : summary.by_deck) {
                output
                    << "dev_metrics_deck policy=" << policy
                    << " deck=" << deck_name(deck.root_deck)
                    << " probes=" << deck.probe_count
                    << " top1="
                    << deck.top1_expected_agreement
                    << " mean_regret="
                    << deck.mean_regret << '\n';
            }
        };
    print_dev("parent", report.frozen_dev.parent);
    print_dev("candidate", report.frozen_dev.candidate);
    output
        << "dev_stability labels="
        << report.frozen_dev.labels
        << " stable_parent_agreements="
        << report.frozen_dev.stable_parent_agreements
        << " lost_stable_parent_agreements="
        << report.frozen_dev
               .lost_stable_parent_agreements
        << " cache_before_bytes="
        << report.frozen_dev.cache_before.byte_size
        << " cache_before_sha256="
        << report.frozen_dev.cache_before.sha256
        << " cache_after_bytes="
        << report.frozen_dev.cache_after.byte_size
        << " cache_after_sha256="
        << report.frozen_dev.cache_after.sha256
        << '\n'
        << "ancestral self_score="
        << report.ancestral.self_score
        << " opponent_score="
        << report.ancestral.opponent_score
        << " support="
        << report.ancestral.selected_support.size()
        << " information_action_fingerprint="
        << report.ancestral
               .information_action_fingerprint
        << '\n'
        << "descriptor_order model_count="
        << report.descriptor_order.model_count
        << " probe_count="
        << report.descriptor_order.probe_count
        << " action_scores_bit_identical="
        << report.descriptor_order
               .action_keyed_scores_bit_identical
        << " hidden_scores_bit_identical="
        << report.descriptor_order
               .hidden_action_keyed_scores_bit_identical
        << '\n'
        << "behavior live_force_spike="
        << report.behavior.live_force_spike_preserved
        << " five_open_force_spike_pass="
        << report.behavior
               .five_open_force_spike_selects_pass
        << " redundant_counter_pass="
        << report.behavior
               .redundant_counter_selects_pass
        << " intervening_counter="
        << report.behavior
               .intervening_counter_selects_opposing_counter
        << " sick_bear_growth_pass="
        << report.behavior
               .sick_bear_growth_selects_pass
        << " opponent_growth_excluded="
        << report.behavior.opponent_growth_excluded
        << " braingeyser_x0_excluded="
        << report.behavior.braingeyser_x_zero_excluded
        << '\n';
}

} // namespace old_school::action_q_multiscale_explore
