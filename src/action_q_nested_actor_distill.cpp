#include "old_school/action_q_nested_actor_distill.hpp"

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
#include <locale>
#include <memory>
#include <numeric>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace old_school::action_q_nested_actor_distill {
namespace {

std::size_t deck_index(DeckId deck) {
    const std::size_t index =
        static_cast<std::size_t>(deck);
    if (index >= kDeckCount) {
        throw std::out_of_range(
            "AQ4-G1 deck index is invalid");
    }
    return index;
}

std::size_t split_index(Split split) {
    const std::size_t index =
        static_cast<std::size_t>(split);
    if (index >= 2) {
        throw std::out_of_range(
            "AQ4-G1 split is invalid");
    }
    return index;
}

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

void require_parent(
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint ||
        learned_critic_schema(parent) !=
            LearnedCriticSchema::LegacyStateOnly) {
        throw std::invalid_argument(
            "AQ4-G1 requires exact frozen C16");
    }
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
        "AQ4-G1 source deck is invalid");
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

void append_optional_u64(
    std::string& output,
    const std::optional<std::uint64_t>& value) {
    append_u64(output, value.has_value() ? 1 : 0);
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
    append_u64(output, action.target.has_value() ? 1 : 0);
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

std::string manifest_payload(const Census& census) {
    std::string payload;
    append_string(
        payload,
        "old-school-action-q-aq4-g1-census-v1");
    append_u64(payload, census.root_seed);
    append_string(payload, census.parent_fingerprint);
    append_size(payload, census.games);
    for (const DeckCensus& deck : census.decks) {
        append_size(payload, deck.actor_games);
        append_size(payload, deck.nontrivial_roots);
        for (std::size_t split = 0; split < 2; ++split) {
            append_size(
                payload, deck.retained_roots[split]);
            append_size(
                payload, deck.retained_options[split]);
        }
    }
    append_size(payload, census.roots.size());
    for (const ManifestRoot& root : census.roots) {
        const RootCoordinate& coordinate =
            root.coordinate;
        append_size(payload, coordinate.schedule_index);
        append_size(payload, coordinate.pairing_index);
        append_u64(payload, coordinate.game_seed);
        append_size(payload, coordinate.starting_player);
        for (const DeckId deck : coordinate.seat_decks) {
            append_u64(
                payload,
                static_cast<std::uint64_t>(deck));
        }
        append_size(payload, coordinate.actor);
        append_size(payload, coordinate.trace_ordinal);
        append_size(
            payload, coordinate.nontrivial_ordinal);
        append_size(
            payload,
            coordinate.actor_game_nontrivial_roots);
        append_size(
            payload, coordinate.retained_position);
        append_u64(
            payload,
            static_cast<std::uint64_t>(coordinate.split));
        append_u64(payload, coordinate.search_seed);
        append_size(payload, root.actions.size());
        for (std::size_t action = 0;
             action < root.actions.size(); ++action) {
            append_action(payload, root.actions[action]);
            append_string(
                payload, root.action_descriptors[action]);
            append_size(
                payload, root.options[action].size());
            for (const double feature :
                 root.options[action]) {
                append_u64(
                    payload,
                    std::bit_cast<std::uint64_t>(
                        feature));
            }
        }
    }
    return payload;
}

ManifestRoot make_manifest_root(
    const LearnedDecisionTracePoint& point,
    const learned_iteration::ScheduledGame& scheduled,
    std::size_t actor, std::size_t trace_ordinal,
    std::size_t nontrivial_ordinal,
    std::size_t nontrivial_roots,
    std::size_t retained_position) {
    if (!point.context.valid ||
        point.context.decision_player != actor) {
        throw std::logic_error(
            "AQ4-G1 retained source context is invalid");
    }
    ManifestRoot root;
    root.coordinate = {
        .schedule_index = scheduled.schedule_index,
        .pairing_index = scheduled.pairing_index,
        .game_seed = scheduled.seed,
        .starting_player = scheduled.starting_player,
        .seat_decks = scheduled.seat_decks,
        .actor = actor,
        .trace_ordinal = trace_ordinal,
        .nontrivial_ordinal = nontrivial_ordinal,
        .actor_game_nontrivial_roots =
            nontrivial_roots,
        .retained_position = retained_position,
        .split =
            split_for_retained_position(
                retained_position),
        .search_seed =
            root_search_seed(
                scheduled.schedule_index, actor,
                nontrivial_ordinal),
    };
    root.actions =
        legal_priority_actions(
            point.state, actor,
            point.context.sorcery_actions);
    if (root.actions.size() < 2) {
        throw std::logic_error(
            "AQ4-G1 retained a trivial Priority root");
    }
    root.action_descriptors.reserve(
        root.actions.size());
    root.options.reserve(root.actions.size());
    for (const PriorityAction& action : root.actions) {
        root.action_descriptors.push_back(
            probes::stable_priority_action_descriptor(
                action));
        root.options.push_back(
            learned_priority_policy_features(
                point.state, actor, action,
                point.context.sorcery_actions,
                point.context.phase,
                point.context.consecutive_passes));
    }
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
    const auto schedule =
        learned_iteration::balanced_schedule(
            kCollectionRootSeed,
            kScheduleGeneration, kScheduleBlock);
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
        ++census.games;

        for (std::size_t actor = 0; actor < 2; ++actor) {
            DeckCensus& deck =
                census.decks[deck_index(
                    scheduled.seat_decks[actor])];
            ++deck.actor_games;
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
                ManifestRoot root =
                    make_manifest_root(
                        point, scheduled, actor,
                        selected.trace_ordinal,
                        selected.nontrivial_ordinal,
                        candidates.size(), position);
                const std::size_t split =
                    split_index(root.coordinate.split);
                ++deck.retained_roots[split];
                deck.retained_options[split] +=
                    root.actions.size();
                visitor(root, point, decks);
                census.roots.push_back(std::move(root));
            }
        }
    }
    census.manifest_hash =
        canonical_manifest_hash(census);
    validate_census(census);
    return census;
}

