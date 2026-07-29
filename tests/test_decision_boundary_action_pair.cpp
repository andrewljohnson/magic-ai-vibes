#include "old_school/decision_boundary_action_pair.hpp"
#include "old_school/action_q_nested_actor_distill.hpp"

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

namespace pair =
    old_school::decision_boundary_action_pair;
namespace direct =
    old_school::decision_boundary_rank_direct;
namespace g1 =
    old_school::action_q_nested_actor_distill;

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

bool bit_equal(double left, double right) {
    return
        std::bit_cast<std::uint64_t>(left) ==
        std::bit_cast<std::uint64_t>(right);
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

static_assert(!HasStateMember<pair::Root>);
static_assert(!HasOpponentHandMember<pair::Root>);
static_assert(
    !HasStateMember<pair::PrecomputedScoreRoot>);
static_assert(
    !HasOpponentHandMember<pair::PrecomputedScoreRoot>);
static_assert(pair::kPolicyFeatureCount == 893);
static_assert(pair::kHiddenCount == 32);
static_assert(pair::kFoldCount == 4);

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

double manual_pair_bce(
    const std::vector<double>& teacher,
    const std::vector<double>& candidate) {
    expect(
        teacher.size() == candidate.size() &&
            teacher.size() >= 2,
        "manual pair fixture is malformed");
    double total_cost = 0.0;
    for (std::size_t first = 0;
         first < teacher.size(); ++first) {
        for (std::size_t second = first + 1;
             second < teacher.size(); ++second) {
            total_cost +=
                std::abs(
                    teacher[first] - teacher[second]);
        }
    }
    if (total_cost == 0.0) {
        return 0.0;
    }
    double result = 0.0;
    for (std::size_t first = 0;
         first < teacher.size(); ++first) {
        for (std::size_t second = first + 1;
             second < teacher.size(); ++second) {
            const double difference =
                teacher[first] - teacher[second];
            if (difference == 0.0) {
                continue;
            }
            const double target =
                sigmoid(
                    difference /
                    pair::kPairTemperature);
            const double candidate_logit =
                (candidate[first] -
                 candidate[second]) /
                pair::kPairTemperature;
            result +=
                std::abs(difference) / total_cost *
                bce_from_logit(
                    target, candidate_logit);
        }
    }
    return result;
}

std::vector<double> mixed_softmax(
    const std::vector<double>& values) {
    const double maximum =
        *std::max_element(
            values.begin(), values.end());
    std::vector<double> result(values.size());
    double total = 0.0;
    for (std::size_t index = 0;
         index < values.size(); ++index) {
        result[index] =
            std::exp(
                (values[index] - maximum) /
                direct::kListwiseTemperature);
        total += result[index];
    }
    const double uniform =
        (1.0 - direct::kListwiseMix) /
        static_cast<double>(values.size());
    for (double& value : result) {
        value =
            direct::kListwiseMix * value / total +
            uniform;
    }
    return result;
}

double manual_listwise_ce(
    const std::vector<double>& teacher,
    const std::vector<double>& candidate) {
    const std::vector<double> teacher_distribution =
        mixed_softmax(teacher);
    const std::vector<double> candidate_distribution =
        mixed_softmax(candidate);
    double result = 0.0;
    for (std::size_t index = 0;
         index < teacher.size(); ++index) {
        result -=
            teacher_distribution[index] *
            std::log(candidate_distribution[index]);
    }
    return result;
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

pair::Root make_root(
    std::string id, old_school::DeckId deck,
    std::size_t schedule_index, std::size_t actor,
    const std::vector<double>& teacher,
    const std::vector<double>& parent,
    std::vector<pair::Hidden> hidden = {}) {
    expect(
        teacher.size() == parent.size() &&
            teacher.size() >= 2,
        "synthetic root score width drifted");
    pair::Root root{
        .ranking =
            direct::RankRoot{
                .stable_root_id = std::move(id),
                .deck = deck,
            },
        .schedule_index = schedule_index,
        .actor = actor,
    };
    for (std::size_t action = 0;
         action < teacher.size(); ++action) {
        root.ranking.actions.push_back(
            make_action(
                teacher[action], parent[action]));
        root.options.emplace_back(
            pair::kPolicyFeatureCount, 0.0);
    }
    if (hidden.empty()) {
        hidden.resize(teacher.size());
    }
    root.hidden = std::move(hidden);
    return root;
}

pair::Dataset replicated_dataset(
    const std::vector<double>& teacher,
    const std::vector<double>& parent,
    const std::vector<pair::Hidden>& hidden = {},
    std::size_t copies = 1) {
    std::vector<pair::Root> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < copies; ++copy) {
            roots.push_back(
                make_root(
                    "root-" + std::to_string(deck) +
                        "-" + std::to_string(copy),
                    static_cast<old_school::DeckId>(
                        deck),
                    deck * copies + copy,
                    copy % 2,
                    teacher, parent, hidden));
        }
    }
    return pair::testing::make_dataset(
        std::move(roots));
}

pair::PrecomputedScoreDataset precompute_scores(
    const pair::Dataset& dataset) {
    pair::PrecomputedScoreDataset result{
        .roots_by_deck = dataset.roots_by_deck,
    };
    for (const pair::Root& root : dataset.roots) {
        pair::PrecomputedScoreRoot scored{
            .deck = root.ranking.deck,
        };
        for (const direct::RankAction& action :
             root.ranking.actions) {
            double base = 0.0;
            double teacher = 0.0;
            std::vector<double> samples;
            for (const direct::RankCell& cell :
                 action.worlds) {
                teacher += cell.teacher_target;
                base +=
                    cell.terminal_before_boundary
                    ? cell.teacher_target
                    : std::accumulate(
                          cell.parent_leaf_values.begin(),
                          cell.parent_leaf_values.end(), 0.0) /
                          static_cast<double>(
                              direct::kLeafCount);
                samples.push_back(cell.teacher_target);
            }
            scored.base_aggregate_scores.push_back(
                base /
                static_cast<double>(
                    action.worlds.size()));
            scored.teacher_aggregate_scores.push_back(
                teacher /
                static_cast<double>(
                    action.worlds.size()));
            scored.common_world_teacher_samples.push_back(
                std::move(samples));
        }
        result.roots.push_back(std::move(scored));
    }
    pair::validate_precomputed_score_dataset(result);
    return result;
}

