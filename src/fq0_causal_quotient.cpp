#include "old_school/fq0_causal_quotient.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq0_bellman.hpp"
#include "old_school/fq0_bellman_science.hpp"
#include "old_school/fq0_information_set.hpp"
#include "old_school/fq0_sequence_projection.hpp"
#include "old_school/probes.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <utility>

namespace old_school::fq0_causal_quotient {
namespace {

namespace information = fq0_information_set;
namespace projection = fq0_sequence_projection;
namespace science = fq0_bellman_science;

inline constexpr std::string_view kBlueRoot =
    "control.blue.counter-same-target-after-intervening-counter.v1";
inline constexpr std::string_view kWhiteRoot =
    "white.mill-before-draw.v3";

GameState canonicalize_graveyards(GameState state) {
    for (PlayerState& player : state.players) {
        std::sort(
            player.graveyard.begin(), player.graveyard.end());
    }
    return state;
}

bool treatment_has_graveyards(
    SequenceTreatment treatment) {
    return treatment == SequenceTreatment::Graveyards ||
           treatment ==
               SequenceTreatment::GraveyardsAndObserverHand;
}

bool treatment_has_observer_hand(
    SequenceTreatment treatment) {
    return treatment == SequenceTreatment::ObserverHand ||
           treatment ==
               SequenceTreatment::GraveyardsAndObserverHand;
}

GameState canonicalize_treated_sequences(
    GameState state, SequenceTreatment treatment,
    std::size_t observer) {
    if (treatment_has_graveyards(treatment)) {
        state = canonicalize_graveyards(std::move(state));
    }
    if (treatment_has_observer_hand(treatment) &&
        observer < state.players.size()) {
        std::sort(
            state.players[observer].hand.begin(),
            state.players[observer].hand.end());
    }
    return state;
}

PlayerObservation canonicalize_graveyards(
    PlayerObservation observation) {
    for (PublicPlayerState& player : observation.players) {
        std::sort(
            player.graveyard.begin(), player.graveyard.end());
    }
    return observation;
}

bool graveyard_order_differs(
    const GameState& first, const GameState& second) {
    return first.players[0].graveyard !=
               second.players[0].graveyard ||
           first.players[1].graveyard !=
               second.players[1].graveyard;
}

bool observer_hand_order_differs(
    const GameState& first, const GameState& second,
    std::size_t observer) {
    return observer < first.players.size() &&
           observer < second.players.size() &&
           first.players[observer].hand !=
               second.players[observer].hand;
}

bool same_card_multiset(
    std::vector<CardId> first,
    std::vector<CardId> second) {
    std::sort(first.begin(), first.end());
    std::sort(second.begin(), second.end());
    return first == second;
}

bool relevant_multisets_preserved(
    const GameState& first, const GameState& second,
    std::size_t observer, SequenceTreatment treatment) {
    if (observer >= first.players.size() ||
        observer >= second.players.size()) {
        return false;
    }
    if (treatment_has_graveyards(treatment)) {
        for (std::size_t player = 0;
             player < first.players.size(); ++player) {
            if (!same_card_multiset(
                    first.players[player].graveyard,
                    second.players[player].graveyard)) {
                return false;
            }
        }
    }
    return !treatment_has_observer_hand(treatment) ||
           same_card_multiset(
               first.players[observer].hand,
               second.players[observer].hand);
}

bool treatment_is_nontrivial(
    const GameState& first, const GameState& second,
    std::size_t observer, SequenceTreatment treatment) {
    const bool graveyards_differ =
        graveyard_order_differs(first, second);
    const bool hand_differs =
        observer_hand_order_differs(
            first, second, observer);
    switch (treatment) {
    case SequenceTreatment::Graveyards:
        return graveyards_differ;
    case SequenceTreatment::ObserverHand:
        return hand_differs;
    case SequenceTreatment::GraveyardsAndObserverHand:
        return graveyards_differ && hand_differs;
    }
    return false;
}

bool bit_identical(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool bit_identical(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    return first.size() == second.size() &&
           std::equal(
               first.begin(), first.end(), second.begin(),
               [](double left, double right) {
                   return bit_identical(left, right);
               });
}

std::size_t candidate_index(
    const probes::DecisionProbe& probe,
    std::string_view descriptor) {
    const auto found = std::find_if(
        probe.candidates.begin(), probe.candidates.end(),
        [&](const probes::Candidate& candidate) {
            return candidate.descriptor == descriptor;
        });
    if (found == probe.candidates.end()) {
        throw std::logic_error(
            "FR1 direct control action is missing");
    }
    return static_cast<std::size_t>(
        std::distance(probe.candidates.begin(), found));
}

void move_card_from_library(
    PlayerState& player, CardId card) {
    const auto found = std::find(
        player.library.begin(), player.library.end(), card);
    if (found == player.library.end()) {
        throw std::logic_error(
            "FR1 direct control lacks a required card");
    }
    player.library.erase(found);
}

GameState buried_white_state() {
    GameState state;
    state.active_player = 0;
    state.starting_player = 0;
    state.turn_number = 9;
    state.next_permanent_id = 2;
    state.players[0].library = white_control_deck();
    move_card_from_library(
        state.players[0], CardId::Plains);
    state.players[0].hand = {CardId::Plains};
    move_card_from_library(
        state.players[0], CardId::Plains);
    move_card_from_library(
        state.players[0], CardId::Plains);
    state.players[0].lands = {
        {.card = CardId::Plains},
        {.card = CardId::Plains},
    };
    move_card_from_library(
        state.players[0], CardId::Millstone);
    state.players[0].artifacts = {
        {
            .id = 1,
            .card = CardId::Millstone,
            .tapped = false,
        },
    };
    move_card_from_library(
        state.players[0], CardId::Plains);
    move_card_from_library(
        state.players[0], CardId::Moat);
    move_card_from_library(
        state.players[0], CardId::Millstone);
    state.players[0].graveyard = {
        CardId::Plains,
        CardId::Moat,
        CardId::Millstone,
    };
    state.players[1].library = blue_deck();
    move_card_from_library(
        state.players[1], CardId::FlyingMen);
    move_card_from_library(
        state.players[1], CardId::Island);
    state.players[1].hand = {
        CardId::FlyingMen,
        CardId::Island,
    };
    return state;
}

bool valid_complete_transition(
    const LearnedPriorityMacroTransition& transition) {
    if (transition.exhausted_limit !=
        LearnedPriorityMacroLimit::None) {
        return false;
    }
    if (transition.disposition ==
        LearnedPriorityMacroDisposition::Terminal) {
        return transition.terminal_result.has_value() &&
               !transition.context.valid &&
               transition.legal_actions.empty();
    }
    if (transition.disposition !=
            LearnedPriorityMacroDisposition::PriorityBoundary ||
        transition.terminal_result.has_value() ||
        !transition.context.valid ||
        transition.legal_actions.size() < 2) {
        return false;
    }
    return transition.legal_actions ==
           legal_priority_actions(
               transition.state,
               transition.context.decision_player,
               transition.context.sorcery_actions);
}

ActionComparison compare_action(
    const GameState& first, const GameState& second,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    std::size_t observer,
    const PriorityAction& action,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t seed,
    SequenceTreatment treatment =
        SequenceTreatment::Graveyards) {
    const LearnedPriorityMacroTransition left =
        advance_learned_priority_macro_transition(
            first, original_decks, context.decision_player,
            context.sorcery_actions, context.phase,
            context.consecutive_passes, action, model, seed);
    const LearnedPriorityMacroTransition right =
        advance_learned_priority_macro_transition(
            second, original_decks, context.decision_player,
            context.sorcery_actions, context.phase,
            context.consecutive_passes, action, model, seed);

    ActionComparison result{
        .action = action,
        .complete =
            valid_complete_transition(left) &&
            valid_complete_transition(right),
        .disposition_equal =
            left.disposition == right.disposition &&
            left.exhausted_limit == right.exhausted_limit,
        .terminal_result_equal =
            left.terminal_result == right.terminal_result,
        .next_context_equal = left.context == right.context,
        .next_legal_actions_equal =
            left.legal_actions == right.legal_actions,
        .observation_bit_identical =
            bit_identical(
                learned_observation(left.state, observer),
                learned_observation(right.state, observer)),
        .canonical_successor_state_equal =
            canonicalize_treated_sequences(
                left.state, treatment, observer) ==
            canonicalize_treated_sequences(
                right.state, treatment, observer),
        .accounting_equal =
            std::tie(
                left.actions_applied,
                left.priority_actions_applied,
                left.phase_transitions,
                left.turn_advances) ==
            std::tie(
                right.actions_applied,
                right.priority_actions_applied,
                right.phase_transitions,
                right.turn_advances),
    };
    if (result.complete && result.disposition_equal) {
        if (left.disposition ==
            LearnedPriorityMacroDisposition::Terminal) {
            result.value_bit_identical =
                bit_identical(
                    information::terminal_root_owner_value(
                        *left.terminal_result, observer),
                    information::terminal_root_owner_value(
                        *right.terminal_result, observer));
        } else {
            const information::LegacyLeafCriticEvaluation
                left_critic =
                    information::evaluate_legacy_leaf_critic(
                        left.state, observer, left.context,
                        model);
            const information::LegacyLeafCriticEvaluation
                right_critic =
                    information::evaluate_legacy_leaf_critic(
                        right.state, observer, right.context,
                        model);
            result.value_bit_identical =
                left_critic.legacy_bit_identity &&
                right_critic.legacy_bit_identity &&
                left_critic == right_critic;
        }
    }
    return result;
}

LearnedDecisionContext context_for(
    const probes::DecisionProbe& probe) {
    bool sorcery_actions = false;
    switch (probe.phase) {
    case TurnPhase::FirstMain:
    case TurnPhase::SecondMain:
        sorcery_actions = true;
        break;
    case TurnPhase::BeginCombat:
    case TurnPhase::EndCombat:
        break;
    case TurnPhase::DeclareAttackers:
    case TurnPhase::DeclareBlockers:
    case TurnPhase::DamageOrder:
        throw std::invalid_argument(
            "FR1 root cannot use a declaration phase");
    }
    return {
        .valid = true,
        .phase = probe.phase,
        .decision_player = probe.root_player,
        .consecutive_passes = probe.consecutive_passes,
        .sorcery_actions = sorcery_actions,
    };
}

GameState canonical_root_state(
    const probes::DecisionProbe& probe) {
    GameState result = probe.state;
    std::sort(
        result.players[probe.root_player].hand.begin(),
        result.players[probe.root_player].hand.end());
    return result;
}

const probes::DecisionProbe& find_root(
    const std::vector<probes::DecisionProbe>& probes,
    std::string_view stable_id) {
    const auto found = std::find_if(
        probes.begin(), probes.end(),
        [stable_id](const probes::DecisionProbe& probe) {
            return probe.stable_id == stable_id;
        });
    if (found == probes.end()) {
        throw std::logic_error(
            "FR1 could not find frozen root " +
            std::string(stable_id));
    }
    return *found;
}

struct ReconstructedState {
    GameState state;
    LearnedDecisionContext context;
    std::vector<PriorityAction> legal_actions;
    std::size_t owner = 0;
    std::size_t root_world = 0;
    std::string root_action;
    std::uint64_t macro_seed = 0;
};

using ReconstructedStates =
    std::map<std::string, std::vector<ReconstructedState>>;

void reconstruct_root(
    const probes::DecisionProbe& probe,
    std::shared_ptr<const LearnedModel> model,
    ReconstructedStates& states,
    RegisteredAnatomyReport& report) {
    const LearnedDecisionContext root_context =
        context_for(probe);
    const GameState root = canonical_root_state(probe);
    const std::string manifest_fingerprint =
        probes::bsr_information_action_fingerprint(probe);
    const std::vector<PriorityAction> authoritative =
        legal_priority_actions(
            root, probe.root_player,
            root_context.sorcery_actions);
    if (authoritative.size() != probe.candidates.size()) {
        throw std::logic_error(
            probe.stable_id +
            ": FR1 root candidates are not authoritative");
    }
    std::vector<std::pair<std::string, PriorityAction>>
        declared;
    declared.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate :
         probe.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate.action);
        if (action == nullptr ||
            std::find(
                authoritative.begin(),
                authoritative.end(),
                *action) == authoritative.end() ||
            std::find_if(
                declared.begin(), declared.end(),
                [&](const auto& existing) {
                    return existing.second == *action;
                }) != declared.end()) {
            throw std::logic_error(
                probe.stable_id +
                ": FR1 root has a duplicate or "
                "nonauthoritative action");
        }
        declared.emplace_back(
            candidate.descriptor, *action);
    }

