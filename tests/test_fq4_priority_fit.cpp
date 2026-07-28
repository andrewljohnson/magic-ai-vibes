#include "old_school/fq4_priority_fit.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fit = old_school::fq4_priority_fit;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_throws(Function&& function,
                   std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool same_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool bit_identical(const std::vector<double>& first,
                   const std::vector<double>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index) {
        if (!same_double(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

double kl_q_to_p(const std::vector<double>& q,
                 const std::vector<double>& p) {
    double result = 0.0;
    for (std::size_t index = 0; index < q.size(); ++index) {
        result += q[index] * std::log(q[index] / p[index]);
    }
    return result;
}

std::vector<double> independent_all_active_formula(
    const std::vector<double>& p, std::size_t pass,
    const std::vector<std::size_t>& active, double ratio) {
    const double denominator =
        ratio + static_cast<double>(active.size());
    const double pass_weight = ratio / denominator;
    const double dominated_weight = 1.0 / denominator;
    long double log_group =
        static_cast<long double>(pass_weight) *
        std::log(
            static_cast<long double>(p[pass] / pass_weight));
    for (const std::size_t index : active) {
        log_group +=
            static_cast<long double>(dominated_weight) *
            std::log(
                static_cast<long double>(
                    p[index] / dominated_weight));
    }
    const long double group_scale = std::exp(log_group);
    long double outside = 0.0L;
    for (std::size_t index = 0; index < p.size(); ++index) {
        if (index != pass &&
            std::find(active.begin(), active.end(), index) ==
                active.end()) {
            outside += static_cast<long double>(p[index]);
        }
    }
    const long double normalization =
        1.0L / (group_scale + outside);
    std::vector<double> q(p.size());
    for (std::size_t index = 0; index < p.size(); ++index) {
        if (index == pass) {
            q[index] =
                static_cast<double>(
                    normalization * group_scale *
                    pass_weight);
        } else if (
            std::find(active.begin(), active.end(), index) !=
            active.end()) {
            q[index] =
                static_cast<double>(
                    normalization * group_scale *
                    dominated_weight);
        } else {
            q[index] =
                static_cast<double>(
                    normalization *
                    static_cast<long double>(p[index]));
        }
    }
    return q;
}

std::shared_ptr<const old_school::LearnedModel> tiny_value_model() {
    static const auto model =
        old_school::train_learned_value_champion(
            1, 0xF004D000ULL);
    return model;
}

fit::FitReport complete_synthetic_report() {
    fit::FitReport report{
        .candidate = tiny_value_model(),
        .discovered_constraints =
            fit::kExpectedDominanceConstraints,
        .parent_margins_below_gate =
            fit::kExpectedParentMarginsBelowGate,
        .candidate_margins_at_gate =
            fit::kExpectedDominanceConstraints,
        .parent_immutable = true,
        .only_priority_component_changed = true,
        .repeated_fit_bit_identical = true,
        .hidden_repartition_bit_identical = true,
        .action_order_bit_identical = true,
        .all_production_base_and_accounting_bit_identical =
            true,
        .every_control_passed = true,
    };
    struct RootSpec {
        std::string_view stable_id;
        std::string_view information_action_fingerprint;
        std::string_view pass_descriptor;
        std::vector<std::string> descriptors;
        std::vector<std::string> dominated;
    };
    const std::vector<RootSpec> roots{
        {
            "field.green.second-main-sick-bear-growth.v1",
            "6bf340aaaca49e8a",
            "pass",
            {
                "growth-own-summoning-sick-grizzly-bears",
                "pass",
            },
            {
                "growth-own-summoning-sick-grizzly-bears",
            },
        },
        {
            "control.blue.braingeyser-x0.v1",
            "a68cd5b38da84990",
            "pass",
            {
                "braingeyser-x0-opponent",
                "braingeyser-x0-self",
                "braingeyser-x1-opponent",
                "braingeyser-x1-self",
                "pass",
            },
            {
                "braingeyser-x0-opponent",
                "braingeyser-x0-self",
            },
        },
        {
            "validation.ru.disintegrate-hold-x0.v1",
            "04d02e0ea36d34be",
            "kind-0.card-0.x-0",
            {
                "kind-0.card-0.x-0",
                "kind-2.card-14.x-0",
                "kind-7.card-17.x-0.target-player-0",
                "kind-7.card-17.x-0.target-player-1",
                "kind-7.card-17.x-1.target-player-0",
                "kind-7.card-17.x-1.target-player-1",
                "kind-7.card-17.x-2.target-player-0",
                "kind-7.card-17.x-2.target-player-1",
            },
            {
                "kind-7.card-17.x-0.target-player-0",
                "kind-7.card-17.x-0.target-player-1",
            },
        },
    };
    std::size_t constraint_number = 0;
    for (const RootSpec& spec : roots) {
        fit::TrainingRootReport root{
            .stable_id = std::string(spec.stable_id),
            .information_action_fingerprint =
                std::string(
                    spec.information_action_fingerprint),
            .descriptors = spec.descriptors,
            .pass_index =
                static_cast<std::size_t>(
                    std::find(
                        spec.descriptors.begin(),
                        spec.descriptors.end(),
                        spec.pass_descriptor) -
                    spec.descriptors.begin()),
            .immutable_base_scores =
                std::vector<double>(
                    spec.descriptors.size(), 0.0),
            .parent_combined_scores =
                std::vector<double>(
                    spec.descriptors.size(), 0.02),
            .candidate_combined_scores =
                std::vector<double>(
                    spec.descriptors.size(), 0.02),
            .parent_latent_probabilities =
                std::vector<double>(
                    spec.descriptors.size(),
                    1.0 /
                        static_cast<double>(
                            spec.descriptors.size())),
            .projected_latent_probabilities =
                std::vector<double>(
                    spec.descriptors.size(),
                    1.0 /
                        static_cast<double>(
                            spec.descriptors.size())),
            .behavior_target_probabilities =
                std::vector<double>(
                    spec.descriptors.size(),
                    1.0 /
                        static_cast<double>(
                            spec.descriptors.size())),
            .parent_exact_support = {
                std::string(spec.pass_descriptor),
            },
            .candidate_exact_support = {
                std::string(spec.pass_descriptor),
            },
            .production_recipe_exact = true,
            .production_base_and_accounting_bit_identical =
                true,
        };
        for (const std::string& descriptor :
             spec.dominated) {
            const auto found = std::find(
                root.descriptors.begin(),
                root.descriptors.end(), descriptor);
            const std::size_t action =
                static_cast<std::size_t>(
                    found - root.descriptors.begin());
            const double parent_margin =
                constraint_number <
                        fit::kExpectedParentMarginsBelowGate
                    ? 0.0
                    : 0.006;
            root.parent_combined_scores[action] =
                root.parent_combined_scores[
                    root.pass_index] -
                parent_margin;
            root.candidate_combined_scores[action] =
                root.candidate_combined_scores[
                    root.pass_index] -
                0.006;
            root.constraints.push_back({
                .descriptor = descriptor,
                .action_index = action,
                .strict_worlds = fit::kDominanceWorlds,
                .active_projection_constraint = true,
                .parent_margin =
                    root.parent_combined_scores[
                        root.pass_index] -
                    root.parent_combined_scores[action],
                .candidate_margin =
                    root.candidate_combined_scores[
                        root.pass_index] -
                    root.candidate_combined_scores[action],
            });
            ++constraint_number;
        }
        report.roots.push_back(std::move(root));
    }

    struct ControlSpec {
        std::string name;
        std::string stable_id;
        std::string information_action_fingerprint;
        std::vector<std::string> descriptors;
    };
    const std::vector<ControlSpec> controls{
            {
                "live Force Spike",
                "control.blue.force-spike-live-gray-ogre.v1",
                "b792d7434096d2cc",
                {"force-spike-gray-ogre", "pass"},
            },
            {
                "useful Giant Growth",
                "green.bolt-on-bear-response.v3",
                "f21baf227fe0161f",
                {"growth-own-grizzly-bears", "pass"},
            },
            {
                "productive Counterspell blue.counter-fire-elemental.v3",
                "blue.counter-fire-elemental.v3",
                "6c90355960714c47",
                {"counter-fire-elemental", "pass"},
            },
            {
                "productive Counterspell blue.counter-lethal-bolt.v3",
                "blue.counter-lethal-bolt.v3",
                "30ff11b9ec056b21",
                {
                    "counter-lethal-lightning-bolt",
                    "pass",
                },
            },
            {
                "productive Counterspell blue.counter-war.v3",
                "blue.counter-war.v3",
                "fc276ae226a9f512",
                {
                    "counter-opponent-counterspell",
                    "counter-own-air-elemental",
                    "pass",
                },
            },
            {
                "payable Force Spike collateral",
                "control.blue.force-spike-payable-gray-ogre.v1",
                "8e24d4696a7c2ad5",
                {"force-spike-gray-ogre", "pass"},
            },
            {
                "redundant same-target Counterspell",
                "control.blue.counter-redundant-same-target.v1",
                "faf53e39aba9e69b",
                {
                    "counter-own-counterspell",
                    "counter-same-air-elemental",
                    "pass",
                },
            },
            {
                "development RU X=0 and positive-X collateral",
                "ru.disintegrate-player-x.v3",
                "6345aec096735eb9",
                {
                    "disintegrate-x0-opponent-player",
                    "disintegrate-x0-self-player",
                    "disintegrate-x1-opponent-player",
                    "disintegrate-x1-self-player",
                    "disintegrate-x2-opponent-player",
                    "disintegrate-x2-self-player",
                    "disintegrate-x3-opponent-player",
                    "disintegrate-x3-self-player",
                    "pass",
                },
            },
            {
                "control.blue.braingeyser-x0.v1 productive-sibling collateral",
                "control.blue.braingeyser-x0.v1",
                "a68cd5b38da84990",
                {
                    "braingeyser-x0-opponent",
                    "braingeyser-x0-self",
                    "braingeyser-x1-opponent",
                    "braingeyser-x1-self",
                    "pass",
                },
            },
            {
                "validation.ru.disintegrate-hold-x0.v1 productive-sibling collateral",
                "validation.ru.disintegrate-hold-x0.v1",
                "04d02e0ea36d34be",
                {
                    "kind-0.card-0.x-0",
                    "kind-2.card-14.x-0",
                    "kind-7.card-17.x-0.target-player-0",
                    "kind-7.card-17.x-0.target-player-1",
                    "kind-7.card-17.x-1.target-player-0",
                    "kind-7.card-17.x-1.target-player-1",
                    "kind-7.card-17.x-2.target-player-0",
                    "kind-7.card-17.x-2.target-player-1",
                },
            },
        };
    for (const ControlSpec& spec : controls) {
        report.controls.push_back({
            .name = spec.name,
            .stable_id = spec.stable_id,
            .information_action_fingerprint =
                spec.information_action_fingerprint,
            .descriptors = spec.descriptors,
            .parent_exact_support = {
                spec.descriptors.front(),
            },
            .candidate_exact_support = {
                spec.descriptors.front(),
            },
            .passed = true,
        });
    }
    return report;
}

void test_projection_rejects_malformed_inputs() {
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection({}, {}, 2.0));
        },
        "empty probability vector was accepted");
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection(
                    {0.0, 1.0}, {}, 2.0));
        },
        "zero probability was accepted");
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection(
                    {0.5,
                     std::numeric_limits<double>::infinity()},
                    {}, 2.0));
        },
        "non-finite probability was accepted");
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection(
                    {0.4, 0.4}, {}, 2.0));
        },
        "unnormalized probabilities were accepted");
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection(
                    {0.5, 0.5}, {{0, 1}}, 1.0));
        },
        "non-strict ratio was accepted");
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection(
                    {0.3, 0.3, 0.4},
                    {{0, 1}, {0, 1}}, 1.1));
        },
        "duplicate constraint was accepted");
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection(
                    {0.3, 0.3, 0.4},
                    {{0, 3}}, 1.1));
        },
        "out-of-range constraint was accepted");
    expect_throws(
        [] {
            static_cast<void>(
                fit::reverse_kl_i_projection(
                    {0.3, 0.3, 0.4},
                    {{0, 1}, {2, 1}}, 1.1));
        },
        "non-star constraints were accepted");
}

