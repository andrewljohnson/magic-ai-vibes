#include "old_school/game.hpp"
#include "old_school/learned_priority_bilinear.hpp"
#include "old_school/learned_priority_sparse_cross.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using old_school::LearnedPriorityBilinear;
using old_school::LearnedPriorityBilinearParameters;
using old_school::LearnedPrioritySparseCross;
using old_school::LearnedPrioritySparseCrossTerm;
using old_school::LearnedPrioritySparseCrossTerms;

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
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool positive_zero(double value) {
    return std::bit_cast<std::uint64_t>(value) == 0;
}

std::vector<std::vector<double>> sample_rows() {
    std::vector<std::vector<double>> rows(
        3,
        std::vector<double>(
            old_school::
                kLearnedPriorityBilinearPolicyFeatureCount,
            0.0));
    for (auto& row : rows) {
        row[0] = 1.0;
        row[7] = -0.5;
        row[101] = 0.25;
    }
    const std::size_t action =
        old_school::
            kLearnedPriorityBilinearStateFeatureCount;
    rows[0][action] = -0.5;
    rows[0][action + 1] = 0.25;
    rows[1][action] = 0.75;
    rows[1][action + 1] = -0.25;
    rows[2][action] = 0.25;
    rows[2][action + 1] = 1.0;
    return rows;
}

std::vector<double> reference_residuals(
    const LearnedPriorityBilinearParameters& parameters,
    const std::vector<std::vector<double>>& rows,
    std::span<const std::size_t> canonical) {
    std::array<
        double,
        old_school::
            kLearnedPriorityBilinearActionFeatureCount>
        mean{};
    for (const std::size_t index : canonical) {
        for (std::size_t feature = 0;
             feature < mean.size(); ++feature) {
            mean[feature] +=
                rows[index]
                    [old_school::
                         kLearnedPriorityBilinearStateFeatureCount +
                     feature];
        }
    }
    for (double& value : mean) {
        value /= static_cast<double>(rows.size());
    }

    std::array<
        double,
        old_school::kLearnedPriorityBilinearRank>
        hidden{};
    const auto& u0 =
        old_school::learned_priority_bilinear_u0();
    const auto& state = rows[canonical.front()];
    for (std::size_t rank = 0;
         rank < hidden.size(); ++rank) {
        double value = 0.0;
        for (std::size_t feature = 0;
             feature <
             old_school::
                 kLearnedPriorityBilinearStateFeatureCount;
             ++feature) {
            value +=
                (u0[rank][feature] +
                 parameters.delta_u[rank][feature]) *
                state[feature];
        }
        hidden[rank] = std::tanh(value);
    }

    std::vector<double> logits(rows.size(), 0.0);
    for (std::size_t index = 0;
         index < rows.size(); ++index) {
        for (std::size_t rank = 0;
             rank < hidden.size(); ++rank) {
            double projected = 0.0;
            for (std::size_t feature = 0;
                 feature < mean.size(); ++feature) {
                projected +=
                    parameters.v[rank][feature] *
                    (rows[index]
                         [old_school::
                              kLearnedPriorityBilinearStateFeatureCount +
                          feature] -
                     mean[feature]);
            }
            logits[index] += hidden[rank] * projected;
        }
    }
    double mean_logit = 0.0;
    for (const std::size_t index : canonical) {
        mean_logit += logits[index];
    }
    mean_logit /= static_cast<double>(rows.size());
    for (double& value : logits) {
        value =
            old_school::
                kLearnedPriorityBilinearResidualWeight *
            std::tanh(value - mean_logit);
        if (value == 0.0) {
            value = 0.0;
        }
    }
    return logits;
}

LearnedPriorityBilinearParameters nonzero_parameters() {
    LearnedPriorityBilinearParameters parameters;
    parameters.delta_u[0][0] = 0.125;
    parameters.delta_u[1][7] = -0.0625;
    parameters.v[0][0] = 0.75;
    parameters.v[0][1] = -0.25;
    parameters.v[1][0] = 0.5;
    parameters.v[1][1] = 0.375;
    return parameters;
}

