#include "old_school/action_q_nested_actor_broad_distill.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <numeric>
#include <ostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::action_q_nested_actor_broad_distill {
namespace {

constexpr std::string_view kManifestSchema =
    "old-school-action-q-aq4-g4b-census-v1";
constexpr std::string_view kInformationActionSchema =
    "old-school-action-q-aq4-g4b-owner-information-action-v1";
constexpr std::string_view kStableRootSchema =
    "old-school-action-q-aq4-g4b-stable-root-v1";
constexpr std::string_view kCorpusSchema =
    "old-school-action-q-aq4-g4b-corpus-v1";
constexpr std::string_view kPreflightSchema =
    "old-school-action-q-aq4-g4b-preflight-v1";

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range(
            "AQ4-G4B deck index is invalid");
    }
    return index;
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ4-G4B requires exact frozen C16");
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
    append_u64(output, value.has_value() ? 1U : 0U);
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
    append_u64(output, action.target.has_value() ? 1U : 0U);
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
    append_u64(
        output,
        static_cast<std::uint64_t>(coordinate.split));
    append_size(output, coordinate.schedule_index);
    append_size(output, coordinate.pairing_index);
    append_u64(output, coordinate.game_seed);
    append_size(output, coordinate.starting_player);
    for (const DeckId deck : coordinate.seat_decks) {
        append_u64(
            output,
            static_cast<std::uint64_t>(deck));
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

void append_accounting(
    std::string& output,
    const action_q_nested_actor_diagnostic::
        EvaluationAccounting& accounting) {
    append_size(output, accounting.sampled_worlds);
    append_size(output, accounting.rollout_evaluations);
    append_size(output, accounting.terminal_evaluations);
    append_size(output, accounting.bootstrapped_evaluations);
    append_size(output, accounting.inner_rollout_evaluations);
    append_size(output, accounting.inner_search_invocations);
    append_size(output, accounting.inner_search_max_depth);
}

void append_root_score(
    std::string& output,
    const action_q_nested_actor_diagnostic::RootScore& score) {
    append_size(output, score.actions.size());
    for (const auto& action : score.actions) {
        append_string(output, action.probe_key);
        append_string(output, action.typed_descriptor);
        append_action(output, action.action);
        append_size(output, action.samples.size());
        for (const double sample : action.samples) {
            append_double(output, sample);
        }
        append_size(
            output,
            action.inner_rollout_evaluations.size());
        for (const std::size_t count :
             action.inner_rollout_evaluations) {
            append_size(output, count);
        }
        append_size(
            output,
            action.inner_search_invocations.size());
        for (const std::size_t count :
             action.inner_search_invocations) {
            append_size(output, count);
        }
        append_size(
            output,
            action.inner_search_max_depth.size());
        for (const std::size_t depth :
             action.inner_search_max_depth) {
            append_size(output, depth);
        }
        append_double(output, action.mean);
        append_bool(output, action.exact_max);
    }
    append_accounting(output, score.accounting);
    append_string(output, score.selected_probe_key);
}

void append_fixture_spec(
    std::string& output,
    const action_q_nested_actor_diagnostic::FixtureSpec&
        spec) {
    append_size(output, spec.fixture_index);
    append_string(output, spec.stable_id);
    append_u64(
        output, static_cast<std::uint64_t>(spec.kind));
    append_string(output, spec.positive_key);
    append_string(output, spec.negative_key);
    append_string(output, spec.secondary_negative_key);
    for (const std::string_view key : spec.excluded_keys) {
        append_string(output, key);
    }
    append_u64(output, spec.expected_seed);
}

void append_direction(
    std::string& output,
    const action_q_nested_actor_diagnostic::
        DirectionSummary& direction) {
    append_bool(output, direction.passed);
    append_double(output, direction.required_margin);
    append_double(output, direction.positive_value);
    append_double(output, direction.negative_value);
    for (const double margin :
         direction.excluded_margins) {
        append_double(output, margin);
    }
    append_size(output, direction.exact_max_support.size());
    for (const std::string& key :
         direction.exact_max_support) {
        append_string(output, key);
    }
}

void append_preflight_report(
    std::string& output,
    const action_q_nested_actor_diagnostic::PreflightReport&
        report) {
    append_string(output, kPreflightSchema);
    append_u64(output, report.recipe.root_seed);
    append_size(output, report.recipe.worlds);
    append_size(output, report.recipe.rollouts_per_world);
    append_size(output, report.recipe.horizon_turns);
    append_size(output, report.recipe.evaluation_threads);
    append_size(output, report.recipe.inner_worlds);

    const auto& evidence = report.evidence;
    append_string(output, evidence.parent_fingerprint);
    append_size(output, evidence.fixtures.size());
    for (const auto& fixture : evidence.fixtures) {
        append_fixture_spec(output, fixture.spec);
        append_u64(output, fixture.seed);
        append_root_score(output, fixture.score);
        append_direction(output, fixture.direction);
        append_bool(
            output, fixture.hidden_repartition_nonvacuous);
        append_bool(
            output,
            fixture.hidden_repartition_bit_identical);
        append_bool(
            output, fixture.reversed_action_bit_identical);
    }
    const auto& actor = evidence.actor_local;
    append_u64(output, actor.seed);
    append_root_score(output, actor.score);
    append_bool(
        output, actor.hidden_repartition_nonvacuous);
    append_bool(output, actor.observation_bit_identical);
    append_bool(output, actor.legal_actions_bit_identical);
    append_bool(output, actor.score_bit_identical);
    append_bool(output, actor.one_level_nesting_bounded);
    for (const bool passed : evidence.direction_passed) {
        append_bool(output, passed);
    }
    append_bool(output, evidence.hypothesis_passed);
}

std::string information_action_fingerprint(
    const ManifestRoot& root) {
    std::string payload;
    append_string(payload, kInformationActionSchema);
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
        "AQ4-G4B source deck is invalid");
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
            "AQ4-G4B retained source context is invalid");
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
            root_search_seed(
                split, scheduled.schedule_index,
                actor, nontrivial_ordinal),
    };
    root.actions =
        legal_priority_actions(
            point.state, actor,
            point.context.sorcery_actions);
    if (root.actions.size() < 2) {
        throw std::logic_error(
            "AQ4-G4B retained a trivial Priority root");
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
    const std::shared_ptr<const LearnedModel>& parent,
    Visitor&& visitor) {
    require_parent(parent);
    Census census;
    census.parent_fingerprint =
        learned_model_fingerprint(parent);
    census.splits = {
        SplitCensus{.split = Split::Train},
        SplitCensus{.split = Split::Dev},
    };

    for (const Split split :
         std::array<Split, 2>{
             Split::Train, Split::Dev}) {
        SplitCensus& split_census =
            census.splits[split_index(split)];
        const auto& schedule = schedule_for(split);
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
                        "AQ4-G4B source has a rootless actor-game");
                }
                const auto retained =
                    learned_iteration::
                        evenly_spaced_retained_indices(
                            candidates.size(),
                            maximum_roots_per_actor_game(split));
                if (retained.empty()) {
                    throw std::logic_error(
                        "AQ4-G4B retention returned no root");
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
        canonical_manifest_hash(census);
    validate_census(census);
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
            "AQ4-G4B outer search accounting drifted");
    }
    for (const auto& row : samples.q_samples) {
        if (row.size() != worlds ||
            !std::all_of(
                row.begin(), row.end(),
                finite_probability)) {
            throw std::logic_error(
                "AQ4-G4B outer sample shape drifted");
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
                "AQ4-G4B base scorer opened nested search");
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
            "AQ4-G4B teacher nested-search shape drifted");
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
                "AQ4-G4B teacher inner matrix drifted");
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
                    "AQ4-G4B teacher inner cell drifted");
            }
            if (rollout_sum >
                    std::numeric_limits<std::size_t>::max() -
                        rollouts[world] ||
                invocation_sum >
                    std::numeric_limits<std::size_t>::max() -
                        invocations[world]) {
                throw std::overflow_error(
                    "AQ4-G4B teacher accounting overflow");
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
            "AQ4-G4B teacher inner accounting does not cross-sum");
    }
}