void test_unqualified_projection_api_is_unambiguous() {
    using namespace old_school::fq4_priority_fit;
    const std::vector<double> probabilities{
        0.5, 0.3, 0.2};
    const std::vector<StarConstraint> constraints{
        {
            .pass_index = 0,
            .dominated_index = 2,
        },
    };
    const auto projected =
        reverse_kl_i_projection(
            probabilities, constraints, 2.0);
    expect(
        projected.probabilities == probabilities,
        "legacy unqualified projection API drifted");
}

void test_noop_projection_is_bit_identical() {
    const std::vector<double> p{0.6, 0.2, 0.2};
    const auto no_constraints =
        fit::reverse_kl_i_projection(p, {}, 2.0);
    const auto feasible =
        fit::reverse_kl_i_projection(
            p, {{0, 1}, {0, 2}}, 2.0);
    expect(
        bit_identical(no_constraints.probabilities, p) &&
            no_constraints.active_dominated_indices.empty() &&
            bit_identical(feasible.probabilities, p) &&
            feasible.active_dominated_indices.empty(),
        "no-op projection did not preserve parent bits");
}

void test_one_active_projection_matches_independent_formula() {
    constexpr double ratio = 1.4;
    const std::vector<double> p{0.20, 0.65, 0.15};
    const auto actual =
        fit::reverse_kl_i_projection(
            p, {{0, 1}}, ratio);
    const auto expected =
        independent_all_active_formula(
            p, 0, {1}, ratio);
    expect(
        actual.active_dominated_indices ==
                std::vector<std::size_t>{1} &&
            actual.probabilities.size() == expected.size(),
        "single active-set identity drifted");
    for (std::size_t index = 0;
         index < expected.size(); ++index) {
        expect(
            std::abs(
                actual.probabilities[index] -
                expected[index]) < 2.0e-15,
            "single active projection differs from independent formula");
    }
    expect(
        std::abs(
            actual.probabilities[0] -
            ratio * actual.probabilities[1]) <
                2.0e-15,
        "single active constraint is not tight");
}

