#include "old_school/information_set_puct_budget_diagnostic.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace old_school::information_set_puct_budget_diagnostic {
namespace {

constexpr std::string_view kBraingeyser =
    "control.blue.braingeyser-x0.v1";
constexpr std::string_view kAncestral =
    "field.blue.ancestral-opponent-seed24.aq0.v1";
constexpr std::string_view kPayableSpike =
    "control.blue.force-spike-payable-five-open-gray-ogre.aq0.v1";
constexpr std::string_view kRedundantCounter =
    "control.blue.counter-redundant-same-target.v1";
constexpr std::string_view kInterveningCounter =
    "control.blue.counter-same-target-after-intervening-counter.v1";
constexpr std::string_view kGrowth =
    "field.green.begin-combat-growth-tapped-air.v1";
constexpr std::string_view kLiveSpike =
    "control.blue.force-spike-live-gray-ogre.v1";
constexpr std::string_view kLife20Block =
    "field.ru.life20-flying-men-chump-air.v1";
constexpr std::string_view kBlueBlock =
    aq5::kNewBlueBlockFixtureId;

constexpr std::array<std::string_view, kRootCount> kRootIds = {
    kBraingeyser,
    kAncestral,
    kPayableSpike,
    kRedundantCounter,
    kInterveningCounter,
    kGrowth,
    kLiveSpike,
    kLife20Block,
    kBlueBlock,
};

bool selected(
    const isp0::RootReport& evidence,
    std::string_view key) {
    return evidence.selected_key == key;
}

bool intervening_counter_completed(
    const isp0::RootReport& evidence) {
    const auto& pv = evidence.principal_variation;
    return (selected(
                evidence,
                "counter-same-air-elemental") ||
            selected(
                evidence,
                "counter-opponent-counterspell")) &&
           pv.completed_trace &&
           pv.searched_depth >= 2 &&
           pv.root_actor_counterspells == 1 &&
           pv.root_actor_counters_targeting_own_counter == 0 &&
           pv.initial_opposing_non_counter_spells_countered &&
           pv.stack_settled;
}

bool blue_block_completed(
    const isp0::RootReport& evidence) {
    const auto& pv = evidence.principal_variation;
    return selected(evidence, "no-block") &&
           pv.completed_cutoff_path &&
           pv.completed_cutoff_simulation.has_value() &&
           pv.exact_combat_completed &&
           pv.exact_combat_completed_at_cutoff &&
           pv.exact_combat_completed_plan_count > 0 &&
           !pv.exact_combat_contains_pure_chump &&
           !pv.completed_damage_ordered_blocks.empty();
}

BudgetReport run_budget(
    const std::vector<aq5::PreparedRoot>& roots,
    std::size_t simulation_count,
    const DiagnosticApi& api) {
    BudgetReport report{
        .simulation_count = simulation_count,
    };
    report.roots.reserve(roots.size());
    std::set<std::string> seen;
    for (const aq5::PreparedRoot& root : roots) {
        TimedRootEvidence measured =
            api.run_root(root, simulation_count);
        if (measured.evidence.stable_id !=
                root.stable_id ||
            measured.evidence.family != root.family ||
            measured.evidence.requested_simulations !=
                simulation_count ||
            !std::isfinite(measured.wall_seconds) ||
            measured.wall_seconds < 0.0) {
            throw std::logic_error(
                "ISP1 root callback changed sealed identity, "
                "budget, or timing");
        }
        RootBudgetReport root_report{
            .stable_id = root.stable_id,
            .role = root_role(root.stable_id),
            .simulation_count = simulation_count,
            .evidence = std::move(measured.evidence),
            .wall_seconds = measured.wall_seconds,
        };
        root_report.semantic_direction_passed =
            semantic_direction_passed(
                root.stable_id,
                root_report.evidence);
        root_report.invariant_gate_passed =
            root_report.evidence.evidence_gate_passed();
        if (root_report.role == RootRole::PrimaryMiss &&
            root_report.semantic_direction_passed) {
            ++report.primary_misses_correct;
        }
        if (root_report.role == RootRole::Control &&
            root_report.semantic_direction_passed) {
            ++report.controls_correct;
        }
        seen.insert(root.stable_id);
        report.roots.push_back(std::move(root_report));
    }

    report.opponent_noninterference =
        api.check_opponent_noninterference(
            roots, simulation_count);
    report.opponent_noninterference
        .no_shared_opponent_node_or_q_update =
        report.opponent_noninterference
            .no_shared_opponent_node_or_q_update &&
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [](const RootBudgetReport& root) {
                return root.evidence
                           .root_observer_only_nodes &&
                       root.evidence.opponent_nodes_absent;
            });
    report.exact_nine_root_census =
        report.roots.size() == kRootCount &&
        seen.size() == kRootCount;
    report.all_invariants_green =
        report.exact_nine_root_census &&
        report.opponent_noninterference.gate_passed() &&
        std::all_of(
            report.roots.begin(), report.roots.end(),
            [](const RootBudgetReport& root) {
                return root.invariant_gate_passed;
            });
    report.all_controls_green =
        report.controls_correct == 6;
    return report;
}

