#include "old_school/learned_priority_bilinear.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace old_school {
namespace {

constexpr std::uint64_t kPositiveZeroBits = 0;
constexpr double kProjectionScale = 1.0 / 32.0;

std::uint64_t splitmix64(std::uint64_t value) {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value =
        (value ^ (value >> 30)) *
        UINT64_C(0xbf58476d1ce4e5b9);
    value =
        (value ^ (value >> 27)) *
        UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

LearnedPriorityBilinearStateProjection make_u0() {
    LearnedPriorityBilinearStateProjection result{};
    for (std::size_t rank = 0;
         rank < kLearnedPriorityBilinearRank; ++rank) {
        for (std::size_t feature = 0;
             feature <
             kLearnedPriorityBilinearStateFeatureCount;
             ++feature) {
            const std::uint64_t initial =
                kLearnedPriorityBilinearFitTag ^
                (static_cast<std::uint64_t>(rank + 1) << 32) ^
                static_cast<std::uint64_t>(feature + 1);
            result[rank][feature] =
                (splitmix64(initial) >> 63) != 0
                    ? kProjectionScale
                    : -kProjectionScale;
        }
    }
    for (const auto& row : result) {
        const bool has_positive =
            std::any_of(
                row.begin(), row.end(),
                [](double value) {
                    return value > 0.0;
                });
        const bool has_negative =
            std::any_of(
                row.begin(), row.end(),
                [](double value) {
                    return value < 0.0;
                });
        if (!has_positive || !has_negative) {
            throw std::logic_error(
                "AQ19 fixed U0 row is constant");
        }
    }
    if (result[0] == result[1]) {
        throw std::logic_error(
            "AQ19 fixed U0 rows are identical");
    }
    return result;
}

bool positive_zero(double value) {
    return std::bit_cast<std::uint64_t>(value) ==
           kPositiveZeroBits;
}

void validate_parameters(
    const LearnedPriorityBilinearParameters& parameters) {
    const auto validate =
        [](const auto& matrix) {
            for (const auto& row : matrix) {
                for (const double value : row) {
                    if (!std::isfinite(value) ||
                        (value == 0.0 &&
                         !positive_zero(value))) {
                        throw std::invalid_argument(
                            "AQ19 bilinear parameters must be finite "
                            "and use positive zero");
                    }
                }
            }
        };
    validate(parameters.delta_u);
    validate(parameters.v);
}

void append_unsigned64(
    std::string& output, std::uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(
            static_cast<char>(
                static_cast<unsigned char>(
                    value >> shift)));
    }
}

std::uint64_t read_unsigned64(
    std::string_view input, std::size_t& cursor) {
    if (cursor > input.size() ||
        input.size() - cursor < 8) {
        throw std::invalid_argument(
            "AQ19 canonical parameter bytes are truncated");
    }
    std::uint64_t value = 0;
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        value |=
            static_cast<std::uint64_t>(
                static_cast<unsigned char>(
                    input[cursor++]))
            << shift;
    }
    return value;
}

std::string hexadecimal(std::uint64_t value) {
    static constexpr char kDigits[] =
        "0123456789abcdef";
    std::string result(16, '0');
    for (std::size_t index = 0; index < result.size();
         ++index) {
        const std::size_t offset =
            result.size() - index - 1;
        result[offset] =
            kDigits[value & UINT64_C(0x0f)];
        value >>= 4;
    }
    return result;
}

std::string local_digest(std::string_view bytes) {
    // Two fixed-domain FNV-1a accumulators provide a stable lightweight
    // identity. Security-sensitive artifact authentication remains in the
    // AQ19 research module.
    std::uint64_t first =
        UINT64_C(0xcbf29ce484222325);
    std::uint64_t second =
        UINT64_C(0x84222325cbf29ce4);
    for (const unsigned char byte : bytes) {
        first ^= static_cast<std::uint64_t>(byte);
        first *= UINT64_C(0x100000001b3);
        second ^=
            static_cast<std::uint64_t>(byte) ^
            UINT64_C(0xa5);
        second *= UINT64_C(0x100000001b3);
    }
    return hexadecimal(first) + hexadecimal(second);
}

bool bit_identical(
    const LearnedPriorityBilinearParameters& first,
    const LearnedPriorityBilinearParameters& second) {
    const auto equal =
        [](const auto& left, const auto& right) {
            for (std::size_t row = 0;
                 row < left.size(); ++row) {
                for (std::size_t column = 0;
                     column < left[row].size(); ++column) {
                    if (std::bit_cast<std::uint64_t>(
                            left[row][column]) !=
                        std::bit_cast<std::uint64_t>(
                            right[row][column])) {
                        return false;
                    }
                }
            }
            return true;
        };
    return equal(first.delta_u, second.delta_u) &&
           equal(first.v, second.v);
}

} // namespace

