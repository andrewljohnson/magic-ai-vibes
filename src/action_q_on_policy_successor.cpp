#include "old_school/action_q_on_policy_successor.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <ostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::action_q_on_policy_successor {
namespace {

constexpr std::string_view kManifestSchema =
    "old-school-action-q-aq4-op1-census-v1";
constexpr std::string_view kInformationActionSchema =
    "old-school-action-q-aq4-op1-owner-information-action-v1";
constexpr std::string_view kStableRootSchema =
    "old-school-action-q-aq4-op1-stable-root-v1";
constexpr std::string_view kCorpusSchema =
    "old-school-action-q-aq4-op1-corpus-v1";

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::out_of_range(
            "AQ4-OP1 deck index is invalid");
    }
    return result;
}

void require_warm_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredWarmParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ4-OP1 requires the exact frozen G4B warm parent");
    }
}

bool finite_probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

void append_u64(std::string& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>(
            (value >> shift) & 0xffU));
    }
}

void append_size(std::string& output, std::size_t value) {
    append_u64(output, static_cast<std::uint64_t>(value));
}

void append_string(
    std::string& output, std::string_view value) {
    append_size(output, value.size());
    output.append(value);
}

void append_double(std::string& output, double value) {
    append_u64(output, std::bit_cast<std::uint64_t>(value));
}

void append_bool(std::string& output, bool value) {
    append_u64(output, value ? 1U : 0U);
}

void append_optional_u64(
    std::string& output,
    const std::optional<std::uint64_t>& value) {
    append_bool(output, value.has_value());
    if (value.has_value()) {
        append_u64(output, *value);
    }
}

void append_action(
    std::string& output, const PriorityAction& action) {
    append_u64(
        output,
        static_cast<std::uint64_t>(action.kind));
    append_u64(
        output,
        static_cast<std::uint64_t>(action.card));
    append_bool(output, action.target.has_value());
    if (action.target.has_value()) {
        append_size(output, action.target->player);
        append_optional_u64(
            output, action.target->creature);
    }
    append_optional_u64(output, action.spell_target);
    append_optional_u64(output, action.source_permanent);
    append_u64(
        output,
        std::bit_cast<std::uint64_t>(
            static_cast<std::int64_t>(action.x_value)));
}

void append_coordinate(
    std::string& output,
    const RootCoordinate& coordinate) {
    append_size(output, kScheduleGeneration);
    append_u64(
        output,
        static_cast<std::uint64_t>(coordinate.split));
    append_size(output, coordinate.schedule_index);
    append_size(output, coordinate.pairing_index);
    append_u64(output, coordinate.game_seed);
    append_size(output, coordinate.starting_player);
    for (const DeckId deck : coordinate.seat_decks) {
        append_u64(
            output, static_cast<std::uint64_t>(deck));
    }
    append_size(output, coordinate.actor);
    append_size(output, coordinate.trace_ordinal);
    append_size(output, coordinate.nontrivial_ordinal);
    append_size(
        output,
        coordinate.actor_game_nontrivial_roots);
    append_size(output, coordinate.retained_position);
    append_size(
        output, coordinate.actor_game_retained_roots);
    append_u64(output, coordinate.search_seed);
}

void append_manifest_root(
    std::string& output, const ManifestRoot& root) {
    append_coordinate(output, root.coordinate);
    append_string(output, root.stable_root_id);
    append_string(
        output, root.information_action_fingerprint);
    append_size(output, root.actions.size());
    for (std::size_t action = 0;
         action < root.actions.size(); ++action) {
        append_action(output, root.actions[action]);
        append_string(
            output, root.action_descriptors[action]);
        append_size(output, root.options[action].size());
        for (const double feature : root.options[action]) {
            append_double(output, feature);
        }
    }
}

void append_components(
    std::string& output,
    const LearnedModelComponentFingerprints& components) {
    append_string(output, components.critic);
    append_string(output, components.priority);
    append_string(output, components.attack);
    append_string(output, components.block);
    append_string(output, components.damage_order);
}

void append_optimizer(
    std::string& output,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    append_size(output, optimizer.batch_size);
    append_size(output, optimizer.epochs);
    append_double(output, optimizer.learning_rate);
    append_double(output, optimizer.beta1);
    append_double(output, optimizer.beta2);
    append_double(output, optimizer.epsilon);
    append_double(
        output, optimizer.global_gradient_norm_clip);
    append_u64(output, optimizer.seed);
    append_double(output, optimizer.residual_weight);
    append_double(output, optimizer.policy_temperature);
}

void append_search_config(
    std::string& output,
    const LearnedSearchConfig& config) {
    append_u64(output, config.seed);
    append_size(output, config.worlds);
    append_size(output, config.rollouts_per_world);
    append_size(output, config.horizon_turns);
    append_u64(
        output,
        static_cast<std::uint64_t>(
            config.continuation_variant));
    append_double(
        output, config.value_continuation_epsilon);
    append_bool(output, config.blend_shallow_prior);
    append_double(
        output,
        config.value_resolved_shallow_prior_weight);
    append_double(
        output,
        config.value_priority_residual_weight);
    append_bool(output, config.value_pass_dominance);
    append_u64(
        output,
        static_cast<std::uint64_t>(
            config.value_continuation_controller));
    append_size(output, config.evaluation_threads);
    append_bool(
        output, config.capture_priority_h0_boundaries);
    append_size(
        output, config.value_continuation_search_worlds);
}

void append_example(
    std::string& output, const RootExample& example) {
    append_manifest_root(output, example.manifest);
    const auto append_values =
        [&](std::span<const double> values) {
            append_size(output, values.size());
            for (const double value : values) {
                append_double(output, value);
            }
        };
    append_values(example.base_scores);
    append_values(example.teacher_scores);
    append_values(example.target_probabilities);
    const RootAccounting& accounting = example.accounting;
    append_size(output, accounting.base_sampled_worlds);
    append_size(output, accounting.base_rollout_evaluations);
    append_size(output, accounting.base_terminal_evaluations);
    append_size(
        output, accounting.base_bootstrapped_evaluations);
    append_size(output, accounting.teacher_sampled_worlds);
    append_size(
        output, accounting.teacher_rollout_evaluations);
    append_size(
        output, accounting.teacher_terminal_evaluations);
    append_size(
        output, accounting.teacher_bootstrapped_evaluations);
    append_size(
        output,
        accounting.teacher_inner_rollout_evaluations);
    append_size(
        output,
        accounting.teacher_inner_search_invocations);
    append_size(
        output,
        accounting.teacher_inner_search_max_depth);
    append_double(output, example.weight);
}

std::string information_action_fingerprint(
    const ManifestRoot& root) {
    std::string payload;
    append_string(payload, kInformationActionSchema);
    append_size(payload, kScheduleGeneration);
    append_size(payload, root.actions.size());
    for (std::size_t action = 0;
         action < root.actions.size(); ++action) {
        append_action(payload, root.actions[action]);
        append_string(
            payload, root.action_descriptors[action]);
        append_size(payload, root.options[action].size());
        for (const double feature : root.options[action]) {
            append_double(payload, feature);
        }
    }
    return artifact_integrity::sha256_string(payload);
}

std::string stable_root_id(const ManifestRoot& root) {
    std::string payload;
    append_string(payload, kStableRootSchema);
    append_coordinate(payload, root.coordinate);
    append_string(
        payload, root.information_action_fingerprint);
    return artifact_integrity::sha256_string(payload);
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
        "AQ4-OP1 source deck is invalid");
}

const std::array<
    learned_iteration::ScheduledGame, kGamesPerSplit>&
schedule_for(Split split) {
    static const auto train =
        learned_iteration::balanced_schedule(
            kCollectionRootSeed,
            kScheduleGeneration, kTrainBlock);
    static const auto dev =
        learned_iteration::balanced_schedule(
            kCollectionRootSeed,
            kScheduleGeneration, kDevBlock);
    return split == Split::Train ? train : dev;
}