RootExample label_root(
    const ManifestRoot& manifest,
    const LearnedDecisionTracePoint& point,
    const std::array<std::vector<CardId>, 2>& decks,
    const std::shared_ptr<const LearnedModel>& parent) {
    const RootCoordinate& coordinate = manifest.coordinate;
    if (!point.context.valid ||
        point.context.decision_player != coordinate.actor) {
        throw std::logic_error(
            "AQ4-G4B label source context drifted");
    }
    const LearnedActionSamples base =
        learned_priority_action_samples(
            point.state, decks, coordinate.actor,
            point.context.sorcery_actions,
            point.context.phase,
            point.context.consecutive_passes,
            manifest.actions, parent,
            base_search_config(coordinate.search_seed));
    const LearnedActionSamples teacher =
        learned_priority_action_samples(
            point.state, decks, coordinate.actor,
            point.context.sorcery_actions,
            point.context.phase,
            point.context.consecutive_passes,
            manifest.actions, parent,
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
            "AQ4-G4B labeled root differs from its census row");
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
            "AQ4-G4B labeled root has invalid values");
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
            "AQ4-G4B labeled root accounting is invalid");
    }
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

void append_components(
    std::string& output,
    const LearnedModelComponentFingerprints& components) {
    append_string(output, components.critic);
    append_string(output, components.priority);
    append_string(output, components.attack);
    append_string(output, components.block);
    append_string(output, components.damage_order);
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
        output,
        config.value_continuation_search_worlds);
}

void append_source_recipe(std::string& output) {
    const GameConfig game = source_game_config({}, 0);
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
        append_bool(
            output, bot.value_pass_dominance);
        append_double(
            output,
            bot.value_resolved_shallow_prior_weight);
        append_bool(
            output, bot.value_adversarial_blocks);
        append_u64(
            output,
            static_cast<std::uint64_t>(
                bot.value_continuation_controller));
        append_size(output, bot.training_games);
    }
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

void add_failure(
    OfflineReport& report, bool passed,
    std::string message) {
    if (!passed) {
        report.failures.push_back(std::move(message));
    }
}

} // namespace

DeckId RootCoordinate::owner_deck() const {
    if (actor >= seat_decks.size()) {
        throw std::out_of_range(
            "AQ4-G4B root actor is invalid");
    }
    return seat_decks[actor];
}

std::size_t SplitCensus::retained_roots() const {
    return std::accumulate(
        decks.begin(), decks.end(), std::size_t{0},
        [](std::size_t total, const DeckCensus& deck) {
            return total + deck.retained_roots;
        });
}

std::size_t SplitCensus::retained_options() const {
    return std::accumulate(
        decks.begin(), decks.end(), std::size_t{0},
        [](std::size_t total, const DeckCensus& deck) {
            return total + deck.retained_options;
        });
}

