#include "old_school/information_set_puct_budget_diagnostic.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace isp0 =
    old_school::information_set_puct_preflight;
namespace isp1 =
    old_school::information_set_puct_budget_diagnostic;
namespace aq5 =
    old_school::action_q_recursive_policy_improvement;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::string correct_key(std::string_view id) {
    if (id ==
        "control.blue.counter-same-target-after-"
        "intervening-counter.v1") {
        return "counter-opponent-counterspell";
    }
    return isp0::expected_fixture_choice(id);
}

std::string wrong_key(const aq5::PreparedRoot& root) {
    const std::string correct =
        correct_key(root.stable_id);
    const auto found = std::find_if(
        root.candidate_keys.begin(),
        root.candidate_keys.end(),
        [&](const std::string& key) {
            return key != correct;
        });
    if (found == root.candidate_keys.end()) {
        throw std::logic_error(
            "fake ISP1 root has no wrong action");
    }
    return *found;
}

isp0::RootReport passing_evidence(
    const aq5::PreparedRoot& fixture,
    std::size_t simulation_count,
    bool semantically_correct) {
    const std::string selected =
        semantically_correct
            ? correct_key(fixture.stable_id)
            : wrong_key(fixture);
    isp0::RootReport report{
        .stable_id = fixture.stable_id,
        .family = fixture.family,
        .expected_key =
            isp0::expected_fixture_choice(
                fixture.stable_id),
        .selected_key = selected,
        .requested_simulations =
            simulation_count,
        .search_seed =
            UINT64_C(0x4953503100000000) +
            fixture.fixture_ordinal,
        .tie_seed =
            UINT64_C(0x4953503200000000) +
            fixture.fixture_ordinal,
        .accounting = {
            .simulations_started = simulation_count,
            .simulations_completed = simulation_count,
            .node_count = 2,
            .expanded_edge_count =
                fixture.candidate_keys.size(),
            .maximum_depth = 2,
            .observation_leaves = simulation_count,
        },
        .principal_variation = {
            .actions = {selected},
            .completed_trace = true,
            .searched_depth = 1,
        },
        .complete_legal_choice_coverage = true,
        .finite_positive_normalized_priors = true,
        .exact_successor_value_prior_formula = true,
        .root_visit_accounting_exact = true,
        .bounded_tree_accounting = true,
        .bounded_macro_accounting = true,
        .opponent_action_accounting_consistent = true,
        .no_combat_bound_fallback = true,
        .transition_seed_candidate_independent = true,
        .deterministic_replay_bit_identical = true,
        .reversed_input_full_evidence_bit_identical = true,
        .hidden_repartition_nonvacuous = true,
        .hidden_root_observation_bit_identical = true,
        .hidden_full_evidence_bit_identical = true,
        .root_observer_only_nodes = true,
        .opponent_nodes_absent = true,
        .actor_hand_preserved = true,
        .public_state_preserved = true,
        .truth_immutable_during_redeterminization = true,
        .legal_signature_preserved = true,
        .required_shared_successor = true,
        .partial_combat_leaf_completed_exactly = true,
    };
    if (fixture.stable_id ==
        "control.blue.counter-same-target-after-"
        "intervening-counter.v1") {
        report.principal_variation.actions.push_back(
            "pass");
        report.principal_variation.searched_depth = 2;
        report.principal_variation
            .root_actor_counterspells = 1;
        report.principal_variation
            .initial_opposing_non_counter_spells_countered =
            true;
        report.principal_variation.stack_settled = true;
    }
    if (fixture.stable_id ==
        "field.ru.life20-flying-men-chump-air.v1") {
        report.required_exact_combat_completion = true;
        report.principal_variation
            .exact_combat_completed = true;
        report.principal_variation
            .exact_combat_completed_plan_count = 1;
    }
    if (fixture.stable_id ==
        aq5::kNewBlueBlockFixtureId) {
        report.required_exact_combat_completion = true;
        report.principal_variation
            .completed_cutoff_path = true;
        report.principal_variation
            .completed_cutoff_simulation = 7;
        report.principal_variation
            .exact_combat_completed = true;
        report.principal_variation
            .exact_combat_completed_at_cutoff = true;
        report.principal_variation
            .exact_combat_completed_plan_count = 2;
        report.principal_variation
            .completed_damage_ordered_blocks = {{2, 3}};
    }

    std::size_t remaining = simulation_count;
    for (std::size_t index = 0;
         index < fixture.candidate_keys.size();
         ++index) {
        const std::size_t remaining_actions =
            fixture.candidate_keys.size() - index;
        const std::size_t visits =
            remaining / remaining_actions;
        remaining -= visits;
        report.actions.push_back({
            .fixture_key =
                fixture.candidate_keys[index],
            .engine_key =
                "engine/" +
                fixture.candidate_keys[index],
            .successor_value = 0.5,
            .prior =
                1.0 /
                static_cast<double>(
                    fixture.candidate_keys.size()),
            .visits = visits,
            .actor_q = 0.5,
        });
    }
    return report;
}

