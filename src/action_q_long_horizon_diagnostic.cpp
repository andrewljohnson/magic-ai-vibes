#include "old_school/action_q_long_horizon_diagnostic.hpp"

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

namespace old_school::action_q_long_horizon_diagnostic {
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
            "AQ3-D0 " + std::string(corpus_name) +
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
            "AQ3-D0 fixture is missing or duplicated: " +
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
                "AQ3-D0 probe action mapping is ambiguous");
        }
        found = &candidate;
    }
    if (found == nullptr) {
        throw std::runtime_error(
            "AQ3-D0 authoritative action has no probe key");
    }
    return *found;
}

std::vector<PriorityAction> authoritative_actions(
    const probes::DecisionProbe& probe) {
    if (probe.decision_kind !=
            probes::DecisionKind::Priority ||
        probe.root_player >= 2) {
        throw std::runtime_error(
            "AQ3-D0 fixture is not a Priority decision");
    }
    const std::vector<PriorityAction> actions =
        legal_priority_actions(
            probe.state, probe.root_player,
            sorcery_actions_for(probe));
    if (actions.size() != probe.candidates.size()) {
        throw std::runtime_error(
            "AQ3-D0 probe omits an authoritative legal action");
    }
    for (const PriorityAction& action : actions) {
        static_cast<void>(
            candidate_for_action(probe, action));
    }
    return actions;
}