void add_metric_root(
    const RootExample& root,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight,
    std::array<std::size_t, kDeckCount>& roots,
    std::array<std::size_t, kDeckCount>& options,
    std::array<double, kDeckCount>& agreements,
    std::array<double, kDeckCount>& regrets) {
    const auto logits =
        learned_policy_head_logits(
            root.manifest.options,
            LearnedPolicyDecisionKind::Priority,
            model);
    const auto scores =
        action_q_explore::combined_scores(
            root.base_scores, logits,
            residual_weight);
    const auto metrics =
        action_q_explore::evaluate_root(
            root.teacher_scores, scores);
    const std::size_t deck =
        deck_index(root.manifest.coordinate.owner_deck());
    ++roots[deck];
    options[deck] += metrics.action_count;
    agreements[deck] +=
        metrics.top_one_expected_agreement;
    regrets[deck] += metrics.regret;
}

BotConfig selector_bot(
    std::shared_ptr<const LearnedModel> model,
    double residual_weight) {
    return {
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = kBaseWorlds,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight =
            residual_weight,
        .value_pass_dominance = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_adversarial_blocks = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .training_games = 800,
        .learned_model = std::move(model),
    };
}

} // namespace

DeckId RootCoordinate::owner_deck() const {
    if (actor >= seat_decks.size()) {
        throw std::out_of_range(
            "AQ4-G1 root actor is invalid");
    }
    return seat_decks[actor];
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
        << "Usage: old-school-action-q-nested-actor-distill "
           "(--census|--run)\n";
}

Split split_for_retained_position(
    std::size_t retained_position) {
    return retained_position % 2 == 0
               ? Split::Fit
               : Split::Check;
}

std::uint64_t root_search_seed(
    std::size_t schedule_index, std::size_t actor,
    std::size_t nontrivial_ordinal) {
    if (schedule_index >=
            learned_iteration::kBalancedScheduleGames ||
        actor >= 2 ||
        nontrivial_ordinal >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range(
            "AQ4-G1 root search coordinate is invalid");
    }
    const std::uint64_t subindex =
        (static_cast<std::uint64_t>(actor) << 32) |
        static_cast<std::uint64_t>(
            nontrivial_ordinal);
    return learned_iteration::derive_seed(
        kCollectionRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        0, schedule_index, subindex);
}

action_q_nested_actor_diagnostic::PreflightRecipe
preflight_recipe() {
    return {
        .root_seed = kCollectionRootSeed,
        .worlds = kTeacherWorlds,
        .rollouts_per_world = 1,
        .horizon_turns = kTeacherHorizonTurns,
        .evaluation_threads =
            kTeacherEvaluationThreads,
        .inner_worlds = kInnerWorlds,
    };
}

LearnedSearchConfig base_search_config(
    std::uint64_t seed) {
    return {
        .seed = seed,
        .worlds = kBaseWorlds,
        .rollouts_per_world = 1,
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
        .evaluation_threads =
            kTeacherEvaluationThreads,
        .capture_priority_h0_boundaries = false,
        .value_continuation_search_worlds = 0,
    };
}

LearnedSearchConfig teacher_search_config(
    std::uint64_t seed) {
    return action_q_nested_actor_diagnostic::
        preflight_outer_search_config(
            preflight_recipe(), seed);
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
        .seed = kFitSeed,
        .residual_weight =
            kCandidateResidualWeight,
        .policy_temperature = 0.10,
    };
}

std::string canonical_manifest_hash(
    const Census& census) {
    return artifact_integrity::sha256_string(
        manifest_payload(census));
}