LearnedPrioritySparseCrossTerms sparse_terms_for_rows(
    const std::vector<std::vector<double>>& rows,
    std::span<const std::size_t> canonical_order) {
    expect(
        !rows.empty() && canonical_order.size() == rows.size(),
        "AQ20 test rows are empty or incomplete");
    std::size_t state_feature =
        old_school::
            kLearnedPrioritySparseCrossStateFeatureCount;
    for (std::size_t feature = 0;
         feature <
         old_school::
             kLearnedPrioritySparseCrossStateFeatureCount;
         ++feature) {
        if (rows[canonical_order.front()][feature] != 0.0) {
            state_feature = feature;
            break;
        }
    }
    expect(
        state_feature <
            old_school::
                kLearnedPrioritySparseCrossStateFeatureCount,
        "AQ20 test rows have no active state feature");

    std::size_t action_feature =
        old_school::
            kLearnedPrioritySparseCrossActionFeatureCount;
    const std::size_t action_offset =
        old_school::
            kLearnedPrioritySparseCrossStateFeatureCount;
    for (std::size_t feature = 0;
         feature <
         old_school::
             kLearnedPrioritySparseCrossActionFeatureCount;
         ++feature) {
        const double first =
            rows[canonical_order.front()]
                [action_offset + feature];
        if (std::any_of(
                canonical_order.begin() + 1,
                canonical_order.end(),
                [&](std::size_t index) {
                    return rows[index]
                               [action_offset + feature] !=
                           first;
                })) {
            action_feature = feature;
            break;
        }
    }
    expect(
        action_feature <
            old_school::
                kLearnedPrioritySparseCrossActionFeatureCount,
        "AQ20 test rows have no varying action feature");

    LearnedPrioritySparseCrossTerms terms;
    terms.reserve(
        old_school::kLearnedPrioritySparseCrossTermCount);
    for (std::size_t term = 0;
         term <
         old_school::kLearnedPrioritySparseCrossTermCount;
         ++term) {
        terms.push_back({
            .state_feature =
                (state_feature + term) %
                old_school::
                    kLearnedPrioritySparseCrossStateFeatureCount,
            .action_feature =
                (action_feature + term) %
                old_school::
                    kLearnedPrioritySparseCrossActionFeatureCount,
            .sigma =
                1.0 +
                static_cast<double>(term) / 32.0,
            .beta =
                term == 0
                    ? 0.75
                    : (term % 2 == 0 ? 0.01 : -0.01),
        });
    }
    return terms;
}

std::vector<double> reference_sparse_cross_residuals(
    const LearnedPrioritySparseCrossTerms& terms,
    const std::vector<std::vector<double>>& rows,
    std::span<const std::size_t> canonical_order) {
    std::vector<double> logits(rows.size(), 0.0);
    const std::size_t action_offset =
        old_school::
            kLearnedPrioritySparseCrossStateFeatureCount;
    for (const LearnedPrioritySparseCrossTerm& term : terms) {
        double action_mean = 0.0;
        for (const std::size_t index : canonical_order) {
            action_mean +=
                rows[index]
                    [action_offset + term.action_feature];
        }
        action_mean /=
            static_cast<double>(canonical_order.size());
        for (std::size_t index = 0;
             index < rows.size(); ++index) {
            const double centered_action =
                rows[index]
                    [action_offset + term.action_feature] -
                action_mean;
            logits[index] +=
                term.beta *
                rows[index][term.state_feature] *
                centered_action / term.sigma;
        }
    }

    double mean_logit = 0.0;
    for (const std::size_t index : canonical_order) {
        mean_logit += logits[index];
    }
    mean_logit /=
        static_cast<double>(canonical_order.size());
    for (double& value : logits) {
        value =
            old_school::
                kLearnedPrioritySparseCrossResidualWeight *
            std::tanh(value - mean_logit);
        if (value == 0.0) {
            value = 0.0;
        }
    }
    return logits;
}

