#include "old_school/action_q_nested_actor_distill.hpp"
#include "old_school/action_q_field_gate.hpp"
#include "old_school/decision_boundary_adaptive_trunk.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace adaptive =
    old_school::decision_boundary_adaptive_trunk;
namespace pair =
    old_school::decision_boundary_action_pair;
namespace direct =
    old_school::decision_boundary_rank_direct;
namespace g1 =
    old_school::action_q_nested_actor_distill;
namespace field =
    old_school::action_q_field_gate;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void expect_near(
    double actual, double expected, double tolerance,
    std::string_view message) {
    if (!std::isfinite(actual) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            std::string(message) + ": actual=" +
            std::to_string(actual) + " expected=" +
            std::to_string(expected));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

template <typename T>
concept HasStateMember = requires(T value) {
    value.state;
};

template <typename T>
concept HasOpponentHandMember = requires(T value) {
    value.opponent_hand;
};

template <typename T>
concept HasOpponentLibraryMember = requires(T value) {
    value.opponent_library;
};

static_assert(!HasStateMember<adaptive::Root>);
static_assert(!HasOpponentHandMember<adaptive::Root>);
static_assert(
    !HasOpponentLibraryMember<adaptive::Root>);
static_assert(adaptive::kPolicyFeatureCount == 893);
static_assert(adaptive::kHiddenCount == 32);
static_assert(adaptive::kParameterCount == 96);
static_assert(adaptive::kFoldCount == 4);

double sigmoid(double value) {
    if (value >= 0.0) {
        const double exponential = std::exp(-value);
        return 1.0 / (1.0 + exponential);
    }
    const double exponential = std::exp(value);
    return exponential / (1.0 + exponential);
}

double bce_from_logit(double target, double logit) {
    const double softplus =
        logit > 0.0
        ? logit + std::log1p(std::exp(-logit))
        : std::log1p(std::exp(logit));
    return softplus - target * logit;
}

direct::RankAction make_action(
    double teacher, double parent,
    std::size_t worlds = 2) {
    direct::RankAction result;
    for (std::size_t world = 0;
         world < worlds; ++world) {
        direct::RankCell cell{
            .observation =
                std::vector<double>(
                    direct::kFeatureCount, 0.0),
            .parent_leaf_values = {parent, parent},
            .teacher_target = teacher,
        };
        cell.observation[world] =
            0.015625 *
            static_cast<double>(world + 1);
        result.worlds.push_back(std::move(cell));
    }
    return result;
}

adaptive::Root make_root(
    std::string id, old_school::DeckId deck,
    std::size_t schedule_index, std::size_t actor,
    const std::vector<double>& teacher,
    const std::vector<double>& parent,
    std::vector<adaptive::Hidden> preactivations) {
    expect(
        teacher.size() == parent.size() &&
            teacher.size() ==
                preactivations.size() &&
            teacher.size() >= 2,
        "adaptive synthetic root width drifted");
    adaptive::Root root{
        .paired =
            pair::Root{
                .ranking =
                    direct::RankRoot{
                        .stable_root_id =
                            std::move(id),
                        .deck = deck,
                    },
                .schedule_index = schedule_index,
                .actor = actor,
            },
        .parent_preactivations =
            std::move(preactivations),
    };
    for (std::size_t action = 0;
         action < teacher.size(); ++action) {
        root.paired.ranking.actions.push_back(
            make_action(
                teacher[action], parent[action]));
        root.paired.options.emplace_back(
            adaptive::kPolicyFeatureCount, 0.0);
        adaptive::Hidden activation{};
        for (std::size_t hidden = 0;
             hidden < adaptive::kHiddenCount;
             ++hidden) {
            activation[hidden] =
                std::tanh(
                    root.parent_preactivations
                        [action][hidden]);
        }
        root.paired.hidden.push_back(activation);
    }
    return root;
}

adaptive::Dataset replicated_dataset(
    const std::vector<double>& teacher,
    const std::vector<double>& parent,
    const std::vector<adaptive::Hidden>&
        preactivations,
    std::size_t copies = 1) {
    std::vector<adaptive::Root> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < copies; ++copy) {
            roots.push_back(
                make_root(
                    "adaptive-" +
                        std::to_string(deck) + "-" +
                        std::to_string(copy),
                    static_cast<old_school::DeckId>(
                        deck),
                    deck * copies + copy,
                    copy % 2, teacher, parent,
                    preactivations));
        }
    }
    return adaptive::testing::make_dataset(
        std::move(roots));
}

adaptive::Dataset learning_dataset() {
    std::vector<adaptive::Root> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < 2; ++copy) {
            std::vector<adaptive::Hidden>
                preactivations(2);
            preactivations[0][deck] = 0.8;
            preactivations[1][deck] = -0.2;
            roots.push_back(
                make_root(
                    "learn-" +
                        std::to_string(deck) + "-" +
                        std::to_string(copy),
                    static_cast<old_school::DeckId>(
                        deck),
                    2 * deck + copy, copy,
                    {0.9, 0.1},
                    {0.499, 0.501},
                    std::move(preactivations)));
        }
    }
    return adaptive::testing::make_dataset(
        std::move(roots));
}

std::shared_ptr<const old_school::LearnedModel>
test_model() {
    static const auto model =
        old_school::train_learned_model(
            1, 0xDBC5A11CEULL);
    return model;
}

adaptive::Hidden model_preactivation(
    const std::vector<double>& option,
    const old_school::LearnedPriorityHeadParameters&
        parameters) {
    adaptive::Hidden result{};
    for (std::size_t hidden = 0;
         hidden < adaptive::kHiddenCount; ++hidden) {
        double value = 0.0;
        for (std::size_t feature = 0;
             feature < option.size(); ++feature) {
            value +=
                parameters.input_hidden[hidden][feature] *
                option[feature];
        }
        result[hidden] = value;
    }
    return result;
}

