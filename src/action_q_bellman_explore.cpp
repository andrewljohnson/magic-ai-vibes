#include "old_school/action_q_bellman_explore.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/learned_iteration.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace old_school::action_q_bellman_explore {
namespace {

namespace teacher = action_q_bellman_teacher;

std::size_t deck_index(DeckId deck) {
    const std::size_t index = static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument(
            "AQ1 deck is outside the five-deck environment");
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
    throw std::invalid_argument("AQ1 source deck is invalid");
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ1 requires exact frozen Legacy C16");
    }
}

void add_macro(
    teacher::MacroAccounting& destination,
    const teacher::MacroAccounting& source) {
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

void add_teacher(
    teacher::TeacherAccounting& destination,
    const teacher::TeacherAccounting& source) {
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

void validate_teacher_accounting(
    const teacher::TeacherAccounting& accounting,
    std::size_t action_count) {
    if (accounting.root_actions != action_count ||
        !owner_partition_complete(
            accounting, action_count) ||
        accounting.root_determinizations !=
            teacher::kRootWorlds ||
        accounting.root_terminal_particles +
                accounting.root_boundary_particles !=
            action_count * teacher::kRootWorlds ||
        accounting.root_macros.transitions !=
            action_count * teacher::kRootWorlds ||
        accounting.root_macros.terminal_transitions +
                accounting.root_macros.boundary_transitions !=
            accounting.root_macros.transitions ||
        accounting.root_macros.critic_leaves != 0 ||
        accounting.successor_determinizations !=
            accounting.unique_successor_information_sets * 2 *
                teacher::kSuccessorWorlds ||
        accounting.successor_macros.transitions !=
            accounting.successor_actions * 2 *
                teacher::kSuccessorWorlds ||
        accounting.successor_macros.terminal_transitions +
                accounting.successor_macros.boundary_transitions !=
            accounting.successor_macros.transitions ||
        accounting.successor_macros.critic_leaves !=
            accounting.successor_macros.boundary_transitions ||
        accounting.unique_successor_information_sets >
            accounting.successor_group_occurrences) {
        throw std::invalid_argument(
            "AQ1 Bellman accounting cross-sum failed");
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
            "AQ1 retained root context is invalid");
    }
    const std::vector<PriorityAction> legal =
        legal_priority_actions(
            state, coordinate.actor,
            context.sorcery_actions);
    if (legal.size() < 2) {
        throw std::invalid_argument(
            "AQ1 retained root is not nontrivial");
    }
    const teacher::RootTargets targets =
        teacher::score_priority_root(
            state, decks, context, legal, parent,
            coordinate.search_seed);
    teacher::validate_root_targets(targets);
    if (targets.root_seed != coordinate.search_seed ||
        targets.root_owner != coordinate.actor ||
        targets.actions.size() != legal.size()) {
        throw std::logic_error(
            "AQ1 teacher result drifted from retained root");
    }

    RootExample result;
    result.coordinate = std::move(coordinate);
    result.information_set_fingerprint =
        targets.root_information_set_fingerprint;
    result.teacher_accounting = targets.accounting;
    for (const teacher::RootActionTarget& target :
         targets.actions) {
        result.action_descriptors.push_back(
            target.descriptor);
        result.actions.push_back(target.action);
        result.teacher_scores.push_back(target.value);
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
    add_teacher(
        deck.teacher_accounting,
        root.teacher_accounting);
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
                "AQ1 block omitted a metagame deck");
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

std::string census_payload(const Corpus& corpus) {
    std::ostringstream output;
    const auto append_string =
        [&output](std::string_view value) {
            output << value.size() << ':' << value;
        };
    const auto append_macro =
        [&output](
            const teacher::MacroAccounting& accounting) {
            output
                << accounting.transitions << ','
                << accounting.terminal_transitions << ','
                << accounting.boundary_transitions << ','
                << accounting.critic_leaves << ','
                << accounting.actions_applied << ','
                << accounting.priority_actions_applied << ','
                << accounting.phase_transitions << ','
                << accounting.turn_advances;
        };
    const auto append_teacher =
        [&output, &append_macro](
            const teacher::TeacherAccounting& accounting) {
            output
                << accounting.root_actions << ','
                << accounting.root_determinizations << ','
                << accounting.root_terminal_particles << ','
                << accounting.root_boundary_particles << ','
                << accounting.successor_group_occurrences << ','
                << accounting.same_owner_group_occurrences
                << ','
                << accounting.opponent_owner_group_occurrences
                << ','
                << accounting.same_owner_root_particles << ','
                << accounting.opponent_owner_root_particles << ','
                << accounting.unique_successor_information_sets
                << ',' << accounting.successor_actions << ','
                << accounting.successor_determinizations << ',';
            append_macro(accounting.root_macros);
            output << ',';
            append_macro(accounting.successor_macros);
        };
    output << corpus.root_seed << '|'
           << corpus.parent_fingerprint;
    const auto append_block =
        [&output, &append_string, &append_teacher](
            const CorpusBlock& block) {
            output << '|' << block.block
                   << '|' << block.census.games;
            for (const DeckCensus& deck :
                 block.census.decks) {
                const auto& t = deck.teacher_accounting;
                output
                    << '|' << deck.actor_games
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
                    << ',' << t.root_actions
                    << ',' << t.root_determinizations
                    << ',' << t.root_terminal_particles
                    << ',' << t.root_boundary_particles
                    << ',' << t.successor_group_occurrences
                    << ',' << t.same_owner_group_occurrences
                    << ',' << t.opponent_owner_group_occurrences
                    << ',' << t.same_owner_root_particles
                    << ',' << t.opponent_owner_root_particles
                    << ',' << t.unique_successor_information_sets
                    << ',' << t.successor_actions
                    << ',' << t.successor_determinizations
                    << ',' << t.root_macros.transitions
                    << ',' << t.root_macros.terminal_transitions
                    << ',' << t.root_macros.boundary_transitions
                    << ',' << t.root_macros.actions_applied
                    << ',' << t.root_macros.priority_actions_applied
                    << ',' << t.root_macros.phase_transitions
                    << ',' << t.root_macros.turn_advances
                    << ',' << t.successor_macros.transitions
                    << ',' << t.successor_macros.terminal_transitions
                    << ',' << t.successor_macros.boundary_transitions
                    << ',' << t.successor_macros.critic_leaves
                    << ',' << t.successor_macros.actions_applied
                    << ',' << t.successor_macros.priority_actions_applied
                    << ',' << t.successor_macros.phase_transitions
                    << ',' << t.successor_macros.turn_advances;
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
                    << ','
                    << c.actor_game_nontrivial_roots
                    << ',' << c.retained_position
                    << ','
                    << static_cast<std::size_t>(
                           c.seat_decks[0])
                    << ','
                    << static_cast<std::size_t>(
                           c.seat_decks[1])
                    << ',' << c.search_seed << ',';
                append_string(
                    root.information_set_fingerprint);
                output << ',' << root.action_descriptors.size();
                for (const std::string& descriptor :
                     root.action_descriptors) {
                    output << ',';
                    append_string(descriptor);
                }
                output << ',' << root.actions.size();
                for (const PriorityAction& action :
                     root.actions) {
                    output
                        << ','
                        << static_cast<std::size_t>(
                               action.kind)
                        << ','
                        << static_cast<std::size_t>(
                               action.card)
                        << ',' << action.target.has_value();
                    if (action.target.has_value()) {
                        output
                            << ',' << action.target->player
                            << ','
                            << action.target->creature
                                   .has_value();
                        if (action.target->creature.has_value()) {
                            output
                                << ','
                                << *action.target->creature;
                        }
                    }
                    output
                        << ','
                        << action.spell_target.has_value();
                    if (action.spell_target.has_value()) {
                        output << ',' << *action.spell_target;
                    }
                    output
                        << ','
                        << action.source_permanent.has_value();
                    if (action.source_permanent.has_value()) {
                        output << ','
                               << *action.source_permanent;
                    }
                    output << ',' << action.x_value;
                }
                output << ',' << root.options.size();
                for (const auto& option : root.options) {
                    output << ',' << option.size();
                    for (const double value : option) {
                        output << ','
                               << std::bit_cast<
                                      std::uint64_t>(
                                      value);
                    }
                }
                const auto append_doubles =
                    [&output](std::span<const double> values) {
                        output << ',' << values.size();
                        for (const double value : values) {
                            output << ','
                                   << std::bit_cast<
                                          std::uint64_t>(
                                          value);
                        }
                    };
                append_doubles(root.base_scores);
                append_doubles(root.teacher_scores);
                append_doubles(root.target_probabilities);
                output
                    << ','
                    << std::bit_cast<std::uint64_t>(
                           root.weight)
                    << ',' << root.base_sampled_worlds
                    << ',' << root.base_rollout_evaluations
                    << ',' << root.base_terminal_evaluations
                    << ','
                    << root.base_bootstrapped_evaluations
                    << ',';
                append_teacher(root.teacher_accounting);
            }
        };
    append_block(corpus.fit);
    append_block(corpus.check);
    return output.str();
}

} // namespace

DeckId RootExample::owner_deck() const {
    if (coordinate.actor >= 2) {
        throw std::out_of_range("AQ1 root actor is invalid");
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
        add_teacher(
            total.teacher_accounting,
            deck.teacher_accounting);
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
        << "Usage: old-school-action-q-bellman-explore "
           "(--census|--run)\n";
}

std::uint64_t root_search_seed(
    std::size_t block, std::size_t schedule_index,
    std::size_t actor, std::size_t nontrivial_ordinal) {
    if (actor >= 2 ||
        schedule_index >=
            learned_iteration::kBalancedScheduleGames) {
        throw std::invalid_argument(
            "AQ1 root seed coordinate is invalid");
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
        throw std::logic_error("AQ1 fit seed drifted");
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

bool owner_partition_complete(
    const teacher::TeacherAccounting& accounting,
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
            action_count * teacher::kRootWorlds;
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
            "AQ1 block identity/accounting is invalid");
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
                "AQ1 schedule coordinate is invalid");
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
            root.coordinate
                    .actor_game_nontrivial_roots == 0 ||
            actions < 2 ||
            root.action_descriptors.size() != actions ||
            root.options.size() != actions ||
            root.base_scores.size() != actions ||
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
                "AQ1 retained root is malformed");
        }
        validate_teacher_accounting(
            root.teacher_accounting, actions);
        double target_total = 0.0;
        for (std::size_t action = 0;
             action < actions; ++action) {
            if (root.options[action].size() !=
                    kPolicyFeatureCount ||
                !std::all_of(
                    root.options[action].begin(),
                    root.options[action].end(),
                    [](double value) {
                        return std::isfinite(value);
                    }) ||
                !probability(root.base_scores[action]) ||
                !probability(root.teacher_scores[action]) ||
                !probability(
                    root.target_probabilities[action])) {
                throw std::invalid_argument(
                    "AQ1 action row is malformed");
            }
            target_total +=
                root.target_probabilities[action];
        }
        if (std::abs(target_total - 1.0) > 1.0e-9 ||
            action_q_explore::teacher_distribution(
                root.teacher_scores) !=
                root.target_probabilities) {
            throw std::invalid_argument(
                "AQ1 target distribution drifted");
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
                "AQ1 actor-game retention count drifted");
        }
        deck.nontrivial_roots += total;
        for (std::size_t position = 0;
             position < roots.size(); ++position) {
            const RootCoordinate& coordinate =
                roots[position]->coordinate;
            if (coordinate.schedule_index !=
                    schedule_index ||
                coordinate.actor != actor ||
                coordinate
                        .actor_game_nontrivial_roots !=
                    total ||
                coordinate.retained_position !=
                    position ||
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
                    "AQ1 retained root coordinate drifted");
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
                "AQ1 per-deck census cross-sum failed");
        }
        const double expected_weight =
            1.0 /
            static_cast<double>(
                expected.retained_roots);
        for (const RootExample& root : block.roots) {
            if (deck_index(root.owner_deck()) == deck &&
                root.weight != expected_weight) {
                throw std::invalid_argument(
                    "AQ1 equal-deck root weight drifted");
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
            "AQ1 corpus identity is invalid");
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
            "AQ1 evaluation requires a model");
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
                "AQ1 metrics omitted a deck");
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
        report.check.candidate
                .equal_deck_mean_regret <
            report.check.parent
                .equal_deck_mean_regret;
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
    const auto find_blue =
        [](const probe_eval::ProbeMetricSummary& summary) {
            for (const auto& deck : summary.by_deck) {
                if (deck.root_deck == DeckId::Blue) {
                    return deck.mean_regret;
                }
            }
            return std::numeric_limits<double>::infinity();
        };
    const double parent_blue =
        find_blue(report.model.frozen_dev.parent);
    const double candidate_blue =
        find_blue(report.model.frozen_dev.candidate);
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
            "Bellman corpus accounting gate failed");
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
            "AQ1 census is not frozen in source: " +
            identity);
    }
    if (identity != kFrozenCensusIdentity) {
        throw std::runtime_error(
            "AQ1 census identity drifted from freeze");
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
            const auto& t = total.teacher_accounting;
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
                << " root_actions=" << t.root_actions
                << " root_determinizations="
                << t.root_determinizations
                << " root_terminal="
                << t.root_terminal_particles
                << " root_boundary="
                << t.root_boundary_particles
                << " successor_groups="
                << t.successor_group_occurrences
                << " same_owner_groups="
                << t.same_owner_group_occurrences
                << " opponent_owner_groups="
                << t.opponent_owner_group_occurrences
                << " same_owner_particles="
                << t.same_owner_root_particles
                << " opponent_owner_particles="
                << t.opponent_owner_root_particles
                << " successor_information_sets="
                << t.unique_successor_information_sets
                << " successor_actions="
                << t.successor_actions
                << " successor_determinizations="
                << t.successor_determinizations
                << " root_macro_transitions="
                << t.root_macros.transitions
                << " root_macro_actions="
                << t.root_macros.actions_applied
                << " root_macro_priority_actions="
                << t.root_macros.priority_actions_applied
                << " root_macro_phases="
                << t.root_macros.phase_transitions
                << " root_macro_turns="
                << t.root_macros.turn_advances
                << " successor_macro_transitions="
                << t.successor_macros.transitions
                << " successor_terminal_leaves="
                << t.successor_macros.terminal_transitions
                << " successor_critic_leaves="
                << t.successor_macros.critic_leaves
                << " successor_macro_actions="
                << t.successor_macros.actions_applied
                << " successor_macro_priority_actions="
                << t.successor_macros
                       .priority_actions_applied
                << " successor_macro_phases="
                << t.successor_macros.phase_transitions
                << " successor_macro_turns="
                << t.successor_macros.turn_advances
                << '\n';
            for (std::size_t index = 0;
                 index < kDeckCount; ++index) {
                const DeckCensus& deck =
                    block.census.decks[index];
                const auto& d = deck.teacher_accounting;
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
                    << " spread_roots="
                    << deck.nonzero_spread_roots
                    << " min_width="
                    << deck.minimum_legal_width
                    << " max_width="
                    << deck.maximum_legal_width
                    << " base_worlds="
                    << deck.base_sampled_worlds
                    << " base_rollouts="
                    << deck.base_rollout_evaluations
                    << " root_determinizations="
                    << d.root_determinizations
                    << " root_terminal="
                    << d.root_terminal_particles
                    << " root_boundary="
                    << d.root_boundary_particles
                    << " successor_groups="
                    << d.successor_group_occurrences
                    << " same_owner_groups="
                    << d.same_owner_group_occurrences
                    << " opponent_owner_groups="
                    << d.opponent_owner_group_occurrences
                    << " same_owner_particles="
                    << d.same_owner_root_particles
                    << " opponent_owner_particles="
                    << d.opponent_owner_root_particles
                    << " successor_information_sets="
                    << d.unique_successor_information_sets
                    << " successor_actions="
                    << d.successor_actions
                    << " successor_determinizations="
                    << d.successor_determinizations
                    << " terminal_leaves="
                    << d.successor_macros
                           .terminal_transitions
                    << " critic_leaves="
                    << d.successor_macros.critic_leaves
                    << " total_weight="
                    << std::setprecision(17)
                    << deck.root_weight << '\n';
            }
        };
    print_block("FIT", corpus.fit);
    print_block("CHECK", corpus.check);
    output << "census_identity="
           << canonical_census_identity(corpus) << '\n';
}

