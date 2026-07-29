#pragma once

#include "old_school/information_set_puct_preflight.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace old_school::information_set_puct_budget_diagnostic {

namespace aq5 =
    action_q_recursive_policy_improvement;
namespace isp0 = information_set_puct_preflight;

inline constexpr std::uint64_t kDiagnosticSeed =
    UINT64_C(202607291101);
inline constexpr std::size_t kRootCount = 9;
inline constexpr std::size_t kSmallBudget = 64;
inline constexpr std::size_t kLargeBudget = 512;

enum class RootRole {
    PrimaryMiss,
    Control,
};

enum class Verdict {
    MechanismPass,
    MechanismSupportCandidateReject,
    InconclusiveScaling,
    RejectCloseBudgetAxis,
};

struct TimedRootEvidence {
    isp0::RootReport evidence;
    double wall_seconds = 0.0;
};

struct RootBudgetReport {
    std::string stable_id;
    RootRole role = RootRole::Control;
    std::size_t simulation_count = 0;
    isp0::RootReport evidence;
    double wall_seconds = 0.0;
    bool semantic_direction_passed = false;
    bool invariant_gate_passed = false;

    bool gate_passed() const;
};

struct BudgetReport {
    std::size_t simulation_count = 0;
    std::vector<RootBudgetReport> roots;
    isp0::OpponentNoninterferenceReport
        opponent_noninterference;
    std::size_t primary_misses_correct = 0;
    std::size_t controls_correct = 0;
    bool exact_nine_root_census = false;
    bool all_invariants_green = false;
    bool all_controls_green = false;

    bool gate_passed() const;
};

struct DiagnosticReport {
    std::uint64_t seed = kDiagnosticSeed;
    std::string parent_fingerprint;
    BudgetReport small;
    BudgetReport large;
    bool exact_configuration = false;
    bool common_prefix_seed_contract = false;
    bool no_primary_regression = false;
    std::size_t primary_improvements = 0;
    Verdict verdict =
        Verdict::RejectCloseBudgetAxis;

    bool gate_passed() const;
};

struct DiagnosticApi {
    std::function<TimedRootEvidence(
        const aq5::PreparedRoot&, std::size_t)>
        run_root;
    std::function<isp0::OpponentNoninterferenceReport(
        const std::vector<aq5::PreparedRoot>&,
        std::size_t)>
        check_opponent_noninterference;
};

std::vector<aq5::PreparedRoot> diagnostic_roots();
RootRole root_role(std::string_view stable_id);
bool semantic_direction_passed(
    std::string_view stable_id,
    const isp0::RootReport& evidence);

DiagnosticReport assemble_diagnostic(
    std::string parent_fingerprint,
    const DiagnosticApi& api);

DiagnosticReport run_diagnostic(
    std::shared_ptr<const LearnedModel> parent);

std::string_view verdict_name(Verdict verdict);
void print_report(
    const DiagnosticReport& report,
    std::ostream& output);

} // namespace old_school::information_set_puct_budget_diagnostic