adaptive::Root model_root(
    std::string id, old_school::DeckId deck,
    std::size_t schedule, std::size_t actor,
    const old_school::LearnedPriorityHeadParameters&
        parameters,
    const std::vector<double>& teacher =
        {0.9, 0.1},
    const std::vector<double>& parent =
        {0.499, 0.501}) {
    std::vector<adaptive::Hidden>
        preactivations(teacher.size());
    adaptive::Root root =
        make_root(
            std::move(id), deck, schedule, actor,
            teacher, parent, preactivations);
    for (std::size_t action = 0;
         action < teacher.size(); ++action) {
        root.paired.options[action][0] =
            action == 0 ? 1.0 : -1.0;
        root.parent_preactivations[action] =
            model_preactivation(
                root.paired.options[action],
                parameters);
        for (std::size_t hidden = 0;
             hidden < adaptive::kHiddenCount;
             ++hidden) {
            root.paired.hidden[action][hidden] =
                std::tanh(
                    root.parent_preactivations
                        [action][hidden]);
        }
    }
    return root;
}

adaptive::Dataset model_dataset(
    std::shared_ptr<const old_school::LearnedModel>
        parent,
    std::size_t copies = 2) {
    const auto parameters =
        old_school::learned_priority_head_parameters(
            parent);
    std::vector<adaptive::Root> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < copies; ++copy) {
            roots.push_back(
                model_root(
                    "model-" +
                        std::to_string(deck) + "-" +
                        std::to_string(copy),
                    static_cast<old_school::DeckId>(
                        deck),
                    deck * copies + copy,
                    copy % 2, parameters));
        }
    }
    return adaptive::testing::make_dataset(
        std::move(roots));
}

adaptive::Dataset fold_dataset(
    std::shared_ptr<const old_school::LearnedModel>
        parent) {
    const auto parameters =
        old_school::learned_priority_head_parameters(
            parent);
    std::vector<adaptive::Root> roots;
    roots.reserve(80);
    for (std::size_t fold = 0;
         fold < adaptive::kFoldCount; ++fold) {
        for (std::size_t game = 0;
             game < 10; ++game) {
            const std::size_t schedule =
                fold +
                adaptive::kFoldCount * game;
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                const std::size_t deck =
                    (2 * game + actor) %
                    old_school::kDeckCount;
                roots.push_back(
                    model_root(
                        "fold-" +
                            std::to_string(schedule) +
                            "-" +
                            std::to_string(actor),
                        static_cast<
                            old_school::DeckId>(deck),
                        schedule, actor, parameters));
            }
        }
    }
    return adaptive::testing::make_dataset(
        std::move(roots));
}

pair::Dataset paired_dataset(
    const adaptive::Dataset& dataset) {
    std::vector<pair::Root> roots;
    roots.reserve(dataset.roots.size());
    for (const adaptive::Root& root :
         dataset.roots) {
        roots.push_back(root.paired);
    }
    return pair::testing::make_dataset(
        std::move(roots));
}

double parameter_l2_squared(
    const adaptive::Parameters& parameters) {
    double result = 0.0;
    for (const auto* block :
         {&parameters.gain,
          &parameters.bias,
          &parameters.output}) {
        for (const double value : *block) {
            result += value * value;
        }
    }
    return result;
}

std::vector<std::vector<double>>
priority_option_rows(
    const field::AncestralFieldRoot& root) {
    std::vector<std::vector<double>> result;
    result.reserve(root.legal_actions.size());
    for (const old_school::PriorityAction& action :
         root.legal_actions) {
        result.push_back(
            old_school::
                learned_priority_policy_features(
                    root.state, root.actor, action,
                    root.context.sorcery_actions,
                    root.context.phase,
                    root.context.consecutive_passes));
    }
    return result;
}

std::vector<double> centered_residuals(
    const std::vector<double>& logits) {
    expect(
        !logits.empty(),
        "hidden-repartition logits are empty");
    double mean = 0.0;
    for (const double logit : logits) {
        mean +=
            logit /
            static_cast<double>(logits.size());
    }
    std::vector<double> result = logits;
    for (double& value : result) {
        value =
            adaptive::kResidualWeight *
            std::tanh(value - mean);
    }
    return result;
}

std::vector<double> center_logits(
    const std::vector<double>& logits) {
    expect(
        !logits.empty(),
        "hidden-repartition logits are empty");
    const double mean =
        std::accumulate(
            logits.begin(), logits.end(), 0.0) /
        static_cast<double>(logits.size());
    std::vector<double> result = logits;
    for (double& value : result) {
        value -= mean;
    }
    return result;
}

std::size_t most_action_sensitive_hidden_unit(
    const std::vector<std::vector<double>>& options,
    const old_school::LearnedPriorityHeadParameters&
        parameters) {
    std::size_t best = 0;
    double best_range = -1.0;
    for (std::size_t hidden = 0;
         hidden < adaptive::kHiddenCount; ++hidden) {
        double minimum =
            std::numeric_limits<double>::infinity();
        double maximum =
            -std::numeric_limits<double>::infinity();
        for (const auto& option : options) {
            const adaptive::Hidden preactivation =
                model_preactivation(
                    option, parameters);
            const double activation =
                std::tanh(
                    preactivation[hidden]);
            minimum = std::min(minimum, activation);
            maximum = std::max(maximum, activation);
        }
        if (maximum - minimum > best_range) {
            best_range = maximum - minimum;
            best = hidden;
        }
    }
    expect(
        best_range > 0.0,
        "live Priority options did not separate a hidden unit");
    return best;
}

void test_fixed_recipe_and_owner_safe_shape() {
    const adaptive::OptimizerConfig recipe;
    expect(
        recipe.fit_tag == 202607291701ULL &&
            adaptive::kSelectorSeed ==
                202607291711ULL &&
            recipe.steps == 256 &&
            recipe.learning_rate == 0.001 &&
            recipe.beta_one == 0.9 &&
            recipe.beta_two == 0.999 &&
            recipe.epsilon == 1.0e-8 &&
            recipe.residual_weight == 0.10 &&
            recipe.pair_temperature == 0.10 &&
            recipe.l2_tether == 0.10 &&
            recipe.global_gradient_norm_clip == 5.0,
        "fixed DBC5 recipe drifted");
    const adaptive::Dataset dataset =
        learning_dataset();
    expect(
        dataset.roots.size() == 10,
        "adaptive synthetic root count drifted");
    for (const std::size_t roots :
         dataset.roots_by_deck) {
        expect(
            roots == 2,
            "adaptive synthetic deck balance drifted");
    }
}