pair::Dataset learning_dataset() {
    std::vector<pair::Root> roots;
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        for (std::size_t copy = 0;
             copy < 2; ++copy) {
            std::vector<pair::Hidden> hidden(2);
            hidden[0][deck] = 1.0;
            hidden[1][deck] = -1.0;
            roots.push_back(
                make_root(
                    "learn-" + std::to_string(deck) +
                        "-" + std::to_string(copy),
                    static_cast<old_school::DeckId>(
                        deck),
                    deck * 2 + copy, copy,
                    {0.9, 0.1}, {0.495, 0.505},
                    std::move(hidden)));
        }
    }
    return pair::testing::make_dataset(
        std::move(roots));
}

std::shared_ptr<const old_school::LearnedModel>
test_model() {
    static const auto model =
        old_school::train_learned_model(
            1, 0xDBC4A11CEULL);
    return model;
}

pair::Hidden hidden_activation(
    const std::vector<double>& option,
    const old_school::LearnedPriorityHeadParameters&
        parameters) {
    pair::Hidden result{};
    for (std::size_t hidden = 0;
         hidden < pair::kHiddenCount; ++hidden) {
        double value = parameters.hidden_bias[hidden];
        for (std::size_t feature = 0;
             feature < option.size(); ++feature) {
            value +=
                parameters.input_hidden[hidden][feature] *
                option[feature];
        }
        result[hidden] = std::tanh(value);
    }
    return result;
}

pair::Dataset fold_dataset(
    std::shared_ptr<const old_school::LearnedModel>
        parent) {
    const auto parameters =
        old_school::learned_priority_head_parameters(
            parent);
    std::vector<pair::Root> roots;
    roots.reserve(80);
    for (std::size_t fold = 0;
         fold < pair::kFoldCount; ++fold) {
        for (std::size_t game = 0;
             game < 10; ++game) {
            const std::size_t schedule =
                fold + pair::kFoldCount * game;
            for (std::size_t actor = 0;
                 actor < 2; ++actor) {
                const std::size_t deck =
                    (2 * game + actor) %
                    old_school::kDeckCount;
                pair::Root root =
                    make_root(
                        "fold-" +
                            std::to_string(schedule) +
                            "-" +
                            std::to_string(actor),
                        static_cast<old_school::DeckId>(
                            deck),
                        schedule, actor,
                        {0.9, 0.1}, {0.45, 0.55});
                root.options[0][0] = 1.0;
                root.options[1][0] = -1.0;
                root.hidden[0] =
                    hidden_activation(
                        root.options[0],
                        parameters);
                root.hidden[1] =
                    hidden_activation(
                        root.options[1],
                        parameters);
                roots.push_back(std::move(root));
            }
        }
    }
    return pair::testing::make_dataset(
        std::move(roots));
}

void test_fixed_recipe_and_shape() {
    const pair::OptimizerConfig recipe;
    expect(
        recipe.fit_tag == 202607291601ULL &&
            pair::kSelectorSeed ==
                202607291611ULL &&
            recipe.steps == 256 &&
            recipe.learning_rate == 0.001 &&
            recipe.beta_one == 0.9 &&
            recipe.beta_two == 0.999 &&
            recipe.epsilon == 1.0e-8 &&
            recipe.residual_weight == 0.10 &&
            recipe.pair_temperature == 0.10 &&
            recipe.l2_tether == 0.10 &&
            recipe.global_gradient_norm_clip == 5.0,
        "fixed DBC4 recipe drifted");
    const pair::Dataset dataset =
        learning_dataset();
    expect(
        dataset.roots.size() == 10,
        "synthetic root count drifted");
    for (const std::size_t count :
         dataset.roots_by_deck) {
        expect(
            count == 2,
            "synthetic deck balance drifted");
    }
}

void test_manual_two_action_objective_and_gradient() {
    std::vector<pair::Hidden> hidden(2);
    hidden[0][0] = 0.5;
    hidden[1][0] = -0.25;
    const pair::Dataset dataset =
        replicated_dataset(
            {0.8, 0.2}, {0.4, 0.6}, hidden);
    pair::Delta delta{};
    delta[0] = 0.4;
    const pair::testing::ObjectiveProbe probe =
        pair::testing::objective_probe(
            dataset, delta);

    const double mean_logit =
        (0.5 * delta[0] -
         0.25 * delta[0]) /
        2.0;
    const double first_centered =
        0.5 * delta[0] - mean_logit;
    const double second_centered =
        -0.25 * delta[0] - mean_logit;
    const std::vector<double> candidate{
        0.4 +
            pair::kResidualWeight *
                std::tanh(first_centered),
        0.6 +
            pair::kResidualWeight *
                std::tanh(second_centered),
    };
    const double expected_pair =
        manual_pair_bce({0.8, 0.2}, candidate);
    const double expected_objective =
        expected_pair +
        0.5 * pair::kL2Tether *
            delta[0] * delta[0];
    expect_near(
        probe.objective, expected_objective,
        1.0e-14,
        "two-action pair objective drifted");

    const double target =
        sigmoid(
            (0.8 - 0.2) /
            pair::kPairTemperature);
    const double prediction =
        sigmoid(
            (candidate[0] - candidate[1]) /
            pair::kPairTemperature);
    const double mean_hidden =
        (0.5 - 0.25) / 2.0;
    const double first_derivative =
        pair::kResidualWeight *
        (1.0 -
         std::tanh(first_centered) *
             std::tanh(first_centered)) *
        (0.5 - mean_hidden);
    const double second_derivative =
        pair::kResidualWeight *
        (1.0 -
         std::tanh(second_centered) *
             std::tanh(second_centered)) *
        (-0.25 - mean_hidden);
    const double expected_gradient =
        (prediction - target) /
            pair::kPairTemperature *
            (first_derivative -
             second_derivative) +
        pair::kL2Tether * delta[0];
    expect_near(
        probe.gradient[0], expected_gradient,
        1.0e-13,
        "centered pair gradient drifted");

    constexpr double epsilon = 1.0e-6;
    pair::Delta plus = delta;
    pair::Delta minus = delta;
    plus[0] += epsilon;
    minus[0] -= epsilon;
    const double numerical =
        (pair::testing::objective_probe(
             dataset, plus)
             .objective -
         pair::testing::objective_probe(
             dataset, minus)
             .objective) /
        (2.0 * epsilon);
    expect_near(
        numerical, probe.gradient[0], 1.0e-9,
        "nonzero centered gradient failed finite difference");
}

