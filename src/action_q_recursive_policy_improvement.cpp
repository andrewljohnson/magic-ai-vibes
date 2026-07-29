#include "old_school/action_q_recursive_policy_improvement.hpp"

#include "old_school/action_q_field_gate.hpp"
#include "old_school/action_q_offline_gate.hpp"
#include "old_school/learned_iteration.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace old_school::action_q_recursive_policy_improvement {
namespace {

constexpr std::string_view kRedundantCounterId =
    "control.blue.counter-redundant-same-target.v1";
constexpr std::string_view kInterveningCounterId =
    "control.blue.counter-same-target-after-intervening-counter.v1";
constexpr std::string_view kBraingeyserId =
    "control.blue.braingeyser-x0.v1";
constexpr std::string_view kAncestralId =
    action_q_field_gate::kStableId;
constexpr std::string_view kSickBearGrowthId =
    "field.green.second-main-sick-bear-growth.v1";
constexpr std::string_view kOpponentGrowthId =
    "field.green.begin-combat-growth-tapped-air.v1";
constexpr std::string_view kLiveForceSpikeId =
    "control.blue.force-spike-live-gray-ogre.v1";
constexpr std::string_view kFiveOpenForceSpikeId =
    action_q_offline_gate::kFiveOpenForceSpikeId;
constexpr std::string_view kLife20BlockId =
    "field.ru.life20-flying-men-chump-air.v1";
constexpr std::string_view kLife4BlockId =
    "field.ru.life4-flying-men-chump-air.v1";
constexpr std::string_view kAttackId =
    "diagnostic.ru.life20-flying-men-attack-air.v1";
constexpr std::string_view kPriorityBridgeId =
    "bridge.ru-mirror.seed42.first-island-pass";

constexpr PermanentId kFlyingMen = 1;
constexpr PermanentId kAttackingAirElemental = 2;
constexpr PermanentId kDefendingAirElemental = 3;

bool probability(double value) {
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

bool same_bits(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

std::size_t family_index(DecisionFamily family) {
    const std::size_t index =
        static_cast<std::size_t>(family);
    if (index >= 3) {
        throw std::invalid_argument(
            "AQ5 decision family is invalid");
    }
    return index;
}

std::uint64_t kind_tag(DecisionFamily family) {
    switch (family) {
    case DecisionFamily::Priority:
        return 0;
    case DecisionFamily::Attack:
        return 1;
    case DecisionFamily::Block:
        return 2;
    }
    throw std::invalid_argument(
        "AQ5 decision family is invalid");
}

learned_iteration::SeedDomain search_domain(
    DecisionFamily family) {
    return family == DecisionFamily::Priority
               ? learned_iteration::SeedDomain::PrioritySearch
               : learned_iteration::SeedDomain::AttackSearch;
}

learned_iteration::SeedDomain choice_domain(
    DecisionFamily family) {
    return family == DecisionFamily::Priority
               ? learned_iteration::SeedDomain::PriorityChoice
               : learned_iteration::SeedDomain::AttackChoice;
}

bool sorcery_actions_for(TurnPhase phase) {
    return phase == TurnPhase::FirstMain ||
           phase == TurnPhase::SecondMain;
}

void require_no_errors(
    std::span<const std::string> errors,
    std::string_view corpus) {
    if (!errors.empty()) {
        throw std::runtime_error(
            "AQ5 fixture validation failed for " +
            std::string(corpus) + ": " + errors.front());
    }
}

const probes::DecisionProbe& unique_probe(
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
            "AQ5 fixture is missing or duplicated: " +
            std::string(stable_id));
    }
    return *found;
}

std::array<std::size_t, kCardCount> card_counts(
    const std::vector<CardId>& cards) {
    std::array<std::size_t, kCardCount> counts{};
    for (const CardId card : cards) {
        const std::size_t index =
            static_cast<std::size_t>(card);
        if (index >= counts.size()) {
            throw std::invalid_argument(
                "AQ5 fixture contains an invalid card");
        }
        ++counts[index];
    }
    return counts;
}

void subtract_card(
    std::array<std::size_t, kCardCount>& counts,
    CardId card) {
    const std::size_t index =
        static_cast<std::size_t>(card);
    if (index >= counts.size() || counts[index] == 0) {
        throw std::invalid_argument(
            "AQ5 fixture exceeds its exact deck");
    }
    --counts[index];
}

void subtract_public_cards(
    std::array<std::size_t, kCardCount>& counts,
    const PlayerState& player) {
    for (const CardId card : player.hand) {
        subtract_card(counts, card);
    }
    for (const CardId card : player.graveyard) {
        subtract_card(counts, card);
    }
    for (const CardId card : player.exile) {
        subtract_card(counts, card);
    }
    for (const LandPermanent& land : player.lands) {
        subtract_card(counts, land.card);
    }
    for (const CreaturePermanent& creature : player.creatures) {
        subtract_card(counts, creature.card);
    }
    for (const ArtifactPermanent& artifact : player.artifacts) {
        subtract_card(counts, artifact.card);
    }
    for (const CardId card : player.enchantments) {
        subtract_card(counts, card);
    }
}

std::vector<CardId> expand_counts(
    const std::array<std::size_t, kCardCount>& counts) {
    std::vector<CardId> cards;
    for (std::size_t index = 0;
         index < counts.size(); ++index) {
        cards.insert(
            cards.end(), counts[index],
            static_cast<CardId>(index));
    }
    return cards;
}

void complete_hidden_zones(
    PreparedRoot& root,
    std::size_t opponent_hand_size = 5) {
    if (root.actor >= root.state.players.size()) {
        throw std::invalid_argument(
            "AQ5 fixture actor is invalid");
    }
    for (std::size_t player = 0;
         player < root.state.players.size(); ++player) {
        auto remaining =
            card_counts(root.original_decks[player]);
        subtract_public_cards(
            remaining, root.state.players[player]);
        for (const StackObject& object : root.state.stack) {
            if (object.kind == StackObjectKind::Spell &&
                object.controller == player) {
                subtract_card(remaining, object.card);
            }
        }
        std::vector<CardId> hidden =
            expand_counts(remaining);
        PlayerState& state = root.state.players[player];
        if (player == root.actor) {
            state.library = std::move(hidden);
            continue;
        }
        if (hidden.size() < opponent_hand_size) {
            throw std::invalid_argument(
                "AQ5 opponent hand exceeds its hidden pool");
        }
        const auto hand_end =
            hidden.begin() +
            static_cast<std::ptrdiff_t>(
                opponent_hand_size);
        state.hand.assign(hidden.begin(), hand_end);
        state.library.assign(hand_end, hidden.end());
    }
}

PreparedRoot from_probe(
    std::size_t fixture_ordinal,
    const probes::DecisionProbe& probe) {
    if (fixture_ordinal >= kFixtureCount ||
        probe.root_player >= 2 ||
        probe.candidates.empty()) {
        throw std::invalid_argument(
            "AQ5 cannot prepare an invalid probe");
    }

    PreparedRoot root{
        .fixture_ordinal = fixture_ordinal,
        .stable_id = probe.stable_id,
        .family = static_cast<DecisionFamily>(
            probe.decision_kind),
        .state = probe.state,
        .original_decks = probe.original_decks,
        .actor = probe.root_player,
        .phase = probe.phase,
        .consecutive_passes =
            probe.consecutive_passes,
        .sorcery_actions =
            sorcery_actions_for(probe.phase),
    };
    root.candidate_keys.reserve(
        probe.candidates.size());
    root.candidates.reserve(probe.candidates.size());
    for (const probes::Candidate& candidate :
         probe.candidates) {
        if (candidate.descriptor.empty()) {
            throw std::invalid_argument(
                "AQ5 probe candidate key is empty");
        }
        root.candidate_keys.push_back(
            candidate.descriptor);
        root.candidates.push_back(candidate.action);
    }

    if (root.family == DecisionFamily::Priority) {
        std::vector<PriorityAction> authored;
        authored.reserve(root.candidates.size());
        for (const auto& candidate : root.candidates) {
            const auto* action =
                std::get_if<PriorityAction>(&candidate);
            if (action == nullptr) {
                throw std::invalid_argument(
                    "AQ5 Priority fixture has a non-Priority "
                    "candidate");
            }
            authored.push_back(*action);
        }
        if (authored != legal_priority_actions(
                root.state, root.actor,
                root.sorcery_actions)) {
            throw std::invalid_argument(
                "AQ5 Priority fixture omits or reorders an "
                "authoritative legal action");
        }
    } else if (root.family == DecisionFamily::Attack) {
        if (root.candidates.size() != 2) {
            throw std::invalid_argument(
                "AQ5 Attack fixture is not binary");
        }
        std::optional<PermanentId> subject;
        std::array<bool, 2> includes{};
        for (std::size_t index = 0;
             index < root.candidates.size(); ++index) {
            const auto* action =
                std::get_if<probes::BinaryAttackDecision>(
                    &root.candidates[index]);
            if (action == nullptr ||
                (subject.has_value() &&
                 *subject != action->attacker)) {
                throw std::invalid_argument(
                    "AQ5 Attack fixture candidates disagree");
            }
            subject = action->attacker;
            includes[index] = action->include;
        }
        if (!subject.has_value() ||
            includes[0] == includes[1]) {
            throw std::invalid_argument(
                "AQ5 Attack fixture lacks Skip/Include");
        }
        root.subject = *subject;
    } else {
        if (root.candidates.size() != 2) {
            throw std::invalid_argument(
                "AQ5 Block fixture is not binary");
        }
        std::optional<PermanentId> attacker;
        std::optional<PermanentId> blocker;
        std::array<bool, 2> includes{};
        for (std::size_t index = 0;
             index < root.candidates.size(); ++index) {
            const auto* action =
                std::get_if<probes::BinaryBlockDecision>(
                    &root.candidates[index]);
            if (action == nullptr ||
                (attacker.has_value() &&
                 *attacker != action->attacker) ||
                (blocker.has_value() &&
                 *blocker != action->blocker)) {
                throw std::invalid_argument(
                    "AQ5 Block fixture candidates disagree");
            }
            attacker = action->attacker;
            blocker = action->blocker;
            includes[index] = action->include;
        }
        if (!attacker.has_value() ||
            !blocker.has_value() ||
            includes[0] == includes[1]) {
            throw std::invalid_argument(
                "AQ5 Block fixture lacks No Block/Block");
        }
        root.attackers = {*attacker};
        root.subject_blocker = *blocker;
    }
    return root;
}

PreparedRoot ancestral_root() {
    const auto source =
        action_q_field_gate::make_ancestral_field_root();
    if (!action_q_field_gate::
            has_required_action_identities(source)) {
        throw std::logic_error(
            "AQ5 Ancestral field root lost required actions");
    }
    PreparedRoot root{
        .fixture_ordinal = 2,
        .stable_id = std::string(kAncestralId),
        .family = DecisionFamily::Priority,
        .state = source.state,
        .original_decks = source.original_decks,
        .actor = source.actor,
        .phase = source.context.phase,
        .consecutive_passes =
            source.context.consecutive_passes,
        .sorcery_actions =
            source.context.sorcery_actions,
    };
    root.candidate_keys.reserve(
        source.legal_actions.size());
    root.candidates.reserve(source.legal_actions.size());
    for (std::size_t index = 0;
         index < source.legal_actions.size(); ++index) {
        if (index == source.pass_index) {
            root.candidate_keys.emplace_back("pass");
        } else if (index == source.self_target_index) {
            root.candidate_keys.emplace_back(
                "ancestral-self");
        } else if (index ==
                   source.opponent_target_index) {
            root.candidate_keys.emplace_back(
                "ancestral-opponent");
        } else {
            root.candidate_keys.push_back(
                probes::stable_priority_action_descriptor(
                    source.legal_actions[index]));
        }
        root.candidates.emplace_back(
            source.legal_actions[index]);
    }
    if (source.legal_actions !=
        legal_priority_actions(
            root.state, root.actor,
            root.sorcery_actions)) {
        throw std::logic_error(
            "AQ5 Ancestral legal action order drifted");
    }
    return root;
}

void validate_candidate_keys(const PreparedRoot& root) {
    if (root.candidate_keys.size() !=
            root.candidates.size() ||
        root.candidate_keys.empty()) {
        throw std::invalid_argument(
            "AQ5 root candidate metadata is incomplete");
    }
    std::vector<std::string> sorted =
        root.candidate_keys;
    if (std::any_of(
            sorted.begin(), sorted.end(),
            [](const std::string& key) {
                return key.empty();
            })) {
        throw std::invalid_argument(
            "AQ5 candidate key is empty");
    }
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(
            sorted.begin(), sorted.end()) !=
        sorted.end()) {
        throw std::invalid_argument(
            "AQ5 candidate keys are not unique");
    }
}