void test_two_active_projection_is_joint_and_optimal() {
    constexpr double ratio = 1.25;
    const std::vector<double> p{0.10, 0.50, 0.40};
    const std::vector<fit::StarConstraint> constraints{
        {0, 1},
        {0, 2},
    };
    const auto joint =
        fit::reverse_kl_i_projection(
            p, constraints, ratio);
    const auto reversed =
        fit::reverse_kl_i_projection(
            p, {{0, 2}, {0, 1}}, ratio);
    const auto expected =
        independent_all_active_formula(
            p, 0, {1, 2}, ratio);
    expect(
        joint.active_dominated_indices ==
                std::vector<std::size_t>({1, 2}) &&
            bit_identical(
                joint.probabilities,
                reversed.probabilities) &&
            joint.active_dominated_indices ==
                reversed.active_dominated_indices,
        "joint projection depends on constraint order");
    for (std::size_t index = 0;
         index < expected.size(); ++index) {
        expect(
            std::abs(
                joint.probabilities[index] -
                expected[index]) < 2.0e-15,
            "joint projection differs from independent KKT formula");
    }

    const auto first =
        fit::reverse_kl_i_projection(
            p, {{0, 1}}, ratio);
    const auto sequential =
        fit::reverse_kl_i_projection(
            first.probabilities, {{0, 2}}, ratio);
    expect(
        !bit_identical(
            sequential.probabilities,
            joint.probabilities),
        "joint projection silently became sequential projection");

    // Independent grid oracle over the three-action simplex. Its coarse
    // resolution can only upper-bound the true optimum.
    double best_grid =
        std::numeric_limits<double>::infinity();
    std::vector<double> best_q;
    constexpr double step = 0.001;
    for (std::size_t q0_units = 1;
         q0_units < 1000; ++q0_units) {
        const double q0 =
            step * static_cast<double>(q0_units);
        for (std::size_t q1_units = 1;
             q1_units + q0_units < 1000;
             ++q1_units) {
            const double q1 =
                step * static_cast<double>(q1_units);
            const double q2 = 1.0 - q0 - q1;
            if (q2 <= 0.0 ||
                q0 + 1.0e-12 < ratio * q1 ||
                q0 + 1.0e-12 < ratio * q2) {
                continue;
            }
            const std::vector<double> q{q0, q1, q2};
            const double objective = kl_q_to_p(q, p);
            if (objective < best_grid) {
                best_grid = objective;
                best_q = q;
            }
        }
    }
    const double joint_objective =
        kl_q_to_p(joint.probabilities, p);
    expect(
        !best_q.empty() &&
            joint_objective <= best_grid + 1.0e-12,
        "joint projection is worse than independent feasible grid");
    for (std::size_t index = 0; index < 3; ++index) {
        expect(
            std::abs(
                joint.probabilities[index] -
                best_q[index]) < 0.003,
            "joint projection disagrees with grid optimum");
    }
}

