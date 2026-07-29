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
    kLearnedPriorityBilinearIdentifier =
        "AQ19-DBC6-R2-BILINEAR";
inline constexpr std::string_view
    kLearnedPriorityBilinearRequiredFingerprint =
        "68126afc5a3e3757eb1d510a056585aa974c4f54ce1b4a789ff430f1c7413e2f";
inline constexpr std::uint64_t
    kLearnedPriorityBilinearFitTag = UINT64_C(202607291901);
inline constexpr std::size_t
    kLearnedPriorityBilinearRank = 2;
inline constexpr std::size_t
    kLearnedPriorityBilinearStateFeatureCount = 674;
inline constexpr std::size_t
    kLearnedPriorityBilinearActionFeatureCount = 219;
inline constexpr std::size_t
    kLearnedPriorityBilinearPolicyFeatureCount =
        kLearnedPriorityBilinearStateFeatureCount +
        kLearnedPriorityBilinearActionFeatureCount;
inline constexpr std::size_t
    kLearnedPriorityBilinearParameterCount =
        kLearnedPriorityBilinearRank *
        (kLearnedPriorityBilinearStateFeatureCount +
         kLearnedPriorityBilinearActionFeatureCount);
inline constexpr double
    kLearnedPriorityBilinearResidualWeight = 0.10;
inline constexpr std::size_t
    kLearnedPriorityBilinearWorlds = 8;

static_assert(
    kLearnedPriorityBilinearPolicyFeatureCount == 893);
static_assert(
    kLearnedPriorityBilinearParameterCount == 1786);

using LearnedPriorityBilinearStateProjection =
    std::array<
        std::array<
            double,
            kLearnedPriorityBilinearStateFeatureCount>,
        kLearnedPriorityBilinearRank>;
using LearnedPriorityBilinearActionProjection =
    std::array<
        std::array<
            double,
            kLearnedPriorityBilinearActionFeatureCount>,
        kLearnedPriorityBilinearRank>;

struct LearnedPriorityBilinearParameters {
    LearnedPriorityBilinearStateProjection delta_u{};
    LearnedPriorityBilinearActionProjection v{};

    bool operator==(
        const LearnedPriorityBilinearParameters&) const =
        default;
};

// The immutable, declaration-bound state projection. Trained state
// parameters are deltas around this fixed nonzero projection.
const LearnedPriorityBilinearStateProjection&
learned_priority_bilinear_u0();

// Canonical, platform-independent parameter bytes. The AQ19 research runner
// hashes these bytes cryptographically; the lightweight runtime deliberately
// has no artifact-integrity dependency.
std::string learned_priority_bilinear_canonical_bytes(
    const LearnedPriorityBilinearParameters& parameters);

// Strict inverse of the canonical representation above. The decoder rejects
// every schema, recipe, length, floating-point, and signed-zero drift.
LearnedPriorityBilinearParameters
learned_priority_bilinear_parameters_from_canonical_bytes(
    std::string_view bytes);

class LearnedPriorityBilinear {
  public:
    explicit LearnedPriorityBilinear(
        LearnedPriorityBilinearParameters parameters);

    const LearnedPriorityBilinearParameters&
    parameters() const noexcept;
    const std::string& digest() const noexcept;
    bool zero_action_projection() const noexcept;

    // `canonical_order` contains each row index exactly once, in the engine's
    // authoritative typed-action order. Results retain caller row order.
    std::vector<double> residuals(
        const std::vector<std::vector<double>>& option_rows,
        std::span<const std::size_t> canonical_order) const;

  private:
    LearnedPriorityBilinearParameters parameters_;
    std::string digest_;
    bool zero_action_projection_ = false;
};

// Semantic equality for benchmark policy-distinctness checks. Pointer
// identity alone is insufficient because separately allocated identical
// parameter objects are the same policy.
bool learned_priority_bilinear_equivalent(
    const std::shared_ptr<const LearnedPriorityBilinear>& first,
    const std::shared_ptr<const LearnedPriorityBilinear>& second);

} // namespace old_school