std::size_t Census::games() const {
    return splits[0].games + splits[1].games;
}

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() != 1) {
        return std::nullopt;
    }
    if (arguments.front() == "--preflight") {
        return Command::Preflight;
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
        << "Usage: old-school-action-q-broad-distill "
           "(--preflight|--census|--run)\n";
}

std::size_t split_index(Split split) {
    const std::size_t index =
        static_cast<std::size_t>(split);
    if (index >= 2) {
        throw std::out_of_range(
            "AQ4-G4B split is invalid");
    }
    return index;
}

std::size_t maximum_roots_per_actor_game(Split split) {
    switch (split) {
    case Split::Train:
        return kTrainMaximumRootsPerActorGame;
    case Split::Dev:
        return kDevMaximumRootsPerActorGame;
    }
    throw std::out_of_range(
        "AQ4-G4B split cap is invalid");
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
            "AQ4-G4B root search coordinate is invalid");
    }
    const std::uint64_t subindex =
        (static_cast<std::uint64_t>(actor) << 32) |
        static_cast<std::uint64_t>(nontrivial_ordinal);
    return learned_iteration::derive_seed(
        kCollectionRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        split_index(split), schedule_index, subindex);
}

double root_weight(
    std::size_t actor_game_retained_roots) {
    if (actor_game_retained_roots == 0 ||
        actor_game_retained_roots >
            kTrainMaximumRootsPerActorGame) {
        throw std::out_of_range(
            "AQ4-G4B retained-root weight denominator is invalid");
    }
    return 1.0 /
           (static_cast<double>(
                kActorGamesPerDeckAndSplit) *
            static_cast<double>(
                actor_game_retained_roots));
}

action_q_nested_actor_diagnostic::PreflightRecipe
preflight_recipe() {
    return g1::preflight_recipe();
}

std::string canonical_preflight_digest(
    const action_q_nested_actor_diagnostic::PreflightReport&
        report) {
    std::string payload;
    append_preflight_report(payload, report);
    return artifact_integrity::sha256_string(payload);
}

bool preflight_exact(
    const action_q_nested_actor_diagnostic::PreflightReport&
        report) {
    return !kFrozenPreflightDigest.empty() &&
           report.recipe == preflight_recipe() &&
           report.evidence.parent_fingerprint ==
               kRequiredParentFingerprint &&
           report.gate_passed() &&
           canonical_preflight_digest(report) ==
               kFrozenPreflightDigest;
}

LearnedSearchConfig base_search_config(
    std::uint64_t seed) {
    return g1::base_search_config(seed);
}

LearnedSearchConfig teacher_search_config(
    std::uint64_t seed) {
    return g1::teacher_search_config(seed);
}

LearnedValuePriorityHeadUpdateConfig optimizer_config() {
    return g1::optimizer_config();
}

std::string canonical_manifest_hash(
    const Census& census) {
    std::string payload;
    append_string(payload, kManifestSchema);
    append_u64(payload, census.root_seed);
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

void validate_census(const Census& census) {
    if (census.root_seed != kCollectionRootSeed ||
        census.parent_fingerprint !=
            kRequiredParentFingerprint ||
        census.splits[0].split != Split::Train ||
        census.splits[1].split != Split::Dev ||
        census.roots.empty() ||
        census.manifest_hash.size() != 64 ||
        census.manifest_hash !=
            canonical_manifest_hash(census)) {
        throw std::invalid_argument(
            "AQ4-G4B census identity or manifest hash is invalid");
    }

    std::set<std::uint64_t> game_seeds;
    std::set<std::string> stable_root_ids;
    std::size_t root_position = 0;
    for (const Split split :
         std::array<Split, 2>{Split::Train, Split::Dev}) {
        const std::size_t split_value = split_index(split);
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
                "AQ4-G4B split census shape is invalid");
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
                    "AQ4-G4B source schedules are malformed or collide");
            }
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                if (actor_game_position >=
                    recorded.actor_games.size()) {
                    throw std::invalid_argument(
                        "AQ4-G4B actor-game census is truncated");
                }
                const ActorGameCensus& actor_game =
                    recorded.actor_games[
                        actor_game_position++];
                const DeckId owner =
                    scheduled.seat_decks[actor];
                const std::size_t retained =
                    std::min(
                        actor_game.nontrivial_roots,
                        maximum_roots_per_actor_game(split));
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
                        "AQ4-G4B actor-game census row is invalid");
                }
                const auto retained_indices =
                    learned_iteration::
                        evenly_spaced_retained_indices(
                            actor_game.nontrivial_roots,
                            maximum_roots_per_actor_game(split));
                if (retained_indices.size() !=
                    actor_game.retained_roots) {
                    throw std::logic_error(
                        "AQ4-G4B blind retention drifted");
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
                            "AQ4-G4B census is missing retained roots");
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
                            root_search_seed(
                                split,
                                scheduled.schedule_index,
                                actor,
                                coordinate
                                    .nontrivial_ordinal)) {
                        throw std::invalid_argument(
                            "AQ4-G4B retained root coordinate drifted");
                    }
                    previous_trace_ordinal =
                        coordinate.trace_ordinal;
                    if (root.actions.size() < 2 ||
                        root.action_descriptors.size() !=
                            root.actions.size() ||
                        root.options.size() !=
                            root.actions.size()) {
                        throw std::invalid_argument(
                            "AQ4-G4B manifest action shape is invalid");
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
                                "AQ4-G4B manifest action or feature drifted");
                        }
                        for (std::size_t earlier = 0;
                             earlier < action; ++earlier) {
                            if (root.actions[earlier] ==
                                    root.actions[action] ||
                                root.action_descriptors[earlier] ==
                                    root.action_descriptors[action]) {
                                throw std::invalid_argument(
                                    "AQ4-G4B manifest duplicates an action");
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
                            "AQ4-G4B owner-safe root identity drifted or collided");
                    }
                    observed_options += root.actions.size();
                }
                if (observed_options !=
                    actor_game.retained_options) {
                    throw std::invalid_argument(
                        "AQ4-G4B actor-game option cross-sum drifted");
                }
                deck.retained_options += observed_options;
            }
        }
        if (actor_game_position !=
                recorded.actor_games.size() ||
            observed != recorded.decks) {
            throw std::invalid_argument(
                "AQ4-G4B split/deck census cross-sum drifted");
        }
        for (const DeckCensus& deck : recorded.decks) {
            if (deck.actor_games !=
                    kActorGamesPerDeckAndSplit ||
                deck.retained_roots == 0 ||
                deck.retained_options <
                    deck.retained_roots * 2) {
                throw std::invalid_argument(
                    "AQ4-G4B census is missing a balanced deck");
            }
        }
    }
    if (root_position != census.roots.size() ||
        census.games() != 2 * kGamesPerSplit) {
        throw std::invalid_argument(
            "AQ4-G4B census contains unaccounted roots or games");
    }
}