void test_fixed_u0_and_shape() {
    static_assert(
        old_school::kLearnedPriorityBilinearRank == 2);
    static_assert(
        old_school::
            kLearnedPriorityBilinearStateFeatureCount ==
        674);
    static_assert(
        old_school::
            kLearnedPriorityBilinearActionFeatureCount ==
        219);
    static_assert(
        old_school::
            kLearnedPriorityBilinearPolicyFeatureCount ==
        893);
    static_assert(
        old_school::kLearnedPriorityBilinearWorlds ==
        8);
    const auto& u0 =
        old_school::learned_priority_bilinear_u0();
    constexpr std::array<int, 8> first = {
        -1, 1, -1, -1, -1, 1, -1, -1,
    };
    constexpr std::array<int, 8> second = {
        -1, 1, 1, -1, 1, -1, -1, -1,
    };
    for (std::size_t feature = 0;
         feature < u0[0].size(); ++feature) {
        expect(
            std::abs(u0[0][feature]) == 1.0 / 32.0 &&
                std::abs(u0[1][feature]) == 1.0 / 32.0,
            "U0 coordinate is not fixed Rademacher scale");
    }
    for (std::size_t feature = 0;
         feature < first.size(); ++feature) {
        expect(
            u0[0][feature] ==
                static_cast<double>(first[feature]) /
                    32.0 &&
                u0[1][feature] ==
                    static_cast<double>(second[feature]) /
                        32.0,
            "U0 SplitMix64 prefix drifted");
    }
    expect(
        u0[0] != u0[1],
        "U0 rank rows must be distinct");
    expect(
        std::any_of(
            u0[0].begin(), u0[0].end(),
            [](double value) { return value > 0.0; }) &&
            std::any_of(
                u0[0].begin(), u0[0].end(),
                [](double value) {
                    return value < 0.0;
                }),
        "U0 row must be nonconstant");
}

void test_zero_parameters_are_exact_positive_zero() {
    const auto object =
        std::make_shared<LearnedPriorityBilinear>(
            LearnedPriorityBilinearParameters{});
    expect(
        object->zero_action_projection(),
        "zero parameters did not select V fast path");
    constexpr std::array<std::size_t, 3> canonical = {
        0, 1, 2,
    };
    const auto residuals =
        object->residuals(sample_rows(), canonical);
    expect(
        residuals.size() == 3 &&
            std::all_of(
                residuals.begin(), residuals.end(),
                positive_zero),
        "zero V did not return exact positive zero");
    expect(
        object->digest().size() == 32,
        "local digest width drifted");
    expect(
        old_school::
                learned_priority_bilinear_canonical_bytes(
                    object->parameters()) ==
            old_school::
                learned_priority_bilinear_canonical_bytes(
                    LearnedPriorityBilinearParameters{}),
        "canonical zero parameter bytes changed");
}

void test_forward_matches_reference_and_is_bounded() {
    const LearnedPriorityBilinearParameters parameters =
        nonzero_parameters();
    const LearnedPriorityBilinear object(parameters);
    const auto rows = sample_rows();
    const std::array<std::size_t, 3> canonical = {
        0, 1, 2,
    };
    const auto actual =
        object.residuals(rows, canonical);
    const auto expected =
        reference_residuals(
            parameters, rows, canonical);
    expect(actual.size() == expected.size(),
           "forward result width drifted");
    for (std::size_t index = 0;
         index < actual.size(); ++index) {
        expect_near(
            actual[index], expected[index], 1.0e-15,
            "bilinear forward disagrees with reference");
        expect(
            std::abs(actual[index]) <=
                old_school::
                    kLearnedPriorityBilinearResidualWeight,
            "bilinear residual exceeded bound");
    }
    const std::vector<std::vector<double>> one = {
        rows.front(),
    };
    constexpr std::array<std::size_t, 1>
        one_canonical = {0};
    const auto one_result =
        object.residuals(one, one_canonical);
    expect(
        one_result.size() == 1 &&
            positive_zero(one_result.front()),
        "one-action centered residual is not positive zero");
}

