#include "old_school/action_q_nested_actor_diagnostic.hpp"

#include "old_school/learned_iteration.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::action_q_nested_actor_diagnostic {
namespace {

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool vector_bits_identical(
    std::span<const double> first,
    std::span<const double> second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!same_bits(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool direction_bit_identical(
    const DirectionSummary& first,
    const DirectionSummary& second) {
    return first.passed == second.passed &&
           same_bits(
               first.required_margin,
               second.required_margin) &&
           same_bits(
               first.positive_value,
               second.positive_value) &&
           same_bits(
               first.negative_value,
               second.negative_value) &&
           same_bits(
               first.excluded_margins[0],
               second.excluded_margins[0]) &&
           same_bits(
               first.excluded_margins[1],
               second.excluded_margins[1]) &&
           first.exact_max_support ==
               second.exact_max_support;
}

void require_no_probe_errors(
    std::span<const std::string> errors,
    std::string_view corpus_name) {
    if (!errors.empty()) {
        throw std::runtime_error(
            "AQ4-D1 " + std::string(corpus_name) +
            " fixture validation failed: " +
            errors.front());
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
            "AQ4-D1 fixture is missing or duplicated: " +
            std::string(stable_id));
    }
    return *found;
}

std::vector<probes::DecisionProbe> load_fixtures() {
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

    const auto manifest = fixture_manifest();
    return {
        unique_probe(counter, manifest[0].stable_id),
        unique_probe(braingeyser, manifest[1].stable_id),
        unique_probe(field, manifest[2].stable_id),
        unique_probe(spike, manifest[3].stable_id),
    };
}

bool sorcery_actions_for(
    const probes::DecisionProbe& probe) {
    return probe.phase == TurnPhase::FirstMain ||
           probe.phase == TurnPhase::SecondMain;
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
                "AQ4-D1 probe action mapping is ambiguous");
        }
        found = &candidate;
    }
    if (found == nullptr) {
        throw std::runtime_error(
            "AQ4-D1 authoritative action has no probe key");
    }
    return *found;
}

const PriorityAction& action_for_key(
    const probes::DecisionProbe& probe,
    std::string_view key) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [key](const probes::Candidate& candidate) {
            return candidate.descriptor == key;
        });
    if (found == probe.candidates.end() ||
        !std::holds_alternative<PriorityAction>(
            found->action)) {
        throw std::runtime_error(
            "AQ4-D1 required Priority action key is absent");
    }
    return std::get<PriorityAction>(found->action);
}

std::vector<PriorityAction> authoritative_actions(
    const probes::DecisionProbe& probe) {
    if (probe.decision_kind !=
            probes::DecisionKind::Priority ||
        probe.root_player >= 2) {
        throw std::runtime_error(
            "AQ4-D1 fixture is not a Priority decision");
    }
    const std::vector<PriorityAction> actions =
        legal_priority_actions(
            probe.state, probe.root_player,
            sorcery_actions_for(probe));
    if (actions.size() != probe.candidates.size()) {
        throw std::runtime_error(
            "AQ4-D1 probe omits an authoritative legal action");
    }
    for (const PriorityAction& action : actions) {
        static_cast<void>(
            candidate_for_action(probe, action));
    }
    return actions;
}

GameState hidden_repartition_clone(
    const GameState& state, std::size_t observer,
    bool reverse_observer_library) {
    if (observer >= state.players.size()) {
        throw std::invalid_argument(
            "AQ4-D1 hidden witness observer is invalid");
    }
    GameState clone = state;
    if (reverse_observer_library) {
        std::reverse(
            clone.players[observer].library.begin(),
            clone.players[observer].library.end());
    }

    const std::size_t opponent = 1 - observer;
    const PlayerState& original_hidden =
        state.players[opponent];
    PlayerState& hidden = clone.players[opponent];
    bool crossed_zone_boundary = false;
    for (std::size_t hand_index = 0;
         hand_index < hidden.hand.size() &&
         !crossed_zone_boundary;
         ++hand_index) {
        for (std::size_t library_index = 0;
             library_index < hidden.library.size();
             ++library_index) {
            if (hidden.hand[hand_index] ==
                hidden.library[library_index]) {
                continue;
            }
            std::swap(
                hidden.hand[hand_index],
                hidden.library[library_index]);
            crossed_zone_boundary = true;
            break;
        }
    }

    std::vector<CardId> original_cards =
        original_hidden.hand;
    original_cards.insert(
        original_cards.end(),
        original_hidden.library.begin(),
        original_hidden.library.end());
    std::vector<CardId> cloned_cards = hidden.hand;
    cloned_cards.insert(
        cloned_cards.end(),
        hidden.library.begin(), hidden.library.end());
    std::sort(
        original_cards.begin(), original_cards.end());
    std::sort(
        cloned_cards.begin(), cloned_cards.end());
    std::vector<CardId> original_hand =
        original_hidden.hand;
    std::vector<CardId> cloned_hand = hidden.hand;
    std::vector<CardId> original_library =
        original_hidden.library;
    std::vector<CardId> cloned_library =
        hidden.library;
    std::sort(original_hand.begin(), original_hand.end());
    std::sort(cloned_hand.begin(), cloned_hand.end());
    std::sort(
        original_library.begin(), original_library.end());
    std::sort(
        cloned_library.begin(), cloned_library.end());
    if (!crossed_zone_boundary ||
        hidden.hand.size() !=
            original_hidden.hand.size() ||
        hidden.library.size() !=
            original_hidden.library.size() ||
        cloned_hand == original_hand ||
        cloned_library == original_library ||
        cloned_cards != original_cards ||
        observe_game_state(state, observer) !=
            observe_game_state(clone, observer)) {
        throw std::runtime_error(
            "AQ4-D1 hidden repartition witness is vacuous "
            "or observation-changing");
    }
    return clone;
}

struct PreparedFixture {
    probes::DecisionProbe probe;
    std::vector<PriorityAction> actions;
    probes::DecisionProbe hidden_probe;
    std::vector<PriorityAction> hidden_actions;
};

struct PreparedActorLocal {
    GameState direct_state;
    GameState hidden_state;
    std::array<std::vector<CardId>, 2> original_decks;
    std::size_t player = 0;
    bool sorcery_actions = false;
    TurnPhase phase = TurnPhase::FirstMain;
    int consecutive_passes = 0;
    std::vector<PriorityAction> actions;
    bool observation_identical = false;
    bool legal_actions_identical = false;
};