ManifestRoot make_manifest_root(
    const LearnedDecisionTracePoint& point,
    const learned_iteration::ScheduledGame& scheduled,
    Split split, std::size_t actor,
    std::size_t trace_ordinal,
    std::size_t nontrivial_ordinal,
    std::size_t nontrivial_roots,
    std::size_t retained_position,
    std::size_t retained_roots) {
    if (!point.context.valid ||
        point.context.decision_player != actor ||
        retained_roots == 0) {
        throw std::logic_error(
            "AQ4-OP1 retained source context is invalid");
    }
    ManifestRoot root;
    root.coordinate = {
        .split = split,
        .schedule_index = scheduled.schedule_index,
        .pairing_index = scheduled.pairing_index,
        .game_seed = scheduled.seed,
        .starting_player = scheduled.starting_player,
        .seat_decks = scheduled.seat_decks,
        .actor = actor,
        .trace_ordinal = trace_ordinal,
        .nontrivial_ordinal = nontrivial_ordinal,
        .actor_game_nontrivial_roots = nontrivial_roots,
        .retained_position = retained_position,
        .actor_game_retained_roots = retained_roots,
        .search_seed =
            action_q_on_policy_successor::root_search_seed(
                split, scheduled.schedule_index,
                actor, nontrivial_ordinal),
    };
    root.actions =
        legal_priority_actions(
            point.state, actor,
            point.context.sorcery_actions);
    if (root.actions.size() < 2) {
        throw std::logic_error(
            "AQ4-OP1 retained a trivial Priority root");
    }
    root.action_descriptors.reserve(root.actions.size());
    root.options.reserve(root.actions.size());
    for (const PriorityAction& action : root.actions) {
        root.action_descriptors.push_back(
            probes::stable_priority_action_descriptor(action));
        root.options.push_back(
            learned_priority_policy_features(
                point.state, actor, action,
                point.context.sorcery_actions,
                point.context.phase,
                point.context.consecutive_passes));
    }
    root.information_action_fingerprint =
        information_action_fingerprint(root);
    root.stable_root_id = stable_root_id(root);
    return root;
}

template <typename Visitor>
Census visit_source_roots(
    const std::shared_ptr<const LearnedModel>& warm_parent,
    Visitor&& visitor) {
    require_warm_parent(warm_parent);
    Census census;
    census.root_seed = kCollectionRootSeed;
    census.parent_fingerprint =
        learned_model_fingerprint(warm_parent);
    census.splits = {
        SplitCensus{.split = Split::Train},
        SplitCensus{.split = Split::Dev},
    };

    for (const Split split :
         std::array<Split, 2>{
             Split::Train, Split::Dev}) {
        SplitCensus& split_census =
            census.splits[
                action_q_on_policy_successor::split_index(
                    split)];
        const auto& schedule = schedule_for(split);
        for (const auto& scheduled : schedule) {
            const std::array<std::vector<CardId>, 2> decks{
                cards_for_deck(scheduled.seat_decks[0]),
                cards_for_deck(scheduled.seat_decks[1]),
            };
            Game game(
                decks[0], decks[1], scheduled.seed,
                source_game_config(
                    warm_parent,
                    scheduled.starting_player));
            std::vector<LearnedDecisionTracePoint> trace;
            static_cast<void>(
                game.run_with_priority_root_trace(trace));
            ++split_census.games;

            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                struct Candidate {
                    std::size_t trace_ordinal = 0;
                    std::size_t nontrivial_ordinal = 0;
                };
                std::vector<Candidate> candidates;
                for (std::size_t trace_ordinal = 0;
                     trace_ordinal < trace.size();
                     ++trace_ordinal) {
                    const auto& point = trace[trace_ordinal];
                    if (!point.context.valid ||
                        point.context.decision_player != actor ||
                        legal_priority_actions(
                            point.state, actor,
                            point.context.sorcery_actions)
                                .size() < 2) {
                        continue;
                    }
                    candidates.push_back({
                        .trace_ordinal = trace_ordinal,
                        .nontrivial_ordinal =
                            candidates.size(),
                    });
                }
                if (candidates.empty()) {
                    throw std::runtime_error(
                        "AQ4-OP1 source has a rootless actor-game");
                }
                const auto retained =
                    learned_iteration::
                        evenly_spaced_retained_indices(
                            candidates.size(),
                            action_q_on_policy_successor::
                                maximum_roots_per_actor_game(
                                    split));
                if (retained.empty()) {
                    throw std::logic_error(
                        "AQ4-OP1 retention returned no root");
                }
                const DeckId owner =
                    scheduled.seat_decks[actor];
                DeckCensus& deck =
                    split_census.decks[deck_index(owner)];
                ++deck.actor_games;
                deck.nontrivial_roots += candidates.size();

                ActorGameCensus actor_game{
                    .split = split,
                    .schedule_index =
                        scheduled.schedule_index,
                    .actor = actor,
                    .owner_deck = owner,
                    .nontrivial_roots =
                        candidates.size(),
                    .retained_roots = retained.size(),
                };
                for (std::size_t position = 0;
                     position < retained.size();
                     ++position) {
                    const Candidate& selected =
                        candidates[retained[position]];
                    const auto& point =
                        trace[selected.trace_ordinal];
                    ManifestRoot root =
                        make_manifest_root(
                            point, scheduled, split, actor,
                            selected.trace_ordinal,
                            selected.nontrivial_ordinal,
                            candidates.size(), position,
                            retained.size());
                    actor_game.retained_options +=
                        root.actions.size();
                    ++deck.retained_roots;
                    deck.retained_options +=
                        root.actions.size();
                    visitor(root, point, decks);
                    census.roots.push_back(std::move(root));
                }
                split_census.actor_games.push_back(
                    std::move(actor_game));
            }
        }
    }
    census.manifest_hash =
        action_q_on_policy_successor::
            canonical_manifest_hash(census);
    action_q_on_policy_successor::validate_census(
        census);
    return census;
}

void validate_outer_samples(
    const LearnedActionSamples& samples,
    std::size_t actions, std::size_t worlds,
    bool expect_inner) {
    const std::size_t expected = actions * worlds;
    if (samples.sampled_worlds != worlds ||
        samples.rollout_evaluations != expected ||
        samples.terminal_evaluations +
                samples.bootstrapped_evaluations !=
            samples.rollout_evaluations ||
        samples.exact_priority_aggregate_scores.size() !=
            actions ||
        samples.q_samples.size() != actions ||
        !std::all_of(
            samples.exact_priority_aggregate_scores.begin(),
            samples.exact_priority_aggregate_scores.end(),
            finite_probability)) {
        throw std::logic_error(
            "AQ4-OP1 outer search accounting drifted");
    }
    for (const auto& row : samples.q_samples) {
        if (row.size() != worlds ||
            !std::all_of(
                row.begin(), row.end(),
                finite_probability)) {
            throw std::logic_error(
                "AQ4-OP1 outer sample shape drifted");
        }
    }
    if (!expect_inner) {
        if (!samples
                 .priority_inner_rollout_evaluations.empty() ||
            !samples
                 .priority_inner_search_invocations.empty() ||
            !samples
                 .priority_inner_search_max_depth.empty() ||
            samples.inner_rollout_evaluations != 0 ||
            samples.inner_search_invocations != 0 ||
            samples.inner_search_max_depth != 0) {
            throw std::logic_error(
                "AQ4-OP1 base scorer opened nested search");
        }
        return;
    }
    if (samples.priority_inner_rollout_evaluations.size() !=
            actions ||
        samples.priority_inner_search_invocations.size() !=
            actions ||
        samples.priority_inner_search_max_depth.size() !=
            actions ||
        samples.inner_search_max_depth > 1) {
        throw std::logic_error(
            "AQ4-OP1 teacher nested-search shape drifted");
    }
    std::size_t rollout_sum = 0;
    std::size_t invocation_sum = 0;
    std::size_t maximum_depth = 0;
    for (std::size_t action = 0;
         action < actions; ++action) {
        const auto& rollouts =
            samples.priority_inner_rollout_evaluations[action];
        const auto& invocations =
            samples.priority_inner_search_invocations[action];
        const auto& depths =
            samples.priority_inner_search_max_depth[action];
        if (rollouts.size() != worlds ||
            invocations.size() != worlds ||
            depths.size() != worlds) {
            throw std::logic_error(
                "AQ4-OP1 teacher inner matrix drifted");
        }
        for (std::size_t world = 0;
             world < worlds; ++world) {
            const bool inactive =
                invocations[world] == 0 &&
                rollouts[world] == 0 &&
                depths[world] == 0;
            const bool active =
                invocations[world] != 0 &&
                rollouts[world] != 0 &&
                rollouts[world] % kInnerWorlds == 0 &&
                depths[world] == 1;
            if (!inactive && !active) {
                throw std::logic_error(
                    "AQ4-OP1 teacher inner cell drifted");
            }
            if (rollout_sum >
                    std::numeric_limits<std::size_t>::max() -
                        rollouts[world] ||
                invocation_sum >
                    std::numeric_limits<std::size_t>::max() -
                        invocations[world]) {
                throw std::overflow_error(
                    "AQ4-OP1 teacher accounting overflow");
            }
            rollout_sum += rollouts[world];
            invocation_sum += invocations[world];
            maximum_depth =
                std::max(maximum_depth, depths[world]);
        }
    }
    if (rollout_sum != samples.inner_rollout_evaluations ||
        invocation_sum != samples.inner_search_invocations ||
        maximum_depth != samples.inner_search_max_depth) {
        throw std::logic_error(
            "AQ4-OP1 teacher inner accounting does not cross-sum");
    }
}