isp0::OpponentNoninterferenceReport
passing_opponent() {
    return {
        .fixture_id =
            "control.blue.counter-redundant-same-target.v1",
        .scored_action_count = 2,
        .selected_action_membership_count = 1,
        .nonvacuous_private_repartition = true,
        .opponent_observation_bit_identical = true,
        .legal_signature_bit_identical = true,
        .c16_scores_bit_identical = true,
        .selected_action_bit_identical = true,
        .local_accounting_bit_identical = true,
        .no_shared_opponent_node_or_q_update = true,
    };
}

isp1::DiagnosticApi fake_api(
    std::set<std::string> small_correct,
    std::set<std::string> large_correct,
    std::string failed_invariant = {}) {
    return {
        .run_root =
            [small_correct = std::move(small_correct),
             large_correct = std::move(large_correct),
             failed_invariant = std::move(failed_invariant)](
                const aq5::PreparedRoot& root,
                std::size_t simulation_count) {
                const bool primary =
                    isp1::root_role(root.stable_id) ==
                    isp1::RootRole::PrimaryMiss;
                const bool correct =
                    !primary ||
                    (simulation_count ==
                             isp1::kSmallBudget
                         ? small_correct.contains(
                               root.stable_id)
                         : large_correct.contains(
                               root.stable_id));
                isp0::RootReport evidence =
                    passing_evidence(
                        root, simulation_count, correct);
                if (root.stable_id == failed_invariant) {
                    evidence
                        .deterministic_replay_bit_identical =
                        false;
                }
                return isp1::TimedRootEvidence{
                    .evidence = std::move(evidence),
                    .wall_seconds = 0.125,
                };
            },
        .check_opponent_noninterference =
            [](const std::vector<aq5::PreparedRoot>&,
               std::size_t) {
                return passing_opponent();
            },
    };
}

const std::set<std::string> kAllMisses = {
    "control.blue.braingeyser-x0.v1",
    "field.blue.ancestral-opponent-seed24.aq0.v1",
    "control.blue.force-spike-payable-five-open-gray-ogre.aq0.v1",
};

void test_manifest_and_semantic_controls() {
    const auto roots = isp1::diagnostic_roots();
    expect(
        roots.size() == isp1::kRootCount,
        "ISP1 did not select exactly nine roots");
    std::size_t misses = 0;
    for (const auto& root : roots) {
        misses +=
            isp1::root_role(root.stable_id) ==
            isp1::RootRole::PrimaryMiss;
        const auto evidence =
            passing_evidence(
                root, isp1::kSmallBudget, true);
        expect(
            isp1::semantic_direction_passed(
                root.stable_id, evidence),
            "ISP1 rejected a correct semantic witness");
    }
    expect(misses == 3, "ISP1 role census drifted");
}

void test_precommitted_verdict_rules() {
    const std::string fingerprint(
        isp0::kRequiredParentFingerprint);
    const auto pass =
        isp1::assemble_diagnostic(
            fingerprint,
            fake_api({}, kAllMisses));
    expect(
        pass.verdict ==
                isp1::Verdict::MechanismPass &&
            pass.primary_improvements == 3 &&
            pass.gate_passed(),
        "ISP1 did not pass three genuine improvements");

    std::set<std::string> two = kAllMisses;
    two.erase(two.begin());
    const auto support =
        isp1::assemble_diagnostic(
            fingerprint,
            fake_api({}, two));
    expect(
        support.verdict ==
                isp1::Verdict::
                    MechanismSupportCandidateReject &&
            support.primary_improvements == 2 &&
            !support.gate_passed(),
        "ISP1 did not report two genuine improvements as "
        "support/reject");

    const auto unchanged_two =
        isp1::assemble_diagnostic(
            fingerprint,
            fake_api(two, two));
    expect(
        unchanged_two.verdict ==
                isp1::Verdict::RejectCloseBudgetAxis &&
            unchanged_two.primary_improvements == 0,
        "ISP1 treated two unchanged 64-correct roots as "
        "scaling support");

    const auto inconclusive =
        isp1::assemble_diagnostic(
            fingerprint,
            fake_api(kAllMisses, kAllMisses));
    expect(
        inconclusive.verdict ==
            isp1::Verdict::InconclusiveScaling,
        "ISP1 did not mark an already-perfect 64 budget "
        "inconclusive");

    std::set<std::string> regressed = kAllMisses;
    regressed.erase(regressed.begin());
    const auto regression =
        isp1::assemble_diagnostic(
            fingerprint,
            fake_api(kAllMisses, regressed));
    expect(
        !regression.no_primary_regression &&
            regression.verdict ==
                isp1::Verdict::RejectCloseBudgetAxis,
        "ISP1 did not close the axis on a primary regression");
}

