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
#include <vector>

namespace {

using old_school::LearnedPrioritySparseCross;
using old_school::LearnedPrioritySparseCrossAction;
using old_school::LearnedPrioritySparseCrossState;
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

LearnedPrioritySparseCrossTerms sample_terms() {
    LearnedPrioritySparseCrossTerms terms;
    terms.reserve(
        old_school::kLearnedPrioritySparseCrossTermCount);
    for (std::size_t index = 0;
         index <
         old_school::kLearnedPrioritySparseCrossTermCount;
         ++index) {
        terms.push_back({
            .state_feature = index,
            .action_feature = index,
            .sigma =
                0.5 + 0.125 *
                          static_cast<double>(index),
            .beta =
                (index % 2 == 0 ? 1.0 : -1.0) *
                0.05 *
                static_cast<double>(index + 1),
        });
    }
    return terms;
}

std::vector<std::vector<double>> sample_rows() {
    std::vector<std::vector<double>> rows(
        3,
        std::vector<double>(
            old_school::
                kLearnedPrioritySparseCrossPolicyFeatureCount,
            0.0));
    for (auto& row : rows) {
        for (std::size_t feature = 0; feature < 16;
             ++feature) {
            row[feature] =
                (feature % 3 == 0 ? -1.0 : 1.0) *
                0.03 *
                static_cast<double>(feature + 1);
        }
    }
    for (std::size_t action = 0;
         action < rows.size(); ++action) {
        for (std::size_t feature = 0; feature < 16;
             ++feature) {
            rows[action]
                [old_school::
                     kLearnedPrioritySparseCrossStateFeatureCount +
                 feature] =
                (action == 1 ? -1.0 : 1.0) *
                0.01 *
                static_cast<double>(
                    (action + 1) * (feature + 2));
        }
    }
    return rows;
}

std::vector<double> reference_residuals(
    const LearnedPrioritySparseCrossTerms& terms,
    const std::vector<std::vector<double>>& rows,
    std::span<const std::size_t> canonical_order) {
    std::array<double, 16> means{};
    for (std::size_t term_index = 0;
         term_index < terms.size(); ++term_index) {
        for (const std::size_t action :
             canonical_order) {
            means[term_index] +=
                rows[action]
                    [old_school::
                         kLearnedPrioritySparseCrossStateFeatureCount +
                     terms[term_index].action_feature];
        }
        means[term_index] /=
            static_cast<double>(rows.size());
    }
    std::vector<double> logits(rows.size(), 0.0);
    for (std::size_t action = 0;
         action < rows.size(); ++action) {
        for (std::size_t term_index = 0;
             term_index < terms.size(); ++term_index) {
            const auto& term = terms[term_index];
            logits[action] +=
                term.beta *
                (rows[action][term.state_feature] *
                 (rows[action]
                      [old_school::
                           kLearnedPrioritySparseCrossStateFeatureCount +
                       term.action_feature] -
                  means[term_index]) /
                 term.sigma);
        }
    }
    double mean = 0.0;
    for (const std::size_t action :
         canonical_order) {
        mean += logits[action];
    }
    mean /= static_cast<double>(rows.size());
    for (double& value : logits) {
        value =
            old_school::
                kLearnedPrioritySparseCrossResidualWeight *
            std::tanh(value - mean);
        if (value == 0.0) {
            value = 0.0;
        }
    }
    return logits;
}

void projected_rows(
    const std::vector<std::vector<double>>& rows,
    LearnedPrioritySparseCrossState& state,
    std::vector<LearnedPrioritySparseCrossAction>&
        actions) {
    std::copy_n(
        rows.front().begin(), state.size(),
        state.begin());
    actions.resize(rows.size());
    for (std::size_t action = 0;
         action < rows.size(); ++action) {
        std::copy_n(
            rows[action].begin() +
                static_cast<std::ptrdiff_t>(
                    state.size()),
            actions[action].size(),
            actions[action].begin());
    }
}

void test_constants_and_runtime_shapes() {
    static_assert(
        old_school::
            kLearnedPrioritySparseCrossStateFeatureCount ==
        674);
    static_assert(
        old_school::
            kLearnedPrioritySparseCrossActionFeatureCount ==
        219);
    static_assert(
        old_school::
            kLearnedPrioritySparseCrossPolicyFeatureCount ==
        893);
    static_assert(
        old_school::kLearnedPrioritySparseCrossTermCount ==
        16);
    static_assert(
        old_school::kLearnedPrioritySparseCrossWorlds ==
        8);
    expect(
        old_school::
                kLearnedPrioritySparseCrossRequiredFingerprint ==
            "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f",
        "AQ20 required C16 fingerprint drifted");
    expect(
        LearnedPrioritySparseCross({}).empty(),
        "empty AQ20 control object is not empty");
    expect(
        !LearnedPrioritySparseCross(sample_terms()).empty(),
        "sixteen-term AQ20 object is empty");
    for (const std::size_t invalid_count :
         std::array<std::size_t, 3>{1, 15, 17}) {
        expect_rejected(
            [invalid_count] {
                auto terms = sample_terms();
                terms.resize(invalid_count);
                static_cast<void>(
                    LearnedPrioritySparseCross(
                        std::move(terms)));
            },
            "invalid AQ20 term count was accepted");
    }
}

void test_empty_fast_path_and_input_validation() {
    const LearnedPrioritySparseCross empty({});
    auto rows = sample_rows();
    constexpr std::array<std::size_t, 3> canonical = {
        0, 1, 2,
    };
    const auto residuals =
        empty.residuals(rows, canonical);
    expect(
        residuals.size() == rows.size() &&
            std::all_of(
                residuals.begin(), residuals.end(),
                positive_zero),
        "empty AQ20 object did not return exact +0");
    expect(
        empty.digest().size() == 32,
        "AQ20 local digest width drifted");

    rows[1][0] = -0.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                empty.residuals(rows, canonical));
        },
        "empty AQ20 object skipped state-prefix validation");
    rows = sample_rows();
    rows[0].pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                empty.residuals(rows, canonical));
        },
        "empty AQ20 object accepted a short row");
    rows = sample_rows();
    rows[0][800] =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                empty.residuals(rows, canonical));
        },
        "empty AQ20 object accepted nonfinite features");

    LearnedPrioritySparseCrossState state{};
    std::vector<LearnedPrioritySparseCrossAction> actions;
    const std::array<std::size_t, 0> no_order{};
    expect_rejected(
        [&] {
            static_cast<void>(
                empty.residuals(
                    state, actions, no_order));
        },
        "AQ20 accepted an empty projected action set");
}