RootExample label_root(
    const ManifestRoot& manifest,
    const LearnedDecisionTracePoint& point,
    const std::array<std::vector<CardId>, 2>& decks,
    const std::shared_ptr<const LearnedModel>& warm_parent) {
    const RootCoordinate& coordinate = manifest.coordinate;
    if (!point.context.valid ||
        point.context.decision_player != coordinate.actor) {
        throw std::logic_error(
            "AQ4-OP1 label source context drifted");
    }
    const LearnedActionSamples base =
        learned_priority_action_samples(
            point.state, decks, coordinate.actor,
            point.context.sorcery_actions,
            point.context.phase,
            point.context.consecutive_passes,
            manifest.actions, warm_parent,
            base_search_config(coordinate.search_seed));
    const LearnedActionSamples teacher =
        learned_priority_action_samples(
            point.state, decks, coordinate.actor,
            point.context.sorcery_actions,
            point.context.phase,
            point.context.consecutive_passes,
            manifest.actions, warm_parent,
            teacher_search_config(coordinate.search_seed));
    validate_outer_samples(
        base, manifest.actions.size(),
        kBaseWorlds, false);
    validate_outer_samples(
        teacher, manifest.actions.size(),
        kTeacherWorlds, true);
    return {
        .manifest = manifest,
        .base_scores =
            base.exact_priority_aggregate_scores,
        .teacher_scores =
            teacher.exact_priority_aggregate_scores,
        .target_probabilities =
            learned_soft_priority_target(
                teacher.exact_priority_aggregate_scores),
        .accounting = {
            .base_sampled_worlds = base.sampled_worlds,
            .base_rollout_evaluations =
                base.rollout_evaluations,
            .base_terminal_evaluations =
                base.terminal_evaluations,
            .base_bootstrapped_evaluations =
                base.bootstrapped_evaluations,
            .teacher_sampled_worlds =
                teacher.sampled_worlds,
            .teacher_rollout_evaluations =
                teacher.rollout_evaluations,
            .teacher_terminal_evaluations =
                teacher.terminal_evaluations,
            .teacher_bootstrapped_evaluations =
                teacher.bootstrapped_evaluations,
            .teacher_inner_rollout_evaluations =
                teacher.inner_rollout_evaluations,
            .teacher_inner_search_invocations =
                teacher.inner_search_invocations,
            .teacher_inner_search_max_depth =
                teacher.inner_search_max_depth,
        },
        .weight =
            root_weight(
                coordinate.actor_game_retained_roots),
    };
}

bool normalized_target(
    std::span<const double> probabilities) {
    if (probabilities.empty() ||
        !std::all_of(
            probabilities.begin(), probabilities.end(),
            finite_probability)) {
        return false;
    }
    const double total =
        std::accumulate(
            probabilities.begin(),
            probabilities.end(), 0.0);
    return std::abs(total - 1.0) <= 1.0e-9;
}

void validate_example(
    const RootExample& example,
    const ManifestRoot& expected) {
    if (example.manifest != expected) {
        throw std::invalid_argument(
            "AQ4-OP1 labeled root differs from its census row");
    }
    const std::size_t actions = expected.actions.size();
    if (example.base_scores.size() != actions ||
        example.teacher_scores.size() != actions ||
        example.target_probabilities.size() != actions ||
        !std::all_of(
            example.base_scores.begin(),
            example.base_scores.end(), finite_probability) ||
        !std::all_of(
            example.teacher_scores.begin(),
            example.teacher_scores.end(),
            finite_probability) ||
        !normalized_target(example.target_probabilities) ||
        example.target_probabilities !=
            learned_soft_priority_target(
                example.teacher_scores) ||
        example.weight !=
            root_weight(
                expected.coordinate
                    .actor_game_retained_roots)) {
        throw std::invalid_argument(
            "AQ4-OP1 labeled root has invalid values");
    }
    const RootAccounting& accounting = example.accounting;
    if (accounting.base_sampled_worlds != kBaseWorlds ||
        accounting.base_rollout_evaluations !=
            actions * kBaseWorlds ||
        accounting.base_terminal_evaluations +
                accounting.base_bootstrapped_evaluations !=
            accounting.base_rollout_evaluations ||
        accounting.teacher_sampled_worlds !=
            kTeacherWorlds ||
        accounting.teacher_rollout_evaluations !=
            actions * kTeacherWorlds ||
        accounting.teacher_terminal_evaluations +
                accounting.teacher_bootstrapped_evaluations !=
            accounting.teacher_rollout_evaluations ||
        accounting.teacher_inner_search_max_depth > 1 ||
        (accounting.teacher_inner_search_invocations == 0 &&
         (accounting.teacher_inner_rollout_evaluations != 0 ||
          accounting.teacher_inner_search_max_depth != 0)) ||
        (accounting.teacher_inner_search_invocations != 0 &&
         (accounting.teacher_inner_rollout_evaluations == 0 ||
          accounting.teacher_inner_rollout_evaluations %
                  kInnerWorlds !=
              0 ||
          accounting.teacher_inner_search_max_depth != 1))) {
        throw std::invalid_argument(
            "AQ4-OP1 labeled root accounting is invalid");
    }
}

std::vector<LearnedValuePriorityTrainingExample>
project_training_examples(const Corpus& corpus) {
    std::vector<LearnedValuePriorityTrainingExample> result;
    result.reserve(corpus.train.size());
    for (const RootExample& root : corpus.train) {
        result.push_back({
            .options = root.manifest.options,
            .base_scores = root.base_scores,
            .target_probabilities =
                root.target_probabilities,
            .weight = root.weight,
        });
    }
    return result;
}

void append_source_recipe(std::string& output) {
    const GameConfig game =
        action_q_on_policy_successor::
            source_game_config({}, 0);
    append_size(output, game.max_turns);
    append_u64(output, game.learned_training_seed);
    append_size(output, game.learned_search_depth);
    for (const BotConfig& bot : game.bots) {
        append_u64(
            output,
            static_cast<std::uint64_t>(bot.kind));
        append_u64(
            output,
            static_cast<std::uint64_t>(
                bot.learned_variant));
        append_size(output, bot.rollouts_per_action);
        append_double(output, bot.exploration_rate);
        append_double(
            output, bot.value_continuation_epsilon);
        append_double(
            output,
            bot.value_priority_residual_weight);
        append_bool(output, bot.value_pass_dominance);
        append_double(
            output,
            bot.value_resolved_shallow_prior_weight);
        append_bool(
            output, bot.value_adversarial_blocks);
        append_bool(
            output, bot.value_actor_local_search);
        append_u64(
            output,
            static_cast<std::uint64_t>(
                bot.value_continuation_controller));
        append_size(output, bot.training_games);
    }
}

void add_failure(
    OfflineReport& report, bool passed,
    std::string message) {
    if (!passed) {
        report.failures.push_back(std::move(message));
    }
}

} // namespace

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
        << "Usage: old-school-action-q-on-policy-successor "
           "(--census|--run)\n";
}

std::size_t split_index(Split split) {
    const std::size_t result =
        static_cast<std::size_t>(split);
    if (result >= 2) {
        throw std::out_of_range(
            "AQ4-OP1 split is invalid");
    }
    return result;
}

std::size_t maximum_roots_per_actor_game(Split split) {
    switch (split) {
    case Split::Train:
        return kTrainMaximumRootsPerActorGame;
    case Split::Dev:
        return kDevMaximumRootsPerActorGame;
    }
    throw std::out_of_range(
        "AQ4-OP1 split cap is invalid");
}

std::uint64_t root_search_seed(
    Split split, std::size_t schedule_index,
    std::size_t actor,
    std::size_t nontrivial_ordinal) {
    if (schedule_index >= kGamesPerSplit ||
        actor >= 2 ||
        nontrivial_ordinal >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range(
            "AQ4-OP1 root search coordinate is invalid");
    }
    const std::uint64_t subindex =
        (static_cast<std::uint64_t>(actor) << 32) |
        static_cast<std::uint64_t>(nontrivial_ordinal);
    return learned_iteration::derive_seed(
        kCollectionRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        kScheduleGeneration,
        action_q_on_policy_successor::split_index(split) *
                kGamesPerSplit +
            schedule_index,
        subindex);
}

double root_weight(
    std::size_t actor_game_retained_roots) {
    if (actor_game_retained_roots == 0 ||
        actor_game_retained_roots >
            kTrainMaximumRootsPerActorGame) {
        throw std::out_of_range(
            "AQ4-OP1 retained-root weight denominator is invalid");
    }
    return 1.0 /
           (static_cast<double>(
                kActorGamesPerDeckAndSplit) *
            static_cast<double>(
                actor_game_retained_roots));
}

GameConfig source_game_config(
    std::shared_ptr<const LearnedModel> warm_parent,
    std::size_t starting_player) {
    if (starting_player >= 2) {
        throw std::out_of_range(
            "AQ4-OP1 source starting player is invalid");
    }
    const BotConfig bot{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = kBaseWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight =
            kResidualWeight,
        .value_pass_dominance = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_adversarial_blocks = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = 800,
        .learned_model = warm_parent,
    };
    return {
        .max_turns = kSourceTurnCap,
        .starting_player = starting_player,
        .bots = {bot, bot},
        .learned_training_seed = 424242,
        .learned_model = std::move(warm_parent),
        .learned_search_depth = 1,
    };
}