const CandidateScore& score_for(
    const std::vector<CandidateScore>& scores,
    std::string_view key) {
    const auto found = std::find_if(
        scores.begin(), scores.end(),
        [key](const CandidateScore& candidate) {
            return candidate.key == key;
        });
    if (found == scores.end() ||
        std::find_if(
            std::next(found), scores.end(),
            [key](const CandidateScore& candidate) {
                return candidate.key == key;
            }) != scores.end()) {
        throw std::invalid_argument(
            "AQ5 action-keyed score is missing or duplicated: " +
            std::string(key));
    }
    return *found;
}

const RootReport& root_for(
    const std::vector<RootReport>& roots,
    std::string_view stable_id) {
    const auto found = std::find_if(
        roots.begin(), roots.end(),
        [stable_id](const RootReport& root) {
            return root.stable_id == stable_id;
        });
    if (found == roots.end() ||
        std::find_if(
            std::next(found), roots.end(),
            [stable_id](const RootReport& root) {
                return root.stable_id == stable_id;
            }) != roots.end()) {
        throw std::invalid_argument(
            "AQ5 root report is missing or duplicated: " +
            std::string(stable_id));
    }
    return *found;
}

bool exact_max_contains(
    const RootReport& root, std::string_view key) {
    return score_for(root.candidates, key).exact_max;
}

bool selected(
    const RootReport& root, std::string_view key) {
    return root.selected_key == key;
}

std::size_t checked_product(
    std::size_t left, std::size_t right) {
    if (left != 0 &&
        right >
            std::numeric_limits<std::size_t>::max() /
                left) {
        throw std::overflow_error(
            "AQ5 sampler accounting overflow");
    }
    return left * right;
}

std::size_t checked_sum(
    std::span<const std::size_t> values) {
    std::size_t result = 0;
    for (const std::size_t value : values) {
        if (result >
            std::numeric_limits<std::size_t>::max() -
                value) {
            throw std::overflow_error(
                "AQ5 sampler accounting overflow");
        }
        result += value;
    }
    return result;
}

double mean_of(std::span<const double> values) {
    if (values.empty()) {
        throw std::invalid_argument(
            "AQ5 candidate has no samples");
    }
    double total = 0.0;
    for (const double value : values) {
        if (!probability(value)) {
            throw std::invalid_argument(
                "AQ5 sampler returned an invalid value");
        }
        total += value;
    }
    const double mean =
        total / static_cast<double>(values.size());
    if (!probability(mean)) {
        throw std::logic_error(
            "AQ5 sample mean is invalid");
    }
    return mean;
}

bool matrix_accounting_consistent(
    const std::vector<std::vector<std::size_t>>&
        rollout_counts,
    const std::vector<std::vector<std::size_t>>&
        invocation_counts,
    const std::vector<std::vector<std::size_t>>&
        maximum_depths,
    std::size_t rows,
    std::size_t samples_per_row,
    const SearchAccounting& accounting) {
    if (rollout_counts.size() != rows ||
        invocation_counts.size() != rows ||
        maximum_depths.size() != rows) {
        return false;
    }
    std::size_t rollout_sum = 0;
    std::size_t invocation_sum = 0;
    std::size_t maximum_depth = 0;
    for (std::size_t row = 0; row < rows; ++row) {
        if (rollout_counts[row].size() !=
                samples_per_row ||
            invocation_counts[row].size() !=
                samples_per_row ||
            maximum_depths[row].size() !=
                samples_per_row) {
            return false;
        }
        const std::size_t row_rollouts =
            checked_sum(rollout_counts[row]);
        const std::size_t row_invocations =
            checked_sum(invocation_counts[row]);
        if (rollout_sum >
                std::numeric_limits<std::size_t>::max() -
                    row_rollouts ||
            invocation_sum >
                std::numeric_limits<std::size_t>::max() -
                    row_invocations) {
            throw std::overflow_error(
                "AQ5 inner accounting overflow");
        }
        rollout_sum += row_rollouts;
        invocation_sum += row_invocations;
        for (std::size_t sample = 0;
             sample < samples_per_row; ++sample) {
            const std::size_t depth =
                maximum_depths[row][sample];
            if (depth > kMaximumActiveNesting ||
                (invocation_counts[row][sample] != 0 &&
                 depth != 1)) {
                return false;
            }
            maximum_depth =
                std::max(maximum_depth, depth);
        }
    }
    return rollout_sum ==
               accounting.inner_rollout_evaluations &&
           invocation_sum ==
               accounting.inner_search_invocations &&
           maximum_depth ==
               accounting.inner_search_max_depth;
}