    for (std::size_t world = 0;
         world < science::kProductionRootWorlds; ++world) {
        const std::uint64_t determinization_seed =
            information::derive_indexed_seed(
                science::kProductionRootSeedBase,
                {
                    .domain =
                        information::SeedDomain::
                            RootDeterminization,
                    .scope = probe.stable_id,
                    .group = manifest_fingerprint,
                    .bank = information::SeedBank::Root,
                    .block = fq0_bellman::kBlockCount,
                    .world = world,
                });
        const std::uint64_t macro_seed =
            information::derive_indexed_seed(
                science::kProductionRootSeedBase,
                {
                    .domain =
                        information::SeedDomain::
                            RootMacroTransition,
                    .scope = probe.stable_id,
                    .group = manifest_fingerprint,
                    .bank = information::SeedBank::Root,
                    .block = fq0_bellman::kBlockCount,
                    .world = world,
                });
        const GameState sampled = sample_determinization(
            root, probe.original_decks, probe.root_player,
            determinization_seed);
        for (const auto& [descriptor, action] : declared) {
            const LearnedPriorityMacroTransition transition =
                advance_learned_priority_macro_transition(
                    sampled, probe.original_decks,
                    probe.root_player,
                    root_context.sorcery_actions,
                    root_context.phase,
                    root_context.consecutive_passes,
                    action, model, macro_seed);
            ++report.bounded_root_macros;
            if (transition.disposition ==
                LearnedPriorityMacroDisposition::Incomplete) {
                ++report.incomplete_root_macros;
                continue;
            }
            if (transition.disposition ==
                LearnedPriorityMacroDisposition::Terminal) {
                continue;
            }
            if (transition.disposition !=
                    LearnedPriorityMacroDisposition::
                        PriorityBoundary ||
                !transition.context.valid ||
                transition.legal_actions.size() < 2) {
                throw std::logic_error(
                    probe.stable_id +
                    ": FR1 root macro returned an invalid boundary");
            }
            const std::size_t owner =
                transition.context.decision_player;
            const auto authoritative_successor =
                legal_priority_actions(
                    transition.state, owner,
                    transition.context.sorcery_actions);
            if (authoritative_successor !=
                transition.legal_actions) {
                throw std::logic_error(
                    probe.stable_id +
                    ": FR1 root macro returned stale actions");
            }
            const std::string fingerprint =
                information::information_set_sha256(
                    information::make_information_set_key(
                        transition.state,
                        transition.context,
                        transition.legal_actions));
            states[fingerprint].push_back({
                .state = transition.state,
                .context = transition.context,
                .legal_actions =
                    transition.legal_actions,
                .owner = owner,
                .root_world = world,
                .root_action = descriptor,
                .macro_seed = macro_seed,
            });
        }
    }
}

bool same_public_except_graveyard_order(
    const ReconstructedState& first,
    const ReconstructedState& second) {
    return first.owner == second.owner &&
           first.context == second.context &&
           first.legal_actions == second.legal_actions &&
           canonicalize_graveyards(
               observe_game_state(first.state, first.owner)) ==
               canonicalize_graveyards(
                   observe_game_state(
                       second.state, second.owner));
}

std::string public_difference(
    const ReconstructedState& first,
    const ReconstructedState& second) {
    if (first.owner != second.owner) {
        return "owner";
    }
    if (first.context != second.context) {
        return "context";
    }
    if (first.legal_actions != second.legal_actions) {
        return "legal-actions";
    }
    PlayerObservation left =
        observe_game_state(first.state, first.owner);
    PlayerObservation right =
        observe_game_state(second.state, second.owner);
    for (std::size_t player = 0;
         player < left.players.size(); ++player) {
        std::sort(
            left.players[player].graveyard.begin(),
            left.players[player].graveyard.end());
        std::sort(
            right.players[player].graveyard.begin(),
            right.players[player].graveyard.end());
    }
    if (left.hand != right.hand) {
        std::vector<CardId> left_hand = left.hand;
        std::vector<CardId> right_hand = right.hand;
        std::sort(left_hand.begin(), left_hand.end());
        std::sort(right_hand.begin(), right_hand.end());
        if (left_hand != right_hand) {
            return "observer-hand-composition-or-multiple";
        }
        left.hand = std::move(left_hand);
        right.hand = std::move(right_hand);
        return left == right
                   ? "observer-hand-order-only"
                   : "observer-hand-order-plus-other";
    }
    if (left.players != right.players) {
        return "public-player-state";
    }
    if (left.stack != right.stack) {
        return "stack";
    }
    if (left.extra_turns_pending !=
        right.extra_turns_pending) {
        return "extra-turns";
    }
    if (std::tie(
            left.observer, left.revealed_opponent_hand,
            left.active_player, left.starting_player,
            left.turn_number) !=
        std::tie(
            right.observer, right.revealed_opponent_hand,
            right.active_player, right.starting_player,
            right.turn_number)) {
        return "observation-metadata";
    }
    return "unknown";
}

const ReconstructedState* canonical_representative(
    const std::vector<ReconstructedState>& states) {
    if (states.empty()) {
        return nullptr;
    }
    return &*std::min_element(
        states.begin(), states.end(),
        [](const ReconstructedState& left,
           const ReconstructedState& right) {
            return std::tie(
                       left.root_world,
                       left.root_action) <
                   std::tie(
                       right.root_world,
                       right.root_action);
        });
}

std::string fingerprint_for(
    const GameState& state,
    const LearnedDecisionContext& context,
    const std::vector<PriorityAction>& actions) {
    return information::information_set_sha256(
        information::make_information_set_key(
            state, context, actions));
}

FactorialContrast compare_sequence_treatment(
    const RegisteredRepresentativePair& pair,
    const GameState& intervention,
    SequenceTreatment treatment,
    std::shared_ptr<const LearnedModel> model) {
    FactorialContrast result{
        .spec = pair.spec,
        .treatment = treatment,
        .continuation_seed = pair.continuation_seed,
        .relevant_multisets_preserved =
            relevant_multisets_preserved(
                pair.first_state, intervention,
                pair.observer, treatment),
        .treatment_nontrivial =
            treatment_is_nontrivial(
                pair.first_state, intervention,
                pair.observer, treatment),
        .treated_factor_only =
            canonicalize_treated_sequences(
                pair.first_state, treatment,
                pair.observer) ==
            canonicalize_treated_sequences(
                intervention, treatment,
                pair.observer),
        .expected_information_set =
            treatment == SequenceTreatment::ObserverHand
                ? pair.spec.first_information_set
                : pair.spec.second_information_set,
    };
    if (!pair.context.valid ||
        pair.context.decision_player != pair.observer ||
        pair.observer >= pair.first_state.players.size()) {
        return result;
    }
    const std::vector<PriorityAction> baseline_actions =
        legal_priority_actions(
            pair.first_state, pair.observer,
            pair.context.sorcery_actions);
    const std::vector<PriorityAction> intervention_actions =
        legal_priority_actions(
            intervention, pair.observer,
            pair.context.sorcery_actions);
    result.shared_authoritative_actions =
        !baseline_actions.empty() &&
        baseline_actions == pair.authoritative_actions &&
        intervention_actions == baseline_actions;
    if (result.shared_authoritative_actions) {
        result.baseline_information_set =
            fingerprint_for(
                pair.first_state, pair.context,
                baseline_actions);
        result.intervention_information_set =
            fingerprint_for(
                intervention, pair.context,
                intervention_actions);
        result.information_identity_equal =
            result.baseline_information_set ==
                pair.spec.first_information_set &&
            result.intervention_information_set ==
                result.expected_information_set;
    }
    if (!result.relevant_multisets_preserved ||
        !result.treatment_nontrivial ||
        !result.treated_factor_only ||
        !result.information_identity_equal ||
        !result.shared_authoritative_actions) {
        return result;
    }
    result.actions.reserve(baseline_actions.size());
    for (const PriorityAction& action : baseline_actions) {
        result.actions.push_back(compare_action(
            pair.first_state, intervention,
            pair.original_decks, pair.context,
            pair.observer, action, model,
            pair.continuation_seed, treatment));
    }
    return result;
}

GameState make_intervention(
    const RegisteredRepresentativePair& pair,
    SequenceTreatment treatment) {
    GameState result = pair.first_state;
    if (treatment_has_graveyards(treatment)) {
        for (std::size_t player = 0;
             player < result.players.size(); ++player) {
            result.players[player].graveyard =
                pair.second_state.players[player].graveyard;
        }
    }
    if (treatment_has_observer_hand(treatment)) {
        result.players[pair.observer].hand =
            pair.second_state.players[pair.observer].hand;
    }
    return result;
}

std::string contrast_prefix(
    const FactorialContrast& contrast) {
    return contrast.spec.root_stable_id + ":" +
           contrast.spec.first_information_set + ":" +
           contrast.spec.second_information_set + ":" +
           std::string(
               sequence_treatment_name(
                   contrast.treatment));
}

void append_contrast_failures(
    const FactorialContrast& contrast,
    std::vector<std::string>& failures) {
    const std::string prefix =
        contrast_prefix(contrast);
    const auto add_setup =
        [&](bool passed, std::string_view field) {
            if (!passed) {
                failures.push_back(
                    prefix + ":setup:" +
                    std::string(field));
            }
        };
    add_setup(
        contrast.relevant_multisets_preserved,
        "relevant-multisets");
    add_setup(
        contrast.treatment_nontrivial,
        "nontrivial-treatment");
    add_setup(
        contrast.treated_factor_only,
        "treated-factor-only");
    add_setup(
        contrast.information_identity_equal,
        "information-identity");
    add_setup(
        contrast.shared_authoritative_actions,
        "authoritative-actions");
    for (const ActionComparison& action :
         contrast.actions) {
        const std::string action_prefix =
            prefix + ":action:" +
            probes::stable_priority_action_descriptor(
                action.action) + ":";
        const auto add_action =
            [&](bool passed, std::string_view field) {
                if (!passed) {
                    failures.push_back(
                        action_prefix +
                        std::string(field));
                }
            };
        add_action(action.complete, "complete");
        add_action(
            action.disposition_equal, "disposition");
        add_action(
            action.terminal_result_equal,
            "terminal-result");
        add_action(
            action.next_context_equal, "context");
        add_action(
            action.next_legal_actions_equal,
            "legal-actions");
        add_action(
            action.observation_bit_identical,
            "observation");
        add_action(
            action.value_bit_identical, "value");
        add_action(
            action.accounting_equal, "accounting");
        add_action(
            action.canonical_successor_state_equal,
            "successor-state");
    }
}

} // namespace