LearnedSearchConfig base_search_config(
    std::uint64_t seed) {
    const LearnedSearchConfig config =
        g4b::base_search_config(seed);
    if (config.worlds != kBaseWorlds ||
        config.rollouts_per_world != 1 ||
        config.horizon_turns != g1::kBaseHorizonTurns ||
        !config.blend_shallow_prior ||
        config.value_priority_residual_weight != 0.0 ||
        config.value_continuation_search_worlds != 0) {
        throw std::logic_error(
            "AQ4-OP1 base search recipe drifted");
    }
    return config;
}

LearnedSearchConfig teacher_search_config(
    std::uint64_t seed) {
    LearnedSearchConfig config =
        learned_value_actor_local_search_config(seed);
    config.value_priority_residual_weight =
        kResidualWeight;
    if (config.worlds != kTeacherWorlds ||
        config.rollouts_per_world != 1 ||
        config.horizon_turns != g1::kTeacherHorizonTurns ||
        config.blend_shallow_prior ||
        config.evaluation_threads !=
            g1::kTeacherEvaluationThreads ||
        config.value_continuation_search_worlds !=
            kInnerWorlds) {
        throw std::logic_error(
            "AQ4-OP1 teacher search recipe drifted");
    }
    return config;
}

LearnedValuePriorityHeadUpdateConfig optimizer_config() {
    const auto config = g4b::optimizer_config();
    if (config.batch_size != 64 ||
        config.epochs != 64 ||
        config.learning_rate != 0.003 ||
        config.beta1 != 0.9 ||
        config.beta2 != 0.999 ||
        config.epsilon != 1.0e-8 ||
        config.global_gradient_norm_clip != 5.0 ||
        config.seed != 12262988820247274425ULL ||
        config.residual_weight != kResidualWeight ||
        config.policy_temperature != 0.10) {
        throw std::logic_error(
            "AQ4-OP1 optimizer recipe drifted");
    }
    return config;
}

std::string canonical_manifest_hash(
    const Census& census) {
    std::string payload;
    append_string(payload, kManifestSchema);
    append_u64(payload, census.root_seed);
    append_size(payload, kScheduleGeneration);
    append_size(payload, kTrainBlock);
    append_size(payload, kDevBlock);
    append_size(payload, kSourceTurnCap);
    append_size(
        payload, kTrainMaximumRootsPerActorGame);
    append_size(
        payload, kDevMaximumRootsPerActorGame);
    append_double(payload, kResidualWeight);
    append_source_recipe(payload);
    append_string(payload, census.parent_fingerprint);
    for (const SplitCensus& split : census.splits) {
        append_u64(
            payload,
            static_cast<std::uint64_t>(split.split));
        append_size(payload, split.games);
        for (const DeckCensus& deck : split.decks) {
            append_size(payload, deck.actor_games);
            append_size(payload, deck.nontrivial_roots);
            append_size(payload, deck.retained_roots);
            append_size(payload, deck.retained_options);
        }
        append_size(payload, split.actor_games.size());
        for (const ActorGameCensus& game :
             split.actor_games) {
            append_u64(
                payload,
                static_cast<std::uint64_t>(game.split));
            append_size(payload, game.schedule_index);
            append_size(payload, game.actor);
            append_u64(
                payload,
                static_cast<std::uint64_t>(
                    game.owner_deck));
            append_size(payload, game.nontrivial_roots);
            append_size(payload, game.retained_roots);
            append_size(payload, game.retained_options);
        }
    }
    append_size(payload, census.roots.size());
    for (const ManifestRoot& root : census.roots) {
        append_manifest_root(payload, root);
    }
    return artifact_integrity::sha256_string(payload);
}

CensusCountSeal census_count_seal(
    const Census& census) {
    CensusCountSeal result;
    for (std::size_t split = 0;
         split < result.splits.size(); ++split) {
        const SplitCensus& source = census.splits[split];
        SplitCountSeal& target = result.splits[split];
        target.games = source.games;
        target.actor_games = source.actor_games.size();
        target.decks = source.decks;
        for (const DeckCensus& deck : source.decks) {
            if (target.nontrivial_roots >
                    std::numeric_limits<std::size_t>::max() -
                        deck.nontrivial_roots ||
                target.retained_roots >
                    std::numeric_limits<std::size_t>::max() -
                        deck.retained_roots ||
                target.retained_options >
                    std::numeric_limits<std::size_t>::max() -
                        deck.retained_options) {
                throw std::overflow_error(
                    "AQ4-OP1 census count seal overflowed");
            }
            target.nontrivial_roots +=
                deck.nontrivial_roots;
            target.retained_roots +=
                deck.retained_roots;
            target.retained_options +=
                deck.retained_options;
        }
    }
    return result;
}

bool frozen_census_seal_populated() noexcept {
    if (kFrozenCensusManifestHash.empty()) {
        return false;
    }
    for (const SplitCountSeal& split :
         kFrozenCensusCounts.splits) {
        if (split.games != kGamesPerSplit ||
            split.actor_games != kActorGamesPerSplit ||
            split.nontrivial_roots == 0 ||
            split.retained_roots == 0 ||
            split.retained_roots >
                split.retained_options / 2) {
            return false;
        }
        std::size_t nontrivial = 0;
        std::size_t retained = 0;
        std::size_t options = 0;
        for (const DeckCensus& deck : split.decks) {
            if (deck.actor_games !=
                    kActorGamesPerDeckAndSplit ||
                deck.nontrivial_roots == 0 ||
                deck.retained_roots == 0 ||
                deck.retained_roots >
                    deck.retained_options / 2 ||
                nontrivial >
                    std::numeric_limits<std::size_t>::max() -
                        deck.nontrivial_roots ||
                retained >
                    std::numeric_limits<std::size_t>::max() -
                        deck.retained_roots ||
                options >
                    std::numeric_limits<std::size_t>::max() -
                        deck.retained_options) {
                return false;
            }
            nontrivial += deck.nontrivial_roots;
            retained += deck.retained_roots;
            options += deck.retained_options;
        }
        if (nontrivial != split.nontrivial_roots ||
            retained != split.retained_roots ||
            options != split.retained_options) {
            return false;
        }
    }
    return true;
}