std::vector<PreparedFixture> prepare_fixtures() {
    const auto manifest = fixture_manifest();
    const auto fixtures = load_fixtures();
    if (fixtures.size() != manifest.size()) {
        throw std::logic_error(
            "AQ4-D1 fixture loader changed size");
    }

    std::vector<PreparedFixture> prepared;
    prepared.reserve(fixtures.size());
    for (std::size_t index = 0;
         index < fixtures.size(); ++index) {
        probes::DecisionProbe probe = fixtures[index];
        if (probe.stable_id !=
            manifest[index].stable_id) {
            throw std::logic_error(
                "AQ4-D1 fixture order drifted");
        }
        std::vector<PriorityAction> actions =
            authoritative_actions(probe);
        probes::DecisionProbe hidden_probe = probe;
        hidden_probe.state =
            hidden_repartition_clone(
                probe.state, probe.root_player, true);
        std::vector<PriorityAction> hidden_actions =
            authoritative_actions(hidden_probe);
        if (hidden_probe.state == probe.state ||
            hidden_actions != actions) {
            throw std::logic_error(
                "AQ4-D1 root hidden witness changed legal actions");
        }
        prepared.push_back({
            .probe = std::move(probe),
            .actions = std::move(actions),
            .hidden_probe = std::move(hidden_probe),
            .hidden_actions =
                std::move(hidden_actions),
        });
    }
    return prepared;
}

PreparedActorLocal prepare_actor_local() {
    const auto controls =
        probes::make_braingeyser_x_zero_control_v1();
    require_no_probe_errors(
        probes::validate_braingeyser_x_zero_control_v1(
            controls),
        probes::kBraingeyserXZeroControlV1);
    const probes::DecisionProbe probe =
        unique_probe(
            controls,
            "control.blue.braingeyser-x0.v1");
    const PriorityAction& x_zero =
        action_for_key(
            probe, "braingeyser-x0-opponent");
    GameState response = probe.state;
    const auto move_from_library_to_hand =
        [&response](
            std::size_t player, CardId card) {
            auto& library =
                response.players[player].library;
            const auto found =
                std::find(
                    library.begin(), library.end(), card);
            if (found == library.end()) {
                throw std::logic_error(
                    "AQ4-D1 actor-local conserved card is "
                    "missing");
            }
            response.players[player].hand.push_back(card);
            library.erase(found);
        };
    move_from_library_to_hand(
        probe.root_player, CardId::FlyingMen);
    const std::size_t responder = 1 - probe.root_player;
    move_from_library_to_hand(
        responder, CardId::LightningBolt);
    if (!apply_priority_action(
            response, probe.root_player, x_zero,
            sorcery_actions_for(probe))) {
        throw std::logic_error(
            "AQ4-D1 could not construct Red X=0 response");
    }
    PriorityState priority{
        .player = probe.root_player,
        .consecutive_passes = 0,
    };
    if (pass_priority(response, priority) !=
            PriorityPassResult::Passed ||
        priority.player != responder ||
        priority.consecutive_passes != 1) {
        throw std::logic_error(
            "AQ4-D1 could not advance priority to Red");
    }
    const auto actions =
        legal_priority_actions(
            response, responder,
            sorcery_actions_for(probe));
    const bool has_pass =
        std::find(
            actions.begin(), actions.end(),
            PriorityAction::pass()) != actions.end();
    const bool has_bolt =
        std::any_of(
            actions.begin(), actions.end(),
            [](const PriorityAction& action) {
                return action.kind ==
                    PriorityActionKind::
                        CastLightningBolt;
            });
    if (actions.size() < 2 ||
        !has_pass || !has_bolt ||
        response.stack.empty() ||
        response.stack.back().card != CardId::Braingeyser ||
        response.stack.back().x_value != 0 ||
        response.players[probe.root_player].hand.empty() ||
        std::find(
            response.players[responder].hand.begin(),
            response.players[responder].hand.end(),
            CardId::LightningBolt) ==
            response.players[responder].hand.end()) {
        throw std::logic_error(
            "AQ4-D1 actor-local response is not nontrivial");
    }
    GameState hidden =
        hidden_repartition_clone(
            response, responder, false);
    const auto hidden_actions =
        legal_priority_actions(
            hidden, responder,
            sorcery_actions_for(probe));
    const bool observation_identical =
        observe_game_state(response, responder) ==
        observe_game_state(hidden, responder);
    const bool actions_identical =
        actions == hidden_actions;
    if (response == hidden ||
        !observation_identical ||
        !actions_identical) {
        throw std::logic_error(
            "AQ4-D1 actor-local witness is invalid");
    }
    return {
        .direct_state = std::move(response),
        .hidden_state = std::move(hidden),
        .original_decks = probe.original_decks,
        .player = responder,
        .sorcery_actions = sorcery_actions_for(probe),
        .phase = probe.phase,
        .consecutive_passes =
            priority.consecutive_passes,
        .actions = actions,
        .observation_identical =
            observation_identical,
        .legal_actions_identical =
            actions_identical,
    };
}

std::size_t checked_sum(
    std::span<const std::size_t> values) {
    std::size_t total = 0;
    for (const std::size_t value : values) {
        if (total >
            std::numeric_limits<std::size_t>::max() -
                value) {
            throw std::overflow_error(
                "AQ4-D1 accounting sum overflow");
        }
        total += value;
    }
    return total;
}