void validate_census(const Census& census) {
    const auto schedule =
        learned_iteration::balanced_schedule(
            kCollectionRootSeed,
            kScheduleGeneration, kScheduleBlock);
    if (census.root_seed != kCollectionRootSeed ||
        census.parent_fingerprint !=
            kRequiredParentFingerprint ||
        census.games != schedule.size() ||
        census.games !=
            learned_iteration::kBalancedScheduleGames ||
        census.roots.empty() ||
        census.manifest_hash.size() != 64 ||
        census.manifest_hash !=
            canonical_manifest_hash(census)) {
        throw std::invalid_argument(
            "AQ4-G1 census identity or manifest hash is invalid");
    }

    constexpr std::size_t kActorGameCount =
        learned_iteration::kBalancedScheduleGames * 2;
    std::array<
        std::vector<const ManifestRoot*>,
        kActorGameCount>
        actor_game_roots;
    std::array<DeckCensus, kDeckCount> observed{};
    for (const auto& scheduled : schedule) {
        for (const DeckId deck : scheduled.seat_decks) {
            ++observed[deck_index(deck)].actor_games;
        }
    }

    std::optional<std::tuple<
        std::size_t, std::size_t, std::size_t>>
        previous_coordinate;
    for (const ManifestRoot& root : census.roots) {
        const RootCoordinate& coordinate =
            root.coordinate;
        if (coordinate.schedule_index >= schedule.size() ||
            coordinate.actor >= 2 ||
            coordinate.actor_game_nontrivial_roots == 0 ||
            coordinate.nontrivial_ordinal >=
                coordinate.actor_game_nontrivial_roots ||
            coordinate.trace_ordinal <
                coordinate.nontrivial_ordinal ||
            coordinate.split !=
                split_for_retained_position(
                    coordinate.retained_position) ||
            coordinate.search_seed !=
                root_search_seed(
                    coordinate.schedule_index,
                    coordinate.actor,
                    coordinate.nontrivial_ordinal)) {
            throw std::invalid_argument(
                "AQ4-G1 root coordinate is invalid");
        }
        const auto& expected =
            schedule[coordinate.schedule_index];
        if (coordinate.pairing_index !=
                expected.pairing_index ||
            coordinate.game_seed != expected.seed ||
            coordinate.starting_player !=
                expected.starting_player ||
            coordinate.seat_decks !=
                expected.seat_decks) {
            throw std::invalid_argument(
                "AQ4-G1 root source-game provenance drifted");
        }
        const auto order_key = std::tuple{
            coordinate.schedule_index,
            coordinate.actor,
            coordinate.retained_position,
        };
        if (previous_coordinate.has_value() &&
            *previous_coordinate >= order_key) {
            throw std::invalid_argument(
                "AQ4-G1 roots are not in canonical order");
        }
        previous_coordinate = order_key;

        if (root.actions.size() < 2 ||
            root.action_descriptors.size() !=
                root.actions.size() ||
            root.options.size() != root.actions.size()) {
            throw std::invalid_argument(
                "AQ4-G1 manifest root has an invalid action shape");
        }
        for (std::size_t action = 0;
             action < root.actions.size(); ++action) {
            if (root.action_descriptors[action] !=
                    probes::stable_priority_action_descriptor(
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
                    "AQ4-G1 manifest action or feature row drifted");
            }
            for (std::size_t earlier = 0;
                 earlier < action; ++earlier) {
                if (root.actions[earlier] ==
                        root.actions[action] ||
                    root.action_descriptors[earlier] ==
                        root.action_descriptors[action]) {
                    throw std::invalid_argument(
                        "AQ4-G1 manifest contains duplicate actions");
                }
            }
        }

        actor_game_roots[
            coordinate.schedule_index * 2 +
            coordinate.actor]
            .push_back(&root);
        DeckCensus& deck =
            observed[deck_index(coordinate.owner_deck())];
        const std::size_t split =
            split_index(coordinate.split);
        ++deck.retained_roots[split];
        deck.retained_options[split] +=
            root.actions.size();
    }

    for (std::size_t actor_game = 0;
         actor_game < actor_game_roots.size();
         ++actor_game) {
        const auto& roots = actor_game_roots[actor_game];
        if (roots.empty()) {
            continue;
        }
        const std::size_t schedule_index =
            actor_game / 2;
        const std::size_t actor = actor_game % 2;
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
                "AQ4-G1 actor-game retained-root count drifted");
        }
        DeckCensus& deck =
            observed[deck_index(
                schedule[schedule_index]
                    .seat_decks[actor])];
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
                 coordinate.trace_ordinal <=
                     roots[position - 1]
                         ->coordinate.trace_ordinal)) {
                throw std::invalid_argument(
                    "AQ4-G1 retained ordinal or actor-game order drifted");
            }
        }
    }

    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (observed[deck].actor_games != 16 ||
            observed[deck].retained_roots[0] == 0 ||
            observed[deck].retained_roots[1] == 0 ||
            observed[deck] != census.decks[deck]) {
            throw std::invalid_argument(
                "AQ4-G1 per-deck census is inconsistent");
        }
    }
}

void require_frozen_census(const Census& census) {
    validate_census(census);
    if (kFrozenCensusManifestHash.empty()) {
        throw std::runtime_error(
            "AQ4-G1 census hash is not frozen; --run is sealed");
    }
    if (census.manifest_hash !=
        kFrozenCensusManifestHash) {
        throw std::runtime_error(
            "AQ4-G1 reconstructed census differs from the frozen hash");
    }
}

Census collect_census(
    std::shared_ptr<const LearnedModel> parent) {
    action_q_nested_actor_diagnostic::
        validate_fixture_witnesses();
    return visit_source_roots(
        parent,
        [](const ManifestRoot&,
           const LearnedDecisionTracePoint&,
           const std::array<std::vector<CardId>, 2>&) {});
}

namespace {

void validate_outer_samples(
    const LearnedActionSamples& samples,
    std::size_t actions, std::size_t worlds,
    bool expect_inner) {
    const std::size_t expected =
        actions * worlds;
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
            probability)) {
        throw std::logic_error(
            "AQ4-G1 outer search accounting drifted");
    }
    for (const auto& row : samples.q_samples) {
        if (row.size() != worlds ||
            !std::all_of(
                row.begin(), row.end(),
                [](double score) {
                    return std::isfinite(score);
                })) {
            throw std::logic_error(
                "AQ4-G1 outer search sample shape drifted");
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
                "AQ4-G1 BaseScore unexpectedly opened nested search");
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
            "AQ4-G1 teacher nested-search shape drifted");
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
                "AQ4-G1 teacher nested-search matrix drifted");
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
                    "AQ4-G1 teacher inner-cell accounting drifted");
            }
            rollout_sum += rollouts[world];
            invocation_sum += invocations[world];
            maximum_depth =
                std::max(maximum_depth, depths[world]);
        }
    }
    if (rollout_sum !=
            samples.inner_rollout_evaluations ||
        invocation_sum !=
            samples.inner_search_invocations ||
        maximum_depth !=
            samples.inner_search_max_depth) {
        throw std::logic_error(
            "AQ4-G1 teacher inner accounting does not cross-sum");
    }
}