void test_manual_three_action_pair_weighting() {
    const std::vector<double> teacher{
        0.9, 0.5, 0.2};
    const std::vector<double> parent{
        0.2, 0.6, 0.4};
    const pair::Dataset dataset =
        replicated_dataset(teacher, parent);
    const pair::Metrics metrics =
        pair::evaluate(dataset, pair::Delta{});
    const double expected =
        manual_pair_bce(teacher, parent);
    expect_near(
        metrics.pairs.equal_deck_pair_bce,
        expected, 1.0e-14,
        "three-action cost-normalized pair BCE drifted");
    expect(
        metrics.pairs.roots == 5 &&
            metrics.pairs.unordered_pairs == 15 &&
            metrics.pairs.eligible_pairs == 15 &&
            metrics.pairs.all_tied_roots == 0,
        "three-action pair census drifted");
    for (const auto& deck : metrics.pairs.decks) {
        expect_near(
            deck.pair_bce, expected, 1.0e-14,
            "per-deck three-action BCE drifted");
    }
}

void test_tied_roots_and_outer_weighting() {
    const pair::Dataset all_tied =
        replicated_dataset(
            {0.5, 0.5, 0.5},
            {0.2, 0.5, 0.8});
    const pair::Metrics tied_metrics =
        pair::evaluate(
            all_tied, pair::Delta{});
    const pair::testing::ObjectiveProbe tied_objective =
        pair::testing::objective_probe(
            all_tied, pair::Delta{});
    expect(
        tied_metrics.pairs.roots == 5 &&
            tied_metrics.pairs.all_tied_roots == 5 &&
            tied_metrics.pairs.unordered_pairs == 15 &&
            tied_metrics.pairs.eligible_pairs == 0,
        "all-tied pair census drifted");
    expect(
        std::bit_cast<std::uint64_t>(
            tied_metrics.pairs.equal_deck_pair_bce) ==
                UINT64_C(0) &&
            std::bit_cast<std::uint64_t>(
                tied_objective.objective) ==
                UINT64_C(0),
        "all-tied root did not report exact positive zero");
    for (const double gradient :
         tied_objective.gradient) {
        expect(
            std::bit_cast<std::uint64_t>(gradient) ==
                UINT64_C(0),
            "all-tied zero-delta gradient was not +0");
    }

    std::vector<pair::Root> roots;
    std::array<double, old_school::kDeckCount>
        active_bce{};
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        const std::vector<double> active_parent{
            0.4 + 0.025 *
                      static_cast<double>(deck),
            0.6 - 0.025 *
                      static_cast<double>(deck)};
        active_bce[deck] =
            manual_pair_bce(
                {0.8, 0.2}, active_parent);
        roots.push_back(
            make_root(
                "active-" + std::to_string(deck),
                static_cast<old_school::DeckId>(
                    deck),
                2 * deck, 0,
                {0.8, 0.2}, active_parent));
        roots.push_back(
            make_root(
                "tied-" + std::to_string(deck),
                static_cast<old_school::DeckId>(
                    deck),
                2 * deck + 1, 1,
                {0.5, 0.5, 0.5},
                {0.2, 0.5, 0.8}));
    }
    const pair::Metrics weighted =
        pair::evaluate(
            pair::testing::make_dataset(
                std::move(roots)),
            pair::Delta{});
    double expected_equal_deck = 0.0;
    for (const double value : active_bce) {
        expected_equal_deck +=
            value /
            (2.0 *
             static_cast<double>(
                 old_school::kDeckCount));
    }
    expect_near(
        weighted.pairs.equal_deck_pair_bce,
        expected_equal_deck, 1.0e-14,
        "all-tied root left the equal-root denominator");
    expect(
        weighted.pairs.roots == 10 &&
            weighted.pairs.all_tied_roots == 5 &&
            weighted.pairs.unordered_pairs == 20 &&
            weighted.pairs.eligible_pairs == 5,
        "weighted pair census drifted");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect_near(
            weighted.pairs.decks[deck].pair_bce,
            active_bce[deck] / 2.0, 1.0e-14,
            "equal-root reduction drifted");
    }
}

void test_ranking_metric_tie_semantics() {
    const std::vector<double> teacher{
        0.9, 0.9, 0.2};
    const std::vector<double> parent{
        0.8, 0.1, 0.8};
    const pair::Metrics metrics =
        pair::evaluate(
            replicated_dataset(teacher, parent),
            pair::Delta{});
    expect_near(
        metrics.ranking
            .equal_deck_listwise_cross_entropy,
        manual_listwise_ce(teacher, parent),
        1.0e-14,
        "listwise diagnostic drifted");
    expect_near(
        metrics.ranking
            .equal_deck_top_one_expected_agreement,
        0.5, 1.0e-15,
        "exact-max top-one tie semantics drifted");
    expect_near(
        metrics.ranking.equal_deck_mean_regret,
        0.35, 1.0e-15,
        "exact-max regret tie semantics drifted");
    expect(
        metrics.ranking.stable_pairs == 10,
        "stable pair census drifted");
    expect_near(
        metrics.ranking
            .equal_deck_stable_pair_agreement,
        0.25, 1.0e-15,
        "stable-pair tie credit drifted");
    for (const auto& deck :
         metrics.ranking.decks) {
        expect(
            deck.stable_pairs == 2,
            "per-deck stable pair count drifted");
        expect_near(
            deck.top_one_expected_agreement,
            0.5, 1.0e-15,
            "per-deck top-one tie semantics drifted");
        expect_near(
            deck.mean_regret, 0.35, 1.0e-15,
            "per-deck regret semantics drifted");
        expect_near(
            deck.stable_pair_agreement,
            0.25, 1.0e-15,
            "per-deck stable-pair semantics drifted");
    }
}

