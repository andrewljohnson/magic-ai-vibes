#include "old_school/decision_density_bilinear.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace aq19 =
    old_school::decision_density_bilinear;

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cout << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, "
                  << failed_ << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

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
            std::string(message) + " actual=" +
            std::to_string(actual) + " expected=" +
            std::to_string(expected));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool bit_equal(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) ==
           std::bit_cast<std::uint64_t>(right);
}

bool positive_zero(double value) {
    return std::bit_cast<std::uint64_t>(value) == 0;
}

std::size_t action_count(
    aq19::priority::WidthStratum width) {
    switch (width) {
    case aq19::priority::WidthStratum::B2:
        return 2;
    case aq19::priority::WidthStratum::B3:
        return 3;
    case aq19::priority::WidthStratum::B4Plus:
        return 4;
    }
    throw std::runtime_error("invalid synthetic width");
}

aq19::Root make_root(
    old_school::DeckId deck,
    aq19::priority::WidthStratum width,
    std::size_t serial,
    std::string physical_group) {
    aq19::Root root{
        .stable_root_id =
            "root-" + std::to_string(serial),
        .physical_game_group =
            std::move(physical_group),
        .deck = deck,
        .width = width,
    };
    root.state[0] = 0.2 +
                    0.01 *
                        static_cast<double>(serial);
    root.state[1] = -0.4;
    root.state[17] = 0.75;
    const std::size_t options = action_count(width);
    root.options.reserve(options);
    for (std::size_t action = 0;
         action < options; ++action) {
        aq19::Option option{
            .canonical_ordinal = action,
            .base_aggregate_score =
                0.35 +
                0.04 * static_cast<double>(action),
            .teacher_aggregate_score =
                0.25 +
                0.12 * static_cast<double>(action),
        };
        option.action_features[0] =
            static_cast<double>(action) - 0.5;
        option.action_features[1] =
            (action % 2 == 0) ? -0.3 : 0.6;
        option.action_features[31] =
            0.1 * static_cast<double>(
                      serial % 5) +
            0.02 * static_cast<double>(action);
        for (std::size_t world = 0;
             world < aq19::labels::kWorlds; ++world) {
            option.common_world_teacher_samples[world] =
                std::clamp(
                    option.teacher_aggregate_score +
                        0.002 *
                            (static_cast<double>(world) -
                             3.5),
                    0.0, 1.0);
        }
        root.options.push_back(std::move(option));
    }
    aq19::validate_root(root);
    return root;
}

aq19::Dataset make_balanced_dataset(
    std::size_t repetitions = 1) {
    std::vector<aq19::Root> roots;
    std::size_t serial = 0;
    for (std::size_t repetition = 0;
         repetition < repetitions; ++repetition) {
        const std::string group =
            "group-" + std::to_string(repetition);
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            for (std::size_t width = 0;
                 width < aq19::priority::kWidthStrata;
                 ++width) {
                roots.push_back(
                    make_root(
                        static_cast<old_school::DeckId>(
                            deck),
                        static_cast<
                            aq19::priority::WidthStratum>(
                            width),
                        serial++, group));
            }
        }
    }
    return aq19::testing::make_dataset(
        std::move(roots));
}

void test_command_contract() {
    const std::array<std::string_view, 1> run{
        "--run"};
    expect(
        aq19::parse_command(run) ==
            aq19::Command::Run,
        "--run was not accepted");
    const std::array<std::string_view, 1> offline{
        "--offline-report"};
    expect(
        aq19::parse_command(offline) ==
            aq19::Command::OfflineReport,
        "--offline-report was not accepted");
    expect(
        aq19::command_authorizes_selector_seed(
            aq19::Command::Run) &&
            !aq19::command_authorizes_selector_seed(
                aq19::Command::OfflineReport),
        "offline command did not seal the selector seed");
    const std::array<std::string_view, 2> knobs{
        "--run", "--steps=1"};
    expect(
        !aq19::parse_command(knobs),
        "an undeclared knob was accepted");
}

void test_u0_and_zero_identity() {
    const auto& u0 =
        old_school::learned_priority_bilinear_u0();
    expect(u0[0] != u0[1], "U0 rows are identical");
    for (const auto& row : u0) {
        expect(
            std::any_of(
                row.begin(), row.end(),
                [](double value) {
                    return value == 1.0 / 32.0;
                }) &&
                std::any_of(
                    row.begin(), row.end(),
                    [](double value) {
                        return value == -1.0 / 32.0;
                    }),
            "a U0 row is constant");
    }
    const aq19::Dataset dataset =
        make_balanced_dataset();
    const aq19::Parameters zero;
    for (const aq19::Root& root : dataset.roots) {
        const aq19::ForwardResult result =
            aq19::forward(root, zero);
        for (std::size_t action = 0;
             action < root.options.size(); ++action) {
            expect(
                positive_zero(result.residuals[action]),
                "zero V did not return positive zero");
            expect(
                bit_equal(
                    result.scores[action],
                    root.options[action]
                        .base_aggregate_score),
                "zero V changed the base score");
        }
    }
}