const LearnedPriorityBilinearStateProjection&
learned_priority_bilinear_u0() {
    static const LearnedPriorityBilinearStateProjection projection =
        make_u0();
    return projection;
}

std::string learned_priority_bilinear_canonical_bytes(
    const LearnedPriorityBilinearParameters& parameters) {
    validate_parameters(parameters);
    std::string result;
    result.append(kLearnedPriorityBilinearIdentifier);
    result.push_back('\n');
    append_unsigned64(
        result, kLearnedPriorityBilinearFitTag);
    append_unsigned64(
        result, kLearnedPriorityBilinearRank);
    append_unsigned64(
        result,
        kLearnedPriorityBilinearStateFeatureCount);
    append_unsigned64(
        result,
        kLearnedPriorityBilinearActionFeatureCount);
    append_unsigned64(
        result,
        std::bit_cast<std::uint64_t>(
            kLearnedPriorityBilinearResidualWeight));
    const auto append =
        [&result](const auto& matrix) {
            for (const auto& row : matrix) {
                for (const double value : row) {
                    append_unsigned64(
                        result,
                        std::bit_cast<std::uint64_t>(
                            value));
                }
            }
        };
    append(parameters.delta_u);
    append(parameters.v);
    return result;
}

LearnedPriorityBilinearParameters
learned_priority_bilinear_parameters_from_canonical_bytes(
    std::string_view bytes) {
    const std::size_t text_bytes =
        kLearnedPriorityBilinearIdentifier.size() + 1;
    const std::size_t expected_bytes =
        text_bytes + 5 * sizeof(std::uint64_t) +
        kLearnedPriorityBilinearParameterCount *
            sizeof(std::uint64_t);
    if (bytes.size() != expected_bytes ||
        !bytes.starts_with(
            kLearnedPriorityBilinearIdentifier) ||
        bytes[kLearnedPriorityBilinearIdentifier.size()] !=
            '\n') {
        throw std::invalid_argument(
            "AQ19 canonical parameter bytes have the wrong "
            "schema or length");
    }
    std::size_t cursor = text_bytes;
    if (read_unsigned64(bytes, cursor) !=
            kLearnedPriorityBilinearFitTag ||
        read_unsigned64(bytes, cursor) !=
            kLearnedPriorityBilinearRank ||
        read_unsigned64(bytes, cursor) !=
            kLearnedPriorityBilinearStateFeatureCount ||
        read_unsigned64(bytes, cursor) !=
            kLearnedPriorityBilinearActionFeatureCount ||
        read_unsigned64(bytes, cursor) !=
            std::bit_cast<std::uint64_t>(
                kLearnedPriorityBilinearResidualWeight)) {
        throw std::invalid_argument(
            "AQ19 canonical parameter recipe drifted");
    }

    LearnedPriorityBilinearParameters parameters;
    const auto read_matrix =
        [&](auto& matrix) {
            for (auto& row : matrix) {
                for (double& value : row) {
                    value = std::bit_cast<double>(
                        read_unsigned64(bytes, cursor));
                }
            }
        };
    read_matrix(parameters.delta_u);
    read_matrix(parameters.v);
    if (cursor != bytes.size() ||
        learned_priority_bilinear_canonical_bytes(
            parameters) != bytes) {
        throw std::invalid_argument(
            "AQ19 canonical parameter bytes are not canonical");
    }
    return parameters;
}

LearnedPriorityBilinear::LearnedPriorityBilinear(
    LearnedPriorityBilinearParameters parameters)
    : parameters_(std::move(parameters)) {
    const std::string bytes =
        learned_priority_bilinear_canonical_bytes(
            parameters_);
    digest_ = local_digest(bytes);
    zero_action_projection_ =
        std::all_of(
            parameters_.v.begin(), parameters_.v.end(),
            [](const auto& row) {
                return std::all_of(
                    row.begin(), row.end(),
                    positive_zero);
            });
}

const LearnedPriorityBilinearParameters&
LearnedPriorityBilinear::parameters() const noexcept {
    return parameters_;
}

const std::string&
LearnedPriorityBilinear::digest() const noexcept {
    return digest_;
}

bool
LearnedPriorityBilinear::zero_action_projection() const noexcept {
    return zero_action_projection_;
}