void test_forward_and_gradient_math() {
    std::vector<adaptive::Hidden>
        preactivations(2);
    preactivations[0][0] = 0.5;
    preactivations[0][1] = -0.3;
    preactivations[1][0] = -0.25;
    preactivations[1][1] = 0.4;
    const adaptive::Dataset dataset =
        replicated_dataset(
            {0.8, 0.2}, {0.4, 0.6},
            preactivations);
    adaptive::Parameters parameters;
    parameters.gain[0] = 0.2;
    parameters.bias[0] = 0.1;
    parameters.output[0] = 0.3;
    parameters.gain[1] = -0.1;
    parameters.bias[1] = -0.05;
    parameters.output[1] = -0.2;

    const adaptive::testing::ForwardProbe forward =
        adaptive::testing::analytic_forward(
            dataset.roots.front(), parameters);
    std::array<std::array<double, 2>, 2>
        transformed{};
    std::array<std::array<double, 2>, 2>
        activation{};
    std::array<double, 2> logits{};
    for (std::size_t action = 0;
         action < 2; ++action) {
        for (std::size_t hidden = 0;
             hidden < 2; ++hidden) {
            transformed[action][hidden] =
                (1.0 + parameters.gain[hidden]) *
                    preactivations[action][hidden] +
                parameters.bias[hidden];
            activation[action][hidden] =
                std::tanh(
                    transformed[action][hidden]);
            logits[action] +=
                parameters.output[hidden] *
                activation[action][hidden];
            expect_near(
                forward.preactivations[action][hidden],
                transformed[action][hidden],
                1.0e-15,
                "adaptive preactivation drifted");
            expect_near(
                forward.activations[action][hidden],
                activation[action][hidden],
                1.0e-15,
                "adaptive activation drifted");
        }
        expect_near(
            forward.logits[action],
            logits[action], 1.0e-15,
            "adaptive logit drifted");
    }
    const double mean_logit =
        (logits[0] + logits[1]) / 2.0;
    std::array<double, 2> residual{};
    std::array<double, 2> score{};
    for (std::size_t action = 0;
         action < 2; ++action) {
        residual[action] =
            adaptive::kResidualWeight *
            std::tanh(
                logits[action] - mean_logit);
        score[action] =
            (action == 0 ? 0.4 : 0.6) +
            residual[action];
        expect_near(
            forward.residuals[action],
            residual[action], 1.0e-15,
            "adaptive residual drifted");
    }

    const double teacher_difference = 0.6;
    const double target =
        sigmoid(
            teacher_difference /
            adaptive::kPairTemperature);
    const double candidate_logit =
        (score[0] - score[1]) /
        adaptive::kPairTemperature;
    const double expected_objective =
        bce_from_logit(
            target, candidate_logit) +
        0.5 * adaptive::kL2Tether *
            parameter_l2_squared(parameters);
    const adaptive::testing::ObjectiveProbe probe =
        adaptive::testing::objective_probe(
            dataset, parameters);
    expect_near(
        probe.objective, expected_objective,
        1.0e-14,
        "adaptive objective drifted");

    const double prediction =
        sigmoid(candidate_logit);
    const double loss_derivative =
        (prediction - target) /
        adaptive::kPairTemperature;
    const auto expected_coordinate =
        [&](std::size_t hidden,
            char block) {
            std::array<double, 2>
                logit_derivative{};
            for (std::size_t action = 0;
                 action < 2; ++action) {
                const double sech_squared =
                    1.0 -
                    activation[action][hidden] *
                        activation[action][hidden];
                if (block == 'g') {
                    logit_derivative[action] =
                        parameters.output[hidden] *
                        sech_squared *
                        preactivations[action][hidden];
                } else if (block == 'b') {
                    logit_derivative[action] =
                        parameters.output[hidden] *
                        sech_squared;
                } else {
                    logit_derivative[action] =
                        activation[action][hidden];
                }
            }
            const double derivative_mean =
                (logit_derivative[0] +
                 logit_derivative[1]) /
                2.0;
            std::array<double, 2>
                residual_derivative{};
            for (std::size_t action = 0;
                 action < 2; ++action) {
                const double centered =
                    logits[action] - mean_logit;
                residual_derivative[action] =
                    adaptive::kResidualWeight *
                    (1.0 -
                     std::tanh(centered) *
                         std::tanh(centered)) *
                    (logit_derivative[action] -
                     derivative_mean);
            }
            const double value =
                block == 'g'
                ? parameters.gain[hidden]
                : block == 'b'
                ? parameters.bias[hidden]
                : parameters.output[hidden];
            return
                loss_derivative *
                    (residual_derivative[0] -
                     residual_derivative[1]) +
                adaptive::kL2Tether * value;
        };
    for (std::size_t hidden = 0;
         hidden < 2; ++hidden) {
        expect_near(
            probe.gradient.gain[hidden],
            expected_coordinate(hidden, 'g'),
            1.0e-13,
            "adaptive gain gradient drifted");
        expect_near(
            probe.gradient.bias[hidden],
            expected_coordinate(hidden, 'b'),
            1.0e-13,
            "adaptive bias gradient drifted");
        expect_near(
            probe.gradient.output[hidden],
            expected_coordinate(hidden, 'o'),
            1.0e-13,
            "adaptive output gradient drifted");
    }

    constexpr double epsilon = 1.0e-6;
    const auto finite_difference =
        [&](char block, std::size_t hidden) {
            adaptive::Parameters plus = parameters;
            adaptive::Parameters minus = parameters;
            auto coordinate =
                [&](adaptive::Parameters& value)
                    -> double& {
                    if (block == 'g') {
                        return value.gain[hidden];
                    }
                    if (block == 'b') {
                        return value.bias[hidden];
                    }
                    return value.output[hidden];
                };
            coordinate(plus) += epsilon;
            coordinate(minus) -= epsilon;
            const double numerical =
                (adaptive::testing::objective_probe(
                     dataset, plus)
                     .objective -
                 adaptive::testing::objective_probe(
                     dataset, minus)
                     .objective) /
                (2.0 * epsilon);
            const double analytic =
                block == 'g'
                ? probe.gradient.gain[hidden]
                : block == 'b'
                ? probe.gradient.bias[hidden]
                : probe.gradient.output[hidden];
            expect_near(
                numerical, analytic, 1.0e-9,
                "adaptive gradient failed finite difference");
        };
    for (const char block : {'g', 'b', 'o'}) {
        finite_difference(block, 0);
        finite_difference(block, 1);
    }
}

