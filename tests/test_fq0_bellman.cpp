#include "old_school/fq0_bellman.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fq0 = old_school::fq0_bellman;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_invalid(
    Function&& function, std::string_view message) {
    try {
        std::forward<Function>(function)();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

bool close(double first, double second,
           double tolerance = 1e-12) {
    return std::abs(first - second) <= tolerance;
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

fq0::TargetBlocks constant_target(double value) {
    fq0::TargetBlocks target;
    target.full = value;
    target.blocks.fill(value);
    return target;
}

fq0::ActionSamples action_samples(
    std::string descriptor, std::string stream,
    std::vector<double> values) {
    std::vector<std::size_t> worlds(values.size());
    for (std::size_t index = 0; index < worlds.size(); ++index) {
        worlds[index] = index;
    }
    return {
        .descriptor = std::move(descriptor),
        .sample_stream_key = std::move(stream),
        .world_indices = std::move(worlds),
        .samples = std::move(values),
    };
}

fq0::FeatureTargetRow collision_row(
    std::string row_id, std::string information_set,
    std::string legal_set, std::string common_world,
    std::vector<double> features, std::string consequence,
    fq0::TargetBlocks target, bool unique_max = true) {
    const std::string action_descriptor = row_id;
    return {
        .row_id = std::move(row_id),
        .information_set_id = std::move(information_set),
        .legal_set_id = std::move(legal_set),
        .common_world_key = std::move(common_world),
        .action_descriptor = action_descriptor,
        .features = std::move(features),
        .canonical_consequence_fingerprint =
            std::move(consequence),
        .target = target,
        .unique_exact_max = unique_max,
    };
}

void test_cross_fit_v0_is_explicit_and_order_invariant() {
    const std::vector<fq0::ActionSamples> bank_a = {
        action_samples("spell", "bank-a", {0.7, 0.7}),
        action_samples("pass", "bank-a", {0.8, 0.8}),
    };
    const std::vector<fq0::ActionSamples> bank_b = {
        action_samples("pass", "bank-b", {0.4, 0.4}),
        action_samples("spell", "bank-b", {0.9, 0.9}),
    };
    const fq0::CrossFitValue value =
        fq0::cross_fit_v0(bank_a, bank_b);
    expect(
        value.support_a == std::vector<std::string>{"pass"},
        "bank A selected the wrong exact support");
    expect(
        value.support_b == std::vector<std::string>{"spell"},
        "bank B selected the wrong exact support");
    expect(close(value.a_selected_b_value, 0.4),
           "bank A support was not evaluated by bank B");
    expect(close(value.b_selected_a_value, 0.7),
           "bank B support was not evaluated by bank A");
    expect(close(value.value, 0.55),
           "symmetric cross-fit mean is wrong");

    auto reversed_a = bank_a;
    auto reversed_b = bank_b;
    std::reverse(reversed_a.begin(), reversed_a.end());
    std::reverse(reversed_b.begin(), reversed_b.end());
    expect(
        fq0::cross_fit_v0(reversed_a, reversed_b) == value,
        "cross-fit result depends on descriptor input order");

    const std::vector<fq0::ActionSamples> tied_a = {
        action_samples("left", "tie-a", {0.8, 0.8}),
        action_samples("right", "tie-a", {0.8, 0.8}),
    };
    const std::vector<fq0::ActionSamples> split_b = {
        action_samples("left", "tie-b", {0.2, 0.2}),
        action_samples("right", "tie-b", {0.6, 0.6}),
    };
    const auto tied = fq0::cross_fit_v0(tied_a, split_b);
    expect(
        tied.support_a ==
                std::vector<std::string>({"left", "right"}) &&
            tied.support_b ==
                std::vector<std::string>{"right"} &&
            close(tied.a_selected_b_value, 0.4) &&
            close(tied.b_selected_a_value, 0.8) &&
            close(tied.value, 0.6),
        "multi-action support was not evaluated by the other bank");
}

void test_group_before_max_rejects_strategy_fusion() {
    const std::vector<fq0::ActionSamples> worlds = {
        action_samples("left", "bank-a", {1.0, 0.0}),
        action_samples("right", "bank-a", {0.0, 1.0}),
    };
    auto independent_worlds = worlds;
    for (auto& row : independent_worlds) {
        row.sample_stream_key = "bank-b";
    }
    const fq0::CrossFitValue grouped =
        fq0::cross_fit_v0(worlds, independent_worlds);
    expect(
        grouped.support_a ==
            std::vector<std::string>({"left", "right"}) &&
            grouped.support_b == grouped.support_a,
        "information-set action means did not retain the tie");
    expect(close(grouped.value, 0.5),
           "group-before-max value is wrong");

    const double strategy_fused =
        (std::max(1.0, 0.0) + std::max(0.0, 1.0)) / 2.0;
    expect(close(strategy_fused, 1.0) &&
               strategy_fused > grouped.value,
           "counterexample did not distinguish strategy fusion");
}

void test_exact_support_and_finite_checks() {
    const std::vector<fq0::ActionMean> tied = {
        {.descriptor = "b", .value = 0.75},
        {.descriptor = "a", .value = 0.75},
    };
    expect(
        fq0::exact_max_support(tied) ==
            std::vector<std::string>({"a", "b"}),
        "exact tie support is not descriptor canonical");

    const std::vector<fq0::ActionMean> signed_zero = {
        {.descriptor = "positive", .value = 0.0},
        {.descriptor = "negative", .value = -0.0},
    };
    const auto zero_support =
        fq0::exact_max_support(signed_zero);
    expect(
        zero_support == std::vector<std::string>{"negative"} &&
            same_bits(signed_zero[1].value, -0.0),
        "support did not use IEEE-bit equality");

    const double next =
        std::nextafter(0.5, 1.0);
    const std::vector<fq0::ActionMean> nearby = {
        {.descriptor = "low", .value = 0.5},
        {.descriptor = "high", .value = next},
    };
    expect(
        fq0::exact_max_support(nearby) ==
            std::vector<std::string>{"high"},
        "nearby non-tie was added to exact support");

    expect_invalid(
        [] {
            const std::vector<fq0::ActionMean> empty;
            fq0::exact_max_support(empty);
        },
        "empty support input was accepted");
    expect_invalid(
        [] {
            const std::vector<fq0::ActionMean> nonfinite = {
                {
                    .descriptor = "nan",
                    .value =
                        std::numeric_limits<double>::quiet_NaN()},
            };
            fq0::exact_max_support(nonfinite);
        },
        "non-finite support value was accepted");
    expect_invalid(
        [] {
            const std::vector<fq0::ActionSamples> duplicate = {
                action_samples("same", "bank-a", {0.2}),
                action_samples("same", "bank-a", {0.3}),
            };
            fq0::canonical_action_means(duplicate);
        },
        "duplicate action descriptor was accepted");
    expect_invalid(
        [] {
            const std::vector<fq0::ActionSamples> wide = {
                action_samples("a", "bank-a", {0.2, 0.3}),
            };
            const std::vector<fq0::ActionSamples> narrow = {
                action_samples("a", "bank-b", {0.2}),
            };
            fq0::cross_fit_v0(wide, narrow);
        },
        "unequal bank widths were accepted");
    expect_invalid(
        [] {
            auto first =
                action_samples("a", "same-stream", {0.2});
            auto second =
                action_samples("a", "same-stream", {0.3});
            const std::vector<fq0::ActionSamples> bank_a = {
                first};
            const std::vector<fq0::ActionSamples> bank_b = {
                second};
            fq0::cross_fit_v0(bank_a, bank_b);
        },
        "cross-fit reused one sample stream");
    expect_invalid(
        [] {
            auto malformed =
                action_samples("a", "bank-a", {0.2, 0.3});
            malformed.world_indices = {1, 0};
            const std::vector<fq0::ActionSamples> rows = {
                malformed};
            fq0::canonical_action_means(rows);
        },
        "noncanonical world order was accepted");
}

void test_group_weighted_backup_and_perspective() {
    expect(close(
               fq0::root_owner_continuation_value(
                   0.25, fq0::OwnerRelation::SameOwner),
               0.25),
           "same-owner value changed perspective");
    expect(close(
               fq0::root_owner_continuation_value(
                   0.25, fq0::OwnerRelation::OpponentOwner),
               0.75),
           "opponent-owner value was not complemented");
    expect(close(
               fq0::root_owner_continuation_value(
                   0.5, fq0::OwnerRelation::OpponentOwner),
               0.5),
           "draw complement is not exact");

    const std::vector<fq0::TerminalParticle> terminals = {
        {.world_index = 1, .root_owner_value = 0.5},
        {.world_index = 0, .root_owner_value = 1.0},
    };
    const std::vector<fq0::SuccessorGroup> groups = {
        {
            .fingerprint = "opponent",
            .mass = 4,
            .world_indices = {4, 5, 6, 7},
            .relation = fq0::OwnerRelation::OpponentOwner,
            .successor_owner_value = 0.25,
        },
        {
            .fingerprint = "same",
            .mass = 2,
            .world_indices = {2, 3},
            .relation = fq0::OwnerRelation::SameOwner,
            .successor_owner_value = 0.75,
        },
    };
    const fq0::BackedTarget target =
        fq0::back_up_root_target(8, terminals, groups);
    expect(close(target.value, 0.75),
           "group-weighted Tq0 is wrong");
    expect(
        target.terminal_particles == 2 &&
            target.same_owner_particles == 2 &&
            target.opponent_owner_particles == 4,
        "Bellman particle accounting is wrong");

    auto reversed = groups;
    std::reverse(reversed.begin(), reversed.end());
    expect(
        fq0::back_up_root_target(
            8, terminals, reversed) == target,
        "Bellman target depends on group input order");

    expect_invalid(
        [&] {
            fq0::back_up_root_target(9, terminals, groups);
        },
        "incomplete particle partition was accepted");
    expect_invalid(
        [&] {
            auto duplicate = groups;
            duplicate[1].fingerprint =
                duplicate[0].fingerprint;
            fq0::back_up_root_target(
                8, terminals, duplicate);
        },
        "duplicate successor group was accepted");
    expect_invalid(
        [&] {
            auto overlap = groups;
            overlap[0].world_indices[0] = 3;
            fq0::back_up_root_target(
                8, terminals, overlap);
        },
        "overlapping successor membership was accepted");
    expect_invalid(
        [&] {
            auto omitted = groups;
            omitted[0].world_indices[0] = 8;
            fq0::back_up_root_target(
                8, terminals, omitted);
        },
        "out-of-range/omitted successor membership was accepted");
    expect_invalid(
        [] {
            fq0::root_owner_continuation_value(
                1.1, fq0::OwnerRelation::OpponentOwner);
        },
        "out-of-range successor value was accepted");
    expect_invalid(
        [] {
            fq0::root_owner_continuation_value(
                0.5, static_cast<fq0::OwnerRelation>(99));
        },
        "invalid owner relation was treated as an opponent");
}

void test_student_t_block_contrast() {
    const fq0::BlockContrast constant =
        fq0::summarize_block_contrast(
            constant_target(0.6),
            constant_target(0.4));
    expect(close(constant.delta64, 0.2) &&
               close(constant.block_mean, 0.2) &&
               close(
                   constant.sample_standard_deviation, 0.0) &&
               close(constant.lower_95, 0.2),
           "zero-variance Student-t contrast is wrong");
    expect(
        constant.positive_blocks == fq0::kBlockCount &&
            constant.nonnegative_blocks == fq0::kBlockCount &&
            fq0::passes_directional_gate(constant),
        "positive block contrast failed its gate");

    fq0::TargetBlocks positive;
    fq0::TargetBlocks negative;
    positive.full = 0.6;
    negative.full = 0.5;
    for (std::size_t index = 0; index < fq0::kBlockCount;
         ++index) {
        positive.blocks[index] =
            0.51 + 0.01 * static_cast<double>(index);
        negative.blocks[index] = 0.5;
    }
    const fq0::BlockContrast varied =
        fq0::summarize_block_contrast(positive, negative);
    const double expected_mean = 0.045;
    const double expected_sd =
        std::sqrt(0.0042 / 7.0);
    const double expected_lower =
        expected_mean -
        fq0::kStudentT95Df7 * expected_sd /
            std::sqrt(8.0);
    expect(close(varied.block_mean, expected_mean) &&
               close(
                   varied.sample_standard_deviation,
                   expected_sd) &&
               close(varied.lower_95, expected_lower),
           "Student-t block calculation drifted");

    positive.full = 0.4;
    const fq0::BlockContrast full_negative =
        fq0::summarize_block_contrast(positive, negative);
    expect(!fq0::passes_directional_gate(full_negative),
           "negative full-K64 delta passed directional gate");

    fq0::BlockContrast five = constant;
    five.positive_blocks = 5;
    expect(!fq0::passes_directional_gate(five),
           "five positive blocks passed a six-block gate");
    five.positive_blocks = 6;
    expect(fq0::passes_directional_gate(five),
           "six positive blocks failed a six-block gate");
}

void test_global_collision_census_crosses_information_sets() {
    const std::vector<fq0::FeatureTargetRow> rows = {
        collision_row(
            "deck-a.row", "info-a", "legal-a", "shared-world-key",
            {1.0, 2.0}, "consequence-a",
            constant_target(0.5)),
        collision_row(
            "deck-b.row", "info-b", "legal-b", "shared-world-key",
            {1.0, 2.0}, "consequence-b",
            constant_target(0.5)),
        collision_row(
            "signed-zero.row", "info-c", "legal-c",
            "world-c", {1.0, -0.0}, "consequence-c",
            constant_target(0.5)),
    };
    const fq0::FeatureCollisionAnalysis analysis =
        fq0::analyze_global_feature_collisions(rows);
    expect(
        analysis.rows == 3 &&
            analysis.colliding_feature_classes == 1 &&
            analysis.collisions.size() == 1 &&
            analysis.harmful_collisions == 1 &&
            !analysis.passed,
        "global feature census did not cross information sets");
    const fq0::FeatureCollision& collision =
        analysis.collisions.front();
    expect(
        collision.first_row_id == "deck-a.row" &&
            collision.second_row_id == "deck-b.row" &&
            collision.target_method ==
                fq0::CollisionTargetMethod::IndependentNormal &&
            collision.consequence_conflict &&
            !collision.target_conflict &&
            collision.harmful,
        "cross-information-set collision was misclassified");
}

void test_collision_target_and_support_conflicts() {
    const std::vector<fq0::FeatureTargetRow> paired = {
        collision_row(
            "a", "info", "legal", "world", {4.0},
            "same", constant_target(0.8), true),
        collision_row(
            "b", "info", "legal", "world", {4.0},
            "same", constant_target(0.2), false),
    };
    const auto paired_analysis =
        fq0::analyze_global_feature_collisions(paired);
    expect(
        paired_analysis.collisions.size() == 1 &&
            paired_analysis.collisions.front().target_method ==
                fq0::CollisionTargetMethod::PairedStudentT &&
            close(
                paired_analysis.collisions.front()
                    .target_separation_lower_95,
                0.6) &&
            paired_analysis.collisions.front().target_conflict &&
            paired_analysis.collisions.front().support_conflict &&
            !paired_analysis.passed,
        "paired target/support collision was not harmful");

    const std::vector<fq0::FeatureTargetRow> support_only = {
        collision_row(
            "selected", "same-info", "same-legal", "same-world",
            {4.5}, "same",
            [] {
                auto target = constant_target(0.5);
                target.full = std::nextafter(0.5, 1.0);
                return target;
            }(),
            true),
        collision_row(
            "not-selected", "same-info", "same-legal",
            "same-world", {4.5}, "same",
            constant_target(0.5), false),
    };
    const auto support_only_analysis =
        fq0::analyze_global_feature_collisions(support_only);
    expect(
        support_only_analysis.collisions.size() == 1 &&
            close(
                support_only_analysis.collisions.front()
                    .target_separation_lower_95,
                0.0) &&
            support_only_analysis.collisions.front()
                .support_conflict &&
            support_only_analysis.collisions.front()
                .target_conflict &&
            !support_only_analysis.passed,
        "support-only conflict was not classified as a target conflict");

    const std::vector<fq0::FeatureTargetRow> independent = {
        collision_row(
            "a", "info-a", "legal-a", "world-a", {5.0},
            "same", constant_target(0.9)),
        collision_row(
            "b", "info-b", "legal-b", "world-b", {5.0},
            "same", constant_target(0.1)),
    };
    const auto independent_analysis =
        fq0::analyze_global_feature_collisions(independent);
    expect(
        independent_analysis.collisions.front().target_method ==
                fq0::CollisionTargetMethod::IndependentNormal &&
            independent_analysis.collisions.front()
                .target_conflict &&
            !independent_analysis.collisions.front()
                 .consequence_conflict,
        "independent target collision was misclassified");

    const std::vector<fq0::FeatureTargetRow> harmless = {
        collision_row(
            "a", "info-a", "legal-a", "world-a", {6.0},
            "same", constant_target(0.5)),
        collision_row(
            "b", "info-b", "legal-b", "world-b", {6.0},
            "same", constant_target(0.5)),
    };
    const auto harmless_analysis =
        fq0::analyze_global_feature_collisions(harmless);
    expect(
        harmless_analysis.collisions.size() == 1 &&
            !harmless_analysis.collisions.front().harmful &&
            harmless_analysis.passed,
        "harmless exact feature collision failed");
}

void test_collision_validation_and_ieee_features() {
    const std::vector<fq0::FeatureTargetRow> signed_zeros = {
        collision_row(
            "negative", "info-a", "legal-a", "world-a",
            {-0.0}, "same", constant_target(0.5)),
        collision_row(
            "positive", "info-b", "legal-b", "world-b",
            {0.0}, "same", constant_target(0.5)),
    };
    const auto analysis =
        fq0::analyze_global_feature_collisions(signed_zeros);
    expect(
        analysis.colliding_feature_classes == 0 &&
            analysis.collisions.empty() && analysis.passed,
        "signed-zero feature vectors were not bit-exact");

    expect_invalid(
        [] {
            auto first = collision_row(
                "duplicate", "info-a", "legal-a", "world-a",
                {1.0}, "same", constant_target(0.5));
            auto second = collision_row(
                "duplicate", "info-b", "legal-b", "world-b",
                {1.0}, "same", constant_target(0.5));
            const std::vector<fq0::FeatureTargetRow> rows = {
                first, second};
            fq0::analyze_global_feature_collisions(rows);
        },
        "duplicate collision row ID was accepted");
    expect_invalid(
        [] {
            auto row = collision_row(
                "nan", "info", "legal", "world",
                {std::numeric_limits<double>::quiet_NaN()},
                "same", constant_target(0.5));
            const std::vector<fq0::FeatureTargetRow> rows = {
                row};
            fq0::analyze_global_feature_collisions(rows);
        },
        "non-finite feature was accepted");
    expect_invalid(
        [] {
            auto first = collision_row(
                "first", "same-info", "same-legal", "world-a",
                {2.0}, "same", constant_target(0.5));
            auto second = collision_row(
                "second", "same-info", "same-legal", "world-b",
                {2.0}, "same", constant_target(0.5));
            const std::vector<fq0::FeatureTargetRow> rows = {
                first, second};
            fq0::analyze_global_feature_collisions(rows);
        },
        "one information set used inconsistent common-world keys");
    expect_invalid(
        [] {
            auto best = collision_row(
                "best", "info", "legal", "world",
                {3.0}, "same", constant_target(0.6), false);
            auto worse = collision_row(
                "worse", "info", "legal", "world",
                {4.0}, "same", constant_target(0.4), false);
            const std::vector<fq0::FeatureTargetRow> rows = {
                best, worse};
            fq0::analyze_global_feature_collisions(rows);
        },
        "mismatched unique exact-max flag was accepted");
    expect_invalid(
        [] {
            auto best = collision_row(
                "best", "info", "legal", "world",
                {3.0}, "same", constant_target(0.6), true);
            auto duplicate = collision_row(
                "other-row", "info", "legal", "world",
                {4.0}, "same", constant_target(0.4), false);
            duplicate.action_descriptor =
                best.action_descriptor;
            const std::vector<fq0::FeatureTargetRow> rows = {
                best, duplicate};
            fq0::analyze_global_feature_collisions(rows);
        },
        "duplicate legal action descriptor was accepted");
}

} // namespace

int main() {
    const std::vector<
        std::pair<std::string_view, std::function<void()>>>
        tests = {
            {"explicit cross-fit V0",
             test_cross_fit_v0_is_explicit_and_order_invariant},
            {"group-before-max strategy fusion",
             test_group_before_max_rejects_strategy_fusion},
            {"exact support and finite checks",
             test_exact_support_and_finite_checks},
            {"group-weighted backup and perspective",
             test_group_weighted_backup_and_perspective},
            {"Student-t block contrast",
             test_student_t_block_contrast},
            {"global cross-information-set collision census",
             test_global_collision_census_crosses_information_sets},
            {"collision target and support conflicts",
             test_collision_target_and_support_conflicts},
            {"collision validation and IEEE features",
             test_collision_validation_and_ieee_features},
        };

    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& failure) {
            std::cerr << "[FAIL] " << name << ": "
                      << failure.what() << '\n';
        }
    }
    std::cout << passed << "/" << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