void test_forward_runtime_and_permutation() {
    const aq19::Root root =
        make_root(
            old_school::DeckId::Blue,
            aq19::priority::WidthStratum::B4Plus,
            77, "physical");
    aq19::Parameters parameters;
    parameters.delta_u[0][0] = 0.03;
    parameters.delta_u[1][17] = -0.02;
    parameters.v[0][0] = 0.11;
    parameters.v[0][1] = -0.04;
    parameters.v[1][31] = 0.08;
    const aq19::ForwardResult expected =
        aq19::forward(root, parameters);
    const old_school::LearnedPriorityBilinear runtime(
        parameters);
    std::vector<std::size_t> canonical(
        root.options.size());
    std::iota(
        canonical.begin(), canonical.end(), 0);
    const std::vector<double> actual =
        runtime.residuals(
            aq19::testing::option_rows(root),
            canonical);
    expect(
        actual.size() == expected.residuals.size(),
        "runtime residual shape drifted");
    for (std::size_t action = 0;
         action < actual.size(); ++action) {
        expect(
            bit_equal(
                actual[action],
                expected.residuals[action]),
            "runtime and fitter arithmetic differ");
    }

    const std::vector<std::size_t> permutation{
        2, 0, 3, 1};
    const aq19::Root permuted =
        aq19::testing::permute_options(
            root, permutation);
    const auto permuted_values =
        aq19::forward(permuted, parameters).residuals;
    for (std::size_t row = 0;
         row < permutation.size(); ++row) {
        expect(
            bit_equal(
                permuted_values[row],
                expected.residuals[
                    permutation[row]]),
            "canonical reduction changed after permutation");
    }
}

void test_gradient_finite_difference() {
    const aq19::Dataset dataset =
        make_balanced_dataset();
    aq19::Parameters parameters;
    parameters.delta_u[0][0] = 0.02;
    parameters.delta_u[1][1] = -0.03;
    parameters.v[0][0] = 0.04;
    parameters.v[0][1] = -0.025;
    parameters.v[1][31] = 0.035;
    const aq19::testing::ObjectiveProbe analytic =
        aq19::testing::objective_probe(
            dataset, parameters);
    constexpr double epsilon = 1.0e-6;
    const auto check =
        [&](double aq19::Parameters::* /*unused*/) {};
    (void)check;
    const auto finite_difference =
        [&](bool state, std::size_t rank,
            std::size_t feature) {
            aq19::Parameters plus = parameters;
            aq19::Parameters minus = parameters;
            if (state) {
                plus.delta_u[rank][feature] += epsilon;
                minus.delta_u[rank][feature] -= epsilon;
            } else {
                plus.v[rank][feature] += epsilon;
                minus.v[rank][feature] -= epsilon;
            }
            return (
                       aq19::testing::objective_probe(
                           dataset, plus)
                           .objective -
                       aq19::testing::objective_probe(
                           dataset, minus)
                           .objective) /
                   (2.0 * epsilon);
        };
    expect_near(
        analytic.gradient.delta_u[0][0],
        finite_difference(true, 0, 0),
        2.0e-7,
        "delta-U gradient mismatch");
    expect_near(
        analytic.gradient.v[0][0],
        finite_difference(false, 0, 0),
        2.0e-7,
        "V gradient mismatch");
    expect_near(
        analytic.gradient.v[1][31],
        finite_difference(false, 1, 31),
        2.0e-7,
        "second-rank V gradient mismatch");

    std::vector<aq19::Root> permuted_roots =
        dataset.roots;
    permuted_roots[2] =
        aq19::testing::permute_options(
            permuted_roots[2], {2, 0, 3, 1});
    const aq19::testing::ObjectiveProbe permuted =
        aq19::testing::objective_probe(
            aq19::testing::make_dataset(
                std::move(permuted_roots)),
            parameters);
    expect(
        bit_equal(
            analytic.objective,
            permuted.objective) &&
            aq19::testing::parameters_bit_identical(
                analytic.gradient,
                permuted.gradient),
        "objective/gradient reductions ignored "
        "canonical action order");
}