void test_mixed_active_projection_matches_kkt_and_grid() {
    constexpr double ratio = 1.30;
    const std::vector<double> p{
        0.18, 0.50, 0.08, 0.24,
    };
    const auto actual =
        fit::reverse_kl_i_projection(
            p, {{0, 1}, {0, 2}}, ratio);
    const auto expected =
        independent_all_active_formula(
            p, 0, {1}, ratio);
    expect(
        actual.active_dominated_indices ==
            std::vector<std::size_t>{1},
        "mixed projection selected the wrong active set");
    for (std::size_t index = 0;
         index < expected.size(); ++index) {
        expect(
            std::abs(
                actual.probabilities[index] -
                expected[index]) < 2.0e-15,
            "mixed projection differs from independent formula");
    }

    const double scale =
        actual.probabilities[3] / p[3];
    const double geometric =
        actual.probabilities[0] / scale;
    expect(
        std::abs(
            actual.probabilities[0] -
            ratio * actual.probabilities[1]) <
                2.0e-15 &&
            actual.probabilities[0] >
                ratio * actual.probabilities[2] &&
            ratio * p[1] >= geometric &&
            ratio * p[2] <= geometric,
        "mixed projection violates active/inactive KKT conditions");
    expect(
        std::abs(
            actual.probabilities[2] /
                    actual.probabilities[3] -
                p[2] / p[3]) <
            2.0e-15,
        "mixed projection did not preserve inactive relative mass");

    double best_grid =
        std::numeric_limits<double>::infinity();
    std::vector<double> best_q;
    constexpr double step = 0.01;
    for (std::size_t q0_units = 1;
         q0_units < 100; ++q0_units) {
        const double q0 =
            step * static_cast<double>(q0_units);
        for (std::size_t q1_units = 1;
             q0_units + q1_units < 100;
             ++q1_units) {
            const double q1 =
                step * static_cast<double>(q1_units);
            for (std::size_t q2_units = 1;
                 q0_units + q1_units + q2_units < 100;
                 ++q2_units) {
                const double q2 =
                    step *
                    static_cast<double>(q2_units);
                const double q3 =
                    1.0 - q0 - q1 - q2;
                if (q3 <= 0.0 ||
                    q0 + 1.0e-12 < ratio * q1 ||
                    q0 + 1.0e-12 < ratio * q2) {
                    continue;
                }
                const std::vector<double> q{
                    q0, q1, q2, q3,
                };
                const double objective =
                    kl_q_to_p(q, p);
                if (objective < best_grid) {
                    best_grid = objective;
                    best_q = q;
                }
            }
        }
    }
    const double actual_objective =
        kl_q_to_p(actual.probabilities, p);
    expect(
        !best_q.empty() &&
            actual_objective <= best_grid + 1.0e-12,
        "mixed projection is worse than independent feasible grid");
    for (std::size_t index = 0;
         index < actual.probabilities.size(); ++index) {
        expect(
            std::abs(
                actual.probabilities[index] -
                best_q[index]) < 0.03,
            "mixed projection disagrees with grid optimum");
    }
}