void require_frozen_census(const Census& census) {
    validate_census(census);
    if (kFrozenCensusManifestHash.empty()) {
        throw std::runtime_error(
            "AQ4-G4B census hash is not frozen; --run is sealed");
    }
    if (census.manifest_hash !=
        kFrozenCensusManifestHash) {
        throw std::runtime_error(
            "AQ4-G4B census differs from its frozen manifest");
    }
}

Census collect_census(
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    if (kFrozenPreflightDigest.empty()) {
        throw std::runtime_error(
            "AQ4-G4B preflight digest is not frozen; --census is sealed");
    }
    action_q_nested_actor_diagnostic::
        validate_fixture_witnesses();
    return visit_source_roots(
        parent,
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
    append_double(payload, kCandidateResidualWeight);
    append_string(payload, kFrozenPreflightDigest);
    append_source_recipe(payload);
    append_search_config(
        payload, base_search_config(0));
    append_search_config(
        payload, teacher_search_config(0));
    append_string(payload, corpus.census.manifest_hash);
    append_string(
        payload, corpus.census.parent_fingerprint);
    append_components(payload, corpus.parent_components);
    append_optimizer(payload, optimizer_config());
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

Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const Census& frozen_census,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight) {
    require_parent(parent);
    require_frozen_census(frozen_census);
    if (!preflight_exact(preflight)) {
        throw std::invalid_argument(
            "AQ4-G4B labeling requires exact frozen preflight evidence");
    }
    if (learned_model_fingerprint(parent) !=
        frozen_census.parent_fingerprint) {
        throw std::invalid_argument(
            "AQ4-G4B census and labeling parent differ");
    }

    Corpus result;
    result.parent_components =
        learned_model_component_fingerprints(parent);
    std::size_t manifest_position = 0;
    const Census reconstructed =
        visit_source_roots(
            parent,
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
                        "AQ4-G4B source root differs from frozen census");
                }
                RootExample example =
                    label_root(root, point, decks, parent);
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
            "AQ4-G4B reconstructed census is not bit-exact");
    }
    result.census = reconstructed;
    result.digest = canonical_corpus_digest(result);
    validate_corpus(result);
    return result;
}

void validate_corpus(const Corpus& corpus) {
    validate_census(corpus.census);
    if (corpus.parent_components.critic.size() != 64 ||
        corpus.parent_components.priority.size() != 64 ||
        corpus.parent_components.attack.size() != 64 ||
        corpus.parent_components.block.size() != 64 ||
        corpus.parent_components.damage_order.size() != 64 ||
        corpus.digest.size() != 64 ||
        corpus.digest != canonical_corpus_digest(corpus)) {
        throw std::invalid_argument(
            "AQ4-G4B corpus digest is invalid");
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
            split_index(expected.coordinate.split);
        const auto& examples =
            expected.coordinate.split == Split::Train
                ? corpus.train
                : corpus.dev;
        if (positions[split] >= examples.size()) {
            throw std::invalid_argument(
                "AQ4-G4B corpus is missing a census root");
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
                "AQ4-G4B corpus accounting overflow");
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
            "AQ4-G4B corpus accounting is incomplete");
    }
    for (const auto& split_mass : deck_mass) {
        for (const double mass : split_mass) {
            if (std::abs(mass - 1.0) > 1.0e-12) {
                throw std::invalid_argument(
                    "AQ4-G4B deck-balanced weight mass drifted");
            }
        }
    }
}