const RootBudgetReport& root_for(
    const BudgetReport& report,
    std::string_view stable_id) {
    const auto found = std::find_if(
        report.roots.begin(), report.roots.end(),
        [&](const RootBudgetReport& root) {
            return root.stable_id == stable_id;
        });
    if (found == report.roots.end()) {
        throw std::logic_error(
            "ISP1 budget report omitted a sealed root");
    }
    return *found;
}

} // namespace

bool RootBudgetReport::gate_passed() const {
    return !stable_id.empty() &&
           (simulation_count == kSmallBudget ||
            simulation_count == kLargeBudget) &&
           evidence.stable_id == stable_id &&
           evidence.requested_simulations ==
               simulation_count &&
           std::isfinite(wall_seconds) &&
           wall_seconds >= 0.0 &&
           semantic_direction_passed &&
           invariant_gate_passed;
}

bool BudgetReport::gate_passed() const {
    return (simulation_count == kSmallBudget ||
            simulation_count == kLargeBudget) &&
           exact_nine_root_census &&
           all_invariants_green &&
           all_controls_green;
}

bool DiagnosticReport::gate_passed() const {
    return seed == kDiagnosticSeed &&
           parent_fingerprint ==
               isp0::kRequiredParentFingerprint &&
           exact_configuration &&
           common_prefix_seed_contract &&
           no_primary_regression &&
           small.gate_passed() &&
           large.gate_passed() &&
           verdict == Verdict::MechanismPass;
}

std::vector<aq5::PreparedRoot> diagnostic_roots() {
    const std::vector<aq5::PreparedRoot> all =
        aq5::build_fixture_roots();
    std::map<std::string, aq5::PreparedRoot> indexed;
    for (const aq5::PreparedRoot& root : all) {
        indexed.emplace(root.stable_id, root);
    }
    std::vector<aq5::PreparedRoot> result;
    result.reserve(kRootCount);
    for (const std::string_view id : kRootIds) {
        const auto found =
            indexed.find(std::string(id));
        if (found == indexed.end()) {
            throw std::logic_error(
                "ISP1 sealed root is absent from AQ5");
        }
        result.push_back(found->second);
    }
    return result;
}

RootRole root_role(std::string_view stable_id) {
    if (stable_id == kBraingeyser ||
        stable_id == kAncestral ||
        stable_id == kPayableSpike) {
        return RootRole::PrimaryMiss;
    }
    if (std::find(
            kRootIds.begin() + 3, kRootIds.end(),
            stable_id) != kRootIds.end()) {
        return RootRole::Control;
    }
    throw std::invalid_argument(
        "ISP1 root is outside the sealed census");
}

bool semantic_direction_passed(
    std::string_view stable_id,
    const isp0::RootReport& evidence) {
    if (stable_id == kBraingeyser) {
        return selected(
            evidence, "braingeyser-x1-self");
    }
    if (stable_id == kAncestral) {
        return selected(evidence, "ancestral-self");
    }
    if (stable_id == kPayableSpike ||
        stable_id == kRedundantCounter) {
        return selected(evidence, "pass");
    }
    if (stable_id == kInterveningCounter) {
        return intervening_counter_completed(evidence);
    }
    if (stable_id == kGrowth) {
        return selected(
            evidence,
            "growth-own-ironroot-treefolk");
    }
    if (stable_id == kLiveSpike) {
        return selected(
            evidence, "force-spike-gray-ogre");
    }
    if (stable_id == kLife20Block) {
        const auto& pv = evidence.principal_variation;
        return selected(evidence, "no-blocks") &&
               evidence.required_exact_combat_completion &&
               pv.exact_combat_completed &&
               pv.exact_combat_completed_plan_count > 0;
    }
    if (stable_id == kBlueBlock) {
        return blue_block_completed(evidence);
    }
    throw std::invalid_argument(
        "ISP1 semantic check received an unknown root");
}

