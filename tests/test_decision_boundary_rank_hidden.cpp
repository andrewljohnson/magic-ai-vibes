#include "old_school/decision_boundary_rank_hidden.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace hidden =
    old_school::decision_boundary_rank_hidden;
namespace direct =
    old_school::decision_boundary_rank_direct;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
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

static_assert(!HasStateMember<hidden::RankHiddenCell>);
static_assert(
    !HasOpponentHandMember<hidden::RankHiddenCell>);
static_assert(hidden::kFeatureCount == 674);
static_assert(hidden::kLeafCount == 2);
static_assert(hidden::kHiddenCount == 16);
static_assert(
    hidden::kTrainableCoordinateCount == 32);

hidden::RankHiddenCell make_cell(
    std::size_t feature, double sign,
    double teacher) {
    hidden::RankHiddenCell cell{
        .source = direct::RankCell{
            .observation =
                std::vector<double>(
                    hidden::kFeatureCount, 0.0),
            .parent_leaf_values = {0.45, 0.55},
            .teacher_target = teacher,
        },
    };
    cell.source.observation[feature] = sign;
    cell.hidden[0][feature] = sign;
    cell.hidden[1][feature + 8] =
        0.75 * sign;
    return cell;
}

hidden::Dataset learning_dataset() {
    std::vector<hidden::RankHiddenRoot> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < 2; ++copy) {
            roots.push_back(
                hidden::RankHiddenRoot{
                    .stable_root_id =
                        "hidden-root-" +
                        std::to_string(deck) +
                        "-" + std::to_string(copy),
                    .deck =
                        static_cast<
                            old_school::DeckId>(deck),
                    .actions = {
                        hidden::RankHiddenAction{
                            .worlds = {
                                make_cell(
                                    deck, 1.0, 0.9),
                                make_cell(
                                    deck, 1.0, 0.9),
                            },
                        },
                        hidden::RankHiddenAction{
                            .worlds = {
                                make_cell(
                                    deck, -1.0, 0.1),
                                make_cell(
                                    deck, -1.0, 0.1),
                            },
                        },
                    },
                });
        }
    }
    return hidden::testing::make_dataset(
        std::move(roots));
}

std::shared_ptr<const old_school::LearnedModel>
test_model() {
    static const auto model =
        old_school::train_learned_model(
            1, 0xDBC3D1CEULL);
    return model;
}

direct::Dataset model_backed_dataset(
    std::shared_ptr<const old_school::LearnedModel>
        model) {
    std::vector<direct::RankRoot> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < 2; ++copy) {
            direct::RankRoot root{
                .stable_root_id =
                    "engine-root-" +
                    std::to_string(deck) +
                    "-" + std::to_string(copy),
                .deck =
                    static_cast<
                        old_school::DeckId>(deck),
                .actions =
                    std::vector<direct::RankAction>(2),
            };
            for (std::size_t action = 0;
                 action < root.actions.size();
                 ++action) {
                for (std::size_t world = 0;
                     world < 2; ++world) {
                    direct::RankCell cell{
                        .observation =
                            std::vector<double>(
                                hidden::kFeatureCount,
                                0.0),
                        .teacher_target =
                            action == 0 ? 0.8 : 0.2,
                    };
                    cell.observation[deck] =
                        action == 0 ? 1.0 : -1.0;
                    cell.observation[
                        20 + copy + world] =
                        0.125;
                    cell.parent_leaf_values =
                        old_school::
                            learned_critic_observation_leaf_values(
                                cell.observation,
                                model);
                    root.actions[action].worlds.push_back(
                        std::move(cell));
                }
            }
            roots.push_back(std::move(root));
        }
    }
    return direct::testing::make_dataset(
        std::move(roots));
}

