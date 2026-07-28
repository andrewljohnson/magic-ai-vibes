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
inline constexpr std::uint64_t kPd0StageE0Seed =
    202607280803ULL;
inline constexpr std::size_t kPd0StageE0Repetitions = 1;
inline constexpr std::uint64_t kPd0StageE1Seed =
    202607280804ULL;
inline constexpr std::size_t kPd0StageE1Repetitions = 4;
inline constexpr double kPd0BlendAlpha = 0.50;

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

// The two EXPLORE-2 candidates are represented by alpha zero (exact
// C16+PD0) and alpha 0.50 (the half-blend+PD0). Ties prefer alpha zero.
double select_pd0_winner_alpha(
    const std::array<CandidateScore, 2>& scores);

// Returns the exact K8/H4/R1 C16 deployment recipe with only the explicit
// Pass-dominance switch selected by the caller.
BotConfig make_exploratory_bot(
    std::shared_ptr<const LearnedModel> model,
    bool pass_dominance);

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

} // namespace old_school::fq4_blend_explore