void validate_census(const Census& census) {
    if (census.root_seed != kCollectionRootSeed ||
        census.parent_fingerprint !=
            kRequiredWarmParentFingerprint ||
        census.splits[0].split != Split::Train ||
        census.splits[1].split != Split::Dev ||
        census.roots.empty() ||
        census.manifest_hash.size() != 64 ||
        census.manifest_hash !=
            action_q_on_policy_successor::
                canonical_manifest_hash(census)) {
        throw std::invalid_argument(
            "AQ4-OP1 census identity or manifest hash is invalid");
    }

    std::set<std::uint64_t> game_seeds;
    std::set<std::string> stable_root_ids;
    std::size_t root_position = 0;
    for (const Split split :
         std::array<Split, 2>{Split::Train, Split::Dev}) {
        const std::size_t split_value =
            action_q_on_policy_successor::split_index(
                split);
        const SplitCensus& recorded =
            census.splits[split_value];
        const auto& schedule = schedule_for(split);
        if (recorded.games != schedule.size() ||
            recorded.games != kGamesPerSplit ||
            recorded.actor_games.size() !=
                kActorGamesPerSplit ||
            recorded.retained_roots() == 0 ||
            recorded.retained_roots() >
                (split == Split::Train
                     ? kTrainRootCeiling
                     : kDevRootCeiling)) {
            throw std::invalid_argument(
                "AQ4-OP1 split census shape is invalid");
        }

        std::array<DeckCensus, kDeckCount> observed{};
        std::size_t actor_game_position = 0;
        for (std::size_t schedule_position = 0;
             schedule_position < schedule.size();
             ++schedule_position) {
            const auto& scheduled =
                schedule[schedule_position];
            if (scheduled.schedule_index !=
                    schedule_position ||
                scheduled.starting_player >= 2 ||
                scheduled.seat_decks[0] ==
                    scheduled.seat_decks[1] ||
                !game_seeds.insert(scheduled.seed).second) {
                throw std::invalid_argument(
                    "AQ4-OP1 source schedules are malformed or collide");
            }
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                if (actor_game_position >=
                    recorded.actor_games.size()) {
                    throw std::invalid_argument(
                        "AQ4-OP1 actor-game census is truncated");
                }
                const ActorGameCensus& actor_game =
                    recorded.actor_games[
                        actor_game_position++];
                const DeckId owner =
                    scheduled.seat_decks[actor];
                const std::size_t retained =
                    std::min(
                        actor_game.nontrivial_roots,
                        action_q_on_policy_successor::
                            maximum_roots_per_actor_game(
                                split));
                if (actor_game.split != split ||
                    actor_game.schedule_index !=
                        scheduled.schedule_index ||
                    actor_game.actor != actor ||
                    actor_game.owner_deck != owner ||
                    actor_game.nontrivial_roots == 0 ||
                    actor_game.retained_roots != retained ||
                    actor_game.retained_roots == 0 ||
                    actor_game.retained_options <
                        actor_game.retained_roots * 2) {
                    throw std::invalid_argument(
                        "AQ4-OP1 actor-game census row is invalid");
                }
                const auto retained_indices =
                    learned_iteration::
                        evenly_spaced_retained_indices(
                            actor_game.nontrivial_roots,
                            action_q_on_policy_successor::
                                maximum_roots_per_actor_game(
                                    split));
                if (retained_indices.size() !=
                    actor_game.retained_roots) {
                    throw std::logic_error(
                        "AQ4-OP1 blind retention drifted");
                }

                DeckCensus& deck =
                    observed[deck_index(owner)];
                ++deck.actor_games;
                deck.nontrivial_roots +=
                    actor_game.nontrivial_roots;
                deck.retained_roots +=
                    actor_game.retained_roots;
                std::size_t observed_options = 0;
                std::optional<std::size_t>
                    previous_trace_ordinal;
                for (std::size_t retained_position = 0;
                     retained_position <
                         actor_game.retained_roots;
                     ++retained_position) {
                    if (root_position >= census.roots.size()) {
                        throw std::invalid_argument(
                            "AQ4-OP1 census is missing retained roots");
                    }
                    const ManifestRoot& root =
                        census.roots[root_position++];
                    const RootCoordinate& coordinate =
                        root.coordinate;
                    if (coordinate.split != split ||
                        coordinate.schedule_index !=
                            scheduled.schedule_index ||
                        coordinate.pairing_index !=
                            scheduled.pairing_index ||
                        coordinate.game_seed != scheduled.seed ||
                        coordinate.starting_player !=
                            scheduled.starting_player ||
                        coordinate.seat_decks !=
                            scheduled.seat_decks ||
                        coordinate.actor != actor ||
                        coordinate.owner_deck() != owner ||
                        coordinate.nontrivial_ordinal !=
                            retained_indices[
                                retained_position] ||
                        coordinate.nontrivial_ordinal >=
                            actor_game.nontrivial_roots ||
                        coordinate.trace_ordinal <
                            coordinate.nontrivial_ordinal ||
                        (previous_trace_ordinal.has_value() &&
                         coordinate.trace_ordinal <=
                             *previous_trace_ordinal) ||
                        coordinate
                                .actor_game_nontrivial_roots !=
                            actor_game.nontrivial_roots ||
                        coordinate.retained_position !=
                            retained_position ||
                        coordinate.actor_game_retained_roots !=
                            actor_game.retained_roots ||
                        coordinate.search_seed !=
                            action_q_on_policy_successor::
                                root_search_seed(
                                split,
                                scheduled.schedule_index,
                                actor,
                                coordinate
                                    .nontrivial_ordinal)) {
                        throw std::invalid_argument(
                            "AQ4-OP1 retained root coordinate drifted");
                    }
                    previous_trace_ordinal =
                        coordinate.trace_ordinal;
                    if (root.actions.size() < 2 ||
                        root.action_descriptors.size() !=
                            root.actions.size() ||
                        root.options.size() !=
                            root.actions.size()) {
                        throw std::invalid_argument(
                            "AQ4-OP1 manifest action shape is invalid");
                    }
                    for (std::size_t action = 0;
                         action < root.actions.size();
                         ++action) {
                        if (root.action_descriptors[action] !=
                                probes::
                                    stable_priority_action_descriptor(
                                        root.actions[action]) ||
                            root.options[action].size() !=
                                kPolicyFeatureCount ||
                            !std::all_of(
                                root.options[action].begin(),
                                root.options[action].end(),
                                [](double feature) {
                                    return std::isfinite(feature);
                                })) {
                            throw std::invalid_argument(
                                "AQ4-OP1 manifest action or feature drifted");
                        }
                        for (std::size_t earlier = 0;
                             earlier < action; ++earlier) {
                            if (root.actions[earlier] ==
                                    root.actions[action] ||
                                root.action_descriptors[earlier] ==
                                    root.action_descriptors[action]) {
                                throw std::invalid_argument(
                                    "AQ4-OP1 manifest duplicates an action");
                            }
                        }
                    }
                    if (root.information_action_fingerprint.size() !=
                            64 ||
                        root.information_action_fingerprint !=
                            information_action_fingerprint(root) ||
                        root.stable_root_id.size() != 64 ||
                        root.stable_root_id !=
                            stable_root_id(root) ||
                        !stable_root_ids
                             .insert(root.stable_root_id)
                             .second) {
                        throw std::invalid_argument(
                            "AQ4-OP1 owner-safe root identity drifted or collided");
                    }
                    observed_options += root.actions.size();
                }
                if (observed_options !=
                    actor_game.retained_options) {
                    throw std::invalid_argument(
                        "AQ4-OP1 actor-game option cross-sum drifted");
                }
                deck.retained_options += observed_options;
            }
        }
        if (actor_game_position !=
                recorded.actor_games.size() ||
            observed != recorded.decks) {
            throw std::invalid_argument(
                "AQ4-OP1 split/deck census cross-sum drifted");
        }
        for (const DeckCensus& deck : recorded.decks) {
            if (deck.actor_games !=
                    kActorGamesPerDeckAndSplit ||
                deck.retained_roots == 0 ||
                deck.retained_options <
                    deck.retained_roots * 2) {
                throw std::invalid_argument(
                    "AQ4-OP1 census is missing a balanced deck");
            }
        }
    }
    if (root_position != census.roots.size() ||
        census.games() != 2 * kGamesPerSplit) {
        throw std::invalid_argument(
            "AQ4-OP1 census contains unaccounted roots or games");
    }
}

void require_frozen_census(const Census& census) {
    action_q_on_policy_successor::validate_census(
        census);
    if (!frozen_census_seal_populated()) {
        throw std::runtime_error(
            "AQ4-OP1 census hash/count seal is not frozen; --run is sealed");
    }
    if (census.manifest_hash !=
            kFrozenCensusManifestHash ||
        action_q_on_policy_successor::
                census_count_seal(census) !=
            kFrozenCensusCounts) {
        throw std::runtime_error(
            "AQ4-OP1 census differs from its frozen hash/count seal");
    }
}

Census collect_census(
    std::shared_ptr<const LearnedModel> warm_parent) {
    require_warm_parent(warm_parent);
    return visit_source_roots(
        warm_parent,
        [](const ManifestRoot&,
           const LearnedDecisionTracePoint&,
           const std::array<std::vector<CardId>, 2>&) {});
}

std::string canonical_corpus_digest(
    const Corpus& corpus) {
    std::string payload;
    append_string(payload, kCorpusSchema);
    append_u64(payload, kCollectionRootSeed);
    append_size(payload, kScheduleGeneration);
    append_size(payload, kTrainBlock);
    append_size(payload, kDevBlock);
    append_size(payload, kSourceTurnCap);
    append_size(
        payload, kTrainMaximumRootsPerActorGame);
    append_size(
        payload, kDevMaximumRootsPerActorGame);
    append_double(payload, kResidualWeight);
    append_size(payload, kBaseWorlds);
    append_size(payload, g1::kBaseHorizonTurns);
    append_size(payload, kTeacherWorlds);
    append_size(payload, g1::kTeacherHorizonTurns);
    append_size(
        payload, g1::kTeacherEvaluationThreads);
    append_size(payload, kInnerWorlds);
    append_size(
        payload,
        action_q_nested_actor_diagnostic::
            kInnerHorizonTurns);
    append_string(
        payload, g4b::kFrozenPreflightDigest);
    append_source_recipe(payload);
    append_search_config(
        payload, base_search_config(0));
    append_search_config(
        payload, teacher_search_config(0));
    append_optimizer(payload, optimizer_config());
    append_string(payload, corpus.census.manifest_hash);
    append_string(
        payload, corpus.census.parent_fingerprint);
    append_components(payload, corpus.parent_components);
    append_size(payload, corpus.train.size());
    for (const RootExample& example : corpus.train) {
        append_example(payload, example);
    }
    append_size(payload, corpus.dev.size());
    for (const RootExample& example : corpus.dev) {
        append_example(payload, example);
    }
    return artifact_integrity::sha256_string(payload);
}