void test_permutation_equivariance() {
    const LearnedPriorityBilinear object(
        nonzero_parameters());
    const auto rows = sample_rows();
    constexpr std::array<std::size_t, 3> canonical = {
        0, 1, 2,
    };
    const auto expected =
        object.residuals(rows, canonical);
    const std::vector<std::vector<double>> permuted = {
        rows[2], rows[0], rows[1],
    };
    constexpr std::array<std::size_t, 3>
        permuted_canonical = {1, 2, 0};
    const auto actual =
        object.residuals(
            permuted, permuted_canonical);
    expect(
        std::bit_cast<std::uint64_t>(actual[0]) ==
                std::bit_cast<std::uint64_t>(
                    expected[2]) &&
            std::bit_cast<std::uint64_t>(actual[1]) ==
                std::bit_cast<std::uint64_t>(
                    expected[0]) &&
            std::bit_cast<std::uint64_t>(actual[2]) ==
                std::bit_cast<std::uint64_t>(
                    expected[1]),
        "canonical scorer is not permutation equivariant");
}

void test_validation_and_semantic_identity() {
    auto rows = sample_rows();
    const auto first =
        std::make_shared<LearnedPriorityBilinear>(
            nonzero_parameters());
    const auto second =
        std::make_shared<LearnedPriorityBilinear>(
            nonzero_parameters());
    LearnedPriorityBilinearParameters changed =
        nonzero_parameters();
    changed.v[0][0] += 0.25;
    const auto third =
        std::make_shared<LearnedPriorityBilinear>(
            changed);
    expect(
        old_school::
            learned_priority_bilinear_equivalent(
                first, second),
        "equal parameters were treated as distinct");
    expect(
        !old_school::
             learned_priority_bilinear_equivalent(
                 first, third) &&
            old_school::
                learned_priority_bilinear_equivalent(
                    nullptr, nullptr) &&
            !old_school::
                 learned_priority_bilinear_equivalent(
                     first, nullptr),
        "semantic policy identity is incorrect");

    constexpr std::array<std::size_t, 3>
        duplicate_order = {0, 0, 2};
    constexpr std::array<std::size_t, 3>
        canonical = {0, 1, 2};
    expect_rejected(
        [&] {
            static_cast<void>(
                first->residuals(
                    rows, duplicate_order));
        },
        "duplicate canonical index was accepted");
    rows[1][0] = 2.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                first->residuals(rows, canonical));
        },
        "mismatched root state prefix was accepted");
    rows = sample_rows();
    rows[0].pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                first->residuals(rows, canonical));
        },
        "short option row was accepted");

    LearnedPriorityBilinearParameters negative_zero;
    negative_zero.v[0][0] = -0.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPriorityBilinear(
                    negative_zero));
        },
        "negative-zero parameter was accepted");
    LearnedPriorityBilinearParameters nonfinite;
    nonfinite.v[0][0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPriorityBilinear(nonfinite));
        },
        "nonfinite parameter was accepted");
}

old_school::GameState live_state(
    old_school::CardId hidden_card) {
    old_school::GameState state;
    state.active_player = 0;
    state.turn_number = 3;
    state.players[0].hand = {
        old_school::CardId::Forest,
        old_school::CardId::GrizzlyBears,
    };
    state.players[0].library = {
        old_school::CardId::Forest,
    };
    state.players[1].hand = {hidden_card};
    state.players[1].library = {
        hidden_card ==
                old_school::CardId::Mountain
            ? old_school::CardId::Forest
            : old_school::CardId::Mountain,
    };
    return state;
}