void test_precomputed_score_metric_equivalence() {
    const pair::Dataset source =
        replicated_dataset(
            {0.9, 0.5, 0.2},
            {0.2, 0.6, 0.4});
    const pair::PrecomputedScoreDataset precomputed =
        precompute_scores(source);
    const std::vector<std::vector<double>> residuals(
        source.roots.size(),
        {0.0125, -0.00625, 0.003125});
    const pair::Metrics legacy =
        pair::evaluate_residuals(
            source, residuals);
    const pair::Metrics scored =
        pair::evaluate_precomputed_residuals(
            precomputed, residuals);

    expect(
        legacy.pairs == scored.pairs &&
            bit_equal(
                legacy.pairs.equal_deck_pair_bce,
                scored.pairs.equal_deck_pair_bce),
        "precomputed pair metrics differed from DBC4");
    const auto expect_ranking_equal =
        [](const direct::Metrics& left,
           const direct::Metrics& right,
           std::string_view message) {
            bool equal =
                left.roots == right.roots &&
                left.stable_pairs ==
                    right.stable_pairs &&
                bit_equal(
                    left.equal_deck_listwise_cross_entropy,
                    right.equal_deck_listwise_cross_entropy) &&
                bit_equal(
                    left.equal_deck_top_one_expected_agreement,
                    right.equal_deck_top_one_expected_agreement) &&
                bit_equal(
                    left.equal_deck_stable_pair_agreement,
                    right.equal_deck_stable_pair_agreement) &&
                bit_equal(
                    left.equal_deck_mean_regret,
                    right.equal_deck_mean_regret);
            for (std::size_t deck = 0;
                 deck < old_school::kDeckCount; ++deck) {
                const auto& left_deck =
                    left.decks[deck];
                const auto& right_deck =
                    right.decks[deck];
                equal =
                    equal &&
                    left_deck.deck ==
                        right_deck.deck &&
                    left_deck.roots ==
                        right_deck.roots &&
                    left_deck.stable_pairs ==
                        right_deck.stable_pairs &&
                    bit_equal(
                        left_deck.listwise_cross_entropy,
                        right_deck.listwise_cross_entropy) &&
                    bit_equal(
                        left_deck.top_one_expected_agreement,
                        right_deck.top_one_expected_agreement) &&
                    bit_equal(
                        left_deck.stable_pair_agreement,
                        right_deck.stable_pair_agreement) &&
                    bit_equal(
                        left_deck.mean_regret,
                        right_deck.mean_regret);
            }
            expect(equal, message);
        };
    expect_ranking_equal(
        legacy.ranking, scored.ranking,
        "precomputed ranking metrics differed from DBC4");

    pair::PrecomputedScoreDataset changed_samples =
        precomputed;
    for (pair::PrecomputedScoreRoot& root :
         changed_samples.roots) {
        for (auto& action_samples :
             root.common_world_teacher_samples) {
            std::fill(
                action_samples.begin(),
                action_samples.end(), 0.25);
        }
    }
    const pair::Metrics sample_only =
        pair::evaluate_precomputed_residuals(
            changed_samples, residuals);
    expect(
        sample_only.pairs == scored.pairs,
        "teacher samples were used to recreate aggregates");
    expect_ranking_equal(
        sample_only.ranking, scored.ranking,
        "teacher samples changed non-uncertainty metrics");

    pair::PrecomputedScoreDataset changed_teacher =
        precomputed;
    changed_teacher.roots.front()
        .teacher_aggregate_scores.front() = 0.7;
    const pair::Metrics teacher_result =
        pair::evaluate_precomputed_residuals(
            changed_teacher, residuals);
    expect(
        !bit_equal(
            teacher_result.pairs
                .equal_deck_pair_bce,
            scored.pairs.equal_deck_pair_bce) &&
            !bit_equal(
                teacher_result.ranking
                    .equal_deck_listwise_cross_entropy,
                scored.ranking
                    .equal_deck_listwise_cross_entropy),
        "authoritative teacher aggregate was ignored");

    pair::PrecomputedScoreDataset changed_base =
        precomputed;
    changed_base.roots.front()
        .base_aggregate_scores.front() = 0.7;
    const pair::Metrics base_result =
        pair::evaluate_precomputed_residuals(
            changed_base, residuals);
    expect(
        !bit_equal(
            base_result.pairs.equal_deck_pair_bce,
            scored.pairs.equal_deck_pair_bce) &&
            !bit_equal(
                base_result.ranking
                    .equal_deck_listwise_cross_entropy,
                scored.ranking
                    .equal_deck_listwise_cross_entropy),
        "authoritative base aggregate was ignored");

    const pair::Dataset stable_source =
        replicated_dataset(
            {0.52, 0.48},
            {0.49, 0.51});
    pair::PrecomputedScoreDataset noisy =
        precompute_scores(stable_source);
    const std::vector<std::vector<double>>
        stable_residuals(
            stable_source.roots.size(),
            std::vector<double>(2, 0.0));
    const pair::Metrics stable =
        pair::evaluate_precomputed_residuals(
            noisy, stable_residuals);
    for (pair::PrecomputedScoreRoot& root :
         noisy.roots) {
        root.common_world_teacher_samples[0] =
            {1.0, 0.0};
        root.common_world_teacher_samples[1] =
            {0.0, 1.0};
    }
    const pair::Metrics uncertain =
        pair::evaluate_precomputed_residuals(
            noisy, stable_residuals);
    expect(
        stable.ranking.stable_pairs ==
                old_school::kDeckCount &&
            uncertain.ranking.stable_pairs == 0 &&
            stable.pairs == uncertain.pairs &&
            bit_equal(
                stable.ranking
                    .equal_deck_listwise_cross_entropy,
                uncertain.ranking
                    .equal_deck_listwise_cross_entropy) &&
            bit_equal(
                stable.ranking.equal_deck_mean_regret,
                uncertain.ranking.equal_deck_mean_regret),
        "common-world samples did not exclusively control "
        "stable-pair uncertainty");
}

void test_adam_is_bit_deterministic() {
    const pair::Dataset dataset =
        learning_dataset();
    const pair::OptimizerReport first =
        pair::optimize(dataset);
    const pair::OptimizerReport repeated =
        pair::optimize(dataset);
    expect(
        pair::optimizer_bit_identical(
            first, repeated),
        "repeated DBC4 fit was not bit-identical");
    pair::OptimizerReport signed_zero = first;
    expect(
        std::bit_cast<std::uint64_t>(
            signed_zero.delta.back()) ==
            UINT64_C(0),
        "unused optimizer coordinate was not +0");
    signed_zero.delta.back() = -0.0;
    expect(
        !pair::optimizer_bit_identical(
            first, signed_zero),
        "optimizer replay ignored +0/-0 drift");
    expect(
        first.completed_steps ==
                pair::kAdamSteps &&
            first.final_objective <
                first.initial_objective &&
            first.after.pairs.equal_deck_pair_bce <
                first.before.pairs
                    .equal_deck_pair_bce &&
            first.after.ranking
                    .equal_deck_mean_regret <
                first.before.ranking
                    .equal_deck_mean_regret &&
            first.after.ranking
                    .equal_deck_top_one_expected_agreement >
                first.before.ranking
                    .equal_deck_top_one_expected_agreement &&
            std::isfinite(first.delta_l2_norm) &&
            first.delta_l2_norm > 0.0 &&
            std::isfinite(
                first.final_gradient_l2_norm),
        "synthetic pairwise Adam fit did not improve");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            first.delta[deck] > 0.0 &&
                first.after.ranking.decks[deck]
                        .mean_regret <
                    first.before.ranking.decks[deck]
                        .mean_regret,
            "pairwise fit missed a synthetic deck");
    }
}