RootScore score_root(
    const GameState& state,
    const std::array<std::vector<CardId>, 2>& original_decks,
    std::size_t player, bool sorcery_actions, TurnPhase phase,
    int consecutive_passes,
    const std::vector<PriorityAction>& actions,
    const probes::DecisionProbe* probe,
    const std::shared_ptr<const LearnedModel>& parent,
    LearnedSearchConfig config,
    bool require_nested_search) {
    const LearnedActionSamples scored =
        learned_priority_action_samples(
            state, original_decks, player,
            sorcery_actions, phase, consecutive_passes,
            actions, parent, config);
    const std::size_t samples_per_action =
        config.worlds * config.rollouts_per_world;
    if (scored.q_samples.size() != actions.size() ||
        scored.priority_continuation_samples.size() !=
            actions.size() ||
        scored.priority_shallow_prior_samples.size() !=
            actions.size() ||
        scored.exact_priority_aggregate_scores.size() !=
            actions.size() ||
        !scored.priority_h0_boundaries.empty() ||
        (require_nested_search &&
         scored.priority_inner_rollout_evaluations.size() !=
             actions.size()) ||
        (require_nested_search &&
         scored.priority_inner_search_invocations.size() !=
             actions.size()) ||
        (require_nested_search &&
         scored.priority_inner_search_max_depth.size() !=
             actions.size()) ||
        (!require_nested_search &&
         (!scored.priority_inner_rollout_evaluations.empty() ||
          !scored.priority_inner_search_invocations.empty() ||
          !scored.priority_inner_search_max_depth.empty() ||
          scored.inner_rollout_evaluations != 0 ||
          scored.inner_search_invocations != 0 ||
          scored.inner_search_max_depth != 0))) {
        throw std::logic_error(
            "AQ4-D1 scorer returned incomplete action rows");
    }

    RootScore result;
    result.accounting = {
        .sampled_worlds = scored.sampled_worlds,
        .rollout_evaluations =
            scored.rollout_evaluations,
        .terminal_evaluations =
            scored.terminal_evaluations,
        .bootstrapped_evaluations =
            scored.bootstrapped_evaluations,
        .inner_rollout_evaluations =
            scored.inner_rollout_evaluations,
        .inner_search_invocations =
            scored.inner_search_invocations,
        .inner_search_max_depth =
            scored.inner_search_max_depth,
    };
    std::size_t inner_cross_sum = 0;
    std::size_t invocation_cross_sum = 0;
    std::size_t maximum_depth = 0;
    result.actions.reserve(actions.size());
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        const auto& samples = scored.q_samples[index];
        if (samples.size() != samples_per_action ||
            scored.priority_continuation_samples[index]
                    .size() != samples_per_action ||
            scored.priority_shallow_prior_samples[index]
                    .size() != samples_per_action) {
            throw std::logic_error(
                "AQ4-D1 scorer row has an invalid sample count");
        }
        for (const double sample : samples) {
            if (!probability(sample)) {
                throw std::logic_error(
                    "AQ4-D1 continuation is not a probability");
            }
        }
        if (!config.blend_shallow_prior) {
            if (!vector_bits_identical(
                    scored.priority_continuation_samples[
                        index],
                    samples) ||
                !std::all_of(
                    scored.priority_shallow_prior_samples[index]
                        .begin(),
                    scored.priority_shallow_prior_samples[index]
                        .end(),
                    [](double value) {
                        return same_bits(value, 0.0);
                    })) {
                throw std::logic_error(
                    "AQ4-D1 unblended continuation rows drifted");
            }
            double mean = 0.0;
            for (const double sample : samples) {
                mean += sample;
            }
            mean /= static_cast<double>(samples.size());
            if (!same_bits(
                    mean,
                    scored.exact_priority_aggregate_scores[
                        index])) {
                throw std::logic_error(
                    "AQ4-D1 mean drifted from engine aggregate");
            }
        }
        std::vector<std::size_t> inner_counts;
        std::vector<std::size_t> invocation_counts;
        std::vector<std::size_t> maximum_depths;
        if (require_nested_search) {
            inner_counts =
                scored.priority_inner_rollout_evaluations[
                    index];
            invocation_counts =
                scored.priority_inner_search_invocations[
                    index];
            maximum_depths =
                scored.priority_inner_search_max_depth[
                    index];
            if (inner_counts.size() != samples_per_action ||
                invocation_counts.size() !=
                    samples_per_action ||
                maximum_depths.size() !=
                    samples_per_action ||
                std::any_of(
                    inner_counts.begin(), inner_counts.end(),
                    [](std::size_t count) {
                        return count % kInnerWorlds != 0;
                    })) {
                throw std::logic_error(
                    "AQ4-D1 inner rollout cells are invalid");
            }
            const std::size_t row_sum =
                checked_sum(inner_counts);
            if (inner_cross_sum >
                std::numeric_limits<std::size_t>::max() -
                    row_sum) {
                throw std::overflow_error(
                    "AQ4-D1 inner rollout cross-sum overflow");
            }
            inner_cross_sum += row_sum;
            const std::size_t invocation_row_sum =
                checked_sum(invocation_counts);
            if (invocation_cross_sum >
                std::numeric_limits<std::size_t>::max() -
                    invocation_row_sum) {
                throw std::overflow_error(
                    "AQ4-D1 invocation cross-sum overflow");
            }
            invocation_cross_sum += invocation_row_sum;
            for (std::size_t sample_index = 0;
                 sample_index < samples_per_action;
                 ++sample_index) {
                maximum_depth =
                    std::max(
                        maximum_depth,
                        maximum_depths[sample_index]);
                if (maximum_depths[sample_index] > 1 ||
                    (invocation_counts[sample_index] != 0 &&
                     maximum_depths[sample_index] != 1)) {
                    throw std::logic_error(
                        "AQ4-D1 per-cell nesting evidence "
                        "is invalid");
                }
            }
        }
        const std::string typed =
            probes::stable_priority_action_descriptor(
                actions[index]);
        result.actions.push_back({
            .probe_key =
                probe == nullptr
                    ? typed
                    : candidate_for_action(
                          *probe, actions[index])
                          .descriptor,
            .typed_descriptor = typed,
            .action = actions[index],
            .samples = samples,
            .inner_rollout_evaluations =
                std::move(inner_counts),
            .inner_search_invocations =
                std::move(invocation_counts),
            .inner_search_max_depth =
                std::move(maximum_depths),
            .mean =
                scored.exact_priority_aggregate_scores[
                    index],
        });
    }
    if (require_nested_search &&
        (inner_cross_sum !=
             result.accounting.inner_rollout_evaluations ||
         invocation_cross_sum !=
             result.accounting.inner_search_invocations ||
         maximum_depth !=
             result.accounting.inner_search_max_depth ||
         inner_cross_sum == 0 ||
         invocation_cross_sum == 0 ||
         maximum_depth != 1)) {
        throw std::logic_error(
            "AQ4-D1 inner rollout accounting cross-sum failed");
    }
    std::sort(
        result.actions.begin(), result.actions.end(),
        [](const ActionScore& first,
           const ActionScore& second) {
            return first.typed_descriptor <
                   second.typed_descriptor;
        });
    if (result.actions.empty() ||
        std::adjacent_find(
            result.actions.begin(), result.actions.end(),
            [](const ActionScore& first,
               const ActionScore& second) {
                return first.typed_descriptor ==
                       second.typed_descriptor;
            }) != result.actions.end()) {
        throw std::logic_error(
            "AQ4-D1 typed action descriptors are not unique");
    }
    double best =
        -std::numeric_limits<double>::infinity();
    for (const ActionScore& action : result.actions) {
        if (!probability(action.mean)) {
            throw std::logic_error(
                "AQ4-D1 aggregate is not a probability");
        }
        best = std::max(best, action.mean);
    }
    for (ActionScore& action : result.actions) {
        action.exact_max = action.mean == best;
        if (result.selected_probe_key.empty() &&
            action.exact_max) {
            result.selected_probe_key =
                action.probe_key;
        }
    }
    return result;
}

bool action_score_bit_identical(
    const ActionScore& first,
    const ActionScore& second) {
    return first.probe_key == second.probe_key &&
           first.typed_descriptor ==
               second.typed_descriptor &&
           first.action == second.action &&
           vector_bits_identical(
               first.samples, second.samples) &&
           first.inner_rollout_evaluations ==
               second.inner_rollout_evaluations &&
           first.inner_search_invocations ==
               second.inner_search_invocations &&
           first.inner_search_max_depth ==
               second.inner_search_max_depth &&
           same_bits(first.mean, second.mean) &&
           first.exact_max == second.exact_max;
}

std::string support_string(
    std::span<const std::string> support) {
    std::string result;
    for (std::size_t index = 0;
         index < support.size(); ++index) {
        if (index != 0) {
            result += ',';
        }
        result += support[index];
    }
    return result;
}

} // namespace

std::optional<Command> parse_command(
    std::span<const std::string_view> arguments) {
    if (arguments.size() == 1 &&
        arguments.front() == "--diagnose") {
        return Command::Diagnose;
    }
    return std::nullopt;
}