void test_zero_parameters_are_exact_parent() {
    std::vector<adaptive::Hidden>
        preactivations(3);
    preactivations[0][0] = -0.5;
    preactivations[1][1] = 0.25;
    preactivations[2][2] = 0.75;
    const adaptive::Dataset dataset =
        replicated_dataset(
            {0.9, 0.5, 0.1},
            {0.4, 0.6, 0.5},
            preactivations);
    const adaptive::Parameters zero;
    const adaptive::Metrics adaptive_metrics =
        adaptive::evaluate(dataset, zero);
    const pair::Metrics parent_metrics =
        pair::evaluate(
            paired_dataset(dataset),
            pair::Delta{});
    expect(
        adaptive_metrics == parent_metrics,
        "zero adaptive parameters changed parent metrics");
    const auto forward =
        adaptive::testing::analytic_forward(
            dataset.roots.front(), zero);
    for (std::size_t action = 0;
         action < forward.logits.size(); ++action) {
        expect(
            forward.preactivations[action] ==
                    dataset.roots.front()
                        .parent_preactivations[action] &&
                forward.activations[action] ==
                    dataset.roots.front()
                        .paired.hidden[action] &&
                std::bit_cast<std::uint64_t>(
                    forward.logits[action]) ==
                    UINT64_C(0) &&
                std::bit_cast<std::uint64_t>(
                    forward.residuals[action]) ==
                    UINT64_C(0),
            "zero adaptive forward was not exact parent");
    }
}

void test_optimizer_is_bit_deterministic() {
    const adaptive::Dataset dataset =
        learning_dataset();
    const adaptive::OptimizerReport first =
        adaptive::optimize(dataset);
    const adaptive::OptimizerReport repeated =
        adaptive::optimize(dataset);
    expect(
        adaptive::optimizer_bit_identical(
            first, repeated),
        "repeated adaptive fit was not bit-identical");
    adaptive::OptimizerReport signed_zero = first;
    expect(
        std::bit_cast<std::uint64_t>(
            signed_zero.parameters.gain.back()) ==
            UINT64_C(0),
        "unused adaptive parameter was not +0");
    signed_zero.parameters.gain.back() = -0.0;
    expect(
        !adaptive::optimizer_bit_identical(
            first, signed_zero),
        "adaptive optimizer ignored signed-zero drift");
    expect(
        first.completed_steps ==
                adaptive::kAdamSteps &&
            first.final_objective <
                first.initial_objective &&
            first.after.pairs.equal_deck_pair_bce <
                first.before.pairs
                    .equal_deck_pair_bce &&
            first.after.ranking
                    .equal_deck_mean_regret <
                first.before.ranking
                    .equal_deck_mean_regret &&
            first.parameter_l2_norm > 0.0 &&
            std::isfinite(
                first.parameter_l2_norm) &&
            std::isfinite(
                first.final_gradient_l2_norm),
        "adaptive synthetic fit did not improve");
    const bool gain_moved =
        std::any_of(
            first.parameters.gain.begin(),
            first.parameters.gain.end(),
            [](double value) {
                return value != 0.0;
            });
    const bool bias_moved =
        std::any_of(
            first.parameters.bias.begin(),
            first.parameters.bias.end(),
            [](double value) {
                return value != 0.0;
            });
    const bool output_moved =
        std::any_of(
            first.parameters.output.begin(),
            first.parameters.output.end(),
            [](double value) {
                return value != 0.0;
            });
    expect(
        gain_moved && bias_moved && output_moved,
        "adaptive optimizer left a parameter block dead");
}

void test_model_isolation_and_exact_engine_agreement() {
    const auto parent = test_model();
    const adaptive::Dataset dataset =
        model_dataset(parent);
    adaptive::Parameters parameters;
    parameters.gain[0] = 0.125;
    parameters.bias[0] = -0.0625;
    parameters.output[0] = 0.25;
    parameters.gain[3] = -0.03125;
    parameters.bias[3] = 0.015625;
    parameters.output[3] = -0.125;
    const adaptive::ModelIsolationReport isolation =
        adaptive::apply_parameters(
            parent, parameters);
    expect(
        isolation.passed() &&
            isolation.safe_for_evaluation() &&
            isolation.parent_positive_zero &&
            isolation.parent_immutable &&
            isolation
                .repeated_application_bit_identical &&
            isolation
                .only_priority_adaptive_trunk_changed &&
            isolation.exact_transform &&
            isolation.changed_coordinates == 6 &&
            isolation.parent_fingerprint_before ==
                isolation.parent_fingerprint_after &&
            isolation.candidate_fingerprint ==
                isolation
                    .repeated_candidate_fingerprint,
        "adaptive model isolation drifted");

    const adaptive::Root& root =
        dataset.roots.front();
    const auto analytic =
        adaptive::testing::analytic_forward(
            root, parameters);
    const auto engine =
        adaptive::testing::model_forward(
            root, isolation.candidate);
    expect(
        analytic.preactivations.size() ==
            engine.preactivations.size(),
        "adaptive engine forward width drifted");
    for (std::size_t action = 0;
         action < analytic.logits.size(); ++action) {
        for (std::size_t hidden = 0;
             hidden < adaptive::kHiddenCount;
             ++hidden) {
            expect_near(
                engine.preactivations[action][hidden],
                analytic.preactivations[action][hidden],
                adaptive::kMaximumAgreementError,
                "engine preactivation disagreed");
            expect_near(
                engine.activations[action][hidden],
                analytic.activations[action][hidden],
                adaptive::kMaximumAgreementError,
                "engine activation disagreed");
        }
        expect_near(
            engine.logits[action],
            analytic.logits[action],
            adaptive::kMaximumAgreementError,
            "engine logit disagreed");
        expect_near(
            engine.residuals[action],
            analytic.residuals[action],
            adaptive::kMaximumAgreementError,
            "engine residual disagreed");
    }

    const adaptive::Corpus corpus =
        adaptive::testing::make_corpus(
            dataset, dataset,
            old_school::
                learned_model_component_fingerprints(
                    parent));
    const adaptive::ExactEvaluationReport exact =
        adaptive::evaluate_exact(
            corpus, parent, isolation,
            parameters);
    expect(
        exact.maximum_preactivation_difference <=
                adaptive::kMaximumAgreementError &&
            exact.maximum_activation_difference <=
                adaptive::kMaximumAgreementError &&
            exact.maximum_logit_difference <=
                adaptive::kMaximumAgreementError &&
            exact.maximum_residual_difference <=
                adaptive::kMaximumAgreementError &&
            exact.legal_action_permutation_equivariant &&
            exact
                .hidden_repartition_live_probe_eligible &&
            exact.hidden_repartition_nonvacuous &&
            exact
                .hidden_repartition_owner_observation_bit_identical &&
            exact
                .hidden_repartition_actions_bit_identical &&
            exact
                .hidden_repartition_options_bit_identical &&
            exact
                .hidden_repartition_logits_bit_identical &&
            exact
                .hidden_repartition_centered_logits_bit_identical &&
            exact
                .hidden_repartition_residuals_bit_identical &&
            exact.zero_parameters_equivalent &&
            exact.successor_predictions_bit_identical &&
            exact.successor_metrics_bit_identical,
        "adaptive exact evaluation invariant failed");
    expect(
        adaptive::model_scores_bit_identical(
            dataset, isolation.candidate,
            isolation.candidate) &&
            !adaptive::model_scores_bit_identical(
                dataset, parent,
                isolation.candidate),
        "adaptive model score identity drifted");

    const adaptive::ModelIsolationReport zero =
        adaptive::apply_parameters(
            parent, adaptive::Parameters{});
    expect(
        zero.safe_for_evaluation() &&
            !zero.passed() &&
            zero.changed_coordinates == 0 &&
            zero.candidate_fingerprint ==
                zero.parent_fingerprint_before,
        "zero transform did not preserve exact parent");
}