void test_optimizer_is_live_and_deterministic() {
    const aq19::Dataset dataset =
        make_balanced_dataset();
    const aq19::testing::ObjectiveProbe initial =
        aq19::testing::objective_probe(
            dataset, aq19::Parameters{});
    bool live_v_gradient = false;
    for (const auto& rank : initial.gradient.v) {
        live_v_gradient =
            live_v_gradient ||
            std::any_of(
                rank.begin(), rank.end(),
                [](double value) {
                    return value != 0.0;
                });
    }
    expect(
        live_v_gradient,
        "zero-V derivative workspace killed the "
        "declared live first gradient");
    aq19::OptimizerConfig config;
    config.steps = 8;
    const aq19::OptimizerReport first =
        aq19::optimize(dataset, config);
    const aq19::OptimizerReport second =
        aq19::optimize(dataset, config);
    expect(
        aq19::optimizer_bit_identical(first, second),
        "deterministic repeat drifted");
    expect(
        first.final_objective <
            first.initial_objective,
        "optimizer did not reduce its objective");
    expect(
        !std::all_of(
            first.parameters.v[0].begin(),
            first.parameters.v[0].end(),
            positive_zero),
        "fixed U0 did not give V a live gradient");
    expect(
        first.parameter_sha256.size() == 64,
        "parameter SHA-256 is malformed");
    aq19::OptimizerConfig drifted = config;
    drifted.residual_weight = 0.20;
    expect_rejected(
        [&] {
            (void)aq19::optimize(dataset, drifted);
        },
        "an unsealed residual weight was accepted");
}

void test_grouped_folds() {
    const aq19::Dataset dataset =
        make_balanced_dataset(4);
    const aq19::FoldAssignment first =
        aq19::assign_grouped_folds(
            dataset, "synthetic-corpus");
    const aq19::FoldAssignment second =
        aq19::assign_grouped_folds(
            dataset, "synthetic-corpus");
    expect(first == second, "fold assignment drifted");
    expect(
        first.group_folds.size() == 4,
        "physical groups were not grouped");
    for (std::size_t fold = 0;
         fold < aq19::kFoldCount; ++fold) {
        expect(
            first.folds[fold].physical_groups == 1 &&
                first.folds[fold].roots == 15,
            "synthetic fold balance is wrong");
        const aq19::Dataset holdout =
            aq19::fold_holdout_dataset(
                dataset, first, fold);
        const aq19::Dataset fit =
            aq19::fold_training_dataset(
                dataset, first, fold);
        expect(
            holdout.roots.size() == 15 &&
                fit.roots.size() == 45,
            "fold selection leaked or dropped roots");
    }
}

void test_metrics_and_gate() {
    const aq19::Dataset dataset =
        make_balanced_dataset();
    const aq19::Metrics metrics =
        aq19::evaluate(dataset, aq19::Parameters{});
    expect(
        metrics.pairs.roots == 15,
        "precomputed metric root census drifted");
    const aq19::PairCensus census =
        aq19::pair_census(dataset);
    expect(
        census.total.roots == 15 &&
            census.total.eligible_pairs ==
                census.total.potential_pairs,
        "pair census is wrong");

    aq19::Metrics parent;
    aq19::Metrics candidate;
    parent.pairs.equal_deck_pair_bce = 0.7;
    candidate.pairs.equal_deck_pair_bce = 0.6;
    parent.ranking
        .equal_deck_listwise_cross_entropy = 0.8;
    candidate.ranking
        .equal_deck_listwise_cross_entropy = 0.7;
    parent.ranking.equal_deck_mean_regret = 0.2;
    candidate.ranking.equal_deck_mean_regret = 0.1;
    parent.ranking
        .equal_deck_top_one_expected_agreement = 0.5;
    candidate.ranking
        .equal_deck_top_one_expected_agreement = 0.6;
    parent.ranking
        .equal_deck_stable_pair_agreement = 0.5;
    candidate.ranking
        .equal_deck_stable_pair_agreement = 0.6;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        parent.ranking.decks[deck].mean_regret = 0.2;
        candidate.ranking.decks[deck].mean_regret =
            0.1;
    }
    const aq19::OfflineGate gate =
        aq19::evaluate_offline_gate({
            .parent_train = parent,
            .candidate_train = candidate,
            .parent_oof = parent,
            .candidate_oof = candidate,
            .parent_dev = parent,
            .candidate_dev = candidate,
            .cache_identity_exact = true,
            .corpus_census_exact = true,
            .pair_census_exact = true,
            .fold_manifest_exact = true,
            .feature_layout_exact = true,
            .state_prefix_bit_identical = true,
            .teacher_zero_invariant = true,
            .optimizer_recipe_exact = true,
            .grouped_oof_exact = true,
            .repeated_fits_bit_identical = true,
            .parameter_replay_bit_identical = true,
            .positive_zero_parent_equivalent = true,
            .residuals_finite_and_bounded = true,
            .legal_action_permutation_equivariant = true,
            .hidden_repartition_bit_identical = true,
            .symmetric_continuation_propagation = true,
            .parent_immutable = true,
            .treatment_only_isolation = true,
        });
    expect(gate.passed(), "valid offline gate rejected");
    aq19::OfflineGateInputs bad{
        .parent_train = parent,
        .candidate_train = candidate,
        .parent_oof = parent,
        .candidate_oof = candidate,
        .parent_dev = parent,
        .candidate_dev = candidate,
    };
    expect(
        !aq19::evaluate_offline_gate(bad).passed(),
        "missing invariants passed the gate");
}