SamplerOutput convert_samples(
    const PreparedRoot& root,
    const LearnedSearchConfig& config,
    const LearnedActionSamples& samples,
    std::span<const std::size_t> canonical_rows,
    std::span<const double> canonical_aggregates,
    std::size_t legal_choice_count) {
    validate_candidate_keys(root);
    if (canonical_rows.size() !=
            root.candidate_keys.size() ||
        samples.q_samples.size() !=
            legal_choice_count ||
        (!canonical_aggregates.empty() &&
         canonical_aggregates.size() !=
             legal_choice_count)) {
        throw std::logic_error(
            "AQ5 sampler returned an invalid action shape");
    }
    const std::size_t samples_per_row =
        checked_product(
            config.worlds,
            config.rollouts_per_world);
    SamplerOutput output;
    output.legal_choice_count = legal_choice_count;
    output.rules_settled = true;
    output.accounting = {
        .sampled_worlds = samples.sampled_worlds,
        .rollout_evaluations =
            samples.rollout_evaluations,
        .terminal_evaluations =
            samples.terminal_evaluations,
        .bootstrapped_evaluations =
            samples.bootstrapped_evaluations,
        .inner_rollout_evaluations =
            samples.inner_rollout_evaluations,
        .inner_search_invocations =
            samples.inner_search_invocations,
        .inner_search_max_depth =
            samples.inner_search_max_depth,
    };
    output.candidates.reserve(canonical_rows.size());
    for (std::size_t index = 0;
         index < canonical_rows.size(); ++index) {
        const std::size_t row = canonical_rows[index];
        if (row >= samples.q_samples.size() ||
            samples.q_samples[row].size() !=
                samples_per_row) {
            throw std::logic_error(
                "AQ5 sampler returned an invalid sample row");
        }
        std::vector<std::uint8_t> terminal_flags;
        if (!samples.terminal_evaluation_flags.empty()) {
            if (samples.terminal_evaluation_flags.size() !=
                    legal_choice_count ||
                samples.terminal_evaluation_flags[row].size() !=
                    samples_per_row ||
                !std::all_of(
                    samples.terminal_evaluation_flags[row].begin(),
                    samples.terminal_evaluation_flags[row].end(),
                    [](std::uint8_t flag) {
                        return flag <= 1U;
                    })) {
                throw std::logic_error(
                    "AQ6 terminal-flag sample shape is invalid");
            }
            terminal_flags =
                samples.terminal_evaluation_flags[row];
        }
        const double mean =
            canonical_aggregates.empty()
                ? mean_of(samples.q_samples[row])
                : canonical_aggregates[row];
        if (!probability(mean)) {
            throw std::logic_error(
                "AQ5 sampler aggregate is invalid");
        }
        std::vector<double> settled_boundary_samples;
        std::optional<double> settled_boundary_mean;
        if (!samples.settled_boundary_samples.empty()) {
            if (samples.settled_boundary_samples.size() !=
                    legal_choice_count ||
                samples.settled_boundary_samples[row].size() !=
                    samples_per_row) {
                throw std::logic_error(
                    "AQ6 settled-boundary sample shape is invalid");
            }
            settled_boundary_samples =
                samples.settled_boundary_samples[row];
            settled_boundary_mean =
                mean_of(settled_boundary_samples);
            if (!probability(*settled_boundary_mean)) {
                throw std::logic_error(
                    "AQ6 settled-boundary aggregate is invalid");
            }
        }
        std::vector<std::uint8_t>
            exact_combat_pure_chump_flags;
        std::vector<std::uint8_t>
            exact_combat_bound_fallback_flags;
        const bool has_exact_combat_evidence =
            !samples.exact_combat_pure_chump_flags.empty() ||
            !samples
                 .exact_combat_bound_fallback_flags.empty();
        if (has_exact_combat_evidence) {
            if (samples.exact_combat_pure_chump_flags.size() !=
                    legal_choice_count ||
                samples
                        .exact_combat_bound_fallback_flags
                        .size() !=
                    legal_choice_count ||
                samples.exact_combat_pure_chump_flags[row]
                        .size() !=
                    samples_per_row ||
                samples
                        .exact_combat_bound_fallback_flags[row]
                        .size() !=
                    samples_per_row) {
                throw std::logic_error(
                    "AQ6 exact-combat evidence shape is invalid");
            }
            for (std::size_t sample = 0;
                 sample < samples_per_row; ++sample) {
                const std::uint8_t pure_chump =
                    samples.exact_combat_pure_chump_flags[row]
                        [sample];
                const std::uint8_t bound_fallback =
                    samples
                        .exact_combat_bound_fallback_flags[row]
                        [sample];
                if (pure_chump > 1U ||
                    bound_fallback > 1U ||
                    (pure_chump != 0U &&
                     bound_fallback != 0U)) {
                    throw std::logic_error(
                        "AQ6 exact-combat evidence byte is "
                        "invalid");
                }
            }
            exact_combat_pure_chump_flags =
                samples.exact_combat_pure_chump_flags[row];
            exact_combat_bound_fallback_flags =
                samples
                    .exact_combat_bound_fallback_flags[row];
        }
        output.candidates.push_back({
            .key = root.candidate_keys[index],
            .samples = samples.q_samples[row],
            .terminal_evaluation_flags =
                std::move(terminal_flags),
            .settled_boundary_samples =
                std::move(settled_boundary_samples),
            .settled_boundary_mean =
                settled_boundary_mean,
            .exact_combat_pure_chump_flags =
                std::move(
                    exact_combat_pure_chump_flags),
            .exact_combat_bound_fallback_flags =
                std::move(
                    exact_combat_bound_fallback_flags),
            .mean = mean,
        });
    }

    const std::size_t expected_outer =
        checked_product(
            legal_choice_count, samples_per_row);
    bool accounting_consistent =
        samples.sampled_worlds == config.worlds &&
        samples.rollout_evaluations == expected_outer &&
        samples.terminal_evaluations <= expected_outer &&
        samples.bootstrapped_evaluations ==
            expected_outer -
                samples.terminal_evaluations &&
        samples.inner_search_max_depth <=
            kMaximumActiveNesting &&
        ((samples.inner_search_invocations == 0) ==
         (samples.inner_search_max_depth == 0));

    if (config.value_continuation_search_worlds == 0) {
        accounting_consistent =
            accounting_consistent &&
            samples.inner_rollout_evaluations == 0 &&
            samples.inner_search_invocations == 0 &&
            samples.inner_search_max_depth == 0;
    } else {
        const auto& rollout_matrix =
            root.family == DecisionFamily::Priority
                ? samples
                      .priority_inner_rollout_evaluations
                : samples
                      .combat_inner_rollout_evaluations;
        const auto& invocation_matrix =
            root.family == DecisionFamily::Priority
                ? samples
                      .priority_inner_search_invocations
                : samples
                      .combat_inner_search_invocations;
        const auto& depth_matrix =
            root.family == DecisionFamily::Priority
                ? samples
                      .priority_inner_search_max_depth
                : samples
                      .combat_inner_search_max_depth;
        accounting_consistent =
            accounting_consistent &&
            matrix_accounting_consistent(
                rollout_matrix, invocation_matrix,
                depth_matrix, legal_choice_count,
                samples_per_row, output.accounting);
    }
    output.accounting_consistent =
        accounting_consistent;
    return output;
}

std::vector<PriorityAction> priority_actions(
    const PreparedRoot& root) {
    std::vector<PriorityAction> actions;
    actions.reserve(root.candidates.size());
    for (const auto& candidate : root.candidates) {
        const auto* action =
            std::get_if<PriorityAction>(&candidate);
        if (action == nullptr) {
            throw std::invalid_argument(
                "AQ5 Priority adapter received a non-Priority "
                "candidate");
        }
        actions.push_back(*action);
    }
    return actions;
}

SamplerOutput sample_priority(
    const PreparedRoot& root,
    const std::shared_ptr<const LearnedModel>& model,
    LearnedSearchConfig config) {
    const auto actions = priority_actions(root);
    const auto authoritative =
        legal_priority_actions(
            root.state, root.actor,
            root.sorcery_actions);
    if (actions.size() != authoritative.size()) {
        throw std::invalid_argument(
            "AQ5 Priority adapter lacks complete legal coverage");
    }
    std::vector<std::size_t> canonical_rows;
    canonical_rows.reserve(actions.size());
    for (const PriorityAction& action : actions) {
        const auto found = std::find(
            authoritative.begin(),
            authoritative.end(), action);
        if (found == authoritative.end()) {
            throw std::invalid_argument(
                "AQ5 Priority adapter received an illegal action");
        }
        canonical_rows.push_back(
            static_cast<std::size_t>(
                std::distance(
                    authoritative.begin(), found)));
    }
    const LearnedActionSamples samples =
        learned_priority_action_samples(
            root.state, root.original_decks,
            root.actor, root.sorcery_actions,
            root.phase, root.consecutive_passes,
            authoritative, model, config);
    return convert_samples(
        root, config, samples, canonical_rows,
        samples.exact_priority_aggregate_scores,
        authoritative.size());
}

SamplerOutput sample_attack(
    const PreparedRoot& root,
    const std::shared_ptr<const LearnedModel>& model,
    LearnedSearchConfig config) {
    std::vector<std::size_t> canonical_rows;
    canonical_rows.reserve(root.candidates.size());
    for (const auto& candidate : root.candidates) {
        const auto* action =
            std::get_if<probes::BinaryAttackDecision>(
                &candidate);
        if (action == nullptr ||
            action->attacker != root.subject) {
            throw std::invalid_argument(
                "AQ5 Attack adapter received an invalid action");
        }
        canonical_rows.push_back(
            action->include ? 1U : 0U);
    }
    if (canonical_rows.size() != 2 ||
        canonical_rows[0] == canonical_rows[1]) {
        throw std::invalid_argument(
            "AQ5 Attack adapter lacks Skip/Include coverage");
    }
    const LearnedActionSamples samples =
        learned_binary_attack_samples(
            root.state, root.original_decks,
            root.actor, root.selected_attackers,
            root.subject, root.remaining_attackers,
            model, config);
    return convert_samples(
        root, config, samples, canonical_rows, {}, 2);
}

SamplerOutput sample_block(
    const PreparedRoot& root,
    const std::shared_ptr<const LearnedModel>& model,
    LearnedSearchConfig config) {
    const LearnedBlockChoiceSamples scored =
        learned_block_choice_samples(
            root.state, root.original_decks,
            root.actor, root.attackers,
            root.selected_blocks, root.subject_blocker,
            root.remaining_blockers, model, config);
    std::vector<std::size_t> canonical_rows;
    canonical_rows.reserve(root.candidates.size());
    for (const auto& candidate : root.candidates) {
        const auto* action =
            std::get_if<probes::BinaryBlockDecision>(
                &candidate);
        if (action == nullptr ||
            action->blocker != root.subject_blocker) {
            throw std::invalid_argument(
                "AQ5 Block adapter received an invalid action");
        }
        if (!action->include) {
            canonical_rows.push_back(0);
            continue;
        }
        const auto found = std::find(
            scored.legal_attackers.begin(),
            scored.legal_attackers.end(),
            action->attacker);
        if (found == scored.legal_attackers.end()) {
            throw std::invalid_argument(
                "AQ5 Block adapter received an illegal "
                "assignment");
        }
        canonical_rows.push_back(
            1 +
            static_cast<std::size_t>(
                std::distance(
                    scored.legal_attackers.begin(),
                    found)));
    }
    const std::size_t legal_choice_count =
        1 + scored.legal_attackers.size();
    if (canonical_rows.size() != legal_choice_count) {
        throw std::invalid_argument(
            "AQ5 Block adapter lacks complete legal coverage");
    }
    std::vector<std::size_t> sorted_rows =
        canonical_rows;
    std::sort(sorted_rows.begin(), sorted_rows.end());
    for (std::size_t index = 0;
         index < sorted_rows.size(); ++index) {
        if (sorted_rows[index] != index) {
            throw std::invalid_argument(
                "AQ5 Block adapter duplicated or omitted a "
                "legal assignment");
        }
    }
    return convert_samples(
        root, config, scored.samples,
        canonical_rows, {}, legal_choice_count);
}

