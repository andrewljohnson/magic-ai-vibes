#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::fq4_priority_fit {

inline constexpr std::string_view kRequiredParentFingerprint =
    "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr double kResidualWeight = 0.10;
inline constexpr double kPolicyTemperature = 0.10;
inline constexpr double kRequiredScoreMargin = 0.01;
inline constexpr double kGateScoreMargin = 0.005;
inline constexpr double kSearchChoiceWeight = 0.90;
inline constexpr std::uint64_t kOptimizerSeed = 202607272321ULL;
inline constexpr std::size_t kDominanceWorlds = 64;
inline constexpr std::size_t kExpectedTrainingRoots = 3;
inline constexpr std::size_t kExpectedDominanceConstraints = 5;
inline constexpr std::size_t kExpectedParentMarginsBelowGate = 4;
inline constexpr std::size_t kExpectedControls = 10;

inline constexpr std::size_t kD0bAnchorEpochs = 256;
inline constexpr std::size_t kD0bTreatmentEpochs = 512;
inline constexpr std::size_t
    kD0bExpectedParentMarginsBelowGate = 3;
inline constexpr std::string_view kD0bRequiredAnchorFingerprint =
    "7079f8f306f90d54df21fedcf81213cc6ad5df6c5c1df46e9b3456ccd550c312";
inline constexpr std::string_view kD0bRequiredTreatmentFingerprint =
    "81ad05d2c32bea9b17ca4c89cbbf7a9be105ad130897f79fa4d8a29a5ea1105e";
inline constexpr std::array<std::uint64_t, 5>
    kD0bRequiredParentMarginBits{
        0x3f5a0f22b369b200ULL,
        0xbf9a696aeeb4ed80ULL,
        0x3f8c314f54c3f600ULL,
        0x3f7f2b2651a85780ULL,
        0xbf5325e8217c8200ULL,
    };
inline constexpr std::array<std::uint64_t, 5>
    kD0bRequiredAnchorMarginBits{
        0x3f847a090a0a5700ULL,
        0x3f7092468b125340ULL,
        0x3fa15654c6fe01c0ULL,
        0x3f900f82c7ce68e0ULL,
        0x3f8299d1df3cdb40ULL,
    };

struct StarConstraint {
    std::size_t pass_index = 0;
    std::size_t dominated_index = 0;

    bool operator==(const StarConstraint&) const = default;
};

struct ReverseKlProjection {
    // The unique I-projection argmin_q D_KL(q || p).
    std::vector<double> probabilities;
    std::vector<std::size_t> active_dominated_indices;

    bool operator==(const ReverseKlProjection&) const = default;
};

// Computes the joint star-constraint I-projection. Every constraint must
// share one pass index and has the form q(pass) >= ratio*q(dominated).
// Invalid distributions, duplicate constraints, and non-star inputs fail
// closed. An already-feasible input is returned bit-for-bit unchanged.
ReverseKlProjection reverse_kl_i_projection(
    const std::vector<double>& parent_probabilities,
    const std::vector<StarConstraint>& constraints,
    double ratio);

struct DominanceConstraintReport {
    std::string descriptor;
    std::size_t action_index = 0;
    std::size_t strict_worlds = 0;
    bool active_projection_constraint = false;
    double parent_margin = 0.0;
    double candidate_margin = 0.0;

    bool operator==(
        const DominanceConstraintReport&) const = default;
};

struct TrainingRootReport {
    std::string stable_id;
    std::string information_action_fingerprint;
    std::vector<std::string> descriptors;
    std::size_t pass_index = 0;
    std::vector<double> immutable_base_scores;
    std::vector<double> parent_combined_scores;
    std::vector<double> candidate_combined_scores;
    std::vector<double> parent_latent_probabilities;
    std::vector<double> projected_latent_probabilities;
    std::vector<double> behavior_target_probabilities;
    std::vector<std::string> parent_exact_support;
    std::vector<std::string> candidate_exact_support;
    std::vector<DominanceConstraintReport> constraints;
    bool production_recipe_exact = false;
    bool production_base_and_accounting_bit_identical = false;

    bool operator==(const TrainingRootReport&) const = default;
};

struct ControlReport {
    std::string name;
    std::string stable_id;
    std::string information_action_fingerprint;
    std::vector<std::string> descriptors;
    std::vector<std::string> parent_exact_support;
    std::vector<std::string> candidate_exact_support;
    bool passed = false;