void validate_corpus(const Corpus& corpus) {
    action_q_on_policy_successor::validate_census(
        corpus.census);
    if (corpus.parent_components.critic.size() != 64 ||
        corpus.parent_components.priority.size() != 64 ||
        corpus.parent_components.attack.size() != 64 ||
        corpus.parent_components.block.size() != 64 ||
        corpus.parent_components.damage_order.size() != 64 ||
        corpus.digest.size() != 64 ||
        corpus.digest !=
            action_q_on_policy_successor::
                canonical_corpus_digest(corpus)) {
        throw std::invalid_argument(
            "AQ4-OP1 corpus digest is invalid");
    }
    std::array<std::size_t, 2> positions{};
    std::array<
        std::array<double, kDeckCount>, 2>
        deck_mass{};
    std::size_t inner_rollouts = 0;
    std::size_t inner_invocations = 0;
    std::size_t maximum_inner_depth = 0;
    for (const ManifestRoot& expected :
         corpus.census.roots) {
        const std::size_t split =
            action_q_on_policy_successor::split_index(
                expected.coordinate.split);
        const auto& examples =
            expected.coordinate.split == Split::Train
                ? corpus.train
                : corpus.dev;
        if (positions[split] >= examples.size()) {
            throw std::invalid_argument(
                "AQ4-OP1 corpus is missing a census root");
        }
        const RootExample& example =
            examples[positions[split]++];
        validate_example(example, expected);
        deck_mass[split][deck_index(
            expected.coordinate.owner_deck())] +=
            example.weight;
        if (inner_rollouts >
                std::numeric_limits<std::size_t>::max() -
                    example.accounting
                        .teacher_inner_rollout_evaluations ||
            inner_invocations >
                std::numeric_limits<std::size_t>::max() -
                    example.accounting
                        .teacher_inner_search_invocations) {
            throw std::overflow_error(
                "AQ4-OP1 corpus accounting overflow");
        }
        inner_rollouts +=
            example.accounting
                .teacher_inner_rollout_evaluations;
        inner_invocations +=
            example.accounting
                .teacher_inner_search_invocations;
        maximum_inner_depth =
            std::max(
                maximum_inner_depth,
                example.accounting
                    .teacher_inner_search_max_depth);
    }
    if (positions[0] != corpus.train.size() ||
        positions[1] != corpus.dev.size() ||
        inner_rollouts == 0 ||
        inner_invocations == 0 ||
        maximum_inner_depth != 1) {
        throw std::invalid_argument(
            "AQ4-OP1 corpus accounting is incomplete");
    }
    for (const auto& split_mass : deck_mass) {
        for (const double mass : split_mass) {
            if (std::abs(mass - 1.0) > 1.0e-12) {
                throw std::invalid_argument(
                    "AQ4-OP1 deck-balanced weight mass drifted");
            }
        }
    }
}

Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> warm_parent,
    const Census& frozen_census,
    const action_q_nested_actor_diagnostic::PreflightReport&
        frozen_preflight) {
    require_warm_parent(warm_parent);
    action_q_on_policy_successor::
        require_frozen_census(frozen_census);
    if (!g4b::preflight_exact(frozen_preflight)) {
        throw std::invalid_argument(
            "AQ4-OP1 labeling requires exact frozen AQ4 controls");
    }
    if (learned_model_fingerprint(warm_parent) !=
        frozen_census.parent_fingerprint) {
        throw std::invalid_argument(
            "AQ4-OP1 census and labeling parent differ");
    }

    Corpus result;
    result.parent_components =
        learned_model_component_fingerprints(warm_parent);
    std::size_t manifest_position = 0;
    const Census reconstructed =
        visit_source_roots(
            warm_parent,
            [&](const ManifestRoot& root,
                const LearnedDecisionTracePoint& point,
                const std::array<
                    std::vector<CardId>, 2>& decks) {
                if (manifest_position >=
                        frozen_census.roots.size() ||
                    root !=
                        frozen_census.roots[
                            manifest_position]) {
                    throw std::runtime_error(
                        "AQ4-OP1 source root differs from frozen census");
                }
                RootExample example =
                    label_root(
                        root, point, decks, warm_parent);
                if (root.coordinate.split ==
                    Split::Train) {
                    result.train.push_back(
                        std::move(example));
                } else {
                    result.dev.push_back(
                        std::move(example));
                }
                ++manifest_position;
            });
    if (manifest_position !=
            frozen_census.roots.size() ||
        reconstructed != frozen_census) {
        throw std::runtime_error(
            "AQ4-OP1 reconstructed census is not bit-exact");
    }
    result.census = reconstructed;
    result.digest =
        action_q_on_policy_successor::
            canonical_corpus_digest(result);
    action_q_on_policy_successor::validate_corpus(
        result);
    return result;
}

Metrics evaluate(
    std::span<const RootExample> examples,
    std::shared_ptr<const LearnedModel> model) {
    return g4b::evaluate(
        examples, std::move(model), kResidualWeight);
}

FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> warm_parent) {
    require_warm_parent(warm_parent);
    action_q_on_policy_successor::validate_corpus(
        corpus);
    action_q_on_policy_successor::
        require_frozen_census(corpus.census);
    const auto actual_parent_components =
        learned_model_component_fingerprints(warm_parent);
    if (learned_model_fingerprint(warm_parent) !=
            corpus.census.parent_fingerprint ||
        corpus.parent_components !=
            actual_parent_components) {
        throw std::invalid_argument(
            "AQ4-OP1 fit parent differs from the corpus");
    }

    FitReport report;
    report.corpus_digest = corpus.digest;
    report.parent_fingerprint_before =
        learned_model_fingerprint(warm_parent);
    report.parent_components =
        actual_parent_components;
    report.optimizer = optimizer_config();
    report.fit_examples = corpus.train.size();
    const auto examples =
        project_training_examples(corpus);
    for (const auto& example : examples) {
        if (example.options.size() >
            std::numeric_limits<std::size_t>::max() -
                report.fit_options) {
            throw std::overflow_error(
                "AQ4-OP1 fit option count overflow");
        }
        report.fit_options += example.options.size();
    }
    report.candidate =
        update_learned_value_priority_head(
            warm_parent, examples, report.optimizer);
    const auto repeated =
        update_learned_value_priority_head(
            warm_parent, examples, report.optimizer);
    report.candidate_fingerprint =
        learned_model_fingerprint(report.candidate);
    report.repeated_fit_bit_identical =
        report.candidate_fingerprint ==
        learned_model_fingerprint(repeated);
    report.parent_fingerprint_after =
        learned_model_fingerprint(warm_parent);
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
    report.parent_train =
        action_q_on_policy_successor::evaluate(
            corpus.train, warm_parent);
    report.candidate_train =
        action_q_on_policy_successor::evaluate(
            corpus.train, report.candidate);
    report.parent_dev =
        action_q_on_policy_successor::evaluate(
            corpus.dev, warm_parent);
    report.candidate_dev =
        action_q_on_policy_successor::evaluate(
            corpus.dev, report.candidate);
    return report;
}