SamplerOutput engine_score_rpi(
    const PreparedRoot& root,
    std::shared_ptr<const LearnedModel> model,
    LearnedSearchConfig config) {
    if (!model) {
        throw std::invalid_argument(
            "AQ5 engine adapter requires a model");
    }
    validate_candidate_keys(root);
    switch (root.family) {
    case DecisionFamily::Priority:
        return sample_priority(
            root, model, std::move(config));
    case DecisionFamily::Attack:
        return sample_attack(
            root, model, std::move(config));
    case DecisionFamily::Block:
        return sample_block(
            root, model, std::move(config));
    }
    throw std::invalid_argument(
        "AQ5 engine adapter received an invalid family");
}

std::string choose_exact_max(
    std::vector<CandidateScore>& candidates,
    std::uint64_t seed) {
    if (candidates.empty()) {
        throw std::invalid_argument(
            "AQ5 cannot choose from no candidates");
    }
    double best =
        -std::numeric_limits<double>::infinity();
    for (const CandidateScore& candidate : candidates) {
        if (!probability(candidate.mean)) {
            throw std::invalid_argument(
                "AQ5 candidate score is invalid");
        }
        best = std::max(best, candidate.mean);
    }
    std::vector<std::string> support;
    for (CandidateScore& candidate : candidates) {
        candidate.exact_max =
            same_bits(candidate.mean, best);
        if (candidate.exact_max) {
            support.push_back(candidate.key);
        }
    }
    std::sort(support.begin(), support.end());
    if (support.empty() ||
        std::adjacent_find(
            support.begin(), support.end()) !=
            support.end()) {
        throw std::logic_error(
            "AQ5 exact-max support is invalid");
    }
    std::mt19937_64 random(seed);
    std::uniform_int_distribution<std::size_t> choose(
        0, support.size() - 1);
    return support[choose(random)];
}

double selected_margin(
    const std::vector<CandidateScore>& candidates,
    std::string_view selected_key) {
    const CandidateScore& chosen =
        score_for(candidates, selected_key);
    double runner_up =
        -std::numeric_limits<double>::infinity();
    for (const CandidateScore& candidate : candidates) {
        if (candidate.key != selected_key) {
            runner_up =
                std::max(runner_up, candidate.mean);
        }
    }
    if (!std::isfinite(runner_up) ||
        chosen.mean < runner_up) {
        throw std::logic_error(
            "AQ5 untreated C16 selection has no runner-up");
    }
    return chosen.mean - runner_up;
}

UntreatedC16RootReport capture_untreated_c16(
    const PreparedRoot& root,
    std::shared_ptr<const LearnedModel> model,
    std::uint64_t search,
    std::uint64_t tie) {
    if (!model ||
        learned_model_fingerprint(model) !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "AQ5 untreated capture requires exact C16");
    }
    validate_candidate_keys(root);

    std::vector<CandidateScore> candidates;
    std::string selected_key;
    if (root.family == DecisionFamily::Priority) {
        const std::vector<PriorityAction> actions =
            priority_actions(root);
        const LearnedSearchConfig config{
            .seed = search,
            .worlds = 2,
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
            .value_continuation_search_scope =
                LearnedContinuationSearchScope::PriorityOnly,
        };
        const LearnedActionSamples samples =
            learned_priority_action_samples(
                root.state, root.original_decks,
                root.actor, root.sorcery_actions,
                root.phase, root.consecutive_passes,
                actions, model, config);
        if (samples.q_samples.size() !=
                root.candidate_keys.size() ||
            samples.exact_priority_aggregate_scores.size() !=
                root.candidate_keys.size()) {
            throw std::logic_error(
                "AQ5 untreated Priority capture shape changed");
        }
        candidates.reserve(root.candidate_keys.size());
        for (std::size_t index = 0;
             index < root.candidate_keys.size(); ++index) {
            candidates.push_back({
                .key = root.candidate_keys[index],
                .samples = samples.q_samples[index],
                .mean =
                    samples
                        .exact_priority_aggregate_scores[index],
            });
        }
        selected_key = choose_exact_max(candidates, tie);
    } else if (root.family == DecisionFamily::Attack) {
        std::vector<std::vector<PermanentId>> attack_sets;
        attack_sets.reserve(root.candidates.size());
        for (const probes::CandidateAction& candidate :
             root.candidates) {
            const auto* action =
                std::get_if<probes::BinaryAttackDecision>(
                    &candidate);
            if (action == nullptr ||
                action->attacker != root.subject) {
                throw std::invalid_argument(
                    "AQ5 untreated Attack candidate is invalid");
            }
            std::vector<PermanentId> attackers =
                root.selected_attackers;
            if (action->include) {
                attackers.push_back(root.subject);
            }
            if (!root.remaining_attackers.empty()) {
                throw std::invalid_argument(
                    "AQ5 untreated Attack capture requires a "
                    "complete binary root");
            }
            attack_sets.push_back(std::move(attackers));
        }
        const LearnedValueAttackSetScores scored =
            learned_value_attack_set_scores(
                root.state, root.actor, attack_sets,
                model, search, false);
        if (scored.scores.size() !=
                root.candidate_keys.size() ||
            scored.selected_candidate >=
                root.candidate_keys.size()) {
            throw std::logic_error(
                "AQ5 untreated Attack capture shape changed");
        }
        candidates.reserve(root.candidate_keys.size());
        for (std::size_t index = 0;
             index < root.candidate_keys.size(); ++index) {
            candidates.push_back({
                .key = root.candidate_keys[index],
                .samples = {scored.scores[index]},
                .mean = scored.scores[index],
            });
        }
        static_cast<void>(choose_exact_max(candidates, tie));
        selected_key =
            root.candidate_keys[scored.selected_candidate];
    } else if (root.family == DecisionFamily::Block) {
        const LearnedValueBlockChoiceScores scored =
            learned_value_block_choice_scores(
                root.state, root.actor, root.attackers,
                root.selected_blocks, root.subject_blocker,
                root.remaining_blockers, model, search);
        if (scored.scores.size() !=
                scored.legal_attackers.size() + 1 ||
            scored.selected_candidate >=
                scored.scores.size()) {
            throw std::logic_error(
                "AQ5 untreated Block capture shape changed");
        }
        candidates.reserve(root.candidates.size());
        std::optional<std::size_t> selected_candidate;
        for (std::size_t index = 0;
             index < root.candidates.size(); ++index) {
            const auto* action =
                std::get_if<probes::BinaryBlockDecision>(
                    &root.candidates[index]);
            if (action == nullptr ||
                action->blocker != root.subject_blocker) {
                throw std::invalid_argument(
                    "AQ5 untreated Block candidate is invalid");
            }
            std::size_t row = 0;
            if (action->include) {
                const auto found = std::find(
                    scored.legal_attackers.begin(),
                    scored.legal_attackers.end(),
                    action->attacker);
                if (found == scored.legal_attackers.end()) {
                    throw std::invalid_argument(
                        "AQ5 untreated Block assignment is "
                        "illegal");
                }
                row =
                    1 + static_cast<std::size_t>(
                            std::distance(
                                scored.legal_attackers.begin(),
                                found));
            }
            candidates.push_back({
                .key = root.candidate_keys[index],
                .samples = {scored.scores[row]},
                .mean = scored.scores[row],
            });
            if (row == scored.selected_candidate) {
                selected_candidate = index;
            }
        }
        if (candidates.size() != scored.scores.size() ||
            !selected_candidate.has_value()) {
            throw std::logic_error(
                "AQ5 untreated Block capture lacks complete "
                "legal coverage");
        }
        static_cast<void>(choose_exact_max(candidates, tie));
        selected_key =
            root.candidate_keys[*selected_candidate];
    } else {
        throw std::invalid_argument(
            "AQ5 untreated capture received an invalid family");
    }

    const double margin =
        selected_margin(candidates, selected_key);
    return {
        .stable_id = root.stable_id,
        .family = root.family,
        .candidates = std::move(candidates),
        .selected_key = selected_key,
        .selected_margin = margin,
        .complete_legal_choice_coverage = true,
        .finite_scores = true,
    };
}

bool treatment_off_fixed_seed_game_bit_identical(
    std::shared_ptr<const LearnedModel> model) {
    if (!model ||
        learned_model_fingerprint(model) !=
            kRequiredParentFingerprint) {
        return false;
    }
    const BotConfig c16{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 2,
        .exploration_rate = 0.0,
        .learned_model = model,
    };
    GameConfig baseline{
        .max_turns = 8,
        .starting_player = 0,
        .bots = {c16, c16},
        .learned_model = model,
        .learned_search_depth = 1,
        .recursive_policy_improvement_evaluation_depth = 0,
    };
    GameConfig sentinel = baseline;
    sentinel.recursive_policy_improvement_evaluation_depth = 1;
    Game first(
        ru_aggro_deck(), blue_deck(),
        kPreflightSeed, baseline);
    Game second(
        ru_aggro_deck(), blue_deck(),
        kPreflightSeed, sentinel);
    std::vector<LearnedDecisionTracePoint> first_trace;
    std::vector<LearnedDecisionTracePoint> second_trace;
    const GameResult first_result =
        first.run_with_priority_root_trace(first_trace);
    const GameResult second_result =
        second.run_with_priority_root_trace(second_trace);
    return first_result == second_result &&
           first_trace == second_trace &&
           first.state() == second.state();
}

std::string selected_key(
    std::vector<CandidateScore>& candidates,
    std::uint64_t tie) {
    return choose_exact_max(candidates, tie);
}