void test_portable_fit_has_exact_operator_census() {
    const fit::FitReport report =
        fit::testing::fit(tiny_value_model());
    expect(
        report.infrastructure_valid() &&
            fit::classify_exit(report) !=
                fit::ExitClassification::
                    InfrastructureFailure &&
            report.roots.size() == 3 &&
            report.discovered_constraints == 5 &&
            report.parent_immutable &&
            report.only_priority_component_changed &&
            report.repeated_fit_bit_identical &&
            report.hidden_repartition_bit_identical &&
            report.action_order_bit_identical &&
            report
                .all_production_base_and_accounting_bit_identical,
        "portable FQ4-D0 fit lost an infrastructure gate");

    constexpr std::size_t expected_legal[] = {2, 5, 8};
    constexpr std::size_t expected_constraints[] = {1, 2, 2};
    const std::vector<std::vector<std::string>>
        expected_descriptors{
            {
                "growth-own-summoning-sick-grizzly-bears",
            },
            {
                "braingeyser-x0-opponent",
                "braingeyser-x0-self",
            },
            {
                "kind-7.card-17.x-0.target-player-0",
                "kind-7.card-17.x-0.target-player-1",
            },
        };
    for (std::size_t root = 0;
         root < report.roots.size(); ++root) {
        const auto& actual = report.roots[root];
        expect(
            actual.descriptors.size() ==
                    expected_legal[root] &&
                actual.constraints.size() ==
                    expected_constraints[root] &&
                actual.production_recipe_exact &&
                actual
                    .production_base_and_accounting_bit_identical,
            "portable FQ4-D0 root census or recipe drifted");
        std::vector<std::string> descriptors;
        for (const auto& constraint : actual.constraints) {
            expect(
                constraint.strict_worlds ==
                    fit::kDominanceWorlds,
                "dominance label was not strict in all 64 worlds");
            descriptors.push_back(constraint.descriptor);
        }
        expect(
            descriptors == expected_descriptors[root],
            "generic dominance discovery descriptor census drifted");
    }
}