RootExample label_root(
    const ManifestRoot& manifest,
    const LearnedDecisionTracePoint& point,
    const std::array<std::vector<CardId>, 2>& decks,
    const std::shared_ptr<const LearnedModel>& parent) {
    const RootCoordinate& coordinate =
        manifest.coordinate;
    if (!point.context.valid ||
        point.context.decision_player != coordinate.actor) {
        throw std::logic_error(
            "AQ4-G1 label source context drifted");
    }
    const LearnedSearchConfig base_config =
        base_search_config(coordinate.search_seed);
    const LearnedSearchConfig teacher_config =
        teacher_search_config(coordinate.search_seed);
    const LearnedActionSamples base =
        learned_priority_action_samples(
            point.state, decks, coordinate.actor,
            point.context.sorcery_actions,
            point.context.phase,
            point.context.consecutive_passes,
            manifest.actions, parent, base_config);
    const LearnedActionSamples teacher =
        learned_priority_action_samples(
            point.state, decks, coordinate.actor,
            point.context.sorcery_actions,
            point.context.phase,
            point.context.consecutive_passes,
            manifest.actions, parent, teacher_config);
    validate_outer_samples(
        base, manifest.actions.size(),
        kBaseWorlds, false);
    validate_outer_samples(
        teacher, manifest.actions.size(),
        kTeacherWorlds, true);

    RootExample result;
    result.manifest = manifest;
    result.base_scores =
        base.exact_priority_aggregate_scores;
    result.teacher_scores =
        teacher.exact_priority_aggregate_scores;
    result.target_probabilities =
        learned_soft_priority_target(
            result.teacher_scores);
    result.accounting = {
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
    };
    return result;
}

void validate_example(
    const RootExample& example,
    const ManifestRoot& expected) {
    if (example.manifest != expected) {
        throw std::invalid_argument(
            "AQ4-G1 labeled root no longer maps to its census row");
    }
    const std::size_t actions =
        expected.actions.size();
    if (example.base_scores.size() != actions ||
        example.teacher_scores.size() != actions ||
        example.target_probabilities.size() != actions ||
        !std::all_of(
            example.base_scores.begin(),
            example.base_scores.end(), probability) ||
        !std::all_of(
            example.teacher_scores.begin(),
            example.teacher_scores.end(), probability) ||
        !std::all_of(
            example.target_probabilities.begin(),
            example.target_probabilities.end(),
            probability) ||
        !std::isfinite(example.weight) ||
        example.weight <= 0.0) {
        throw std::invalid_argument(
            "AQ4-G1 labeled root has invalid score shapes");
    }
    const double target_total =
        std::accumulate(
            example.target_probabilities.begin(),
            example.target_probabilities.end(), 0.0);
    if (std::abs(target_total - 1.0) > 1.0e-9) {
        throw std::invalid_argument(
            "AQ4-G1 target probabilities do not sum to one");
    }
    if (example.target_probabilities !=
        learned_soft_priority_target(
            example.teacher_scores)) {
        throw std::invalid_argument(
            "AQ4-G1 target probabilities drifted from the teacher");
    }
    const RootAccounting& accounting =
        example.accounting;
    if (accounting.base_sampled_worlds !=
            kBaseWorlds ||
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
            "AQ4-G1 labeled root accounting is invalid");
    }
}

} // namespace

Corpus collect_corpus(
    std::shared_ptr<const LearnedModel> parent,
    const Census& frozen_census) {
    require_parent(parent);
    require_frozen_census(frozen_census);
    if (learned_model_fingerprint(parent) !=
        frozen_census.parent_fingerprint) {
        throw std::invalid_argument(
            "AQ4-G1 census and labeling parent differ");
    }

    Corpus result;
    std::size_t manifest_index = 0;
    const Census reconstructed =
        visit_source_roots(
            parent,
            [&](const ManifestRoot& root,
                const LearnedDecisionTracePoint& point,
                const std::array<
                    std::vector<CardId>, 2>& decks) {
                if (manifest_index >=
                        frozen_census.roots.size() ||
                    root != frozen_census
                                .roots[manifest_index]) {
                    throw std::runtime_error(
                        "AQ4-G1 source root differs from frozen census");
                }
                RootExample example =
                    label_root(root, point, decks, parent);
                if (root.coordinate.split == Split::Fit) {
                    result.fit.push_back(
                        std::move(example));
                } else {
                    result.check.push_back(
                        std::move(example));
                }
                ++manifest_index;
            });
    if (manifest_index != frozen_census.roots.size() ||
        reconstructed != frozen_census) {
        throw std::runtime_error(
            "AQ4-G1 reconstructed census is not bit-exact");
    }
    result.census = reconstructed;
    const auto assign_weights =
        [&](std::vector<RootExample>& examples,
            Split split) {
            for (RootExample& example : examples) {
                const std::size_t deck =
                    deck_index(
                        example.manifest.coordinate
                            .owner_deck());
                const std::size_t roots =
                    result.census.decks[deck]
                        .retained_roots[
                            split_index(split)];
                if (roots == 0) {
                    throw std::logic_error(
                        "AQ4-G1 cannot weight an empty deck split");
                }
                example.weight =
                    1.0 /
                    static_cast<double>(roots);
            }
        };
    assign_weights(result.fit, Split::Fit);
    assign_weights(result.check, Split::Check);
    validate_corpus(result);
    return result;
}

