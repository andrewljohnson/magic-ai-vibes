#include "old_school/decision_boundary_rank_direct.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace rank =
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

static_assert(!HasStateMember<rank::RankCell>);
static_assert(!HasOpponentHandMember<rank::RankCell>);
static_assert(rank::kFeatureCount == 674);
static_assert(rank::kLeafCount == 2);

rank::RankCell make_cell(
    std::size_t feature, double feature_value,
    double teacher, double parent) {
    rank::RankCell cell{
        .observation =
            std::vector<double>(
                rank::kFeatureCount, 0.0),
        .parent_leaf_values = {parent, parent},
        .teacher_target = teacher,
    };
    cell.observation[feature] = feature_value;
    return cell;
}

rank::Dataset learning_dataset() {
    std::vector<rank::RankRoot> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < 2; ++copy) {
            rank::RankRoot root{
                .stable_root_id =
                    "root-" + std::to_string(deck) +
                    "-" + std::to_string(copy),
                .deck =
                    static_cast<old_school::DeckId>(deck),
                .actions = {
                    rank::RankAction{
                        .worlds = {
                            make_cell(
                                deck, 1.0, 0.9, 0.48),
                            make_cell(
                                deck, 1.0, 0.9, 0.48),
                        },
                    },
                    rank::RankAction{
                        .worlds = {
                            make_cell(
                                deck, -1.0, 0.1, 0.52),
                            make_cell(
                                deck, -1.0, 0.1, 0.52),
                        },
                    },
                },
            };
            roots.push_back(std::move(root));
        }
    }
    return rank::testing::make_dataset(
        std::move(roots));
}

std::shared_ptr<const old_school::LearnedModel>
test_model() {
    static const auto model =
        old_school::train_learned_model(
            1, 0xDBC2D1CEULL);
    return model;
}

rank::Dataset model_backed_dataset(
    std::shared_ptr<const old_school::LearnedModel> model) {
    rank::Dataset dataset =
        learning_dataset();
    for (rank::RankRoot& root : dataset.roots) {
        for (rank::RankAction& action : root.actions) {
            for (rank::RankCell& cell : action.worlds) {
                cell.parent_leaf_values =
                    old_school::
                        learned_critic_observation_leaf_values(
                            cell.observation, model);
            }
        }
    }
    rank::validate_dataset(dataset);
    return dataset;
}

void test_fixed_recipe_and_owner_safe_shape() {
    const rank::OptimizerConfig recipe;
    expect(
        recipe.fit_tag == 202607291401ULL &&
            rank::kSelectorSeed == 202607291411ULL &&
            recipe.steps == 256 &&
            recipe.learning_rate == 0.001 &&
            recipe.beta_one == 0.9 &&
            recipe.beta_two == 0.999 &&
            recipe.epsilon == 1.0e-8 &&
            recipe.global_gradient_norm_clip == 5.0 &&
            recipe.temperature == 0.10 &&
            recipe.mix == 0.90 &&
            recipe.l2_tether == 0.10,
        "fixed DBC2 optimizer recipe drifted");
    const rank::Dataset dataset =
        learning_dataset();
    rank::validate_dataset(dataset);
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
    rank::RankCell terminal{
        .parent_leaf_values = {0.8, 0.8},
        .teacher_target = 0.8,
        .terminal_before_boundary = true,
    };
    std::vector<double> delta(
        rank::kFeatureCount, 1000.0);
    expect(
        rank::candidate_cell_value(
            terminal, delta) == 0.8,
        "terminal utility was changed by critic delta");
}

void test_shared_logit_shift_matches_definition() {
    rank::RankCell cell =
        make_cell(0, 2.0, 0.5, 0.5);
    cell.parent_leaf_values = {0.25, 0.75};
    std::vector<double> delta(
        rank::kFeatureCount, 0.0);
    delta[0] = 0.125;
    const double shift = 0.25;
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
        (sigmoid(logit(0.25) + shift) +
         sigmoid(logit(0.75) + shift));
    expect(
        rank::candidate_cell_value(
            cell, delta) == expected,
        "shared leaf-logit delta semantics drifted");
    std::fill(delta.begin(), delta.end(), 0.0);
    expect(
        rank::candidate_cell_value(cell, delta) ==
            (cell.parent_leaf_values[0] +
             cell.parent_leaf_values[1]) /
                2.0,
        "zero delta did not preserve cached parent bits");
}