GameState hidden_repartition_clone(
    const probes::DecisionProbe& probe) {
    if (probe.root_player >= probe.state.players.size()) {
        throw std::invalid_argument(
            "AQ3-D0 hidden witness observer is invalid");
    }
    GameState clone = probe.state;
    std::reverse(
        clone.players[probe.root_player].library.begin(),
        clone.players[probe.root_player].library.end());

    const std::size_t opponent = 1 - probe.root_player;
    const PlayerState& original_hidden =
        probe.state.players[opponent];
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
    // This conjunction is the nonvacuous witness: private identities really
    // cross the hand/library boundary while the root player's complete
    // observation and legal set remain unchanged.
    if (!crossed_zone_boundary ||
        hidden.hand.size() !=
            original_hidden.hand.size() ||
        hidden.library.size() !=
            original_hidden.library.size() ||
        cloned_hand == original_hand ||
        cloned_library == original_library ||
        cloned_cards != original_cards ||
        observe_game_state(
            probe.state, probe.root_player) !=
            observe_game_state(
                clone, probe.root_player) ||
        legal_priority_actions(
            probe.state, probe.root_player,
            sorcery_actions_for(probe)) !=
            legal_priority_actions(
                clone, probe.root_player,
                sorcery_actions_for(probe))) {
        throw std::runtime_error(
            "AQ3-D0 hidden repartition witness is vacuous "
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

std::vector<PreparedFixture> prepare_fixtures() {
    const auto manifest = fixture_manifest();
    const auto fixtures = load_fixtures();
    if (fixtures.size() != manifest.size()) {
        throw std::logic_error(
            "AQ3-D0 fixture loader changed size");
    }

    std::vector<PreparedFixture> prepared;
    prepared.reserve(fixtures.size());
    for (std::size_t index = 0;
         index < fixtures.size(); ++index) {
        probes::DecisionProbe probe = fixtures[index];
        if (probe.stable_id !=
            manifest[index].stable_id) {
            throw std::logic_error(
                "AQ3-D0 fixture order drifted");
        }
        std::vector<PriorityAction> actions =
            authoritative_actions(probe);
        probes::DecisionProbe hidden_probe = probe;
        hidden_probe.state =
            hidden_repartition_clone(probe);
        std::vector<PriorityAction> hidden_actions =
            authoritative_actions(hidden_probe);
        if (hidden_probe.state == probe.state ||
            hidden_actions != actions) {
            throw std::logic_error(
                "AQ3-D0 hidden witness is vacuous or changed "
                "legal actions");
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

RootScore score_root(
    const probes::DecisionProbe& probe,
    const std::vector<PriorityAction>& actions,
    const std::shared_ptr<const LearnedModel>& parent,
    std::uint64_t seed) {
    const LearnedActionSamples scored =
        learned_priority_action_samples(
            probe.state, probe.original_decks,
            probe.root_player,
            sorcery_actions_for(probe), probe.phase,
            probe.consecutive_passes, actions, parent,
            search_config(seed));
    if (scored.q_samples.size() != actions.size() ||
        scored.priority_continuation_samples.size() !=
            actions.size() ||
        scored.priority_shallow_prior_samples.size() !=
            actions.size() ||
        scored.exact_priority_aggregate_scores.size() !=
            actions.size() ||
        !scored.priority_h0_boundaries.empty()) {
        throw std::logic_error(
            "AQ3-D0 scorer returned incomplete action rows");
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
    };
    result.actions.reserve(actions.size());
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        const auto& samples = scored.q_samples[index];
        if (samples.size() != kWorlds ||
            scored.priority_continuation_samples[index]
                    .size() != kWorlds ||
            scored.priority_shallow_prior_samples[index]
                    .size() != kWorlds ||
            !vector_bits_identical(
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
                "AQ3-D0 unblended continuation rows drifted");
        }
        double mean = 0.0;
        for (const double sample : samples) {
            if (!probability(sample)) {
                throw std::logic_error(
                    "AQ3-D0 continuation is not a "
                    "probability");
            }
            mean += sample;
        }
        mean /= static_cast<double>(samples.size());
        if (!same_bits(
                mean,
                scored.exact_priority_aggregate_scores[
                    index])) {
            throw std::logic_error(
                "AQ3-D0 mean drifted from engine aggregate");
        }
        const probes::Candidate& candidate =
            candidate_for_action(probe, actions[index]);
        result.actions.push_back({
            .probe_key = candidate.descriptor,
            .typed_descriptor =
                probes::stable_priority_action_descriptor(
                    actions[index]),
            .action = actions[index],
            .samples = samples,
            .mean = mean,
        });
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
            "AQ3-D0 typed action descriptors are not unique");
    }
    return result;
}

bool action_score_bit_identical(
    const ActionScore& first,
    const ActionScore& second) {
    if (first.probe_key != second.probe_key ||
        first.typed_descriptor != second.typed_descriptor ||
        first.action != second.action ||
        first.samples.size() != second.samples.size() ||
        !same_bits(first.mean, second.mean) ||
        first.exact_max != second.exact_max) {
        return false;
    }
    return vector_bits_identical(
        first.samples, second.samples);
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
        << "Usage: old-school-action-q-long-horizon-diagnostic "
           "--diagnose\n";
}

std::uint64_t fixture_seed(std::size_t fixture_index) {
    if (fixture_index >= kFixtureCount) {
        throw std::out_of_range(
            "AQ3-D0 fixture index is outside the manifest");
    }
    return learned_iteration::derive_seed(
        kRootSeed,
        learned_iteration::SeedDomain::PrioritySearch,
        fixture_index, 0, 0);
}

std::array<FixtureSpec, kFixtureCount>
fixture_manifest() {
    const std::array<FixtureSpec, kFixtureCount> manifest{{
        {
            .fixture_index = 0,
            .stable_id =
                "control.blue.counter-same-target-after-"
                "intervening-counter.v1",
            .kind = DirectionKind::StrictPair,
            .positive_key =
                "counter-opponent-counterspell",
            .negative_key = "pass",
            .expected_seed =
                14244684161368182184ULL,
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
                10350313418552302294ULL,
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
                2017596409782985296ULL,
        },
        {
            .fixture_index = 3,
            .stable_id =
                "control.blue.force-spike-live-gray-ogre.v1",
            .kind = DirectionKind::StrictPair,
            .positive_key = "force-spike-gray-ogre",
            .negative_key = "pass",
            .expected_seed =
                2496096984247125995ULL,
        },
    }};
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        if (manifest[index].fixture_index != index ||
            manifest[index].stable_id.empty() ||
            manifest[index].expected_seed !=
                fixture_seed(index)) {
            throw std::logic_error(
                "AQ3-D0 frozen manifest seed drifted");
        }
    }
    return manifest;
}

LearnedSearchConfig search_config(std::uint64_t seed) {
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
    };
}

DirectionSummary evaluate_direction(
    const FixtureSpec& spec,
    std::span<const ActionScore> actions) {
    if (actions.empty()) {
        throw std::invalid_argument(
            "AQ3-D0 direction requires legal actions");
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
                "AQ3-D0 action rows are invalid or duplicated");
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
                    "AQ3-D0 required probe key is absent");
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
                "AQ3-D0 strict-pair manifest is invalid");
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
    if (spec.kind != DirectionKind::ExcludeXZero ||
        spec.excluded_keys[0].empty() ||
        spec.excluded_keys[1].empty() ||
        spec.excluded_keys[0] ==
            spec.excluded_keys[1]) {
        throw std::invalid_argument(
            "AQ3-D0 X=0 manifest is invalid");
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
            "AQ3-D0 hidden-repartition identity failed");
    }
    if (!root_scores_bit_identical(
            direct, reversed_actions)) {
        throw std::runtime_error(
            "AQ3-D0 reversed-action identity failed");
    }
}