void test_fixed_recipe_and_owner_safe_shape() {
    const hidden::OptimizerConfig recipe;
    expect(
        recipe.fit_tag == 202607291501ULL &&
            hidden::kSelectorSeed ==
                202607291511ULL &&
            recipe.steps == 256 &&
            recipe.learning_rate == 0.001 &&
            recipe.beta_one == 0.9 &&
            recipe.beta_two == 0.999 &&
            recipe.epsilon == 1.0e-8 &&
            recipe.global_gradient_norm_clip ==
                5.0 &&
            recipe.temperature == 0.10 &&
            recipe.mix == 0.90 &&
            recipe.l2_tether == 0.10,
        "fixed DBC3 optimizer recipe drifted");
    const hidden::Dataset dataset =
        learning_dataset();
    expect(
        dataset.roots.size() == 10,
        "balanced synthetic root count drifted");
    for (const std::size_t roots :
         dataset.roots_by_deck) {
        expect(
            roots == 2,
            "synthetic deck balance drifted");
    }
}

void test_terminal_cells_are_fixed() {
    hidden::RankHiddenCell terminal{
        .source = direct::RankCell{
            .parent_leaf_values = {0.8, 0.8},
            .teacher_target = 0.8,
            .terminal_before_boundary = true,
        },
    };
    hidden::Delta delta{};
    for (auto& leaf : delta) {
        leaf.fill(1000.0);
    }
    expect(
        hidden::candidate_cell_value(
            terminal, delta) == 0.8,
        "terminal utility was changed by hidden delta");
}

void test_independent_leaf_shift_matches_definition() {
    hidden::RankHiddenCell cell =
        make_cell(0, 1.0, 0.5);
    cell.source.parent_leaf_values =
        {0.25, 0.75};
    cell.hidden[0][0] = 0.5;
    cell.hidden[1][8] = -0.25;
    hidden::Delta delta{};
    delta[0][0] = 0.5;
    delta[1][8] = -1.0;
    const auto sigmoid = [](double value) {
        return 1.0 /
               (1.0 + std::exp(-value));
    };
    const auto logit = [](double value) {
        return std::log(value) -
               std::log1p(-value);
    };
    const double expected =
        0.5 *
        (sigmoid(logit(0.25) + 0.25) +
         sigmoid(logit(0.75) + 0.25));
    expect(
        hidden::candidate_cell_value(
            cell, delta) == expected,
        "independent hidden leaf shifts drifted");
    expect(
        hidden::candidate_cell_value(
            cell, hidden::Delta{}) ==
            (cell.source.parent_leaf_values[0] +
             cell.source.parent_leaf_values[1]) /
                2.0,
        "zero hidden delta did not preserve parent bits");
}

void test_gradient_matches_both_leaf_blocks() {
    const hidden::Dataset dataset =
        learning_dataset();
    hidden::Delta delta{};
    delta[0][0] = 0.03125;
    delta[1][8] = -0.046875;
    const auto analytic =
        hidden::testing::objective_probe(
            dataset, delta);
    constexpr double epsilon = 1.0e-6;
    const auto check =
        [&](std::size_t leaf,
            std::size_t coordinate) {
            hidden::Delta plus = delta;
            hidden::Delta minus = delta;
            plus[leaf][coordinate] += epsilon;
            minus[leaf][coordinate] -= epsilon;
            const double numerical =
                (hidden::testing::objective_probe(
                     dataset, plus)
                     .objective -
                 hidden::testing::objective_probe(
                     dataset, minus)
                     .objective) /
                (2.0 * epsilon);
            expect(
                std::abs(
                    numerical -
                    analytic.gradient[leaf]
                                     [coordinate]) <
                    1.0e-8,
                "hidden gradient failed finite difference");
        };
    check(0, 0);
    check(1, 8);
}