void test_adam_fit_is_bit_deterministic_and_improves_rank() {
    const rank::Dataset dataset =
        learning_dataset();
    const rank::OptimizerReport first =
        rank::optimize(dataset);
    const rank::OptimizerReport repeated =
        rank::optimize(dataset);
    expect(
        first == repeated,
        "repeated full-batch Adam fit drifted");
    expect(
        first.completed_steps == rank::kAdamSteps &&
            first.delta.size() ==
                rank::kFeatureCount &&
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
            std::isfinite(first.delta_l2_norm) &&
            first.delta_l2_norm > 0.0 &&
            std::isfinite(
                first.final_gradient_l2_norm) &&
            std::isfinite(
                first
                    .maximum_preclip_gradient_l2_norm) &&
            first.before.stable_pairs > 0 &&
            first.after
                    .equal_deck_stable_pair_agreement >
                first.before
                    .equal_deck_stable_pair_agreement &&
            std::isfinite(
                first.after.equal_deck_successor_bce) &&
            std::isfinite(
                first.after.equal_deck_successor_brier) &&
            std::isfinite(
                first.after.equal_deck_successor_bias) &&
            std::isfinite(
                first.after.equal_deck_successor_ece),
        "synthetic listwise fit did not improve");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            first.delta[deck] > 0.0 &&
                first.after.decks[deck].mean_regret <
                    first.before.decks[deck].mean_regret,
            "listwise fit missed a synthetic deck");
    }
}

void test_model_application_is_direct_only_and_exact() {
    const auto parent = test_model();
    std::vector<double> delta(
        rank::kFeatureCount, 0.0);
    // Exactly representable values make the realized-addition witness
    // independent of decimal-rounding subtleties.
    delta[0] = 0.125;
    delta[17] = -0.0625;
    const auto isolation =
        rank::apply_delta(parent, delta);
    expect(
        isolation.passed() &&
            isolation.shared_delta_exact &&
            isolation.context_direct_path_frozen &&
            isolation.changed_coordinates == 2 &&
            isolation.parent_fingerprint_before ==
                isolation.parent_fingerprint_after &&
            isolation.candidate_fingerprint ==
                isolation
                    .repeated_candidate_fingerprint,
        "shared direct-path application failed isolation");
}

void test_global_gradient_clip_is_active() {
    rank::Dataset dataset =
        learning_dataset();
    for (rank::RankRoot& root : dataset.roots) {
        for (rank::RankAction& action : root.actions) {
            for (rank::RankCell& cell : action.worlds) {
                for (double& value : cell.observation) {
                    value *= 10000.0;
                }
            }
        }
    }
    const rank::OptimizerReport fit =
        rank::optimize(dataset);
    expect(
        fit.maximum_preclip_gradient_l2_norm >
                rank::kGlobalGradientNormClip &&
            fit.clipped_steps > 0 &&
            std::isfinite(fit.final_objective) &&
            std::isfinite(fit.delta_l2_norm),
        "global pre-Adam gradient clip was not active");
}

