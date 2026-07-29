#pragma once

#include "old_school/action_q_recursive_policy_improvement.hpp"
#include "old_school/game.hpp"
#include "old_school/information_set_puct.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::information_set_puct_preflight {

namespace aq5 =
    action_q_recursive_policy_improvement;
namespace puct = information_set_puct;

inline constexpr std::uint64_t kPreflightSeed =
    202607291001ULL;
inline constexpr std::string_view kRequiredParentFingerprint =
    aq5::kRequiredParentFingerprint;
inline constexpr std::string_view kRequiredParentPath =
    aq5::kRequiredParentPath;
inline constexpr std::uintmax_t kRequiredParentBytes =
    aq5::kRequiredParentBytes;
inline constexpr std::string_view kRequiredParentSha256 =
    aq5::kRequiredParentSha256;

struct RootActionEvidence {
    std::string fixture_key;
    std::string engine_key;
    double successor_value = 0.5;
    double prior = 0.0;
    std::size_t visits = 0;
    double actor_q = 0.5;
    std::size_t terminal_transitions = 0;
    std::size_t terminal_path_backups = 0;
    double terminal_player_zero_utility_sum = 0.0;
    double terminal_exact_player_zero_utility_sum = 0.0;
    double terminal_absolute_utility_delta_sum = 0.0;

    bool operator==(const RootActionEvidence&) const = default;
};

struct PrincipalVariationWitness {
    std::vector<std::string> actions;
    bool completed_trace = false;
    bool completed_cutoff_path = false;
    std::optional<std::size_t> completed_cutoff_simulation;
    std::size_t searched_depth = 0;
    std::size_t root_actor_counterspells = 0;
    std::size_t root_actor_counters_targeting_own_counter = 0;
    bool initial_opposing_non_counter_spells_countered = false;
    bool stack_settled = false;
    bool exact_combat_completed = false;
    bool exact_combat_completed_at_cutoff = false;
    std::size_t exact_combat_completed_plan_count = 0;
    bool exact_combat_contains_pure_chump = false;
    std::vector<std::pair<PermanentId, PermanentId>>
        completed_damage_ordered_blocks;

    bool operator==(
        const PrincipalVariationWitness&) const = default;
};

// Hidden-safe, evaluation-only trace projection. A completed cutoff is a
// simulation whose recorded transitions end exactly after the final
// principal-variation action. Leaf evidence is admissible only there.
struct PrincipalVariationTraceStepEvidence {
    std::string action_key;
    bool stack_settled = false;
    std::size_t root_actor_counterspells = 0;
    std::size_t root_actor_counters_targeting_own_counter = 0;
    bool initial_opposing_non_counter_spells_countered = false;
    bool exact_combat_completed = false;
    std::size_t exact_combat_completed_plan_count = 0;
    bool exact_combat_contains_pure_chump = false;
    std::vector<std::pair<PermanentId, PermanentId>>
        completed_damage_ordered_blocks;
    std::optional<LearnedGenerativeLeafEvaluation>
        successor_leaf_evaluation;

    bool operator==(
        const PrincipalVariationTraceStepEvidence&) const =
        default;
};

struct PrincipalVariationTraceEvidence {
    std::size_t simulation_index = 0;
    std::vector<PrincipalVariationTraceStepEvidence> steps;

    bool operator==(
        const PrincipalVariationTraceEvidence&) const =
        default;
};

PrincipalVariationWitness build_principal_variation_witness(
    const std::vector<std::string>& principal_variation,
    const std::vector<PrincipalVariationTraceEvidence>& traces);

struct RootReport {
    std::string stable_id;
    aq5::DecisionFamily family =
        aq5::DecisionFamily::Priority;
    std::string expected_key;
    std::string selected_key;
    std::vector<RootActionEvidence> actions;
    std::size_t requested_simulations =
        puct::kSimulationCount;
    LearnedTerminalUtilityMode terminal_utility_mode =
        LearnedTerminalUtilityMode::ExactOutcome;
    std::uint64_t search_seed = 0;
    std::uint64_t tie_seed = 0;
    puct::Accounting accounting;
    PrincipalVariationWitness principal_variation;

    bool strategic_direction_passed = false;
    bool complete_legal_choice_coverage = false;
    bool finite_positive_normalized_priors = false;
    bool exact_successor_value_prior_formula = false;
    bool root_visit_accounting_exact = false;
    bool bounded_tree_accounting = false;
    bool bounded_macro_accounting = false;
    bool opponent_action_accounting_consistent = false;
    bool no_combat_bound_fallback = false;
    bool transition_seed_candidate_independent = false;
    bool deterministic_replay_bit_identical = false;
    bool reversed_input_full_evidence_bit_identical = false;
    bool hidden_repartition_nonvacuous = false;
    bool hidden_root_observation_bit_identical = false;
    bool hidden_full_evidence_bit_identical = false;
    bool root_observer_only_nodes = false;
    bool opponent_nodes_absent = false;
    bool actor_hand_preserved = false;
    bool public_state_preserved = false;
    bool truth_immutable_during_redeterminization = false;
    bool legal_signature_preserved = false;
    bool required_shared_successor = false;
    bool required_counter_principal_variation = false;
    bool required_exact_combat_completion = false;
    bool partial_combat_leaf_completed_exactly = false;