void test_adam_is_deterministic_and_improves_rank() {
    const hidden::Dataset dataset =
        learning_dataset();
    const hidden::OptimizerReport first =
        hidden::optimize(dataset);
    const hidden::OptimizerReport repeated =
        hidden::optimize(dataset);
    expect(
        first == repeated,
        "repeated hidden full-batch Adam fit drifted");
    expect(
        first.completed_steps ==
                hidden::kAdamSteps &&
            first.final_objective <
                first.initial_objective &&
            first.after
                    .equal_deck_listwise_cross_entropy <
                first.before
                    .equal_deck_listwise_cross_entropy &&
            first.after.equal_deck_mean_regret <
                first.before.equal_deck_mean_regret &&
            first.after
                    .equal_deck_top_one_expected_agreement >
                first.before
                    .equal_deck_top_one_expected_agreement &&
            first.delta_l2_norm > 0.0 &&
            std::isfinite(first.delta_l2_norm) &&
            std::isfinite(
                first.final_gradient_l2_norm) &&
            std::isfinite(
                first
                    .maximum_preclip_gradient_l2_norm),
        "synthetic hidden listwise fit did not improve");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            first.delta[0][deck] > 0.0 &&
                first.delta[1][deck + 8] > 0.0 &&
                first.after.decks[deck].mean_regret <
                    first.before.decks[deck].mean_regret,
            "hidden listwise fit missed a synthetic deck");
    }
}

void test_global_gradient_clip_is_active() {
    hidden::Dataset dataset =
        learning_dataset();
    for (hidden::RankHiddenRoot& root :
         dataset.roots) {
        for (std::size_t action = 0;
             action < root.actions.size(); ++action) {
            const double activation =
                action == 0 ? 1.0 : -1.0;
            for (hidden::RankHiddenCell& cell :
                 root.actions[action].worlds) {
                for (auto& leaf : cell.hidden) {
                    leaf.fill(activation);
                }
            }
        }
    }
    hidden::validate_dataset(dataset);
    const hidden::OptimizerReport fit =
        hidden::optimize(dataset);
    expect(
        fit.maximum_preclip_gradient_l2_norm >
                hidden::kGlobalGradientNormClip &&
            fit.clipped_steps > 0 &&
            std::isfinite(fit.final_objective) &&
            std::isfinite(fit.delta_l2_norm),
        "global hidden pre-Adam gradient clip was not active");
}

void test_engine_seam_and_model_isolation() {
    const auto parent = test_model();
    const std::vector<double> observation(
        hidden::kFeatureCount, 0.0);
    const auto parent_hidden =
        old_school::
            learned_critic_observation_hidden_activations(
                observation, parent);
    for (const auto& leaf : parent_hidden) {
        expect(
            std::all_of(
                leaf.begin(), leaf.end(),
                [](double value) {
                    return std::isfinite(value) &&
                           value >= -1.0 &&
                           value <= 1.0;
                }),
            "engine exposed an invalid tanh activation");
    }
    const auto zero =
        old_school::
            with_learned_critic_hidden_output_delta(
                parent, hidden::Delta{});
    expect(
        zero.get() == parent.get() &&
            old_school::learned_model_fingerprint(zero) ==
                old_school::learned_model_fingerprint(
                    parent),
        "zero hidden delta did not return exact C16");

    hidden::Delta delta{};
    delta[0][0] = 0.125;
    delta[1][3] = -0.0625;
    const hidden::ModelIsolationReport isolation =
        hidden::apply_delta(parent, delta);
    expect(
        isolation.passed() &&
            isolation.independent_delta_exact &&
            isolation.output_bias_frozen &&
            isolation.direct_path_frozen &&
            isolation.context_direct_path_frozen &&
            isolation.changed_coordinates == 2 &&
            isolation.parent_fingerprint_before ==
                isolation.parent_fingerprint_after &&
            isolation.candidate_fingerprint ==
                isolation
                    .repeated_candidate_fingerprint,
        "hidden-output application failed isolation");
    expect(
        old_school::
                learned_critic_observation_hidden_activations(
                    observation, isolation.candidate) ==
            parent_hidden,
        "candidate changed frozen hidden activations");
}

