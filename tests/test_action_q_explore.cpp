#include "old_school/action_q_explore.hpp"
#include "old_school/action_q_field_gate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aq = old_school::action_q_explore;
namespace field = old_school::action_q_field_gate;

namespace {

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
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
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

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
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

void test_frozen_constants_and_seed_coordinates() {
    expect(
        aq::kCollectionRootSeed == 202607281751ULL &&
            aq::kScheduleGeneration == 0 &&
            aq::kFitBlock == 0 &&
            aq::kCheckBlock == 1 &&
            aq::kSourceTurnCap == 128 &&
            aq::kWorlds == 8 &&
            aq::kRolloutsPerWorld == 1 &&
            aq::kHorizonTurns == 4 &&
            aq::kMaximumRootsPerActorGame == 8 &&
            aq::kCandidateResidualWeight == 0.10,
        "AQ0 frozen constants drifted");
    expect(
        aq::fit_seed() == aq::kFitSeed &&
            aq::kFitSeed == 15687967101834164397ULL,
        "AQ0 fit seed drifted");
    expect(
        aq::root_search_seed(0, 3, 1, 7) !=
            aq::root_search_seed(1, 3, 1, 7) &&
            aq::root_search_seed(0, 3, 0, 7) !=
                aq::root_search_seed(0, 3, 1, 7) &&
            aq::root_search_seed(0, 3, 1, 6) !=
                aq::root_search_seed(0, 3, 1, 7),
        "AQ0 root seed domains collided");

    const auto optimizer = aq::optimizer_config();
    expect(
        optimizer.batch_size == 64 &&
            optimizer.epochs == 64 &&
            optimizer.learning_rate == 0.003 &&
            optimizer.beta1 == 0.9 &&
            optimizer.beta2 == 0.999 &&
            optimizer.epsilon == 1.0e-8 &&
            optimizer.global_gradient_norm_clip == 5.0 &&
            optimizer.seed == aq::kFitSeed &&
            optimizer.residual_weight == 0.10 &&
            optimizer.policy_temperature == 0.10,
        "AQ0 optimizer contract drifted");
}

void test_teacher_distribution_is_soft_and_normalized() {
    const std::vector<double> scores{0.1, 0.5, 0.9};
    const auto distribution =
        aq::teacher_distribution(scores);
    expect(
        distribution.size() == scores.size() &&
            std::all_of(
                distribution.begin(), distribution.end(),
                [](double value) {
                    return std::isfinite(value) && value > 0.0;
                }) &&
            std::abs(
                std::accumulate(
                    distribution.begin(), distribution.end(), 0.0) -
                1.0) < 1.0e-12 &&
            distribution[2] > distribution[1] &&
            distribution[1] > distribution[0],
        "AQ0 teacher distribution is not positive and ordered");
    expect_rejected(
        [] {
            const std::vector<double> empty;
            static_cast<void>(
                aq::teacher_distribution(empty));
        },
        "AQ0 accepted an empty teacher vector");
}

void test_combined_scores_use_bounded_centered_residual() {
    const std::vector<double> base{0.20, 0.40, 0.60};
    const std::vector<double> logits{-1.0, 0.0, 1.0};
    const auto combined =
        aq::combined_scores(base, logits, 0.10);
    expect(
        combined.size() == 3 &&
            combined[0] ==
                base[0] + 0.10 * std::tanh(-1.0) &&
            combined[1] == base[1] &&
            combined[2] ==
                base[2] + 0.10 * std::tanh(1.0),
        "AQ0 combined score formula drifted");
    const auto zero =
        aq::combined_scores(base, logits, 0.0);
    expect(zero == base,
           "zero residual did not preserve exact base scores");
}

void test_metrics_match_uniform_exact_tie_deployment() {
    const std::vector<double> teacher{0.9, 0.4, 0.9, 0.2};
    const std::vector<double> policy{0.8, 0.8, 0.1, 0.8};
    const auto metrics =
        aq::evaluate_root(teacher, policy);
    expect(
        metrics.teacher_support ==
                std::vector<std::size_t>{0, 2} &&
            metrics.policy_support ==
                std::vector<std::size_t>{0, 1, 3} &&
            metrics.top_one_expected_agreement == 1.0 / 3.0 &&
            metrics.regret ==
                0.9 - (0.9 + 0.4 + 0.2) / 3.0,
        "AQ0 tie metrics do not match uniform exact-max deployment");
}

void test_owner_features_and_action_scores_are_order_invariant() {
    const field::AncestralFieldRoot fixture =
        field::make_ancestral_field_root();
    const field::AncestralFieldRoot hidden =
        field::hidden_repartition_clone(fixture);
    std::vector<std::vector<double>> options;
    std::vector<std::vector<double>> hidden_options;
    for (const auto& action : fixture.legal_actions) {
        options.push_back(
            old_school::learned_priority_policy_features(
                fixture.state, fixture.actor, action,
                fixture.context.sorcery_actions,
                fixture.context.phase,
                fixture.context.consecutive_passes));
        hidden_options.push_back(
            old_school::learned_priority_policy_features(
                hidden.state, hidden.actor, action,
                hidden.context.sorcery_actions,
                hidden.context.phase,
                hidden.context.consecutive_passes));
    }
    expect(
        options == hidden_options,
        "AQ0 owner-safe action features exposed hidden cards");

    const auto seed_model =
        old_school::train_learned_value_champion(
            1, 0x4151304f52444552ULL);
    auto parameters =
        old_school::learned_priority_head_parameters(seed_model);
    for (std::size_t index = 0;
         index < parameters.direct.size(); ++index) {
        parameters.direct[index] =
            static_cast<double>(
                static_cast<int>(index % 7) - 3) *
            0.0078125;
    }
    const auto model =
        old_school::with_learned_priority_head_parameters(
            seed_model, parameters);
    const auto logits =
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Priority,
            model);
    std::vector<double> base(options.size());
    for (std::size_t index = 0; index < base.size(); ++index) {
        base[index] =
            0.25 + 0.05 * static_cast<double>(index);
    }
    const auto scores =
        aq::combined_scores(base, logits);

    std::reverse(options.begin(), options.end());
    std::reverse(base.begin(), base.end());
    const auto reversed_logits =
        old_school::learned_policy_head_logits(
            options,
            old_school::LearnedPolicyDecisionKind::Priority,
            model);
    auto reversed_scores =
        aq::combined_scores(base, reversed_logits);
    std::reverse(reversed_scores.begin(), reversed_scores.end());
    expect(
        scores == reversed_scores &&
            aq::exact_max_support(scores) ==
                aq::exact_max_support(reversed_scores),
        "AQ0 action-keyed scores changed under descriptor order");
}

} // namespace

int main() {
    TestRunner tests;
    tests.run(
        "frozen constants and seed coordinates",
        test_frozen_constants_and_seed_coordinates);
    tests.run(
        "teacher distribution",
        test_teacher_distribution_is_soft_and_normalized);
    tests.run(
        "bounded centered residual",
        test_combined_scores_use_bounded_centered_residual);
    tests.run(
        "uniform exact-tie metrics",
        test_metrics_match_uniform_exact_tie_deployment);
    tests.run(
        "owner-safe and order-invariant action scoring",
        test_owner_features_and_action_scores_are_order_invariant);
    return tests.finish();
}