void test_permutation_equivariance() {
    const auto parent = test_model();
    const auto model_parameters =
        old_school::learned_priority_head_parameters(
            parent);
    adaptive::Root root =
        model_root(
            "permutation",
            old_school::DeckId::Blue,
            0, 0, model_parameters,
            {0.9, 0.5, 0.1},
            {0.4, 0.6, 0.5});
    root.paired.options[2][0] = 0.5;
    root.parent_preactivations[2] =
        model_preactivation(
            root.paired.options[2],
            model_parameters);
    for (std::size_t hidden = 0;
         hidden < adaptive::kHiddenCount; ++hidden) {
        root.paired.hidden[2][hidden] =
            std::tanh(
                root.parent_preactivations[2][hidden]);
    }
    adaptive::Parameters parameters;
    parameters.gain[0] = 0.125;
    parameters.bias[0] = -0.03125;
    parameters.output[0] = 0.25;
    const auto isolation =
        adaptive::apply_parameters(
            parent, parameters);
    expect(
        isolation.passed() &&
            adaptive::testing::
                legal_action_permutation_equivariant(
                    root, parameters,
                    isolation.candidate),
        "legal-action permutation contract failed");

    const std::vector<std::size_t>
        permutation{2, 0, 1};
    const adaptive::Root permuted =
        adaptive::testing::permute_actions(
            root, permutation);
    const auto original_forward =
        adaptive::testing::analytic_forward(
            root, parameters);
    const auto permuted_forward =
        adaptive::testing::analytic_forward(
            permuted, parameters);
    for (std::size_t action = 0;
         action < permutation.size(); ++action) {
        expect(
            permuted.paired.ranking.actions[action] ==
                    root.paired.ranking.actions[
                        permutation[action]] &&
                permuted.paired.options[action] ==
                    root.paired.options[
                        permutation[action]] &&
                permuted.parent_preactivations[action] ==
                    root.parent_preactivations[
                        permutation[action]],
            "action permutation lost aligned payload");
        expect_near(
            permuted_forward.logits[action],
            original_forward.logits[
                permutation[action]],
            1.0e-15,
            "permuted analytic logit drifted");
        expect_near(
            permuted_forward.residuals[action],
            original_forward.residuals[
                permutation[action]],
            1.0e-15,
            "permuted analytic residual drifted");
    }

    const adaptive::Dataset original_dataset =
        replicated_dataset(
            {0.9, 0.5, 0.1},
            {0.4, 0.6, 0.5},
            root.parent_preactivations);
    std::vector<adaptive::Root> permuted_roots;
    for (const adaptive::Root& source :
         original_dataset.roots) {
        permuted_roots.push_back(
            adaptive::testing::permute_actions(
                source, permutation));
    }
    const adaptive::Dataset permuted_dataset =
        adaptive::testing::make_dataset(
            std::move(permuted_roots));
    expect(
        adaptive::evaluate(
            original_dataset, parameters) ==
            adaptive::evaluate(
                permuted_dataset, parameters),
        "dataset metrics depended on legal-action order");
    const auto original_objective =
        adaptive::testing::objective_probe(
            original_dataset, parameters);
    const auto permuted_objective =
        adaptive::testing::objective_probe(
            permuted_dataset, parameters);
    expect_near(
        permuted_objective.objective,
        original_objective.objective,
        1.0e-12,
        "permuted objective drifted");
    for (std::size_t hidden = 0;
         hidden < adaptive::kHiddenCount; ++hidden) {
        expect_near(
            permuted_objective.gradient.gain[hidden],
            original_objective.gradient.gain[hidden],
            1.0e-12,
            "permuted gain gradient drifted");
        expect_near(
            permuted_objective.gradient.bias[hidden],
            original_objective.gradient.bias[hidden],
            1.0e-12,
            "permuted bias gradient drifted");
        expect_near(
            permuted_objective.gradient.output[hidden],
            original_objective.gradient.output[hidden],
            1.0e-12,
            "permuted output gradient drifted");
    }

    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::testing::permute_actions(
                    root, {0, 0, 2}));
        },
        "duplicate action permutation was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::testing::permute_actions(
                    root, {0, 1}));
        },
        "short action permutation was accepted");
}

