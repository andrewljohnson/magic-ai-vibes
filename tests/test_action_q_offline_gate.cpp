#include "old_school/action_q_offline_gate.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace gate = old_school::action_q_offline_gate;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void fill_passing_report(gate::Report& report) {
    report.parent_fingerprint = "parent";
    report.candidate_fingerprint = "candidate";
    report.isolation = {
        .parent_identity_exact = true,
        .candidate_identity_exact = true,
        .parent_immutable = true,
        .repeated_fit_bit_identical = true,
        .only_priority_component_changed = true,
    };
    report.check.metrics_match_fit_report = true;
    report.check.regret_strictly_improved = true;
    report.check.top_one_not_lower = true;
    report.check.deck_regret_guard.fill(true);
    report.check.parent.equal_deck_mean_regret = 0.2;
    report.check.candidate.equal_deck_mean_regret = 0.1;
    report.check.parent
        .equal_deck_top_one_expected_agreement = 0.5;
    report.check.candidate
        .equal_deck_top_one_expected_agreement = 0.6;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        report.check.parent.decks[deck].deck =
            static_cast<old_school::DeckId>(deck);
        report.check.candidate.decks[deck].deck =
            static_cast<old_school::DeckId>(deck);
        report.check.parent.decks[deck].roots = 1;
        report.check.candidate.decks[deck].roots = 1;
        report.check.parent.decks[deck].mean_regret = 0.2;
        report.check.candidate.decks[deck].mean_regret = 0.1;
    }
    report.frozen_dev.labels =
        gate::kFrozenDevProbeCount;
    report.frozen_dev.labels_by_deck.fill(
        gate::kFrozenDevProbesPerDeck);
    report.frozen_dev.stable_parent_agreements = 1;
    report.frozen_dev
        .lost_stable_parent_agreements = 0;
    report.frozen_dev
        .pair_hidden_repartition.passed = true;
    report.frozen_dev
        .pair_hidden_repartition.policy_count = 2;
    report.frozen_dev
        .pair_hidden_repartition.probe_count =
        gate::kFrozenDevProbeCount;
    report.frozen_dev
        .explicit_hidden_repartition.passed = true;
    report.frozen_dev
        .explicit_hidden_repartition.policy_count = 2;
    report.frozen_dev
        .explicit_hidden_repartition.probe_count =
        gate::kFrozenDevProbeCount;
    report.frozen_dev.pooled_regret_no_worse = true;
    report.frozen_dev.parent.mean_regret = 0.2;
    report.frozen_dev.candidate.mean_regret = 0.1;
    report.frozen_dev.cache_before.byte_size =
        gate::kFrozenDevCacheBytes;
    report.frozen_dev.cache_before.sha256 =
        gate::kFrozenDevCacheSha256;
    report.frozen_dev.cache_after =
        report.frozen_dev.cache_before;
    report.ancestral.self_score = 0.8;
    report.ancestral.opponent_score = 0.2;
    report.ancestral.legal_actions = {
        old_school::PriorityAction::pass(),
        old_school::PriorityAction::play_land(
            old_school::CardId::Island),
        old_school::PriorityAction::cast_artifact(
            old_school::CardId::SolRing),
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(0)),
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(1)),
    };
    report.ancestral.selected_support = {
        old_school::PriorityAction::cast_ancestral_recall(
            old_school::Target::player_target(0)),
    };
    report.ancestral.information_action_fingerprint =
        gate::kAncestralInformationActionFingerprint;
    report.ancestral.complete_legal_actions_exact = true;
    report.ancestral
        .information_action_fingerprint_exact = true;
    report.ancestral.hidden_repartition_bit_identical =
        true;
    report.ancestral.self_strictly_above_opponent = true;
    report.ancestral.opponent_absent_from_support = true;
    report.descriptor_order.model_count = 2;
    report.descriptor_order.probe_count =
        gate::kFocusedDescriptorProbeCount;
    report.descriptor_order.hidden_model_count = 2;
    report.descriptor_order.hidden_probe_count =
        gate::kFocusedDescriptorProbeCount;
    report.descriptor_order
        .action_keyed_scores_bit_identical = true;
    report.descriptor_order
        .selected_supports_identical = true;
    report.descriptor_order
        .hidden_repartitions_distinct_owner_equivalent = true;
    report.descriptor_order
        .hidden_action_keyed_scores_bit_identical = true;
    report.descriptor_order
        .hidden_selected_supports_identical = true;
    report.behavior.live_force_spike_preserved = true;
    report.behavior.force_spike.model_fingerprint =
        report.candidate_fingerprint;
    report.behavior.force_spike.worlds =
        old_school::action_q_explore::kWorlds;
    report.behavior.force_spike.horizon_turns =
        old_school::action_q_explore::kHorizonTurns;
    report.behavior.force_spike
        .value_priority_residual_weight =
        old_school::action_q_explore::
            kCandidateResidualWeight;
    report.behavior.force_spike
        .value_continuation_controller =
        old_school::LearnedContinuationController::Legacy;
    report.behavior.force_spike
        .hidden_repartition_passed = true;
    report.behavior.force_spike.live.selected_keys = {
        "force-spike-gray-ogre",
    };
    report.behavior.force_spike.payable.selected_keys = {
        "force-spike-gray-ogre",
    };
    report.behavior.one_open_payable_selects_pass = false;
    report.behavior.five_open_selected_keys = {"pass"};
    report.behavior.five_open_force_spike_selects_pass =
        true;
    report.behavior.redundant_counter_selected_keys = {
        "pass",
    };
    report.behavior.redundant_counter_selects_pass = true;
    report.behavior.intervening_counter_selected_keys = {
        "counter-opponent-counterspell",
    };
    report.behavior
        .intervening_counter_selects_opposing_counter = true;
    report.behavior.sick_bear_growth_selected_keys = {
        "pass",
    };
    report.behavior.sick_bear_growth_selects_pass = true;
    report.behavior.opponent_growth_selected_keys = {
        "pass",
    };
    report.behavior.opponent_growth_excluded = true;
    report.behavior.braingeyser_selected_keys = {
        "braingeyser-x1-self",
    };
    report.behavior.braingeyser_x_zero_excluded = true;
}