void print_usage(std::ostream& output) {
    output
        << "Usage: old-school-action-q-nested-actor-diagnostic "
           "--diagnose\n";
}

std::uint64_t fixture_seed(std::size_t fixture_index) {
    if (fixture_index >= kFixtureCount) {
        throw std::out_of_range(
            "AQ4-D1 fixture index is outside the manifest");
    }
    return learned_iteration::derive_seed(
        kRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        fixture_index, 0, 0);
}

std::uint64_t actor_local_seed() {
    return learned_iteration::derive_seed(
        kRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        kFixtureCount, 0, 0);
}

std::array<FixtureSpec, kFixtureCount>
fixture_manifest() {
    const std::array<FixtureSpec, kFixtureCount> manifest{{
        {
            .fixture_index = 0,
            .stable_id =
                "control.blue.counter-redundant-same-target.v1",
            .kind = DirectionKind::StrictPair,
            .positive_key = "pass",
            .negative_key =
                "counter-same-air-elemental",
            .secondary_negative_key =
                "counter-own-counterspell",
            .expected_seed =
                3875276633833541024ULL,
        },
        {
            .fixture_index = 1,
            .stable_id =
                "control.blue.braingeyser-x0.v1",
            .kind = DirectionKind::ExcludeXZero,
            .excluded_keys = {
                "braingeyser-x0-self",
                "braingeyser-x0-opponent",
            },
            .expected_seed =
                10554634509341308714ULL,
        },
        {
            .fixture_index = 2,
            .stable_id =
                "field.green.second-main-sick-bear-growth.v1",
            .kind = DirectionKind::StrictPair,
            .positive_key = "pass",
            .negative_key =
                "growth-own-summoning-sick-grizzly-bears",
            .expected_seed =
                15818607149009889277ULL,
        },
        {
            .fixture_index = 3,
            .stable_id =
                "control.blue.force-spike-live-gray-ogre.v1",
            .kind = DirectionKind::StrictPair,
            .positive_key = "force-spike-gray-ogre",
            .negative_key = "pass",
            .expected_seed =
                14402092525871157609ULL,
        },
    }};
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        if (manifest[index].fixture_index != index ||
            manifest[index].stable_id.empty() ||
            manifest[index].expected_seed !=
                fixture_seed(index)) {
            throw std::logic_error(
                "AQ4-D1 frozen manifest seed drifted");
        }
    }
    if (actor_local_seed() != kActorLocalSeed) {
        throw std::logic_error(
            "AQ4-D1 actor-local seed drifted");
    }
    return manifest;
}

LearnedSearchConfig outer_search_config(
    std::uint64_t seed) {
    return {
        .seed = seed,
        .worlds = kWorlds,
        .rollouts_per_world = kRolloutsPerWorld,
        .horizon_turns = kHorizonTurns,
        .continuation_variant =
            LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .evaluation_threads = kEvaluationThreads,
        .capture_priority_h0_boundaries = false,
        .value_continuation_search_worlds =
            kInnerWorlds,
    };
}

LearnedSearchConfig actor_search_config(
    std::uint64_t seed) {
    return {
        .seed = seed,
        .worlds = kInnerWorlds,
        .rollouts_per_world =
            kLearnedValueSearchRolloutsPerWorld,
        .horizon_turns =
            kLearnedValueSearchHorizonTurns,
        .continuation_variant =
            LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior =
            kLearnedValueSearchBlendsShallowPrior,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .evaluation_threads = 1,
        .capture_priority_h0_boundaries = false,
        .value_continuation_search_worlds = 0,
    };
}

std::uint64_t preflight_fixture_seed(
    const PreflightRecipe& recipe,
    std::size_t fixture_index) {
    if (fixture_index >= kFixtureCount ||
        recipe.root_seed == 0) {
        throw std::out_of_range(
            "AQ4 preflight fixture coordinate is invalid");
    }
    return learned_iteration::derive_seed(
        recipe.root_seed,
        learned_iteration::SeedDomain::PrioritySearch,
        1, fixture_index, 0);
}

std::uint64_t preflight_actor_local_seed(
    const PreflightRecipe& recipe) {
    if (recipe.root_seed == 0) {
        throw std::invalid_argument(
            "AQ4 preflight root seed is zero");
    }
    return learned_iteration::derive_seed(
        recipe.root_seed,
        learned_iteration::SeedDomain::PrioritySearch,
        1, kFixtureCount, 0);
}

LearnedSearchConfig preflight_outer_search_config(
    const PreflightRecipe& recipe,
    std::uint64_t seed) {
    if (recipe.root_seed == 0 ||
        recipe.worlds == 0 ||
        recipe.worlds > 4096 ||
        recipe.rollouts_per_world == 0 ||
        recipe.rollouts_per_world > 256 ||
        recipe.horizon_turns == 0 ||
        recipe.horizon_turns > 128 ||
        recipe.evaluation_threads == 0 ||
        recipe.evaluation_threads > 4096 ||
        recipe.inner_worlds == 0 ||
        recipe.inner_worlds > 4096) {
        throw std::invalid_argument(
            "AQ4 preflight recipe is invalid");
    }
    return {
        .seed = seed,
        .worlds = recipe.worlds,
        .rollouts_per_world =
            recipe.rollouts_per_world,
        .horizon_turns = recipe.horizon_turns,
        .continuation_variant =
            LearnedVariant::ValueSearchChampion,
        .value_continuation_epsilon = 0.0,
        .blend_shallow_prior = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .evaluation_threads =
            recipe.evaluation_threads,
        .capture_priority_h0_boundaries = false,
        .value_continuation_search_worlds =
            recipe.inner_worlds,
    };
}