void test_model_isolation_and_signed_zero() {
    const auto parent = test_model();
    pair::Delta delta{};
    delta[0] = 0.125;
    delta[7] = -0.0625;
    const pair::ModelIsolationReport isolation =
        pair::apply_delta(parent, delta);
    expect(
        isolation.passed() &&
            isolation.parent_positive_zero &&
            isolation.parent_immutable &&
            isolation
                .repeated_application_bit_identical &&
            isolation
                .only_priority_hidden_output_changed &&
            isolation.exact_delta &&
            isolation.changed_coordinates == 2 &&
            isolation.parent_fingerprint_before ==
                isolation.parent_fingerprint_after &&
            isolation.candidate_fingerprint ==
                isolation
                    .repeated_candidate_fingerprint,
        "Priority-readout isolation drifted");

    const pair::ModelIsolationReport zero =
        pair::apply_delta(parent, pair::Delta{});
    expect(
        zero.parent_positive_zero &&
            zero.changed_coordinates == 0 &&
            zero.safe_for_evaluation() &&
            !zero.passed(),
        "unchanged positive-zero readout became a candidate");
    const pair::Dataset zero_train =
        fold_dataset(parent);
    const pair::Corpus zero_corpus =
        pair::testing::make_corpus(
            zero_train, fold_dataset(parent),
            old_school::
                learned_model_component_fingerprints(
                    parent));
    const pair::ExactEvaluationReport zero_exact =
        pair::evaluate_exact(
            zero_corpus, parent, zero, pair::Delta{});
    expect(
        zero_exact.parent_train ==
                zero_exact.candidate_train &&
            zero_exact.parent_dev ==
                zero_exact.candidate_dev,
        "zero-coordinate exact evaluation changed scores");
    const pair::OfflineGate zero_gate =
        pair::evaluate_offline_gate({
            .parent_train = zero_exact.parent_train,
            .candidate_train =
                zero_exact.candidate_train,
            .parent_oof = zero_exact.parent_train,
            .candidate_oof =
                zero_exact.candidate_train,
            .parent_dev = zero_exact.parent_dev,
            .candidate_dev =
                zero_exact.candidate_dev,
            .cache_identity_exact = true,
            .pair_census_exact = true,
            .optimizer_recipe_exact = true,
            .grouped_folds_exact = true,
            .repeat_fits_bit_identical = true,
            .parameter_replay_bit_identical = true,
            .zero_delta_equivalent = true,
            .actual_model_agreement = true,
            .parent_immutable = true,
            .model_isolation_passed = zero.passed(),
            .successor_predictions_bit_identical =
                true,
            .successor_metrics_bit_identical = true,
        });
    expect(
        !zero_gate.passed() &&
            !zero_gate.invariants_passed,
        "zero-coordinate candidate opened conditional gates");

    pair::Delta negative_zero{};
    negative_zero[0] = -0.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::apply_delta(
                    parent, negative_zero));
        },
        "negative-zero delta was accepted");

    const auto original =
        old_school::learned_priority_head_parameters(
            parent);
    const auto require_negative_zero_rejected =
        [&](auto mutate, std::string_view message) {
            auto parameters = original;
            mutate(parameters);
            const auto malformed =
                old_school::
                    with_learned_priority_head_parameters(
                        parent, parameters);
            expect_rejected(
                [&] {
                    static_cast<void>(
                        pair::apply_delta(
                            malformed, delta));
                },
                message);
        };
    require_negative_zero_rejected(
        [](auto& parameters) {
            parameters.hidden_bias[0] = -0.0;
        },
        "negative-zero hidden bias was accepted");
    require_negative_zero_rejected(
        [](auto& parameters) {
            parameters.hidden_output[0] = -0.0;
        },
        "negative-zero hidden output was accepted");
    require_negative_zero_rejected(
        [](auto& parameters) {
            parameters.direct[0] = -0.0;
        },
        "negative-zero direct path was accepted");
    require_negative_zero_rejected(
        [](auto& parameters) {
            parameters.output_bias = -0.0;
        },
        "negative-zero output bias was accepted");
}

void test_selector_contract_and_exact_model_scores() {
    const auto parent = test_model();
    pair::Delta delta{};
    delta[0] = 0.125;
    const pair::ModelIsolationReport isolation =
        pair::apply_delta(parent, delta);
    expect(
        isolation.passed(),
        "selector fixture candidate failed isolation");
    const old_school::BotConfig challenger =
        g1::selector_bot_config(
            isolation.candidate,
            pair::kResidualWeight);
    const old_school::BotConfig baseline =
        g1::selector_bot_config(parent, 0.0);
    expect(
        pair::selector_config_exact(
            challenger, baseline, parent,
            isolation.candidate),
        "predeclared selector configuration was rejected");

    const auto require_selector_reject =
        [&](old_school::BotConfig changed_challenger,
            old_school::BotConfig changed_baseline,
            std::string_view message) {
            expect(
                !pair::selector_config_exact(
                    changed_challenger,
                    changed_baseline, parent,
                    isolation.candidate),
                message);
        };
    auto changed_challenger = challenger;
    changed_challenger.rollouts_per_action = 7;
    require_selector_reject(
        changed_challenger, baseline,
        "changed K8 selector passed");
    changed_challenger = challenger;
    changed_challenger.value_priority_residual_weight =
        0.0;
    require_selector_reject(
        changed_challenger, baseline,
        "changed candidate residual passed");
    changed_challenger = challenger;
    changed_challenger.value_pass_dominance = true;
    require_selector_reject(
        changed_challenger, baseline,
        "changed dominance selector passed");
    changed_challenger = challenger;
    changed_challenger
        .value_recursive_policy_improvement = true;
    require_selector_reject(
        changed_challenger, baseline,
        "changed recursive selector passed");
    changed_challenger = challenger;
    changed_challenger.training_games = 799;
    require_selector_reject(
        changed_challenger, baseline,
        "changed selector training census passed");
    auto changed_baseline = baseline;
    changed_baseline.value_priority_residual_weight =
        pair::kResidualWeight;
    require_selector_reject(
        challenger, changed_baseline,
        "residualized baseline selector passed");
    changed_baseline = baseline;
    changed_baseline.learned_model =
        isolation.candidate;
    require_selector_reject(
        challenger, changed_baseline,
        "candidate model entered the baseline arm");
    expect(
        !pair::selector_config_exact(
            challenger, baseline, nullptr,
            isolation.candidate) &&
            !pair::selector_config_exact(
                challenger, baseline, parent,
                nullptr),
        "null selector model passed");

    const pair::Dataset dataset =
        fold_dataset(parent);
    expect(
        pair::model_scores_bit_identical(
            dataset, isolation.candidate,
            isolation.candidate),
        "same-model Priority scores were not bit-identical");
    expect(
        !pair::model_scores_bit_identical(
            dataset, parent,
            isolation.candidate),
        "changed Priority readout had identical scores");
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::model_scores_bit_identical(
                    dataset, nullptr,
                    isolation.candidate));
        },
        "null score-comparison model was accepted");
}

