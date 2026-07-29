#pragma once

#include "old_school/action_q_on_policy_successor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::action_q_priority_trust_region {

namespace op1 = action_q_on_policy_successor;

inline constexpr std::array<double, 3> kCandidateAlphas{
    0.75, 0.50, 0.25,
};
inline constexpr std::uint64_t kSelectorSeed =
    202607290211ULL;
inline constexpr double kMaximumDevDeckRegretIncrease =
    op1::kMaximumDevDeckRegretIncrease;
inline constexpr std::string_view kRequiredCorpusDigest =
    "985026631f56dceba5c42bc4c7247757640d092f92a3d030759f607cd5b8c5df";
inline constexpr std::string_view kRequiredFullChildFingerprint =
    "a4cdb8a7cf53cea58d79a7591eafd76c8b4724bce457c8d0ab7e533ba19b036f";

enum class SweepDisposition {
    Continue,
    Selected,
    Reject,
};

enum class SelectorDisposition {
    Reject,
    ManualPilot,
};

struct ArmGateInputs {
    bool repeated_construction_bit_identical = false;
    bool only_priority_component_changed = false;
    bool train_regret_strictly_improved = false;
    bool dev_regret_strictly_improved = false;
    std::array<bool, kDeckCount> dev_deck_regret_guard{};
    bool model_gate_passed = false;

    bool operator==(const ArmGateInputs&) const = default;
};

struct ArmEvaluation {
    double alpha = 0.0;
    std::shared_ptr<const LearnedModel> model;
    std::string fingerprint;
    std::string repeated_fingerprint;
    op1::Metrics parent_train;
    op1::Metrics candidate_train;
    op1::Metrics parent_dev;
    op1::Metrics candidate_dev;
    action_q_offline_gate::ModelGateReport model_gates;
    bool repeated_construction_bit_identical = false;
    bool only_priority_component_changed = false;
    bool train_regret_strictly_improved = false;
    bool dev_regret_strictly_improved = false;
    std::array<bool, kDeckCount> dev_deck_regret_guard{};
    std::vector<std::string> failures;

    bool gate_passed() const;
};

struct FullControlGateInputs {
    bool endpoint_pointer_exact = false;
    bool corpus_digest_exact = false;
    bool fingerprint_exact = false;
    bool repeated_construction_bit_identical = false;
    bool only_priority_component_changed = false;
    bool parent_train_metrics_exact = false;
    bool candidate_train_metrics_exact = false;
    bool parent_dev_metrics_exact = false;
    bool candidate_dev_metrics_exact = false;
    bool offline_metric_gates_exact = false;
    bool expected_safety_signature_exact = false;

    bool operator==(const FullControlGateInputs&) const = default;
};

struct FullControlReport {
    ArmEvaluation arm;
    bool endpoint_pointer_exact = false;
    bool corpus_digest_exact = false;
    bool parent_train_metrics_exact = false;
    bool candidate_train_metrics_exact = false;
    bool parent_dev_metrics_exact = false;
    bool candidate_dev_metrics_exact = false;
    bool expected_safety_signature_exact = false;
    std::vector<std::string> failures;

    bool control_exact() const;
};

bool arm_gate_passed(const ArmGateInputs& inputs);
bool full_control_gate_passed(
    const FullControlGateInputs& inputs);

// Interpolates only the outer Priority tensors. Alpha zero and one return the
// exact endpoint pointers. The child must differ from the warm parent only in
// Priority.
std::shared_ptr<const LearnedModel> interpolate_priority_head(
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> full_child,
    double alpha);

ArmEvaluation evaluate_arm(
    const op1::Corpus& corpus,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> full_child,
    double alpha);

FullControlReport evaluate_full_control(
    const op1::Corpus& corpus,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> full_child);

// Pure protocol seams. `arms` must be a prefix of kCandidateAlphas.
std::optional<std::size_t> first_passing_arm_index(
    std::span<const ArmEvaluation> arms);
std::optional<double> first_passing_alpha(
    std::span<const ArmEvaluation> arms);
SweepDisposition classify_sweep(
    std::span<const ArmEvaluation> arms);

// Primitive equivalents used by protocol tests and by a runner that has
// retained only the attempted arms' gate results.
std::optional<std::size_t> first_passing_gate_index(
    std::span<const bool> gate_results);
std::optional<double> first_passing_gate_alpha(
    std::span<const bool> gate_results);
SweepDisposition classify_gate_prefix(
    std::span<const bool> gate_results);

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary);

} // namespace old_school::action_q_priority_trust_region