void validate_corpus(const Corpus& corpus) {
    validate_census(corpus.census);
    std::array<std::size_t, 2> positions{};
    std::array<
        std::array<double, kDeckCount>, 2>
        deck_mass{};
    std::size_t total_inner_rollouts = 0;
    std::size_t total_inner_invocations = 0;
    std::size_t maximum_inner_depth = 0;
    for (const ManifestRoot& expected :
         corpus.census.roots) {
        const Split split =
            expected.coordinate.split;
        const std::size_t split_value =
            split_index(split);
        const auto& examples =
            split == Split::Fit
                ? corpus.fit
                : corpus.check;
        if (positions[split_value] >= examples.size()) {
            throw std::invalid_argument(
                "AQ4-G1 corpus is missing a census root");
        }
        const RootExample& example =
            examples[positions[split_value]++];
        validate_example(example, expected);
        const std::size_t deck =
            deck_index(expected.coordinate.owner_deck());
        const double expected_weight =
            1.0 /
            static_cast<double>(
                corpus.census.decks[deck]
                    .retained_roots[split_value]);
        if (example.weight != expected_weight) {
            throw std::invalid_argument(
                "AQ4-G1 root weight is not inverse deck/split count");
        }
        deck_mass[split_value][deck] +=
            example.weight;
        total_inner_rollouts +=
            example.accounting
                .teacher_inner_rollout_evaluations;
        total_inner_invocations +=
            example.accounting
                .teacher_inner_search_invocations;
        maximum_inner_depth =
            std::max(
                maximum_inner_depth,
                example.accounting
                    .teacher_inner_search_max_depth);
    }
    if (positions[0] != corpus.fit.size() ||
        positions[1] != corpus.check.size() ||
        total_inner_rollouts == 0 ||
        total_inner_invocations == 0 ||
        maximum_inner_depth != 1) {
        throw std::invalid_argument(
            "AQ4-G1 corpus accounting is incomplete");
    }
    for (const auto& split_mass : deck_mass) {
        for (const double mass : split_mass) {
            if (std::abs(mass - 1.0) > 1.0e-12) {
                throw std::invalid_argument(
                    "AQ4-G1 deck-balanced weight mass drifted");
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
            "AQ4-G1 metric evaluation inputs are invalid");
    }
    std::array<std::size_t, kDeckCount> roots{};
    std::array<std::size_t, kDeckCount> options{};
    std::array<double, kDeckCount> agreements{};
    std::array<double, kDeckCount> regrets{};
    for (const RootExample& example : examples) {
        add_metric_root(
            example, model, residual_weight,
            roots, options, agreements, regrets);
    }

    Metrics result;
    double agreement_total = 0.0;
    double regret_total = 0.0;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (roots[deck] == 0) {
            throw std::invalid_argument(
                "AQ4-G1 metric evaluation is missing a deck");
        }
        auto& metrics = result.decks[deck];
        metrics.deck =
            static_cast<DeckId>(deck);
        metrics.roots = roots[deck];
        metrics.options = options[deck];
        metrics.top_one_expected_agreement =
            agreements[deck] /
            static_cast<double>(roots[deck]);
        metrics.mean_regret =
            regrets[deck] /
            static_cast<double>(roots[deck]);
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
    return fit_with_optimizer(
        corpus, std::move(parent), optimizer_config());
}

FitReport fit_with_optimizer(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    LearnedValuePriorityHeadUpdateConfig optimizer) {
    return fit_with_optimizer_and_supplement(
        corpus, std::move(parent), optimizer, {});
}

FitReport fit_with_optimizer_and_supplement(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    LearnedValuePriorityHeadUpdateConfig optimizer,
    std::span<
        const LearnedValuePriorityTrainingExample>
        supplement) {
    require_parent(parent);
    validate_corpus(corpus);
    require_frozen_census(corpus.census);
    if (learned_model_fingerprint(parent) !=
        corpus.census.parent_fingerprint) {
        throw std::invalid_argument(
            "AQ4-G1 fit parent differs from the corpus");
    }

    FitReport report;
    report.parent_fingerprint_before =
        learned_model_fingerprint(parent);
    report.parent_components =
        learned_model_component_fingerprints(parent);
    report.optimizer = optimizer;
    if (supplement.size() >
        std::numeric_limits<std::size_t>::max() -
            corpus.fit.size()) {
        throw std::overflow_error(
            "AQ4-G1 supplemental example count overflow");
    }
    report.fit_examples =
        corpus.fit.size() + supplement.size();
    std::vector<LearnedValuePriorityTrainingExample>
        examples;
    examples.reserve(report.fit_examples);
    for (const RootExample& root : corpus.fit) {
        report.fit_options +=
            root.manifest.actions.size();
        examples.push_back({
            .options = root.manifest.options,
            .base_scores = root.base_scores,
            .target_probabilities =
                root.target_probabilities,
            .weight = root.weight,
        });
    }
    for (const auto& example : supplement) {
        if (example.options.size() >
            std::numeric_limits<std::size_t>::max() -
                report.fit_options) {
            throw std::overflow_error(
                "AQ4-G1 supplemental option count overflow");
        }
        report.fit_options += example.options.size();
        examples.push_back(example);
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

bool OfflineReport::gate_passed() const {
    return parent_immutable &&
           repeated_fit_bit_identical &&
           only_priority_component_changed &&
           fit_regret_strictly_improved &&
           check_regret_strictly_improved &&
           std::all_of(
               check_deck_regret_guard.begin(),
               check_deck_regret_guard.end(),
               [](bool passed) { return passed; }) &&
           descriptor_order_identity &&
           redundant_counter_pass &&
           braingeyser_x_zero_excluded &&
           sick_bear_growth_pass &&
           live_force_spike;
}

OfflineReport evaluate_offline(
    const Corpus& corpus, const FitReport& fit_report,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate) {
    require_parent(parent);
    validate_corpus(corpus);
    require_frozen_census(corpus.census);
    if (!candidate ||
        candidate != fit_report.candidate ||
        fit_report.parent_fit !=
            evaluate(corpus.fit, parent, 0.0) ||
        fit_report.candidate_fit !=
            evaluate(
                corpus.fit, candidate,
                kCandidateResidualWeight) ||
        fit_report.parent_check !=
            evaluate(corpus.check, parent, 0.0) ||
        fit_report.candidate_check !=
            evaluate(
                corpus.check, candidate,
                kCandidateResidualWeight)) {
        throw std::invalid_argument(
            "AQ4-G1 offline inputs do not match the fitted corpus");
    }
    const auto gates =
        action_q_offline_gate::evaluate_model_gates(
            parent, candidate);
    OfflineReport report;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    report.candidate_fingerprint =
        learned_model_fingerprint(candidate);
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
    report.parent_fit = fit_report.parent_fit;
    report.candidate_fit = fit_report.candidate_fit;
    report.parent_check = fit_report.parent_check;
    report.candidate_check =
        fit_report.candidate_check;
    report.fit_regret_strictly_improved =
        report.candidate_fit
                .equal_deck_mean_regret <
        report.parent_fit
                .equal_deck_mean_regret;
    report.check_regret_strictly_improved =
        report.candidate_check
                .equal_deck_mean_regret <
        report.parent_check
                .equal_deck_mean_regret;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        report.check_deck_regret_guard[deck] =
            report.candidate_check.decks[deck]
                    .mean_regret <=
            report.parent_check.decks[deck]
                    .mean_regret +
                kMaximumCheckDeckRegretIncrease;
    }
    report.descriptor_order_identity =
        gates.descriptor_order.gate_passed();
    report.redundant_counter_pass =
        gates.behavior
            .redundant_counter_selects_pass;
    report.braingeyser_x_zero_excluded =
        gates.behavior
            .braingeyser_x_zero_excluded;
    report.sick_bear_growth_pass =
        gates.behavior
            .sick_bear_growth_selects_pass;
    report.live_force_spike =
        gates.behavior.live_force_spike_preserved;
    // Frozen-dev results are reported only; they are not a conjunctive gate.
    report.frozen_dev = gates.frozen_dev;
    // The user-reported targeting failure remains descriptive because it was
    // not one of AQ4-G1's preregistered conjunctive behavior gates.
    report.ancestral = gates.ancestral;
    return report;
}

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary) {
    const bool deck_floor =
        std::all_of(
            summary.challenger_decks.begin(),
            summary.challenger_decks.end(),
            [](const DeckSimulationStats& deck) {
                return deck.wins >=
                    kMinimumDeckWins;
            });
    if (!deck_floor) {
        return SelectorDisposition::Reject;
    }
    if (summary.challenger_stats.wins >=
        kFastGoWins) {
        return SelectorDisposition::FastGo;
    }
    if (summary.challenger_stats.wins >=
        kManualOnlyWins) {
        return SelectorDisposition::ManualOnly;
    }
    return SelectorDisposition::Reject;
}

namespace {

void validate_selector(
    const BotBenchmarkSummary& summary,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    std::uint64_t expected_seed) {
    const auto accounted =
        [](const auto& stats) {
            return stats.games ==
                stats.wins + stats.losses + stats.draws;
        };
    if (summary.evaluation_seed != expected_seed ||
        summary.learned_training_seed != 424242 ||
        summary.repetitions_per_deck_pairing !=
            kSelectorRepetitions ||
        summary.total_games != kExpectedSelectorGames ||
        summary.challenger.learned_model != candidate ||
        summary.baseline.learned_model != parent ||
        summary.challenger_model_fingerprint !=
            learned_model_fingerprint(candidate) ||
        summary.baseline_model_fingerprint !=
            learned_model_fingerprint(parent) ||
        summary.challenger.kind != BotKind::Learned ||
        summary.baseline.kind != BotKind::Learned ||
        summary.challenger.learned_variant !=
            LearnedVariant::ValueSearchChampion ||
        summary.baseline.learned_variant !=
            LearnedVariant::ValueSearchChampion ||
        summary.challenger.rollouts_per_action !=
            kBaseWorlds ||
        summary.baseline.rollouts_per_action !=
            kBaseWorlds ||
        summary.challenger.exploration_rate != 0.0 ||
        summary.baseline.exploration_rate != 0.0 ||
        summary.challenger
                .value_continuation_epsilon != 0.0 ||
        summary.baseline
                .value_continuation_epsilon != 0.0 ||
        summary.challenger
                .value_priority_residual_weight !=
            kCandidateResidualWeight ||
        summary.baseline
                .value_priority_residual_weight !=
            0.0 ||
        summary.challenger.value_pass_dominance ||
        summary.baseline.value_pass_dominance ||
        summary.challenger
                .value_resolved_shallow_prior_weight != 0.0 ||
        summary.baseline
                .value_resolved_shallow_prior_weight != 0.0 ||
        summary.challenger.value_adversarial_blocks ||
        summary.baseline.value_adversarial_blocks ||
        summary.challenger
                .value_continuation_controller !=
            LearnedContinuationController::Legacy ||
        summary.baseline
                .value_continuation_controller !=
            LearnedContinuationController::Legacy ||
        summary.challenger.training_games != 800 ||
        summary.baseline.training_games != 800 ||
        !accounted(summary.challenger_stats) ||
        !accounted(summary.baseline_stats) ||
        summary.challenger_stats.games !=
            kExpectedSelectorGames ||
        summary.baseline_stats.games !=
            kExpectedSelectorGames ||
        summary.challenger_stats.wins !=
            summary.baseline_stats.losses ||
        summary.challenger_stats.losses !=
            summary.baseline_stats.wins ||
        summary.challenger_stats.draws !=
            summary.baseline_stats.draws ||
        summary.life_total_finishes +
                summary.empty_library_finishes +
                summary.turn_limit_draws !=
            kExpectedSelectorGames ||
        summary.challenger_quartet_cr1.clusters != 15 ||
        summary.challenger_quartet_cr1.records !=
            kExpectedSelectorGames) {
        throw std::runtime_error(
            "AQ4-G1 selector aggregate recipe drifted");
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& challenger =
            summary.challenger_decks[deck];
        const auto& baseline =
            summary.baseline_decks[deck];
        if (!accounted(challenger) ||
            !accounted(baseline) ||
            challenger.games !=
                kExpectedSelectorGamesPerDeck ||
            baseline.games !=
                kExpectedSelectorGamesPerDeck ||
            challenger.on_play_games != 6 ||
            challenger.on_draw_games != 6 ||
            baseline.on_play_games != 6 ||
            baseline.on_draw_games != 6) {
            throw std::runtime_error(
                "AQ4-G1 selector deck balance drifted");
        }
        for (std::size_t seat = 0; seat < 2; ++seat) {
            for (std::size_t play_draw = 0;
                 play_draw < 2; ++play_draw) {
                if (summary.challenger_outcome_quadrants
                            [deck][seat][play_draw]
                                .games != 3 ||
                    summary.baseline_outcome_quadrants
                            [deck][seat][play_draw]
                                .games != 3) {
                    throw std::runtime_error(
                        "AQ4-G1 selector quadrant balance drifted");
                }
            }
        }
        for (std::size_t opponent = 0;
             opponent < kDeckCount; ++opponent) {
            const std::size_t expected_games =
                deck == opponent ? 4 : 2;
            if (summary.challenger_deck_matchups
                        [deck][opponent]
                            .games != expected_games) {
                throw std::runtime_error(
                    "AQ4-G1 selector matchup balance drifted");
            }
        }
    }
}

} // namespace

BotConfig selector_bot_config(
    std::shared_ptr<const LearnedModel> model,
    double residual_weight) {
    return selector_bot(
        std::move(model), residual_weight);
}

void validate_selector_summary(
    const BotBenchmarkSummary& summary,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    std::uint64_t expected_seed) {
    if (expected_seed == 0) {
        throw std::invalid_argument(
            "AQ4 selector expected seed is zero");
    }
    validate_selector(
        summary, parent, candidate, expected_seed);
}

BotBenchmarkSummary run_selector(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const FitReport& fit_report,
    const OfflineReport& offline) {
    require_parent(parent);
    if (!candidate || candidate == parent ||
        learned_model_fingerprint(candidate) ==
            learned_model_fingerprint(parent) ||
        fit_report.candidate != candidate ||
        fit_report.candidate_fingerprint !=
            learned_model_fingerprint(candidate) ||
        fit_report.parent_fingerprint_after !=
            learned_model_fingerprint(parent) ||
        offline.parent_fingerprint !=
            learned_model_fingerprint(parent) ||
        offline.candidate_fingerprint !=
            learned_model_fingerprint(candidate) ||
        offline.parent_fit != fit_report.parent_fit ||
        offline.candidate_fit != fit_report.candidate_fit ||
        offline.parent_check != fit_report.parent_check ||
        offline.candidate_check !=
            fit_report.candidate_check ||
        !offline.gate_passed()) {
        throw std::invalid_argument(
            "AQ4-G1 selector requires the passing fitted candidate");
    }
    GameConfig game;
    game.max_turns = 500;
    game.learned_training_seed = 424242;
    game.learned_search_depth = 1;
    const BotBenchmarkSummary summary =
        run_bot_benchmark(
            kSelectorRepetitions, kSelectorSeed,
            selector_bot(
                candidate,
                kCandidateResidualWeight),
            selector_bot(parent, 0.0),
            game, false);
    validate_selector(
        summary, parent, candidate, kSelectorSeed);
    return summary;
}

void print_census(
    std::ostream& output, const Census& census) {
    output
        << "schema=old-school-action-q-aq4-g1-census-v1\n"
        << "mode=census root_seed=" << census.root_seed
        << " games=" << census.games
        << " roots=" << census.roots.size()
        << " manifest_hash=" << census.manifest_hash
        << " base=K8R1H4 teacher=K8R1H8"
        << " inner=K2R1H4 cap=8 split=even_fit_odd_check\n";
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& counts = census.decks[deck];
        output
            << "census_deck deck="
            << deck_name(static_cast<DeckId>(deck))
            << " actor_games=" << counts.actor_games
            << " nontrivial_roots="
            << counts.nontrivial_roots
            << " fit_roots=" << counts.retained_roots[0]
            << " check_roots=" << counts.retained_roots[1]
            << " fit_options="
            << counts.retained_options[0]
            << " check_options="
            << counts.retained_options[1] << '\n';
    }
    output
        << "result=PASS disposition=CENSUS_ONLY"
        << " model_created=0 reserved_seed_opened=0"
        << " artifact_published=0\n";
}