void test_owner_safe_forward_contract() {
    std::vector<adaptive::Hidden>
        preactivations(2);
    preactivations[0][0] = 0.5;
    preactivations[1][0] = -0.25;
    adaptive::Root first =
        make_root(
            "owner-safe",
            old_school::DeckId::Green,
            0, 0, {0.8, 0.2}, {0.4, 0.6},
            preactivations);
    adaptive::Root label_only = first;
    label_only.paired.ranking.actions[0]
        .worlds[0].teacher_target = 0.1;
    label_only.paired.ranking.actions[1]
        .worlds[0].teacher_target = 0.9;
    label_only.paired.ranking.actions[0]
        .worlds[0].parent_leaf_values =
        {0.2, 0.2};
    label_only.paired.ranking.actions[1]
        .worlds[0].parent_leaf_values =
        {0.8, 0.8};
    std::fill(
        label_only.paired.ranking.actions[0]
            .worlds[0].observation.begin(),
        label_only.paired.ranking.actions[0]
            .worlds[0].observation.end(),
        0.75);
    adaptive::Parameters parameters;
    parameters.gain[0] = 0.25;
    parameters.bias[0] = -0.125;
    parameters.output[0] = 0.5;
    expect(
        adaptive::testing::analytic_forward(
            first, parameters) ==
            adaptive::testing::analytic_forward(
                label_only, parameters),
        "teacher or successor label leaked into policy forward");

    const auto parent = test_model();
    const auto parent_parameters =
        old_school::learned_priority_head_parameters(
            parent);
    adaptive::Root model_backed =
        model_root(
            "hidden-safe",
            old_school::DeckId::Blue,
            1, 1, parent_parameters);
    adaptive::Root public_clone = model_backed;
    for (auto& action :
         public_clone.paired.ranking.actions) {
        for (auto& cell : action.worlds) {
            std::reverse(
                cell.observation.begin(),
                cell.observation.end());
        }
    }
    const auto isolation =
        adaptive::apply_parameters(
            parent, parameters);
    expect(
        adaptive::testing::model_forward(
            model_backed,
            isolation.candidate) ==
            adaptive::testing::model_forward(
                public_clone,
                isolation.candidate),
        "non-policy successor state entered model forward");
    expect(
        model_backed.paired.options ==
                public_clone.paired.options &&
            model_backed.parent_preactivations ==
                public_clone.parent_preactivations,
        "owner-safe clone changed policy inputs");
}

void test_live_hidden_repartition_is_mutation_sensitive() {
    const field::AncestralFieldRoot original =
        field::make_ancestral_field_root();
    const field::AncestralFieldRoot repartitioned =
        field::hidden_repartition_clone(original);
    const bool live_probe_eligible =
        field::has_required_action_identities(
            original) &&
        field::has_required_action_identities(
            repartitioned) &&
        original.legal_actions.size() >= 3;
    const std::size_t opponent =
        1U - original.actor;
    const auto& original_opponent =
        original.state.players[opponent];
    const auto& repartitioned_opponent =
        repartitioned.state.players[opponent];
    const bool opponent_hidden_zones_changed =
        original_opponent.hand.size() ==
            repartitioned_opponent.hand.size() &&
        original_opponent.library.size() ==
            repartitioned_opponent.library.size() &&
        (original_opponent.hand !=
             repartitioned_opponent.hand ||
         original_opponent.library !=
             repartitioned_opponent.library);
    const bool nonvacuous =
        original.state != repartitioned.state &&
        opponent_hidden_zones_changed &&
        original.actor == repartitioned.actor &&
        old_school::observe_game_state(
            original.state, original.actor) ==
            old_school::observe_game_state(
                repartitioned.state,
                repartitioned.actor);
    expect(
        live_probe_eligible && nonvacuous &&
            original.legal_actions ==
                repartitioned.legal_actions,
        "live hidden-repartition fixture is ineligible or vacuous");

    const auto original_rows =
        priority_option_rows(original);
    const auto repartitioned_rows =
        priority_option_rows(repartitioned);
    const auto parent = test_model();
    const auto parent_parameters =
        old_school::learned_priority_head_parameters(
            parent);
    const std::size_t hidden =
        most_action_sensitive_hidden_unit(
            original_rows, parent_parameters);
    adaptive::Parameters parameters;
    parameters.gain[hidden] = 0.125;
    parameters.bias[hidden] = -0.0625;
    parameters.output[hidden] = 0.25;
    const adaptive::ModelIsolationReport isolation =
        adaptive::apply_parameters(
            parent, parameters);
    expect(
        isolation.passed(),
        "hidden-repartition candidate failed isolation");

    const std::vector<double> original_logits =
        old_school::learned_policy_head_logits(
            original_rows,
            old_school::LearnedPolicyDecisionKind::
                Priority,
            isolation.candidate);
    const std::vector<double> repartitioned_logits =
        old_school::learned_policy_head_logits(
            repartitioned_rows,
            old_school::LearnedPolicyDecisionKind::
                Priority,
            isolation.candidate);
    const adaptive::testing::HiddenRepartitionView
        original_view{
            .owner_observation =
                old_school::learned_observation(
                    original.state,
                    original.actor),
            .actions = original.legal_actions,
            .option_rows = original_rows,
            .logits = original_logits,
            .centered_logits =
                center_logits(original_logits),
            .centered_residuals =
                centered_residuals(original_logits),
        };
    const adaptive::testing::HiddenRepartitionView
        repartitioned_view{
            .owner_observation =
                old_school::learned_observation(
                    repartitioned.state,
                    repartitioned.actor),
            .actions =
                repartitioned.legal_actions,
            .option_rows = repartitioned_rows,
            .logits = repartitioned_logits,
            .centered_logits =
                center_logits(
                    repartitioned_logits),
            .centered_residuals =
                centered_residuals(
                    repartitioned_logits),
        };
    const auto witness =
        adaptive::testing::
            compare_hidden_repartition_views(
                live_probe_eligible, nonvacuous,
                original_view,
                repartitioned_view);
    expect(
        witness.passed() &&
            witness.live_probe_eligible &&
            witness.nonvacuous &&
            witness.owner_observation_bit_identical &&
            witness.actions_bit_identical &&
            witness.options_bit_identical &&
            witness.logits_bit_identical &&
            witness.centered_logits_bit_identical &&
            witness.residuals_bit_identical,
        "adaptive live hidden-repartition witness failed");
    expect(
        std::any_of(
            original_view.centered_residuals.begin(),
            original_view.centered_residuals.end(),
            [](double value) {
                return value != 0.0;
            }),
        "hidden-repartition score witness was vacuous");

    auto changed_row = repartitioned_view;
    double& admitted_feature =
        changed_row.option_rows.at(
            repartitioned.self_target_index)
            .at(0);
    admitted_feature =
        std::nextafter(
            admitted_feature,
            std::numeric_limits<double>::infinity());
    const auto row_mutation =
        adaptive::testing::
            compare_hidden_repartition_views(
                live_probe_eligible, nonvacuous,
                original_view, changed_row);
    expect(
        !row_mutation.passed() &&
            !row_mutation.options_bit_identical &&
            row_mutation
                .owner_observation_bit_identical &&
            row_mutation.actions_bit_identical &&
            row_mutation.logits_bit_identical &&
            row_mutation
                .centered_logits_bit_identical &&
            row_mutation.residuals_bit_identical,
        "admitted option-row mutation escaped witness");

    auto changed_residual = repartitioned_view;
    double& admitted_residual =
        changed_residual.centered_residuals.at(
            repartitioned.self_target_index);
    admitted_residual =
        std::nextafter(
            admitted_residual,
            std::numeric_limits<double>::infinity());
    const auto residual_mutation =
        adaptive::testing::
            compare_hidden_repartition_views(
                live_probe_eligible, nonvacuous,
                original_view, changed_residual);
    expect(
        !residual_mutation.passed() &&
            residual_mutation.options_bit_identical &&
            residual_mutation.logits_bit_identical &&
            residual_mutation
                .centered_logits_bit_identical &&
            !residual_mutation.residuals_bit_identical,
        "candidate residual mutation escaped witness");

    expect(
        !adaptive::testing::
             compare_hidden_repartition_views(
                 false, nonvacuous, original_view,
                 repartitioned_view)
             .passed() &&
            !adaptive::testing::
                 compare_hidden_repartition_views(
                     live_probe_eligible, false,
                     original_view,
                     repartitioned_view)
                 .passed(),
        "vacuous or ineligible hidden witness passed");
}