bool FixtureReport::gate_passed() const {
    if (spec.fixture_index >= kFixtureCount ||
        seed != spec.expected_seed ||
        seed != fixture_seed(spec.fixture_index) ||
        actions.empty() ||
        accounting.sampled_worlds != kWorlds ||
        accounting.rollout_evaluations !=
            actions.size() * kWorlds *
                kRolloutsPerWorld ||
        accounting.terminal_evaluations +
                accounting.bootstrapped_evaluations !=
            accounting.rollout_evaluations ||
        !direction.passed ||
        !hidden_repartition_nonvacuous ||
        !hidden_repartition_bit_identical ||
        !reversed_action_bit_identical) {
        return false;
    }
    for (const ActionScore& action : actions) {
        if (action.samples.size() !=
                kWorlds * kRolloutsPerWorld) {
            return false;
        }
        double mean = 0.0;
        for (const double sample : action.samples) {
            if (!probability(sample)) {
                return false;
            }
            mean += sample;
        }
        mean /= static_cast<double>(
            action.samples.size());
        if (!same_bits(mean, action.mean)) {
            return false;
        }
    }
    DirectionSummary observed;
    try {
        observed = evaluate_direction(spec, actions);
    } catch (const std::exception&) {
        return false;
    }
    if (!direction_bit_identical(observed, direction)) {
        return false;
    }
    for (const ActionScore& action : actions) {
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

bool Report::gate_passed() const {
    if (parent_fingerprint !=
            kRequiredParentFingerprint ||
        fixtures.size() != kFixtureCount) {
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
    return hypothesis_passed == all_directions &&
           hypothesis_passed;
}

void validate_fixture_witnesses() {
    static_cast<void>(prepare_fixtures());
}

Report diagnose(
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "AQ3-D0 requires exact frozen C16");
    }
    const auto manifest = fixture_manifest();
    // Complete the hidden-safety/nonvacuity preflight for every fixture
    // before the first call that can consume an AQ3 science seed.
    const auto prepared = prepare_fixtures();

    Report report;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    bool all_directions = true;
    for (std::size_t index = 0;
         index < prepared.size(); ++index) {
        const PreparedFixture& input =
            prepared[index];
        const probes::DecisionProbe& probe =
            input.probe;
        const FixtureSpec& spec = manifest[index];
        const std::uint64_t seed =
            fixture_seed(index);
        RootScore direct =
            score_root(
                probe, input.actions, parent, seed);

        auto reversed_actions = input.actions;
        std::reverse(
            reversed_actions.begin(),
            reversed_actions.end());
        const RootScore reversed =
            score_root(
                probe, reversed_actions, parent, seed);

        const RootScore hidden =
            score_root(
                input.hidden_probe,
                input.hidden_actions, parent, seed);
        // Identity drift invalidates the evidence. It must never be
        // interpreted or printed as an ordinary directional rejection.
        require_invariant_root_scores(
            direct, hidden, reversed);

        FixtureReport fixture;
        fixture.spec = spec;
        fixture.seed = seed;
        fixture.accounting = direct.accounting;
        fixture.hidden_repartition_nonvacuous =
            input.hidden_probe.state != probe.state;
        fixture.hidden_repartition_bit_identical = true;
        fixture.reversed_action_bit_identical = true;
        fixture.actions = std::move(direct.actions);
        fixture.direction =
            evaluate_direction(
                fixture.spec, fixture.actions);
        for (ActionScore& action : fixture.actions) {
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
        all_directions =
            all_directions &&
            fixture.direction.passed;
        report.fixtures.push_back(
            std::move(fixture));
    }
    report.hypothesis_passed = all_directions;
    return report;
}

void print_report(
    std::ostream& output, const Report& report) {
    output
        << std::setprecision(17)
        << "schema=old-school-action-q-aq3-d0-long-horizon-v1\n"
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
        << " continuation_variant=ValueSearchChampion"
        << " continuation_controller=Legacy"
        << " continuation_exploration=0"
        << " priority_residual=0"
        << " pass_dominance=0"
        << " shallow_prior_blend=0"
        << " resolved_shallow_prior=0\n";
    EvaluationAccounting total;
    std::size_t total_actions = 0;
    for (const FixtureReport& fixture :
         report.fixtures) {
        total.sampled_worlds +=
            fixture.accounting.sampled_worlds;
        total.rollout_evaluations +=
            fixture.accounting.rollout_evaluations;
        total.terminal_evaluations +=
            fixture.accounting.terminal_evaluations;
        total.bootstrapped_evaluations +=
            fixture.accounting.bootstrapped_evaluations;
        total_actions += fixture.actions.size();
        output
            << "fixture index="
            << fixture.spec.fixture_index
            << " stable_id=" << fixture.spec.stable_id
            << " seed=" << fixture.seed
            << " legal_actions="
            << fixture.actions.size()
            << " sampled_worlds="
            << fixture.accounting.sampled_worlds
            << " rollout_evaluations="
            << fixture.accounting.rollout_evaluations
            << " terminal_evaluations="
            << fixture.accounting.terminal_evaluations
            << " bootstrapped_evaluations="
            << fixture.accounting
                   .bootstrapped_evaluations
            << " hidden_repartition_nonvacuous="
            << fixture.hidden_repartition_nonvacuous
            << " hidden_repartition_bit_identical="
            << fixture.hidden_repartition_bit_identical
            << " reversed_action_bit_identical="
            << fixture.reversed_action_bit_identical
            << '\n';
        for (const ActionScore& action :
             fixture.actions) {
            output
                << "action fixture="
                << fixture.spec.fixture_index
                << " probe_key=" << action.probe_key
                << " typed_descriptor="
                << action.typed_descriptor
                << " mean=" << action.mean
                << " exact_max=" << action.exact_max
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
        << total.bootstrapped_evaluations << '\n'
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

} // namespace old_school::action_q_long_horizon_diagnostic