DirectionSummary evaluate_direction(
    const FixtureSpec& spec,
    std::span<const ActionScore> actions) {
    if (actions.empty()) {
        throw std::invalid_argument(
            "AQ4-D1 direction requires legal actions");
    }
    double best =
        -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        if (actions[index].probe_key.empty() ||
            actions[index].typed_descriptor.empty() ||
            !probability(actions[index].mean) ||
            std::find_if(
                actions.begin(), actions.begin() + index,
                [&](const ActionScore& earlier) {
                    return earlier.probe_key ==
                               actions[index].probe_key ||
                           earlier.action ==
                               actions[index].action;
                }) != actions.begin() + index) {
            throw std::invalid_argument(
                "AQ4-D1 action rows are invalid or duplicated");
        }
        best = std::max(best, actions[index].mean);
    }
    const auto value_for =
        [&actions](std::string_view key) {
            const auto found = std::find_if(
                actions.begin(), actions.end(),
                [key](const ActionScore& action) {
                    return action.probe_key == key;
                });
            if (found == actions.end()) {
                throw std::invalid_argument(
                    "AQ4-D1 required probe key is absent");
            }
            return found->mean;
        };

    DirectionSummary result;
    for (const ActionScore& action : actions) {
        if (action.mean == best) {
            result.exact_max_support.push_back(
                action.probe_key);
        }
    }
    if (spec.kind == DirectionKind::StrictPair) {
        if (spec.positive_key.empty() ||
            spec.negative_key.empty() ||
            spec.positive_key == spec.negative_key) {
            throw std::invalid_argument(
                "AQ4-D1 strict-pair manifest is invalid");
        }
        result.positive_value =
            value_for(spec.positive_key);
        result.negative_value =
            value_for(spec.negative_key);
        const double first_margin =
            result.positive_value -
            result.negative_value;
        result.excluded_margins[0] = first_margin;
        if (spec.secondary_negative_key.empty()) {
            result.required_margin = first_margin;
            result.passed = first_margin > 0.0;
            return result;
        }
        if (spec.secondary_negative_key ==
                spec.positive_key ||
            spec.secondary_negative_key ==
                spec.negative_key) {
            throw std::invalid_argument(
                "AQ4-D1 secondary strict-pair key is invalid");
        }
        const double second_negative =
            value_for(spec.secondary_negative_key);
        const double second_margin =
            result.positive_value - second_negative;
        result.negative_value =
            std::max(
                result.negative_value, second_negative);
        result.excluded_margins[1] = second_margin;
        result.required_margin =
            std::min(first_margin, second_margin);
        result.passed =
            first_margin > 0.0 &&
            second_margin > 0.0;
        return result;
    }
    if (spec.kind != DirectionKind::ExcludeXZero ||
        spec.excluded_keys[0].empty() ||
        spec.excluded_keys[1].empty() ||
        spec.excluded_keys[0] ==
            spec.excluded_keys[1]) {
        throw std::invalid_argument(
            "AQ4-D1 X=0 manifest is invalid");
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

bool root_scores_bit_identical(
    const RootScore& first, const RootScore& second) {
    if (first.accounting != second.accounting ||
        first.selected_probe_key !=
            second.selected_probe_key ||
        first.actions.size() != second.actions.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.actions.size(); ++index) {
        if (!action_score_bit_identical(
                first.actions[index],
                second.actions[index])) {
            return false;
        }
    }
    return true;
}

void require_invariant_root_scores(
    const RootScore& direct,
    const RootScore& hidden_repartition,
    const RootScore& reversed_actions) {
    if (!root_scores_bit_identical(
            direct, hidden_repartition)) {
        throw std::runtime_error(
            "AQ4-D1 hidden-repartition identity failed");
    }
    if (!root_scores_bit_identical(
            direct, reversed_actions)) {
        throw std::runtime_error(
            "AQ4-D1 reversed-action identity failed");
    }
}

bool FixtureReport::gate_passed() const {
    const std::size_t expected_samples =
        kWorlds * kRolloutsPerWorld;
    if (spec.fixture_index >= kFixtureCount ||
        seed != spec.expected_seed ||
        seed != fixture_seed(spec.fixture_index) ||
        score.actions.empty() ||
        score.selected_probe_key.empty() ||
        score.accounting.sampled_worlds != kWorlds ||
        score.accounting.rollout_evaluations !=
            score.actions.size() * expected_samples ||
        score.accounting.terminal_evaluations +
                score.accounting.bootstrapped_evaluations !=
            score.accounting.rollout_evaluations ||
        score.accounting.inner_rollout_evaluations == 0 ||
        score.accounting.inner_search_invocations == 0 ||
        score.accounting.inner_search_max_depth != 1 ||
        !direction.passed ||
        !hidden_repartition_nonvacuous ||
        !hidden_repartition_bit_identical ||
        !reversed_action_bit_identical) {
        return false;
    }
    std::size_t inner_cross_sum = 0;
    std::size_t invocation_cross_sum = 0;
    std::size_t maximum_depth = 0;
    for (const ActionScore& action : score.actions) {
        if (action.samples.size() != expected_samples ||
            action.inner_rollout_evaluations.size() !=
                expected_samples ||
            action.inner_search_invocations.size() !=
                expected_samples ||
            action.inner_search_max_depth.size() !=
                expected_samples) {
            return false;
        }
        for (const double sample : action.samples) {
            if (!probability(sample)) {
                return false;
            }
        }
        for (std::size_t sample_index = 0;
             sample_index < expected_samples;
             ++sample_index) {
            const std::size_t count =
                action.inner_rollout_evaluations[
                    sample_index];
            const std::size_t invocations =
                action.inner_search_invocations[
                    sample_index];
            const std::size_t depth =
                action.inner_search_max_depth[
                    sample_index];
            if (count % kInnerWorlds != 0 ||
                inner_cross_sum >
                    std::numeric_limits<std::size_t>::max() -
                        count ||
                invocation_cross_sum >
                    std::numeric_limits<std::size_t>::max() -
                        invocations ||
                depth > 1 ||
                (invocations != 0 && depth != 1)) {
                return false;
            }
            inner_cross_sum += count;
            invocation_cross_sum += invocations;
            maximum_depth =
                std::max(maximum_depth, depth);
        }
    }
    if (inner_cross_sum !=
            score.accounting.inner_rollout_evaluations ||
        invocation_cross_sum !=
            score.accounting.inner_search_invocations ||
        maximum_depth !=
            score.accounting.inner_search_max_depth) {
        return false;
    }
    DirectionSummary observed;
    try {
        observed =
            evaluate_direction(spec, score.actions);
    } catch (const std::exception&) {
        return false;
    }
    return direction_bit_identical(
        observed, direction);
}

bool ActorLocalReport::gate_passed() const {
    const std::size_t expected_samples =
        kInnerWorlds *
        kLearnedValueSearchRolloutsPerWorld;
    return seed == kActorLocalSeed &&
           seed == actor_local_seed() &&
           !score.actions.empty() &&
           !score.selected_probe_key.empty() &&
           score.accounting.sampled_worlds ==
               kInnerWorlds &&
           score.accounting.rollout_evaluations ==
               score.actions.size() * expected_samples &&
           score.accounting.terminal_evaluations +
                   score.accounting.bootstrapped_evaluations ==
               score.accounting.rollout_evaluations &&
           score.accounting.inner_rollout_evaluations == 0 &&
           score.accounting.inner_search_invocations == 0 &&
           score.accounting.inner_search_max_depth == 0 &&
           std::all_of(
               score.actions.begin(), score.actions.end(),
               [](const ActionScore& action) {
                   return action.samples.size() ==
                              expected_samples &&
                          action
                              .inner_rollout_evaluations
                              .empty() &&
                          action
                              .inner_search_invocations
                              .empty() &&
                          action
                              .inner_search_max_depth
                              .empty() &&
                          probability(action.mean);
               }) &&
           hidden_repartition_nonvacuous &&
           observation_bit_identical &&
           legal_actions_bit_identical &&
           score_bit_identical &&
           one_level_nesting_bounded;
}

bool Report::gate_passed() const {
    if (parent_fingerprint !=
            kRequiredParentFingerprint ||
        fixtures.size() != kFixtureCount ||
        !actor_local.gate_passed()) {
        return false;
    }
    const auto manifest = fixture_manifest();
    bool all_directions = true;
    for (std::size_t index = 0;
         index < fixtures.size(); ++index) {
        if (fixtures[index].spec != manifest[index] ||
            direction_passed[index] !=
                fixtures[index].direction.passed ||
            !fixtures[index].gate_passed()) {
            return false;
        }
        all_directions =
            all_directions &&
            direction_passed[index];
    }
    return hypothesis_passed ==
               (all_directions &&
                actor_local.gate_passed()) &&
           hypothesis_passed;
}

bool PreflightReport::gate_passed() const {
    if (recipe.root_seed == 0 ||
        evidence.parent_fingerprint !=
            kRequiredParentFingerprint ||
        evidence.fixtures.size() != kFixtureCount) {
        return false;
    }
    const std::size_t samples_per_action =
        recipe.worlds * recipe.rollouts_per_world;
    const auto base_manifest = fixture_manifest();
    bool all_directions = true;
    for (std::size_t index = 0;
         index < evidence.fixtures.size(); ++index) {
        const FixtureReport& fixture =
            evidence.fixtures[index];
        FixtureSpec expected = base_manifest[index];
        expected.expected_seed =
            preflight_fixture_seed(recipe, index);
        if (fixture.spec != expected ||
            fixture.seed != expected.expected_seed ||
            fixture.score.actions.empty() ||
            fixture.score.selected_probe_key.empty() ||
            fixture.score.accounting.sampled_worlds !=
                recipe.worlds ||
            fixture.score.accounting.rollout_evaluations !=
                fixture.score.actions.size() *
                    samples_per_action ||
            fixture.score.accounting.terminal_evaluations +
                    fixture.score.accounting
                        .bootstrapped_evaluations !=
                fixture.score.accounting.rollout_evaluations ||
            fixture.score.accounting
                    .inner_rollout_evaluations == 0 ||
            fixture.score.accounting
                    .inner_search_invocations == 0 ||
            fixture.score.accounting
                    .inner_search_max_depth != 1 ||
            !fixture.direction.passed ||
            !fixture.hidden_repartition_nonvacuous ||
            !fixture.hidden_repartition_bit_identical ||
            !fixture.reversed_action_bit_identical) {
            return false;
        }
        std::size_t inner_rollouts = 0;
        std::size_t inner_invocations = 0;
        std::size_t maximum_depth = 0;
        for (const ActionScore& action :
             fixture.score.actions) {
            if (action.samples.size() !=
                    samples_per_action ||
                action.inner_rollout_evaluations.size() !=
                    samples_per_action ||
                action.inner_search_invocations.size() !=
                    samples_per_action ||
                action.inner_search_max_depth.size() !=
                    samples_per_action ||
                !probability(action.mean)) {
                return false;
            }
            for (std::size_t sample = 0;
                 sample < samples_per_action; ++sample) {
                const std::size_t rollouts =
                    action.inner_rollout_evaluations[sample];
                const std::size_t invocations =
                    action.inner_search_invocations[sample];
                const std::size_t depth =
                    action.inner_search_max_depth[sample];
                const bool inactive =
                    invocations == 0 &&
                    rollouts == 0 && depth == 0;
                const bool active =
                    invocations != 0 &&
                    rollouts != 0 &&
                    rollouts % recipe.inner_worlds == 0 &&
                    depth == 1;
                if (!probability(action.samples[sample]) ||
                    (!inactive && !active) ||
                    inner_rollouts >
                        std::numeric_limits<std::size_t>::max() -
                            rollouts ||
                    inner_invocations >
                        std::numeric_limits<std::size_t>::max() -
                            invocations) {
                    return false;
                }
                inner_rollouts += rollouts;
                inner_invocations += invocations;
                maximum_depth =
                    std::max(maximum_depth, depth);
            }
        }
        if (inner_rollouts !=
                fixture.score.accounting
                    .inner_rollout_evaluations ||
            inner_invocations !=
                fixture.score.accounting
                    .inner_search_invocations ||
            maximum_depth != 1) {
            return false;
        }
        DirectionSummary observed;
        try {
            observed = evaluate_direction(
                fixture.spec, fixture.score.actions);
        } catch (const std::exception&) {
            return false;
        }
        if (!direction_bit_identical(
                observed, fixture.direction) ||
            evidence.direction_passed[index] !=
                fixture.direction.passed) {
            return false;
        }
        all_directions =
            all_directions && fixture.direction.passed;
    }

    const ActorLocalReport& actor = evidence.actor_local;
    const std::size_t actor_samples =
        recipe.inner_worlds *
        kLearnedValueSearchRolloutsPerWorld;
    if (actor.seed !=
            preflight_actor_local_seed(recipe) ||
        actor.score.actions.empty() ||
        actor.score.selected_probe_key.empty() ||
        actor.score.accounting.sampled_worlds !=
            recipe.inner_worlds ||
        actor.score.accounting.rollout_evaluations !=
            actor.score.actions.size() * actor_samples ||
        actor.score.accounting.terminal_evaluations +
                actor.score.accounting
                    .bootstrapped_evaluations !=
            actor.score.accounting.rollout_evaluations ||
        actor.score.accounting.inner_rollout_evaluations != 0 ||
        actor.score.accounting.inner_search_invocations != 0 ||
        actor.score.accounting.inner_search_max_depth != 0 ||
        !actor.hidden_repartition_nonvacuous ||
        !actor.observation_bit_identical ||
        !actor.legal_actions_bit_identical ||
        !actor.score_bit_identical ||
        !actor.one_level_nesting_bounded) {
        return false;
    }
    for (const ActionScore& action : actor.score.actions) {
        if (action.samples.size() != actor_samples ||
            !action.inner_rollout_evaluations.empty() ||
            !action.inner_search_invocations.empty() ||
            !action.inner_search_max_depth.empty() ||
            !probability(action.mean) ||
            !std::all_of(
                action.samples.begin(), action.samples.end(),
                [](double value) {
                    return probability(value);
                })) {
            return false;
        }
    }
    return all_directions &&
           evidence.hypothesis_passed;
}

void validate_fixture_witnesses() {
    static_cast<void>(prepare_fixtures());
    static_cast<void>(prepare_actor_local());
}

PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent,
    const PreflightRecipe& recipe) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "AQ4 preflight requires exact frozen C16");
    }
    static_cast<void>(
        preflight_outer_search_config(recipe, recipe.root_seed));
    const auto prepared = prepare_fixtures();
    const PreparedActorLocal actor =
        prepare_actor_local();
    auto manifest = fixture_manifest();

    PreflightReport output;
    output.recipe = recipe;
    Report& report = output.evidence;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    bool all_directions = true;
    for (std::size_t index = 0;
         index < prepared.size(); ++index) {
        const PreparedFixture& input =
            prepared[index];
        const std::uint64_t seed =
            preflight_fixture_seed(recipe, index);
        manifest[index].expected_seed = seed;
        const LearnedSearchConfig search =
            preflight_outer_search_config(recipe, seed);
        RootScore direct =
            score_root(
                input.probe.state,
                input.probe.original_decks,
                input.probe.root_player,
                sorcery_actions_for(input.probe),
                input.probe.phase,
                input.probe.consecutive_passes,
                input.actions, &input.probe, parent,
                search, true);
        auto reversed_actions = input.actions;
        std::reverse(
            reversed_actions.begin(),
            reversed_actions.end());
        const RootScore reversed =
            score_root(
                input.probe.state,
                input.probe.original_decks,
                input.probe.root_player,
                sorcery_actions_for(input.probe),
                input.probe.phase,
                input.probe.consecutive_passes,
                reversed_actions, &input.probe, parent,
                search, true);
        const RootScore hidden =
            score_root(
                input.hidden_probe.state,
                input.hidden_probe.original_decks,
                input.hidden_probe.root_player,
                sorcery_actions_for(input.hidden_probe),
                input.hidden_probe.phase,
                input.hidden_probe.consecutive_passes,
                input.hidden_actions,
                &input.hidden_probe, parent,
                search, true);
        require_invariant_root_scores(
            direct, hidden, reversed);

        FixtureReport fixture;
        fixture.spec = manifest[index];
        fixture.seed = seed;
        fixture.direction =
            evaluate_direction(
                fixture.spec, direct.actions);
        fixture.hidden_repartition_nonvacuous =
            input.hidden_probe.state !=
            input.probe.state;
        fixture.hidden_repartition_bit_identical = true;
        fixture.reversed_action_bit_identical = true;
        fixture.score = std::move(direct);
        report.direction_passed[index] =
            fixture.direction.passed;
        all_directions =
            all_directions &&
            fixture.direction.passed;
        report.fixtures.push_back(
            std::move(fixture));
    }

    LearnedSearchConfig actor_search =
        actor_search_config(
            preflight_actor_local_seed(recipe));
    actor_search.worlds = recipe.inner_worlds;
    const RootScore actor_direct =
        score_root(
            actor.direct_state, actor.original_decks,
            actor.player, actor.sorcery_actions,
            actor.phase, actor.consecutive_passes,
            actor.actions, nullptr, parent,
            actor_search, false);
    const RootScore actor_hidden =
        score_root(
            actor.hidden_state, actor.original_decks,
            actor.player, actor.sorcery_actions,
            actor.phase, actor.consecutive_passes,
            actor.actions, nullptr, parent,
            actor_search, false);
    if (!root_scores_bit_identical(
            actor_direct, actor_hidden)) {
        throw std::runtime_error(
            "AQ4 preflight actor-local response leaked "
            "private state");
    }
    report.actor_local = {
        .seed = preflight_actor_local_seed(recipe),
        .score = actor_direct,
        .hidden_repartition_nonvacuous =
            actor.direct_state != actor.hidden_state,
        .observation_bit_identical =
            actor.observation_identical,
        .legal_actions_bit_identical =
            actor.legal_actions_identical,
        .score_bit_identical = true,
        .one_level_nesting_bounded =
            actor_direct.accounting
                .inner_rollout_evaluations == 0,
    };
    report.hypothesis_passed =
        all_directions &&
        report.actor_local.hidden_repartition_nonvacuous &&
        report.actor_local.observation_bit_identical &&
        report.actor_local.legal_actions_bit_identical &&
        report.actor_local.score_bit_identical &&
        report.actor_local.one_level_nesting_bounded;
    return output;
}