Metrics evaluate(
    std::span<const RootExample> examples,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight) {
    if (!model || examples.empty() ||
        !std::isfinite(residual_weight) ||
        residual_weight < 0.0 ||
        residual_weight > 1.0) {
        throw std::invalid_argument(
            "AQ4-G4B metric inputs are invalid");
    }
    Metrics result;
    std::array<double, kDeckCount> agreement_sums{};
    std::array<double, kDeckCount> regret_sums{};
    for (const RootExample& example : examples) {
        if (!std::isfinite(example.weight) ||
            example.weight <= 0.0) {
            throw std::invalid_argument(
                "AQ4-G4B metric root weight is invalid");
        }
        const auto logits =
            learned_policy_head_logits(
                example.manifest.options,
                LearnedPolicyDecisionKind::Priority,
                model);
        const auto scores =
            action_q_explore::combined_scores(
                example.base_scores, logits,
                residual_weight);
        const auto root_metrics =
            action_q_explore::evaluate_root(
                example.teacher_scores, scores);
        const std::size_t deck =
            deck_index(
                example.manifest.coordinate.owner_deck());
        DeckMetrics& deck_metrics =
            result.decks[deck];
        deck_metrics.deck =
            static_cast<DeckId>(deck);
        ++deck_metrics.roots;
        deck_metrics.options +=
            root_metrics.action_count;
        deck_metrics.weight_mass += example.weight;
        agreement_sums[deck] +=
            example.weight *
            root_metrics.top_one_expected_agreement;
        regret_sums[deck] +=
            example.weight * root_metrics.regret;
    }

    double agreement_total = 0.0;
    double regret_total = 0.0;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        DeckMetrics& metrics = result.decks[deck];
        if (metrics.roots == 0 ||
            std::abs(metrics.weight_mass - 1.0) >
                1.0e-12) {
            throw std::invalid_argument(
                "AQ4-G4B metrics are not deck balanced");
        }
        metrics.top_one_expected_agreement =
            agreement_sums[deck] /
            metrics.weight_mass;
        metrics.mean_regret =
            regret_sums[deck] /
            metrics.weight_mass;
        result.roots += metrics.roots;
        result.options += metrics.options;
        agreement_total +=
            metrics.top_one_expected_agreement;
        regret_total += metrics.mean_regret;
    }
    result.equal_deck_top_one_expected_agreement =
        agreement_total /
        static_cast<double>(kDeckCount);
    result.equal_deck_mean_regret =
        regret_total /
        static_cast<double>(kDeckCount);
    return result;
}

FitReport fit(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent) {
    require_parent(parent);
    validate_corpus(corpus);
    require_frozen_census(corpus.census);
    const auto actual_parent_components =
        learned_model_component_fingerprints(parent);
    if (learned_model_fingerprint(parent) !=
            corpus.census.parent_fingerprint ||
        corpus.parent_components !=
            actual_parent_components) {
        throw std::invalid_argument(
            "AQ4-G4B fit parent differs from the corpus");
    }

    FitReport report;
    report.corpus_digest = corpus.digest;
    report.parent_fingerprint_before =
        learned_model_fingerprint(parent);
    report.parent_components =
        actual_parent_components;
    report.optimizer = optimizer_config();
    report.fit_examples = corpus.train.size();
    std::vector<LearnedValuePriorityTrainingExample>
        examples = project_training_examples(corpus);
    for (const auto& example : examples) {
        if (example.options.size() >
            std::numeric_limits<std::size_t>::max() -
                report.fit_options) {
            throw std::overflow_error(
                "AQ4-G4B fit option count overflow");
        }
        report.fit_options += example.options.size();
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
    report.parent_train =
        evaluate(corpus.train, parent, 0.0);
    report.candidate_train =
        evaluate(
            corpus.train, report.candidate,
            kCandidateResidualWeight);
    report.parent_dev =
        evaluate(corpus.dev, parent, 0.0);
    report.candidate_dev =
        evaluate(
            corpus.dev, report.candidate,
            kCandidateResidualWeight);
    return report;
}

namespace {

bool equal_deck_probe_metrics(
    const probe_eval::DeckProbeMetrics& left,
    const probe_eval::DeckProbeMetrics& right) {
    return left.root_deck == right.root_deck &&
           left.probe_count == right.probe_count &&
           left.stable_pair_count ==
               right.stable_pair_count &&
           left.top1_expected_agreement ==
               right.top1_expected_agreement &&
           left.stable_pair_agreement ==
               right.stable_pair_agreement &&
           left.mean_regret == right.mean_regret &&
           left.critic_brier == right.critic_brier &&
           left.critic_mse == right.critic_mse &&
           left.critic_log_loss ==
               right.critic_log_loss &&
           left.critic_bias == right.critic_bias &&
           left.critic_ece == right.critic_ece;
}

bool equal_probe_metrics(
    const probe_eval::ProbeMetricSummary& left,
    const probe_eval::ProbeMetricSummary& right) {
    if (left.probe_count != right.probe_count ||
        left.stable_pair_count !=
            right.stable_pair_count ||
        left.top1_expected_agreement !=
            right.top1_expected_agreement ||
        left.stable_pair_agreement !=
            right.stable_pair_agreement ||
        left.mean_regret != right.mean_regret ||
        left.critic_brier != right.critic_brier ||
        left.critic_mse != right.critic_mse ||
        left.critic_log_loss != right.critic_log_loss ||
        left.critic_bias != right.critic_bias ||
        left.critic_ece != right.critic_ece) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (!equal_deck_probe_metrics(
                left.by_deck[deck],
                right.by_deck[deck])) {
            return false;
        }
    }
    return true;
}

bool equal_hidden_repartition(
    const probe_runner::HiddenRepartitionSummary& left,
    const probe_runner::HiddenRepartitionSummary& right) {
    return left.passed == right.passed &&
           left.policy_count == right.policy_count &&
           left.probe_count == right.probe_count;
}

bool equal_frozen_dev(
    const action_q_offline_gate::FrozenDevGate& left,
    const action_q_offline_gate::FrozenDevGate& right) {
    return equal_probe_metrics(left.parent, right.parent) &&
           equal_probe_metrics(
               left.candidate, right.candidate) &&
           left.labels == right.labels &&
           left.labels_by_deck == right.labels_by_deck &&
           left.stable_parent_agreements ==
               right.stable_parent_agreements &&
           left.lost_stable_parent_agreements ==
               right.lost_stable_parent_agreements &&
           equal_hidden_repartition(
               left.pair_hidden_repartition,
               right.pair_hidden_repartition) &&
           equal_hidden_repartition(
               left.explicit_hidden_repartition,
               right.explicit_hidden_repartition) &&
           left.cache_before == right.cache_before &&
           left.cache_after == right.cache_after &&
           left.pooled_regret_no_worse ==
               right.pooled_regret_no_worse;
}

bool equal_ancestral(
    const action_q_offline_gate::AncestralGate& left,
    const action_q_offline_gate::AncestralGate& right) {
    return left.self_score == right.self_score &&
           left.opponent_score == right.opponent_score &&
           left.legal_actions == right.legal_actions &&
           left.selected_support ==
               right.selected_support &&
           left.information_action_fingerprint ==
               right.information_action_fingerprint &&
           left.complete_legal_actions_exact ==
               right.complete_legal_actions_exact &&
           left.information_action_fingerprint_exact ==
               right.information_action_fingerprint_exact &&
           left.hidden_repartition_bit_identical ==
               right.hidden_repartition_bit_identical &&
           left.self_strictly_above_opponent ==
               right.self_strictly_above_opponent &&
           left.opponent_absent_from_support ==
               right.opponent_absent_from_support;
}

} // namespace