void test_exit_classification_separates_science_and_setup() {
    fit::FitReport report =
        complete_synthetic_report();
    expect(
        fit::classify_exit(report) ==
            fit::ExitClassification::Pass,
        "complete passing report did not classify as exit 0");
    report.scientific_failures.push_back(
        "injected candidate margin failure");
    expect(
        fit::classify_exit(report) ==
            fit::ExitClassification::ScientificReject,
        "complete scientific failure did not classify as exit 1");
    report.hidden_repartition_bit_identical = false;
    expect(
        fit::classify_exit(report) ==
            fit::ExitClassification::InfrastructureFailure,
        "setup/invariance failure did not classify as exit 2");
}

void test_exit_classification_requires_complete_census() {
    fit::FitReport missing_root =
        complete_synthetic_report();
    missing_root.roots.erase(
        missing_root.roots.begin() + 1);
    expect(
        fit::classify_exit(missing_root) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "omitted training root did not classify as setup failure");

    fit::FitReport missing_control =
        complete_synthetic_report();
    missing_control.controls.erase(
        missing_control.controls.begin() + 4);
    expect(
        fit::classify_exit(missing_control) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "omitted collateral control did not classify as setup failure");

    fit::FitReport failed_control =
        complete_synthetic_report();
    failed_control.controls[3].passed = false;
    failed_control.every_control_passed = false;
    failed_control.scientific_failures.push_back(
        "injected control failure");
    expect(
        fit::classify_exit(failed_control) ==
            fit::ExitClassification::
                ScientificReject,
        "complete failed control did not classify as scientific reject");

    fit::FitReport failed_candidate_row =
        complete_synthetic_report();
    failed_candidate_row.roots[0]
        .candidate_exact_support = {
            "growth-own-summoning-sick-grizzly-bears",
        };
    expect(
        fit::classify_exit(failed_candidate_row) ==
            fit::ExitClassification::
                ScientificReject,
        "dominated candidate support did not fail the scientific gate");
}

void test_parent_behavior_failure_completes_report() {
    const fit::FitReport report =
        fit::testing::
            fit_enforcing_parent_behavior_contract(
                tiny_value_model());
    expect(
        !report.infrastructure_failures.empty(),
        "diagnostic parent unexpectedly satisfied every C16 behavior contract");
    expect(
        report.candidate != nullptr &&
            report.roots.size() ==
                fit::kExpectedTrainingRoots &&
            report.controls.size() ==
                fit::kExpectedControls &&
            report.hidden_repartition_bit_identical &&
            report.action_order_bit_identical,
        "parent behavior mismatch short-circuited the complete report");
    expect(
        fit::classify_exit(report) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "completed parent behavior mismatch did not classify as exit 2");
}

void test_production_boundary_rejects_non_c16_parent() {
    expect_throws(
        [] {
            static_cast<void>(
                fit::fit_production(tiny_value_model()));
        },
        "production FQ4-D0 accepted a non-C16 parent");
}

std::vector<double> independent_d0b_behavior(
    const std::vector<double>& scores) {
    const double maximum =
        *std::max_element(scores.begin(), scores.end());
    std::vector<double> probabilities;
    probabilities.reserve(scores.size());
    long double total = 0.0L;
    for (const double score : scores) {
        const double probability = std::exp(
            (score - maximum) /
            fit::kPolicyTemperature);
        probabilities.push_back(probability);
        total += static_cast<long double>(probability);
    }
    const double uniform =
        (1.0 - fit::kSearchChoiceWeight) /
        static_cast<double>(scores.size());
    for (double& probability : probabilities) {
        probability =
            fit::kSearchChoiceWeight *
                static_cast<double>(
                    static_cast<long double>(probability) /
                    total) +
            uniform;
    }
    return probabilities;
}

const fit::D0bReport& portable_d0b_report() {
    static const fit::D0bReport report =
        fit::testing::fit_d0b(tiny_value_model());
    return report;
}