    bool operator==(const ControlReport&) const = default;
};

struct FitReport {
    std::shared_ptr<const LearnedModel> candidate;
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    std::string training_input_sha256;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    std::vector<TrainingRootReport> roots;
    std::vector<ControlReport> controls;

    std::size_t discovered_constraints = 0;
    std::size_t parent_margins_below_gate = 0;
    std::size_t candidate_margins_at_gate = 0;
    bool parent_immutable = false;
    bool only_priority_component_changed = false;
    bool repeated_fit_bit_identical = false;
    bool hidden_repartition_bit_identical = false;
    bool action_order_bit_identical = false;
    bool all_production_base_and_accounting_bit_identical = false;
    bool every_control_passed = false;

    std::vector<std::string> infrastructure_failures;
    std::vector<std::string> scientific_failures;

    bool infrastructure_valid() const;
    bool passed() const;
};

enum class ExitClassification : int {
    Pass = 0,
    ScientificReject = 1,
    InfrastructureFailure = 2,
};

ExitClassification classify_exit(const FitReport& report);

// Production entry point: requires the exact immutable C16 fingerprint.
FitReport fit_production(
    std::shared_ptr<const LearnedModel> frozen_c16);

struct D0bRootKlReport {
    std::string stable_id;
    std::vector<double> candidate_behavior_probabilities;
    double target_to_candidate_kl = 0.0;

    bool operator==(const D0bRootKlReport&) const = default;
};

struct D0bCheckpointReport {
    std::size_t epochs = 0;
    LearnedValuePriorityHeadUpdateConfig optimizer;
    FitReport fit;
    std::vector<D0bRootKlReport> root_kl;
    double pooled_target_to_candidate_kl = 0.0;
};

struct D0bReport {
    // The anchor is a reproduction control. Only treatment.fit.candidate is
    // the eligible D0b candidate.
    D0bCheckpointReport anchor;
    D0bCheckpointReport treatment;
    std::vector<double> parent_margins;
    std::size_t parent_margins_below_gate = 0;

    bool exact_contracts_required = false;
    bool parent_contract_qualified = false;
    bool anchor_contract_qualified = false;
    bool optimizer_only_epochs_differ = false;
    bool checkpoint_inputs_bit_identical = false;
    bool target_kl_strictly_improved = false;

    std::vector<std::string> infrastructure_failures;
    std::vector<std::string> scientific_failures;

    bool infrastructure_valid() const;
    bool passed() const;
};

ExitClassification classify_d0b_exit(
    const D0bReport& report);

// Fixed production experiment: independently fits the 256-epoch
// reproduction anchor and the one eligible 512-epoch treatment directly
// from the exact immutable C16 parent.
D0bReport fit_d0b_production(
    std::shared_ptr<const LearnedModel> frozen_c16);

namespace testing {

// Portable clean-clone seam. It uses the same roots, K8/H4 production
// surface, dominance worlds, optimizer, controls, and invariance gates, but
// deliberately omits only the exact C16 fingerprint requirement.
FitReport fit(
    std::shared_ptr<const LearnedModel> value_model);

// Test-only diagnostic seam: applies the exact frozen-parent behavioral
// contracts without requiring the production artifact fingerprint. Contract
// mismatches must be returned as infrastructure evidence after the complete
// fit, rather than short-circuiting the report.
FitReport fit_enforcing_parent_behavior_contract(
    std::shared_ptr<const LearnedModel> value_model);

// Portable clean-clone seam for the compile-time-fixed D0b experiment. It
// omits only the exact C16 parent and 256-anchor artifact qualifications;
// all structural, KL, optimizer, control, and invariance gates remain live.
D0bReport fit_d0b(
    std::shared_ptr<const LearnedModel> value_model);

// Negative-control trajectory for portable tests only: 256 epochs from the
// parent followed by another 256 epochs from that fitted model with Adam
// moments reset. The fixed D0b treatment must not equal this trajectory.
std::string d0b_two_stage_reset_fingerprint(
    std::shared_ptr<const LearnedModel> value_model);

bool d0b_exact_margin_contract(
    const std::vector<double>& parent_margins,
    const std::vector<double>& anchor_margins);

} // namespace testing

} // namespace old_school::fq4_priority_fit