DiagnosticReport assemble_diagnostic(
    std::string parent_fingerprint,
    const DiagnosticApi& api) {
    if (!api.run_root ||
        !api.check_opponent_noninterference) {
        throw std::invalid_argument(
            "ISP1 diagnostic API is incomplete");
    }
    const std::vector<aq5::PreparedRoot> roots =
        diagnostic_roots();
    DiagnosticReport report{
        .parent_fingerprint =
            std::move(parent_fingerprint),
        .small =
            run_budget(roots, kSmallBudget, api),
        .large =
            run_budget(roots, kLargeBudget, api),
        .exact_configuration =
            information_set_puct::kSimulationCount ==
                kSmallBudget &&
            information_set_puct::kMaximumSimulationCount ==
                kLargeBudget &&
            information_set_puct::kMaximumDecisionPlies == 8 &&
            information_set_puct::kMaximumNodeCount == 513 &&
            information_set_puct::
                    kMaximumExpandedEdgeCount ==
                512 &&
            information_set_puct::kExplorationConstant ==
                1.0,
    };

    report.common_prefix_seed_contract =
        report.small.simulation_count == kSmallBudget &&
        report.large.simulation_count == kLargeBudget &&
        report.small.opponent_noninterference ==
            report.large.opponent_noninterference;
    for (const std::string_view id : kRootIds) {
        const auto& small =
            root_for(report.small, id).evidence;
        const auto& large =
            root_for(report.large, id).evidence;
        report.common_prefix_seed_contract =
            report.common_prefix_seed_contract &&
            small.search_seed != 0 &&
            small.tie_seed != 0 &&
            small.search_seed == large.search_seed &&
            small.tie_seed == large.tie_seed &&
            isp0::transition_seed(
                small.search_seed, 0, 0) ==
                isp0::transition_seed(
                    large.search_seed, 0, 0) &&
            isp0::transition_seed(
                small.search_seed,
                kSmallBudget - 1, 0) ==
                isp0::transition_seed(
                    large.search_seed,
                    kSmallBudget - 1, 0);
    }

    report.no_primary_regression = true;
    for (const std::string_view id :
         {kBraingeyser, kAncestral, kPayableSpike}) {
        const bool small_correct =
            root_for(report.small, id)
                .semantic_direction_passed;
        const bool large_correct =
            root_for(report.large, id)
                .semantic_direction_passed;
        if (small_correct && !large_correct) {
            report.no_primary_regression = false;
        }
        if (!small_correct && large_correct) {
            ++report.primary_improvements;
        }
    }

    const bool infrastructure_green =
        report.parent_fingerprint ==
            isp0::kRequiredParentFingerprint &&
        report.exact_configuration &&
        report.common_prefix_seed_contract &&
        report.no_primary_regression &&
        report.small.gate_passed() &&
        report.large.gate_passed();
    if (!infrastructure_green) {
        report.verdict =
            Verdict::RejectCloseBudgetAxis;
    } else if (
        report.small.primary_misses_correct == 3) {
        report.verdict =
            Verdict::InconclusiveScaling;
    } else if (
        report.large.primary_misses_correct == 3) {
        report.verdict = Verdict::MechanismPass;
    } else if (
        report.large.primary_misses_correct == 2 &&
        report.primary_improvements >= 1) {
        report.verdict =
            Verdict::MechanismSupportCandidateReject;
    } else {
        report.verdict =
            Verdict::RejectCloseBudgetAxis;
    }
    return report;
}