bool candidate_scores_bit_identical(
    const std::vector<CandidateScore>& left,
    const std::vector<CandidateScore>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    std::vector<const CandidateScore*> left_sorted;
    std::vector<const CandidateScore*> right_sorted;
    left_sorted.reserve(left.size());
    right_sorted.reserve(right.size());
    for (const CandidateScore& score : left) {
        left_sorted.push_back(&score);
    }
    for (const CandidateScore& score : right) {
        right_sorted.push_back(&score);
    }
    const auto by_key =
        [](const CandidateScore* first,
           const CandidateScore* second) {
            return first->key < second->key;
        };
    std::sort(
        left_sorted.begin(), left_sorted.end(), by_key);
    std::sort(
        right_sorted.begin(), right_sorted.end(), by_key);
    for (std::size_t index = 0;
         index < left_sorted.size(); ++index) {
        const CandidateScore& first =
            *left_sorted[index];
        const CandidateScore& second =
            *right_sorted[index];
        if (first.key != second.key ||
            first.exact_max != second.exact_max ||
            !same_bits(first.mean, second.mean) ||
            first.samples.size() !=
                second.samples.size()) {
            return false;
        }
        for (std::size_t sample = 0;
             sample < first.samples.size(); ++sample) {
            if (!same_bits(
                    first.samples[sample],
                    second.samples[sample])) {
                return false;
            }
        }
    }
    return true;
}

bool outputs_bit_identical(
    const SamplerOutput& left,
    const SamplerOutput& right) {
    return candidate_scores_bit_identical(
               left.candidates, right.candidates) &&
           left.accounting == right.accounting &&
           left.legal_choice_count ==
               right.legal_choice_count &&
           left.rules_settled ==
               right.rules_settled &&
           left.accounting_consistent ==
               right.accounting_consistent;
}

bool finite_candidate_scores(
    std::span<const CandidateScore> candidates) {
    return !candidates.empty() &&
           std::all_of(
               candidates.begin(), candidates.end(),
               [](const CandidateScore& candidate) {
                   return !candidate.key.empty() &&
                          probability(candidate.mean) &&
                          !candidate.samples.empty() &&
                          std::all_of(
                              candidate.samples.begin(),
                              candidate.samples.end(),
                              [](double sample) {
                                  return probability(sample);
                              });
               });
}

bool root_ids_match(
    const FixtureSpec& spec,
    const std::vector<RootReport>& roots) {
    if (spec.root_count == 0 ||
        spec.root_count > spec.stable_ids.size() ||
        roots.size() != spec.root_count) {
        return false;
    }
    for (std::size_t index = 0;
         index < spec.root_count; ++index) {
        const std::string_view wanted =
            spec.stable_ids[index];
        const std::size_t count =
            static_cast<std::size_t>(std::count_if(
                roots.begin(), roots.end(),
                [wanted](const RootReport& root) {
                    return root.stable_id == wanted;
                }));
        if (wanted.empty() || count != 1) {
            return false;
        }
    }
    return true;
}

bool treatment_config_exact(const BotConfig& bot) {
    return bot.kind == BotKind::Learned &&
           bot.learned_variant ==
               LearnedVariant::ValueSearchChampion &&
           bot.rollouts_per_action == 8 &&
           bot.exploration_rate == 0.0 &&
           bot.value_continuation_epsilon == 0.0 &&
           bot.value_priority_residual_weight == 0.0 &&
           !bot.value_pass_dominance &&
           bot.value_resolved_shallow_prior_weight == 0.0 &&
           !bot.value_adversarial_blocks &&
           !bot.value_actor_local_search &&
           bot.value_recursive_policy_improvement &&
           bot.value_continuation_controller ==
               LearnedContinuationController::Legacy &&
           static_cast<bool>(bot.learned_model);
}

bool search_config_matches_budget(
    const LearnedSearchConfig& config,
    const SearchBudget& budget) {
    return config.worlds == budget.worlds &&
           config.rollouts_per_world ==
               budget.rollouts_per_world &&
           config.horizon_turns ==
               budget.horizon_turns &&
           config.evaluation_threads ==
               budget.evaluation_threads &&
           config.value_continuation_search_worlds ==
               budget.continuation_worlds &&
           config.blend_shallow_prior ==
               budget.blend_shallow_prior &&
           config.continuation_variant ==
               LearnedVariant::ValueSearchChampion &&
           config.value_continuation_epsilon == 0.0 &&
           config.value_resolved_shallow_prior_weight ==
               0.0 &&
           config.value_priority_residual_weight == 0.0 &&
           !config.value_pass_dominance &&
           config.value_continuation_controller ==
               LearnedContinuationController::Legacy &&
           !config.capture_priority_h0_boundaries &&
           config.value_continuation_search_scope ==
               LearnedContinuationSearchScope::AllDecisions;
}

bool core_preflight_passed(
    const PreflightReport& report) {
    if (report.recipe != sealed_recipe() ||
        report.parent_fingerprint !=
            kRequiredParentFingerprint ||
        !report.untreated.gate_passed() ||
        !report.isolation.gate_passed()) {
        return false;
    }
    const auto manifest = fixture_manifest();
    constexpr std::array<std::size_t, 3>
        kExpectedFamilyRoots = {8, 1, 3};
    for (std::size_t index = 0;
         index < report.fixtures.size(); ++index) {
        if (report.fixtures[index].spec != manifest[index] ||
            report.fixtures[index].roots.size() !=
                manifest[index].root_count) {
            return false;
        }
    }
    for (std::size_t index = 0;
         index < report.families.size(); ++index) {
        if (report.families[index].family !=
                static_cast<DecisionFamily>(index) ||
            report.families[index].roots !=
                kExpectedFamilyRoots[index]) {
            return false;
        }
    }
    if (!std::all_of(
            report.fixtures.begin(),
            report.fixtures.end(),
            [](const FixtureReport& fixture) {
                return fixture.gate_passed();
            }) ||
        !std::all_of(
            report.families.begin(),
            report.families.end(),
            [](const FamilyInvariantReport& family) {
                return family.gate_passed();
            })) {
        return false;
    }
    std::size_t roots = 0;
    for (const FixtureReport& fixture :
         report.fixtures) {
        roots += fixture.roots.size();
    }
    return roots == kFixtureRootCount;
}

} // namespace

SealedRecipe sealed_recipe() {
    return {
        .root_seed = kPreflightSeed,
        .priority_outer = {
            .worlds = 8,
            .rollouts_per_world = 1,
            .horizon_turns = 8,
            .evaluation_threads = 4,
            .continuation_worlds = 2,
            .blend_shallow_prior = false,
        },
        .combat_outer = {
            .worlds = 2,
            .rollouts_per_world = 1,
            .horizon_turns = 4,
            .evaluation_threads = 1,
            .continuation_worlds = 1,
            .blend_shallow_prior = false,
        },
        .priority_inner = {
            .worlds = 2,
            .rollouts_per_world = 1,
            .horizon_turns = 4,
            .evaluation_threads = 1,
            .continuation_worlds = 0,
            .blend_shallow_prior = true,
        },
        .combat_inner = {
            .worlds = 1,
            .rollouts_per_world = 1,
            .horizon_turns = 4,
            .evaluation_threads = 1,
            .continuation_worlds = 0,
            .blend_shallow_prior = false,
        },
        .maximum_active_nesting =
            kMaximumActiveNesting,
        .treatment_default_off = true,
        .actor_local_redeterminization = true,
        .candidate_identity_absent_from_seeds = true,
        .inner_priority_blends_shallow_prior = true,
        .inner_combat_unblended = true,
        .damage_order_uses_c16 = true,
        .cleanup_uses_c16 = true,
    };
}

std::uint64_t search_seed(
    std::size_t fixture_ordinal,
    DecisionFamily family) {
    if (fixture_ordinal >= kFixtureCount) {
        throw std::out_of_range(
            "AQ5 fixture ordinal is outside the manifest");
    }
    return learned_iteration::derive_seed(
        kPreflightSeed, search_domain(family),
        0, fixture_ordinal, kind_tag(family));
}

std::uint64_t tie_seed(
    std::size_t fixture_ordinal,
    DecisionFamily family) {
    if (fixture_ordinal >= kFixtureCount) {
        throw std::out_of_range(
            "AQ5 fixture ordinal is outside the manifest");
    }
    return learned_iteration::derive_seed(
        kPreflightSeed, choice_domain(family),
        0, fixture_ordinal, kind_tag(family));
}

