#include "old_school/fq4_priority_math.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace math = old_school::fq4_priority_math;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

void test_projection_noop_preserves_bits() {
    const std::vector<double> parent{0.5, 0.3, 0.2};
    const auto result =
        math::reverse_kl_i_projection(
            parent, {{0, 2}}, 2.0);
    expect(
        result.probabilities.size() == parent.size(),
        "projection changed dimensions");
    for (std::size_t index = 0;
         index < parent.size(); ++index) {
        expect(
            same_bits(
                result.probabilities[index],
                parent[index]),
            "feasible projection changed a parent bit");
    }
}

void test_projection_enforces_joint_star() {
    const auto result =
        math::reverse_kl_i_projection(
            {0.20, 0.45, 0.35},
            {{0, 1}, {0, 2}}, 1.1);
    expect(
        result.active_dominated_indices.size() == 2,
        "joint projection selected wrong active set");
    expect(
        result.probabilities[0] + 1.0e-14 >=
                1.1 * result.probabilities[1] &&
            result.probabilities[0] + 1.0e-14 >=
                1.1 * result.probabilities[2],
        "joint projection violated a star constraint");
}

void test_softmax_and_behavior_are_normalized() {
    const auto softmax =
        math::stable_softmax(
            {0.1, -0.4, 0.9}, 0.1);
    const auto behavior =
        math::behavior_mixture(softmax, 0.9);
    constexpr std::array<std::uint64_t, 3>
        kSoftmaxBits{
            0x3f35fa3a96ac7891ULL,
            0x3ec2f461b5c60817ULL,
            0x3feffd3bfb94bd00ULL,
        };
    constexpr std::array<std::uint64_t, 3>
        kBehaviorBits{
            0x3fa138a047537ab6ULL,
            0x3fa111554da405d9ULL,
            0x3feddb60a6b087f8ULL,
        };
    double total = 0.0;
    for (std::size_t index = 0;
         index < behavior.size(); ++index) {
        const double value = behavior[index];
        expect(
            std::bit_cast<std::uint64_t>(
                softmax[index]) ==
                kSoftmaxBits[index],
            "softmax full-bit anchor drifted");
        expect(
            std::bit_cast<std::uint64_t>(value) ==
                kBehaviorBits[index],
            "behavior-mixture full-bit anchor drifted");
        expect(
            value > 0.0 && value < 1.0,
            "behavior probability is not interior");
        total += value;
    }
    expect(
        std::abs(total - 1.0) < 1.0e-12,
        "behavior probabilities are not normalized");
}

void test_centered_scores_match_formula() {
    const auto result =
        math::centered_tanh_scores(
            {0.4, 0.4, 0.2},
            {2.0, 1.0, 0.0}, 0.1);
    expect(
        result.centered_logits ==
            std::vector<double>({1.0, 0.0, -1.0}),
        "logits were not centered");
    expect(
        same_bits(
            result.residuals[0],
            0.1 * std::tanh(1.0)) &&
            same_bits(result.residuals[1], 0.0) &&
            same_bits(
                result.residuals[2],
                0.1 * std::tanh(-1.0)),
        "residual formula drifted");
    expect(
        result.exact_max_indices ==
            std::vector<std::size_t>{0},
        "exact support is wrong");
    constexpr std::array<std::uint64_t, 3>
        kCombinedBits{
            0x3fde7965576a971eULL,
            0x3fd999999999999aULL,
            0x3fbfb4043bef3d24ULL,
        };
    for (std::size_t index = 0;
         index < result.combined_scores.size();
         ++index) {
        expect(
            std::bit_cast<std::uint64_t>(
                result.combined_scores[index]) ==
                kCombinedBits[index],
            "combined-score full-bit anchor drifted");
    }

    const auto tie =
        math::centered_tanh_scores(
            {0.5, 0.5}, {0.0, 0.0}, 0.1);
    expect(
        tie.exact_max_indices ==
            std::vector<std::size_t>({0, 1}),
        "exact tied support was not preserved");
    expect(
        same_bits(tie.residuals[0], 0.0) &&
            same_bits(tie.residuals[1], 0.0),
        "uniform logits produced a nonzero residual");
}

void test_invalid_inputs_fail() {
    bool projection_threw = false;
    try {
        static_cast<void>(
            math::reverse_kl_i_projection(
                {0.5, 0.5},
                {{0, 0}}, 1.1));
    } catch (const std::invalid_argument&) {
        projection_threw = true;
    }
    expect(projection_threw,
           "malformed projection passed");

    bool softmax_threw = false;
    try {
        static_cast<void>(
            math::stable_softmax({0.0}, 0.0));
    } catch (const std::invalid_argument&) {
        softmax_threw = true;
    }
    expect(softmax_threw,
           "invalid softmax temperature passed");

    bool behavior_threw = false;
    try {
        static_cast<void>(
            math::behavior_mixture(
                {0.4, 0.4}, 0.9));
    } catch (const std::invalid_argument&) {
        behavior_threw = true;
    }
    expect(behavior_threw,
           "unnormalized behavior input passed");

    bool residual_threw = false;
    try {
        static_cast<void>(
            math::centered_tanh_scores(
                {0.5}, {0.0, 1.0}, 0.1));
    } catch (const std::invalid_argument&) {
        residual_threw = true;
    }
    expect(residual_threw,
           "mismatched residual vectors passed");
}

} // namespace

int main() {
    const std::array<
        std::pair<std::string_view,
                  std::function<void()>>,
        5>
        tests{{
            {
                "projection no-op bit identity",
                test_projection_noop_preserves_bits,
            },
            {
                "joint star projection",
                test_projection_enforces_joint_star,
            },
            {
                "softmax behavior normalization",
                test_softmax_and_behavior_are_normalized,
            },
            {
                "centered tanh scoring",
                test_centered_scores_match_formula,
            },
            {
                "invalid input rejection",
                test_invalid_inputs_fail,
            },
        }};
    std::size_t passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr
                << "[FAIL] " << name << ": "
                << error.what() << '\n';
        }
    }
    std::cout << passed << "/" << tests.size()
              << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