void test_projection_exact_scoring_and_surrogate() {
    const auto parent = test_model();
    const direct::Dataset source =
        model_backed_dataset(parent);
    const direct::Corpus direct_corpus =
        direct::testing::make_corpus(
            source, source,
            old_school::
                learned_model_component_fingerprints(
                    parent));
    const hidden::Corpus corpus =
        hidden::project_corpus(
            direct_corpus, parent);
    hidden::validate_corpus(corpus, parent);
    expect(
        hidden::evaluate_model(
            corpus.train, parent) ==
            direct::evaluate_model(source, parent),
        "DBC3 did not reuse exact DBC2 model metrics");

    hidden::Delta delta{};
    delta[0][1] = 0.125;
    delta[1][2] = -0.0625;
    const auto isolation =
        hidden::apply_delta(parent, delta);
    const hidden::ExactEvaluationReport exact =
        hidden::evaluate_exact(
            corpus, parent, isolation.candidate,
            delta);
    expect(
        exact.parent_train ==
                direct::evaluate_model(
                    source, parent) &&
            exact.candidate_train ==
                hidden::evaluate_model(
                    corpus.train,
                    isolation.candidate) &&
            exact
                    .maximum_surrogate_engine_cell_difference <=
                1.0e-12,
        "actual model and hidden surrogate diverged");

    hidden::Corpus tampered = corpus;
    tampered.train.roots.front()
        .actions.front().worlds.front()
        .hidden[0][0] += 0.0001;
    expect_rejected(
        [&] {
            hidden::validate_corpus(
                tampered, parent);
        },
        "tampered hidden projection replayed");
}

direct::OfflineGateInputs passing_gate_inputs() {
    direct::OfflineGateInputs inputs{
        .repeated_optimizer_bit_identical = true,
        .optimizer_recipe_exact = true,
        .objective_strictly_improved = true,
        .surrogate_engine_agreement = true,
        .exact_model_identity = true,
        .model_isolation_passed = true,
    };
    inputs.parent_train
        .equal_deck_listwise_cross_entropy = 2.0;
    inputs.candidate_train
        .equal_deck_listwise_cross_entropy = 1.0;
    inputs.parent_train
        .equal_deck_mean_regret = 0.2;
    inputs.candidate_train
        .equal_deck_mean_regret = 0.1;
    inputs.parent_dev
        .equal_deck_listwise_cross_entropy = 2.0;
    inputs.candidate_dev
        .equal_deck_listwise_cross_entropy = 1.0;
    inputs.parent_dev.equal_deck_mean_regret = 0.2;
    inputs.candidate_dev.equal_deck_mean_regret = 0.1;
    inputs.parent_dev
        .equal_deck_top_one_expected_agreement = 0.5;
    inputs.candidate_dev
        .equal_deck_top_one_expected_agreement = 0.6;
    inputs.parent_dev
        .equal_deck_stable_pair_agreement = 0.5;
    inputs.candidate_dev
        .equal_deck_stable_pair_agreement = 0.6;
    inputs.parent_dev.equal_deck_successor_bce = 0.4;
    inputs.candidate_dev.equal_deck_successor_bce =
        0.4;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        inputs.parent_dev.decks[deck].mean_regret =
            0.2;
        inputs.candidate_dev.decks[deck].mean_regret =
            0.1;
    }
    return inputs;
}