DiagnosticReport run_diagnostic(
    std::shared_ptr<const LearnedModel> parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            isp0::kRequiredParentFingerprint) {
        throw std::invalid_argument(
            "ISP1 requires exact frozen C16");
    }
    DiagnosticApi api{
        .run_root =
            [parent](
                const aq5::PreparedRoot& root,
                std::size_t simulation_count) {
                const auto start =
                    std::chrono::steady_clock::now();
                isp0::RootReport evidence =
                    isp0::run_root_evidence(
                        root, parent, kDiagnosticSeed,
                        simulation_count);
                const auto stop =
                    std::chrono::steady_clock::now();
                return TimedRootEvidence{
                    .evidence = std::move(evidence),
                    .wall_seconds =
                        std::chrono::duration<double>(
                            stop - start)
                            .count(),
                };
            },
        .check_opponent_noninterference =
            [parent](
                const std::vector<aq5::PreparedRoot>& roots,
                std::size_t) {
                return isp0::
                    run_opponent_noninterference_evidence(
                        roots, parent, kDiagnosticSeed);
            },
    };
    return assemble_diagnostic(
        learned_model_fingerprint(parent), api);
}

std::string_view verdict_name(Verdict verdict) {
    switch (verdict) {
    case Verdict::MechanismPass:
        return "MECHANISM_PASS";
    case Verdict::MechanismSupportCandidateReject:
        return "MECHANISM_SUPPORT_CANDIDATE_REJECT";
    case Verdict::InconclusiveScaling:
        return "INCONCLUSIVE_SCALING";
    case Verdict::RejectCloseBudgetAxis:
        return "REJECT_CLOSE_BUDGET_AXIS";
    }
    return "INVALID";
}