void test_grouped_folds_and_gates() {
    const auto parent = test_model();
    const adaptive::Dataset dataset =
        fold_dataset(parent);
    for (std::size_t fold = 0;
         fold < adaptive::kFoldCount; ++fold) {
        const adaptive::Dataset fitting =
            adaptive::fold_training_dataset(
                dataset, fold);
        const adaptive::Dataset held =
            adaptive::fold_holdout_dataset(
                dataset, fold);
        expect(
            fitting.roots.size() == 60 &&
                held.roots.size() == 20,
            "adaptive fold root balance drifted");
        std::set<std::size_t> fitting_games;
        std::map<
            std::size_t,
            std::array<std::size_t, 2>>
            held_games;
        for (const adaptive::Root& root :
             fitting.roots) {
            fitting_games.insert(
                root.paired.schedule_index);
        }
        for (const adaptive::Root& root :
             held.roots) {
            expect(
                !fitting_games.contains(
                    root.paired.schedule_index),
                "adaptive source game crossed folds");
            ++held_games[
                root.paired.schedule_index]
                [root.paired.actor];
        }
        expect(
            held_games.size() == 10,
            "adaptive held-out game count drifted");
        for (const auto& [schedule, actors] :
             held_games) {
            static_cast<void>(schedule);
            expect(
                actors[0] == 1 &&
                    actors[1] == 1,
                "adaptive actor pair crossed folds");
        }
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            expect(
                fitting.roots_by_deck[deck] == 12 &&
                    held.roots_by_deck[deck] == 4,
                "adaptive fold deck balance drifted");
        }
    }
    const adaptive::FoldReport report =
        adaptive::evaluate_grouped_oof(
            dataset, parent);
    expect(
        report.exact_balance &&
            report.repeated_fits_bit_identical &&
            report.repeated_scores_bit_identical &&
            report.model_isolation_passed &&
            report.maximum_preactivation_difference <=
                adaptive::kMaximumAgreementError &&
            report.maximum_activation_difference <=
                adaptive::kMaximumAgreementError &&
            report.maximum_logit_difference <=
                adaptive::kMaximumAgreementError &&
            report.maximum_residual_difference <=
                adaptive::kMaximumAgreementError,
        "adaptive grouped OOF invariant failed");
    expect(
        report.parent ==
            adaptive::evaluate(
                dataset, adaptive::Parameters{}),
        "adaptive OOF parent order drifted");

    adaptive::OfflineGateInputs passing{
        .cache_identity_exact = true,
        .pair_census_exact = true,
        .optimizer_recipe_exact = true,
        .grouped_folds_exact = true,
        .repeat_fits_bit_identical = true,
        .parameter_replay_bit_identical = true,
        .zero_delta_equivalent = true,
        .actual_model_agreement = true,
        .hidden_repartition_live_probe_eligible =
            true,
        .hidden_repartition_nonvacuous = true,
        .hidden_repartition_owner_observation_bit_identical =
            true,
        .hidden_repartition_actions_bit_identical =
            true,
        .hidden_repartition_options_bit_identical =
            true,
        .hidden_repartition_logits_bit_identical =
            true,
        .hidden_repartition_centered_logits_bit_identical =
            true,
        .hidden_repartition_residuals_bit_identical =
            true,
        .parent_immutable = true,
        .model_isolation_passed = true,
        .successor_predictions_bit_identical = true,
        .successor_metrics_bit_identical = true,
    };
    const auto passing_metrics =
        [](adaptive::Metrics& before,
           adaptive::Metrics& after) {
            before.pairs.equal_deck_pair_bce =
                0.7;
            after.pairs.equal_deck_pair_bce =
                0.6;
            before.ranking
                .equal_deck_mean_regret = 0.2;
            after.ranking
                .equal_deck_mean_regret = 0.1;
            before.ranking
                .equal_deck_listwise_cross_entropy =
                0.9;
            after.ranking
                .equal_deck_listwise_cross_entropy =
                0.9;
            before.ranking
                .equal_deck_top_one_expected_agreement =
                0.5;
            after.ranking
                .equal_deck_top_one_expected_agreement =
                0.5;
            before.ranking
                .equal_deck_stable_pair_agreement =
                0.5;
            after.ranking
                .equal_deck_stable_pair_agreement =
                0.5;
            for (std::size_t deck = 0;
                 deck < old_school::kDeckCount;
                 ++deck) {
                before.ranking.decks[deck]
                    .mean_regret = 0.2;
                after.ranking.decks[deck]
                    .mean_regret = 0.2;
            }
        };
    passing_metrics(
        passing.parent_train,
        passing.candidate_train);
    passing_metrics(
        passing.parent_oof,
        passing.candidate_oof);
    passing_metrics(
        passing.parent_dev,
        passing.candidate_dev);
    expect(
        adaptive::evaluate_offline_gate(
            passing).passed(),
        "passing adaptive offline gate was rejected");

    const auto require_reject =
        [](adaptive::OfflineGateInputs changed,
           std::string_view message) {
            expect(
                !adaptive::evaluate_offline_gate(
                     changed)
                     .passed(),
                message);
        };
    auto changed = passing;
    changed.candidate_train.pairs
        .equal_deck_pair_bce =
        changed.parent_train.pairs
            .equal_deck_pair_bce;
    require_reject(
        changed,
        "equal TRAIN pair BCE passed adaptive gate");
    changed = passing;
    changed.candidate_oof.ranking
        .equal_deck_mean_regret =
        changed.parent_oof.ranking
            .equal_deck_mean_regret;
    require_reject(
        changed,
        "equal OOF regret passed adaptive gate");
    changed = passing;
    changed.candidate_dev.ranking
        .equal_deck_stable_pair_agreement =
        std::nextafter(
            changed.parent_dev.ranking
                .equal_deck_stable_pair_agreement,
            -std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "DEV stable-pair regression passed adaptive gate");
    changed = passing;
    changed.candidate_oof.ranking.decks[2]
        .mean_regret =
        std::nextafter(
            changed.parent_oof.ranking.decks[2]
                .mean_regret,
            std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "OOF deck regret regression passed adaptive gate");
    changed = passing;
    changed.successor_metrics_bit_identical = false;
    require_reject(
        changed,
        "changed successor metrics passed adaptive gate");
    changed = passing;
    changed.actual_model_agreement = false;
    require_reject(
        changed,
        "analytic/engine drift passed adaptive gate");
    changed = passing;
    changed.hidden_repartition_nonvacuous = false;
    require_reject(
        changed,
        "vacuous hidden repartition passed adaptive gate");
    changed = passing;
    changed
        .hidden_repartition_residuals_bit_identical =
        false;
    require_reject(
        changed,
        "changed hidden residual passed adaptive gate");
}

