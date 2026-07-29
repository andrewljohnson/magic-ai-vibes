#include "old_school/action_q_explore.hpp"

#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace old_school::action_q_explore {
namespace {

static_assert(
    kTeacherTemperature ==
    learned_iteration::kP16ExplorationTemperature);
static_assert(
    kTeacherPrimaryWeight ==
    learned_iteration::kP16ExplorationTeacherWeight);

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::invalid_argument(
            "AQ0 deck is outside the five-deck environment");
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
    throw std::invalid_argument(
        "AQ0 source game has an invalid deck");
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent) {
        throw std::invalid_argument(
            "AQ0 requires a frozen C16 parent");
    }
    const std::string fingerprint =
        learned_model_fingerprint(parent);
    if (fingerprint != kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "AQ0 parent fingerprint does not match frozen C16");
    }
    if (learned_critic_schema(parent) !=
        LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ0 requires the frozen legacy C16 critic schema");
    }
}

double mean_probability(std::span<const double> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "AQ0 cannot average an empty sample row");
    }
    double total = 0.0;
    for (const double value : values) {
        if (!probability(value)) {
            throw std::invalid_argument(
                "AQ0 teacher sample is not a probability");
        }
        total += value;
    }
    const double mean =
        total / static_cast<double>(values.size());
    if (!probability(mean)) {
        throw std::logic_error(
            "AQ0 teacher mean is not a probability");
    }
    return mean;
}