void print_preflight(
    std::ostream& output,
    const action_q_nested_actor_diagnostic::
        PreflightReport& report) {
    const auto print_accounting =
        [&](const action_q_nested_actor_diagnostic::
                EvaluationAccounting& accounting) {
            output
                << " outer_worlds="
                << accounting.sampled_worlds
                << " outer_rollouts="
                << accounting.rollout_evaluations
                << " terminal="
                << accounting.terminal_evaluations
                << " bootstrap="
                << accounting.bootstrapped_evaluations
                << " inner_rollouts="
                << accounting.inner_rollout_evaluations
                << " inner_invocations="
                << accounting.inner_search_invocations
                << " inner_max_depth="
                << accounting.inner_search_max_depth;
        };
    const auto print_actions =
        [&](std::string_view root,
            const action_q_nested_actor_diagnostic::
                RootScore& score) {
            for (const auto& action : score.actions) {
                output
                    << "preflight_action root=" << root
                    << " key=" << std::quoted(action.probe_key)
                    << " descriptor="
                    << std::quoted(action.typed_descriptor)
                    << " mean=" << std::setprecision(17)
                    << action.mean
                    << " exact_max=" << action.exact_max
                    << " samples=" << action.samples.size()
                    << " inner_rollout_cross_sum="
                    << std::accumulate(
                           action
                               .inner_rollout_evaluations
                               .begin(),
                           action
                               .inner_rollout_evaluations
                               .end(),
                           std::size_t{0})
                    << '\n';
            }
        };
    output
        << "preflight root_seed=" << report.recipe.root_seed
        << " fixtures=" << report.evidence.fixtures.size()
        << " worlds=" << report.recipe.worlds
        << " horizon=" << report.recipe.horizon_turns
        << " inner_worlds=" << report.recipe.inner_worlds
        << " result="
        << (report.gate_passed() ? "PASS" : "FAIL")
        << '\n';
    for (const auto& fixture :
         report.evidence.fixtures) {
        output
            << "preflight_fixture id="
            << fixture.spec.stable_id
            << " seed=" << fixture.seed
            << " selected="
            << std::quoted(
                   fixture.score.selected_probe_key)
            << " direction=" << fixture.direction.passed
            << " required_margin="
            << fixture.direction.required_margin
            << " positive="
            << fixture.direction.positive_value
            << " negative="
            << fixture.direction.negative_value
            << " excluded_margin_0="
            << fixture.direction.excluded_margins[0]
            << " excluded_margin_1="
            << fixture.direction.excluded_margins[1]
            << " hidden_nonvacuous="
            << fixture.hidden_repartition_nonvacuous
            << " hidden_identity="
            << fixture.hidden_repartition_bit_identical
            << " reverse_identity="
            << fixture.reversed_action_bit_identical;
        print_accounting(fixture.score.accounting);
        output << '\n';
        for (const std::string& key :
             fixture.direction.exact_max_support) {
            output
                << "preflight_support id="
                << fixture.spec.stable_id
                << " key=" << std::quoted(key) << '\n';
        }
        print_actions(
            fixture.spec.stable_id,
            fixture.score);
    }
    const auto& actor = report.evidence.actor_local;
    output
        << "preflight_actor_local seed=" << actor.seed
        << " selected="
        << std::quoted(actor.score.selected_probe_key)
        << " hidden_nonvacuous="
        << actor.hidden_repartition_nonvacuous
        << " observation_identity="
        << actor.observation_bit_identical
        << " legal_identity="
        << actor.legal_actions_bit_identical
        << " score_identity="
        << actor.score_bit_identical
        << " depth_one="
        << actor.one_level_nesting_bounded;
    print_accounting(actor.score.accounting);
    output << '\n';
    print_actions("actor-local", actor.score);
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
}

} // namespace