std::string_view sequence_treatment_name(
    SequenceTreatment treatment) {
    switch (treatment) {
    case SequenceTreatment::Graveyards:
        return "graveyards";
    case SequenceTreatment::ObserverHand:
        return "observer-hand";
    case SequenceTreatment::GraveyardsAndObserverHand:
        return "graveyards-and-observer-hand";
    }
    return "unknown";
}

bool ActionComparison::equivalent() const {
    return complete && disposition_equal &&
           terminal_result_equal && next_context_equal &&
           next_legal_actions_equal &&
           observation_bit_identical &&
           value_bit_identical &&
           canonical_successor_state_equal &&
           accounting_equal;
}

bool FactorialContrast::equivalent() const {
    return relevant_multisets_preserved &&
           treatment_nontrivial &&
           treated_factor_only &&
           information_identity_equal &&
           shared_authoritative_actions &&
           actions.size() == 2 &&
           std::all_of(
               actions.begin(), actions.end(),
               [](const ActionComparison& action) {
                   return action.equivalent();
               });
}

bool PairComparison::equivalent() const {
    return graveyard_order_only &&
           graveyard_order_nontrivial &&
           shared_authoritative_actions &&
           !actions.empty() &&
           std::all_of(
               actions.begin(), actions.end(),
               [](const ActionComparison& action) {
                   return action.equivalent();
               });
}

PairComparison compare_graveyard_order_pair(
    const GameState& first, const GameState& second,
    const std::array<std::vector<CardId>, 2>& original_decks,
    const LearnedDecisionContext& context,
    std::size_t observer,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t continuation_seed) {
    PairComparison result{
        .graveyard_order_only =
            canonicalize_graveyards(first) ==
            canonicalize_graveyards(second),
        .graveyard_order_nontrivial =
            graveyard_order_differs(first, second),
    };
    if (!result.graveyard_order_only ||
        !result.graveyard_order_nontrivial ||
        !context.valid ||
        context.decision_player >= first.players.size() ||
        observer >= first.players.size() ||
        observer != context.decision_player) {
        return result;
    }
    const std::vector<PriorityAction> first_actions =
        legal_priority_actions(
            first, context.decision_player,
            context.sorcery_actions);
    const std::vector<PriorityAction> second_actions =
        legal_priority_actions(
            second, context.decision_player,
            context.sorcery_actions);
    result.shared_authoritative_actions =
        !first_actions.empty() &&
        first_actions == second_actions;
    if (!result.shared_authoritative_actions) {
        return result;
    }
    result.actions.reserve(first_actions.size());
    for (const PriorityAction& action : first_actions) {
        result.actions.push_back(compare_action(
            first, second, original_decks, context, observer,
            action, model, continuation_seed));
    }
    return result;
}

bool DirectControlReport::passed() const {
    return blue_counter.equivalent() &&
           buried_white.equivalent() &&
           blue_counter.actions.size() == 2 &&
           buried_white.actions.size() == 4 &&
           life_perturbation_detected;
}

DirectControlReport compare_direct_controls(
    std::shared_ptr<const LearnedModel> model) {
    constexpr std::uint64_t kControlSeed = 577215;
    const std::vector<probes::DecisionProbe> controls =
        probes::make_counter_composition_controls_v1();
    const std::vector<std::string> validation_errors =
        probes::validate_counter_composition_controls_v1(
            controls);
    if (!validation_errors.empty() || controls.size() != 2) {
        throw std::logic_error(
            "FR1 Blue direct-control corpus drifted");
    }
    const probes::DecisionProbe& blue = controls[1];
    const GameState world = sample_determinization(
        blue.state, blue.original_decks, blue.root_player,
        kControlSeed);
    const probes::Dc1CanonicalSettlement first =
        probes::settle_dc1_priority_candidate(
            blue, world,
            candidate_index(
                blue, "counter-same-air-elemental"));
    const probes::Dc1CanonicalSettlement second =
        probes::settle_dc1_priority_candidate(
            blue, world,
            candidate_index(
                blue, "counter-opponent-counterspell"));
    if (!first.window_ended || !second.window_ended ||
        first.settled_state.players[1].graveyard !=
            std::vector<CardId>{
                CardId::AirElemental,
                CardId::Counterspell,
            } ||
        second.settled_state.players[1].graveyard !=
            std::vector<CardId>{
                CardId::Counterspell,
                CardId::AirElemental,
            }) {
        throw std::logic_error(
            "FR1 Blue direct-control settlement drifted");
    }
    GameState blue_first = first.settled_state;
    const std::size_t blue_owner =
        blue_first.active_player;
    const auto mox = std::find(
        blue_first.players[blue_owner].library.begin(),
        blue_first.players[blue_owner].library.end(),
        CardId::MoxSapphire);
    if (mox ==
        blue_first.players[blue_owner].library.end()) {
        throw std::logic_error(
            "FR1 Blue direct control has no hidden Mox");
    }
    blue_first.players[blue_owner].hand.push_back(*mox);
    blue_first.players[blue_owner].library.erase(mox);
    GameState blue_second = blue_first;
    for (std::size_t player = 0;
         player < blue_second.players.size(); ++player) {
        blue_second.players[player].graveyard =
            second.settled_state.players[player].graveyard;
    }
    const LearnedDecisionContext blue_context{
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player = blue_owner,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };

    GameState white_first = buried_white_state();
    GameState white_second = white_first;
    std::swap(
        white_second.players[0].graveyard[0],
        white_second.players[0].graveyard[1]);
    const LearnedDecisionContext white_context{
        .valid = true,
        .phase = TurnPhase::FirstMain,
        .decision_player = 0,
        .consecutive_passes = 0,
        .sorcery_actions = true,
    };
    const std::array<std::vector<CardId>, 2> white_decks{
        white_control_deck(),
        blue_deck(),
    };

    DirectControlReport report{
        .blue_counter = compare_graveyard_order_pair(
            blue_first, blue_second, blue.original_decks,
            blue_context, blue_owner, model, kControlSeed),
        .buried_white = compare_graveyard_order_pair(
            white_first, white_second, white_decks,
            white_context, 0, model, kControlSeed),
    };
    --white_second.players[0].life;
    const PairComparison life_control =
        compare_graveyard_order_pair(
            white_first, white_second, white_decks,
            white_context, 0, std::move(model),
            kControlSeed);
    report.life_perturbation_detected =
        !life_control.graveyard_order_only &&
        !life_control.equivalent();
    return report;
}

