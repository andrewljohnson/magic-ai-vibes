#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace old_school {

inline constexpr std::string_view
    kLearnedPrioritySparseCrossIdentifier =
        "AQ20-DBC6-S16-SPARSE-CROSS";
inline constexpr std::string_view
    kLearnedPrioritySparseCrossSchema =
        "old-school-aq20-sparse-cross-v1";
inline constexpr std::string_view
    kLearnedPrioritySparseCrossRequiredFingerprint =
        "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::uint64_t
    kLearnedPrioritySparseCrossFitTag =
        UINT64_C(202607292001);
inline constexpr std::size_t
    kLearnedPrioritySparseCrossStateFeatureCount = 674;
inline constexpr std::size_t
    kLearnedPrioritySparseCrossActionFeatureCount = 219;
inline constexpr std::size_t
    kLearnedPrioritySparseCrossPolicyFeatureCount =
        kLearnedPrioritySparseCrossStateFeatureCount +
        kLearnedPrioritySparseCrossActionFeatureCount;
inline constexpr std::size_t
    kLearnedPrioritySparseCrossTermCount = 16;
inline constexpr double
    kLearnedPrioritySparseCrossResidualWeight = 0.10;
inline constexpr std::size_t
    kLearnedPrioritySparseCrossWorlds = 8;

static_assert(
    kLearnedPrioritySparseCrossPolicyFeatureCount == 893);

using LearnedPrioritySparseCrossState =
    std::array<
        double,
        kLearnedPrioritySparseCrossStateFeatureCount>;
using LearnedPrioritySparseCrossAction =
    std::array<
        double,
        kLearnedPrioritySparseCrossActionFeatureCount>;

struct LearnedPrioritySparseCrossTerm {
    std::size_t state_feature = 0;
    std::size_t action_feature = 0;
    double sigma = 1.0;
    double beta = 0.0;

    bool operator==(
        const LearnedPrioritySparseCrossTerm&) const =
        default;
};

using LearnedPrioritySparseCrossTerms =
    std::vector<LearnedPrioritySparseCrossTerm>;

// Canonical, platform-independent term bytes. Numerical zero is normalized
// to positive zero. The decoder rejects schema, recipe, length,
// floating-point, duplicate-coordinate, and signed-zero drift.
std::string learned_priority_sparse_cross_canonical_bytes(
    const LearnedPrioritySparseCrossTerms& terms);

LearnedPrioritySparseCrossTerms
learned_priority_sparse_cross_terms_from_canonical_bytes(
    std::string_view bytes);

class LearnedPrioritySparseCross {
  public:
    // Only the exact deployed sixteen-term object and the empty C16 control
    // object are valid runtime shapes.
    explicit LearnedPrioritySparseCross(
        LearnedPrioritySparseCrossTerms terms);

    const LearnedPrioritySparseCrossTerms&
    terms() const noexcept;
    const std::string& digest() const noexcept;
    bool empty() const noexcept;

    // Each policy row is the exact 674-state/219-action projection. The
    // state prefix must be bit-identical across all legal actions.
    std::vector<double> residuals(
        const std::vector<std::vector<double>>& option_rows,
        std::span<const std::size_t> canonical_order) const;

    // Research-only projection seam. Action rows retain caller order, while
    // `canonical_order` contains every action index exactly once in the
    // authoritative typed-action order.
    std::vector<double> residuals(
        std::span<
            const double,
            kLearnedPrioritySparseCrossStateFeatureCount>
            state,
        std::span<const LearnedPrioritySparseCrossAction>
            action_features,
        std::span<const std::size_t> canonical_order) const;

  private:
    LearnedPrioritySparseCrossTerms terms_;
    std::string digest_;
};

// Semantic equality for benchmark policy-distinctness checks.
bool learned_priority_sparse_cross_equivalent(
    const std::shared_ptr<
        const LearnedPrioritySparseCross>& first,
    const std::shared_ptr<
        const LearnedPrioritySparseCross>& second);

} // namespace old_school