OfflineReport evaluate_offline(
    const Corpus& corpus, const FitReport& fit_report,
    const action_q_nested_actor_diagnostic::PreflightReport&
        frozen_preflight,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> candidate) {
    require_warm_parent(warm_parent);
    if (!candidate) {
        throw std::invalid_argument(
            "AQ4-OP1 offline candidate is null");
    }
    action_q_on_policy_successor::validate_corpus(
        corpus);
    action_q_on_policy_successor::
        require_frozen_census(corpus.census);
    const Metrics parent_train =
        action_q_on_policy_successor::evaluate(
            corpus.train, warm_parent);
    const Metrics candidate_train =
        action_q_on_policy_successor::evaluate(
            corpus.train, candidate);
    const Metrics parent_dev =
        action_q_on_policy_successor::evaluate(
            corpus.dev, warm_parent);
    const Metrics candidate_dev =
        action_q_on_policy_successor::evaluate(
            corpus.dev, candidate);
    if (candidate != fit_report.candidate ||
        fit_report.corpus_digest != corpus.digest ||
        fit_report.parent_components !=
            corpus.parent_components ||
        fit_report.parent_train != parent_train ||
        fit_report.candidate_train != candidate_train ||
        fit_report.parent_dev != parent_dev ||
        fit_report.candidate_dev != candidate_dev) {
        throw std::invalid_argument(
            "AQ4-OP1 offline inputs do not match the fitted corpus");
    }

    // This shared battery deliberately scores its control side at residual
    // zero. For OP1 that is an additional C16-safety comparison; the
    // on-policy TRAIN/DEV metrics above are the preregistered warm-.10 versus
    // child-.10 comparison.
    const auto model_gates =
        action_q_offline_gate::evaluate_model_gates(
            warm_parent, candidate);

    OfflineReport report;
    report.corpus_digest = corpus.digest;
    report.parent_fingerprint =
        learned_model_fingerprint(warm_parent);
    report.candidate_fingerprint =
        learned_model_fingerprint(candidate);
    report.census_frozen =
        frozen_census_seal_populated() &&
        corpus.census.manifest_hash ==
            kFrozenCensusManifestHash &&
        action_q_on_policy_successor::
                census_count_seal(corpus.census) ==
            kFrozenCensusCounts;
    report.corpus_digest_exact =
        corpus.digest ==
            action_q_on_policy_successor::
                canonical_corpus_digest(corpus) &&
        fit_report.corpus_digest == corpus.digest;
    report.preflight_exact =
        g4b::preflight_exact(frozen_preflight);
    report.parent_immutable =
        fit_report.parent_immutable &&
        fit_report.parent_fingerprint_before ==
            report.parent_fingerprint &&
        fit_report.parent_fingerprint_after ==
            report.parent_fingerprint;
    report.repeated_fit_bit_identical =
        fit_report.repeated_fit_bit_identical;
    report.only_priority_component_changed =
        fit_report.only_priority_component_changed;
    report.parent_train = parent_train;
    report.candidate_train = candidate_train;
    report.parent_dev = parent_dev;
    report.candidate_dev = candidate_dev;
    report.train_regret_strictly_improved =
        candidate_train.equal_deck_mean_regret <
        parent_train.equal_deck_mean_regret;
    report.dev_regret_strictly_improved =
        candidate_dev.equal_deck_mean_regret <
        parent_dev.equal_deck_mean_regret;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        report.dev_deck_regret_guard[deck] =
            candidate_dev.decks[deck].mean_regret <=
            parent_dev.decks[deck].mean_regret +
                kMaximumDevDeckRegretIncrease;
        report.parent_train_signal_nonzero[deck] =
            parent_train.decks[deck].mean_regret > 0.0;
        report.parent_dev_signal_nonzero[deck] =
            parent_dev.decks[deck].mean_regret > 0.0;
    }
    report.targets_finite_and_normalized =
        std::all_of(
            corpus.train.begin(), corpus.train.end(),
            [](const RootExample& example) {
                return normalized_target(
                    example.target_probabilities);
            }) &&
        std::all_of(
            corpus.dev.begin(), corpus.dev.end(),
            [](const RootExample& example) {
                return normalized_target(
                    example.target_probabilities);
            });
    report.descriptor_order_identity =
        model_gates.descriptor_order.gate_passed();
    report.redundant_counter_pass =
        model_gates.behavior
            .redundant_counter_selects_pass;
    report.braingeyser_productive =
        model_gates.behavior
            .braingeyser_x_zero_excluded;
    report.sick_bear_growth_pass =
        model_gates.behavior
            .sick_bear_growth_selects_pass;
    report.live_force_spike =
        model_gates.behavior.live_force_spike_preserved;
    report.ancestral_pass =
        model_gates.ancestral.gate_passed();
    report.frozen_dev = model_gates.frozen_dev;
    report.ancestral = model_gates.ancestral;

    add_failure(
        report, report.census_frozen,
        "census manifest is not frozen");
    add_failure(
        report, report.corpus_digest_exact,
        "corpus digest is not exact");
    add_failure(
        report, report.preflight_exact,
        "AQ4 preflight report is not bit-exact");
    add_failure(
        report, report.parent_immutable,
        "warm parent changed during fit");
    add_failure(
        report, report.repeated_fit_bit_identical,
        "repeated fit was not bit-identical");
    add_failure(
        report, report.only_priority_component_changed,
        "fit changed a non-Priority component");
    add_failure(
        report, report.train_regret_strictly_improved,
        "TRAIN equal-deck regret did not improve");
    add_failure(
        report, report.dev_regret_strictly_improved,
        "DEV equal-deck regret did not improve");
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const std::string name{
            deck_name(static_cast<DeckId>(deck))};
        add_failure(
            report, report.dev_deck_regret_guard[deck],
            "DEV regret guard failed for " + name);
        add_failure(
            report,
            report.parent_train_signal_nonzero[deck],
            "warm-parent TRAIN signal is zero for " + name);
        add_failure(
            report,
            report.parent_dev_signal_nonzero[deck],
            "warm-parent DEV signal is zero for " + name);
    }
    add_failure(
        report, report.targets_finite_and_normalized,
        "teacher targets are not finite and normalized");
    add_failure(
        report, model_gates.gate_passed(),
        "shared model-only safety battery failed");
    return report;
}

namespace {

bool selector_binding_exact_impl(
    const Corpus& corpus, const FitReport& fit_report,
    const OfflineReport& offline,
    std::string_view c16_fingerprint,
    std::string_view warm_parent_fingerprint,
    std::string_view candidate_fingerprint,
    std::string_view required_census_hash,
    const CensusCountSeal& required_census_counts) {
    try {
        action_q_on_policy_successor::validate_corpus(
            corpus);
    } catch (const std::exception&) {
        return false;
    }
    std::size_t fit_options = 0;
    for (const RootExample& example : corpus.train) {
        if (example.manifest.actions.size() >
            std::numeric_limits<std::size_t>::max() -
                fit_options) {
            return false;
        }
        fit_options += example.manifest.actions.size();
    }
    return !required_census_hash.empty() &&
           c16_fingerprint ==
               g4b::kRequiredParentFingerprint &&
           warm_parent_fingerprint ==
               kRequiredWarmParentFingerprint &&
           candidate_fingerprint.size() == 64 &&
           candidate_fingerprint !=
               warm_parent_fingerprint &&
           corpus.census.parent_fingerprint ==
               warm_parent_fingerprint &&
           corpus.census.manifest_hash ==
               required_census_hash &&
           action_q_on_policy_successor::
                   census_count_seal(corpus.census) ==
               required_census_counts &&
           corpus.digest ==
               action_q_on_policy_successor::
                   canonical_corpus_digest(corpus) &&
           fit_report.corpus_digest == corpus.digest &&
           fit_report.parent_fingerprint_before ==
               warm_parent_fingerprint &&
           fit_report.parent_fingerprint_after ==
               warm_parent_fingerprint &&
           fit_report.candidate_fingerprint ==
               candidate_fingerprint &&
           fit_report.parent_components ==
               corpus.parent_components &&
           fit_report.optimizer == optimizer_config() &&
           fit_report.fit_examples ==
               corpus.train.size() &&
           fit_report.fit_options == fit_options &&
           fit_report.parent_train ==
               offline.parent_train &&
           fit_report.candidate_train ==
               offline.candidate_train &&
           fit_report.parent_dev ==
               offline.parent_dev &&
           fit_report.candidate_dev ==
               offline.candidate_dev &&
           fit_report.parent_immutable &&
           fit_report.repeated_fit_bit_identical &&
           fit_report.only_priority_component_changed &&
           offline.corpus_digest == corpus.digest &&
           offline.parent_fingerprint ==
               warm_parent_fingerprint &&
           offline.candidate_fingerprint ==
               candidate_fingerprint &&
           offline.gate_passed();
}

void print_metrics(
    std::ostream& output, std::string_view split,
    std::string_view policy, const Metrics& metrics) {
    output
        << std::setprecision(17)
        << "metrics split=" << split
        << " policy=" << policy
        << " roots=" << metrics.roots
        << " options=" << metrics.options
        << " agreement="
        << metrics.equal_deck_top_one_expected_agreement
        << " regret="
        << metrics.equal_deck_mean_regret << '\n';
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const DeckMetrics& row = metrics.decks[deck];
        output
            << "metrics_deck split=" << split
            << " policy=" << policy
            << " deck="
            << deck_name(static_cast<DeckId>(deck))
            << " roots=" << row.roots
            << " options=" << row.options
            << " weight_mass=" << row.weight_mass
            << " agreement="
            << row.top_one_expected_agreement
            << " regret=" << row.mean_regret << '\n';
    }
}

} // namespace

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary) {
    const bool deck_floor =
        std::all_of(
            summary.challenger_decks.begin(),
            summary.challenger_decks.end(),
            [](const DeckSimulationStats& deck) {
                return deck.wins >=
                    g1::kMinimumDeckWins;
            });
    if (deck_floor &&
        summary.challenger_stats.wins >=
            g1::kManualOnlyWins) {
        // OP1 has one publication tier: even a large 60-game result licenses
        // only a dated manual pilot, never G1's inherited FAST_GO label.
        return SelectorDisposition::ManualOnly;
    }
    return SelectorDisposition::Reject;
}

BotBenchmarkSummary run_selector(
    const Corpus& corpus,
    const action_q_nested_actor_diagnostic::PreflightReport&
        frozen_preflight,
    std::shared_ptr<const LearnedModel> c16,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> candidate,
    const FitReport& fit_report,
    const OfflineReport& offline) {
    require_warm_parent(warm_parent);
    if (!c16 ||
        learned_model_fingerprint(c16) !=
            g4b::kRequiredParentFingerprint ||
        !candidate ||
        candidate == warm_parent ||
        fit_report.candidate != candidate ||
        fit_report.parent_components !=
            learned_model_component_fingerprints(
                warm_parent) ||
        fit_report.candidate_components !=
            learned_model_component_fingerprints(candidate) ||
        fit_report.candidate_fingerprint !=
            learned_model_fingerprint(candidate) ||
        !g4b::preflight_exact(frozen_preflight)) {
        throw std::invalid_argument(
            "AQ4-OP1 selector requires exact live models and controls");
    }
    action_q_on_policy_successor::validate_corpus(
        corpus);
    action_q_on_policy_successor::
        require_frozen_census(corpus.census);
    const OfflineReport recomputed =
        action_q_on_policy_successor::evaluate_offline(
            corpus, fit_report, frozen_preflight,
            warm_parent, candidate);
    if (!(recomputed == offline) ||
        !selector_binding_exact_impl(
            corpus, fit_report, recomputed,
            learned_model_fingerprint(c16),
            learned_model_fingerprint(warm_parent),
            learned_model_fingerprint(candidate),
            kFrozenCensusManifestHash,
            kFrozenCensusCounts)) {
        throw std::invalid_argument(
            "AQ4-OP1 selector authorization binding failed");
    }

    const BotBenchmarkSummary summary =
        run_bot_benchmark(
            g1::kSelectorRepetitions, kSelectorSeed,
            g1::selector_bot_config(
                candidate, kResidualWeight),
            g1::selector_bot_config(c16, 0.0),
            GameConfig{
                .max_turns = kSelectorTurnCap,
            },
            false);
    g1::validate_selector_summary(
        summary, c16, candidate, kSelectorSeed);
    return summary;
}