void test_shared_offline_gate_fails_closed() {
    const auto require_reject =
        [](direct::OfflineGateInputs inputs) {
            expect(
                !direct::evaluate_offline_gate(inputs)
                     .passed(),
                "mutated offline conjunct passed");
        };
    const direct::OfflineGateInputs passing =
        passing_gate_inputs();
    expect(
        direct::evaluate_offline_gate(passing).passed(),
        "valid shared offline gate was rejected");

    auto changed = passing;
    changed.repeated_optimizer_bit_identical = false;
    require_reject(changed);
    changed = passing;
    changed.optimizer_recipe_exact = false;
    require_reject(changed);
    changed = passing;
    changed.objective_strictly_improved = false;
    require_reject(changed);
    changed = passing;
    changed.surrogate_engine_agreement = false;
    require_reject(changed);
    changed = passing;
    changed.exact_model_identity = false;
    require_reject(changed);
    changed = passing;
    changed.model_isolation_passed = false;
    require_reject(changed);
    changed = passing;
    changed.candidate_train
        .equal_deck_listwise_cross_entropy =
        changed.parent_train
            .equal_deck_listwise_cross_entropy;
    require_reject(changed);
    changed = passing;
    changed.candidate_train.equal_deck_mean_regret =
        changed.parent_train.equal_deck_mean_regret;
    require_reject(changed);
    changed = passing;
    changed.candidate_dev
        .equal_deck_listwise_cross_entropy =
        changed.parent_dev
            .equal_deck_listwise_cross_entropy;
    require_reject(changed);
    changed = passing;
    changed.candidate_dev.equal_deck_mean_regret =
        changed.parent_dev.equal_deck_mean_regret;
    require_reject(changed);
    changed = passing;
    changed.candidate_dev
        .equal_deck_top_one_expected_agreement = 0.49;
    require_reject(changed);
    changed = passing;
    changed.candidate_dev
        .equal_deck_stable_pair_agreement = 0.49;
    require_reject(changed);
    changed = passing;
    changed.candidate_dev.equal_deck_successor_bce =
        changed.parent_dev.equal_deck_successor_bce +
        direct::kMaximumDevSuccessorBceIncrease +
        0.001;
    require_reject(changed);
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        changed = passing;
        changed.candidate_dev.decks[deck].mean_regret =
            changed.parent_dev.decks[deck].mean_regret +
            direct::kMaximumDevDeckRegretIncrease +
            0.001;
        require_reject(changed);
    }
}

void test_validation_fails_closed() {
    const auto parent = test_model();
    std::vector<double> short_observation(
        hidden::kFeatureCount - 1, 0.0);
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    learned_critic_observation_hidden_activations(
                        short_observation, parent));
        },
        "short hidden observation was accepted");

    std::vector<double> nonfinite_observation(
        hidden::kFeatureCount, 0.0);
    nonfinite_observation[0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    learned_critic_observation_hidden_activations(
                        nonfinite_observation,
                        parent));
        },
        "nonfinite hidden observation was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    learned_critic_hidden_output_parameters(
                        nullptr));
        },
        "null hidden-output parent was accepted");

    const auto actor =
        old_school::train_learned_actor_model(
            1, 0xDBC3BADULL);
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    learned_critic_hidden_output_parameters(
                        actor));
        },
        "wrong critic topology was accepted");

    hidden::Delta nonfinite_delta{};
    nonfinite_delta[0][0] =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    with_learned_critic_hidden_output_delta(
                        parent, nonfinite_delta));
        },
        "nonfinite engine delta was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                hidden::candidate_cell_value(
                    learning_dataset()
                        .roots.front()
                        .actions.front()
                        .worlds.front(),
                    nonfinite_delta));
        },
        "nonfinite analytic delta was accepted");

    hidden::Dataset invalid_hidden =
        learning_dataset();
    invalid_hidden.roots.front()
        .actions.front().worlds.front()
        .hidden[0][0] = 1.01;
    expect_rejected(
        [&] {
            hidden::validate_dataset(
                invalid_hidden);
        },
        "out-of-range tanh activation was accepted");

    expect_rejected(
        [&] {
            hidden::OptimizerConfig changed_recipe;
            changed_recipe.steps = 255;
            static_cast<void>(
                hidden::optimize(
                    learning_dataset(),
                    changed_recipe));
        },
        "changed hidden optimizer recipe was accepted");
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
        "terminal cells are fixed",
        test_terminal_cells_are_fixed);
    run(
        "independent leaf shift matches definition",
        test_independent_leaf_shift_matches_definition);
    run(
        "gradient matches both leaf blocks",
        test_gradient_matches_both_leaf_blocks);
    run(
        "Adam is deterministic and improves rank",
        test_adam_is_deterministic_and_improves_rank);
    run(
        "global gradient clip is active",
        test_global_gradient_clip_is_active);
    run(
        "engine seam and model isolation",
        test_engine_seam_and_model_isolation);
    run(
        "projection exact scoring and surrogate",
        test_projection_exact_scoring_and_surrogate);
    run(
        "shared offline gate fails closed",
        test_shared_offline_gate_fails_closed);
    run(
        "validation fails closed",
        test_validation_fails_closed);

    std::cout << passed
              << " decision-boundary rank-hidden tests passed\n";
}