GameConfig source_game_config(
    const std::shared_ptr<const LearnedModel>& parent,
    std::size_t starting_player) {
    const BotConfig bot{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = kWorlds,
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

LearnedSearchConfig root_search_config(
    std::uint64_t seed) {
    return {
        .seed = seed,
        .worlds = kWorlds,
        .rollouts_per_world = kRolloutsPerWorld,
        .horizon_turns = kHorizonTurns,
        .continuation_variant =
            LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = true,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
    };
}

RootExample build_root_example_impl(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    RootCoordinate coordinate,
    const std::shared_ptr<const LearnedModel>& parent,
    bool validate_frozen_parent);

void add_root_to_census(
    const RootExample& root, BlockCensus& census) {
    DeckCensus& deck =
        census.decks[deck_index(root.owner_deck())];
    ++deck.retained_roots;
    deck.retained_options += root.actions.size();
    deck.teacher_samples +=
        root.actions.size() * kWorlds;
    for (const auto& samples : root.teacher_samples) {
        deck.teacher_exact_zero_samples +=
            static_cast<std::size_t>(std::count(
                samples.begin(), samples.end(), 0.0));
        deck.teacher_exact_one_samples +=
            static_cast<std::size_t>(std::count(
                samples.begin(), samples.end(), 1.0));
    }
    const auto [minimum, maximum] =
        std::minmax_element(
            root.teacher_scores.begin(),
            root.teacher_scores.end());
    if (*maximum - *minimum > 0.0) {
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

    ++census.base_score_calls;
    census.base_sampled_worlds +=
        root.base_sampled_worlds;
    census.base_rollout_evaluations +=
        root.base_rollout_evaluations;
    census.base_terminal_evaluations +=
        root.base_terminal_evaluations;
    census.base_bootstrapped_evaluations +=
        root.base_bootstrapped_evaluations;
}

void validate_root(const RootExample& root) {
    const std::size_t action_count = root.actions.size();
    if (root.coordinate.actor >= 2 ||
        root.coordinate.starting_player >= 2 ||
        root.coordinate.schedule_index >=
            learned_iteration::kBalancedScheduleGames ||
        action_count < 2 ||
        root.options.size() != action_count ||
        root.base_scores.size() != action_count ||
        root.teacher_samples.size() != action_count ||
        root.teacher_scores.size() != action_count ||
        root.target_probabilities.size() != action_count ||
        root.base_sampled_worlds != kWorlds ||
        root.base_rollout_evaluations !=
            action_count * kWorlds *
                kRolloutsPerWorld ||
        root.base_terminal_evaluations +
                root.base_bootstrapped_evaluations !=
            root.base_rollout_evaluations ||
        !std::isfinite(root.weight) ||
        root.weight <= 0.0) {
        throw std::invalid_argument(
            "AQ0 root has invalid dimensions or coordinates");
    }
    static_cast<void>(deck_index(root.owner_deck()));
    if (root.coordinate.actor_game_nontrivial_roots != 0) {
        const auto retained =
            learned_iteration::
                evenly_spaced_retained_indices(
                    root.coordinate
                        .actor_game_nontrivial_roots,
                    kMaximumRootsPerActorGame);
        if (root.coordinate.retained_position >=
                retained.size() ||
            root.coordinate.nontrivial_ordinal !=
                retained[
                    root.coordinate
                        .retained_position]) {
            throw std::invalid_argument(
                "AQ0 root retention coordinate drifted");
        }
    }
    if (root.coordinate.search_seed !=
        root_search_seed(
            root.coordinate.block,
            root.coordinate.schedule_index,
            root.coordinate.actor,
            root.coordinate.nontrivial_ordinal)) {
        throw std::invalid_argument(
            "AQ0 root search seed drifted");
    }

    double target_total = 0.0;
    for (std::size_t action = 0;
         action < action_count; ++action) {
        if (root.options[action].size() !=
                kPolicyFeatureCount ||
            !std::all_of(
                root.options[action].begin(),
                root.options[action].end(),
                [](double value) {
                    return std::isfinite(value);
                }) ||
            !probability(root.base_scores[action]) ||
            root.teacher_samples[action].size() != kWorlds ||
            !probability(root.teacher_scores[action]) ||
            !probability(
                root.target_probabilities[action])) {
            throw std::invalid_argument(
                "AQ0 root contains an invalid action row");
        }
        if (mean_probability(root.teacher_samples[action]) !=
            root.teacher_scores[action]) {
            throw std::invalid_argument(
                "AQ0 teacher mean drifted from its samples");
        }
        target_total += root.target_probabilities[action];
    }
    if (std::abs(target_total - 1.0) > 1.0e-9 ||
        teacher_distribution(root.teacher_scores) !=
            root.target_probabilities) {
        throw std::invalid_argument(
            "AQ0 teacher distribution is invalid");
    }
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
        if (scheduled.starting_player >= 2 ||
            scheduled.schedule_index >= schedule.size()) {
            throw std::logic_error(
                "AQ0 balanced schedule is malformed");
        }
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

        for (std::size_t actor = 0;
             actor < 2; ++actor) {
            DeckCensus& deck_census =
                result.census.decks[
                    deck_index(
                        scheduled.seat_decks[actor])];
            ++deck_census.actor_games;

            struct Candidate {
                std::size_t trace_ordinal = 0;
                std::size_t nontrivial_ordinal = 0;
            };
            std::vector<Candidate> candidates;
            for (std::size_t trace_ordinal = 0;
                 trace_ordinal < trace.size();
                 ++trace_ordinal) {
                const LearnedDecisionTracePoint& point =
                    trace[trace_ordinal];
                if (!point.context.valid ||
                    point.context.decision_player != actor) {
                    continue;
                }
                const auto actions =
                    legal_priority_actions(
                        point.state, actor,
                        point.context.sorcery_actions);
                if (actions.size() < 2) {
                    continue;
                }
                candidates.push_back({
                    .trace_ordinal = trace_ordinal,
                    .nontrivial_ordinal =
                        candidates.size(),
                });
            }
            deck_census.nontrivial_roots +=
                candidates.size();

            const auto retained =
                learned_iteration::
                    evenly_spaced_retained_indices(
                        candidates.size(),
                        kMaximumRootsPerActorGame);
            for (std::size_t retained_position = 0;
                 retained_position < retained.size();
                 ++retained_position) {
                const std::size_t retained_index =
                    retained[retained_position];
                const Candidate& selected =
                    candidates[retained_index];
                const LearnedDecisionTracePoint& point =
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
                    .retained_position =
                        retained_position,
                    .seat_decks =
                        scheduled.seat_decks,
                    .search_seed =
                        root_search_seed(
                            block,
                            scheduled.schedule_index,
                            actor,
                            selected
                                .nontrivial_ordinal),
                };

                RootExample root =
                    build_root_example_impl(
                        point.state, decks,
                        point.context,
                        std::move(coordinate), parent,
                        false);
                add_root_to_census(
                    root, result.census);
                result.roots.push_back(
                    std::move(root));
            }
        }
    }

    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const std::size_t roots =
            result.census.decks[deck].retained_roots;
        if (roots == 0) {
            throw std::logic_error(
                "AQ0 block has no retained root for a deck");
        }
        const double weight =
            1.0 / static_cast<double>(roots);
        result.census.decks[deck].root_weight = 1.0;
        for (RootExample& root : result.roots) {
            if (deck_index(root.owner_deck()) == deck) {
                root.weight = weight;
            }
        }
    }
    validate_block(result);
    return result;
}

} // namespace