void test_grouped_folds_and_canonical_oof() {
    const auto parent = test_model();
    const pair::Dataset dataset =
        fold_dataset(parent);
    expect(
        dataset.roots.size() == 80,
        "grouped synthetic root count drifted");
    for (std::size_t fold = 0;
         fold < pair::kFoldCount; ++fold) {
        const pair::Dataset fitting =
            pair::fold_training_dataset(
                dataset, fold);
        const pair::Dataset held =
            pair::fold_holdout_dataset(
                dataset, fold);
        expect(
            fitting.roots.size() == 60 &&
                held.roots.size() == 20,
            "fold root balance drifted");
        for (std::size_t deck = 0;
             deck < old_school::kDeckCount; ++deck) {
            expect(
                fitting.roots_by_deck[deck] == 12 &&
                    held.roots_by_deck[deck] == 4,
                "fold deck balance drifted");
        }
        std::set<std::size_t> fitting_games;
        std::map<
            std::size_t,
            std::array<std::size_t, 2>>
            held_games;
        for (const pair::Root& root :
             fitting.roots) {
            expect(
                root.schedule_index %
                        pair::kFoldCount !=
                    fold,
                "held-out game leaked into fitting");
            fitting_games.insert(
                root.schedule_index);
        }
        for (const pair::Root& root : held.roots) {
            expect(
                root.schedule_index %
                        pair::kFoldCount ==
                    fold &&
                    !fitting_games.contains(
                        root.schedule_index),
                "fitting game leaked into holdout");
            ++held_games[root.schedule_index]
                        [root.actor];
        }
        expect(
            held_games.size() == 10,
            "held-out source-game count drifted");
        for (const auto& [schedule, actors] :
             held_games) {
            static_cast<void>(schedule);
            expect(
                actors[0] == 1 &&
                    actors[1] == 1,
                "source-game actors crossed folds");
        }
    }

    const pair::FoldReport report =
        pair::evaluate_grouped_oof(
            dataset, parent);
    expect(
        report.exact_balance &&
            report.repeated_fits_bit_identical &&
            report.repeated_scores_bit_identical &&
            report.model_isolation_passed &&
            report.maximum_activation_difference <=
                1.0e-12 &&
            report.maximum_logit_difference <=
                1.0e-12 &&
            report.maximum_residual_difference <=
                1.0e-12,
        "grouped OOF replay invariant failed");
    expect(
        report.parent ==
            pair::evaluate(
                dataset, pair::Delta{}),
        "OOF parent was not canonical full-root order");

    pair::Dataset adjusted = dataset;
    for (pair::Root& root : adjusted.roots) {
        const pair::Delta& delta =
            report.fits[
                root.schedule_index %
                pair::kFoldCount]
                .delta;
        std::vector<double> logits(
            root.hidden.size(), 0.0);
        double mean = 0.0;
        for (std::size_t action = 0;
             action < root.hidden.size(); ++action) {
            for (std::size_t hidden = 0;
                 hidden < pair::kHiddenCount;
                 ++hidden) {
                logits[action] +=
                    root.hidden[action][hidden] *
                    delta[hidden];
            }
            mean +=
                logits[action] /
                static_cast<double>(
                    root.hidden.size());
        }
        for (std::size_t action = 0;
             action < root.hidden.size(); ++action) {
            const double residual =
                pair::kResidualWeight *
                std::tanh(logits[action] - mean);
            for (direct::RankCell& cell :
                 root.ranking.actions[action]
                     .worlds) {
                cell.parent_leaf_values[0] +=
                    residual;
                cell.parent_leaf_values[1] +=
                    residual;
            }
        }
    }
    const pair::Metrics expected =
        pair::evaluate(
            adjusted, pair::Delta{});
    expect(
        report.candidate.pairs ==
            expected.pairs,
        "OOF pair metrics were not combined canonically");
    expect(
        report.candidate.ranking
                .equal_deck_listwise_cross_entropy ==
            expected.ranking
                .equal_deck_listwise_cross_entropy &&
            report.candidate.ranking
                .equal_deck_top_one_expected_agreement ==
            expected.ranking
                .equal_deck_top_one_expected_agreement &&
            report.candidate.ranking
                .equal_deck_stable_pair_agreement ==
            expected.ranking
                .equal_deck_stable_pair_agreement &&
            report.candidate.ranking
                .equal_deck_mean_regret ==
            expected.ranking
                .equal_deck_mean_regret,
        "OOF ranking metrics were not combined canonically");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        expect(
            report.candidate.ranking.decks[deck]
                    .listwise_cross_entropy ==
                expected.ranking.decks[deck]
                    .listwise_cross_entropy &&
                report.candidate.ranking.decks[deck]
                    .top_one_expected_agreement ==
                expected.ranking.decks[deck]
                    .top_one_expected_agreement &&
                report.candidate.ranking.decks[deck]
                    .stable_pair_agreement ==
                expected.ranking.decks[deck]
                    .stable_pair_agreement &&
                report.candidate.ranking.decks[deck]
                    .mean_regret ==
                expected.ranking.decks[deck]
                    .mean_regret,
            "per-deck OOF assembly drifted");
    }
}

pair::OfflineGateInputs passing_gate_inputs() {
    pair::OfflineGateInputs inputs{
        .cache_identity_exact = true,
        .pair_census_exact = true,
        .optimizer_recipe_exact = true,
        .grouped_folds_exact = true,
        .repeat_fits_bit_identical = true,
        .parameter_replay_bit_identical = true,
        .zero_delta_equivalent = true,
        .actual_model_agreement = true,
        .parent_immutable = true,
        .model_isolation_passed = true,
        .successor_predictions_bit_identical = true,
        .successor_metrics_bit_identical = true,
    };
    const auto set_pair =
        [](pair::Metrics& parent,
           pair::Metrics& candidate) {
            parent.pairs.equal_deck_pair_bce =
                0.7;
            candidate.pairs.equal_deck_pair_bce =
                0.6;
            parent.ranking
                .equal_deck_mean_regret = 0.2;
            candidate.ranking
                .equal_deck_mean_regret = 0.1;
            parent.ranking
                .equal_deck_listwise_cross_entropy =
                0.9;
            candidate.ranking
                .equal_deck_listwise_cross_entropy =
                0.9;
            parent.ranking
                .equal_deck_top_one_expected_agreement =
                0.5;
            candidate.ranking
                .equal_deck_top_one_expected_agreement =
                0.5;
            parent.ranking
                .equal_deck_stable_pair_agreement =
                0.5;
            candidate.ranking
                .equal_deck_stable_pair_agreement =
                0.5;
            for (std::size_t deck = 0;
                 deck < old_school::kDeckCount;
                 ++deck) {
                parent.ranking.decks[deck]
                    .mean_regret = 0.2;
                candidate.ranking.decks[deck]
                    .mean_regret = 0.2;
            }
        };
    set_pair(
        inputs.parent_train,
        inputs.candidate_train);
    set_pair(
        inputs.parent_oof,
        inputs.candidate_oof);
    set_pair(
        inputs.parent_dev,
        inputs.candidate_dev);
    return inputs;
}

