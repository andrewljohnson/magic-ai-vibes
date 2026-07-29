#pragma once

#include "old_school/decision_boundary_critic.hpp"
#include "old_school/information_set_puct_budget_diagnostic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace old_school::decision_boundary_critic_gate {

namespace dbc = decision_boundary_critic;
namespace isp0 = information_set_puct_preflight;

inline constexpr std::uint64_t kMechanismSeed =
    information_set_puct_budget_diagnostic::
        kTerminalScaleSeed;
inline constexpr std::size_t kMechanismSimulations =
    information_set_puct_budget_diagnostic::
        kSmallBudget;
inline constexpr std::uint64_t kSelectorSeed =
    UINT64_C(202607291311);

struct MechanismRootReport {
    std::string stable_id;
    bool repair = false;
    isp0::RootReport parent;
    isp0::RootReport candidate;
    bool parent_correct = false;
    bool candidate_correct = false;
    bool parent_invariants = false;
    bool candidate_invariants = false;
};

struct MechanismReport {
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    std::vector<MechanismRootReport> roots;
    isp0::OpponentNoninterferenceReport parent_opponent;
    isp0::OpponentNoninterferenceReport candidate_opponent;
    bool candidate_derivation_authenticated = false;
    bool exact_configuration = false;
    bool common_seed_contract = false;
    bool exact_nine_root_census = false;
    bool all_invariants_green = false;
    bool all_controls_green = false;
    bool no_parent_correct_repair_regression = false;
    std::size_t repairs_correct = 0;
    std::size_t controls_correct = 0;

    bool mechanism_supported() const;
    bool selector_licensed() const;
};

MechanismReport run_mechanism_gate(
    std::shared_ptr<const LearnedModel> parent,
    const dbc::FitReport& fit);
MechanismReport run_candidate_mechanism_gate(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    bool derivation_authenticated);
void print_mechanism_report(
    std::ostream& output,
    const MechanismReport& report);

struct SelectorReport {
    BotBenchmarkSummary summary;
    std::array<std::size_t, kDeckCount> deck_wins{};
    bool candidate_derivation_authenticated = false;
    bool exact_recipe = false;
    bool exact_balance = false;
    bool every_deck_floor = false;
    bool pilot_licensed = false;
    bool fast_go = false;
};

SelectorReport run_selector(
    std::shared_ptr<const LearnedModel> parent,
    const dbc::FitReport& fit);
SelectorReport run_candidate_selector(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    std::uint64_t selector_seed,
    bool derivation_authenticated);
void print_selector_report(
    std::ostream& output,
    const SelectorReport& report);

} // namespace old_school::decision_boundary_critic_gate