void test_exact_model_scoring_and_offline_gate() {
    const auto parent = test_model();
    const rank::Dataset dataset =
        model_backed_dataset(parent);
    const std::vector<double> zero(
        rank::kFeatureCount, 0.0);
    const rank::Metrics analytic_parent =
        rank::evaluate(dataset, zero);
    const rank::Metrics engine_parent =
        rank::evaluate_model(dataset, parent);
    expect(
        analytic_parent == engine_parent,
        "zero-delta metrics differ from exact parent");

    const rank::OptimizerReport fit =
        rank::optimize(dataset);
    const auto isolation =
        rank::apply_delta(
            parent, fit.delta);
    const rank::Corpus corpus =
        rank::testing::make_corpus(
            dataset, dataset,
            old_school::
                learned_model_component_fingerprints(
                    parent));
    const rank::ExactEvaluationReport exact =
        rank::evaluate_exact(
            corpus, parent, isolation.candidate,
            fit.delta);
    expect(
        exact.parent_train == engine_parent &&
            exact.candidate_train ==
                rank::evaluate_model(
                    dataset, isolation.candidate) &&
            exact
                    .maximum_surrogate_engine_cell_difference <=
                1.0e-12,
        "exact evaluation did not score applied model");

    rank::OptimizerReport diagnostic_only = fit;
    diagnostic_only.after = fit.before;
    const rank::OfflineGate passed =
        rank::evaluate_offline_gate(
            diagnostic_only, diagnostic_only,
            exact, isolation);
    expect(
        passed.passed(),
        "passing exact applied-model treatment was rejected");

    rank::ExactEvaluationReport regressed = exact;
    regressed.candidate_dev.equal_deck_mean_regret =
        exact.parent_dev.equal_deck_mean_regret + 1.0;
    regressed.candidate_dev.decks[0].mean_regret =
        exact.parent_dev.decks[0].mean_regret + 1.0;
    const rank::OfflineGate rejected =
        rank::evaluate_offline_gate(
            fit, fit, regressed, isolation);
    expect(
        !rejected.passed() &&
            !rejected.dev_regret_strictly_improved &&
            !rejected.dev_deck_regret_guard[0],
        "DEV regret mutations escaped the gate");

    rank::ExactEvaluationReport wrong_identity = exact;
    wrong_identity.candidate_fingerprint =
        std::string(64, '0');
    const rank::OfflineGate identity_rejected =
        rank::evaluate_offline_gate(
            fit, fit, wrong_identity, isolation);
    expect(
        !identity_rejected.passed() &&
            !identity_rejected.exact_model_identity,
        "metrics from a different candidate escaped the gate");
}

void test_validation_fails_closed() {
    rank::Dataset imbalanced =
        learning_dataset();
    imbalanced.roots.pop_back();
    --imbalanced.roots_by_deck.back();
    expect_rejected(
        [&] { rank::validate_dataset(imbalanced); },
        "deck imbalance was accepted");

    rank::Dataset short_observation =
        learning_dataset();
    short_observation.roots.front()
        .actions.front().worlds.front()
        .observation.pop_back();
    expect_rejected(
        [&] {
            rank::validate_dataset(
                short_observation);
        },
        "short observation was accepted");

    rank::Dataset duplicate =
        learning_dataset();
    duplicate.roots[1].stable_root_id =
        duplicate.roots[0].stable_root_id;
    expect_rejected(
        [&] { rank::validate_dataset(duplicate); },
        "duplicate root identity was accepted");

    expect_rejected(
        [&] {
            rank::OptimizerConfig changed;
            changed.steps = 255;
            static_cast<void>(
                rank::optimize(
                    learning_dataset(), changed));
        },
        "changed optimizer recipe was accepted");

    std::vector<double> nonfinite(
        rank::kFeatureCount, 0.0);
    nonfinite[0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            static_cast<void>(
                rank::candidate_cell_value(
                    learning_dataset()
                        .roots.front()
                        .actions.front()
                        .worlds.front(),
                    nonfinite));
        },
        "nonfinite delta was accepted");
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
        "shared logit shift matches definition",
        test_shared_logit_shift_matches_definition);
    run(
        "Adam fit is deterministic and improves rank",
        test_adam_fit_is_bit_deterministic_and_improves_rank);
    run(
        "model application is direct-only and exact",
        test_model_application_is_direct_only_and_exact);
    run(
        "global gradient clip is active",
        test_global_gradient_clip_is_active);
    run(
        "exact model scoring and offline gate",
        test_exact_model_scoring_and_offline_gate);
    run(
        "validation fails closed",
        test_validation_fails_closed);

    std::cout << passed
              << " decision-boundary rank-direct tests passed\n";
}