std::vector<double>
LearnedPriorityBilinear::residuals(
    const std::vector<std::vector<double>>& option_rows,
    std::span<const std::size_t> canonical_order) const {
    if (option_rows.empty() ||
        canonical_order.size() != option_rows.size()) {
        throw std::invalid_argument(
            "AQ19 bilinear scoring requires nonempty aligned "
            "options and canonical order");
    }
    std::vector<bool> seen(option_rows.size(), false);
    for (const std::size_t index : canonical_order) {
        if (index >= option_rows.size() || seen[index]) {
            throw std::invalid_argument(
                "AQ19 bilinear canonical order is not a "
                "permutation");
        }
        seen[index] = true;
    }
    for (const auto& row : option_rows) {
        if (row.size() !=
            kLearnedPriorityBilinearPolicyFeatureCount ||
            !std::all_of(
                row.begin(), row.end(),
                [](double value) {
                    return std::isfinite(value);
                })) {
            throw std::invalid_argument(
                "AQ19 bilinear option row is invalid");
        }
    }
    const auto& state_row =
        option_rows[canonical_order.front()];
    for (const auto& row : option_rows) {
        for (std::size_t feature = 0;
             feature <
             kLearnedPriorityBilinearStateFeatureCount;
             ++feature) {
            if (std::bit_cast<std::uint64_t>(
                    row[feature]) !=
                std::bit_cast<std::uint64_t>(
                    state_row[feature])) {
                throw std::invalid_argument(
                    "AQ19 bilinear root state prefix changed "
                    "between legal actions");
            }
        }
    }
    if (zero_action_projection_) {
        return std::vector<double>(
            option_rows.size(), 0.0);
    }

    std::array<
        double,
        kLearnedPriorityBilinearActionFeatureCount>
        mean_action{};
    for (const std::size_t index : canonical_order) {
        for (std::size_t feature = 0;
             feature <
             kLearnedPriorityBilinearActionFeatureCount;
             ++feature) {
            mean_action[feature] +=
                option_rows[index]
                           [kLearnedPriorityBilinearStateFeatureCount +
                            feature];
        }
    }
    for (double& value : mean_action) {
        value /= static_cast<double>(
            option_rows.size());
    }

    std::array<double, kLearnedPriorityBilinearRank>
        hidden{};
    const auto& u0 = learned_priority_bilinear_u0();
    for (std::size_t rank = 0;
         rank < kLearnedPriorityBilinearRank; ++rank) {
        double preactivation = 0.0;
        for (std::size_t feature = 0;
             feature <
             kLearnedPriorityBilinearStateFeatureCount;
             ++feature) {
            preactivation +=
                (u0[rank][feature] +
                 parameters_.delta_u[rank][feature]) *
                state_row[feature];
        }
        hidden[rank] = std::tanh(preactivation);
    }

    std::vector<double> logits(option_rows.size(), 0.0);
    for (std::size_t action = 0;
         action < option_rows.size(); ++action) {
        for (std::size_t rank = 0;
             rank < kLearnedPriorityBilinearRank; ++rank) {
            double action_projection = 0.0;
            for (std::size_t feature = 0;
                 feature <
                 kLearnedPriorityBilinearActionFeatureCount;
                 ++feature) {
                const double centered =
                    option_rows[action]
                               [kLearnedPriorityBilinearStateFeatureCount +
                                feature] -
                    mean_action[feature];
                action_projection +=
                    parameters_.v[rank][feature] *
                    centered;
            }
            logits[action] +=
                hidden[rank] * action_projection;
        }
    }
    double mean_logit = 0.0;
    for (const std::size_t index : canonical_order) {
        mean_logit += logits[index];
    }
    mean_logit /=
        static_cast<double>(option_rows.size());

    std::vector<double> result(option_rows.size());
    for (std::size_t action = 0;
         action < option_rows.size(); ++action) {
        const double residual =
            kLearnedPriorityBilinearResidualWeight *
            std::tanh(logits[action] - mean_logit);
        if (!std::isfinite(residual) ||
            residual <
                -kLearnedPriorityBilinearResidualWeight ||
            residual >
                kLearnedPriorityBilinearResidualWeight) {
            throw std::runtime_error(
                "AQ19 bilinear residual is invalid");
        }
        result[action] =
            residual == 0.0 ? 0.0 : residual;
    }
    return result;
}

bool learned_priority_bilinear_equivalent(
    const std::shared_ptr<const LearnedPriorityBilinear>& first,
    const std::shared_ptr<const LearnedPriorityBilinear>& second) {
    if (!first || !second) {
        return !first && !second;
    }
    return bit_identical(
        first->parameters(), second->parameters());
}

} // namespace old_school
