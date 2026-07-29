#pragma once

#include "old_school/action_q_explore.hpp"
#include "old_school/action_q_field_gate.hpp"
#include "old_school/artifact_integrity.hpp"
#include "old_school/probe_runner.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::action_q_offline_gate {

inline constexpr std::string_view kFrozenDevCachePath =
    "data/old-school-probe-dev-v3-k64-h8-c17-j1.labels.tsv";
inline constexpr std::string_view kFrozenDevActorFingerprint =
    "dd58d3814f46d6661d40690f6ad7ac73226c2160137b2e42b"
    "fadf3e6ac7a1b72";
inline constexpr std::size_t kFrozenDevProbeCount = 20;
inline constexpr std::size_t kFrozenDevProbesPerDeck = 4;
inline constexpr std::size_t kFrozenDevWorlds = 64;
inline constexpr std::size_t kFrozenDevHorizonTurns = 8;
inline constexpr std::uintmax_t kFrozenDevCacheBytes = 276387;
inline constexpr std::string_view kFrozenDevCacheSha256 =
    "949ea2fda448fa76b31a61927721629cfba9e6addee2da383"
    "cfbb68450b04770";
inline constexpr std::size_t kFocusedDescriptorProbeCount = 8;
inline constexpr double kMaximumCheckDeckRegretIncrease = 0.01;
inline constexpr std::string_view kFiveOpenForceSpikeId =
    "control.blue.force-spike-payable-five-open-gray-ogre.aq0.v1";
inline constexpr std::string_view kFiveOpenForceSpikeCorpusId =
    "old-school-action-q-force-spike-five-open-v1";
// Model-free owner-observation plus complete typed-action digest.
inline constexpr std::string_view kAncestralInformationActionFingerprint =
    "3b74bdb37c0572b5";

struct IsolationGate {
    bool parent_identity_exact = false;
    bool candidate_identity_exact = false;
    bool parent_immutable = false;
    bool repeated_fit_bit_identical = false;
    bool only_priority_component_changed = false;

    bool gate_passed() const;
};

struct CheckGate {
    action_q_explore::Metrics parent;
    action_q_explore::Metrics candidate;
    bool metrics_match_fit_report = false;
    bool regret_strictly_improved = false;
    bool top_one_not_lower = false;
    std::array<bool, kDeckCount> deck_regret_guard{};

    bool gate_passed() const;
};

struct FrozenDevGate {
    probe_eval::ProbeMetricSummary parent;
    probe_eval::ProbeMetricSummary candidate;
    std::size_t labels = 0;
    std::array<std::size_t, kDeckCount> labels_by_deck{};
    std::size_t stable_parent_agreements = 0;
    std::size_t lost_stable_parent_agreements = 0;
    probe_runner::HiddenRepartitionSummary pair_hidden_repartition;
    probe_runner::HiddenRepartitionSummary explicit_hidden_repartition;
    artifact_integrity::RegularFileSnapshot cache_before;
    artifact_integrity::RegularFileSnapshot cache_after;
    bool pooled_regret_no_worse = false;

    bool gate_passed() const;
};

struct AncestralGate {
    double self_score = 0.0;
    double opponent_score = 0.0;
    std::vector<PriorityAction> legal_actions;
    std::vector<PriorityAction> selected_support;
    std::string information_action_fingerprint;
    bool complete_legal_actions_exact = false;
    bool information_action_fingerprint_exact = false;
    bool hidden_repartition_bit_identical = false;
    bool self_strictly_above_opponent = false;
    bool opponent_absent_from_support = false;

    bool gate_passed() const;
};

struct DescriptorOrderGate {
    std::size_t model_count = 0;
    std::size_t probe_count = 0;
    std::size_t hidden_model_count = 0;
    std::size_t hidden_probe_count = 0;
    bool action_keyed_scores_bit_identical = false;
    bool selected_supports_identical = false;
    bool hidden_repartitions_distinct_owner_equivalent = false;
    bool hidden_action_keyed_scores_bit_identical = false;
    bool hidden_selected_supports_identical = false;

    bool gate_passed() const;
};

struct BehavioralGate {
    probe_runner::ForceSpikePolicyControlReport force_spike;
    std::vector<std::string> five_open_selected_keys;
    std::vector<std::string> redundant_counter_selected_keys;
    std::vector<std::string> intervening_counter_selected_keys;
    std::vector<std::string> sick_bear_growth_selected_keys;
    std::vector<std::string> opponent_growth_selected_keys;
    std::vector<std::string> braingeyser_selected_keys;
    // The historical one-open control is reported, but is deliberately not
    // conjunctive: AQ0 preregistered it as descriptive only.
    bool one_open_payable_selects_pass = false;
    bool live_force_spike_preserved = false;
    bool five_open_force_spike_selects_pass = false;
    bool redundant_counter_selects_pass = false;
    bool intervening_counter_selects_opposing_counter = false;
    bool sick_bear_growth_selects_pass = false;
    bool opponent_growth_excluded = false;
    bool braingeyser_x_zero_excluded = false;

    bool gate_passed() const;
};

// The model-only part of the AQ0/AQ1 offline battery.  These checks depend
// only on the frozen parent, the candidate, and the shared K8/H4/residual
// deployment recipe, so keeping them here prevents successor experiments
// from cloning authored fixtures and frozen-label evaluation code.
struct ModelGateReport {
    FrozenDevGate frozen_dev;
    AncestralGate ancestral;
    DescriptorOrderGate descriptor_order;
    BehavioralGate behavior;

    bool gate_passed() const;
    std::vector<std::string> failures() const;
};

struct Report {
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    IsolationGate isolation;
    CheckGate check;
    FrozenDevGate frozen_dev;
    AncestralGate ancestral;
    DescriptorOrderGate descriptor_order;
    BehavioralGate behavior;

    bool gate_passed() const;
    std::vector<std::string> failures() const;
};

// A stronger payable control cloned from the existing authored fixture. The
// opposing Gray Ogre controller has exactly five untapped Mountains after the
// three tapped Mountains used to cast the spell. This fixture and every
// preferred-action assertion in this module are evaluation-only.
probes::DecisionProbe make_five_open_force_spike_control();
std::vector<std::string> validate_five_open_force_spike_control(
    const probes::DecisionProbe& probe);

// Pure model-free identity of the captured Ancestral owner observation and
// its complete typed legal Priority action set.
std::string ancestral_information_action_fingerprint(
    const action_q_field_gate::AncestralFieldRoot& root);

ModelGateReport evaluate_model_gates(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate);

// Applies every preregistered AQ0 offline gate. No result from these authored
// fixtures or frozen labels is consumed by training or runtime policy code.
Report evaluate(
    const action_q_explore::Corpus& corpus,
    const action_q_explore::FitReport& fit,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate);

} // namespace old_school::action_q_offline_gate