void print_census(
    std::ostream& output, const Census& census,
    std::size_t replayed_g4b_train_labels,
    std::size_t replayed_g4b_dev_labels) {
    if (replayed_g4b_train_labels == 0 ||
        replayed_g4b_dev_labels == 0 ||
        replayed_g4b_train_labels >
            std::numeric_limits<std::size_t>::max() -
                replayed_g4b_dev_labels) {
        throw std::invalid_argument(
            "AQ4-OP1 warm replay counts are invalid");
    }
    const CensusCountSeal measured =
        action_q_on_policy_successor::
            census_count_seal(census);
    output
        << "schema=old-school-action-q-aq4-op1-census-v1\n"
        << "mode=census root_seed=" << census.root_seed
        << " generation=" << kScheduleGeneration
        << " games=" << census.games()
        << " train_games=" << census.splits[0].games
        << " dev_games=" << census.splits[1].games
        << " train_actor_games="
        << measured.splits[0].actor_games
        << " dev_actor_games="
        << measured.splits[1].actor_games
        << " train_nontrivial_roots="
        << measured.splits[0].nontrivial_roots
        << " dev_nontrivial_roots="
        << measured.splits[1].nontrivial_roots
        << " train_roots="
        << measured.splits[0].retained_roots
        << " dev_roots="
        << measured.splits[1].retained_roots
        << " train_options="
        << measured.splits[0].retained_options
        << " dev_options="
        << measured.splits[1].retained_options
        << " manifest_hash=" << census.manifest_hash
        << " source_count_seal_frozen="
        << frozen_census_seal_populated()
        << " source=K8R1H4+residual0.10"
        << " base=K8R1H4+residual0"
        << " teacher=K8R1H8+residual0.10"
        << " inner=K2R1H4+residual0.10"
        << " train_cap=6 dev_cap=2"
        << " whole_game_disjoint=1\n";
    for (const Split split :
         std::array<Split, 2>{Split::Train, Split::Dev}) {
        const auto& counts =
            census.splits[
                action_q_on_policy_successor::split_index(
                    split)];
        for (std::size_t deck = 0;
             deck < kDeckCount; ++deck) {
            const DeckCensus& row = counts.decks[deck];
            output
                << "census_deck split="
                << (split == Split::Train
                        ? "TRAIN"
                        : "DEV")
                << " deck="
                << deck_name(static_cast<DeckId>(deck))
                << " actor_games=" << row.actor_games
                << " nontrivial_roots="
                << row.nontrivial_roots
                << " retained_roots="
                << row.retained_roots
                << " retained_options="
                << row.retained_options << '\n';
        }
    }
    output
        << "result=PASS disposition=CENSUS_ONLY"
        << " warm_parent_reconstructed=1"
        << " replayed_g4b_train_labels="
        << replayed_g4b_train_labels
        << " replayed_g4b_dev_labels="
        << replayed_g4b_dev_labels
        << " replayed_g4b_total_labels="
        << replayed_g4b_train_labels +
               replayed_g4b_dev_labels
        << " op1_label_coordinates_opened=0"
        << " op1_model_created=0"
        << " selector_opened=0 artifact_published=0\n";
}

void print_offline(
    std::ostream& output, const Corpus& corpus,
    const FitReport& fit_report,
    const OfflineReport& report) {
    output
        << "schema=old-school-action-q-aq4-op1-run-v1\n"
        << "mode=run train_roots=" << corpus.train.size()
        << " dev_roots=" << corpus.dev.size()
        << " fit_examples=" << fit_report.fit_examples
        << " fit_options=" << fit_report.fit_options
        << " fit_seed=" << fit_report.optimizer.seed
        << " corpus_digest=" << report.corpus_digest
        << " warm_parent=" << report.parent_fingerprint
        << " candidate=" << report.candidate_fingerprint
        << '\n';
    print_metrics(
        output, "TRAIN", "warm_parent_residual_0.10",
        report.parent_train);
    print_metrics(
        output, "TRAIN", "candidate_residual_0.10",
        report.candidate_train);
    print_metrics(
        output, "DEV", "warm_parent_residual_0.10",
        report.parent_dev);
    print_metrics(
        output, "DEV", "candidate_residual_0.10",
        report.candidate_dev);
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        output
            << "dev_guard deck="
            << deck_name(static_cast<DeckId>(deck))
            << " regret_delta="
            << report.candidate_dev.decks[deck].mean_regret -
                   report.parent_dev.decks[deck].mean_regret
            << " guard="
            << report.dev_deck_regret_guard[deck]
            << " train_parent_signal="
            << report.parent_train_signal_nonzero[deck]
            << " dev_parent_signal="
            << report.parent_dev_signal_nonzero[deck]
            << '\n';
    }
    output
        << "offline census=" << report.census_frozen
        << " digest=" << report.corpus_digest_exact
        << " preflight=" << report.preflight_exact
        << " isolation="
        << (report.parent_immutable &&
            report.repeated_fit_bit_identical &&
            report.only_priority_component_changed)
        << " train_regret="
        << report.train_regret_strictly_improved
        << " dev_regret="
        << report.dev_regret_strictly_improved
        << " targets="
        << report.targets_finite_and_normalized
        << " descriptor_order="
        << report.descriptor_order_identity
        << " redundant_counter="
        << report.redundant_counter_pass
        << " braingeyser_productive="
        << report.braingeyser_productive
        << " sick_growth="
        << report.sick_bear_growth_pass
        << " force_spike=" << report.live_force_spike
        << " ancestral=" << report.ancestral_pass
        << " model_gate_parent_residual=0"
        << " frozen_dev_labels="
        << report.frozen_dev.labels
        << " result="
        << (report.gate_passed() ? "PASS" : "FAIL")
        << '\n';
    for (const std::string& failure : report.failures) {
        output
            << "offline_failure message="
            << std::quoted(failure) << '\n';
    }
}

void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary,
    SelectorDisposition disposition) {
    g1::print_selector(output, summary, disposition);
}

namespace testing {

Census make_census(
    std::string parent_fingerprint,
    std::array<SplitCensus, 2> splits,
    std::vector<ManifestRoot> roots) {
    for (ManifestRoot& root : roots) {
        root.information_action_fingerprint =
            information_action_fingerprint(root);
        root.stable_root_id = stable_root_id(root);
    }
    Census census{
        .root_seed = kCollectionRootSeed,
        .parent_fingerprint =
            std::move(parent_fingerprint),
        .splits = std::move(splits),
        .roots = std::move(roots),
    };
    census.manifest_hash =
        action_q_on_policy_successor::
            canonical_manifest_hash(census);
    return census;
}

Corpus make_corpus(
    Census census,
    LearnedModelComponentFingerprints parent_components,
    std::vector<RootExample> train,
    std::vector<RootExample> dev) {
    Corpus corpus{
        .census = std::move(census),
        .parent_components =
            std::move(parent_components),
        .train = std::move(train),
        .dev = std::move(dev),
    };
    corpus.digest =
        action_q_on_policy_successor::
            canonical_corpus_digest(corpus);
    return corpus;
}

std::vector<LearnedValuePriorityTrainingExample>
training_examples(const Corpus& corpus) {
    action_q_on_policy_successor::validate_corpus(
        corpus);
    return project_training_examples(corpus);
}

bool selector_binding_exact(
    const Corpus& corpus, const FitReport& fit_report,
    const OfflineReport& offline,
    std::string_view c16_fingerprint,
    std::string_view warm_parent_fingerprint,
    std::string_view candidate_fingerprint,
    std::string_view required_census_hash,
    const CensusCountSeal& required_census_counts) {
    return selector_binding_exact_impl(
        corpus, fit_report, offline,
        c16_fingerprint, warm_parent_fingerprint,
        candidate_fingerprint, required_census_hash,
        required_census_counts);
}

} // namespace testing

} // namespace old_school::action_q_on_policy_successor
