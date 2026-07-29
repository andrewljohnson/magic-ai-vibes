#include "old_school/information_set_puct_preflight.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace isp0 =
    old_school::information_set_puct_preflight;
namespace aq5 =
    old_school::action_q_recursive_policy_improvement;
namespace puct = old_school::information_set_puct;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

isp0::RootReport passing_root(
    const aq5::PreparedRoot& fixture) {
    const std::string expected =
        isp0::expected_fixture_choice(
            fixture.stable_id);
    isp0::RootReport report{
        .stable_id = fixture.stable_id,
        .family = fixture.family,
        .expected_key = expected,
        .selected_key = expected,
        .accounting = {
            .simulations_started =
                puct::kSimulationCount,
            .simulations_completed =
                puct::kSimulationCount,
            .node_count = 2,
            .expanded_edge_count =
                fixture.candidate_keys.size(),
            .maximum_depth = 2,
            .observation_leaves =
                puct::kSimulationCount,
        },
        .principal_variation = {
            .actions = {expected},
            .completed_trace = true,
            .searched_depth = 1,
        },
        .strategic_direction_passed = true,
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
        .required_counter_principal_variation = true,
        .required_exact_combat_completion = true,
        .partial_combat_leaf_completed_exactly = true,
    };
    const std::size_t width =
        fixture.candidate_keys.size();
    std::size_t remaining_visits =
        puct::kSimulationCount;
    for (std::size_t index = 0;
         index < width; ++index) {
        const std::size_t remaining_actions =
            width - index;
        const std::size_t visits =
            remaining_visits / remaining_actions;
        remaining_visits -= visits;
        report.actions.push_back({
            .fixture_key =
                fixture.candidate_keys[index],
            .engine_key =
                "engine/" +
                fixture.candidate_keys[index],
            .successor_value = 0.5,
            .prior = 1.0 /
                     static_cast<double>(width),
            .visits = visits,
            .actor_q = 0.5,
        });
    }
    return report;
}