void print_offline(
    std::ostream& output, const Corpus& corpus,
    const FitReport& fit_report,
    const OfflineReport& report) {
    output
        << "schema=old-school-action-q-aq4-g1-run-v1\n"
        << "mode=run roots=" << corpus.census.roots.size()
        << " fit_examples=" << fit_report.fit_examples
        << " fit_options=" << fit_report.fit_options
        << " fit_seed=" << fit_report.optimizer.seed
        << " parent=" << report.parent_fingerprint
        << " candidate=" << report.candidate_fingerprint
        << '\n';
    print_metrics(output, "FIT", "parent", report.parent_fit);
    print_metrics(
        output, "FIT", "candidate",
        report.candidate_fit);
    print_metrics(
        output, "CHECK", "parent",
        report.parent_check);
    print_metrics(
        output, "CHECK", "candidate",
        report.candidate_check);
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& parent =
            report.parent_check.decks[deck];
        const auto& candidate =
            report.candidate_check.decks[deck];
        output
            << "check_deck deck="
            << deck_name(static_cast<DeckId>(deck))
            << " parent_agreement="
            << parent.top_one_expected_agreement
            << " candidate_agreement="
            << candidate.top_one_expected_agreement
            << " parent_regret=" << parent.mean_regret
            << " candidate_regret="
            << candidate.mean_regret
            << " regret_delta="
            << candidate.mean_regret -
                   parent.mean_regret
            << " guard="
            << report.check_deck_regret_guard[deck]
            << '\n';
    }
    output
        << "offline isolation="
        << (report.parent_immutable &&
            report.repeated_fit_bit_identical &&
            report.only_priority_component_changed)
        << " fit_regret="
        << report.fit_regret_strictly_improved
        << " check_regret="
        << report.check_regret_strictly_improved
        << " descriptor_order="
        << report.descriptor_order_identity
        << " redundant_counter="
        << report.redundant_counter_pass
        << " braingeyser_x0="
        << report.braingeyser_x_zero_excluded
        << " sick_growth="
        << report.sick_bear_growth_pass
        << " force_spike=" << report.live_force_spike
        << " frozen_dev_descriptive_labels="
        << report.frozen_dev.labels
        << " ancestral_self_score="
        << report.ancestral.self_score
        << " ancestral_opponent_score="
        << report.ancestral.opponent_score
        << " ancestral_self_above="
        << report.ancestral.self_strictly_above_opponent
        << " ancestral_opponent_absent="
        << report.ancestral.opponent_absent_from_support
        << " result="
        << (report.gate_passed() ? "PASS" : "FAIL")
        << '\n';
}