bool OfflineReport::gate_passed() const {
    return failures.empty() &&
           census_frozen &&
           corpus_digest_exact &&
           preflight_exact &&
           parent_immutable &&
           repeated_fit_bit_identical &&
           only_priority_component_changed &&
           train_regret_strictly_improved &&
           dev_regret_strictly_improved &&
           std::all_of(
               dev_deck_regret_guard.begin(),
               dev_deck_regret_guard.end(),
               [](bool passed) { return passed; }) &&
           std::all_of(
               parent_train_signal_nonzero.begin(),
               parent_train_signal_nonzero.end(),
               [](bool passed) { return passed; }) &&
           std::all_of(
               parent_dev_signal_nonzero.begin(),
               parent_dev_signal_nonzero.end(),
               [](bool passed) { return passed; }) &&
           targets_finite_and_normalized &&
           descriptor_order_identity &&
           redundant_counter_pass &&
           braingeyser_productive &&
           sick_bear_growth_pass &&
           live_force_spike &&
           ancestral_pass;
}

bool OfflineReport::operator==(
    const OfflineReport& other) const {
    return corpus_digest == other.corpus_digest &&
           parent_fingerprint ==
               other.parent_fingerprint &&
           candidate_fingerprint ==
               other.candidate_fingerprint &&
           census_frozen == other.census_frozen &&
           corpus_digest_exact ==
               other.corpus_digest_exact &&
           preflight_exact == other.preflight_exact &&
           parent_immutable == other.parent_immutable &&
           repeated_fit_bit_identical ==
               other.repeated_fit_bit_identical &&
           only_priority_component_changed ==
               other.only_priority_component_changed &&
           train_regret_strictly_improved ==
               other.train_regret_strictly_improved &&
           dev_regret_strictly_improved ==
               other.dev_regret_strictly_improved &&
           dev_deck_regret_guard ==
               other.dev_deck_regret_guard &&
           parent_train_signal_nonzero ==
               other.parent_train_signal_nonzero &&
           parent_dev_signal_nonzero ==
               other.parent_dev_signal_nonzero &&
           targets_finite_and_normalized ==
               other.targets_finite_and_normalized &&
           descriptor_order_identity ==
               other.descriptor_order_identity &&
           redundant_counter_pass ==
               other.redundant_counter_pass &&
           braingeyser_productive ==
               other.braingeyser_productive &&
           sick_bear_growth_pass ==
               other.sick_bear_growth_pass &&
           live_force_spike == other.live_force_spike &&
           ancestral_pass == other.ancestral_pass &&
           equal_frozen_dev(
               frozen_dev, other.frozen_dev) &&
           equal_ancestral(ancestral, other.ancestral) &&
           parent_train == other.parent_train &&
           candidate_train == other.candidate_train &&
           parent_dev == other.parent_dev &&
           candidate_dev == other.candidate_dev &&
           failures == other.failures;
}