Report diagnose(
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "AQ4-D1 requires exact frozen C16");
    }
    const auto manifest = fixture_manifest();
    // Validate every physical witness before the first sampler call can
    // consume the reserved AQ4-D1 coordinate.
    const auto prepared = prepare_fixtures();
    const PreparedActorLocal actor =
        prepare_actor_local();

    Report report;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    bool all_directions = true;
    for (std::size_t index = 0;
         index < prepared.size(); ++index) {
        const PreparedFixture& input =
            prepared[index];
        const std::uint64_t seed =
            fixture_seed(index);
        RootScore direct =
            score_root(
                input.probe.state,
                input.probe.original_decks,
                input.probe.root_player,
                sorcery_actions_for(input.probe),
                input.probe.phase,
                input.probe.consecutive_passes,
                input.actions, &input.probe, parent,
                outer_search_config(seed), true);
        auto reversed_actions = input.actions;
        std::reverse(
            reversed_actions.begin(),
            reversed_actions.end());
        const RootScore reversed =
            score_root(
                input.probe.state,
                input.probe.original_decks,
                input.probe.root_player,
                sorcery_actions_for(input.probe),
                input.probe.phase,
                input.probe.consecutive_passes,
                reversed_actions, &input.probe, parent,
                outer_search_config(seed), true);
        const RootScore hidden =
            score_root(
                input.hidden_probe.state,
                input.hidden_probe.original_decks,
                input.hidden_probe.root_player,
                sorcery_actions_for(input.hidden_probe),
                input.hidden_probe.phase,
                input.hidden_probe.consecutive_passes,
                input.hidden_actions,
                &input.hidden_probe, parent,
                outer_search_config(seed), true);
        require_invariant_root_scores(
            direct, hidden, reversed);

        FixtureReport fixture;
        fixture.spec = manifest[index];
        fixture.seed = seed;
        fixture.direction =
            evaluate_direction(
                fixture.spec, direct.actions);
        fixture.hidden_repartition_nonvacuous =
            input.hidden_probe.state !=
            input.probe.state;
        fixture.hidden_repartition_bit_identical = true;
        fixture.reversed_action_bit_identical = true;
        fixture.score = std::move(direct);
        report.direction_passed[index] =
            fixture.direction.passed;
        all_directions =
            all_directions &&
            fixture.direction.passed;
        report.fixtures.push_back(
            std::move(fixture));
    }

    const RootScore actor_direct =
        score_root(
            actor.direct_state, actor.original_decks,
            actor.player, actor.sorcery_actions,
            actor.phase, actor.consecutive_passes,
            actor.actions, nullptr, parent,
            actor_search_config(actor_local_seed()),
            false);
    const RootScore actor_hidden =
        score_root(
            actor.hidden_state, actor.original_decks,
            actor.player, actor.sorcery_actions,
            actor.phase, actor.consecutive_passes,
            actor.actions, nullptr, parent,
            actor_search_config(actor_local_seed()),
            false);
    if (!root_scores_bit_identical(
            actor_direct, actor_hidden)) {
        throw std::runtime_error(
            "AQ4-D1 actor-local Red response leaked Blue "
            "private state");
    }
    report.actor_local = {
        .seed = actor_local_seed(),
        .score = actor_direct,
        .hidden_repartition_nonvacuous =
            actor.direct_state != actor.hidden_state,
        .observation_bit_identical =
            actor.observation_identical,
        .legal_actions_bit_identical =
            actor.legal_actions_identical,
        .score_bit_identical = true,
        .one_level_nesting_bounded =
            actor_direct.accounting
                .inner_rollout_evaluations == 0,
    };
    report.hypothesis_passed =
        all_directions &&
        report.actor_local.gate_passed();
    return report;
}

