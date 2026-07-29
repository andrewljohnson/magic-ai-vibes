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
inline constexpr std::uint64_t
    kAdversarialBlocksStageE0Seed = 202607280805ULL;
inline constexpr std::size_t
    kAdversarialBlocksStageE0Repetitions = 1;
inline constexpr std::uint64_t
    kAdversarialBlocksStageE1Seed = 202607280806ULL;
inline constexpr std::size_t
    kAdversarialBlocksStageE1Repetitions = 4;
inline constexpr double kAdversarialBlocksBlendAlpha = 0.50;
inline constexpr std::uint64_t
    kAdversarialCompositionStageE0Seed = 202607280807ULL;
inline constexpr std::size_t
    kAdversarialCompositionStageE0Repetitions = 1;
inline constexpr std::uint64_t
    kAdversarialCompositionStageE1Seed = 202607280808ULL;
inline constexpr std::size_t
    kAdversarialCompositionStageE1Repetitions = 4;
inline constexpr double kAdversarialCompositionBlendAlpha = 0.50;
inline constexpr std::uint64_t kStackDisciplineSeed =
    202607280809ULL;
inline constexpr std::size_t kStackDisciplineRepetitions = 1;
inline constexpr std::uint64_t kLearnedStackCombatSeed =
    202607280810ULL;
inline constexpr std::size_t kLearnedStackCombatRepetitions = 1;
inline constexpr double kLearnedStackCombatBlendAlpha = 0.50;
inline constexpr std::uint64_t kDualBoundarySeed =
    202607281731ULL;
inline constexpr std::size_t kDualBoundaryRepetitions = 1;
inline constexpr double kDualBoundaryResolvedWeight = 0.75;

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

double select_adversarial_blocks_winner_alpha(
    const std::array<CandidateScore, 2>& scores);

// EXPLORE-4 advances only on strictly more wins than losses. Draws do not
// relax the gate.
bool adversarial_composition_advances(
    const CandidateScore& score);

// Builds the EXPLORE-4 direct matchup. Both policies use the exact
// defender-best-response attack treatment; only their frozen model pointers
// differ.
std::array<BotConfig, 2> make_adversarial_composition_bots(
    std::shared_ptr<const LearnedModel> challenger_model,
    std::shared_ptr<const LearnedModel> baseline_model);

// EXPLORE-5 advances the PD0 composition only when it has a strict winning
// record and at least as many wins as attack-only on the common coordinate.
// Equal arm win counts favor PD0.
bool stack_discipline_advances(
    const CandidateScore& attack_only,
    const CandidateScore& pass_dominance);

// Returns attack-only, attack+PD0, and ordinary-C16 recipes in that order.
// All three share the same exact frozen model and deployment configuration.
std::array<BotConfig, 3> make_stack_discipline_bots(
    std::shared_ptr<const LearnedModel> model);

// EXPLORE-6 runs its comparator only after the treatment wins strictly
// against ordinary C16. Draws do not relax the short-circuit gate.
bool learned_stack_combat_runs_comparator(
    const CandidateScore& treatment);

// After the strict-win gate, the treatment advances when it wins at least as
// many games as the common-coordinate no-PD0 comparator. Equal win counts
// favor the treatment carrying the deterministic stack repair.
bool learned_stack_combat_advances(
    const CandidateScore& treatment,
    const CandidateScore& comparator);

// Returns treatment (alpha blend+attack+PD0), comparator (same blend+attack),
// and ordinary-C16 baseline recipes in that order.
std::array<BotConfig, 3> make_learned_stack_combat_bots(
    std::shared_ptr<const LearnedModel> blended_model,
    std::shared_ptr<const LearnedModel> baseline_model);

// EXPLORE-11 advances only when the alpha-0.75 dual-boundary treatment wins
// strictly more than half of its exact 60-game schedule against ordinary C16.
bool dual_boundary_advances(const CandidateScore& treatment);

// Returns alpha-0.75 dual-boundary C16 and ordinary C16 in that order. The
// frozen model and every other deployment setting are identical, including
// absolute K8/H4/R1 search and zero Priority residual.
std::array<BotConfig, 2> make_dual_boundary_bots(
    std::shared_ptr<const LearnedModel> model);

// Returns the exact K8/H4/R1 C16 deployment recipe with only the explicit
// exploratory switches selected by the caller.
BotConfig make_exploratory_bot(
    std::shared_ptr<const LearnedModel> model,
    bool pass_dominance,
    bool adversarial_blocks = false,
    double resolved_shallow_prior_weight = 0.0);

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error);

} // namespace old_school::fq4_blend_explore