const std::vector<RegisteredPairSpec>&
registered_pair_specs() {
    static const std::vector<RegisteredPairSpec> specs = {
        {std::string(kBlueRoot), "05d6f5ef5dd51806cee7ad6d627932ddc67af397a86dc8d9a1485ff32d903c3a", "6f3472fc0b8c6b0cb5cbdb102ecdb01cdbcfde7959ff115754e1dccc14e33630", 2},
        {std::string(kBlueRoot), "09f14aef48eb508691eaadb5d9d11712133c3061eb3a3bb7acadf96985219e4a", "28b53a858718e6c5e4e797e374fa56bd7061e20dfebf6cec2c101d61ece5116f", 4},
        {std::string(kBlueRoot), "0e19646f6e8d4fadaf39ea655be8d174f53e93a87bfd78fd72da09b1f8bf2525", "88dd621008835636dd6e5564d235b2a4980d0327ac9bfe8e18be237cd7a5fcc3", 2},
        {std::string(kBlueRoot), "0e606dac28320b672274a33532c7b110ea8cf1b0c4c8f4d57e0561f9db11cc01", "a8ddda3bef76fc114988dd55e86ddc1a602eed848636fa80b944a5837ae15d12", 4},
        {std::string(kBlueRoot), "0fa30d7237b5e8d98f9442d37c0f3afd7391d3582e6e99c7d48b4aa3b21b81be", "e5d880afdcc228cd06cbbeaa0ff2237b18925373f5687a346b135d78d3ac7a16", 2},
        {std::string(kBlueRoot), "129ca0820cec2e2f9baddbb7c6dcd878607e0472025879028fd49d2b05a29fb2", "85a8c82088312f9a0fcbe60a24037772381171a548451937fadfecfe0d5a544e", 4},
        {std::string(kBlueRoot), "1a3757d33a95a2cb639512fe1ee698520c029085c2effd68af094a8aa1a8537f", "e800c038d0eeaf5b282ebc3bc47424f9f83bd6499e9478eb262370b6a1f9ef3e", 5},
        {std::string(kBlueRoot), "1f569a32b4dc5b98abec6503656a5511488c8c1527349dc1ccba708a2242a24e", "4dc1331e0af52b8b7e14889d839521b677292951ca08b82f71591f072aa2e434", 17},
        {std::string(kBlueRoot), "206521c90190ad978b54068f05a9b6cea6dcb0c9d05f8bd02357905dc1ec3923", "91a02a8c4d7a8b6160514dba881b5b6870e33ba55f270100c584bab7758732c7", 4},
        {std::string(kBlueRoot), "20f8287abc84bee40f5491e850c075559644849e3f6a18fec15fe666db54ef54", "91e7e6cb89c47532fb6a3a224fa4710e1fd8b302298ecf88358e8e2c94a0511b", 2},
        {std::string(kBlueRoot), "210d5b8f744a4e5bab0b0f4a027d2c0ad403db7a6e960a88eab07328fdd44cfb", "aca1437ffb498c609f32abc63d396b62e102b6cbff76cbea910342c026151931", 2},
        {std::string(kBlueRoot), "28180c7ad703689da58f6bd992928bc6265bdf088ffdaf0de1fd134d6e16c529", "fbbdb30687ca4fed8940d9c3a90ca608a95d85c6d25f6b30b80872e67134289e", 15},
        {std::string(kBlueRoot), "29ee71aa5c5d177dfdb38d4f6cff74a059b2997a053fe32d7c7dd14a5e7ad531", "9c77b5392048a0d67a84931b3c7021da4f91625e7157b7d0d9360cd2068a210b", 2},
        {std::string(kBlueRoot), "2cac2ba430baef3a51c0144db18c47db0408dafd7c68cf4e32968b552edc7e52", "b87687f979692d375a4642480c70afc4950cf39b1b4c1b72b5f87f49bc64c3d2", 2},
        {std::string(kBlueRoot), "33038d38642473c25b9fec1972c539dec2cfaf208036782b8405b2cfb05d2940", "a0940356b3e2d3e7cd3e918a60c7934444850f6a25f9b54843daa69c12109d9c", 16},
        {std::string(kBlueRoot), "35048ab8f877703ffa95b62f609b13bc9f69cdf219c6238ffbb80cb26b134a51", "cfa6f82d4a98485bf7ba0afa4c0000c203d166a214424c044c751995d367fce8", 2},
        {std::string(kBlueRoot), "3ff0b44fdb01578c87836d68e30628fab85341c7eb6e43986604d983fb709055", "f59fde055dfa0350d5a4df8551cb754e8ddfda7ff2c79b85b8ea0e5e79cba511", 5},
        {std::string(kBlueRoot), "42bcb59329b913a8296c8d5ebdce9a280ed930dc00fdae9befa20e15fdefbb76", "6cfbbddfde8283c086a800798fa43b176424f7a8ba2c63addec7b5b61037f734", 2},
        {std::string(kBlueRoot), "43d3f4c73f4f27dc24e2b7c3dfad1a8750cc5c06b5c343ace28bca1745068e47", "f2a81e462f178e866e1cd43053ec62b15d8038e468d41f12ecc90c937d236a95", 2},
        {std::string(kBlueRoot), "4a270019311a645bd46d87ffb5af491212faa994a28bfaf62e6bf7cb8e876ce4", "d3414856b3f3e37cf730afd1aa9e97250b3469ef3cdcaca21c567bde489cfe82", 4},
        {std::string(kBlueRoot), "4a518e79641dc1e55835cfdea92c6d717db976cfbb21d595b42e23cb78d00622", "718b1012c12310316a601d04f6e664404aded83d8b53230b9da098f0dd38e7ac", 3},
        {std::string(kBlueRoot), "4e80d026400c32ae5446ad9620d1cc80a482e40d674b37f78d3d7cb27f80d26d", "f9b2f5cc7fe99637f0de12644daabeb00ca7d17d8d46c950f4219bbb76466176", 5},
        {std::string(kBlueRoot), "54dcb297be5403780dc72344092b618402cd3cbc673e446633c9c2b26625806f", "a243bdf51f59d8783328f6d11da8408e5c029da00dbed4be4f1b8b51d91307c7", 5},
        {std::string(kBlueRoot), "5693a4a9bb098a2abfe12264a4635891dc4d50b2483b4841ec95b33417ba39a2", "5f4e2de4d104dcb138261c7b5821a9659425a05e9ff6d4977ae8ac7521509414", 3},
        {std::string(kBlueRoot), "5988686e2eb6c326f095a27ec9a58879282ad0c95d7ee0c53b64cdd7fb85986d", "cc9bbfaa17b23c8d6d44054caac0184ef8a2a5277c0667c3d2d8ed61ade4689b", 2},
        {std::string(kBlueRoot), "7acdb4608b829a1bacf4438e91c86c98d855c3cd30bd6d5c340e7262921be930", "f660c04fb65875e97568c98a0c0cb121d698a0b98e91f447eafbc00e20505600", 2},
        {std::string(kBlueRoot), "7be5f054c30383923aa9fc30d45d147e556ce07156d459c46f1ae8a011b418d6", "a7a9e66b5be90954bd74972a79a78a009c49fc3b441751b99d4a3e837360b202", 3},
        {std::string(kBlueRoot), "82c756b293d8738d68559a2dfe94c1f97c86fc010fca098e25cb1c3ba158689d", "b377e4f61d95971546637486e45584cd32089cca0285f9b45afc2f7da9a662e9", 2},
        {std::string(kBlueRoot), "8eb336a9bcc20a18ce6cfbdbe205abfb98beb5ede4992f8743937f2d168579f4", "dfd2cae20e73e750430cbc8756ae24a362aa7e1a8f92ff6f2c7ef7fda38dc510", 5},
        {std::string(kBlueRoot), "9754e34a56c57f02516dea7fc3cc2f342a579867c18538ccbf5f1bc1ea9b65e7", "987b48b596ec3cb9a769c9fcbb7bdf7febd57f2e4c206873700eb3e11ead1a25", 4},
        {std::string(kBlueRoot), "98eb68c8dbabc05077835736260fc3ed47820931e880681e566e048642c27bd2", "ae7271ce306c3531a4c13aa814ff45f3b1e8d374700fcd118e2edf7472d65f69", 7},
        {std::string(kBlueRoot), "a695416a53fe7b72bef209a7fa020673943029d60a634731c3b1d01d8c7f7bae", "cc554ec7dabec1a279503902f0a157a4a374d44b55fc5027ce1b1e5d2e8de3b9", 4},
        {std::string(kBlueRoot), "aa2a5e6ac51a06930fee3e333c4320f4a12d6228714943632a64c58a00a63db8", "c370bc6339c023266218a4f27d3abfa0e0abca0f13f8baa6caa21ac70c6780bc", 5},
        {std::string(kBlueRoot), "b606c4274c720113da0f65e76dd6348cab37a253a765f4b8bb9963dc329e6b39", "eb2e2535d2559f975188b3c15fb25fd90ea802ed71b51a0518f6587b22a5ca53", 2},
        {std::string(kBlueRoot), "d6b43740d4d358d0cfada5df67e13820f56fdbe20817700cd0ec283154085212", "f4d10f9c19d31fbc146109db68d2d6233714e0aa9f5201e47d07dbcca5a578ba", 5},
        {std::string(kBlueRoot), "da765763534205cbabf3ff70beb908b6202b955da5e56f258772d022488ae954", "fdfb4b31963bd6314984b362d9c4ee9e3242e00241e092cbcb5bea9ee20776c3", 2},
        {std::string(kBlueRoot), "dc9d9785b43f6240c04b775c771a3b79f540a9b13bf4502c2058d69896d8c105", "f554ede9812eabd925a650dbcd7fba9d304c6104d501ebef0fbd2cc24ab4fee3", 4},
        {std::string(kBlueRoot), "e03ce2e21578d057cfad96fcddd1e1f0c89ad0f533b4c052e611afa021511abf", "f0ec1d11d1da3e81e2b2b9ed6fc2a2d79d4f749098729d83866d5a2051fd5ae9", 2},
        {std::string(kWhiteRoot), "13373b5ab30f0eb1832601a9cacd66a95bb846e4f06b9cffb73445eeecd4c5bb", "9cbc2446029f4738a1ed05baac7fa3230d6015056c047a7264aa83ee2e7dbb55", 2},
        {std::string(kWhiteRoot), "37314baad7d9941ec619e1fe48e211e61581a5c416e85405ef5d81112704ba66", "bf17dc395deedc0e40da5a0864766c7a64f3c1847f821ed03cd9bb20c0d8c1f2", 2},
        {std::string(kWhiteRoot), "435fc94759197bc8204b1d652a1c0ce6db33984083b15e517bbb7790657d8e61", "f16da3e2eeef23a8a45dc221245353987095c6e125e1aadef02ee49b3fe7992e", 2},
        {std::string(kWhiteRoot), "45112f2bb4bacd41aa936dd158628594f319096bbcae13e17bb55e991d1f56d1", "c78c9a1c4cb4660b89a499229b2741c79fd48448a8e9a4201b9d7d70bfeb8ecf", 4},
        {std::string(kWhiteRoot), "520832b961bda1af51034d20b5b35c63c6f7a3d5c688dd60eb55c73988dbd7b2", "a3ebe4d0768400b927e3394cb3dbf219689ab93f472008f28a7d2a098aa1e1e7", 2},
        {std::string(kWhiteRoot), "5d4cdf24b01f6a5179e4c21b98c4fd3b5d921c3115b184a0cebbb9d410185176", "e515cc9a6c2b9492ae9ec32be6c80970f7bce395a54e22591b1d75ed159cc275", 2},
    };
    return specs;
}