void print_report(
    std::ostream& output, const Report& report) {
    output
        << std::setprecision(17)
        << "schema=old-school-action-q-aq4-d1-nested-actor-v1\n"
        << "mode=diagnose\n"
        << "parent_fingerprint="
        << report.parent_fingerprint << '\n'
        << "root_seed=" << kRootSeed
        << " worlds=" << kWorlds
        << " rollouts_per_world="
        << kRolloutsPerWorld
        << " horizon_turns=" << kHorizonTurns
        << " evaluation_threads="
        << kEvaluationThreads
        << " inner_worlds=" << kInnerWorlds
        << " inner_rollouts_per_world="
        << kLearnedValueSearchRolloutsPerWorld
        << " inner_horizon_turns="
        << kInnerHorizonTurns
        << " continuation_variant=ValueSearchChampion"
        << " continuation_controller=Legacy"
        << " continuation_exploration=0"
        << " priority_residual=0"
        << " pass_dominance=0"
        << " outer_shallow_prior_blend=0"
        << " inner_shallow_prior_blend=1"
        << " resolved_shallow_prior=0\n";
    EvaluationAccounting total;
    std::size_t total_actions = 0;
    for (const FixtureReport& fixture :
         report.fixtures) {
        total.sampled_worlds +=
            fixture.score.accounting.sampled_worlds;
        total.rollout_evaluations +=
            fixture.score.accounting.rollout_evaluations;
        total.terminal_evaluations +=
            fixture.score.accounting.terminal_evaluations;
        total.bootstrapped_evaluations +=
            fixture.score.accounting.bootstrapped_evaluations;
        total.inner_rollout_evaluations +=
            fixture.score.accounting
                .inner_rollout_evaluations;
        total.inner_search_invocations +=
            fixture.score.accounting
                .inner_search_invocations;
        total.inner_search_max_depth =
            std::max(
                total.inner_search_max_depth,
                fixture.score.accounting
                    .inner_search_max_depth);
        total_actions += fixture.score.actions.size();
        output
            << "fixture index="
            << fixture.spec.fixture_index
            << " stable_id=" << fixture.spec.stable_id
            << " seed=" << fixture.seed
            << " legal_actions="
            << fixture.score.actions.size()
            << " selected="
            << fixture.score.selected_probe_key
            << " sampled_worlds="
            << fixture.score.accounting.sampled_worlds
            << " rollout_evaluations="
            << fixture.score.accounting.rollout_evaluations
            << " terminal_evaluations="
            << fixture.score.accounting.terminal_evaluations
            << " bootstrapped_evaluations="
            << fixture.score.accounting
                   .bootstrapped_evaluations
            << " inner_rollout_evaluations="
            << fixture.score.accounting
                   .inner_rollout_evaluations
            << " inner_search_invocations="
            << fixture.score.accounting
                   .inner_search_invocations
            << " inner_search_max_depth="
            << fixture.score.accounting
                   .inner_search_max_depth
            << " hidden_repartition_nonvacuous="
            << fixture.hidden_repartition_nonvacuous
            << " hidden_repartition_bit_identical="
            << fixture.hidden_repartition_bit_identical
            << " reversed_action_bit_identical="
            << fixture.reversed_action_bit_identical
            << '\n';
        for (const ActionScore& action :
             fixture.score.actions) {
            output
                << "action fixture="
                << fixture.spec.fixture_index
                << " probe_key=" << action.probe_key
                << " typed_descriptor="
                << action.typed_descriptor
                << " mean=" << action.mean
                << " exact_max=" << action.exact_max
                << " inner_rollout_evaluations="
                << checked_sum(
                       action
                           .inner_rollout_evaluations)
                << " inner_search_invocations="
                << checked_sum(
                       action
                           .inner_search_invocations)
                << " inner_search_max_depth="
                << (action.inner_search_max_depth.empty()
                        ? 0
                        : *std::max_element(
                              action
                                  .inner_search_max_depth
                                  .begin(),
                              action
                                  .inner_search_max_depth
                                  .end()))
                << '\n';
        }
        output
            << "support fixture="
            << fixture.spec.fixture_index
            << " keys="
            << support_string(
                   fixture.direction.exact_max_support)
            << '\n'
            << "margin fixture="
            << fixture.spec.fixture_index
            << " positive_value="
            << fixture.direction.positive_value
            << " negative_value="
            << fixture.direction.negative_value
            << " required_margin="
            << fixture.direction.required_margin
            << " excluded_margin_0="
            << fixture.direction.excluded_margins[0]
            << " excluded_margin_1="
            << fixture.direction.excluded_margins[1]
            << " direction_passed="
            << fixture.direction.passed << '\n';
    }
    output
        << "actor_local seed="
        << report.actor_local.seed
        << " legal_actions="
        << report.actor_local.score.actions.size()
        << " selected="
        << report.actor_local.score.selected_probe_key
        << " sampled_worlds="
        << report.actor_local.score.accounting.sampled_worlds
        << " rollout_evaluations="
        << report.actor_local.score.accounting
               .rollout_evaluations
        << " terminal_evaluations="
        << report.actor_local.score.accounting
               .terminal_evaluations
        << " bootstrapped_evaluations="
        << report.actor_local.score.accounting
               .bootstrapped_evaluations
        << " inner_rollout_evaluations="
        << report.actor_local.score.accounting
               .inner_rollout_evaluations
        << " inner_search_invocations="
        << report.actor_local.score.accounting
               .inner_search_invocations
        << " inner_search_max_depth="
        << report.actor_local.score.accounting
               .inner_search_max_depth
        << " hidden_repartition_nonvacuous="
        << report.actor_local.hidden_repartition_nonvacuous
        << " observation_bit_identical="
        << report.actor_local.observation_bit_identical
        << " legal_actions_bit_identical="
        << report.actor_local.legal_actions_bit_identical
        << " score_bit_identical="
        << report.actor_local.score_bit_identical
        << " one_level_nesting_bounded="
        << report.actor_local.one_level_nesting_bounded
        << '\n'
        << "accounting fixtures="
        << report.fixtures.size()
        << " actions=" << total_actions
        << " sampled_worlds="
        << total.sampled_worlds
        << " rollout_evaluations="
        << total.rollout_evaluations
        << " terminal_evaluations="
        << total.terminal_evaluations
        << " bootstrapped_evaluations="
        << total.bootstrapped_evaluations
        << " inner_rollout_evaluations="
        << total.inner_rollout_evaluations
        << " inner_search_invocations="
        << total.inner_search_invocations
        << " inner_search_max_depth="
        << total.inner_search_max_depth << '\n'
        << "result="
        << (report.gate_passed() ? "PASS" : "FAIL")
        << " hypothesis_passed="
        << report.hypothesis_passed
        << " fit_performed=0"
        << " corpus_collected=0"
        << " artifact_published=0"
        << " benchmark_run=0"
        << " gameplay_seed_opened=0\n";
}

} // namespace old_school::action_q_nested_actor_diagnostic
