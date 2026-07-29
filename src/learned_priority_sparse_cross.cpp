#include "old_school/learned_priority_sparse_cross.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace old_school {
namespace {

LearnedPrioritySparseCrossTerms normalize_and_validate(
    LearnedPrioritySparseCrossTerms terms) {
    if (!terms.empty() &&
        terms.size() !=
            kLearnedPrioritySparseCrossTermCount) {
        throw std::invalid_argument(
            "AQ20 sparse-cross object must contain exactly "
            "zero or sixteen terms");
    }
    for (std::size_t index = 0; index < terms.size();
         ++index) {
        auto& term = terms[index];
        if (term.state_feature >=
                kLearnedPrioritySparseCrossStateFeatureCount ||
            term.action_feature >=
                kLearnedPrioritySparseCrossActionFeatureCount) {
            throw std::invalid_argument(
                "AQ20 sparse-cross coordinate is out of "
                "range");
        }
        if (!std::isfinite(term.sigma) ||
            term.sigma <= 0.0 ||
            !std::isfinite(term.beta) ||
            std::abs(term.beta) > 1.0) {
            throw std::invalid_argument(
                "AQ20 sparse-cross parameters must be finite "
                "with positive sigma and beta in [-1,1]");
        }
        if (term.beta == 0.0) {
            term.beta = 0.0;
        }
        for (std::size_t previous = 0;
             previous < index; ++previous) {
            if (terms[previous].state_feature ==
                    term.state_feature &&
                terms[previous].action_feature ==
                    term.action_feature) {
                throw std::invalid_argument(
                    "AQ20 sparse-cross coordinates must be "
                    "unique");
            }
        }
    }
    return terms;
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
        input.size() - cursor <
            sizeof(std::uint64_t)) {
        throw std::invalid_argument(
            "AQ20 canonical parameter bytes are truncated");
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
    // runtime identity. Artifact authentication belongs to the research
    // publisher rather than this small scoring object.
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

void validate_canonical_order(
    std::size_t action_count,
    std::span<const std::size_t> canonical_order) {
    if (action_count == 0 ||
        canonical_order.size() != action_count) {
        throw std::invalid_argument(
            "AQ20 sparse-cross scoring requires nonempty "
            "aligned actions and canonical order");
    }
    std::vector<bool> seen(action_count, false);
    for (const std::size_t index : canonical_order) {
        if (index >= action_count || seen[index]) {
            throw std::invalid_argument(
                "AQ20 sparse-cross canonical order is not a "
                "permutation");
        }
        seen[index] = true;
    }
}

template <typename ActionValue>
std::vector<double> evaluate_residuals(
    const LearnedPrioritySparseCrossTerms& terms,
    std::span<
        const double,
        kLearnedPrioritySparseCrossStateFeatureCount>
        state,
    std::size_t action_count,
    std::span<const std::size_t> canonical_order,
    ActionValue action_value) {
    if (terms.empty()) {
        return std::vector<double>(action_count, 0.0);
    }

    std::array<
        double,
        kLearnedPrioritySparseCrossTermCount>
        mean_action{};
    for (std::size_t term_index = 0;
         term_index < terms.size(); ++term_index) {
        const auto& term = terms[term_index];
        double sum = 0.0;
        for (const std::size_t action :
             canonical_order) {
            sum += action_value(
                action, term.action_feature);
            if (!std::isfinite(sum)) {
                throw std::runtime_error(
                    "AQ20 sparse-cross action mean is "
                    "nonfinite");
            }
        }
        mean_action[term_index] =
            sum / static_cast<double>(action_count);
    }

    std::vector<double> logits(action_count, 0.0);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        double logit = 0.0;
        for (std::size_t term_index = 0;
             term_index < terms.size(); ++term_index) {
            const auto& term = terms[term_index];
            const double centered_action =
                action_value(
                    action, term.action_feature) -
                mean_action[term_index];
            const double phi =
                state[term.state_feature] *
                centered_action / term.sigma;
            const double contribution =
                term.beta * phi;
            if (!std::isfinite(centered_action) ||
                !std::isfinite(phi) ||
                !std::isfinite(contribution)) {
                throw std::runtime_error(
                    "AQ20 sparse-cross standardized term is "
                    "nonfinite");
            }
            logit += contribution;
            if (!std::isfinite(logit)) {
                throw std::runtime_error(
                    "AQ20 sparse-cross logit is nonfinite");
            }
        }
        logits[action] = logit;
    }

    double mean_logit = 0.0;
    for (const std::size_t action :
         canonical_order) {
        mean_logit += logits[action];
        if (!std::isfinite(mean_logit)) {
            throw std::runtime_error(
                "AQ20 sparse-cross centered logit mean is "
                "nonfinite");
        }
    }
    mean_logit /=
        static_cast<double>(action_count);

    std::vector<double> result(action_count);
    for (std::size_t action = 0;
         action < action_count; ++action) {
        const double residual =
            kLearnedPrioritySparseCrossResidualWeight *
            std::tanh(logits[action] - mean_logit);
        if (!std::isfinite(residual) ||
            residual <
                -kLearnedPrioritySparseCrossResidualWeight ||
            residual >
                kLearnedPrioritySparseCrossResidualWeight) {
            throw std::runtime_error(
                "AQ20 sparse-cross residual is invalid");
        }
        result[action] =
            residual == 0.0 ? 0.0 : residual;
    }
    return result;
}

bool bit_identical(
    const LearnedPrioritySparseCrossTerms& first,
    const LearnedPrioritySparseCrossTerms& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.size();
         ++index) {
        if (first[index].state_feature !=
                second[index].state_feature ||
            first[index].action_feature !=
                second[index].action_feature ||
            std::bit_cast<std::uint64_t>(
                first[index].sigma) !=
                std::bit_cast<std::uint64_t>(
                    second[index].sigma) ||
            std::bit_cast<std::uint64_t>(
                first[index].beta) !=
                std::bit_cast<std::uint64_t>(
                    second[index].beta)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string learned_priority_sparse_cross_canonical_bytes(
    const LearnedPrioritySparseCrossTerms& input_terms) {
    const auto terms =
        normalize_and_validate(input_terms);
    std::string result;
    result.append(kLearnedPrioritySparseCrossSchema);
    result.push_back('\n');
    result.append(kLearnedPrioritySparseCrossIdentifier);
    result.push_back('\n');
    append_unsigned64(
        result, kLearnedPrioritySparseCrossFitTag);
    append_unsigned64(
        result, kLearnedPrioritySparseCrossTermCount);
    append_unsigned64(
        result,
        kLearnedPrioritySparseCrossStateFeatureCount);
    append_unsigned64(
        result,
        kLearnedPrioritySparseCrossActionFeatureCount);
    append_unsigned64(
        result,
        std::bit_cast<std::uint64_t>(
            kLearnedPrioritySparseCrossResidualWeight));
    append_unsigned64(
        result,
        static_cast<std::uint64_t>(terms.size()));
    for (const auto& term : terms) {
        append_unsigned64(
            result,
            static_cast<std::uint64_t>(
                term.state_feature));
        append_unsigned64(
            result,
            static_cast<std::uint64_t>(
                term.action_feature));
        append_unsigned64(
            result,
            std::bit_cast<std::uint64_t>(
                term.sigma));
        append_unsigned64(
            result,
            std::bit_cast<std::uint64_t>(
                term.beta));
    }
    return result;
}

LearnedPrioritySparseCrossTerms
learned_priority_sparse_cross_terms_from_canonical_bytes(
    std::string_view bytes) {
    const std::size_t text_bytes =
        kLearnedPrioritySparseCrossSchema.size() + 1 +
        kLearnedPrioritySparseCrossIdentifier.size() + 1;
    constexpr std::size_t kHeaderWords = 6;
    const std::size_t minimum_bytes =
        text_bytes +
        kHeaderWords * sizeof(std::uint64_t);
    if (bytes.size() < minimum_bytes ||
        !bytes.starts_with(
            kLearnedPrioritySparseCrossSchema) ||
        bytes[kLearnedPrioritySparseCrossSchema.size()] !=
            '\n' ||
        bytes.substr(
                 kLearnedPrioritySparseCrossSchema.size() +
                     1)
                .starts_with(
                    kLearnedPrioritySparseCrossIdentifier) ==
            false ||
        bytes[text_bytes - 1] != '\n') {
        throw std::invalid_argument(
            "AQ20 canonical parameter bytes have the wrong "
            "schema or length");
    }

    std::size_t cursor = text_bytes;
    if (read_unsigned64(bytes, cursor) !=
            kLearnedPrioritySparseCrossFitTag ||
        read_unsigned64(bytes, cursor) !=
            kLearnedPrioritySparseCrossTermCount ||
        read_unsigned64(bytes, cursor) !=
            kLearnedPrioritySparseCrossStateFeatureCount ||
        read_unsigned64(bytes, cursor) !=
            kLearnedPrioritySparseCrossActionFeatureCount ||
        read_unsigned64(bytes, cursor) !=
            std::bit_cast<std::uint64_t>(
                kLearnedPrioritySparseCrossResidualWeight)) {
        throw std::invalid_argument(
            "AQ20 canonical parameter recipe drifted");
    }
    const std::uint64_t encoded_count =
        read_unsigned64(bytes, cursor);
    if (encoded_count != 0 &&
        encoded_count !=
            kLearnedPrioritySparseCrossTermCount) {
        throw std::invalid_argument(
            "AQ20 canonical term count is invalid");
    }
    constexpr std::size_t kTermWords = 4;
    const std::size_t expected_bytes =
        minimum_bytes +
        static_cast<std::size_t>(encoded_count) *
            kTermWords * sizeof(std::uint64_t);
    if (bytes.size() != expected_bytes) {
        throw std::invalid_argument(
            "AQ20 canonical parameter bytes have the wrong "
            "length");
    }

    LearnedPrioritySparseCrossTerms terms;
    terms.reserve(
        static_cast<std::size_t>(encoded_count));
    for (std::uint64_t index = 0;
         index < encoded_count; ++index) {
        const std::uint64_t state_feature =
            read_unsigned64(bytes, cursor);
        const std::uint64_t action_feature =
            read_unsigned64(bytes, cursor);
        if (state_feature >
                std::numeric_limits<std::size_t>::max() ||
            action_feature >
                std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument(
                "AQ20 canonical coordinate is not "
                "representable");
        }
        terms.push_back({
            .state_feature =
                static_cast<std::size_t>(
                    state_feature),
            .action_feature =
                static_cast<std::size_t>(
                    action_feature),
            .sigma =
                std::bit_cast<double>(
                    read_unsigned64(bytes, cursor)),
            .beta =
                std::bit_cast<double>(
                    read_unsigned64(bytes, cursor)),
        });
    }
    if (cursor != bytes.size() ||
        learned_priority_sparse_cross_canonical_bytes(
            terms) != bytes) {
        throw std::invalid_argument(
            "AQ20 canonical parameter bytes are not "
            "canonical");
    }
    return terms;
}

LearnedPrioritySparseCross::LearnedPrioritySparseCross(
    LearnedPrioritySparseCrossTerms terms)
    : terms_(normalize_and_validate(std::move(terms))) {
    digest_ = local_digest(
        learned_priority_sparse_cross_canonical_bytes(
            terms_));
}

const LearnedPrioritySparseCrossTerms&
LearnedPrioritySparseCross::terms() const noexcept {
    return terms_;
}

const std::string&
LearnedPrioritySparseCross::digest() const noexcept {
    return digest_;
}

bool LearnedPrioritySparseCross::empty() const noexcept {
    return terms_.empty();
}

std::vector<double>
LearnedPrioritySparseCross::residuals(
    const std::vector<std::vector<double>>& option_rows,
    std::span<const std::size_t> canonical_order) const {
    validate_canonical_order(
        option_rows.size(), canonical_order);
    for (const auto& row : option_rows) {
        if (row.size() !=
                kLearnedPrioritySparseCrossPolicyFeatureCount ||
            !std::all_of(
                row.begin(), row.end(),
                [](double value) {
                    return std::isfinite(value);
                })) {
            throw std::invalid_argument(
                "AQ20 sparse-cross option row is invalid");
        }
    }
    const auto& state_row =
        option_rows[canonical_order.front()];
    for (const auto& row : option_rows) {
        for (std::size_t feature = 0;
             feature <
             kLearnedPrioritySparseCrossStateFeatureCount;
             ++feature) {
            if (std::bit_cast<std::uint64_t>(
                    row[feature]) !=
                std::bit_cast<std::uint64_t>(
                    state_row[feature])) {
                throw std::invalid_argument(
                    "AQ20 sparse-cross root state prefix "
                    "changed between legal actions");
            }
        }
    }
    const std::span<
        const double,
        kLearnedPrioritySparseCrossStateFeatureCount>
        state(
            state_row.data(),
            kLearnedPrioritySparseCrossStateFeatureCount);
    return evaluate_residuals(
        terms_, state, option_rows.size(),
        canonical_order,
        [&option_rows](
            std::size_t action,
            std::size_t feature) {
            return option_rows[action]
                [kLearnedPrioritySparseCrossStateFeatureCount +
                 feature];
        });
}

std::vector<double>
LearnedPrioritySparseCross::residuals(
    std::span<
        const double,
        kLearnedPrioritySparseCrossStateFeatureCount>
        state,
    std::span<const LearnedPrioritySparseCrossAction>
        action_features,
    std::span<const std::size_t>
        canonical_order) const {
    validate_canonical_order(
        action_features.size(), canonical_order);
    if (!std::all_of(
            state.begin(), state.end(),
            [](double value) {
                return std::isfinite(value);
            }) ||
        !std::all_of(
            action_features.begin(),
            action_features.end(),
            [](const auto& row) {
                return std::all_of(
                    row.begin(), row.end(),
                    [](double value) {
                        return std::isfinite(value);
                    });
            })) {
        throw std::invalid_argument(
            "AQ20 sparse-cross projected features are "
            "invalid");
    }
    return evaluate_residuals(
        terms_, state, action_features.size(),
        canonical_order,
        [&action_features](
            std::size_t action,
            std::size_t feature) {
            return action_features[action][feature];
        });
}

bool learned_priority_sparse_cross_equivalent(
    const std::shared_ptr<
        const LearnedPrioritySparseCross>& first,
    const std::shared_ptr<
        const LearnedPrioritySparseCross>& second) {
    const bool first_is_control =
        !first || first->empty();
    const bool second_is_control =
        !second || second->empty();
    if (first_is_control || second_is_control) {
        return first_is_control && second_is_control;
    }
    return bit_identical(
        first->terms(), second->terms());
}

} // namespace old_school