void test_invariants_and_output_fail_closed() {
    const auto failed =
        isp1::assemble_diagnostic(
            std::string(
                isp0::kRequiredParentFingerprint),
            fake_api(
                {}, kAllMisses,
                "control.blue.counter-redundant-"
                "same-target.v1"));
    expect(
        !failed.large.all_invariants_green &&
            failed.verdict ==
                isp1::Verdict::RejectCloseBudgetAxis,
        "ISP1 ignored an evidence invariant failure");

    auto broken_prefix_api =
        fake_api({}, kAllMisses);
    const auto base_runner =
        broken_prefix_api.run_root;
    broken_prefix_api.run_root =
        [base_runner](
            const aq5::PreparedRoot& root,
            std::size_t simulation_count) {
            auto measured =
                base_runner(root, simulation_count);
            if (simulation_count ==
                    isp1::kLargeBudget &&
                root.fixture_ordinal == 1) {
                ++measured.evidence.search_seed;
            }
            return measured;
        };
    const auto broken_prefix =
        isp1::assemble_diagnostic(
            std::string(
                isp0::kRequiredParentFingerprint),
            broken_prefix_api);
    expect(
        !broken_prefix.common_prefix_seed_contract &&
            broken_prefix.verdict ==
                isp1::Verdict::RejectCloseBudgetAxis,
        "ISP1 accepted vacuous or divergent common-prefix "
        "seed evidence");

    auto incomplete_life20 =
        passing_evidence(
            isp1::diagnostic_roots().at(7),
            isp1::kSmallBudget, true);
    incomplete_life20.required_exact_combat_completion =
        false;
    incomplete_life20.principal_variation
        .exact_combat_completed = false;
    incomplete_life20.principal_variation
        .exact_combat_completed_plan_count = 0;
    expect(
        !isp1::semantic_direction_passed(
            "field.ru.life20-flying-men-chump-air.v1",
            incomplete_life20),
        "ISP1 accepted life-20 No Blocks without completed "
        "exact-combat evidence");

    auto visible_mutation_api =
        fake_api({}, kAllMisses);
    const auto visible_base_runner =
        visible_mutation_api.run_root;
    visible_mutation_api.run_root =
        [visible_base_runner](
            const aq5::PreparedRoot& root,
            std::size_t simulation_count) {
            auto measured =
                visible_base_runner(root, simulation_count);
            if (simulation_count ==
                    isp1::kLargeBudget &&
                root.fixture_ordinal == 1) {
                measured.evidence
                    .hidden_root_observation_bit_identical =
                    false;
            }
            return measured;
        };
    visible_mutation_api.check_opponent_noninterference =
        [](const std::vector<aq5::PreparedRoot>&,
           std::size_t) {
            auto opponent = passing_opponent();
            opponent.selected_action_membership_count = 0;
            opponent.original_accounting_through_decision = {
                .actions_applied = 1,
                .phase_transitions = 2,
                .turn_advances = 3,
                .opponent_decisions_applied = 4,
            };
            opponent.repartitioned_accounting_through_decision = {
                .actions_applied = 5,
                .phase_transitions = 6,
                .turn_advances = 7,
                .opponent_decisions_applied = 8,
            };
            return opponent;
        };
    const auto visible_mutation =
        isp1::assemble_diagnostic(
            std::string(
                isp0::kRequiredParentFingerprint),
            visible_mutation_api);

    std::ostringstream output;
    isp1::print_report(visible_mutation, output);
    const std::string text = output.str();
    for (const std::string_view token : {
             "budget=64",
             "budget=512",
             "visits=",
             " q=",
             " prior=",
             "leaves=",
             "underlying-countered=",
             "cutoff-combat=",
             "cutoff-sim=7",
             "blocks=[2-3]",
             "hidden-repartition=",
             "hidden-root=0",
             "hidden-full=",
             "actor-hand=",
             "public-state=",
             "truth-immutable=",
             "legal-signature=",
             "transition-seed=",
             "shared-successor=",
             "opponent-subfields",
             "scored-actions=",
             "selected-membership=0",
             "accounting-original=1/2/3/4",
             "accounting-repartitioned=5/6/7/8",
             "primary-improvements=",
         }) {
        expect(
            text.find(token) != std::string::npos,
            "ISP1 report omitted required evidence");
    }
}

} // namespace

int main() {
    try {
        test_manifest_and_semantic_controls();
        test_precommitted_verdict_rules();
        test_invariants_and_output_fail_closed();
    } catch (const std::exception& error) {
        std::cerr
            << "information-set PUCT budget diagnostic "
               "tests failed: "
            << error.what() << '\n';
        return 1;
    }
    std::cout
        << "information-set PUCT budget diagnostic tests "
           "passed: 3/3 groups\n";
    return 0;
}