void print_selector(
    std::ostream& output,
    const BotBenchmarkSummary& summary,
    SelectorDisposition disposition) {
    const char* name =
        disposition == SelectorDisposition::FastGo
            ? "FAST_GO"
            : disposition ==
                      SelectorDisposition::ManualOnly
                  ? "MANUAL_ONLY"
                  : "REJECT";
    output
        << "selector seed=" << summary.evaluation_seed
        << " games=" << summary.total_games
        << " wins=" << summary.challenger_stats.wins
        << " losses=" << summary.challenger_stats.losses
        << " draws=" << summary.challenger_stats.draws
        << " disposition=" << name << '\n';
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& row =
            summary.challenger_decks[deck];
        output
            << "selector_deck deck="
            << deck_name(static_cast<DeckId>(deck))
            << " record=" << row.wins << '-'
            << row.losses << '-' << row.draws << '\n';
    }
}

namespace testing {

Census make_census(
    std::string parent_fingerprint,
    std::vector<ManifestRoot> roots,
    std::array<DeckCensus, kDeckCount> decks,
    std::size_t games) {
    Census census{
        .root_seed = kCollectionRootSeed,
        .parent_fingerprint =
            std::move(parent_fingerprint),
        .games = games,
        .decks = decks,
        .roots = std::move(roots),
    };
    census.manifest_hash =
        canonical_manifest_hash(census);
    return census;
}

} // namespace testing

} // namespace old_school::action_q_nested_actor_distill