const std::vector<RegisteredPairSpec>&
registered_factorial_pair_specs() {
    static const std::vector<RegisteredPairSpec> specs = [] {
        const auto& registered = registered_pair_specs();
        return std::vector<RegisteredPairSpec>{
            registered.at(38),
            registered.at(39),
            registered.at(40),
            registered.at(42),
            registered.at(43),
        };
    }();
    return specs;
}

bool RegisteredAnatomyReport::exact_registered_anatomy() const {
    return bounded_root_macros == 448 &&
           registered_pairs == 44 &&
           blue_pairs == 38 &&
           white_pairs == 6 &&
           legacy_collision_rows == 177 &&
           blue_collision_rows == 163 &&
           white_collision_rows == 14 &&
           registered_row_identity_sha256 ==
               kRegisteredRowIdentitySha256;
}

bool RegisteredAnatomyReport::
    exact_registered_rejection() const {
    if (!exact_registered_anatomy() ||
        incomplete_root_macros != 0 ||
        reconstructed_pairs != 44 ||
        graveyard_only_pairs != 39 ||
        graveyard_only_rows != 167 ||
        additional_public_difference_pairs != 5 ||
        additional_public_difference_rows != 10 ||
        equivalent_pairs != 39 ||
        pairs.size() != 39 ||
        additional_public_representatives.size() != 5 ||
        reconstruction_failures.size() != 5 ||
        std::any_of(
            pairs.begin(), pairs.end(),
            [](const RegisteredPairResult& pair) {
                return !pair.comparison.equivalent();
            })) {
        return false;
    }
    std::vector<RegisteredPairSpec> retained_specs;
    std::vector<std::string> expected_failures;
    retained_specs.reserve(
        additional_public_representatives.size());
    expected_failures.reserve(
        additional_public_representatives.size());
    const std::vector<PriorityAction> expected_actions{
        PriorityAction::pass(),
        PriorityAction::cast_artifact(
            CardId::MoxSapphire),
    };
    constexpr std::array<std::size_t, 5>
        expected_root_worlds{0, 24, 5, 17, 42};
    constexpr std::array<std::uint64_t, 5>
        expected_continuation_seeds{
            3490941223041417199ULL,
            13999545523412929041ULL,
            5716191040556191100ULL,
            13649792070827152751ULL,
            11293163517197892497ULL,
        };
    for (std::size_t index = 0;
         index <
         additional_public_representatives.size();
         ++index) {
        const RegisteredRepresentativePair& pair =
            additional_public_representatives[index];
        retained_specs.push_back(pair.spec);
        if (pair.authoritative_actions != expected_actions ||
            pair.context.decision_player != pair.observer ||
            pair.first_root_world !=
                expected_root_worlds[index] ||
            pair.first_root_action != "mill-self" ||
            pair.continuation_seed !=
                expected_continuation_seeds[index]) {
            return false;
        }
        expected_failures.push_back(
            pair.spec.root_stable_id +
            ":non-graveyard-public-difference-"
            "observer-hand-order-only:" +
            pair.spec.first_information_set + ":" +
            pair.spec.second_information_set);
    }
    return retained_specs ==
               registered_factorial_pair_specs() &&
           reconstruction_failures == expected_failures &&
           !passed();
}

bool RegisteredAnatomyReport::passed() const {
    return exact_registered_anatomy() &&
           incomplete_root_macros == 0 &&
           reconstructed_pairs == registered_pairs &&
           graveyard_only_pairs == registered_pairs &&
           graveyard_only_rows == legacy_collision_rows &&
           additional_public_difference_pairs == 0 &&
           additional_public_difference_rows == 0 &&
           equivalent_pairs == registered_pairs &&
           pairs.size() == registered_pairs;
}

bool FactorialReport::infrastructure_valid() const {
    if (!anatomy.exact_registered_rejection() ||
        eligible_pairs != 5 ||
        contrasts != 15 ||
        action_comparisons != 30 ||
        results.size() != 15 ||
        wrong_mask_controls_detected != 4 ||
        wrong_mask_controls.size() != 4 ||
        !life_perturbation_detected) {
        return false;
    }
    const std::vector<PriorityAction> expected_actions{
        PriorityAction::pass(),
        PriorityAction::cast_artifact(
            CardId::MoxSapphire),
    };
    constexpr std::array<SequenceTreatment, 3>
        expected_treatments{
            SequenceTreatment::Graveyards,
            SequenceTreatment::ObserverHand,
            SequenceTreatment::
                GraveyardsAndObserverHand,
        };
    for (std::size_t pair_index = 0;
         pair_index <
         anatomy.additional_public_representatives.size();
         ++pair_index) {
        const RegisteredRepresentativePair&
            representative =
                anatomy.additional_public_representatives[
                    pair_index];
        for (std::size_t treatment_index = 0;
             treatment_index <
             expected_treatments.size();
             ++treatment_index) {
            const FactorialContrast& result =
                results[
                    pair_index *
                        expected_treatments.size() +
                    treatment_index];
            const SequenceTreatment treatment =
                expected_treatments[treatment_index];
            const std::string& expected_intervention =
                treatment == SequenceTreatment::ObserverHand
                    ? representative.spec
                          .first_information_set
                    : representative.spec
                          .second_information_set;
            if (result.spec != representative.spec ||
                result.treatment != treatment ||
                result.continuation_seed !=
                    representative.continuation_seed ||
                !result.relevant_multisets_preserved ||
                !result.treatment_nontrivial ||
                !result.treated_factor_only ||
                !result.information_identity_equal ||
                !result.shared_authoritative_actions ||
                result.baseline_information_set !=
                    representative.spec
                        .first_information_set ||
                result.expected_information_set !=
                    expected_intervention ||
                result.intervention_information_set !=
                    expected_intervention ||
                result.actions.size() !=
                    expected_actions.size()) {
                return false;
            }
            for (std::size_t action_index = 0;
                 action_index < expected_actions.size();
                 ++action_index) {
                if (result.actions[action_index].action !=
                        expected_actions[action_index] ||
                    !result.actions[action_index].complete) {
                    return false;
                }
            }
        }
    }

    const RegisteredRepresentativePair& first_pair =
        anatomy.additional_public_representatives.front();
    constexpr std::array<SequenceTreatment, 4>
        expected_wrong_masks{
            SequenceTreatment::ObserverHand,
            SequenceTreatment::Graveyards,
            SequenceTreatment::Graveyards,
            SequenceTreatment::ObserverHand,
        };
    constexpr std::array<bool, 4>
        expected_nontrivial{false, false, true, true};
    constexpr std::array<bool, 4>
        expected_identity_equal{
            false, false, true, false};
    for (std::size_t index = 0;
         index < wrong_mask_controls.size(); ++index) {
        const FactorialContrast& control =
            wrong_mask_controls[index];
        const std::string& expected_intervention =
            index == 1
                ? first_pair.spec.first_information_set
                : first_pair.spec.second_information_set;
        const std::string& expected_identity =
            expected_wrong_masks[index] ==
                    SequenceTreatment::ObserverHand
                ? first_pair.spec.first_information_set
                : first_pair.spec.second_information_set;
        if (control.spec != first_pair.spec ||
            control.treatment !=
                expected_wrong_masks[index] ||
            control.continuation_seed !=
                first_pair.continuation_seed ||
            !control.relevant_multisets_preserved ||
            control.treatment_nontrivial !=
                expected_nontrivial[index] ||
            control.treated_factor_only ||
            control.baseline_information_set !=
                first_pair.spec.first_information_set ||
            control.expected_information_set !=
                expected_identity ||
            control.intervention_information_set !=
                expected_intervention ||
            control.information_identity_equal !=
                expected_identity_equal[index] ||
            !control.shared_authoritative_actions ||
            !control.actions.empty() ||
            control.equivalent()) {
            return false;
        }
    }
    return life_control.spec == first_pair.spec &&
           life_control.treatment ==
               SequenceTreatment::
                   GraveyardsAndObserverHand &&
           life_control.continuation_seed ==
               first_pair.continuation_seed &&
           life_control.relevant_multisets_preserved &&
           life_control.treatment_nontrivial &&
           !life_control.treated_factor_only &&
           !life_control.information_identity_equal &&
           life_control.shared_authoritative_actions &&
           life_control.actions.empty() &&
           !life_control.equivalent();
}

bool FactorialReport::passed() const {
    return infrastructure_valid() &&
           graveyard_contrasts_equal == 5 &&
           observer_hand_contrasts_equal == 5 &&
           combined_contrasts_equal == 5 &&
           failures.empty() &&
           std::all_of(
               results.begin(), results.end(),
               [](const FactorialContrast& result) {
                   return result.equivalent();
               });
}