void print_model_gate_report(
    std::ostream& output,
    const action_q_offline_gate::ModelGateReport& report) {
    const auto print_probe_metrics =
        [&output](
            std::string_view policy,
            const probe_eval::ProbeMetricSummary& metrics) {
            output
                << std::setprecision(17)
                << "dev_metrics policy=" << policy
                << " probes=" << metrics.probe_count
                << " stable_pairs="
                << metrics.stable_pair_count
                << " top1_expected_agreement="
                << metrics.top1_expected_agreement
                << " stable_pair_agreement="
                << metrics.stable_pair_agreement
                << " mean_regret=" << metrics.mean_regret
                << " critic_brier=" << metrics.critic_brier
                << " critic_log_loss="
                << metrics.critic_log_loss
                << " critic_bias=" << metrics.critic_bias
                << " critic_ece=" << metrics.critic_ece
                << '\n';
            for (const auto& deck : metrics.by_deck) {
                output
                    << "dev_metrics_deck policy=" << policy
                    << " deck=" << deck_name(deck.root_deck)
                    << " probes=" << deck.probe_count
                    << " stable_pairs="
                    << deck.stable_pair_count
                    << " top1_expected_agreement="
                    << deck.top1_expected_agreement
                    << " stable_pair_agreement="
                    << deck.stable_pair_agreement
                    << " mean_regret="
                    << deck.mean_regret << '\n';
            }
        };
    const auto print_keys =
        [&output](
            std::string_view label,
            const std::vector<std::string>& keys) {
            output << label << '=';
            for (std::size_t index = 0;
                 index < keys.size(); ++index) {
                if (index != 0) {
                    output << ',';
                }
                output << keys[index];
            }
            output << '\n';
        };
    const auto print_actions =
        [&output](
            std::string_view label,
            const std::vector<PriorityAction>& actions) {
            output << label << '=';
            for (std::size_t index = 0;
                 index < actions.size(); ++index) {
                if (index != 0) {
                    output << ',';
                }
                output
                    << probes::stable_priority_action_descriptor(
                           actions[index]);
            }
            output << '\n';
        };

    print_probe_metrics("parent", report.frozen_dev.parent);
    print_probe_metrics(
        "candidate", report.frozen_dev.candidate);
    output
        << "frozen_dev labels=" << report.frozen_dev.labels
        << " stable_parent_agreements="
        << report.frozen_dev.stable_parent_agreements
        << " lost_stable_parent_agreements="
        << report.frozen_dev.lost_stable_parent_agreements
        << " pooled_regret_no_worse="
        << report.frozen_dev.pooled_regret_no_worse
        << " pair_hidden_passed="
        << report.frozen_dev.pair_hidden_repartition.passed
        << " pair_hidden_policies="
        << report.frozen_dev
               .pair_hidden_repartition.policy_count
        << " pair_hidden_probes="
        << report.frozen_dev
               .pair_hidden_repartition.probe_count
        << " explicit_hidden_passed="
        << report.frozen_dev
               .explicit_hidden_repartition.passed
        << " explicit_hidden_policies="
        << report.frozen_dev
               .explicit_hidden_repartition.policy_count
        << " explicit_hidden_probes="
        << report.frozen_dev
               .explicit_hidden_repartition.probe_count
        << " cache_before_bytes="
        << report.frozen_dev.cache_before.byte_size
        << " cache_before_sha256="
        << report.frozen_dev.cache_before.sha256
        << " cache_after_bytes="
        << report.frozen_dev.cache_after.byte_size
        << " cache_after_sha256="
        << report.frozen_dev.cache_after.sha256
        << " cache_unchanged="
        << (report.frozen_dev.cache_before ==
            report.frozen_dev.cache_after)
        << '\n';
    output << "frozen_dev_labels_by_deck";
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        output
            << ' '
            << deck_name(static_cast<DeckId>(deck))
            << '='
            << report.frozen_dev.labels_by_deck[deck];
    }
    output << '\n';

    output
        << "ancestral self_score="
        << report.ancestral.self_score
        << " opponent_score="
        << report.ancestral.opponent_score
        << " legal_action_count="
        << report.ancestral.legal_actions.size()
        << " support_size="
        << report.ancestral.selected_support.size()
        << " information_action_fingerprint="
        << report.ancestral.information_action_fingerprint
        << " complete_legal_actions_exact="
        << report.ancestral.complete_legal_actions_exact
        << " fingerprint_exact="
        << report.ancestral
               .information_action_fingerprint_exact
        << " hidden_repartition_bit_identical="
        << report.ancestral
               .hidden_repartition_bit_identical
        << " self_strictly_above_opponent="
        << report.ancestral.self_strictly_above_opponent
        << " opponent_absent_from_support="
        << report.ancestral.opponent_absent_from_support
        << '\n';
    print_actions(
        "ancestral_legal_actions",
        report.ancestral.legal_actions);
    print_actions(
        "ancestral_selected_support",
        report.ancestral.selected_support);

    output
        << "descriptor_order models="
        << report.descriptor_order.model_count
        << " probes="
        << report.descriptor_order.probe_count
        << " scores_bit_identical="
        << report.descriptor_order
               .action_keyed_scores_bit_identical
        << " supports_identical="
        << report.descriptor_order
               .selected_supports_identical
        << " hidden_models="
        << report.descriptor_order.hidden_model_count
        << " hidden_probes="
        << report.descriptor_order.hidden_probe_count
        << " hidden_distinct_owner_equivalent="
        << report.descriptor_order
               .hidden_repartitions_distinct_owner_equivalent
        << " hidden_scores_bit_identical="
        << report.descriptor_order
               .hidden_action_keyed_scores_bit_identical
        << " hidden_supports_identical="
        << report.descriptor_order
               .hidden_selected_supports_identical
        << '\n';

    const auto& spike = report.behavior.force_spike;
    output
        << "force_spike policy=" << spike.policy_name
        << " model_fingerprint="
        << spike.model_fingerprint
        << " worlds=" << spike.worlds
        << " horizon=" << spike.horizon_turns
        << " residual="
        << spike.value_priority_residual_weight
        << " pass_dominance="
        << spike.value_pass_dominance
        << " hidden_repartition_passed="
        << spike.hidden_repartition_passed
        << " live_pass_score=" << spike.live.pass_score
        << " live_force_spike_score="
        << spike.live.force_spike_score
        << " payable_pass_score="
        << spike.payable.pass_score
        << " payable_force_spike_score="
        << spike.payable.force_spike_score << '\n';
    print_keys(
        "force_spike_live_selection",
        spike.live.selected_keys);
    print_keys(
        "force_spike_payable_selection",
        spike.payable.selected_keys);
    output
        << "behavior live_force_spike="
        << report.behavior.live_force_spike_preserved
        << " one_open_payable_pass_descriptive="
        << report.behavior.one_open_payable_selects_pass
        << " five_open_payable_pass="
        << report.behavior
               .five_open_force_spike_selects_pass
        << " redundant_counter_pass="
        << report.behavior
               .redundant_counter_selects_pass
        << " intervening_counter_correct="
        << report.behavior
               .intervening_counter_selects_opposing_counter
        << " sick_bear_growth_pass="
        << report.behavior.sick_bear_growth_selects_pass
        << " opponent_growth_excluded="
        << report.behavior.opponent_growth_excluded
        << " braingeyser_x0_excluded="
        << report.behavior.braingeyser_x_zero_excluded
        << '\n';
    print_keys(
        "behavior_five_open_selection",
        report.behavior.five_open_selected_keys);
    print_keys(
        "behavior_redundant_counter_selection",
        report.behavior.redundant_counter_selected_keys);
    print_keys(
        "behavior_intervening_counter_selection",
        report.behavior.intervening_counter_selected_keys);
    print_keys(
        "behavior_sick_bear_growth_selection",
        report.behavior.sick_bear_growth_selected_keys);
    print_keys(
        "behavior_opponent_growth_selection",
        report.behavior.opponent_growth_selected_keys);
    print_keys(
        "behavior_braingeyser_selection",
        report.behavior.braingeyser_selected_keys);
}

} // namespace old_school::action_q_bellman_explore