isp0::OpponentNoninterferenceReport
passing_opponent_report() {
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

isp0::PreflightApi passing_api() {
    return {
        .run_root =
            [](const aq5::PreparedRoot& fixture) {
                return passing_root(fixture);
            },
        .check_opponent_noninterference =
            [](const std::vector<aq5::PreparedRoot>&) {
                return passing_opponent_report();
            },
        .check_default_off_identity =
            []() { return true; },
    };
}

void test_fixture_expectations_cover_exact_manifest() {
    const auto fixtures = aq5::build_fixture_roots();
    expect(
        fixtures.size() == aq5::kFixtureRootCount,
        "ISP0 fixture census changed");
    std::set<std::string> ids;
    std::set<std::size_t> families;
    for (const auto& fixture : fixtures) {
        const std::string expected =
            isp0::expected_fixture_choice(
                fixture.stable_id);
        expect(
            std::find(
                fixture.candidate_keys.begin(),
                fixture.candidate_keys.end(),
                expected) !=
                fixture.candidate_keys.end(),
            "ISP0 expected action is absent from a fixture");
        expect(
            isp0::fixture_direction_passed(
                fixture.stable_id, expected),
            "ISP0 expected action failed its own direction");
        ids.insert(fixture.stable_id);
        families.insert(
            static_cast<std::size_t>(
                fixture.family));
    }
    expect(
        ids.size() == aq5::kFixtureRootCount &&
            families.size() == 3,
        "ISP0 fixture identities or families are incomplete");
    expect_rejected(
        []() {
            static_cast<void>(
                isp0::expected_fixture_choice(
                    "unknown.fixture"));
        },
        "ISP0 accepted an undeclared fixture");
}

void test_transition_seed_has_only_sealed_coordinates() {
    const std::uint64_t root = UINT64_C(1234567);
    const std::uint64_t first =
        isp0::transition_seed(root, 3, 5);
    expect(
        first ==
            isp0::transition_seed(root, 3, 5),
        "ISP0 transition seed is not deterministic");
    expect(
        first !=
                isp0::transition_seed(root, 3, 6) &&
            first !=
                isp0::transition_seed(root, 4, 5) &&
            first !=
                isp0::transition_seed(
                    root + 1, 3, 5),
        "ISP0 transition seed omitted a sealed coordinate");
}

void test_root_gate_is_a_strict_conjunction() {
    const auto fixtures = aq5::build_fixture_roots();
    isp0::RootReport report =
        passing_root(fixtures.front());
    expect(
        report.gate_passed(),
        "synthetic valid ISP0 root did not pass");
    {
        auto changed = report;
        changed.actions.front().visits += 1;
        expect(
            !changed.gate_passed(),
            "ISP0 root gate ignored edge-visit accounting");
    }
    {
        auto changed = report;
        changed.actions.front().successor_value = 0.9;
        expect(
            !changed.gate_passed(),
            "ISP0 root gate ignored the sealed prior formula");
    }
    {
        auto changed = report;
        changed.hidden_full_evidence_bit_identical =
            false;
        expect(
            !changed.gate_passed(),
            "ISP0 root gate ignored hidden full evidence");
    }
    {
        auto changed = report;
        changed.required_counter_principal_variation =
            false;
        expect(
            !changed.gate_passed(),
            "ISP0 root gate ignored Counter PV evidence");
    }
    {
        auto changed = report;
        changed.transition_seed_candidate_independent =
            false;
        expect(
            !changed.gate_passed(),
            "ISP0 root gate ignored seed provenance");
    }
    {
        auto changed = report;
        changed.opponent_action_accounting_consistent =
            false;
        expect(
            !changed.gate_passed(),
            "ISP0 root gate ignored opponent action accounting");
    }
}

void test_fake_orchestration_accepts_only_all_twelve() {
    const isp0::PreflightReport report =
        isp0::assemble_preflight(
            std::string(
                isp0::kRequiredParentFingerprint),
            passing_api());
    expect(
        report.roots.size() ==
                aq5::kFixtureRootCount &&
            report.all_twelve_roots_present_once &&
            report.all_three_decision_families_present &&
            report.hypothesis_passed &&
            report.gate_passed(),
        "ISP0 fake all-root orchestration did not pass");

    isp0::PreflightApi rejected = passing_api();
    rejected.run_root =
        [](const aq5::PreparedRoot& fixture) {
            isp0::RootReport root =
                passing_root(fixture);
            if (fixture.fixture_ordinal == 0 &&
                fixture.stable_id.find(
                    "redundant") !=
                    std::string::npos) {
                const auto wrong = std::find_if(
                    fixture.candidate_keys.begin(),
                    fixture.candidate_keys.end(),
                    [&](const std::string& key) {
                        return key != root.expected_key;
                    });
                root.selected_key = *wrong;
                root.principal_variation.actions = {
                    *wrong};
                root.strategic_direction_passed = false;
            }
            return root;
        };
    const isp0::PreflightReport failed =
        isp0::assemble_preflight(
            std::string(
                isp0::kRequiredParentFingerprint),
            rejected);
    expect(
        !failed.hypothesis_passed &&
            !failed.gate_passed(),
        "ISP0 fake orchestration accepted eleven roots");
}

void test_isolation_and_opponent_gates_fail_closed() {
    expect(
        passing_opponent_report().gate_passed(),
        "valid opponent noninterference report failed");
    {
        auto changed = passing_opponent_report();
        changed.c16_scores_bit_identical = false;
        expect(
            !changed.gate_passed(),
            "opponent gate ignored C16 score drift");
    }
    {
        auto changed = passing_opponent_report();
        changed.scored_action_count = 0;
        changed.selected_action_membership_count = 0;
        expect(
            !changed.gate_passed(),
            "opponent gate accepted empty action evidence");
    }
    {
        auto api = passing_api();
        api.check_default_off_identity =
            []() { return false; };
        const auto report = isp0::assemble_preflight(
            std::string(
                isp0::kRequiredParentFingerprint),
            api);
        expect(
            !report.isolation.gate_passed() &&
                !report.gate_passed(),
            "ISP0 accepted failed default-off identity");
    }
    expect_rejected(
        []() {
            isp0::PreflightApi incomplete;
            static_cast<void>(
                isp0::assemble_preflight(
                    std::string(
                        isp0::
                            kRequiredParentFingerprint),
                    incomplete));
        },
        "ISP0 accepted an incomplete callback API");
}

} // namespace

int main() {
    try {
        test_fixture_expectations_cover_exact_manifest();
        test_transition_seed_has_only_sealed_coordinates();
        test_root_gate_is_a_strict_conjunction();
        test_fake_orchestration_accepts_only_all_twelve();
        test_isolation_and_opponent_gates_fail_closed();
    } catch (const std::exception& error) {
        std::cerr
            << "information-set PUCT preflight tests failed: "
            << error.what() << '\n';
        return 1;
    }
    std::cout
        << "information-set PUCT preflight tests passed: "
           "5/5 groups\n";
    return 0;
}