namespace {

inline constexpr std::string_view kResidualCatalogSchema =
    "old-school-fr3-registered-residual-catalog-v1\n";

bool valid_sha256(std::string_view digest) {
    return digest.size() == 64 &&
           std::all_of(
               digest.begin(), digest.end(),
               [](char digit) {
                   return (digit >= '0' && digit <= '9') ||
                          (digit >= 'a' && digit <= 'f');
               });
}

bool catalog_field_is_safe(std::string_view field) {
    return !field.empty() &&
           field.find_first_of("\t\r\n") ==
               std::string_view::npos;
}

std::string residual_catalog_sha256(
    const std::vector<ResidualCatalogRow>& rows) {
    std::string bytes(kResidualCatalogSchema);
    for (const ResidualCatalogRow& row : rows) {
        if (!catalog_field_is_safe(row.root_stable_id) ||
            !valid_sha256(row.first_information_set) ||
            !valid_sha256(row.second_information_set) ||
            !valid_sha256(
                row.first_quotient_information_set) ||
            !valid_sha256(
                row.second_quotient_information_set) ||
            !catalog_field_is_safe(row.action_descriptor) ||
            !valid_sha256(
                row.first_legacy_consequence) ||
            !valid_sha256(
                row.second_legacy_consequence) ||
            !valid_sha256(
                row.first_quotient_consequence) ||
            !valid_sha256(
                row.second_quotient_consequence)) {
            throw std::invalid_argument(
                "FR3 catalog contains an invalid field");
        }
        bytes += row.root_stable_id;
        bytes += '\t';
        bytes += row.first_information_set;
        bytes += '\t';
        bytes += row.second_information_set;
        bytes += '\t';
        bytes += row.first_quotient_information_set;
        bytes += '\t';
        bytes += row.action_descriptor;
        bytes += '\t';
        bytes += row.first_legacy_consequence;
        bytes += '\t';
        bytes += row.second_legacy_consequence;
        bytes += '\t';
        bytes += row.first_quotient_consequence;
        bytes += '\n';
    }
    return artifact_integrity::sha256_string(bytes);
}

bool residual_row_less(
    const ResidualCatalogRow& first,
    const ResidualCatalogRow& second) {
    return std::tie(
               first.root_stable_id,
               first.first_information_set,
               first.second_information_set,
               first.action_descriptor) <
           std::tie(
               second.root_stable_id,
               second.first_information_set,
               second.second_information_set,
               second.action_descriptor);
}

bool exact_catalog_order(
    const std::vector<ResidualCatalogRow>& rows) {
    return std::is_sorted(
               rows.begin(), rows.end(),
               residual_row_less) &&
           std::adjacent_find(
               rows.begin(), rows.end(),
               [](const ResidualCatalogRow& first,
                  const ResidualCatalogRow& second) {
                   return !residual_row_less(first, second) &&
                          !residual_row_less(second, first);
               }) == rows.end();
}

struct ProjectionFingerprints {
    std::string information_set;
    std::string leaf_consequence;
    std::string priority_consequence;

    bool operator==(
        const ProjectionFingerprints&) const = default;
};

ProjectionFingerprints quotient_fingerprints(
    const GameState& state,
    const RegisteredRepresentativePair& pair,
    const PriorityAction& action) {
    return {
        .information_set =
            projection::
                graveyard_quotient_information_set_sha256(
                    state, pair.context,
                    pair.authoritative_actions),
        .leaf_consequence =
            projection::
                graveyard_quotient_leaf_consequence_sha256(
                    state, pair.observer, pair.context),
        .priority_consequence =
            projection::
                graveyard_quotient_priority_consequence_sha256(
                    state, pair.observer, pair.context,
                    action),
    };
}

bool mutate_public_graveyard_multiset(GameState& state) {
    for (PlayerState& player : state.players) {
        if (player.graveyard.empty()) {
            continue;
        }
        const std::size_t original =
            static_cast<std::size_t>(
                player.graveyard.front());
        player.graveyard.front() = static_cast<CardId>(
            (original + 1) % kCardCount);
        return true;
    }
    return false;
}

bool repartition_hidden_opponent(
    GameState& state, std::size_t observer) {
    if (observer >= state.players.size()) {
        return false;
    }
    PlayerState& opponent =
        state.players[1 - observer];
    for (CardId& hidden_hand_card : opponent.hand) {
        const auto library_card = std::find_if(
            opponent.library.begin(),
            opponent.library.end(),
            [&](CardId card) {
                return card != hidden_hand_card;
            });
        if (library_card != opponent.library.end()) {
            std::swap(hidden_hand_card, *library_card);
            return true;
        }
    }
    return false;
}

void evaluate_projection_controls(
    const std::vector<RegisteredRepresentativePair>& pairs,
    ResidualConflictReport& report) {
    for (const RegisteredRepresentativePair& pair : pairs) {
        if (pair.authoritative_actions.empty()) {
            continue;
        }
        const PriorityAction& action =
            pair.authoritative_actions.front();
        const ProjectionFingerprints baseline =
            quotient_fingerprints(
                pair.first_state, pair, action);

        if (!report.changed_graveyard_multiset_distinct) {
            GameState changed = pair.first_state;
            if (mutate_public_graveyard_multiset(changed)) {
                const ProjectionFingerprints mutation =
                    quotient_fingerprints(
                        changed, pair, action);
                report.changed_graveyard_multiset_distinct =
                    baseline.information_set !=
                        mutation.information_set &&
                    baseline.leaf_consequence !=
                        mutation.leaf_consequence &&
                    baseline.priority_consequence !=
                        mutation.priority_consequence;
            }
        }

        if (!report.life_total_distinct) {
            GameState changed = pair.first_state;
            --changed.players[pair.observer].life;
            const ProjectionFingerprints mutation =
                quotient_fingerprints(
                    changed, pair, action);
            report.life_total_distinct =
                baseline.information_set !=
                    mutation.information_set &&
                baseline.leaf_consequence !=
                    mutation.leaf_consequence &&
                baseline.priority_consequence !=
                    mutation.priority_consequence;
        }

        if (!report.hidden_repartition_aliased) {
            GameState changed = pair.first_state;
            if (repartition_hidden_opponent(
                    changed, pair.observer)) {
                report.hidden_repartition_aliased =
                    quotient_fingerprints(
                        changed, pair, action) ==
                    baseline;
            }
        }
        if (report.changed_graveyard_multiset_distinct &&
            report.life_total_distinct &&
            report.hidden_repartition_aliased) {
            return;
        }
    }
}

} // namespace

bool ResidualConflictReport::infrastructure_valid() const {
    if (!prerequisite.passed() ||
        !prerequisite.anatomy
             .exact_registered_rejection() ||
        controlled_pairs != 44 ||
        paired_actions != 177 ||
        source_state_action_instances != 354 ||
        exact_legacy_identity_pairs != 44 ||
        legacy_leaf_conflict_pairs != 44 ||
        legacy_consequence_conflicts != 177 ||
        catalog_rows.size() != 177 ||
        !changed_graveyard_multiset_distinct ||
        !life_total_distinct ||
        !hidden_repartition_aliased ||
        !infrastructure_failures.empty() ||
        !exact_catalog_order(catalog_rows) ||
        catalog_sha256 !=
            residual_catalog_sha256(catalog_rows)) {
        return false;
    }
    std::size_t observed_legacy_conflicts = 0;
    std::size_t observed_residual_conflicts = 0;
    std::size_t observed_feature_identities = 0;
    for (const ResidualCatalogRow& row : catalog_rows) {
        if (row.action_descriptor !=
                probes::stable_priority_action_descriptor(
                    row.action) ||
            row.first_information_set ==
                row.second_information_set) {
            return false;
        }
        observed_legacy_conflicts +=
            row.first_legacy_consequence !=
            row.second_legacy_consequence;
        observed_residual_conflicts +=
            row.first_quotient_consequence !=
            row.second_quotient_consequence;
        observed_feature_identities +=
            row.policy_features_bit_identical;
    }
    if (observed_legacy_conflicts !=
            legacy_consequence_conflicts ||
        observed_residual_conflicts !=
            residual_quotient_conflicts ||
        observed_feature_identities !=
            policy_feature_rows_bit_identical) {
        return false;
    }
    std::size_t row_offset = 0;
    for (const RegisteredPairSpec& spec :
         registered_pair_specs()) {
        if (row_offset + spec.legacy_collision_rows >
            catalog_rows.size()) {
            return false;
        }
        const std::string& first_quotient =
            catalog_rows[row_offset]
                .first_quotient_information_set;
        const std::string& second_quotient =
            catalog_rows[row_offset]
                .second_quotient_information_set;
        for (std::size_t action_index = 0;
             action_index < spec.legacy_collision_rows;
             ++action_index) {
            const ResidualCatalogRow& row =
                catalog_rows[row_offset + action_index];
            if (row.root_stable_id !=
                    spec.root_stable_id ||
                row.first_information_set !=
                    spec.first_information_set ||
                row.second_information_set !=
                    spec.second_information_set ||
                row.first_quotient_information_set !=
                    first_quotient ||
                row.second_quotient_information_set !=
                    second_quotient) {
                return false;
            }
        }
        row_offset += spec.legacy_collision_rows;
    }
    return row_offset == catalog_rows.size();
}

bool ResidualConflictReport::passed() const {
    return infrastructure_valid() &&
           quotient_information_pairs_equal == 44 &&
           policy_feature_rows_bit_identical == 177 &&
           quotient_leaf_conflict_pairs == 0 &&
           residual_quotient_conflicts == 0 &&
           std::all_of(
               catalog_rows.begin(), catalog_rows.end(),
               [](const ResidualCatalogRow& row) {
                   return row
                              .first_quotient_information_set ==
                              row
                                  .second_quotient_information_set &&
                          row
                              .first_quotient_consequence ==
                              row
                                  .second_quotient_consequence &&
                          row.policy_features_bit_identical;
               });
}