void test_report_is_complete_and_offline_explicit() {
    aq19::RunReport report;
    report.cache_identity_exact = true;
    report.corpus_census_exact = true;
    report.pair_census_exact = true;
    report.fold_manifest_exact = true;
    report.feature_layout_exact = true;
    report.state_prefix_bit_identical = true;
    report.teacher_zero_invariant = true;
    report.optimizer_recipe_exact = true;
    report.grouped_oof_exact = true;
    report.repeated_fits_bit_identical = true;
    report.parameter_replay_bit_identical = true;
    report.positive_zero_parent_equivalent = true;
    report.residuals_finite_and_bounded = true;
    report.legal_action_permutation_equivariant = true;
    report.hidden_repartition_bit_identical = true;
    report.symmetric_continuation_propagation = true;
    report.parent_immutable = true;
    report.treatment_only_isolation = true;
    report.gate.invariants_passed = true;
    std::ostringstream output;
    aq19::print_report(output, report);
    const std::string text = output.str();
    expect(
        text.find(
            "invariants identity cache=pass "
            "corpus_census=pass pair_census=pass "
            "fold_manifest=pass "
            "feature_layout_674_219=pass "
            "state_prefix=pass teacher_zero=pass") !=
            std::string::npos,
        "identity invariant report is incomplete");
    expect(
        text.find(
            "invariants execution optimizer_recipe=pass "
            "grouped_oof=pass repeated_fits=pass "
            "parameter_replay=pass zero_parent=pass "
            "finite_bounded=pass permutation=pass") !=
            std::string::npos,
        "execution invariant report is incomplete");
    expect(
        text.find(
            "invariants isolation hidden_repartition=pass "
            "symmetric_continuation=pass "
            "parent_immutable=pass treatment_only=pass "
            "conjunction=pass") != std::string::npos,
        "isolation invariant report is incomplete");
    expect(
        text.find(
            "selector_seed_authorized=no "
            "selector_opened=no gameplay_games=0") !=
            std::string::npos,
        "offline selector closure is not explicit");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const std::string needle =
            "  " +
            std::string(old_school::deck_name(
                static_cast<old_school::DeckId>(deck))) +
            " pair_bce ";
        std::size_t count = 0;
        std::size_t offset = 0;
        while ((offset = text.find(needle, offset)) !=
               std::string::npos) {
            ++count;
            offset += needle.size();
        }
        expect(
            count == 3,
            "TRAIN/OOF/DEV per-deck metrics are missing");
    }
}

void test_validation_rejects_drift() {
    aq19::Root root =
        make_root(
            old_school::DeckId::Green,
            aq19::priority::WidthStratum::B3,
            1, "physical");
    root.options[1].canonical_ordinal = 0;
    expect_rejected(
        [&] {
            aq19::validate_root(root);
        },
        "duplicate canonical ordinal was accepted");
    root =
        make_root(
            old_school::DeckId::Green,
            aq19::priority::WidthStratum::B3,
            1, "physical");
    root.options[0].base_aggregate_score =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            aq19::validate_root(root);
        },
        "nonfinite aggregate was accepted");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "strict CLI contract", test_command_contract);
    runner.run(
        "fixed U0 and zero identity",
        test_u0_and_zero_identity);
    runner.run(
        "runtime replay and permutation",
        test_forward_runtime_and_permutation);
    runner.run(
        "analytic gradient finite difference",
        test_gradient_finite_difference);
    runner.run(
        "optimizer deterministic and live",
        test_optimizer_is_live_and_deterministic);
    runner.run(
        "grouped folds", test_grouped_folds);
    runner.run(
        "precomputed metrics and gate",
        test_metrics_and_gate);
    runner.run(
        "complete offline report",
        test_report_is_complete_and_offline_explicit);
    runner.run(
        "validation rejects drift",
        test_validation_rejects_drift);
    return runner.finish();
}