std::array<FixtureSpec, kFixtureCount>
fixture_manifest() {
    const std::array<FixtureSpec, kFixtureCount> manifest{{
        {
            .ordinal = 0,
            .family = DecisionFamily::Priority,
            .direction =
                DirectionKind::CounterComposition,
            .stable_ids = {
                kRedundantCounterId,
                kInterveningCounterId,
            },
            .root_count = 2,
            .expected_search_seed =
                14925178104104382783ULL,
            .expected_tie_seed =
                1900242268981296062ULL,
        },
        {
            .ordinal = 1,
            .family = DecisionFamily::Priority,
            .direction =
                DirectionKind::BraingeyserTargetAndX,
            .stable_ids = {kBraingeyserId, {}},
            .root_count = 1,
            .expected_search_seed =
                11688540352458900462ULL,
            .expected_tie_seed =
                335956367961496456ULL,
        },
        {
            .ordinal = 2,
            .family = DecisionFamily::Priority,
            .direction =
                DirectionKind::AncestralTarget,
            .stable_ids = {kAncestralId, {}},
            .root_count = 1,
            .expected_search_seed =
                2928090455408451279ULL,
            .expected_tie_seed =
                8696818647665387427ULL,
        },
        {
            .ordinal = 3,
            .family = DecisionFamily::Priority,
            .direction =
                DirectionKind::GiantGrowthTimingAndTarget,
            .stable_ids = {
                kSickBearGrowthId,
                kOpponentGrowthId,
            },
            .root_count = 2,
            .expected_search_seed =
                13752769021523688216ULL,
            .expected_tie_seed =
                14181649472492185032ULL,
        },
        {
            .ordinal = 4,
            .family = DecisionFamily::Priority,
            .direction =
                DirectionKind::ForceSpikeTax,
            .stable_ids = {
                kLiveForceSpikeId,
                kFiveOpenForceSpikeId,
            },
            .root_count = 2,
            .expected_search_seed =
                13397045430005816879ULL,
            .expected_tie_seed =
                7481074947841747363ULL,
        },
        {
            .ordinal = 5,
            .family = DecisionFamily::Block,
            .direction =
                DirectionKind::LifeSensitiveBlock,
            .stable_ids = {
                kLife20BlockId,
                kLife4BlockId,
            },
            .root_count = 2,
            .expected_search_seed =
                4081014860004364270ULL,
            .expected_tie_seed =
                5489381744213982770ULL,
        },
        {
            .ordinal = 6,
            .family = DecisionFamily::Attack,
            .direction = DirectionKind::AvoidBadAttack,
            .stable_ids = {kAttackId, {}},
            .root_count = 1,
            .expected_search_seed =
                6732938601306594426ULL,
            .expected_tie_seed =
                6198590922445976520ULL,
        },
        {
            .ordinal = 7,
            .family = DecisionFamily::Block,
            .direction =
                DirectionKind::MultiChoiceBlock,
            .stable_ids = {
                kNewBlueBlockFixtureId, {},
            },
            .root_count = 1,
            .expected_search_seed =
                4870559487321488737ULL,
            .expected_tie_seed =
                11392915222590301558ULL,
        },
    }};
    for (std::size_t index = 0;
         index < manifest.size(); ++index) {
        const FixtureSpec& spec = manifest[index];
        if (spec.ordinal != index ||
            spec.root_count == 0 ||
            spec.root_count > spec.stable_ids.size() ||
            spec.expected_search_seed !=
                search_seed(index, spec.family) ||
            spec.expected_tie_seed !=
                tie_seed(index, spec.family)) {
            throw std::logic_error(
                "AQ5 sealed fixture manifest drifted");
        }
        for (std::size_t root = 0;
             root < spec.root_count; ++root) {
            if (spec.stable_ids[root].empty()) {
                throw std::logic_error(
                    "AQ5 sealed fixture id is empty");
            }
        }
    }
    return manifest;
}

LearnedSearchConfig outer_search_config(
    DecisionFamily family, std::uint64_t seed) {
    static_cast<void>(family_index(family));
    LearnedSearchConfig config =
        family == DecisionFamily::Priority
            ? learned_value_recursive_policy_improvement_priority_config(
                  seed)
            : learned_value_recursive_policy_improvement_combat_config(
                  seed);
    const SealedRecipe recipe = sealed_recipe();
    const SearchBudget& expected =
        family == DecisionFamily::Priority
            ? recipe.priority_outer
            : recipe.combat_outer;
    if (config.seed != seed ||
        !search_config_matches_budget(config, expected)) {
        throw std::logic_error(
            "AQ5 engine search recipe drifted from its seal");
    }
    return config;
}

BotConfig treatment_bot_config(
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "AQ5 treatment requires exact frozen C16");
    }
    BotConfig result{
        .kind = BotKind::Learned,
        .learned_variant =
            LearnedVariant::ValueSearchChampion,
        .rollouts_per_action = 8,
        .exploration_rate = 0.0,
        .value_continuation_epsilon = 0.0,
        .value_priority_residual_weight = 0.0,
        .value_pass_dominance = false,
        .value_resolved_shallow_prior_weight = 0.0,
        .value_adversarial_blocks = false,
        .value_actor_local_search = false,
        .value_recursive_policy_improvement = true,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
        .learned_model = std::move(parent),
    };
    if (!treatment_config_exact(result)) {
        throw std::logic_error(
            "AQ5 treatment configuration is not isolated");
    }
    return result;
}

PreparedRoot make_blue_multi_choice_block_fixture() {
    PreparedRoot root{
        .fixture_ordinal = 7,
        .stable_id = std::string(kNewBlueBlockFixtureId),
        .family = DecisionFamily::Block,
        .original_decks = {
            blue_deck(),
            blue_deck(),
        },
        .actor = 0,
        .phase = TurnPhase::DeclareBlockers,
        .candidate_keys = {
            "no-block",
            "block-air-elemental-with-flying-men",
        },
        .candidates = {
            probes::BinaryBlockDecision{
                .attacker = kAttackingAirElemental,
                .blocker = kFlyingMen,
                .include = false,
            },
            probes::BinaryBlockDecision{
                .attacker = kAttackingAirElemental,
                .blocker = kFlyingMen,
                .include = true,
            },
        },
        .attackers = {kAttackingAirElemental},
        .subject_blocker = kFlyingMen,
        .remaining_blockers = {
            kDefendingAirElemental,
        },
    };
    root.state.active_player = 1;
    root.state.starting_player = 0;
    root.state.turn_number = 10;
    root.state.next_permanent_id = 4;
    root.state.next_stack_object_id = 1;
    root.state.players[0].life = 20;
    root.state.players[0].lands.assign(
        5, LandPermanent{
               .card = CardId::Island,
               .tapped = false,
           });
    root.state.players[0].creatures = {
        {
            .id = kFlyingMen,
            .card = CardId::FlyingMen,
            .tapped = false,
            .summoning_sick = false,
        },
        {
            .id = kDefendingAirElemental,
            .card = CardId::AirElemental,
            .tapped = false,
            .summoning_sick = false,
        },
    };
    root.state.players[1].life = 20;
    root.state.players[1].lands.assign(
        5, LandPermanent{
               .card = CardId::Island,
               .tapped = true,
           });
    root.state.players[1].creatures = {
        {
            .id = kAttackingAirElemental,
            .card = CardId::AirElemental,
            .tapped = true,
            .summoning_sick = false,
        },
    };
    complete_hidden_zones(root);
    validate_candidate_keys(root);
    if (!root.state.stack.empty() ||
        !root.state.players[0].graveyard.empty() ||
        !root.state.players[1].graveyard.empty() ||
        !root.state.players[0].exile.empty() ||
        !root.state.players[1].exile.empty() ||
        root.state.players[0].lands.size() != 5 ||
        root.state.players[1].lands.size() != 5 ||
        root.state.players[0].creatures.size() != 2 ||
        root.state.players[1].creatures.size() != 1) {
        throw std::logic_error(
            "AQ5 exact Blue block fixture drifted");
    }
    return root;
}

std::vector<PreparedRoot> build_fixture_roots() {
    const auto counters =
        probes::make_counter_composition_controls_v1();
    require_no_errors(
        probes::validate_counter_composition_controls_v1(
            counters),
        probes::kCounterCompositionControlsV1);
    const auto braingeyser =
        probes::make_braingeyser_x_zero_control_v1();
    require_no_errors(
        probes::validate_braingeyser_x_zero_control_v1(
            braingeyser),
        probes::kBraingeyserXZeroControlV1);
    const auto fields =
        probes::make_field_regressions_v1();
    require_no_errors(
        probes::validate_field_regressions_v1(fields),
        probes::kFieldRegressionsV1);
    const auto spikes =
        probes::make_force_spike_policy_controls_v1();
    require_no_errors(
        probes::validate_force_spike_policy_controls_v1(
            spikes),
        probes::kForceSpikePolicyControlsV1);
    const auto attack =
        probes::make_attack_regression_v1();
    require_no_errors(
        probes::validate_attack_regression_v1(attack),
        probes::kAttackRegressionV1);
    const probes::DecisionProbe five_open =
        action_q_offline_gate::
            make_five_open_force_spike_control();
    require_no_errors(
        action_q_offline_gate::
            validate_five_open_force_spike_control(
                five_open),
        action_q_offline_gate::
            kFiveOpenForceSpikeCorpusId);

    std::vector<PreparedRoot> roots;
    roots.reserve(kFixtureRootCount);
    roots.push_back(from_probe(
        0, unique_probe(
               counters, kRedundantCounterId)));
    roots.push_back(from_probe(
        0, unique_probe(
               counters, kInterveningCounterId)));
    roots.push_back(from_probe(
        1, unique_probe(
               braingeyser, kBraingeyserId)));
    roots.push_back(ancestral_root());
    roots.push_back(from_probe(
        3, unique_probe(
               fields, kSickBearGrowthId)));
    roots.push_back(from_probe(
        3, unique_probe(
               fields, kOpponentGrowthId)));
    roots.push_back(from_probe(
        4, unique_probe(
               spikes, kLiveForceSpikeId)));
    roots.push_back(from_probe(4, five_open));
    roots.push_back(from_probe(
        5, unique_probe(fields, kLife20BlockId)));
    roots.push_back(from_probe(
        5, unique_probe(fields, kLife4BlockId)));
    roots.push_back(from_probe(
        6, unique_probe(attack, kAttackId)));
    roots.push_back(
        make_blue_multi_choice_block_fixture());

    if (roots.size() != kFixtureRootCount) {
        throw std::logic_error(
            "AQ5 fixture root count drifted");
    }
    const auto manifest = fixture_manifest();
    for (const PreparedRoot& root : roots) {
        validate_candidate_keys(root);
        if (root.fixture_ordinal >= manifest.size() ||
            root.family !=
                manifest[root.fixture_ordinal].family ||
            std::find(
                manifest[root.fixture_ordinal]
                    .stable_ids.begin(),
                manifest[root.fixture_ordinal]
                    .stable_ids.begin() +
                    static_cast<std::ptrdiff_t>(
                        manifest[root.fixture_ordinal]
                            .root_count),
                root.stable_id) ==
                manifest[root.fixture_ordinal]
                    .stable_ids.begin() +
                    static_cast<std::ptrdiff_t>(
                        manifest[root.fixture_ordinal]
                            .root_count)) {
            throw std::logic_error(
                "AQ5 fixture root is outside its sealed group");
        }
    }
    return roots;
}