RegisteredAnatomyReport reconstruct_registered_anatomy_impl(
    std::shared_ptr<const LearnedModel> model,
    std::vector<RegisteredRepresentativePair>*
        controlled_pairs = nullptr) {
    if (!model) {
        throw std::invalid_argument(
            "FR1 requires a Value model");
    }
    std::vector<probes::DecisionProbe> blue_controls =
        probes::make_counter_composition_controls_v1();
    std::vector<probes::DecisionProbe> development =
        probes::make_probe_dev_v3();
    const probes::DecisionProbe& blue =
        find_root(blue_controls, kBlueRoot);
    const probes::DecisionProbe& white =
        find_root(development, kWhiteRoot);

    RegisteredAnatomyReport report;
    const auto& specs = registered_pair_specs();
    report.registered_pairs = specs.size();
    for (const RegisteredPairSpec& spec : specs) {
        report.legacy_collision_rows +=
            spec.legacy_collision_rows;
        if (spec.root_stable_id == kBlueRoot) {
            ++report.blue_pairs;
            report.blue_collision_rows +=
                spec.legacy_collision_rows;
        } else if (spec.root_stable_id == kWhiteRoot) {
            ++report.white_pairs;
            report.white_collision_rows +=
                spec.legacy_collision_rows;
        } else {
            throw std::logic_error(
                "FR1 registered an unknown collision root");
        }
    }
    if (report.registered_pairs != 44 ||
        report.blue_pairs != 38 ||
        report.white_pairs != 6 ||
        report.legacy_collision_rows != 177 ||
        report.blue_collision_rows != 163 ||
        report.white_collision_rows != 14) {
        throw std::logic_error(
            "FR1 registered collision anatomy drifted");
    }

    std::map<std::string, ReconstructedStates> by_root;
    reconstruct_root(
        blue, model, by_root[blue.stable_id], report);
    reconstruct_root(
        white, model, by_root[white.stable_id], report);
    if (report.incomplete_root_macros != 0) {
        return report;
    }

    const auto probe_for =
        [&](std::string_view stable_id)
            -> const probes::DecisionProbe& {
        return stable_id == kBlueRoot ? blue : white;
    };
    report.pairs.reserve(specs.size());
    if (controlled_pairs != nullptr) {
        controlled_pairs->clear();
        controlled_pairs->reserve(specs.size());
    }
    std::vector<std::string> registered_row_identities;
    registered_row_identities.reserve(
        report.legacy_collision_rows);
    for (const RegisteredPairSpec& spec : specs) {
        const ReconstructedStates& states =
            by_root.at(spec.root_stable_id);
        const auto first =
            states.find(spec.first_information_set);
        const auto second =
            states.find(spec.second_information_set);
        if (first == states.end() ||
            second == states.end()) {
            report.reconstruction_failures.push_back(
                spec.root_stable_id + ":missing:" +
                spec.first_information_set + ":" +
                spec.second_information_set);
            continue;
        }
        const ReconstructedState* left =
            canonical_representative(first->second);
        const ReconstructedState* right =
            canonical_representative(second->second);
        if (left == nullptr || right == nullptr) {
            report.reconstruction_failures.push_back(
                spec.root_stable_id +
                ":empty-representative:" +
                spec.first_information_set + ":" +
                spec.second_information_set);
            continue;
        }
        if (fingerprint_for(
                left->state, left->context,
                left->legal_actions) !=
                spec.first_information_set ||
            fingerprint_for(
                right->state, right->context,
                right->legal_actions) !=
                spec.second_information_set) {
            throw std::logic_error(
                spec.root_stable_id +
                ": FR1 reconstructed pair identity drifted");
        }
        if (left->legal_actions.size() !=
                spec.legacy_collision_rows ||
            right->legal_actions != left->legal_actions ||
            right->context != left->context ||
            right->owner != left->owner) {
            throw std::logic_error(
                spec.root_stable_id +
                ": FR1 registered row/action context does not "
                "match the paired authoritative census");
        }
        ++report.reconstructed_pairs;
        for (const PriorityAction& action :
             left->legal_actions) {
            const std::string descriptor =
                probes::stable_priority_action_descriptor(
                    action);
            registered_row_identities.push_back(
                "feature/successor/" +
                spec.root_stable_id + "/" +
                spec.first_information_set + "/" +
                descriptor + "\t" +
                "feature/successor/" +
                spec.root_stable_id + "/" +
                spec.second_information_set + "/" +
                descriptor + "\n");
        }
        const bool graveyard_order_only =
            same_public_except_graveyard_order(
                *left, *right);
        const std::string difference =
            graveyard_order_only
                ? std::string()
                : public_difference(*left, *right);
        if (!graveyard_order_only) {
            ++report.additional_public_difference_pairs;
            report.additional_public_difference_rows +=
                spec.legacy_collision_rows;
            report.reconstruction_failures.push_back(
                spec.root_stable_id +
                ":non-graveyard-public-difference-" +
                difference + ":" +
                spec.first_information_set + ":" +
                spec.second_information_set);
            if (difference !=
                "observer-hand-order-only") {
                continue;
            }
        } else {
            ++report.graveyard_only_pairs;
            report.graveyard_only_rows +=
                spec.legacy_collision_rows;
        }

        GameState paired_right = left->state;
        for (std::size_t player = 0;
             player < paired_right.players.size(); ++player) {
            paired_right.players[player].graveyard =
                right->state.players[player].graveyard;
        }
        if (!graveyard_order_only) {
            paired_right.players[left->owner].hand =
                right->state.players[left->owner].hand;
        }
        if (fingerprint_for(
                paired_right, left->context,
                left->legal_actions) !=
            spec.second_information_set) {
            throw std::logic_error(
                spec.root_stable_id +
                ": FR1 controlled intervention did not "
                "reproduce the second identity");
        }

        const probes::DecisionProbe& probe =
            probe_for(spec.root_stable_id);
        const RegisteredRepresentativePair representative{
            .spec = spec,
            .first_state = left->state,
            .second_state = paired_right,
            .original_decks = probe.original_decks,
            .context = left->context,
            .authoritative_actions =
                left->legal_actions,
            .observer = left->owner,
            .first_root_world = left->root_world,
            .first_root_action = left->root_action,
            .continuation_seed = left->macro_seed,
        };
        if (controlled_pairs != nullptr) {
            controlled_pairs->push_back(representative);
        }
        if (!graveyard_order_only) {
            report.additional_public_representatives
                .push_back(representative);
            continue;
        }
        RegisteredPairResult result{
            .spec = spec,
            .observer = left->owner,
            .representative_root_world =
                left->root_world,
            .representative_root_action =
                left->root_action,
            .continuation_seed = left->macro_seed,
            .authoritative_actions =
                left->legal_actions.size(),
            .comparison = compare_graveyard_order_pair(
                left->state, paired_right,
                probe.original_decks, left->context,
                left->owner, model,
                left->macro_seed),
        };
        if (result.comparison.equivalent()) {
            ++report.equivalent_pairs;
        }
        report.pairs.push_back(std::move(result));
    }
    std::sort(
        registered_row_identities.begin(),
        registered_row_identities.end());
    std::string identity_bytes =
        "old-school-fr1-registered-collision-identities-v1\n";
    for (const std::string& row :
         registered_row_identities) {
        identity_bytes += row;
    }
    report.registered_row_identity_sha256 =
        artifact_integrity::sha256_string(identity_bytes);
    return report;
}

FactorialReport evaluate_sequence_factorial_from_anatomy(
    RegisteredAnatomyReport anatomy,
    std::shared_ptr<const LearnedModel> model) {
    FactorialReport report{
        .anatomy = std::move(anatomy),
    };
    report.eligible_pairs =
        report.anatomy
            .additional_public_representatives.size();
    const std::vector<PriorityAction> expected_actions{
        PriorityAction::pass(),
        PriorityAction::cast_artifact(
            CardId::MoxSapphire),
    };
    constexpr std::array<SequenceTreatment, 3>
        treatments{
            SequenceTreatment::Graveyards,
            SequenceTreatment::ObserverHand,
            SequenceTreatment::
                GraveyardsAndObserverHand,
        };
    report.results.reserve(
        report.eligible_pairs * treatments.size());
    for (const RegisteredRepresentativePair& pair :
         report.anatomy
             .additional_public_representatives) {
        if (pair.spec.root_stable_id != kWhiteRoot ||
            pair.spec.legacy_collision_rows != 2 ||
            pair.authoritative_actions != expected_actions) {
            report.failures.push_back(
                pair.spec.root_stable_id + ":" +
                pair.spec.first_information_set + ":" +
                pair.spec.second_information_set +
                ":setup:pinned-pair-actions");
        }
        for (SequenceTreatment treatment : treatments) {
            const GameState intervention =
                make_intervention(pair, treatment);
            FactorialContrast contrast =
                compare_sequence_treatment(
                    pair, intervention, treatment, model);
            ++report.contrasts;
            report.action_comparisons +=
                contrast.actions.size();
            if (contrast.equivalent()) {
                switch (treatment) {
                case SequenceTreatment::Graveyards:
                    ++report.graveyard_contrasts_equal;
                    break;
                case SequenceTreatment::ObserverHand:
                    ++report
                          .observer_hand_contrasts_equal;
                    break;
                case SequenceTreatment::
                    GraveyardsAndObserverHand:
                    ++report.combined_contrasts_equal;
                    break;
                }
            }
            append_contrast_failures(
                contrast, report.failures);
            if (contrast.actions.size() != 2) {
                report.failures.push_back(
                    contrast_prefix(contrast) +
                    ":setup:action-count");
            }
            report.results.push_back(
                std::move(contrast));
        }
    }
    if (!report.anatomy
             .additional_public_representatives.empty()) {
        const RegisteredRepresentativePair& pair =
            report.anatomy
                .additional_public_representatives.front();
        const GameState graveyard_intervention =
            make_intervention(
                pair, SequenceTreatment::Graveyards);
        const GameState hand_intervention =
            make_intervention(
                pair, SequenceTreatment::ObserverHand);
        const GameState combined_intervention =
            make_intervention(
                pair,
                SequenceTreatment::
                    GraveyardsAndObserverHand);
        const auto add_wrong_mask =
            [&](const GameState& intervention,
                SequenceTreatment mask) {
                FactorialContrast control =
                    compare_sequence_treatment(
                        pair, intervention, mask, model);
                if (control
                        .relevant_multisets_preserved &&
                    !control.treated_factor_only &&
                    !control.equivalent()) {
                    ++report
                          .wrong_mask_controls_detected;
                }
                report.wrong_mask_controls.push_back(
                    std::move(control));
            };
        add_wrong_mask(
            graveyard_intervention,
            SequenceTreatment::ObserverHand);
        add_wrong_mask(
            hand_intervention,
            SequenceTreatment::Graveyards);
        add_wrong_mask(
            combined_intervention,
            SequenceTreatment::Graveyards);
        add_wrong_mask(
            combined_intervention,
            SequenceTreatment::ObserverHand);

        GameState perturbed = make_intervention(
            pair,
            SequenceTreatment::
                GraveyardsAndObserverHand);
        --perturbed.players[pair.observer].life;
        report.life_control =
            compare_sequence_treatment(
                pair, perturbed,
                SequenceTreatment::
                    GraveyardsAndObserverHand,
                std::move(model));
        report.life_perturbation_detected =
            report.life_control
                .relevant_multisets_preserved &&
            report.life_control.treatment_nontrivial &&
            !report.life_control.treated_factor_only &&
            !report.life_control.equivalent();
    }
    return report;
}