DeckId RootExample::owner_deck() const {
    if (coordinate.actor >= coordinate.seat_decks.size()) {
        throw std::out_of_range(
            "AQ0 root actor is outside its seat map");
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

std::size_t BlockCensus::teacher_sample_count() const {
    return std::accumulate(
        decks.begin(), decks.end(), std::size_t{0},
        [](std::size_t total, const DeckCensus& deck) {
            return total + deck.teacher_samples;
        });
}

std::vector<double> teacher_distribution(
    std::span<const double> teacher_scores) {
    if (teacher_scores.empty() ||
        !std::all_of(
            teacher_scores.begin(), teacher_scores.end(),
            [](double score) {
                return probability(score);
            })) {
        throw std::invalid_argument(
            "AQ0 teacher scores must be probabilities");
    }
    return learned_iteration::
        p16_exploration_distribution(teacher_scores);
}

std::vector<std::size_t> exact_max_support(
    std::span<const double> scores) {
    if (scores.empty() ||
        !std::all_of(
            scores.begin(), scores.end(),
            [](double score) {
                return std::isfinite(score);
            })) {
        throw std::invalid_argument(
            "AQ0 support requires finite scores");
    }
    const double maximum =
        *std::max_element(
            scores.begin(), scores.end());
    std::vector<std::size_t> support;
    for (std::size_t index = 0;
         index < scores.size(); ++index) {
        if (scores[index] == maximum) {
            support.push_back(index);
        }
    }
    return support;
}

std::vector<double> combined_scores(
    std::span<const double> base_scores,
    std::span<const double> policy_logits,
    double residual_weight) {
    if (base_scores.empty() ||
        base_scores.size() != policy_logits.size() ||
        !std::isfinite(residual_weight) ||
        residual_weight < 0.0 ||
        residual_weight > 1.0 ||
        !std::all_of(
            base_scores.begin(), base_scores.end(),
            [](double score) {
                return probability(score);
            }) ||
        !std::all_of(
            policy_logits.begin(), policy_logits.end(),
            [](double logit) {
                return std::isfinite(logit);
            })) {
        throw std::invalid_argument(
            "AQ0 combined-score inputs are invalid");
    }

    double logit_total = 0.0;
    for (const double logit : policy_logits) {
        logit_total += logit;
    }
    const double mean_logit =
        logit_total /
        static_cast<double>(policy_logits.size());

    std::vector<double> result;
    result.reserve(base_scores.size());
    for (std::size_t action = 0;
         action < base_scores.size(); ++action) {
        result.push_back(
            base_scores[action] +
            residual_weight *
                std::tanh(
                    policy_logits[action] -
                    mean_logit));
    }
    return result;
}

RootMetrics evaluate_root(
    std::span<const double> teacher_scores,
    std::span<const double> policy_scores) {
    if (teacher_scores.empty() ||
        teacher_scores.size() !=
            policy_scores.size() ||
        !std::all_of(
            teacher_scores.begin(),
            teacher_scores.end(),
            [](double score) {
                return probability(score);
            })) {
        throw std::invalid_argument(
            "AQ0 root metrics require aligned teacher probabilities");
    }

    RootMetrics result;
    result.action_count = teacher_scores.size();
    result.teacher_support =
        exact_max_support(teacher_scores);
    result.policy_support =
        exact_max_support(policy_scores);

    std::size_t overlap = 0;
    double selected_teacher_total = 0.0;
    for (const std::size_t selected :
         result.policy_support) {
        selected_teacher_total +=
            teacher_scores[selected];
        overlap +=
            std::find(
                result.teacher_support.begin(),
                result.teacher_support.end(),
                selected) !=
                    result.teacher_support.end()
                ? 1
                : 0;
    }
    result.top_one_expected_agreement =
        static_cast<double>(overlap) /
        static_cast<double>(
            result.policy_support.size());
    const double selected_teacher_mean =
        selected_teacher_total /
        static_cast<double>(
            result.policy_support.size());
    const double best_teacher =
        teacher_scores[
            result.teacher_support.front()];
    result.regret =
        best_teacher -
        selected_teacher_mean;
    if (result.regret < 0.0 &&
        result.regret >
            -32.0 *
                std::numeric_limits<double>::epsilon()) {
        result.regret = 0.0;
    }
    if (!std::isfinite(result.regret) ||
        result.regret < 0.0) {
        throw std::logic_error(
            "AQ0 root regret is invalid");
    }
    return result;
}

std::uint64_t root_search_seed(
    std::size_t block, std::size_t schedule_index,
    std::size_t actor, std::size_t nontrivial_ordinal) {
    if (actor >= 2 ||
        schedule_index >=
            learned_iteration::kBalancedScheduleGames) {
        throw std::invalid_argument(
            "AQ0 root seed coordinate is invalid");
    }
    return learned_iteration::derive_seed(
        kCollectionRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        block,
        schedule_index * 2 + actor,
        nontrivial_ordinal);
}

std::uint64_t fit_seed() {
    const std::uint64_t derived =
        learned_iteration::derive_seed(
        kCollectionRootSeed,
        learned_iteration::SeedDomain::PolicyFit,
        0, 0, 0);
    if (derived != kFitSeed) {
        throw std::logic_error(
            "AQ0 derived fit seed drifted");
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
        .residual_weight =
            kCandidateResidualWeight,
        .policy_temperature =
            kTeacherTemperature,
    };
}

namespace {

RootExample build_root_example_impl(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    RootCoordinate coordinate,
    const std::shared_ptr<const LearnedModel>& parent,
    bool validate_frozen_parent) {
    if (validate_frozen_parent) {
        require_parent(parent);
    } else if (!parent) {
        throw std::invalid_argument(
            "AQ0 root scoring requires a model");
    }
    if (!context.valid ||
        coordinate.actor >= 2 ||
        context.decision_player != coordinate.actor ||
        context.consecutive_passes < 0 ||
        context.consecutive_passes > 1 ||
        coordinate.search_seed !=
            root_search_seed(
                coordinate.block,
                coordinate.schedule_index,
                coordinate.actor,
                coordinate.nontrivial_ordinal)) {
        throw std::invalid_argument(
            "AQ0 root context or coordinate is invalid");
    }

    RootExample result;
    result.coordinate = std::move(coordinate);
    result.actions =
        legal_priority_actions(
            state, result.coordinate.actor,
            context.sorcery_actions);
    if (result.actions.size() < 2) {
        throw std::invalid_argument(
            "AQ0 requires a nontrivial Priority root");
    }

    result.options.reserve(result.actions.size());
    for (const PriorityAction& action :
         result.actions) {
        result.options.push_back(
            learned_priority_policy_features(
                state, result.coordinate.actor,
                action, context.sorcery_actions,
                context.phase,
                context.consecutive_passes));
    }

    const LearnedActionSamples base =
        learned_priority_action_samples(
            state, original_decks,
            result.coordinate.actor,
            context.sorcery_actions,
            context.phase,
            context.consecutive_passes,
            result.actions, parent,
            root_search_config(
                result.coordinate.search_seed));
    if (base.sampled_worlds != kWorlds ||
        base.rollout_evaluations !=
            result.actions.size() *
                kWorlds *
                kRolloutsPerWorld ||
        base.terminal_evaluations +
                base.bootstrapped_evaluations !=
            base.rollout_evaluations ||
        base.exact_priority_aggregate_scores.size() !=
            result.actions.size()) {
        throw std::logic_error(
            "AQ0 base scorer accounting drifted");
    }
    result.base_scores =
        base.exact_priority_aggregate_scores;
    result.base_sampled_worlds =
        base.sampled_worlds;
    result.base_rollout_evaluations =
        base.rollout_evaluations;
    result.base_terminal_evaluations =
        base.terminal_evaluations;
    result.base_bootstrapped_evaluations =
        base.bootstrapped_evaluations;

    result.teacher_samples.assign(
        result.actions.size(),
        std::vector<double>(kWorlds));
    for (std::size_t world = 0;
         world < kWorlds; ++world) {
        const GameState sampled =
            sample_determinization(
                state, original_decks,
                result.coordinate.actor,
                learned_search_world_seed(
                    result.coordinate.search_seed,
                    world));
        for (std::size_t action = 0;
             action < result.actions.size();
             ++action) {
            const auto consequence =
                resolve_priority_action_consequence(
                    sampled,
                    result.coordinate.actor,
                    context.sorcery_actions,
                    context.consecutive_passes,
                    result.actions[action]);
            if (!consequence.has_value()) {
                throw std::logic_error(
                    "AQ0 legal action had no resolved consequence");
            }
            double teacher = 0.5;
            if (consequence->terminal) {
                teacher =
                    learned_iteration::
                        terminal_value_for_perspective(
                            consequence->winner,
                            result.coordinate.actor);
            } else {
                teacher =
                    learned_critic_value(
                        consequence->state,
                        result.coordinate.actor,
                        parent);
            }
            if (!probability(teacher)) {
                throw std::logic_error(
                    "AQ0 resolved teacher is not a probability");
            }
            result.teacher_samples[action][world] =
                teacher;
        }
    }
    result.teacher_scores.reserve(
        result.actions.size());
    for (const auto& samples :
         result.teacher_samples) {
        result.teacher_scores.push_back(
            mean_probability(samples));
    }
    result.target_probabilities =
        teacher_distribution(
            result.teacher_scores);
    // Collection assigns the inverse within-deck root count only after the
    // complete block census is frozen.
    result.weight = 1.0;
    validate_root(result);
    return result;
}

} // namespace

RootExample build_root_example(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    RootCoordinate coordinate,
    std::shared_ptr<const LearnedModel> parent) {
    return build_root_example_impl(
        state, original_decks, context,
        std::move(coordinate), parent, true);
}

void validate_block(const CorpusBlock& block) {
    if (block.block != block.census.block ||
        (block.block != kFitBlock &&
         block.block != kCheckBlock) ||
        block.census.games !=
            learned_iteration::kBalancedScheduleGames ||
        block.census.base_score_calls !=
            block.roots.size() ||
        block.census.base_sampled_worlds !=
            block.roots.size() * kWorlds ||
        block.census.base_rollout_evaluations !=
            block.census.retained_options() *
                kWorlds *
                kRolloutsPerWorld ||
        block.census.base_terminal_evaluations +
                block.census.base_bootstrapped_evaluations !=
            block.census.base_rollout_evaluations ||
        block.census.retained_roots() !=
            block.roots.size() ||
        block.census.teacher_sample_count() !=
            block.census.retained_options() *
                kWorlds) {
        throw std::invalid_argument(
            "AQ0 block census cross-sums failed");
    }

    const auto schedule =
        learned_iteration::balanced_schedule(
            kCollectionRootSeed,
            kScheduleGeneration, block.block);
    std::array<
        std::vector<const RootExample*>,
        learned_iteration::kBalancedScheduleGames * 2>
        actor_game_roots;
    std::array<DeckCensus, kDeckCount> observed{};
    for (const RootExample& root : block.roots) {
        validate_root(root);
        if (root.coordinate.block != block.block) {
            throw std::invalid_argument(
                "AQ0 root is in the wrong block");
        }
        const auto& expected_game =
            schedule[root.coordinate.schedule_index];
        if (root.coordinate.actor_game_nontrivial_roots == 0 ||
            root.coordinate.pairing_index !=
                expected_game.pairing_index ||
            root.coordinate.game_seed !=
                expected_game.seed ||
            root.coordinate.starting_player !=
                expected_game.starting_player ||
            root.coordinate.seat_decks !=
                expected_game.seat_decks) {
            throw std::invalid_argument(
                "AQ0 root source-game provenance drifted");
        }
        actor_game_roots[
            root.coordinate.schedule_index * 2 +
            root.coordinate.actor]
            .push_back(&root);
        DeckCensus& deck =
            observed[deck_index(root.owner_deck())];
        ++deck.retained_roots;
        deck.retained_options += root.actions.size();
        deck.teacher_samples +=
            root.actions.size() * kWorlds;
        for (const auto& samples :
             root.teacher_samples) {
            deck.teacher_exact_zero_samples +=
                static_cast<std::size_t>(
                    std::count(
                        samples.begin(),
                        samples.end(), 0.0));
            deck.teacher_exact_one_samples +=
                static_cast<std::size_t>(
                    std::count(
                        samples.begin(),
                        samples.end(), 1.0));
        }
        const auto [minimum, maximum] =
            std::minmax_element(
                root.teacher_scores.begin(),
                root.teacher_scores.end());
        if (*maximum - *minimum > 0.0) {
            ++deck.nonzero_spread_roots;
        }
        if (deck.minimum_legal_width == 0) {
            deck.minimum_legal_width =
                root.actions.size();
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

    for (std::size_t actor_game = 0;
         actor_game < actor_game_roots.size();
         ++actor_game) {
        const auto& roots =
            actor_game_roots[actor_game];
        if (roots.empty()) {
            continue;
        }
        const std::size_t total =
            roots.front()
                ->coordinate
                .actor_game_nontrivial_roots;
        const auto expected_ordinals =
            learned_iteration::
                evenly_spaced_retained_indices(
                    total,
                    kMaximumRootsPerActorGame);
        if (roots.size() !=
            expected_ordinals.size()) {
            throw std::invalid_argument(
                "AQ0 actor-game retention count drifted");
        }
        const std::size_t schedule_index =
            actor_game / 2;
        const std::size_t actor =
            actor_game % 2;
        DeckCensus& deck =
            observed[deck_index(
                schedule[schedule_index]
                    .seat_decks[actor])];
        deck.nontrivial_roots += total;
        for (std::size_t position = 0;
             position < roots.size();
             ++position) {
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
                    expected_ordinals[position] ||
                (position > 0 &&
                 (coordinate.trace_ordinal <=
                      roots[position - 1]
                          ->coordinate
                          .trace_ordinal ||
                  coordinate.nontrivial_ordinal <=
                      roots[position - 1]
                          ->coordinate
                          .nontrivial_ordinal))) {
                throw std::invalid_argument(
                    "AQ0 actor-game root order or identity drifted");
            }
        }
    }

    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const DeckCensus& expected =
            block.census.decks[deck];
        const DeckCensus& actual =
            observed[deck];
        if (expected.actor_games != 16 ||
            expected.nontrivial_roots !=
                actual.nontrivial_roots ||
            expected.retained_roots == 0 ||
            expected.retained_roots !=
                actual.retained_roots ||
            expected.retained_options !=
                actual.retained_options ||
            expected.teacher_samples !=
                actual.teacher_samples ||
            expected.teacher_exact_zero_samples !=
                actual.teacher_exact_zero_samples ||
            expected.teacher_exact_one_samples !=
                actual.teacher_exact_one_samples ||
            expected.nonzero_spread_roots !=
                actual.nonzero_spread_roots ||
            expected.minimum_legal_width !=
                actual.minimum_legal_width ||
            expected.maximum_legal_width !=
                actual.maximum_legal_width ||
            expected.root_weight != 1.0) {
            throw std::invalid_argument(
                "AQ0 per-deck census is inconsistent");
        }
        const double expected_weight =
            1.0 /
            static_cast<double>(
                expected.retained_roots);
        for (const RootExample& root :
             block.roots) {
            if (deck_index(root.owner_deck()) == deck &&
                root.weight != expected_weight) {
                throw std::invalid_argument(
                    "AQ0 root weight is not inverse deck count");
            }
        }
    }
}

void validate_corpus(const Corpus& corpus) {
    if (corpus.root_seed !=
            kCollectionRootSeed ||
        corpus.parent_fingerprint !=
            kRequiredParentFingerprint ||
        corpus.fit.block != kFitBlock ||
        corpus.check.block != kCheckBlock) {
        throw std::invalid_argument(
            "AQ0 corpus identity is invalid");
    }
    validate_block(corpus.fit);
    validate_block(corpus.check);
}

Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    Corpus result;
    result.parent_fingerprint =
        learned_model_fingerprint(parent);
    result.fit =
        collect_block(kFitBlock, parent);
    result.check =
        collect_block(kCheckBlock, parent);
    validate_corpus(result);
    return result;
}

Metrics evaluate(
    const CorpusBlock& block,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight) {
    if (!model) {
        throw std::invalid_argument(
            "AQ0 evaluation requires a model");
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
            combined_scores(
                root.base_scores, logits,
                residual_weight);
        const RootMetrics metrics =
            evaluate_root(
                root.teacher_scores, scores);
        Accumulator& accumulator =
            accumulators[
                deck_index(root.owner_deck())];
        ++accumulator.roots;
        accumulator.options +=
            metrics.action_count;
        accumulator.agreement +=
            metrics.top_one_expected_agreement;
        accumulator.regret +=
            metrics.regret;
    }

    Metrics result;
    double deck_agreement_total = 0.0;
    double deck_regret_total = 0.0;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const Accumulator& accumulator =
            accumulators[deck];
        if (accumulator.roots == 0) {
            throw std::logic_error(
                "AQ0 evaluation is missing a deck");
        }
        DeckMetrics& metrics =
            result.decks[deck];
        metrics.deck =
            static_cast<DeckId>(deck);
        metrics.roots = accumulator.roots;
        metrics.options = accumulator.options;
        metrics.top_one_expected_agreement =
            accumulator.agreement /
            static_cast<double>(
                accumulator.roots);
        metrics.mean_regret =
            accumulator.regret /
            static_cast<double>(
                accumulator.roots);
        result.roots += accumulator.roots;
        result.options += accumulator.options;
        deck_agreement_total +=
            metrics.top_one_expected_agreement;
        deck_regret_total +=
            metrics.mean_regret;
    }
    result.equal_deck_top_one_expected_agreement =
        deck_agreement_total /
        static_cast<double>(kDeckCount);
    result.equal_deck_mean_regret =
        deck_regret_total /
        static_cast<double>(kDeckCount);
    return result;
}

FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    validate_corpus(corpus);
    if (learned_model_fingerprint(parent) !=
        corpus.parent_fingerprint) {
        throw std::invalid_argument(
            "AQ0 corpus and fit parent differ");
    }

    FitReport report;
    report.parent_fingerprint_before =
        learned_model_fingerprint(parent);
    report.parent_components =
        learned_model_component_fingerprints(parent);
    report.optimizer = optimizer_config();
    report.fit_examples = corpus.fit.roots.size();

    std::vector<LearnedValuePriorityTrainingExample>
        examples;
    examples.reserve(corpus.fit.roots.size());
    for (const RootExample& root :
         corpus.fit.roots) {
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
        learned_model_fingerprint(
            report.candidate);
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

    // Exact C16 is deployed with zero Priority residual. AQ0 is evaluated
    // with its declared bounded 0.10 fitted residual.
    report.parent_fit =
        evaluate(corpus.fit, parent, 0.0);
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

} // namespace old_school::action_q_explore