    bool evidence_gate_passed() const;
    bool gate_passed() const;
};

struct OpponentNoninterferenceReport {
    std::string fixture_id;
    std::size_t scored_action_count = 0;
    std::size_t selected_action_membership_count = 0;
    bool nonvacuous_private_repartition = false;
    bool opponent_observation_bit_identical = false;
    bool legal_signature_bit_identical = false;
    bool c16_scores_bit_identical = false;
    bool selected_action_bit_identical = false;
    bool local_accounting_bit_identical = false;
    bool no_shared_opponent_node_or_q_update = false;
    LearnedGenerativeDecisionAccounting
        original_accounting_through_decision;
    LearnedGenerativeDecisionAccounting
        repartitioned_accounting_through_decision;

    bool operator==(
        const OpponentNoninterferenceReport&) const =
        default;
    bool gate_passed() const;
};

bool opponent_local_accounting_bit_identical(
    const LearnedGenerativeOpponentDecisionWitness& left,
    const LearnedGenerativeOpponentDecisionWitness& right);

struct IsolationReport {
    bool exact_parent = false;
    bool exact_configuration = false;
    bool evaluation_only_default_off = false;
    bool default_off_fixed_game_bit_identical = false;

    bool gate_passed() const;
};

struct PreflightReport {
    std::uint64_t seed = kPreflightSeed;
    std::string parent_fingerprint;
    std::vector<RootReport> roots;
    OpponentNoninterferenceReport opponent_noninterference;
    IsolationReport isolation;
    bool all_twelve_roots_present_once = false;
    bool all_three_decision_families_present = false;
    bool hypothesis_passed = false;

    bool gate_passed() const;
};

// Pure fixture-label seam. These labels are evaluation-only expectations and
// are never imported by a runtime Learned policy.
std::string expected_fixture_choice(std::string_view stable_id);
bool fixture_direction_passed(
    std::string_view stable_id,
    std::string_view selected_key);
std::uint64_t transition_seed(
    std::uint64_t root_seed,
    std::size_t simulation_index,
    std::size_t searched_decision_ply);
puct::Terminal puct_terminal_from_game_result(
    const GameResult& result,
    LearnedTerminalUtilityMode terminal_utility_mode);

// Fakeable orchestration seam used by focused preflight tests. Production
// binds all callbacks to the engine adapter below.
struct PreflightApi {
    std::function<RootReport(const aq5::PreparedRoot&)>
        run_root;
    std::function<OpponentNoninterferenceReport(
        const std::vector<aq5::PreparedRoot>&)>
        check_opponent_noninterference;
    std::function<bool()> check_default_off_identity;
};

PreflightReport assemble_preflight(
    std::string parent_fingerprint,
    const PreflightApi& api);

PreflightReport run_preflight(
    std::shared_ptr<const LearnedModel> parent);

// Shared production evidence seam for sealed follow-on diagnostics. The
// experiment seed and simulation count affect only seed derivation and the
// PUCT core's bounded simulation loop; all engine/search semantics are the
// same as ISP0.
RootReport run_root_evidence(
    const aq5::PreparedRoot& root,
    std::shared_ptr<const LearnedModel> parent,
    std::uint64_t experiment_seed,
    std::size_t simulation_count);

RootReport run_root_evidence(
    const aq5::PreparedRoot& root,
    std::shared_ptr<const LearnedModel> parent,
    std::uint64_t experiment_seed,
    std::size_t simulation_count,
    LearnedTerminalUtilityMode terminal_utility_mode);

OpponentNoninterferenceReport
run_opponent_noninterference_evidence(
    const std::vector<aq5::PreparedRoot>& roots,
    std::shared_ptr<const LearnedModel> parent,
    std::uint64_t experiment_seed);

OpponentNoninterferenceReport
run_opponent_noninterference_evidence(
    const std::vector<aq5::PreparedRoot>& roots,
    std::shared_ptr<const LearnedModel> parent,
    std::uint64_t experiment_seed,
    LearnedTerminalUtilityMode terminal_utility_mode);

// DBC1-only evidence seam for a fitted output-layer candidate. Unlike the
// sealed wrappers above, these functions intentionally do not require the
// exact C16 fingerprint. They do require C16's two-leaf Value/output-
// calibration topology; the caller remains responsible for authenticating
// the candidate's derivation before invoking this evaluation-only seam.
RootReport run_output_calibrated_candidate_root_evidence(
    const aq5::PreparedRoot& root,
    std::shared_ptr<const LearnedModel> candidate,
    std::uint64_t experiment_seed,
    std::size_t simulation_count,
    LearnedTerminalUtilityMode terminal_utility_mode);

OpponentNoninterferenceReport
run_output_calibrated_candidate_opponent_noninterference_evidence(
    const std::vector<aq5::PreparedRoot>& roots,
    std::shared_ptr<const LearnedModel> candidate,
    std::uint64_t experiment_seed,
    LearnedTerminalUtilityMode terminal_utility_mode);

void print_report(
    const PreflightReport& report,
    std::ostream& output);

} // namespace old_school::information_set_puct_preflight