void test_live_canonical_wrapper_and_hidden_safety() {
    const auto object =
        std::make_shared<LearnedPriorityBilinear>(
            nonzero_parameters());
    const auto first_state =
        live_state(old_school::CardId::Mountain);
    const auto actions =
        old_school::legal_priority_actions(
            first_state, 0, true);
    expect(
        actions.size() >= 2,
        "live fixture did not expose multiple actions");
    const auto first =
        old_school::
            diagnose_learned_priority_bilinear_residual(
                first_state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                actions, object);
    const auto direct =
        object->residuals(
            first.option_rows,
            first.canonical_order);
    expect(
        first.residuals == direct,
        "live wrapper disagrees with pure scorer");

    auto reversed_actions = actions;
    std::reverse(
        reversed_actions.begin(),
        reversed_actions.end());
    const auto reversed =
        old_school::
            diagnose_learned_priority_bilinear_residual(
                first_state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                reversed_actions, object);
    for (std::size_t canonical = 0;
         canonical < actions.size(); ++canonical) {
        const std::size_t reversed_index =
            actions.size() - canonical - 1;
        expect(
            std::bit_cast<std::uint64_t>(
                first.residuals[canonical]) ==
                std::bit_cast<std::uint64_t>(
                    reversed.residuals[
                        reversed_index]),
            "live action permutation changed score bits");
    }

    const auto hidden_state =
        live_state(old_school::CardId::Forest);
    const auto hidden_actions =
        old_school::legal_priority_actions(
            hidden_state, 0, true);
    expect(
        hidden_actions == actions,
        "hidden repartition changed legal actions");
    const auto hidden =
        old_school::
            diagnose_learned_priority_bilinear_residual(
                hidden_state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                hidden_actions, object);
    expect(
        hidden.option_rows == first.option_rows &&
            hidden.residuals == first.residuals,
        "opponent hidden identity reached AQ19 scorer");
}

void test_sparse_empty_control_is_exact_c16_identity() {
    const auto object =
        std::make_shared<LearnedPrioritySparseCross>(
            LearnedPrioritySparseCrossTerms{});
    expect(
        object->empty(),
        "AQ20 empty control did not retain its control shape");
    expect(
        old_school::
            learned_priority_sparse_cross_equivalent(
                object, nullptr),
        "AQ20 empty control is not semantically identical to null C16");
    const auto state =
        live_state(old_school::CardId::Mountain);
    const auto actions =
        old_school::legal_priority_actions(
            state, 0, true);
    const auto diagnostic =
        old_school::
            diagnose_learned_priority_sparse_cross_residual(
                state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                actions, object);
    expect(
        diagnostic.residuals.size() == actions.size() &&
            std::all_of(
                diagnostic.residuals.begin(),
                diagnostic.residuals.end(),
                positive_zero),
        "AQ20 empty control changed a production Priority score");

    std::vector<double> c16_scores;
    c16_scores.reserve(actions.size());
    for (std::size_t index = 0;
         index < actions.size(); ++index) {
        c16_scores.push_back(
            0.25 -
            static_cast<double>(index) / 16.0);
    }
    std::vector<double> replay = c16_scores;
    if (!object->empty()) {
        for (std::size_t index = 0;
             index < replay.size(); ++index) {
            replay[index] += diagnostic.residuals[index];
        }
    }
    expect(
        replay == c16_scores,
        "AQ20 empty production fast path changed C16 scores");
}

void test_sparse_live_production_score_replay() {
    const auto state =
        live_state(old_school::CardId::Mountain);
    const auto actions =
        old_school::legal_priority_actions(
            state, 0, true);
    const auto empty =
        std::make_shared<LearnedPrioritySparseCross>(
            LearnedPrioritySparseCrossTerms{});
    const auto projection =
        old_school::
            diagnose_learned_priority_sparse_cross_residual(
                state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                actions, empty);
    const LearnedPrioritySparseCrossTerms terms =
        sparse_terms_for_rows(
            projection.option_rows,
            projection.canonical_order);
    const auto object =
        std::make_shared<LearnedPrioritySparseCross>(
            terms);
    const auto production =
        old_school::
            diagnose_learned_priority_sparse_cross_residual(
                state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                actions, object);
    const auto direct =
        object->residuals(
            production.option_rows,
            production.canonical_order);
    const auto reference =
        reference_sparse_cross_residuals(
            terms, production.option_rows,
            production.canonical_order);
    expect(
        production.residuals == direct,
        "AQ20 production wrapper did not replay the immutable scorer");
    expect(
        std::any_of(
            production.residuals.begin(),
            production.residuals.end(),
            [](double value) { return value != 0.0; }),
        "AQ20 production replay fixture produced no treatment signal");
    for (std::size_t index = 0;
         index < reference.size(); ++index) {
        expect_near(
            production.residuals[index],
            reference[index], 1.0e-15,
            "AQ20 production score disagrees with declared sparse cross");
    }

    const auto hidden_state =
        live_state(old_school::CardId::Forest);
    const auto hidden_actions =
        old_school::legal_priority_actions(
            hidden_state, 0, true);
    const auto hidden =
        old_school::
            diagnose_learned_priority_sparse_cross_residual(
                hidden_state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                hidden_actions, object);
    expect(
        hidden_actions == actions &&
            hidden.option_rows == production.option_rows &&
            hidden.residuals == production.residuals,
        "opponent hidden identity reached AQ20 production scoring");
}