PreparedRoot make_hidden_repartition_clone(
    const PreparedRoot& root) {
    if (root.actor >= root.state.players.size()) {
        throw std::invalid_argument(
            "AQ5 hidden clone actor is invalid");
    }
    PreparedRoot clone = root;
    std::reverse(
        clone.state.players[root.actor].library.begin(),
        clone.state.players[root.actor].library.end());

    const std::size_t opponent = 1 - root.actor;
    PlayerState& hidden = clone.state.players[opponent];
    const PlayerState& original =
        root.state.players[opponent];
    bool crossed_zone = false;
    for (std::size_t hand = 0;
         hand < hidden.hand.size() && !crossed_zone;
         ++hand) {
        for (std::size_t library = 0;
             library < hidden.library.size(); ++library) {
            if (hidden.hand[hand] ==
                hidden.library[library]) {
                continue;
            }
            std::swap(
                hidden.hand[hand],
                hidden.library[library]);
            crossed_zone = true;
            break;
        }
    }

    std::vector<CardId> original_hidden =
        original.hand;
    original_hidden.insert(
        original_hidden.end(),
        original.library.begin(),
        original.library.end());
    std::vector<CardId> cloned_hidden =
        hidden.hand;
    cloned_hidden.insert(
        cloned_hidden.end(),
        hidden.library.begin(),
        hidden.library.end());
    std::sort(
        original_hidden.begin(),
        original_hidden.end());
    std::sort(
        cloned_hidden.begin(),
        cloned_hidden.end());
    if (!crossed_zone ||
        original_hidden != cloned_hidden ||
        clone.state == root.state ||
        observe_game_state(clone.state, root.actor) !=
            observe_game_state(root.state, root.actor)) {
        throw std::logic_error(
            "AQ5 hidden repartition witness is vacuous "
            "or observation-changing");
    }
    return clone;
}

PreparedRoot reverse_candidate_order(
    const PreparedRoot& root) {
    PreparedRoot reversed = root;
    std::reverse(
        reversed.candidate_keys.begin(),
        reversed.candidate_keys.end());
    std::reverse(
        reversed.candidates.begin(),
        reversed.candidates.end());
    validate_candidate_keys(reversed);
    return reversed;
}

bool fixture_direction_passed(
    const FixtureSpec& spec,
    const std::vector<RootReport>& roots) {
    if (!root_ids_match(spec, roots)) {
        return false;
    }
    try {
        switch (spec.direction) {
        case DirectionKind::CounterComposition: {
            const RootReport& redundant =
                root_for(roots, kRedundantCounterId);
            const RootReport& intervening =
                root_for(roots, kInterveningCounterId);
            return selected(redundant, "pass") &&
                   selected(
                       intervening,
                       "counter-opponent-counterspell");
        }
        case DirectionKind::BraingeyserTargetAndX: {
            const RootReport& root =
                root_for(roots, kBraingeyserId);
            return selected(
                       root, "braingeyser-x1-self") &&
                   !exact_max_contains(
                       root, "braingeyser-x0-self") &&
                   !exact_max_contains(
                       root,
                       "braingeyser-x0-opponent");
        }
        case DirectionKind::AncestralTarget: {
            const RootReport& root =
                root_for(roots, kAncestralId);
            return selected(root, "ancestral-self") &&
                   !exact_max_contains(
                       root, "ancestral-opponent");
        }
        case DirectionKind::GiantGrowthTimingAndTarget: {
            const RootReport& sick =
                root_for(roots, kSickBearGrowthId);
            const RootReport& opponent =
                root_for(roots, kOpponentGrowthId);
            return selected(sick, "pass") &&
                   !exact_max_contains(
                       opponent,
                       "growth-opponent-tapped-air-elemental");
        }
        case DirectionKind::ForceSpikeTax: {
            const RootReport& live =
                root_for(roots, kLiveForceSpikeId);
            const RootReport& five_open =
                root_for(roots, kFiveOpenForceSpikeId);
            return selected(
                       live, "force-spike-gray-ogre") &&
                   selected(five_open, "pass");
        }
        case DirectionKind::LifeSensitiveBlock: {
            const RootReport& life20 =
                root_for(roots, kLife20BlockId);
            const RootReport& life4 =
                root_for(roots, kLife4BlockId);
            return selected(life20, "no-blocks") &&
                   selected(
                       life4,
                       "block-air-elemental-with-flying-men");
        }
        case DirectionKind::AvoidBadAttack:
            return selected(
                root_for(roots, kAttackId),
                "no-attack");
        case DirectionKind::MultiChoiceBlock: {
            const RootReport& root =
                root_for(
                    roots, kNewBlueBlockFixtureId);
            const CandidateScore& no_block =
                score_for(root.candidates, "no-block");
            const CandidateScore& chump =
                score_for(
                    root.candidates,
                    "block-air-elemental-with-flying-men");
            return selected(root, "no-block") &&
                   no_block.mean > chump.mean;
        }
        }
    } catch (const std::invalid_argument&) {
        return false;
    }
    return false;
}

bool UntreatedC16Report::gate_passed() const {
    if (parent_fingerprint !=
            kRequiredParentFingerprint ||
        !captured_before_rpi ||
        roots.size() != kFixtureRootCount) {
        return false;
    }
    std::vector<std::pair<std::string, DecisionFamily>>
        observed;
    observed.reserve(roots.size());
    for (const UntreatedC16RootReport& root : roots) {
        std::vector<std::string> keys;
        keys.reserve(root.candidates.size());
        std::size_t selected_count = 0;
        for (const CandidateScore& candidate :
             root.candidates) {
            keys.push_back(candidate.key);
            if (candidate.key == root.selected_key) {
                ++selected_count;
            }
        }
        std::sort(keys.begin(), keys.end());
        if (root.stable_id.empty() ||
            root.candidates.empty() ||
            root.selected_key.empty() ||
            !root.complete_legal_choice_coverage ||
            !root.finite_scores ||
            !std::isfinite(root.selected_margin) ||
            root.selected_margin < 0.0 ||
            selected_count != 1 ||
            std::adjacent_find(
                keys.begin(), keys.end()) != keys.end() ||
            !finite_candidate_scores(root.candidates)) {
            return false;
        }
        const CandidateScore& chosen =
            score_for(
                root.candidates, root.selected_key);
        double runner_up =
            -std::numeric_limits<double>::infinity();
        for (const CandidateScore& candidate :
             root.candidates) {
            if (candidate.key != root.selected_key) {
                runner_up =
                    std::max(
                        runner_up, candidate.mean);
            }
        }
        if (!std::isfinite(runner_up) ||
            chosen.mean < runner_up ||
            !same_bits(
                root.selected_margin,
                chosen.mean - runner_up)) {
            return false;
        }
        observed.emplace_back(
            root.stable_id, root.family);
    }
    std::vector<std::pair<std::string, DecisionFamily>>
        expected;
    for (const FixtureSpec& fixture :
         fixture_manifest()) {
        for (std::size_t root = 0;
             root < fixture.root_count; ++root) {
            expected.emplace_back(
                fixture.stable_ids[root],
                fixture.family);
        }
    }
    const auto by_id =
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        };
    std::sort(observed.begin(), observed.end(), by_id);
    std::sort(expected.begin(), expected.end(), by_id);
    return observed == expected;
}

bool RootReport::gate_passed() const {
    if (stable_id.empty() ||
        selected_key.empty() ||
        !finite_candidate_scores(candidates)) {
        return false;
    }
    std::vector<std::string> keys;
    keys.reserve(candidates.size());
    bool selected_present = false;
    bool selected_is_max = false;
    for (const CandidateScore& candidate : candidates) {
        keys.push_back(candidate.key);
        if (candidate.key == selected_key) {
            selected_present = true;
            selected_is_max = candidate.exact_max;
        }
    }
    std::sort(keys.begin(), keys.end());
    const bool keys_unique =
        std::adjacent_find(
            keys.begin(), keys.end()) == keys.end();
    return selected_present &&
           selected_is_max &&
           keys_unique &&
           complete_legal_choice_coverage &&
           rules_settled &&
           finite_scores &&
           accounting_consistent &&
           reversed_input_action_keyed_bit_identical &&
           hidden_repartition_nonvacuous &&
           hidden_observation_bit_identical &&
           hidden_scores_bit_identical &&
           hidden_choice_bit_identical &&
           hidden_accounting_bit_identical &&
           accounting.inner_search_max_depth <=
               kMaximumActiveNesting;
}

bool FixtureReport::gate_passed() const {
    return spec.ordinal < kFixtureCount &&
           static_cast<std::size_t>(spec.family) < 3 &&
           spec.expected_search_seed ==
               search_seed(spec.ordinal, spec.family) &&
           spec.expected_tie_seed ==
               tie_seed(spec.ordinal, spec.family) &&
           direction_passed &&
           fixture_direction_passed(spec, roots) &&
           std::all_of(
               roots.begin(), roots.end(),
               [this](const RootReport& root) {
                   return root.family == spec.family &&
                          root.gate_passed();
               });
}

bool FamilyInvariantReport::gate_passed() const {
    return static_cast<std::size_t>(family) < 3 &&
           roots > 0 &&
           complete_legal_choice_coverage &&
           rules_settled &&
           finite_scores &&
           accounting_consistent &&
           reversed_input_action_keyed_bit_identical &&
           hidden_repartition_nonvacuous &&
           hidden_observation_bit_identical &&
           hidden_scores_bit_identical &&
           hidden_choice_bit_identical &&
           hidden_accounting_bit_identical &&
           one_level_nesting_bounded &&
           maximum_active_nesting ==
               kMaximumActiveNesting;
}

bool IsolationReport::gate_passed() const {
    return exact_parent &&
           treatment_default_off &&
           exact_configuration &&
           all_other_treatments_off &&
           treatment_off_fixed_seed_game_bit_identical;
}