void test_d0b_fixed_optimizer_and_independent_kl() {
    const fit::D0bReport& report =
        portable_d0b_report();
    expect(
        report.infrastructure_valid() &&
            fit::classify_d0b_exit(report) !=
                fit::ExitClassification::
                    InfrastructureFailure,
        "portable D0b lost an infrastructure gate");
    auto normalized = report.treatment.optimizer;
    normalized.epochs = report.anchor.optimizer.epochs;
    expect(
        report.anchor.epochs == fit::kD0bAnchorEpochs &&
            report.treatment.epochs ==
                fit::kD0bTreatmentEpochs &&
            report.optimizer_only_epochs_differ &&
            normalized == report.anchor.optimizer &&
            report.anchor.optimizer.batch_size == 3 &&
            report.anchor.optimizer.seed ==
                fit::kOptimizerSeed &&
            same_double(
                report.anchor.optimizer.learning_rate,
                0.001) &&
            same_double(
                report.anchor.optimizer.residual_weight,
                fit::kResidualWeight) &&
            same_double(
                report.anchor.optimizer.policy_temperature,
                fit::kPolicyTemperature),
        "D0b optimizer differs beyond the fixed epoch treatment");
    expect(
        report.anchor.fit.candidate_fingerprint !=
                report.treatment.fit.candidate_fingerprint &&
            report.anchor.fit.training_input_sha256.size() ==
                64 &&
            report.anchor.fit.training_input_sha256 ==
                report.treatment.fit.training_input_sha256 &&
            report.checkpoint_inputs_bit_identical,
        "D0b did not independently fit two budgets from one input");

    const std::string reset_fingerprint =
        fit::testing::d0b_two_stage_reset_fingerprint(
            tiny_value_model());
    expect(
        reset_fingerprint !=
            report.treatment.fit.candidate_fingerprint,
        "512 treatment silently became two reset 256 fits");

    const auto check_checkpoint =
        [](const fit::D0bCheckpointReport& checkpoint) {
            long double pooled = 0.0L;
            expect(
                checkpoint.root_kl.size() ==
                    checkpoint.fit.roots.size(),
                "D0b KL root census drifted");
            for (std::size_t index = 0;
                 index < checkpoint.root_kl.size();
                 ++index) {
                const auto& root =
                    checkpoint.fit.roots[index];
                const auto& row =
                    checkpoint.root_kl[index];
                const std::vector<double> behavior =
                    independent_d0b_behavior(
                        root.candidate_combined_scores);
                const double kl = kl_q_to_p(
                    root.behavior_target_probabilities,
                    behavior);
                expect(
                    row.stable_id == root.stable_id &&
                        bit_identical(
                            behavior,
                            row.candidate_behavior_probabilities) &&
                        std::abs(
                            kl -
                            row.target_to_candidate_kl) <
                            2.0e-16,
                    "D0b KL differs from independent reconstruction");
                pooled += static_cast<long double>(kl);
            }
            const double mean = static_cast<double>(
                pooled /
                static_cast<long double>(
                    checkpoint.root_kl.size()));
            expect(
                std::abs(
                    mean -
                    checkpoint
                        .pooled_target_to_candidate_kl) <
                    2.0e-16,
                "D0b pooled KL is not the equal-root mean");
        };
    check_checkpoint(report.anchor);
    check_checkpoint(report.treatment);
    for (std::size_t index = 0;
         index < report.anchor.fit.roots.size(); ++index) {
        expect(
            bit_identical(
                report.anchor.fit.roots[index]
                    .behavior_target_probabilities,
                report.treatment.fit.roots[index]
                    .behavior_target_probabilities),
            "D0b treatment target was not frozen from the parent");
    }
}

void test_d0b_margin_contract_is_full_bit_exact() {
    std::vector<double> parent;
    std::vector<double> anchor;
    for (const std::uint64_t bits :
         fit::kD0bRequiredParentMarginBits) {
        parent.push_back(std::bit_cast<double>(bits));
    }
    for (const std::uint64_t bits :
         fit::kD0bRequiredAnchorMarginBits) {
        anchor.push_back(std::bit_cast<double>(bits));
    }
    expect(
        fit::testing::d0b_exact_margin_contract(
            parent, anchor),
        "exact D0b binary64 margin anchors were rejected");
    parent[0] = std::bit_cast<double>(
        std::bit_cast<std::uint64_t>(parent[0]) ^ 1U);
    expect(
        !fit::testing::d0b_exact_margin_contract(
            parent, anchor),
        "one-bit parent margin drift was accepted");
    parent[0] = std::bit_cast<double>(
        fit::kD0bRequiredParentMarginBits[0]);
    anchor.back() = std::bit_cast<double>(
        std::bit_cast<std::uint64_t>(anchor.back()) ^ 1U);
    expect(
        !fit::testing::d0b_exact_margin_contract(
            parent, anchor),
        "one-bit anchor margin drift was accepted");
}