void test_selector_contract_and_validation() {
    const auto parent = test_model();
    adaptive::Parameters parameters;
    parameters.gain[0] = 0.125;
    parameters.bias[0] = 0.0625;
    parameters.output[0] = 0.25;
    const auto isolation =
        adaptive::apply_parameters(
            parent, parameters);
    expect(
        isolation.passed(),
        "adaptive selector candidate failed isolation");
    const old_school::BotConfig challenger =
        g1::selector_bot_config(
            isolation.candidate,
            adaptive::kResidualWeight);
    const old_school::BotConfig baseline =
        g1::selector_bot_config(parent, 0.0);
    expect(
        adaptive::selector_config_exact(
            challenger, baseline, parent,
            isolation.candidate),
        "exact adaptive selector config was rejected");
    auto changed_challenger = challenger;
    changed_challenger.rollouts_per_action = 7;
    expect(
        !adaptive::selector_config_exact(
            changed_challenger, baseline,
            parent, isolation.candidate),
        "changed adaptive selector config passed");

    adaptive::Dataset malformed =
        learning_dataset();
    malformed.roots.front()
        .parent_preactivations.pop_back();
    expect_rejected(
        [&] {
            adaptive::validate_dataset(
                malformed);
        },
        "short preactivation list was accepted");
    malformed = learning_dataset();
    malformed.roots.front()
        .parent_preactivations.front()[0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            adaptive::validate_dataset(
                malformed);
        },
        "nonfinite preactivation was accepted");
    malformed = learning_dataset();
    malformed.roots.front()
        .paired.hidden.front()[0] += 0.01;
    expect_rejected(
        [&] {
            adaptive::validate_dataset(
                malformed);
        },
        "preactivation/activation mismatch was accepted");
    malformed = learning_dataset();
    malformed.roots.front()
        .paired.options.front().pop_back();
    expect_rejected(
        [&] {
            adaptive::validate_dataset(
                malformed);
        },
        "short owner-safe option was accepted");

    adaptive::Parameters nonfinite;
    nonfinite.output[0] =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::evaluate(
                    learning_dataset(),
                    nonfinite));
        },
        "nonfinite adaptive parameters were accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::testing::objective_probe(
                    learning_dataset(),
                    nonfinite));
        },
        "nonfinite adaptive objective was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::apply_parameters(
                    parent, nonfinite));
        },
        "nonfinite adaptive model transform was accepted");

    adaptive::Parameters negative_zero;
    negative_zero.bias[0] = -0.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::apply_parameters(
                    parent, negative_zero));
        },
        "negative-zero adaptive parameter was accepted");
    expect_rejected(
        [&] {
            adaptive::OptimizerConfig changed;
            changed.steps = 255;
            static_cast<void>(
                adaptive::optimize(
                    learning_dataset(), changed));
        },
        "changed adaptive optimizer recipe was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::fold_holdout_dataset(
                    learning_dataset(),
                    adaptive::kFoldCount));
        },
        "invalid adaptive fold was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                adaptive::apply_parameters(
                    nullptr,
                    adaptive::Parameters{}));
        },
        "null adaptive parent was accepted");

    adaptive::Corpus wrong =
        adaptive::testing::make_corpus(
            learning_dataset(),
            learning_dataset(),
            old_school::
                learned_model_component_fingerprints(
                    parent));
    wrong.source_digest =
        std::string(64, '0');
    expect_rejected(
        [&] {
            adaptive::validate_corpus(
                wrong, parent);
        },
        "wrong adaptive corpus identity was accepted");
}

} // namespace

int main() {
    std::size_t passed = 0;
    const auto run =
        [&](std::string_view name, auto&& test) {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        };

    run(
        "fixed recipe and owner-safe shape",
        test_fixed_recipe_and_owner_safe_shape);
    run(
        "forward and gradient math",
        test_forward_and_gradient_math);
    run(
        "zero parameters are exact parent",
        test_zero_parameters_are_exact_parent);
    run(
        "optimizer is bit deterministic",
        test_optimizer_is_bit_deterministic);
    run(
        "model isolation and exact engine agreement",
        test_model_isolation_and_exact_engine_agreement);
    run(
        "permutation equivariance",
        test_permutation_equivariance);
    run(
        "owner-safe forward contract",
        test_owner_safe_forward_contract);
    run(
        "live hidden repartition is mutation sensitive",
        test_live_hidden_repartition_is_mutation_sensitive);
    run(
        "grouped folds and gates",
        test_grouped_folds_and_gates);
    run(
        "selector contract and validation",
        test_selector_contract_and_validation);

    std::cout << passed
              << " decision-boundary adaptive-trunk tests passed\n";
}