void test_offline_gate_exact_boundaries() {
    const pair::OfflineGateInputs passing =
        passing_gate_inputs();
    expect(
        pair::evaluate_offline_gate(
            passing).passed(),
        "valid conjunctive offline gate was rejected");

    const auto require_reject =
        [](pair::OfflineGateInputs changed,
           std::string_view message) {
            expect(
                !pair::evaluate_offline_gate(
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
        "TRAIN pair-BCE equality passed a strict gate");
    changed = passing;
    changed.candidate_train.ranking
        .equal_deck_mean_regret =
        changed.parent_train.ranking
            .equal_deck_mean_regret;
    require_reject(
        changed,
        "TRAIN regret equality passed a strict gate");
    changed = passing;
    changed.candidate_train.ranking
        .equal_deck_listwise_cross_entropy =
        std::nextafter(
            changed.parent_train.ranking
                .equal_deck_listwise_cross_entropy,
            std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "TRAIN listwise increase passed");

    changed = passing;
    changed.candidate_oof.pairs
        .equal_deck_pair_bce =
        changed.parent_oof.pairs
            .equal_deck_pair_bce;
    require_reject(
        changed,
        "OOF pair-BCE equality passed a strict gate");
    changed = passing;
    changed.candidate_oof.ranking
        .equal_deck_mean_regret =
        changed.parent_oof.ranking
            .equal_deck_mean_regret;
    require_reject(
        changed,
        "OOF regret equality passed a strict gate");
    changed = passing;
    changed.candidate_oof.ranking
        .equal_deck_listwise_cross_entropy =
        std::nextafter(
            changed.parent_oof.ranking
                .equal_deck_listwise_cross_entropy,
            std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "OOF listwise increase passed");
    changed = passing;
    changed.candidate_oof.ranking
        .equal_deck_top_one_expected_agreement =
        std::nextafter(
            changed.parent_oof.ranking
                .equal_deck_top_one_expected_agreement,
            -std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "OOF top-one decrease passed");
    changed = passing;
    changed.candidate_oof.ranking
        .equal_deck_stable_pair_agreement =
        std::nextafter(
            changed.parent_oof.ranking
                .equal_deck_stable_pair_agreement,
            -std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "OOF stable-pair decrease passed");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        changed = passing;
        changed.candidate_oof.ranking.decks[deck]
            .mean_regret =
            std::nextafter(
                changed.parent_oof.ranking.decks[deck]
                    .mean_regret,
                std::numeric_limits<double>::infinity());
        require_reject(
            changed,
            "OOF per-deck regret increase passed");
    }

    changed = passing;
    changed.candidate_dev.pairs
        .equal_deck_pair_bce =
        changed.parent_dev.pairs
            .equal_deck_pair_bce;
    require_reject(
        changed,
        "DEV pair-BCE equality passed a strict gate");
    changed = passing;
    changed.candidate_dev.ranking
        .equal_deck_mean_regret =
        changed.parent_dev.ranking
            .equal_deck_mean_regret;
    require_reject(
        changed,
        "DEV regret equality passed a strict gate");
    changed = passing;
    changed.candidate_dev.ranking
        .equal_deck_listwise_cross_entropy =
        std::nextafter(
            changed.parent_dev.ranking
                .equal_deck_listwise_cross_entropy,
            std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "DEV listwise increase passed");
    changed = passing;
    changed.candidate_dev.ranking
        .equal_deck_top_one_expected_agreement =
        std::nextafter(
            changed.parent_dev.ranking
                .equal_deck_top_one_expected_agreement,
            -std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "DEV top-one decrease passed");
    changed = passing;
    changed.candidate_dev.ranking
        .equal_deck_stable_pair_agreement =
        std::nextafter(
            changed.parent_dev.ranking
                .equal_deck_stable_pair_agreement,
            -std::numeric_limits<double>::infinity());
    require_reject(
        changed,
        "DEV stable-pair decrease passed");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        changed = passing;
        changed.candidate_dev.ranking.decks[deck]
            .mean_regret =
            std::nextafter(
                changed.parent_dev.ranking.decks[deck]
                    .mean_regret,
                std::numeric_limits<double>::infinity());
        require_reject(
            changed,
            "DEV per-deck regret increase passed");
    }

    changed = passing;
    changed.successor_predictions_bit_identical =
        false;
    require_reject(
        changed,
        "changed successor predictions passed");
    changed = passing;
    changed.successor_metrics_bit_identical =
        false;
    require_reject(
        changed,
        "changed successor metrics passed");

    const std::array<
        void (*)(pair::OfflineGateInputs&), 10>
        break_invariant{
            +[](pair::OfflineGateInputs& value) {
                value.cache_identity_exact = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.pair_census_exact = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.optimizer_recipe_exact = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.grouped_folds_exact = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.repeat_fits_bit_identical = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.parameter_replay_bit_identical =
                    false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.zero_delta_equivalent = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.actual_model_agreement = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.parent_immutable = false;
            },
            +[](pair::OfflineGateInputs& value) {
                value.model_isolation_passed = false;
            },
        };
    for (const auto mutation : break_invariant) {
        changed = passing;
        mutation(changed);
        require_reject(
            changed,
            "broken frozen invariant passed");
    }
}