OfflineReport evaluate_offline(
    const Corpus& corpus, const FitReport& fit_report,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate) {
    require_parent(parent);
    validate_corpus(corpus);
    require_frozen_census(corpus.census);
    const Metrics parent_train =
        evaluate(corpus.train, parent, 0.0);
    const Metrics candidate_train =
        evaluate(
            corpus.train, candidate,
            kCandidateResidualWeight);
    const Metrics parent_dev =
        evaluate(corpus.dev, parent, 0.0);
    const Metrics candidate_dev =
        evaluate(
            corpus.dev, candidate,
            kCandidateResidualWeight);
    if (!candidate ||
        candidate != fit_report.candidate ||
        fit_report.corpus_digest != corpus.digest ||
        fit_report.parent_components !=
            corpus.parent_components ||
        fit_report.parent_train != parent_train ||
        fit_report.candidate_train != candidate_train ||
        fit_report.parent_dev != parent_dev ||
        fit_report.candidate_dev != candidate_dev) {
        throw std::invalid_argument(
            "AQ4-G4B offline inputs do not match the fitted corpus");
    }

    const auto gates =
        action_q_offline_gate::evaluate_model_gates(
            parent, candidate);
    OfflineReport report;
    report.corpus_digest = corpus.digest;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    report.candidate_fingerprint =
        learned_model_fingerprint(candidate);
    report.census_frozen =
        !kFrozenCensusManifestHash.empty() &&
        corpus.census.manifest_hash ==
            kFrozenCensusManifestHash;
    report.corpus_digest_exact =
        corpus.digest ==
            canonical_corpus_digest(corpus) &&
        fit_report.corpus_digest == corpus.digest;
    report.preflight_exact =
        action_q_nested_actor_broad_distill::
            preflight_exact(preflight);
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
        gates.descriptor_order.gate_passed();
    report.redundant_counter_pass =
        gates.behavior
            .redundant_counter_selects_pass;
    report.braingeyser_productive =
        gates.behavior
            .braingeyser_x_zero_excluded;
    report.sick_bear_growth_pass =
        gates.behavior
            .sick_bear_growth_selects_pass;
    report.live_force_spike =
        gates.behavior.live_force_spike_preserved;
    report.ancestral_pass =
        gates.ancestral.gate_passed();
    report.frozen_dev = gates.frozen_dev;
    report.ancestral = gates.ancestral;

    add_failure(
        report, report.census_frozen,
        "census manifest is not frozen");
    add_failure(
        report, report.corpus_digest_exact,
        "corpus digest is not exact");
    add_failure(
        report, report.preflight_exact,
        "preflight report is not bit-exact");
    add_failure(
        report, report.parent_immutable,
        "parent model changed during fit");
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
            "parent TRAIN signal is zero for " + name);
        add_failure(
            report,
            report.parent_dev_signal_nonzero[deck],
            "parent DEV signal is zero for " + name);
    }
    add_failure(
        report, report.targets_finite_and_normalized,
        "teacher targets are not finite and normalized");
    add_failure(
        report, report.descriptor_order_identity,
        "descriptor/order identity gate failed");
    add_failure(
        report, report.redundant_counter_pass,
        "redundant Counterspell gate failed");
    add_failure(
        report, report.braingeyser_productive,
        "productive Braingeyser gate failed");
    add_failure(
        report, report.sick_bear_growth_pass,
        "sick-Bear Giant Growth gate failed");
    add_failure(
        report, report.live_force_spike,
        "live Force Spike gate failed");
    add_failure(
        report, report.ancestral_pass,
        "complete Ancestral gate failed");
    return report;
}

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary) {
    return g1::classify_selector(summary);
}