void test_report_gate_is_conjunctive_and_one_open_is_descriptive() {
    gate::Report report;
    fill_passing_report(report);
    expect(
        report.gate_passed() && report.failures().empty(),
        "descriptive one-open result incorrectly failed AQ0");

    report.behavior.five_open_force_spike_selects_pass =
        false;
    expect(
        !report.gate_passed() &&
            report.failures().size() == 1 &&
            report.failures().front() ==
                "focused behavior gate failed",
        "five-open hard gate is not conjunctive");
}

void test_gate_rejects_boolean_evidence_mismatch() {
    gate::Report report;
    fill_passing_report(report);
    report.behavior.redundant_counter_selected_keys = {
        "counter-same-air-elemental",
    };
    expect(
        !report.gate_passed(),
        "behavior gate trusted a stale derived boolean");

    fill_passing_report(report);
    report.ancestral.self_score = 0.1;
    expect(
        !report.gate_passed(),
        "Ancestral gate trusted a stale score-order boolean");

    fill_passing_report(report);
    report.check.candidate.equal_deck_mean_regret = 0.3;
    expect(
        !report.gate_passed(),
        "CHECK gate trusted a stale regret boolean");

    fill_passing_report(report);
    report.behavior.force_spike.worlds = 7;
    expect(
        !report.gate_passed() &&
            !report.failures().empty(),
        "report gate trusted stale scoring metadata");
}

void test_every_check_deck_is_conjunctive() {
    gate::Report report;
    fill_passing_report(report);
    report.check.deck_regret_guard[3] = false;
    const std::vector<std::string> failures =
        report.failures();
    expect(
        !report.gate_passed() &&
            std::find(
                failures.begin(),
                failures.end(),
                "held-out CHECK equal-deck gate failed") !=
                failures.end(),
        "per-deck CHECK regret guard is not conjunctive");
}