bool PreflightReport::gate_passed() const {
    return hypothesis_passed &&
           core_preflight_passed(*this);
}

bool BridgeSmokeReport::gate_passed() const {
    return static_cast<std::size_t>(family) < 3 &&
           !fixture_id.empty() &&
           authoritative_decision_completed &&
           elapsed_seconds < kBridgeTimeoutSeconds;
}

bool BridgeReport::gate_passed() const {
    const bool identities =
        smokes[0].family == DecisionFamily::Priority &&
        smokes[0].fixture_id == kPriorityBridgeId &&
        smokes[1].family == DecisionFamily::Attack &&
        smokes[1].fixture_id == kAttackId &&
        smokes[2].family == DecisionFamily::Block &&
        smokes[2].fixture_id ==
            kNewBlueBlockFixtureId;
    return identities &&
           std::all_of(
               smokes.begin(), smokes.end(),
               [](const BridgeSmokeReport& smoke) {
                   return smoke.gate_passed();
               }) &&
           web_bridge_tests_passed &&
           web_ui_tests_passed;
}

bool public_option_licensed(
    const PreflightReport& preflight,
    const BridgeReport& bridge) {
    return preflight.gate_passed() &&
           bridge.gate_passed();
}

SamplerApi engine_sampler_api() {
    return {
        .score_rpi = engine_score_rpi,
        .capture_untreated_c16 =
            capture_untreated_c16,
        .treatment_off_fixed_seed_game_bit_identical =
            treatment_off_fixed_seed_game_bit_identical,
    };
}

PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent,
    const SamplerApi& samplers) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "AQ5 preflight requires exact frozen C16");
    }
    if (!samplers.score_rpi ||
        !samplers.capture_untreated_c16 ||
        !samplers
             .treatment_off_fixed_seed_game_bit_identical) {
        throw std::invalid_argument(
            "AQ5 preflight sampler API is incomplete");
    }

    const auto manifest = fixture_manifest();
    const std::vector<PreparedRoot> roots =
        build_fixture_roots();
    PreflightReport report;
    report.recipe = sealed_recipe();
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    for (std::size_t index = 0;
         index < report.fixtures.size(); ++index) {
        report.fixtures[index].spec =
            manifest[index];
    }
    for (std::size_t family = 0;
         family < report.families.size(); ++family) {
        FamilyInvariantReport& invariant =
            report.families[family];
        invariant.family =
            static_cast<DecisionFamily>(family);
        invariant.complete_legal_choice_coverage = true;
        invariant.rules_settled = true;
        invariant.finite_scores = true;
        invariant.accounting_consistent = true;
        invariant
            .reversed_input_action_keyed_bit_identical =
            true;
        invariant.hidden_repartition_nonvacuous = true;
        invariant.hidden_observation_bit_identical = true;
        invariant.hidden_scores_bit_identical = true;
        invariant.hidden_choice_bit_identical = true;
        invariant.hidden_accounting_bit_identical = true;
        invariant.one_level_nesting_bounded = true;
    }

    // The declaration requires every immutable untreated fact to be captured
    // before any RPI score is observed. Keep this as a separate complete pass.
    report.untreated.parent_fingerprint =
        report.parent_fingerprint;
    report.untreated.roots.reserve(roots.size());
    for (const PreparedRoot& root : roots) {
        UntreatedC16RootReport captured =
            samplers.capture_untreated_c16(
                root, parent,
                search_seed(
                    root.fixture_ordinal, root.family),
                tie_seed(
                    root.fixture_ordinal, root.family));
        if (captured.stable_id != root.stable_id ||
            captured.family != root.family ||
            captured.candidates.size() !=
                root.candidates.size() ||
            !captured.complete_legal_choice_coverage ||
            !captured.finite_scores) {
            throw std::runtime_error(
                "AQ5 untreated C16 capture is incomplete");
        }
        report.untreated.roots.push_back(
            std::move(captured));
    }
    report.untreated.captured_before_rpi = true;
    if (!report.untreated.gate_passed()) {
        throw std::runtime_error(
            "AQ5 untreated C16 descriptive capture failed");
    }

    for (const PreparedRoot& root : roots) {
        const std::uint64_t root_search_seed =
            search_seed(
                root.fixture_ordinal, root.family);
        const std::uint64_t root_tie_seed =
            tie_seed(
                root.fixture_ordinal, root.family);
        const LearnedSearchConfig config =
            outer_search_config(
                root.family, root_search_seed);

        SamplerOutput direct =
            samplers.score_rpi(root, parent, config);
        const std::string direct_choice =
            selected_key(
                direct.candidates, root_tie_seed);
        SamplerOutput reversed =
            samplers.score_rpi(
                reverse_candidate_order(root),
                parent, config);
        const std::string reversed_choice =
            selected_key(
                reversed.candidates, root_tie_seed);
        const PreparedRoot hidden_root =
            make_hidden_repartition_clone(root);
        SamplerOutput hidden =
            samplers.score_rpi(
                hidden_root, parent, config);
        const std::string hidden_choice =
            selected_key(
                hidden.candidates, root_tie_seed);

        const bool reversed_identical =
            outputs_bit_identical(direct, reversed) &&
            direct_choice == reversed_choice;
        const bool hidden_identical =
            outputs_bit_identical(direct, hidden);
        const bool direct_scores_finite =
            finite_candidate_scores(
                direct.candidates);
        RootReport root_report{
            .stable_id = root.stable_id,
            .family = root.family,
            .candidates =
                std::move(direct.candidates),
            .selected_key = direct_choice,
            .accounting = direct.accounting,
            .complete_legal_choice_coverage =
                direct.legal_choice_count ==
                    root.candidates.size(),
            .rules_settled = direct.rules_settled,
            .finite_scores = direct_scores_finite,
            .accounting_consistent =
                direct.accounting_consistent,
            .reversed_input_action_keyed_bit_identical =
                reversed_identical,
            .hidden_repartition_nonvacuous =
                hidden_root.state != root.state,
            .hidden_observation_bit_identical =
                observe_game_state(
                    hidden_root.state, root.actor) ==
                observe_game_state(
                    root.state, root.actor),
            .hidden_scores_bit_identical =
                hidden_identical,
            .hidden_choice_bit_identical =
                direct_choice == hidden_choice,
            .hidden_accounting_bit_identical =
                direct.accounting == hidden.accounting,
        };
        FixtureReport& fixture =
            report.fixtures[root.fixture_ordinal];
        fixture.roots.push_back(root_report);

        FamilyInvariantReport& family =
            report.families[
                family_index(root.family)];
        ++family.roots;
        family.complete_legal_choice_coverage =
            family.complete_legal_choice_coverage &&
            root_report.complete_legal_choice_coverage;
        family.rules_settled =
            family.rules_settled &&
            root_report.rules_settled;
        family.finite_scores =
            family.finite_scores &&
            root_report.finite_scores;
        family.accounting_consistent =
            family.accounting_consistent &&
            root_report.accounting_consistent;
        family
            .reversed_input_action_keyed_bit_identical =
            family
                .reversed_input_action_keyed_bit_identical &&
            root_report
                .reversed_input_action_keyed_bit_identical;
        family.hidden_repartition_nonvacuous =
            family.hidden_repartition_nonvacuous &&
            root_report.hidden_repartition_nonvacuous;
        family.hidden_observation_bit_identical =
            family.hidden_observation_bit_identical &&
            root_report.hidden_observation_bit_identical;
        family.hidden_scores_bit_identical =
            family.hidden_scores_bit_identical &&
            root_report.hidden_scores_bit_identical;
        family.hidden_choice_bit_identical =
            family.hidden_choice_bit_identical &&
            root_report.hidden_choice_bit_identical;
        family.hidden_accounting_bit_identical =
            family.hidden_accounting_bit_identical &&
            root_report.hidden_accounting_bit_identical;
        family.maximum_active_nesting =
            std::max(
                family.maximum_active_nesting,
                root_report.accounting
                    .inner_search_max_depth);
        family.one_level_nesting_bounded =
            family.one_level_nesting_bounded &&
            root_report.accounting
                    .inner_search_max_depth <=
                kMaximumActiveNesting;
    }

    for (FixtureReport& fixture : report.fixtures) {
        fixture.direction_passed =
            fixture_direction_passed(
                fixture.spec, fixture.roots);
    }
    for (FamilyInvariantReport& family :
         report.families) {
        family.one_level_nesting_bounded =
            family.one_level_nesting_bounded &&
            family.maximum_active_nesting ==
                kMaximumActiveNesting;
    }

    const BotConfig untreated_default;
    const BotConfig treatment =
        treatment_bot_config(parent);
    const SealedRecipe recipe = sealed_recipe();
    const LearnedSearchConfig priority =
        outer_search_config(
            DecisionFamily::Priority,
            search_seed(
                0, DecisionFamily::Priority));
    const LearnedSearchConfig combat =
        outer_search_config(
            DecisionFamily::Attack,
            search_seed(
                6, DecisionFamily::Attack));
    report.isolation = {
        .exact_parent =
            report.parent_fingerprint ==
                kRequiredParentFingerprint,
        .treatment_default_off =
            !untreated_default
                 .value_recursive_policy_improvement,
        .exact_configuration =
            search_config_matches_budget(
                priority, recipe.priority_outer) &&
            search_config_matches_budget(
                combat, recipe.combat_outer),
        .all_other_treatments_off =
            treatment_config_exact(treatment),
        .treatment_off_fixed_seed_game_bit_identical =
            samplers
                .treatment_off_fixed_seed_game_bit_identical(
                    parent),
    };
    report.hypothesis_passed =
        core_preflight_passed(report);
    return report;
}

PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent) {
    return run_preflight(
        std::move(parent), engine_sampler_api());
}

} // namespace old_school::action_q_recursive_policy_improvement