FactorialReport evaluate_sequence_factorial_impl(
    std::shared_ptr<const LearnedModel> model) {
    RegisteredAnatomyReport anatomy =
        reconstruct_registered_anatomy_impl(model);
    return evaluate_sequence_factorial_from_anatomy(
        std::move(anatomy), std::move(model));
}

ResidualConflictReport
evaluate_registered_residual_conflicts_impl(
    std::shared_ptr<const LearnedModel> model) {
    if (!model) {
        throw std::invalid_argument(
            "FR3 requires a Value model");
    }
    std::vector<RegisteredRepresentativePair> pairs;
    RegisteredAnatomyReport anatomy =
        reconstruct_registered_anatomy_impl(
            model, &pairs);
    ResidualConflictReport report;
    report.prerequisite =
        evaluate_sequence_factorial_from_anatomy(
            std::move(anatomy), model);
    report.controlled_pairs = pairs.size();
    report.catalog_rows.reserve(
        report.prerequisite.anatomy
            .legacy_collision_rows);

    for (const RegisteredRepresentativePair& pair :
         pairs) {
        const std::vector<PriorityAction> first_actions =
            legal_priority_actions(
                pair.first_state, pair.observer,
                pair.context.sorcery_actions);
        const std::vector<PriorityAction> second_actions =
            legal_priority_actions(
                pair.second_state, pair.observer,
                pair.context.sorcery_actions);
        if (first_actions != pair.authoritative_actions ||
            second_actions != pair.authoritative_actions ||
            first_actions.size() !=
                pair.spec.legacy_collision_rows) {
            report.infrastructure_failures.push_back(
                pair.spec.root_stable_id + ":" +
                pair.spec.first_information_set + ":" +
                pair.spec.second_information_set +
                ":authoritative-actions");
            continue;
        }
        report.paired_actions += first_actions.size();

        const std::string first_legacy_information =
            fingerprint_for(
                pair.first_state, pair.context,
                first_actions);
        const std::string second_legacy_information =
            fingerprint_for(
                pair.second_state, pair.context,
                second_actions);
        if (first_legacy_information ==
                pair.spec.first_information_set &&
            second_legacy_information ==
                pair.spec.second_information_set) {
            ++report.exact_legacy_identity_pairs;
        } else {
            report.infrastructure_failures.push_back(
                pair.spec.root_stable_id + ":" +
                pair.spec.first_information_set + ":" +
                pair.spec.second_information_set +
                ":legacy-identities");
        }

        const std::string first_quotient_information =
            projection::
                graveyard_quotient_information_set_sha256(
                    pair.first_state, pair.context,
                    first_actions);
        const std::string second_quotient_information =
            projection::
                graveyard_quotient_information_set_sha256(
                    pair.second_state, pair.context,
                    second_actions);
        if (first_quotient_information ==
            second_quotient_information) {
            ++report.quotient_information_pairs_equal;
        }

        const std::string first_legacy_leaf =
            information::
                redacted_leaf_consequence_sha256(
                    pair.first_state, pair.observer,
                    pair.context);
        const std::string second_legacy_leaf =
            information::
                redacted_leaf_consequence_sha256(
                    pair.second_state, pair.observer,
                    pair.context);
        if (first_legacy_leaf != second_legacy_leaf) {
            ++report.legacy_leaf_conflict_pairs;
        }
        const std::string first_quotient_leaf =
            projection::
                graveyard_quotient_leaf_consequence_sha256(
                    pair.first_state, pair.observer,
                    pair.context);
        const std::string second_quotient_leaf =
            projection::
                graveyard_quotient_leaf_consequence_sha256(
                    pair.second_state, pair.observer,
                    pair.context);
        if (first_quotient_leaf !=
            second_quotient_leaf) {
            ++report.quotient_leaf_conflict_pairs;
        }

        for (const PriorityAction& action :
             first_actions) {
            const std::string descriptor =
                probes::stable_priority_action_descriptor(
                    action);
            const std::vector<double> first_features =
                learned_priority_policy_features(
                    pair.first_state, pair.observer,
                    action,
                    pair.context.sorcery_actions,
                    pair.context.phase,
                    pair.context.consecutive_passes);
            const std::vector<double> second_features =
                learned_priority_policy_features(
                    pair.second_state, pair.observer,
                    action,
                    pair.context.sorcery_actions,
                    pair.context.phase,
                    pair.context.consecutive_passes);
            const bool feature_identity =
                bit_identical(
                    first_features, second_features);
            if (feature_identity) {
                ++report
                      .policy_feature_rows_bit_identical;
            }
            ResidualCatalogRow row{
                .root_stable_id =
                    pair.spec.root_stable_id,
                .first_information_set =
                    first_legacy_information,
                .second_information_set =
                    second_legacy_information,
                .first_quotient_information_set =
                    first_quotient_information,
                .second_quotient_information_set =
                    second_quotient_information,
                .action_descriptor = descriptor,
                .action = action,
                .first_legacy_consequence =
                    information::
                        canonical_priority_consequence_sha256(
                            pair.first_state,
                            pair.observer, pair.context,
                            action),
                .second_legacy_consequence =
                    information::
                        canonical_priority_consequence_sha256(
                            pair.second_state,
                            pair.observer, pair.context,
                            action),
                .first_quotient_consequence =
                    projection::
                        graveyard_quotient_priority_consequence_sha256(
                            pair.first_state,
                            pair.observer, pair.context,
                            action),
                .second_quotient_consequence =
                    projection::
                        graveyard_quotient_priority_consequence_sha256(
                            pair.second_state,
                            pair.observer, pair.context,
                            action),
                .policy_features_bit_identical =
                    feature_identity,
            };
            if (row.first_legacy_consequence !=
                row.second_legacy_consequence) {
                ++report.legacy_consequence_conflicts;
            }
            if (row.first_quotient_consequence !=
                row.second_quotient_consequence) {
                ++report.residual_quotient_conflicts;
            }
            report.catalog_rows.push_back(
                std::move(row));
        }
    }
    report.source_state_action_instances =
        report.paired_actions * 2;
    std::sort(
        report.catalog_rows.begin(),
        report.catalog_rows.end(),
        residual_row_less);
    report.catalog_sha256 =
        residual_catalog_sha256(report.catalog_rows);
    evaluate_projection_controls(pairs, report);
    return report;
}

RegisteredAnatomyReport reconstruct_registered_anatomy(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    if (!frozen_c16 ||
        learned_model_fingerprint(frozen_c16) !=
            science::kProductionModelFingerprint) {
        throw std::invalid_argument(
            "FR1 requires the exact frozen C16 model");
    }
    return reconstruct_registered_anatomy_impl(
        std::move(frozen_c16));
}

FactorialReport evaluate_sequence_factorial(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    if (!frozen_c16 ||
        learned_model_fingerprint(frozen_c16) !=
            science::kProductionModelFingerprint) {
        throw std::invalid_argument(
            "FR2 requires the exact frozen C16 model");
    }
    return evaluate_sequence_factorial_impl(
        std::move(frozen_c16));
}

ResidualConflictReport evaluate_registered_residual_conflicts(
    std::shared_ptr<const LearnedModel> frozen_c16) {
    if (!frozen_c16 ||
        learned_model_fingerprint(frozen_c16) !=
            science::kProductionModelFingerprint) {
        throw std::invalid_argument(
            "FR3 requires the exact frozen C16 model");
    }
    if (kRegisteredResidualCatalogSha256.empty()) {
        throw std::logic_error(
            "FR3 catalog digest has not been frozen");
    }
    ResidualConflictReport report =
        evaluate_registered_residual_conflicts_impl(
            std::move(frozen_c16));
    if (report.catalog_sha256 !=
        kRegisteredResidualCatalogSha256) {
        throw std::logic_error(
            "FR3 registered catalog digest drifted");
    }
    return report;
}

namespace testing {

RegisteredAnatomyReport reconstruct_registered_anatomy(
    std::shared_ptr<const LearnedModel> value_model) {
    return reconstruct_registered_anatomy_impl(
        std::move(value_model));
}

FactorialReport evaluate_sequence_factorial(
    std::shared_ptr<const LearnedModel> value_model) {
    return evaluate_sequence_factorial_impl(
        std::move(value_model));
}

ResidualConflictReport evaluate_registered_residual_conflicts(
    std::shared_ptr<const LearnedModel> value_model) {
    return evaluate_registered_residual_conflicts_impl(
        std::move(value_model));
}

} // namespace testing

} // namespace old_school::fq0_causal_quotient