void test_sparse_symmetric_continuation_propagation() {
    const auto state =
        live_state(old_school::CardId::Mountain);
    const auto actions =
        old_school::legal_priority_actions(
            state, 0, true);
    const auto empty =
        std::make_shared<LearnedPrioritySparseCross>(
            LearnedPrioritySparseCrossTerms{});
    const auto projection =
        old_school::
            diagnose_learned_priority_sparse_cross_residual(
                state, 0, true,
                old_school::TurnPhase::FirstMain, 0,
                actions, empty);
    const auto object =
        std::make_shared<LearnedPrioritySparseCross>(
            sparse_terms_for_rows(
                projection.option_rows,
                projection.canonical_order));
    const old_school::BotConfig root{
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .rollouts_per_action =
            old_school::
                kLearnedPrioritySparseCrossWorlds,
        .value_priority_sparse_cross = object,
    };
    const auto diagnostic =
        old_school::
            diagnose_learned_priority_sparse_cross_continuation(
                root);
    expect(
        diagnostic.first_seat_has_root_object &&
            diagnostic.second_seat_has_root_object &&
            diagnostic.seats_share_object_identity &&
            diagnostic.seats_are_semantically_equivalent,
        "production continuation builder did not share the "
        "immutable AQ20 object");
    expect(
        diagnostic.rollout_counts ==
                std::array<std::size_t, 2>{0, 0} &&
            diagnostic.variants ==
                std::array<old_school::LearnedVariant, 2>{
                    old_school::LearnedVariant::
                        ValueSearchChampion,
                    old_school::LearnedVariant::
                        ValueSearchChampion,
                } &&
            diagnostic.exploration_rates ==
                std::array<double, 2>{0.0, 0.0},
        "production continuation builder changed the "
        "AQ20 depth-zero mirror recipe");
}

void test_sparse_treatment_composition_is_rejected() {
    const auto sparse =
        std::make_shared<LearnedPrioritySparseCross>(
            LearnedPrioritySparseCrossTerms{});
    const auto bilinear =
        std::make_shared<LearnedPriorityBilinear>(
            nonzero_parameters());
    const auto rejected =
        [&](old_school::BotConfig root,
            std::string_view message) {
            expect_rejected(
                [&] {
                    static_cast<void>(
                        old_school::
                            diagnose_learned_priority_sparse_cross_continuation(
                                root));
                },
                message);
        };
    rejected(
        {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action =
                old_school::
                    kLearnedPrioritySparseCrossWorlds,
            .value_priority_bilinear = bilinear,
            .value_priority_sparse_cross = sparse,
        },
        "AQ19 and AQ20 were composed");
    rejected(
        {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action =
                old_school::
                    kLearnedPrioritySparseCrossWorlds,
            .value_priority_residual_weight = 0.10,
            .value_priority_sparse_cross = sparse,
        },
        "AQ20 and the legacy Priority residual were composed");
    rejected(
        {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action =
                old_school::
                    kLearnedPrioritySparseCrossWorlds,
            .value_priority_sparse_cross = sparse,
            .value_pass_dominance = true,
        },
        "AQ20 and Pass dominance were composed");
    rejected(
        {
            .kind = old_school::BotKind::Learned,
            .learned_variant =
                old_school::LearnedVariant::
                    ValueSearchChampion,
            .rollouts_per_action = 2,
            .value_priority_sparse_cross = sparse,
        },
        "AQ20 accepted a non-C16 rollout recipe");
}