void test_precomputed_score_validation_fails_closed() {
    const pair::PrecomputedScoreDataset valid =
        precompute_scores(
            replicated_dataset(
                {0.8, 0.2},
                {0.4, 0.6}));
    const std::vector<std::vector<double>> residuals(
        valid.roots.size(),
        std::vector<double>(2, 0.0));
    pair::validate_precomputed_score_dataset(valid);
    static_cast<void>(
        pair::evaluate_precomputed_residuals(
            valid, residuals));

    const auto require_dataset_reject =
        [](pair::PrecomputedScoreDataset changed,
           std::string_view message) {
            expect_rejected(
                [&] {
                    pair::validate_precomputed_score_dataset(
                        changed);
                },
                message);
        };

    pair::PrecomputedScoreDataset changed = valid;
    changed.roots.front().deck =
        static_cast<old_school::DeckId>(
            old_school::kDeckCount);
    require_dataset_reject(
        std::move(changed),
        "invalid precomputed deck was accepted");

    changed = valid;
    changed.roots.front()
        .base_aggregate_scores.pop_back();
    require_dataset_reject(
        std::move(changed),
        "short precomputed base aggregates were accepted");

    changed = valid;
    changed.roots.front()
        .teacher_aggregate_scores.front() =
        std::numeric_limits<double>::quiet_NaN();
    require_dataset_reject(
        std::move(changed),
        "nonfinite teacher aggregate was accepted");

    changed = valid;
    changed.roots.front()
        .base_aggregate_scores.front() =
        std::nextafter(
            1.0,
            std::numeric_limits<double>::infinity());
    require_dataset_reject(
        std::move(changed),
        "out-of-range base aggregate was accepted");

    changed = valid;
    changed.roots.front()
        .common_world_teacher_samples.front()
        .pop_back();
    require_dataset_reject(
        std::move(changed),
        "single precomputed teacher world was accepted");

    changed = valid;
    changed.roots.front()
        .common_world_teacher_samples.back()
        .push_back(0.5);
    require_dataset_reject(
        std::move(changed),
        "unpaired precomputed teacher worlds were accepted");

    changed = valid;
    changed.roots.front()
        .common_world_teacher_samples.front()
        .front() =
        std::numeric_limits<double>::infinity();
    require_dataset_reject(
        std::move(changed),
        "nonfinite precomputed teacher sample was accepted");

    changed = valid;
    ++changed.roots_by_deck.front();
    require_dataset_reject(
        std::move(changed),
        "precomputed deck census mutation was accepted");

    changed = valid;
    changed.roots.erase(changed.roots.begin());
    changed.roots_by_deck.front() = 0;
    require_dataset_reject(
        std::move(changed),
        "empty precomputed deck was accepted");

    auto changed_residuals = residuals;
    changed_residuals.pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::evaluate_precomputed_residuals(
                    valid, changed_residuals));
        },
        "short precomputed residual root list was accepted");

    changed_residuals = residuals;
    changed_residuals.front().pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::evaluate_precomputed_residuals(
                    valid, changed_residuals));
        },
        "short precomputed residual row was accepted");

    changed_residuals = residuals;
    changed_residuals.front().front() =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::evaluate_precomputed_residuals(
                    valid, changed_residuals));
        },
        "nonfinite precomputed residual was accepted");
}

void test_validation_fails_closed() {
    pair::Dataset malformed =
        learning_dataset();
    malformed.roots.front().actor = 2;
    expect_rejected(
        [&] {
            pair::validate_dataset(malformed);
        },
        "invalid actor was accepted");

    malformed = learning_dataset();
    malformed.roots.front().options.front()
        .pop_back();
    expect_rejected(
        [&] {
            pair::validate_dataset(malformed);
        },
        "short option row was accepted");

    malformed = learning_dataset();
    malformed.roots.front().hidden.front()[0] =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            pair::validate_dataset(malformed);
        },
        "nonfinite hidden activation was accepted");

    malformed = learning_dataset();
    malformed.roots.front().hidden.front()[0] =
        std::nextafter(
            1.0,
            std::numeric_limits<double>::infinity());
    expect_rejected(
        [&] {
            pair::validate_dataset(malformed);
        },
        "out-of-range tanh activation was accepted");

    malformed = learning_dataset();
    malformed.roots.front().ranking.actions.front()
        .worlds.pop_back();
    expect_rejected(
        [&] {
            pair::validate_dataset(malformed);
        },
        "single-world action was accepted");

    malformed = learning_dataset();
    malformed.roots.front().ranking.actions.back()
        .worlds.push_back(
            malformed.roots.front()
                .ranking.actions.back()
                .worlds.back());
    expect_rejected(
        [&] {
            pair::validate_dataset(malformed);
        },
        "unpaired action worlds were accepted");

    malformed = learning_dataset();
    malformed.roots[1].ranking.stable_root_id =
        malformed.roots[0].ranking.stable_root_id;
    expect_rejected(
        [&] {
            pair::validate_dataset(malformed);
        },
        "duplicate root identity was accepted");

    pair::Delta nonfinite{};
    nonfinite[0] =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::evaluate(
                    learning_dataset(),
                    nonfinite));
        },
        "nonfinite evaluation delta was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::testing::objective_probe(
                    learning_dataset(),
                    nonfinite));
        },
        "nonfinite objective delta was accepted");

    expect_rejected(
        [&] {
            pair::OptimizerConfig changed;
            changed.steps = 255;
            static_cast<void>(
                pair::optimize(
                    learning_dataset(), changed));
        },
        "changed optimizer recipe was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::fold_holdout_dataset(
                    learning_dataset(),
                    pair::kFoldCount));
        },
        "invalid fold index was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                pair::apply_delta(
                    nullptr, pair::Delta{}));
        },
        "null parent was accepted");
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
        "fixed recipe and shape",
        test_fixed_recipe_and_shape);
    run(
        "manual two-action objective and gradient",
        test_manual_two_action_objective_and_gradient);
    run(
        "manual three-action pair weighting",
        test_manual_three_action_pair_weighting);
    run(
        "tied roots and outer weighting",
        test_tied_roots_and_outer_weighting);
    run(
        "ranking metric tie semantics",
        test_ranking_metric_tie_semantics);
    run(
        "precomputed score metric equivalence",
        test_precomputed_score_metric_equivalence);
    run(
        "Adam is bit deterministic",
        test_adam_is_bit_deterministic);
    run(
        "model isolation and signed zero",
        test_model_isolation_and_signed_zero);
    run(
        "selector contract and exact model scores",
        test_selector_contract_and_exact_model_scores);
    run(
        "grouped folds and canonical OOF",
        test_grouped_folds_and_canonical_oof);
    run(
        "offline gate exact boundaries",
        test_offline_gate_exact_boundaries);
    run(
        "precomputed score validation fails closed",
        test_precomputed_score_validation_fails_closed);
    run(
        "validation fails closed",
        test_validation_fails_closed);

    std::cout << passed
              << " decision-boundary action-pair tests passed\n";
}