void print_report(
    const DiagnosticReport& report,
    std::ostream& output) {
    output << std::fixed << std::setprecision(6);
    output
        << "AQ8-ISP1 64-versus-512 budget diagnostic\n"
        << "seed=" << report.seed
        << " parent=" << report.parent_fingerprint
        << " config=" << report.exact_configuration
        << " common-prefix="
        << report.common_prefix_seed_contract << '\n';
    for (const BudgetReport* budget :
         {&report.small, &report.large}) {
        output
            << "budget=" << budget->simulation_count
            << " primary-correct="
            << budget->primary_misses_correct << "/3"
            << " controls=" << budget->controls_correct
            << "/6 invariants="
            << budget->all_invariants_green
            << " opponent-isolation="
            << budget->opponent_noninterference.gate_passed()
            << '\n';
        for (const RootBudgetReport& root :
             budget->roots) {
            const auto& evidence = root.evidence;
            const auto& accounting = evidence.accounting;
            const auto& pv = evidence.principal_variation;
            output
                << "  root=" << root.stable_id
                << " role="
                << (root.role == RootRole::PrimaryMiss
                        ? "miss"
                        : "control")
                << " selected=" << evidence.selected_key
                << " semantic="
                << root.semantic_direction_passed
                << " invariants="
                << root.invariant_gate_passed
                << " wall-seconds=" << root.wall_seconds
                << " search-seed="
                << evidence.search_seed
                << " tie-seed=" << evidence.tie_seed
                << '\n'
                << "    accounting visits="
                << accounting.simulations_completed
                << " nodes=" << accounting.node_count
                << " edges="
                << accounting.expanded_edge_count
                << " depth=" << accounting.maximum_depth
                << " leaves="
                << accounting.terminal_leaves << '/'
                << accounting.observation_leaves << '/'
                << accounting.depth_leaves << '\n'
                << "    pv";
            for (const std::string& action : pv.actions) {
                output << " [" << action << ']';
            }
            output
                << " completed=" << pv.completed_trace
                << " cutoff=" << pv.completed_cutoff_path
                << " cutoff-sim=";
            if (pv.completed_cutoff_simulation.has_value()) {
                output << *pv.completed_cutoff_simulation;
            } else {
                output << '-';
            }
            output
                << " counterspells="
                << pv.root_actor_counterspells
                << " own-counter-targets="
                << pv
                       .root_actor_counters_targeting_own_counter
                << " underlying-countered="
                << pv
                       .initial_opposing_non_counter_spells_countered
                << " stack-settled=" << pv.stack_settled
                << " exact-combat="
                << pv.exact_combat_completed
                << " cutoff-combat="
                << pv.exact_combat_completed_at_cutoff
                << " plans="
                << pv.exact_combat_completed_plan_count
                << " pure-chump="
                << pv.exact_combat_contains_pure_chump
                << " blocks=";
            for (const auto& [attacker, blocker] :
                 pv.completed_damage_ordered_blocks) {
                output
                    << '[' << attacker << '-' << blocker
                    << ']';
            }
            output
                << '\n'
                << "    invariants coverage="
                << evidence.complete_legal_choice_coverage
                << " prior="
                << evidence.finite_positive_normalized_priors
                << " prior-formula="
                << evidence.exact_successor_value_prior_formula
                << " accounting="
                << evidence.root_visit_accounting_exact
                << " tree-bound="
                << evidence.bounded_tree_accounting
                << " macro-bound="
                << evidence.bounded_macro_accounting
                << " combat-bound="
                << evidence.no_combat_bound_fallback
                << " replay="
                << evidence.deterministic_replay_bit_identical
                << " reverse="
                << evidence
                       .reversed_input_full_evidence_bit_identical
                << " hidden-repartition="
                << evidence.hidden_repartition_nonvacuous
                << " hidden-root="
                << evidence
                       .hidden_root_observation_bit_identical
                << " hidden-full="
                << evidence.hidden_full_evidence_bit_identical
                << " opponent-actions="
                << evidence
                       .opponent_action_accounting_consistent
                << " root-only="
                << evidence.root_observer_only_nodes
                << " opponent-nodes-absent="
                << evidence.opponent_nodes_absent
                << " actor-hand="
                << evidence.actor_hand_preserved
                << " public-state="
                << evidence.public_state_preserved
                << " truth-immutable="
                << evidence
                       .truth_immutable_during_redeterminization
                << " legal-signature="
                << evidence.legal_signature_preserved
                << " transition-seed="
                << evidence
                       .transition_seed_candidate_independent
                << " shared-successor="
                << evidence.required_shared_successor
                << " counter-pv="
                << evidence
                       .required_counter_principal_variation
                << " exact-combat-gate="
                << evidence.required_exact_combat_completion
                << " exact-leaf="
                << evidence.partial_combat_leaf_completed_exactly
                << '\n';
            for (const isp0::RootActionEvidence& action :
                 evidence.actions) {
                output
                    << "    action="
                    << action.fixture_key
                    << " visits=" << action.visits
                    << " q=" << action.actor_q
                    << " prior=" << action.prior
                    << " successor="
                    << action.successor_value << '\n';
            }
        }
        const auto& opponent =
            budget->opponent_noninterference;
        output
            << "  opponent-subfields fixture="
            << opponent.fixture_id
            << " scored-actions="
            << opponent.scored_action_count
            << " selected-membership="
            << opponent.selected_action_membership_count
            << " repartition="
            << opponent.nonvacuous_private_repartition
            << " observation="
            << opponent.opponent_observation_bit_identical
            << " legal="
            << opponent.legal_signature_bit_identical
            << " scores="
            << opponent.c16_scores_bit_identical
            << " selected="
            << opponent.selected_action_bit_identical
            << " local-accounting="
            << opponent.local_accounting_bit_identical
            << " no-node-q="
            << opponent.no_shared_opponent_node_or_q_update
            << " accounting-original="
            << opponent.original_accounting_through_decision
                   .actions_applied
            << '/'
            << opponent.original_accounting_through_decision
                   .phase_transitions
            << '/'
            << opponent.original_accounting_through_decision
                   .turn_advances
            << '/'
            << opponent.original_accounting_through_decision
                   .opponent_decisions_applied
            << " accounting-repartitioned="
            << opponent.repartitioned_accounting_through_decision
                   .actions_applied
            << '/'
            << opponent.repartitioned_accounting_through_decision
                   .phase_transitions
            << '/'
            << opponent.repartitioned_accounting_through_decision
                   .turn_advances
            << '/'
            << opponent.repartitioned_accounting_through_decision
                   .opponent_decisions_applied
            << '\n';
    }
    output
        << "result=" << verdict_name(report.verdict)
        << " no-primary-regression="
        << report.no_primary_regression
        << " primary-improvements="
        << report.primary_improvements
        << " candidate_accepted="
        << report.gate_passed()
        << " strength_claim=0 champion_replaced=0\n";
}

} // namespace old_school::information_set_puct_budget_diagnostic