void test_five_open_force_spike_fixture_is_exact_and_valid() {
    const old_school::probes::DecisionProbe probe =
        gate::make_five_open_force_spike_control();
    const auto errors =
        gate::validate_five_open_force_spike_control(
            probe);
    const auto open_mountains =
        static_cast<std::size_t>(std::count_if(
            probe.state.players[1].lands.begin(),
            probe.state.players[1].lands.end(),
            [](const old_school::LandPermanent& land) {
                return land.card ==
                           old_school::CardId::Mountain &&
                       !land.tapped;
            }));
    expect(
        errors.empty() &&
            probe.stable_id ==
                gate::kFiveOpenForceSpikeId &&
            probe.state.players[1].lands.size() == 8 &&
            open_mountains == 5 &&
            probe.candidates.size() == 2,
        "five-open Force Spike fixture failed its exact census");

    auto mutated = probe;
    mutated.state.players[1].lands.back().tapped = true;
    expect(
        !gate::validate_five_open_force_spike_control(
             mutated)
             .empty(),
        "five-open fixture validator accepted four open mana");
}

void test_frozen_cache_identity_is_exact() {
    const auto snapshot =
        old_school::artifact_integrity::snapshot_regular_file(
            std::filesystem::path(gate::kFrozenDevCachePath));
    expect(
        snapshot.byte_size == gate::kFrozenDevCacheBytes &&
            snapshot.sha256 ==
                gate::kFrozenDevCacheSha256,
        "frozen DevV3 cache identity drifted");
}

void test_ancestral_information_action_fingerprint_is_frozen() {
    const auto root =
        old_school::action_q_field_gate::
            make_ancestral_field_root();
    const auto hidden =
        old_school::action_q_field_gate::
            hidden_repartition_clone(root);
    const std::string fingerprint =
        gate::ancestral_information_action_fingerprint(root);
    expect(
        fingerprint ==
                gate::kAncestralInformationActionFingerprint &&
            gate::ancestral_information_action_fingerprint(
                hidden) == fingerprint,
        "Ancestral owner-information/action fingerprint drifted: " +
            fingerprint);
}

void test_all_eight_focused_hidden_repartitions_are_nonvacuous() {
    std::vector<old_school::probes::DecisionProbe> focused =
        old_school::probes::
            make_force_spike_policy_controls_v1();
    focused.push_back(
        gate::make_five_open_force_spike_control());
    const auto counters =
        old_school::probes::
            make_counter_composition_controls_v1();
    focused.insert(
        focused.end(),
        counters.begin(), counters.end());
    const auto fields =
        old_school::probes::make_field_regressions_v1();
    const auto append_field =
        [&focused, &fields](std::string_view stable_id) {
            const auto found = std::find_if(
                fields.begin(), fields.end(),
                [stable_id](const auto& probe) {
                    return probe.stable_id == stable_id;
                });
            if (found == fields.end()) {
                throw std::runtime_error(
                    "focused hidden test omitted a field fixture");
            }
            focused.push_back(*found);
        };
    append_field(
        "field.green.second-main-sick-bear-growth.v1");
    append_field(
        "field.green.begin-combat-growth-tapped-air.v1");
    const auto braingeyser =
        old_school::probes::
            make_braingeyser_x_zero_control_v1();
    focused.insert(
        focused.end(),
        braingeyser.begin(), braingeyser.end());

    expect(
        focused.size() ==
            gate::kFocusedDescriptorProbeCount,
        "focused hidden fixture census is not exactly eight");
    for (const auto& probe : focused) {
        auto hidden = probe;
        hidden.state =
            old_school::probe_runner::
                hidden_repartition_clone(probe);
        expect(
            hidden.state != probe.state &&
                old_school::observe_game_state(
                    hidden.state, hidden.root_player) ==
                    old_school::observe_game_state(
                        probe.state, probe.root_player) &&
                old_school::probes::validate_probe(hidden).ok(),
            "focused hidden repartition is vacuous or invalid");
    }
}

} // namespace

int main() {
    try {
        test_report_gate_is_conjunctive_and_one_open_is_descriptive();
        test_gate_rejects_boolean_evidence_mismatch();
        test_every_check_deck_is_conjunctive();
        test_five_open_force_spike_fixture_is_exact_and_valid();
        test_frozen_cache_identity_is_exact();
        test_ancestral_information_action_fingerprint_is_frozen();
        test_all_eight_focused_hidden_repartitions_are_nonvacuous();
        std::cout
            << "action-Q offline-gate tests: 7/7 passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "action-Q offline-gate test failed: "
            << error.what() << '\n';
        return 1;
    }
}