namespace {

bool fit_reports_exact(
    const FitReport& left, const FitReport& right) {
    return left.corpus_digest == right.corpus_digest &&
           left.parent_fingerprint_before ==
               right.parent_fingerprint_before &&
           left.parent_fingerprint_after ==
               right.parent_fingerprint_after &&
           left.candidate_fingerprint ==
               right.candidate_fingerprint &&
           left.parent_components ==
               right.parent_components &&
           left.candidate_components ==
               right.candidate_components &&
           left.optimizer == right.optimizer &&
           left.fit_examples == right.fit_examples &&
           left.fit_options == right.fit_options &&
           left.parent_immutable ==
               right.parent_immutable &&
           left.repeated_fit_bit_identical ==
               right.repeated_fit_bit_identical &&
           left.only_priority_component_changed ==
               right.only_priority_component_changed &&
           left.parent_train == right.parent_train &&
           left.candidate_train ==
               right.candidate_train &&
           left.parent_dev == right.parent_dev &&
           left.candidate_dev == right.candidate_dev;
}

bool selector_binding_exact_impl(
    const Corpus& corpus,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight,
    const FitReport& fit_report,
    const OfflineReport& supplied,
    const OfflineReport& recomputed,
    std::string_view parent_fingerprint,
    std::string_view candidate_fingerprint,
    std::string_view required_preflight_digest,
    std::string_view required_census_hash) {
    try {
        validate_corpus(corpus);
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
    return !required_preflight_digest.empty() &&
           !required_census_hash.empty() &&
           parent_fingerprint ==
               kRequiredParentFingerprint &&
           corpus.census.parent_fingerprint ==
               parent_fingerprint &&
           corpus.census.manifest_hash ==
               required_census_hash &&
           corpus.digest ==
               canonical_corpus_digest(corpus) &&
           preflight.recipe == preflight_recipe() &&
           preflight.evidence.parent_fingerprint ==
               parent_fingerprint &&
           canonical_preflight_digest(preflight) ==
               required_preflight_digest &&
           fit_report.corpus_digest == corpus.digest &&
           fit_report.parent_fingerprint_before ==
               parent_fingerprint &&
           fit_report.parent_fingerprint_after ==
               parent_fingerprint &&
           fit_report.candidate_fingerprint ==
               candidate_fingerprint &&
           fit_report.parent_components ==
               corpus.parent_components &&
           fit_report.optimizer == optimizer_config() &&
           fit_report.fit_examples ==
               corpus.train.size() &&
           fit_report.fit_options == fit_options &&
           fit_report.parent_train ==
               recomputed.parent_train &&
           fit_report.candidate_train ==
               recomputed.candidate_train &&
           fit_report.parent_dev ==
               recomputed.parent_dev &&
           fit_report.candidate_dev ==
               recomputed.candidate_dev &&
           supplied == recomputed &&
           recomputed.corpus_digest == corpus.digest &&
           recomputed.parent_fingerprint ==
               parent_fingerprint &&
           recomputed.candidate_fingerprint ==
               candidate_fingerprint &&
           recomputed.gate_passed();
}

} // namespace

BotBenchmarkSummary run_selector(
    const Corpus& corpus,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const FitReport& fit_report,
    const OfflineReport& offline) {
    require_parent(parent);
    if (!candidate || candidate == parent ||
        fit_report.candidate != candidate ||
        fit_report.parent_components !=
            learned_model_component_fingerprints(parent) ||
        fit_report.candidate_components !=
            learned_model_component_fingerprints(candidate) ||
        fit_report.candidate_fingerprint !=
            learned_model_fingerprint(candidate) ||
        fit_report.candidate_fingerprint ==
            learned_model_fingerprint(parent)) {
        throw std::invalid_argument(
            "AQ4-G4B selector requires the passing fitted candidate");
    }
    const FitReport replayed_fit = fit(corpus, parent);
    if (!replayed_fit.candidate ||
        !fit_reports_exact(
            fit_report, replayed_fit) ||
        learned_model_fingerprint(candidate) !=
            learned_model_fingerprint(
                replayed_fit.candidate) ||
        learned_model_component_fingerprints(candidate) !=
            learned_model_component_fingerprints(
                replayed_fit.candidate)) {
        throw std::invalid_argument(
            "AQ4-G4B selector candidate does not replay from TRAIN");
    }
    const OfflineReport recomputed =
        evaluate_offline(
            corpus, replayed_fit, preflight,
            parent, replayed_fit.candidate);
    if (!selector_binding_exact_impl(
            corpus, preflight, fit_report,
            offline, recomputed,
            learned_model_fingerprint(parent),
            learned_model_fingerprint(candidate),
            kFrozenPreflightDigest,
            kFrozenCensusManifestHash)) {
        throw std::invalid_argument(
            "AQ4-G4B selector authorization binding failed");
    }
    GameConfig game;
    game.max_turns = 500;
    game.learned_training_seed = 424242;
    game.learned_search_depth = 1;
    const BotBenchmarkSummary summary =
        run_bot_benchmark(
            g1::kSelectorRepetitions,
            kSelectorSeed,
            g1::selector_bot_config(
                candidate,
                kCandidateResidualWeight),
            g1::selector_bot_config(parent, 0.0),
            game, false);
    g1::validate_selector_summary(
        summary, parent, candidate, kSelectorSeed);
    return summary;
}

void print_census(
    std::ostream& output, const Census& census) {
    output
        << "schema=old-school-action-q-aq4-g4b-census-v1\n"
        << "mode=census root_seed=" << census.root_seed
        << " games=" << census.games()
        << " train_games=" << census.splits[0].games
        << " dev_games=" << census.splits[1].games
        << " train_roots="
        << census.splits[0].retained_roots()
        << " dev_roots="
        << census.splits[1].retained_roots()
        << " manifest_hash=" << census.manifest_hash
        << " base=K8R1H4 teacher=K8R1H8"
        << " inner=K2R1H4 train_cap=6 dev_cap=2"
        << " whole_game_disjoint=1\n";
    for (const Split split :
         std::array<Split, 2>{Split::Train, Split::Dev}) {
        const auto& counts =
            census.splits[split_index(split)];
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
        << " model_created=0"
        << " g4b_label_coordinates_opened=0"
        << " label_roots_scored=0"
        << " selector_opened=0 artifact_published=0\n";
}

void print_preflight(
    std::ostream& output,
    const action_q_nested_actor_diagnostic::PreflightReport&
        report) {
    g1::print_preflight(output, report);
    output
        << "preflight_digest="
        << canonical_preflight_digest(report)
        << " frozen=" << !kFrozenPreflightDigest.empty()
        << " exact=" << preflight_exact(report)
        << " disposition=CONTROL_ONLY"
        << " source_games=0 candidate_scores=0"
        << " selector_opened=0 artifact_published=0\n";
}

namespace {

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

void print_offline(
    std::ostream& output, const Corpus& corpus,
    const FitReport& fit_report,
    const OfflineReport& report) {
    output
        << "schema=old-school-action-q-aq4-g4b-run-v1\n"
        << "mode=run train_roots=" << corpus.train.size()
        << " dev_roots=" << corpus.dev.size()
        << " fit_examples=" << fit_report.fit_examples
        << " fit_options=" << fit_report.fit_options
        << " fit_seed=" << fit_report.optimizer.seed
        << " corpus_digest=" << report.corpus_digest
        << " parent=" << report.parent_fingerprint
        << " candidate=" << report.candidate_fingerprint
        << '\n';
    print_metrics(
        output, "TRAIN", "parent",
        report.parent_train);
    print_metrics(
        output, "TRAIN", "candidate",
        report.candidate_train);
    print_metrics(
        output, "DEV", "parent",
        report.parent_dev);
    print_metrics(
        output, "DEV", "candidate",
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
        << " frozen_dev_descriptive_labels="
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
        canonical_manifest_hash(census);
    return census;
}

std::vector<LearnedValuePriorityTrainingExample>
training_examples(const Corpus& corpus) {
    validate_corpus(corpus);
    return project_training_examples(corpus);
}

bool selector_binding_exact(
    const Corpus& corpus,
    const action_q_nested_actor_diagnostic::PreflightReport&
        preflight,
    const FitReport& fit_report,
    const OfflineReport& supplied,
    const OfflineReport& recomputed,
    std::string_view parent_fingerprint,
    std::string_view candidate_fingerprint,
    std::string_view required_preflight_digest,
    std::string_view required_census_hash) {
    return selector_binding_exact_impl(
        corpus, preflight, fit_report,
        supplied, recomputed,
        parent_fingerprint, candidate_fingerprint,
        required_preflight_digest,
        required_census_hash);
}

} // namespace testing

} // namespace old_school::action_q_nested_actor_broad_distill