void test_forward_projected_seam_and_bound() {
    const auto terms = sample_terms();
    const LearnedPrioritySparseCross object(terms);
    const auto rows = sample_rows();
    constexpr std::array<std::size_t, 3> canonical = {
        0, 1, 2,
    };
    const auto actual =
        object.residuals(rows, canonical);
    const auto expected =
        reference_residuals(
            terms, rows, canonical);
    expect(
        actual.size() == expected.size(),
        "AQ20 forward result width drifted");
    for (std::size_t index = 0;
         index < actual.size(); ++index) {
        expect_near(
            actual[index], expected[index], 1.0e-15,
            "AQ20 forward disagrees with reference");
        expect(
            std::abs(actual[index]) <=
                old_school::
                    kLearnedPrioritySparseCrossResidualWeight,
            "AQ20 residual escaped its declared bound");
    }

    LearnedPrioritySparseCrossState state{};
    std::vector<LearnedPrioritySparseCrossAction> actions;
    projected_rows(rows, state, actions);
    const auto projected =
        object.residuals(
            state, actions, canonical);
    expect(
        projected == actual,
        "AQ20 projected seam disagrees with policy rows");

    const std::vector<std::vector<double>> one = {
        rows.front(),
    };
    constexpr std::array<std::size_t, 1> one_order = {
        0,
    };
    const auto one_result =
        object.residuals(one, one_order);
    expect(
        one_result.size() == 1 &&
            positive_zero(one_result.front()),
        "one-action AQ20 residual is not exact +0");
}

void test_action_permutation_equivariance() {
    const LearnedPrioritySparseCross object(
        sample_terms());
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
        permuted_order = {1, 2, 0};
    const auto actual =
        object.residuals(
            permuted, permuted_order);
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
        "AQ20 scorer is not bitwise permutation "
        "equivariant");
}

