#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <vector>

namespace old_school::fq4_blend_explore {

inline constexpr std::array<double, 4> kCandidateAlphas{
    0.25, 0.50, 0.75, 1.00,
};
inline constexpr std::uint64_t kStageE0Seed =
    202607280801ULL;
inline constexpr std::size_t kStageE0Repetitions = 1;
inline constexpr std::uint64_t kStageE1Seed =
    202607280802ULL;
inline constexpr std::size_t kStageE1Repetitions = 4;

struct CandidateScore {
    double alpha = 0.0;
    std::size_t wins = 0;
    std::size_t losses = 0;
    std::size_t draws = 0;
};

// Interpolates only the outer Priority head. Alpha zero and one return the
// exact endpoint model pointers; every other model retains the parent's
// critic, Attack, Block, and DamageOrder components bit-for-bit.
std::shared_ptr<const LearnedModel> blend_priority_heads(
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    double alpha);

// Ranks by total wins, then prefers the smaller alpha on a tie.
std::array<double, 2> select_top_two_alphas(
    const std::vector<CandidateScore>& scores);

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

} // namespace old_school::fq4_blend_explore
