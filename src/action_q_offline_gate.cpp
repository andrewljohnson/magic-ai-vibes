#include "old_school/action_q_offline_gate.hpp"

#include "old_school/probes.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::action_q_offline_gate {
namespace {

constexpr std::string_view kOneOpenForceSpikeId =
    "control.blue.force-spike-payable-gray-ogre.v1";
constexpr std::string_view kRedundantCounterId =
    "control.blue.counter-redundant-same-target.v1";
constexpr std::string_view kInterveningCounterId =
    "control.blue.counter-same-target-after-intervening-counter.v1";
constexpr std::string_view kSickBearGrowthId =
    "field.green.second-main-sick-bear-growth.v1";
constexpr std::string_view kOpponentGrowthId =
    "field.green.begin-combat-growth-tapped-air.v1";
constexpr std::string_view kBraingeyserId =
    "control.blue.braingeyser-x0.v1";

std::size_t deck_index(DeckId deck) {
    const std::size_t result =
        static_cast<std::size_t>(deck);
    if (result >= kDeckCount) {
        throw std::invalid_argument(
            "AQ0 offline gate encountered an invalid deck");
    }
    return result;
}

void require_model(
    const std::shared_ptr<const LearnedModel>& model,
    std::string_view description) {
    if (!model) {
        throw std::invalid_argument(
            std::string(description) + " model is null");
    }
}

bool double_bit_identical(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

bool contains(
    const std::vector<std::string>& values,
    std::string_view wanted) {
    return std::find(values.begin(), values.end(), wanted) !=
           values.end();
}

bool exact_selection(
    const std::vector<std::string>& selected,
    std::string_view wanted) {
    return selected.size() == 1 &&
           selected.front() == wanted;
}

bool unique_nonempty_keys(
    const std::vector<std::string>& keys) {
    if (keys.empty() ||
        std::any_of(
            keys.begin(), keys.end(),
            [](const std::string& key) {
                return key.empty();
            })) {
        return false;
    }
    std::vector<std::string> sorted = keys;
    std::sort(sorted.begin(), sorted.end());
    return std::adjacent_find(
               sorted.begin(), sorted.end()) ==
           sorted.end();
}

bool frozen_cache_snapshot_exact(
    const artifact_integrity::RegularFileSnapshot& snapshot) {
    return snapshot.byte_size == kFrozenDevCacheBytes &&
           snapshot.sha256 == kFrozenDevCacheSha256;
}

bool report_metadata_exact(const Report& report) {
    return !report.parent_fingerprint.empty() &&
           !report.candidate_fingerprint.empty() &&
           report.behavior.force_spike.model_fingerprint ==
               report.candidate_fingerprint &&
           report.behavior.force_spike.worlds ==
               action_q_explore::kWorlds &&
           report.behavior.force_spike.horizon_turns ==
               action_q_explore::kHorizonTurns &&
           report.behavior.force_spike
                   .value_priority_residual_weight ==
               action_q_explore::
                   kCandidateResidualWeight &&
           !report.behavior.force_spike
                .value_pass_dominance &&
           report.behavior.force_spike
                   .value_continuation_controller ==
               LearnedContinuationController::Legacy;
}

std::vector<PriorityAction> expected_ancestral_legal_actions() {
    return {
        PriorityAction::pass(),
        PriorityAction::play_land(CardId::Island),
        PriorityAction::cast_artifact(CardId::SolRing),
        PriorityAction::cast_ancestral_recall(
            Target::player_target(0)),
        PriorityAction::cast_ancestral_recall(
            Target::player_target(1)),
    };
}

bool support_is_subset(
    const std::vector<PriorityAction>& support,
    const std::vector<PriorityAction>& legal) {
    if (support.empty()) {
        return false;
    }
    for (std::size_t index = 0;
         index < support.size(); ++index) {
        if (std::find(
                legal.begin(), legal.end(),
                support[index]) == legal.end() ||
            std::find(
                support.begin(), support.begin() +
                    static_cast<std::ptrdiff_t>(index),
                support[index]) !=
                support.begin() +
                    static_cast<std::ptrdiff_t>(index)) {
            return false;
        }
    }
    return true;
}

bool probes_equal(
    const probes::DecisionProbe& left,
    const probes::DecisionProbe& right) {
    return left.stable_id == right.stable_id &&
           left.category == right.category &&
           left.decision_kind == right.decision_kind &&
           left.root_deck == right.root_deck &&
           left.opponent_deck == right.opponent_deck &&
           left.root_player == right.root_player &&
           left.phase == right.phase &&
           left.consecutive_passes ==
               right.consecutive_passes &&
           left.state == right.state &&
           left.original_decks == right.original_decks &&
           left.candidates == right.candidates &&
           left.harvest == right.harvest;
}

const probes::DecisionProbe& unique_probe(
    const std::vector<probes::DecisionProbe>& probes,
    std::string_view stable_id) {
    const auto first = std::find_if(
        probes.begin(), probes.end(),
        [stable_id](const probes::DecisionProbe& probe) {
            return probe.stable_id == stable_id;
        });
    if (first == probes.end() ||
        std::find_if(
            first + 1, probes.end(),
            [stable_id](const probes::DecisionProbe& probe) {
                return probe.stable_id == stable_id;
            }) != probes.end()) {
        throw std::runtime_error(
            "AQ0 offline gate requires exactly one fixture " +
            std::string(stable_id));
    }
    return *first;
}

void require_no_validation_errors(
    const std::vector<std::string>& errors,
    std::string_view corpus) {
    if (!errors.empty()) {
        throw std::runtime_error(
            std::string(corpus) +
            " failed validation: " + errors.front());
    }
}

probe_runner::NamedValueScoringModel scoring_model(
    std::string name,
    std::shared_ptr<const LearnedModel> model,
    double residual_weight) {
    return {
        .name = std::move(name),
        .model = std::move(model),
        .transition_family = {},
        .value_priority_residual_weight = residual_weight,
        .value_pass_dominance = false,
        .value_continuation_controller =
            LearnedContinuationController::Legacy,
    };
}

std::vector<std::string> sorted_keys(
    std::vector<std::string> keys) {
    std::sort(keys.begin(), keys.end());
    return keys;
}

struct KeyedDiagnostic {
    std::vector<probe_eval::PolicyScore> raw;
    std::vector<probe_eval::PolicyScore> deployed;
    std::vector<std::string> selected;
};

void sort_scores(
    std::vector<probe_eval::PolicyScore>& scores) {
    std::sort(
        scores.begin(), scores.end(),
        [](const auto& left, const auto& right) {
            return left.key < right.key;
        });
}

KeyedDiagnostic canonical_diagnostic(
    probe_runner::ValueProbeDeploymentDiagnostic diagnostic) {
    sort_scores(diagnostic.raw_candidate_q);
    sort_scores(diagnostic.deployed_policy_scores);
    return {
        .raw = std::move(diagnostic.raw_candidate_q),
        .deployed =
            std::move(diagnostic.deployed_policy_scores),
        .selected =
            sorted_keys(std::move(diagnostic.selected_keys)),
    };
}

bool keyed_scores_bit_identical(
    const std::vector<probe_eval::PolicyScore>& left,
    const std::vector<probe_eval::PolicyScore>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.size(); ++index) {
        if (left[index].key != right[index].key ||
            !double_bit_identical(
                left[index].score, right[index].score)) {
            return false;
        }
    }
    return true;
}

bool keyed_diagnostics_score_identical(
    const KeyedDiagnostic& left,
    const KeyedDiagnostic& right) {
    return keyed_scores_bit_identical(left.raw, right.raw) &&
           keyed_scores_bit_identical(
               left.deployed, right.deployed);
}

KeyedDiagnostic score_probe(
    const probes::DecisionProbe& probe,
    const probe_runner::NamedValueScoringModel& scoring,
    std::string_view corpus_id) {
    return canonical_diagnostic(
        probe_runner::diagnose_value_probe_deployment(
            probe, scoring, corpus_id,
            action_q_explore::kWorlds, 0.0));
}

struct OrderCheck {
    bool scores = true;
    bool support = true;
};

struct HiddenCheck {
    bool distinct_owner_equivalent = false;
    bool scores = false;
    bool support = false;
};

OrderCheck descriptor_order_check(
    const probes::DecisionProbe& probe,
    const probe_runner::NamedValueScoringModel& scoring,
    std::string_view corpus_id) {
    probes::DecisionProbe reversed = probe;
    std::reverse(
        reversed.candidates.begin(),
        reversed.candidates.end());
    const probes::Validation validation =
        probes::validate_probe(reversed);
    if (!validation.ok()) {
        throw std::runtime_error(
            probe.stable_id +
            ": reversed descriptor order is not a valid probe");
    }
    const KeyedDiagnostic original =
        score_probe(probe, scoring, corpus_id);
    const KeyedDiagnostic reordered =
        score_probe(reversed, scoring, corpus_id);
    return {
        .scores =
            keyed_diagnostics_score_identical(
                original, reordered),
        .support =
            original.selected == reordered.selected,
    };
}

HiddenCheck hidden_repartition_check(
    const probes::DecisionProbe& probe,
    const probe_runner::NamedValueScoringModel& scoring,
    std::string_view corpus_id) {
    probes::DecisionProbe hidden = probe;
    hidden.state =
        probe_runner::hidden_repartition_clone(probe);
    const probes::Validation validation =
        probes::validate_probe(hidden);
    if (!validation.ok()) {
        throw std::runtime_error(
            probe.stable_id +
            ": hidden repartition is not a valid probe");
    }
    const bool distinct_owner_equivalent =
        hidden.state != probe.state &&
        observe_game_state(
            hidden.state, hidden.root_player) ==
            observe_game_state(
                probe.state, probe.root_player) &&
        hidden.candidates == probe.candidates &&
        hidden.original_decks == probe.original_decks;
    const KeyedDiagnostic original =
        score_probe(probe, scoring, corpus_id);
    const KeyedDiagnostic repartitioned =
        score_probe(hidden, scoring, corpus_id);
    return {
        .distinct_owner_equivalent =
            distinct_owner_equivalent,
        .scores =
            keyed_diagnostics_score_identical(
                original, repartitioned),
        .support =
            original.selected ==
            repartitioned.selected,
    };
}

const probe_eval::PairLabel* unique_pair(
    const probe_eval::ProbeLabel& label,
    std::string_view first, std::string_view second) {
    const probe_eval::PairLabel* result = nullptr;
    for (const probe_eval::PairLabel& pair : label.pairs) {
        if ((pair.first == first && pair.second == second) ||
            (pair.first == second && pair.second == first)) {
            if (result != nullptr) {
                return nullptr;
            }
            result = &pair;
        }
    }
    return result;
}

bool stable_positive_pair(
    const probe_eval::ProbeLabel& label,
    std::string_view best, std::string_view outside) {
    const probe_eval::PairLabel* pair =
        unique_pair(label, best, outside);
    if (pair == nullptr) {
        return false;
    }
    const double oriented_delta =
        pair->first == best
            ? pair->delta_q
            : -pair->delta_q;
    return oriented_delta > 0.0 &&
           oriented_delta >=
               probe_eval::kStablePairMinimumDelta &&
           oriented_delta >
               probe_eval::kNormal95CriticalValue *
                   pair->paired_standard_error;
}

bool is_stable_best_set(
    const probe_eval::ProbeLabel& label) {
    std::vector<std::string_view> outside;
    for (const auto& candidate : label.candidates) {
        if (!contains(
                label.reference_best_set,
                candidate.key)) {
            outside.push_back(candidate.key);
        }
    }
    if (outside.empty()) {
        return false;
    }
    return std::any_of(
        label.reference_best_set.begin(),
        label.reference_best_set.end(),
        [&label, &outside](const std::string& best) {
            return std::all_of(
                outside.begin(), outside.end(),
                [&label, &best](std::string_view other) {
                    return stable_positive_pair(
                        label, best, other);
                });
        });
}

const probe_runner::ValueProbeDecisionDetail& unique_decision(
    const std::vector<
        probe_runner::ValueProbeDecisionDetail>& decisions,
    std::string_view stable_id) {
    const auto first = std::find_if(
        decisions.begin(), decisions.end(),
        [stable_id](const auto& decision) {
            return decision.stable_id == stable_id;
        });
    if (first == decisions.end() ||
        std::find_if(
            first + 1, decisions.end(),
            [stable_id](const auto& decision) {
                return decision.stable_id == stable_id;
            }) != decisions.end()) {
        throw std::runtime_error(
            "AQ0 frozen scorer requires exactly one decision " +
            std::string(stable_id));
    }
    return *first;
}

bool selection_intersects(
    const std::vector<std::string>& selection,
    const std::vector<std::string>& reference) {
    return std::any_of(
        selection.begin(), selection.end(),
        [&reference](const std::string& key) {
            return contains(reference, key);
        });
}

std::vector<probe_eval::ProbeLabel> load_frozen_labels() {
    const std::vector<probes::DecisionProbe> corpus =
        probes::make_probe_dev_v3();
    require_no_validation_errors(
        probes::validate_probe_dev_v3(corpus),
        probes::kProbeDevV3);
    const probe_runner::ProbeScoreConfig config{
        .training_games = 800,
        .training_seed = 424242,
        .reference_worlds = kFrozenDevWorlds,
        .reference_horizon_turns =
            kFrozenDevHorizonTurns,
        .reference_rollouts_per_world = 1,
        .scoring_value_worlds =
            action_q_explore::kWorlds,
        .scoring_value_continuation_epsilon = 0.0,
        .cache_path =
            std::filesystem::path(kFrozenDevCachePath),
        .refresh_cache = false,
    };
    const probe_runner::ProbeCacheMetadata metadata =
        probe_runner::make_probe_cache_metadata(
            probe_runner::ProbeCorpusKind::DevV3,
            config, corpus, kFrozenDevActorFingerprint);
    return probe_runner::load_probe_label_cache(
        probe_runner::ProbeCorpusKind::DevV3,
        config.cache_path, metadata, corpus);
}

std::vector<PriorityAction> exact_priority_support(
    const LearnedValuePriorityDiagnostic& diagnostic) {
    if (diagnostic.actions.empty() ||
        diagnostic.actions.size() !=
            diagnostic.scores.size()) {
        throw std::runtime_error(
            "AQ0 Ancestral diagnostic has invalid score rows");
    }
    const double maximum =
        *std::max_element(
            diagnostic.scores.begin(),
            diagnostic.scores.end());
    std::vector<PriorityAction> result;
    for (std::size_t index = 0;
         index < diagnostic.actions.size(); ++index) {
        if (diagnostic.scores[index] == maximum) {
            result.push_back(diagnostic.actions[index]);
        }
    }
    return result;
}

double score_for(
    const LearnedValuePriorityDiagnostic& diagnostic,
    const PriorityAction& wanted) {
    std::optional<double> result;
    for (std::size_t index = 0;
         index < diagnostic.actions.size(); ++index) {
        if (diagnostic.actions[index] == wanted) {
            if (result.has_value()) {
                throw std::runtime_error(
                    "AQ0 Ancestral diagnostic duplicated an action");
            }
            result = diagnostic.scores[index];
        }
    }
    if (!result.has_value()) {
        throw std::runtime_error(
            "AQ0 Ancestral diagnostic omitted an action");
    }
    return *result;
}

bool priority_diagnostic_bit_identical(
    const LearnedValuePriorityDiagnostic& left,
    const LearnedValuePriorityDiagnostic& right) {
    if (left.legal_actions != right.legal_actions ||
        left.actions != right.actions ||
        left.pass_dominated_actions !=
            right.pass_dominated_actions ||
        left.sampled_worlds != right.sampled_worlds ||
        left.rollout_evaluations !=
            right.rollout_evaluations ||
        left.value_resolved_shallow_prior_weight !=
            right.value_resolved_shallow_prior_weight ||
        left.base_scores.size() !=
            right.base_scores.size() ||
        left.policy_logits.size() !=
            right.policy_logits.size() ||
        left.centered_policy_logits.size() !=
            right.centered_policy_logits.size() ||
        left.priority_residuals.size() !=
            right.priority_residuals.size() ||
        left.scores.size() != right.scores.size()) {
        return false;
    }
    const auto rows_equal =
        [](const std::vector<double>& a,
           const std::vector<double>& b) {
            for (std::size_t index = 0;
                 index < a.size(); ++index) {
                if (!double_bit_identical(
                        a[index], b[index])) {
                    return false;
                }
            }
            return true;
        };
    return rows_equal(left.base_scores, right.base_scores) &&
           rows_equal(left.policy_logits, right.policy_logits) &&
           rows_equal(
               left.centered_policy_logits,
               right.centered_policy_logits) &&
           rows_equal(
               left.priority_residuals,
               right.priority_residuals) &&
           rows_equal(left.scores, right.scores) &&
           exact_priority_support(left) ==
               exact_priority_support(right);
}

std::vector<probes::DecisionProbe> focused_priority_probes() {
    const auto force =
        probes::make_force_spike_policy_controls_v1();
    require_no_validation_errors(
        probes::validate_force_spike_policy_controls_v1(force),
        probes::kForceSpikePolicyControlsV1);
    const auto counters =
        probes::make_counter_composition_controls_v1();
    require_no_validation_errors(
        probes::validate_counter_composition_controls_v1(counters),
        probes::kCounterCompositionControlsV1);
    const auto fields =
        probes::make_field_regressions_v1();
    require_no_validation_errors(
        probes::validate_field_regressions_v1(fields),
        probes::kFieldRegressionsV1);
    const auto braingeyser =
        probes::make_braingeyser_x_zero_control_v1();
    require_no_validation_errors(
        probes::validate_braingeyser_x_zero_control_v1(
            braingeyser),
        probes::kBraingeyserXZeroControlV1);

    std::vector<probes::DecisionProbe> result = force;
    result.push_back(
        make_five_open_force_spike_control());
    result.insert(
        result.end(), counters.begin(), counters.end());
    result.push_back(unique_probe(fields, kSickBearGrowthId));
    result.push_back(unique_probe(fields, kOpponentGrowthId));
    result.insert(
        result.end(), braingeyser.begin(), braingeyser.end());
    return result;
}

bool selected_x_zero_braingeyser(
    const probes::DecisionProbe& probe,
    const std::vector<std::string>& selected) {
    for (const std::string& key : selected) {
        const auto candidate = std::find_if(
            probe.candidates.begin(),
            probe.candidates.end(),
            [&key](const probes::Candidate& value) {
                return value.descriptor == key;
            });
        if (candidate == probe.candidates.end()) {
            throw std::runtime_error(
                "AQ0 Braingeyser selection has an unknown key");
        }
        const auto* action =
            std::get_if<PriorityAction>(
                &candidate->action);
        if (action != nullptr &&
            action->kind ==
                PriorityActionKind::CastBraingeyser &&
            action->x_value == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string ancestral_information_action_fingerprint(
    const action_q_field_gate::AncestralFieldRoot& root) {
    if (!root.context.valid ||
        root.context.decision_player != root.actor ||
        root.actor >= 2 ||
        legal_priority_actions(
            root.state, root.actor,
            root.context.sorcery_actions) !=
            root.legal_actions) {
        throw std::invalid_argument(
            "Ancestral fingerprint requires a complete valid root");
    }
    probes::DecisionProbe probe{
        .stable_id =
            std::string(action_q_field_gate::kStableId),
        .category = probes::Category::BlueCounterWar,
        .decision_kind = probes::DecisionKind::Priority,
        .root_deck = DeckId::Blue,
        .opponent_deck = DeckId::Red,
        .root_player = root.actor,
        .phase = root.context.phase,
        .consecutive_passes =
            root.context.consecutive_passes,
        .state = root.state,
        .original_decks = root.original_decks,
    };
    probe.candidates.reserve(root.legal_actions.size());
    for (const PriorityAction& action :
         root.legal_actions) {
        probe.candidates.push_back({
            .descriptor =
                probes::stable_priority_action_descriptor(
                    action),
            .action = action,
        });
    }
    const probes::Validation validation =
        probes::validate_probe(probe);
    if (!validation.ok()) {
        throw std::invalid_argument(
            "Ancestral fingerprint probe is invalid");
    }
    return probes::bsr_information_action_fingerprint(
        probe);
}

bool IsolationGate::gate_passed() const {
    return parent_identity_exact &&
           candidate_identity_exact &&
           parent_immutable &&
           repeated_fit_bit_identical &&
           only_priority_component_changed;
}

bool CheckGate::gate_passed() const {
    const bool raw_regret_improved =
        std::isfinite(parent.equal_deck_mean_regret) &&
        std::isfinite(candidate.equal_deck_mean_regret) &&
        candidate.equal_deck_mean_regret <
            parent.equal_deck_mean_regret;
    const bool raw_top_one_not_lower =
        std::isfinite(
            parent.equal_deck_top_one_expected_agreement) &&
        std::isfinite(
            candidate.equal_deck_top_one_expected_agreement) &&
        candidate.equal_deck_top_one_expected_agreement >=
            parent.equal_deck_top_one_expected_agreement;
    if (!metrics_match_fit_report ||
        !regret_strictly_improved ||
        regret_strictly_improved != raw_regret_improved ||
        !top_one_not_lower ||
        top_one_not_lower != raw_top_one_not_lower) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& parent_deck = parent.decks[deck];
        const auto& candidate_deck =
            candidate.decks[deck];
        const bool raw_guard =
            parent_deck.deck ==
                static_cast<DeckId>(deck) &&
            candidate_deck.deck ==
                static_cast<DeckId>(deck) &&
            parent_deck.roots != 0 &&
            candidate_deck.roots ==
                parent_deck.roots &&
            std::isfinite(parent_deck.mean_regret) &&
            std::isfinite(candidate_deck.mean_regret) &&
            candidate_deck.mean_regret <=
                parent_deck.mean_regret +
                    kMaximumCheckDeckRegretIncrease;
        if (!deck_regret_guard[deck] ||
            deck_regret_guard[deck] != raw_guard) {
            return false;
        }
    }
    return true;
}

bool FrozenDevGate::gate_passed() const {
    return labels == kFrozenDevProbeCount &&
           std::all_of(
               labels_by_deck.begin(),
               labels_by_deck.end(),
               [](std::size_t count) {
                   return count ==
                          kFrozenDevProbesPerDeck;
               }) &&
           stable_parent_agreements > 0 &&
           lost_stable_parent_agreements == 0 &&
           pair_hidden_repartition.passed &&
           pair_hidden_repartition.policy_count == 2 &&
           pair_hidden_repartition.probe_count ==
               kFrozenDevProbeCount &&
           explicit_hidden_repartition.passed &&
           explicit_hidden_repartition.policy_count == 2 &&
           explicit_hidden_repartition.probe_count ==
               kFrozenDevProbeCount &&
           frozen_cache_snapshot_exact(cache_before) &&
           frozen_cache_snapshot_exact(cache_after) &&
           cache_before == cache_after &&
           std::isfinite(parent.mean_regret) &&
           std::isfinite(candidate.mean_regret) &&
           pooled_regret_no_worse &&
           pooled_regret_no_worse ==
               (candidate.mean_regret <=
                parent.mean_regret);
}

bool AncestralGate::gate_passed() const {
    const PriorityAction opponent =
        PriorityAction::cast_ancestral_recall(
            Target::player_target(1));
    const bool raw_self_above =
        std::isfinite(self_score) &&
        std::isfinite(opponent_score) &&
        self_score > opponent_score;
    const bool raw_opponent_absent =
        support_is_subset(
            selected_support, legal_actions) &&
        std::find(
            selected_support.begin(),
            selected_support.end(), opponent) ==
            selected_support.end();
    const bool raw_legal_exact =
        legal_actions ==
            expected_ancestral_legal_actions();
    const bool raw_fingerprint_exact =
        information_action_fingerprint ==
            kAncestralInformationActionFingerprint;
    return hidden_repartition_bit_identical &&
           complete_legal_actions_exact &&
           complete_legal_actions_exact ==
               raw_legal_exact &&
           information_action_fingerprint_exact &&
           information_action_fingerprint_exact ==
               raw_fingerprint_exact &&
           self_strictly_above_opponent &&
           self_strictly_above_opponent ==
               raw_self_above &&
           opponent_absent_from_support &&
           opponent_absent_from_support ==
               raw_opponent_absent;
}

bool DescriptorOrderGate::gate_passed() const {
    return model_count == 2 &&
           probe_count == kFocusedDescriptorProbeCount &&
           hidden_model_count == 2 &&
           hidden_probe_count ==
               kFocusedDescriptorProbeCount &&
           action_keyed_scores_bit_identical &&
           selected_supports_identical &&
           hidden_repartitions_distinct_owner_equivalent &&
           hidden_action_keyed_scores_bit_identical &&
           hidden_selected_supports_identical;
}

bool BehavioralGate::gate_passed() const {
    const bool raw_live =
        force_spike.live_selects_force_spike();
    const bool raw_one_open =
        force_spike.payable_selects_pass();
    const bool raw_five_open =
        unique_nonempty_keys(five_open_selected_keys) &&
        exact_selection(five_open_selected_keys, "pass");
    const bool raw_redundant =
        unique_nonempty_keys(
            redundant_counter_selected_keys) &&
        exact_selection(
            redundant_counter_selected_keys, "pass");
    const bool raw_intervening =
        unique_nonempty_keys(
            intervening_counter_selected_keys) &&
        exact_selection(
            intervening_counter_selected_keys,
            "counter-opponent-counterspell");
    const bool raw_sick =
        unique_nonempty_keys(
            sick_bear_growth_selected_keys) &&
        exact_selection(
            sick_bear_growth_selected_keys, "pass");
    const bool raw_opponent_growth =
        unique_nonempty_keys(
            opponent_growth_selected_keys) &&
        std::all_of(
            opponent_growth_selected_keys.begin(),
            opponent_growth_selected_keys.end(),
            [](const std::string& key) {
                return key == "pass" ||
                       key ==
                           "growth-own-ironroot-treefolk";
            });
    const bool braingeyser_keys_valid =
        unique_nonempty_keys(
            braingeyser_selected_keys) &&
        std::all_of(
            braingeyser_selected_keys.begin(),
            braingeyser_selected_keys.end(),
            [](const std::string& key) {
                return key == "pass" ||
                       key == "braingeyser-x0-self" ||
                       key ==
                           "braingeyser-x0-opponent" ||
                       key == "braingeyser-x1-self" ||
                       key ==
                           "braingeyser-x1-opponent";
            });
    const bool raw_braingeyser =
        braingeyser_keys_valid &&
        !contains(
            braingeyser_selected_keys,
            "braingeyser-x0-self") &&
        !contains(
            braingeyser_selected_keys,
            "braingeyser-x0-opponent");
    return force_spike.hidden_repartition_passed &&
           live_force_spike_preserved &&
           live_force_spike_preserved == raw_live &&
           one_open_payable_selects_pass ==
               raw_one_open &&
           five_open_force_spike_selects_pass &&
           five_open_force_spike_selects_pass ==
               raw_five_open &&
           redundant_counter_selects_pass &&
           redundant_counter_selects_pass ==
               raw_redundant &&
           intervening_counter_selects_opposing_counter &&
           intervening_counter_selects_opposing_counter ==
               raw_intervening &&
           sick_bear_growth_selects_pass &&
           sick_bear_growth_selects_pass == raw_sick &&
           opponent_growth_excluded &&
           opponent_growth_excluded ==
               raw_opponent_growth &&
           braingeyser_x_zero_excluded &&
           braingeyser_x_zero_excluded ==
               raw_braingeyser;
}

bool Report::gate_passed() const {
    return report_metadata_exact(*this) &&
           isolation.gate_passed() &&
           check.gate_passed() &&
           frozen_dev.gate_passed() &&
           ancestral.gate_passed() &&
           descriptor_order.gate_passed() &&
           behavior.gate_passed();
}

std::vector<std::string> Report::failures() const {
    std::vector<std::string> result;
    const auto append =
        [&result](bool passed, std::string text) {
            if (!passed) {
                result.push_back(std::move(text));
            }
        };
    append(
        report_metadata_exact(*this),
        "offline report model/recipe identity gate failed");
    append(
        isolation.gate_passed(),
        "fit repeatability/component-isolation gate failed");
    append(
        check.gate_passed(),
        "held-out CHECK equal-deck gate failed");
    append(
        frozen_dev.gate_passed(),
        "frozen DevV3 regret/agreement gate failed");
    append(
        ancestral.gate_passed(),
        "captured Ancestral target gate failed");
    append(
        descriptor_order.gate_passed(),
        "descriptor-order invariance gate failed");
    append(
        behavior.gate_passed(),
        "focused behavior gate failed");
    return result;
}

probes::DecisionProbe make_five_open_force_spike_control() {
    const auto controls =
        probes::make_force_spike_policy_controls_v1();
    require_no_validation_errors(
        probes::validate_force_spike_policy_controls_v1(
            controls),
        probes::kForceSpikePolicyControlsV1);
    probes::DecisionProbe result =
        unique_probe(controls, kOneOpenForceSpikeId);
    result.stable_id =
        std::string(kFiveOpenForceSpikeId);
    // Player one can have eight lands by its eighth turn. Moving the four
    // extra Mountains from the hidden library keeps exact deck conservation.
    result.state.turn_number = 16;
    PlayerState& opponent = result.state.players[1];
    for (std::size_t extra = 0; extra < 4; ++extra) {
        const auto mountain = std::find(
            opponent.library.begin(),
            opponent.library.end(),
            CardId::Mountain);
        if (mountain == opponent.library.end()) {
            throw std::logic_error(
                "five-open Force Spike control lacks a Mountain");
        }
        opponent.library.erase(mountain);
        opponent.lands.push_back({
            .card = CardId::Mountain,
            .tapped = false,
        });
    }
    const probes::Validation validation =
        probes::validate_probe(result);
    if (!validation.ok()) {
        throw std::logic_error(
            "constructed five-open Force Spike control is invalid: " +
            (validation.errors.empty()
                 ? std::string("unknown validation failure")
                 : validation.errors.front()));
    }
    return result;
}

std::vector<std::string> validate_five_open_force_spike_control(
    const probes::DecisionProbe& probe) {
    std::vector<std::string> errors;
    const probes::DecisionProbe expected =
        make_five_open_force_spike_control();
    if (!probes_equal(probe, expected)) {
        errors.push_back(
            "five-open Force Spike fixture drifted from its "
            "canonical construction");
    }
    const std::size_t open_mountains =
        static_cast<std::size_t>(std::count_if(
            probe.state.players[1].lands.begin(),
            probe.state.players[1].lands.end(),
            [](const LandPermanent& land) {
                return land.card == CardId::Mountain &&
                       !land.tapped;
            }));
    if (open_mountains != 5 ||
        probe.state.players[1].lands.size() != 8 ||
        probe.state.players[1].mana_pool != ManaCost{}) {
        errors.push_back(
            "five-open Force Spike fixture must expose exactly "
            "five open Mountains after casting Gray Ogre");
    }
    const probes::Validation validation =
        probes::validate_probe(probe);
    for (const std::string& error : validation.errors) {
        errors.push_back(
            probe.stable_id + ": " + error);
    }
    return errors;
}

Report evaluate(
    const action_q_explore::Corpus& corpus,
    const action_q_explore::FitReport& fit,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate) {
    require_model(parent, "AQ0 offline parent");
    require_model(candidate, "AQ0 offline candidate");
    action_q_explore::validate_corpus(corpus);

    Report report;
    report.parent_fingerprint =
        learned_model_fingerprint(parent);
    report.candidate_fingerprint =
        learned_model_fingerprint(candidate);

    const LearnedModelComponentFingerprints parent_components =
        learned_model_component_fingerprints(parent);
    const LearnedModelComponentFingerprints candidate_components =
        learned_model_component_fingerprints(candidate);
    report.isolation.parent_identity_exact =
        report.parent_fingerprint ==
            action_q_explore::kRequiredParentFingerprint &&
        corpus.parent_fingerprint ==
            report.parent_fingerprint &&
        fit.parent_fingerprint_before ==
            report.parent_fingerprint &&
        fit.parent_fingerprint_after ==
            report.parent_fingerprint &&
        fit.parent_components == parent_components;
    report.isolation.candidate_identity_exact =
        fit.candidate &&
        learned_model_fingerprint(fit.candidate) ==
            report.candidate_fingerprint &&
        fit.candidate_fingerprint ==
            report.candidate_fingerprint &&
        fit.candidate_components == candidate_components;
    report.isolation.parent_immutable =
        fit.parent_immutable;
    report.isolation.repeated_fit_bit_identical =
        fit.repeated_fit_bit_identical;
    report.isolation.only_priority_component_changed =
        fit.only_priority_component_changed &&
        parent_components.critic ==
            candidate_components.critic &&
        parent_components.attack ==
            candidate_components.attack &&
        parent_components.block ==
            candidate_components.block &&
        parent_components.damage_order ==
            candidate_components.damage_order &&
        parent_components.priority !=
            candidate_components.priority;

    report.check.parent =
        action_q_explore::evaluate(
            corpus.check, parent, 0.0);
    report.check.candidate =
        action_q_explore::evaluate(
            corpus.check, candidate,
            action_q_explore::kCandidateResidualWeight);
    report.check.metrics_match_fit_report =
        report.check.parent == fit.parent_check &&
        report.check.candidate == fit.candidate_check;
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
                    kMaximumCheckDeckRegretIncrease;
    }

    report.frozen_dev.cache_before =
        artifact_integrity::snapshot_regular_file(
            std::filesystem::path(kFrozenDevCachePath));
    if (!frozen_cache_snapshot_exact(
            report.frozen_dev.cache_before)) {
        throw std::runtime_error(
            "AQ0 frozen DevV3 cache identity drifted");
    }
    const std::vector<probe_eval::ProbeLabel> labels =
        load_frozen_labels();
    report.frozen_dev.labels = labels.size();
    for (const probe_eval::ProbeLabel& label : labels) {
        ++report.frozen_dev.labels_by_deck[
            deck_index(label.root_deck)];
    }
    const auto parent_scoring =
        scoring_model("AQ0 parent", parent, 0.0);
    const auto candidate_scoring =
        scoring_model(
            "AQ0 candidate", candidate,
            action_q_explore::kCandidateResidualWeight);
    const auto frozen_pair =
        probe_runner::score_value_probe_pair_against_labels(
            probe_runner::ProbeCorpusKind::DevV3,
            labels, parent_scoring, candidate_scoring,
            action_q_explore::kWorlds, 0.0);
    report.frozen_dev.parent =
        frozen_pair.control.metrics;
    report.frozen_dev.candidate =
        frozen_pair.treatment.metrics;
    report.frozen_dev.pair_hidden_repartition =
        frozen_pair.hidden_repartition;
    report.frozen_dev.explicit_hidden_repartition =
        probe_runner::verify_value_hidden_repartition(
            probe_runner::ProbeCorpusKind::DevV3,
            {parent_scoring, candidate_scoring},
            action_q_explore::kWorlds, 0.0);
    report.frozen_dev.cache_after =
        artifact_integrity::snapshot_regular_file(
            std::filesystem::path(kFrozenDevCachePath));
    if (!frozen_cache_snapshot_exact(
            report.frozen_dev.cache_after) ||
        report.frozen_dev.cache_before !=
            report.frozen_dev.cache_after) {
        throw std::runtime_error(
            "AQ0 frozen DevV3 cache changed while scoring");
    }
    report.frozen_dev.pooled_regret_no_worse =
        report.frozen_dev.candidate.mean_regret <=
        report.frozen_dev.parent.mean_regret;
    for (const probe_eval::ProbeLabel& label : labels) {
        if (!is_stable_best_set(label)) {
            continue;
        }
        const auto& parent_decision =
            unique_decision(
                frozen_pair.control.decisions,
                label.stable_id);
        const auto& candidate_decision =
            unique_decision(
                frozen_pair.treatment.decisions,
                label.stable_id);
        const bool parent_agrees =
            selection_intersects(
                parent_decision.selected_keys,
                label.reference_best_set);
        const bool candidate_agrees =
            selection_intersects(
                candidate_decision.selected_keys,
                label.reference_best_set);
        if (parent_agrees) {
            ++report.frozen_dev
                  .stable_parent_agreements;
            if (!candidate_agrees) {
                ++report.frozen_dev
                      .lost_stable_parent_agreements;
            }
        }
    }

    const auto ancestral =
        action_q_field_gate::make_ancestral_field_root();
    const auto hidden_ancestral =
        action_q_field_gate::hidden_repartition_clone(
            ancestral);
    const action_q_field_gate::LearnedValueScoringRecipe
        candidate_recipe{
            .worlds = action_q_explore::kWorlds,
            .seed =
                action_q_field_gate::kReferenceSearchSeed,
            .continuation_epsilon = 0.0,
            .priority_residual_weight =
                action_q_explore::kCandidateResidualWeight,
            .pass_dominance = false,
            .continuation_controller =
                LearnedContinuationController::Legacy,
            .resolved_shallow_prior_weight = 0.0,
        };
    const LearnedValuePriorityDiagnostic
        ancestral_diagnostic =
            action_q_field_gate::score_learned_value(
                ancestral, candidate, candidate_recipe);
    const LearnedValuePriorityDiagnostic
        hidden_ancestral_diagnostic =
            action_q_field_gate::score_learned_value(
                hidden_ancestral, candidate,
                candidate_recipe);
    const PriorityAction ancestral_self =
        PriorityAction::cast_ancestral_recall(
            Target::player_target(ancestral.actor));
    const PriorityAction ancestral_opponent =
        PriorityAction::cast_ancestral_recall(
            Target::player_target(1 - ancestral.actor));
    report.ancestral.self_score =
        score_for(
            ancestral_diagnostic, ancestral_self);
    report.ancestral.opponent_score =
        score_for(
            ancestral_diagnostic,
            ancestral_opponent);
    report.ancestral.legal_actions =
        ancestral_diagnostic.legal_actions;
    report.ancestral.selected_support =
        exact_priority_support(
            ancestral_diagnostic);
    report.ancestral.information_action_fingerprint =
        ancestral_information_action_fingerprint(
            ancestral);
    report.ancestral.complete_legal_actions_exact =
        report.ancestral.legal_actions ==
            expected_ancestral_legal_actions() &&
        ancestral.legal_actions ==
            expected_ancestral_legal_actions() &&
        ancestral_diagnostic.actions ==
            ancestral.legal_actions;
    report.ancestral
        .information_action_fingerprint_exact =
        report.ancestral
                .information_action_fingerprint ==
            kAncestralInformationActionFingerprint;
    report.ancestral
        .hidden_repartition_bit_identical =
        priority_diagnostic_bit_identical(
            ancestral_diagnostic,
            hidden_ancestral_diagnostic);
    report.ancestral.self_strictly_above_opponent =
        report.ancestral.self_score >
        report.ancestral.opponent_score;
    report.ancestral.opponent_absent_from_support =
        std::find(
            report.ancestral.selected_support.begin(),
            report.ancestral.selected_support.end(),
            ancestral_opponent) ==
        report.ancestral.selected_support.end();

    const std::vector<probes::DecisionProbe> focused =
        focused_priority_probes();
    if (focused.size() !=
        kFocusedDescriptorProbeCount) {
        throw std::logic_error(
            "AQ0 focused descriptor-order census drifted");
    }
    report.descriptor_order.model_count = 2;
    report.descriptor_order.probe_count =
        focused.size();
    report.descriptor_order.hidden_model_count = 2;
    report.descriptor_order.hidden_probe_count =
        focused.size();
    report.descriptor_order
        .action_keyed_scores_bit_identical = true;
    report.descriptor_order
        .selected_supports_identical = true;
    report.descriptor_order
        .hidden_repartitions_distinct_owner_equivalent =
        true;
    report.descriptor_order
        .hidden_action_keyed_scores_bit_identical = true;
    report.descriptor_order
        .hidden_selected_supports_identical = true;
    for (const auto& scoring :
         {parent_scoring, candidate_scoring}) {
        for (const probes::DecisionProbe& probe :
             focused) {
            const OrderCheck checked =
                descriptor_order_check(
                    probe, scoring,
                    "old-school-action-q-offline-focused-v1");
            report.descriptor_order
                .action_keyed_scores_bit_identical =
                report.descriptor_order
                    .action_keyed_scores_bit_identical &&
                checked.scores;
            report.descriptor_order
                .selected_supports_identical =
                report.descriptor_order
                    .selected_supports_identical &&
                checked.support;
            const HiddenCheck hidden_checked =
                hidden_repartition_check(
                    probe, scoring,
                    "old-school-action-q-offline-focused-v1");
            report.descriptor_order
                .hidden_repartitions_distinct_owner_equivalent =
                report.descriptor_order
                    .hidden_repartitions_distinct_owner_equivalent &&
                hidden_checked.distinct_owner_equivalent;
            report.descriptor_order
                .hidden_action_keyed_scores_bit_identical =
                report.descriptor_order
                    .hidden_action_keyed_scores_bit_identical &&
                hidden_checked.scores;
            report.descriptor_order
                .hidden_selected_supports_identical =
                report.descriptor_order
                    .hidden_selected_supports_identical &&
                hidden_checked.support;
        }
    }

    report.behavior.force_spike =
        probe_runner::
            score_value_force_spike_policy_controls(
                candidate, "AQ0 candidate",
                action_q_explore::kWorlds, 0.0,
                action_q_explore::
                    kCandidateResidualWeight,
                false,
                LearnedContinuationController::Legacy);
    report.behavior.live_force_spike_preserved =
        report.behavior.force_spike
            .live_selects_force_spike();
    report.behavior.one_open_payable_selects_pass =
        report.behavior.force_spike
            .payable_selects_pass();

    const probes::DecisionProbe& five_open =
        unique_probe(focused, kFiveOpenForceSpikeId);
    require_no_validation_errors(
        validate_five_open_force_spike_control(
            five_open),
        kFiveOpenForceSpikeId);
    report.behavior.five_open_selected_keys =
        score_probe(
            five_open, candidate_scoring,
            kFiveOpenForceSpikeCorpusId)
            .selected;
    report.behavior
        .five_open_force_spike_selects_pass =
        exact_selection(
            report.behavior
                .five_open_selected_keys,
            "pass");

    const auto score_focused =
        [&focused, &candidate_scoring](
            std::string_view stable_id) {
            return score_probe(
                unique_probe(focused, stable_id),
                candidate_scoring,
                "old-school-action-q-offline-focused-v1")
                .selected;
        };
    report.behavior
        .redundant_counter_selected_keys =
        score_focused(kRedundantCounterId);
    report.behavior
        .redundant_counter_selects_pass =
        exact_selection(
            report.behavior
                .redundant_counter_selected_keys,
            "pass");
    report.behavior
        .intervening_counter_selected_keys =
        score_focused(kInterveningCounterId);
    report.behavior
        .intervening_counter_selects_opposing_counter =
        exact_selection(
            report.behavior
                .intervening_counter_selected_keys,
            "counter-opponent-counterspell");
    report.behavior
        .sick_bear_growth_selected_keys =
        score_focused(kSickBearGrowthId);
    report.behavior
        .sick_bear_growth_selects_pass =
        exact_selection(
            report.behavior
                .sick_bear_growth_selected_keys,
            "pass");
    report.behavior
        .opponent_growth_selected_keys =
        score_focused(kOpponentGrowthId);
    report.behavior.opponent_growth_excluded =
        !report.behavior
             .opponent_growth_selected_keys.empty() &&
        !contains(
            report.behavior
                .opponent_growth_selected_keys,
            "growth-opponent-tapped-air-elemental");
    report.behavior.braingeyser_selected_keys =
        score_focused(kBraingeyserId);
    report.behavior.braingeyser_x_zero_excluded =
        !report.behavior
             .braingeyser_selected_keys.empty() &&
        !selected_x_zero_braingeyser(
            unique_probe(focused, kBraingeyserId),
            report.behavior
                .braingeyser_selected_keys);

    return report;
}

} // namespace old_school::action_q_offline_gate