void test_d0b_classification_boundaries_are_self_validating() {
    const fit::D0bReport& source =
        portable_d0b_report();

    fit::D0bReport bad_anchor = source;
    bad_anchor.anchor_contract_qualified = false;
    expect(
        fit::classify_d0b_exit(bad_anchor) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "D0b anchor drift was not exit 2");

    fit::D0bReport bad_optimizer = source;
    bad_optimizer.treatment.optimizer.learning_rate *= 2.0;
    expect(
        fit::classify_d0b_exit(bad_optimizer) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "D0b non-epoch optimizer drift was not exit 2");

    fit::D0bReport bad_input = source;
    bad_input.anchor.fit.training_input_sha256[0] =
        bad_input.anchor.fit.training_input_sha256[0] == '0'
            ? '1'
            : '0';
    expect(
        fit::classify_d0b_exit(bad_input) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "D0b input digest drift was not exit 2");

    fit::D0bReport bad_root = source;
    bad_root.anchor.fit.roots[0]
        .information_action_fingerprint = "drift";
    expect(
        fit::classify_d0b_exit(bad_root) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "D0b root fingerprint drift was not exit 2");

    fit::D0bReport bad_control = source;
    bad_control.treatment.fit.controls[0]
        .descriptors.pop_back();
    expect(
        fit::classify_d0b_exit(bad_control) ==
            fit::ExitClassification::
                InfrastructureFailure,
        "D0b control descriptor drift was not exit 2");

    fit::D0bReport failed_kl = source;
    failed_kl.target_kl_strictly_improved = false;
    failed_kl.scientific_failures.push_back(
        "injected KL failure");
    expect(
        fit::classify_d0b_exit(failed_kl) ==
            fit::ExitClassification::
                ScientificReject,
        "D0b treatment KL miss was not exit 1");

    fit::D0bReport failed_treatment_control = source;
    failed_treatment_control.treatment.fit.controls[0].passed =
        false;
    failed_treatment_control.treatment.fit.every_control_passed =
        false;
    failed_treatment_control.treatment.fit.scientific_failures
        .push_back("injected treatment control failure");
    failed_treatment_control.scientific_failures.push_back(
        "injected treatment control failure");
    expect(
        fit::classify_d0b_exit(
            failed_treatment_control) ==
            fit::ExitClassification::ScientificReject,
        "D0b treatment control miss was not exit 1");
}

void test_d0b_production_boundary_rejects_non_c16_parent() {
    expect_throws(
        [] {
            static_cast<void>(
                fit::fit_d0b_production(
                    tiny_value_model()));
        },
        "production FQ4-D0b accepted a non-C16 parent");
}

class Runner {
  public:
    void run(std::string name,
             const std::function<void()>& test) {
        try {
            test();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& failure) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << failure.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

} // namespace

int main() {
    Runner runner;
    runner.run(
        "projection rejects malformed inputs",
        test_projection_rejects_malformed_inputs);
    runner.run(
        "unqualified projection API",
        test_unqualified_projection_api_is_unambiguous);
    runner.run(
        "no-op projection is bit-identical",
        test_noop_projection_is_bit_identical);
    runner.run(
        "one-active projection analytic oracle",
        test_one_active_projection_matches_independent_formula);
    runner.run(
        "two-active joint projection oracle",
        test_two_active_projection_is_joint_and_optimal);
    runner.run(
        "mixed active/inactive projection oracle",
        test_mixed_active_projection_matches_kkt_and_grid);
    runner.run(
        "portable fit exact operator census",
        test_portable_fit_has_exact_operator_census);
    runner.run(
        "exit classification",
        test_exit_classification_separates_science_and_setup);
    runner.run(
        "classification requires exact report census",
        test_exit_classification_requires_complete_census);
    runner.run(
        "parent contract completes report",
        test_parent_behavior_failure_completes_report);
    runner.run(
        "production parent boundary",
        test_production_boundary_rejects_non_c16_parent);
    runner.run(
        "D0b fixed optimizer and independent KL",
        test_d0b_fixed_optimizer_and_independent_kl);
    runner.run(
        "D0b full-bit margin contract",
        test_d0b_margin_contract_is_full_bit_exact);
    runner.run(
        "D0b classification boundaries",
        test_d0b_classification_boundaries_are_self_validating);
    runner.run(
        "D0b production parent boundary",
        test_d0b_production_boundary_rejects_non_c16_parent);
    return runner.finish();
}