void test_parameter_and_action_validation() {
    auto terms = sample_terms();
    terms[1].state_feature = terms[0].state_feature;
    terms[1].action_feature =
        terms[0].action_feature;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted a duplicate coordinate");

    terms = sample_terms();
    terms[0].state_feature =
        old_school::
            kLearnedPrioritySparseCrossStateFeatureCount;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted an out-of-range state coordinate");
    terms = sample_terms();
    terms[0].action_feature =
        old_school::
            kLearnedPrioritySparseCrossActionFeatureCount;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted an out-of-range action coordinate");
    terms = sample_terms();
    terms[0].sigma = 0.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted zero sigma");
    terms = sample_terms();
    terms[0].sigma = -1.0;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted negative sigma");
    terms = sample_terms();
    terms[0].sigma =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted infinite sigma");
    terms = sample_terms();
    terms[0].beta =
        std::numeric_limits<double>::quiet_NaN();
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted nonfinite beta");
    terms = sample_terms();
    terms[0].beta = 1.0000000000000002;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted beta above its declared clamp");
    terms[0].beta = -1.0000000000000002;
    expect_rejected(
        [&] {
            static_cast<void>(
                LearnedPrioritySparseCross(terms));
        },
        "AQ20 accepted beta below its declared clamp");

    const LearnedPrioritySparseCross object(
        sample_terms());
    const auto rows = sample_rows();
    constexpr std::array<std::size_t, 3>
        duplicate_order = {0, 0, 2};
    constexpr std::array<std::size_t, 3>
        out_of_range_order = {0, 1, 3};
    constexpr std::array<std::size_t, 2>
        short_order = {0, 1};
    expect_rejected(
        [&] {
            static_cast<void>(
                object.residuals(
                    rows, duplicate_order));
        },
        "AQ20 accepted duplicate canonical action index");
    expect_rejected(
        [&] {
            static_cast<void>(
                object.residuals(
                    rows, out_of_range_order));
        },
        "AQ20 accepted out-of-range canonical action");
    expect_rejected(
        [&] {
            static_cast<void>(
                object.residuals(rows, short_order));
        },
        "AQ20 accepted unaligned canonical order");
}

void test_canonical_round_trip_and_identity() {
    auto terms = sample_terms();
    terms.back().beta = -0.0;
    const LearnedPrioritySparseCross normalized(terms);
    expect(
        positive_zero(
            normalized.terms().back().beta),
        "AQ20 constructor did not normalize -0 beta");
    const std::string bytes =
        old_school::
            learned_priority_sparse_cross_canonical_bytes(
                terms);
    const auto decoded =
        old_school::
            learned_priority_sparse_cross_terms_from_canonical_bytes(
                bytes);
    expect(
        decoded == normalized.terms() &&
            old_school::
                    learned_priority_sparse_cross_canonical_bytes(
                        decoded) ==
                bytes,
        "AQ20 canonical terms did not round-trip");

    const auto first =
        std::make_shared<LearnedPrioritySparseCross>(
            normalized.terms());
    const auto second =
        std::make_shared<LearnedPrioritySparseCross>(
            decoded);
    auto changed = decoded;
    changed[0].beta += 0.125;
    const auto third =
        std::make_shared<LearnedPrioritySparseCross>(
            changed);
    const auto empty =
        std::make_shared<LearnedPrioritySparseCross>(
            LearnedPrioritySparseCrossTerms{});
    expect(
        old_school::
                learned_priority_sparse_cross_equivalent(
                    first, second) &&
            !old_school::
                 learned_priority_sparse_cross_equivalent(
                     first, third) &&
            old_school::
                learned_priority_sparse_cross_equivalent(
                    nullptr, nullptr) &&
            old_school::
                learned_priority_sparse_cross_equivalent(
                    empty, nullptr) &&
            old_school::
                learned_priority_sparse_cross_equivalent(
                    nullptr, empty) &&
            !old_school::
                 learned_priority_sparse_cross_equivalent(
                     first, nullptr),
        "AQ20 semantic object identity is incorrect");
    expect(
        first->digest() == second->digest() &&
            first->digest() != third->digest(),
        "AQ20 local digest does not bind parameters");

    std::string corrupt = bytes;
    corrupt.front() =
        corrupt.front() == 'x' ? 'y' : 'x';
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    learned_priority_sparse_cross_terms_from_canonical_bytes(
                        corrupt));
        },
        "AQ20 decoder accepted schema corruption");
    corrupt = bytes.substr(0, bytes.size() - 1);
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    learned_priority_sparse_cross_terms_from_canonical_bytes(
                        corrupt));
        },
        "AQ20 decoder accepted truncated bytes");
    corrupt = bytes;
    corrupt.back() =
        static_cast<char>(
            static_cast<unsigned char>(
                corrupt.back()) |
            0x80U);
    expect_rejected(
        [&] {
            static_cast<void>(
                old_school::
                    learned_priority_sparse_cross_terms_from_canonical_bytes(
                        corrupt));
        },
        "AQ20 decoder accepted negative-zero drift");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "constants and runtime shapes",
        test_constants_and_runtime_shapes);
    runner.run(
        "empty fast path and input validation",
        test_empty_fast_path_and_input_validation);
    runner.run(
        "forward projected seam and bound",
        test_forward_projected_seam_and_bound);
    runner.run(
        "action permutation equivariance",
        test_action_permutation_equivariance);
    runner.run(
        "parameter and action validation",
        test_parameter_and_action_validation);
    runner.run(
        "canonical round trip and identity",
        test_canonical_round_trip_and_identity);
    return runner.finish();
}