void test_bot_shape_rejects_non_c16_recipes() {
    const auto object =
        std::make_shared<LearnedPriorityBilinear>(
            nonzero_parameters());
    const auto rejected =
        [&](old_school::BotConfig challenger) {
            expect_rejected(
                [&] {
                    static_cast<void>(
                        old_school::run_bot_benchmark(
                            1, 7, challenger,
                            old_school::BotConfig{
                                .kind =
                                    old_school::BotKind::
                                        Random,
                            }));
                },
                "invalid AQ19 BotConfig shape was accepted");
        };
    rejected({
        .kind = old_school::BotKind::Random,
        .value_priority_bilinear = object,
    });
    rejected({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .rollouts_per_action = 2,
        .value_priority_bilinear = object,
    });
    rejected({
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .rollouts_per_action =
            old_school::kLearnedPriorityBilinearWorlds,
        .value_priority_residual_weight = 0.10,
        .value_priority_bilinear = object,
    });
}

void test_symmetric_continuation_propagation() {
    const auto object =
        std::make_shared<LearnedPriorityBilinear>(
            nonzero_parameters());
    const old_school::BotConfig root{
        .kind = old_school::BotKind::Learned,
        .learned_variant =
            old_school::LearnedVariant::
                ValueSearchChampion,
        .rollouts_per_action =
            old_school::kLearnedPriorityBilinearWorlds,
        .value_priority_bilinear = object,
    };
    const auto diagnostic =
        old_school::
            diagnose_learned_priority_bilinear_continuation(
                root);
    expect(
        diagnostic.first_seat_has_root_object &&
            diagnostic.second_seat_has_root_object &&
            diagnostic.seats_share_object_identity &&
            diagnostic.seats_are_semantically_equivalent,
        "production continuation builder did not share the "
        "immutable AQ19 object");
    expect(
        diagnostic.rollout_counts ==
                std::array<std::size_t, 2>{0, 0} &&
            diagnostic.variants ==
                std::array<old_school::LearnedVariant, 2>{
                    old_school::LearnedVariant::
                        ValueSearchChampion,
                    old_school::LearnedVariant::
                        ValueSearchChampion,
                } &&
            diagnostic.exploration_rates ==
                std::array<double, 2>{0.0, 0.0},
        "production continuation builder changed the "
        "depth-zero mirror recipe");
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    diagnose_learned_priority_bilinear_continuation(
                        old_school::BotConfig{
                            .kind =
                                old_school::BotKind::
                                    Learned,
                            .learned_variant =
                                old_school::
                                    LearnedVariant::
                                        ValueSearchChampion,
                            .rollouts_per_action = 0,
                            .value_priority_bilinear =
                                object,
                        }));
        },
        "depth-zero input was accepted as a real AQ19 root");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "fixed U0 and shape",
        test_fixed_u0_and_shape);
    runner.run(
        "zero parameters are exact positive zero",
        test_zero_parameters_are_exact_positive_zero);
    runner.run(
        "forward matches reference and is bounded",
        test_forward_matches_reference_and_is_bounded);
    runner.run(
        "permutation equivariance",
        test_permutation_equivariance);
    runner.run(
        "validation and semantic identity",
        test_validation_and_semantic_identity);
    runner.run(
        "live canonical wrapper and hidden safety",
        test_live_canonical_wrapper_and_hidden_safety);
    runner.run(
        "AQ20 empty control is exact C16 identity",
        test_sparse_empty_control_is_exact_c16_identity);
    runner.run(
        "AQ20 production score replay",
        test_sparse_live_production_score_replay);
    runner.run(
        "AQ20 symmetric continuation propagation",
        test_sparse_symmetric_continuation_propagation);
    runner.run(
        "AQ20 treatment composition is rejected",
        test_sparse_treatment_composition_is_rejected);
    runner.run(
        "BotConfig rejects non-C16 recipes",
        test_bot_shape_rejects_non_c16_recipes);
    runner.run(
        "symmetric continuation propagation",
        test_symmetric_continuation_propagation);
    return runner.finish();
}
