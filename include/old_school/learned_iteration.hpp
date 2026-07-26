#pragma once

#include "old_school/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace old_school::learned_iteration {

// Every stochastic part of an iteration receives a seed derived from its
// semantic domain and stable indices. Adding work in one domain therefore
// cannot shift the random stream used by another domain.
enum class SeedDomain : std::uint64_t {
    SelfPlayGame = 0x53454c46504c4159ULL,
    PrioritySearch = 0x5052494f52495459ULL,
    AttackSearch = 0x41545441434b5352ULL,
    PriorityChoice = 0x5052494f43484345ULL,
    AttackChoice = 0x41544b43484f4943ULL,
    CriticFit = 0x4352495449434649ULL,
    PolicyFit = 0x504f4c4943594649ULL,
    ReplayShuffle = 0x5245504c41595348ULL,
};

std::uint64_t derive_seed(
    std::uint64_t root_seed, SeedDomain domain,
    std::uint64_t generation, std::uint64_t index,
    std::uint64_t subindex = 0) noexcept;

inline constexpr std::size_t kBalancedDeckCount = kDeckCount;
inline constexpr std::size_t kBalancedPairings =
    kBalancedDeckCount * (kBalancedDeckCount - 1) / 2;
inline constexpr std::size_t kBalancedGamesPerPairing = 4;
inline constexpr std::size_t kBalancedScheduleGames =
    kBalancedPairings * kBalancedGamesPerPairing;

struct ScheduledGame {
    std::size_t schedule_index = 0;
    std::size_t pairing_index = 0;
    std::array<DeckId, 2> seat_decks = {
        DeckId::Green,
        DeckId::Red,
    };
    std::size_t starting_player = 0;
    std::uint64_t seed = 0;

    bool operator==(const ScheduledGame&) const = default;
};

// One block contains each of the ten unordered five-deck pairs in both seat
// orientations and with each seat starting once: 10 * 2 * 2 = 40 games.
std::array<ScheduledGame, kBalancedScheduleGames>
balanced_schedule(std::uint64_t root_seed, std::uint64_t generation,
                  std::uint64_t block_index = 0);

// Learned critics are win probabilities: win=1, loss=0, draw=0.5.
double terminal_value_for_perspective(
    int winner, std::size_t perspective);

// `chronological_values[t]` is V(s_t) for one player's chronologically
// ordered decision states. With no intermediate reward, the target for the
// final state is terminal_z and earlier targets use
//   G_t = (1-lambda) * V(s_{t+1}) + lambda * G_{t+1}.
// V(s_0) is intentionally not part of its own target.
std::vector<double> td_lambda_targets(
    std::span<const double> chronological_values,
    double terminal_z, double lambda);

// Returns exactly min(total, cap) chronological indices. When truncation is
// required, index i is retained precisely when
//   floor((i + 1) * cap / total) != floor(i * cap / total).
// The implementation avoids overflowing either product.
std::vector<std::size_t> evenly_spaced_retained_indices(
    std::size_t total, std::size_t cap);

inline constexpr double kP16ExplorationTeacherWeight = 0.90;
inline constexpr double kP16ExplorationTemperature = 0.10;
inline constexpr double kP16TdLambda = 0.90;
inline constexpr double kP16TerminalReturnWeight = 0.50;
inline constexpr double kP16AdvantageLimit = 0.50;
inline constexpr double kP16AdvantageTemperature = 0.25;
inline constexpr double kP16MechanismMovementTolerance = 1.0e-12;
inline constexpr double kP16MechanismKlDefinedTolerance = 1.0e-15;
inline constexpr double kP16ResidualSaturationThreshold = 0.95;

// Converts finite action scores into the P16 collection behavior:
// 90% stable softmax at temperature 0.10 plus 10% uniform exploration.
// The result is nonempty, normalized, and gives every action positive mass.
std::vector<double> p16_exploration_distribution(
    std::span<const double> scores);

struct P16OutcomeSignal {
    std::vector<double> returns;
    std::vector<double> advantages;

    bool operator==(const P16OutcomeSignal&) const = default;
};

// Builds the P16 outcome signal from one actor's chronological frozen-parent
// baselines. G_lambda is computed by td_lambda_targets; each returned target
// is the equal blend of G_lambda and terminal_z. Advantages are return minus
// the corresponding baseline, clamped to [-0.5, +0.5].
P16OutcomeSignal p16_outcome_signal(
    std::span<const double> chronological_baselines,
    double terminal_z, double lambda = kP16TdLambda);

// Tilts a normalized all-action behavior distribution only at `chosen` by
// exp(advantage / 0.25), then renormalizes. A zero advantage returns an exact
// copy of the input distribution.
std::vector<double> p16_all_action_target(
    std::span<const double> behavior_distribution,
    std::size_t chosen, double advantage);

// One card-agnostic retained root for P-family mechanism accounting. Combined
// scores are Q plus the bounded Priority residual. The evaluator converts
// both score vectors through the canonical 90/10 P16 distribution.
struct P16MechanismObservation {
    std::vector<double> parent_combined_scores;
    std::vector<double> candidate_combined_scores;
    std::vector<double> candidate_centered_policy_logits;
    std::vector<double> target_probabilities;
    std::size_t chosen = 0;
    double advantage = 0.0;
    double weight = 1.0;

    bool operator==(
        const P16MechanismObservation&) const = default;
};

struct P16MechanismMetrics {
    std::size_t observation_count = 0;
    std::size_t residual_option_count = 0;
    std::size_t saturated_residual_count = 0;
    double total_weight = 0.0;

    double parent_kl = 0.0;
    double candidate_kl = 0.0;
    bool kl_reduction_defined = false;
    double kl_reduction_fraction = 0.0;

    double positive_advantage_weight = 0.0;
    double negative_advantage_weight = 0.0;
    double zero_advantage_weight = 0.0;
    double conflict_weight = 0.0;

    double eligible_signed_movement_weight = 0.0;
    double correct_signed_movement_weight = 0.0;
    double signed_movement_correct_rate = 0.0;
    double eligible_positive_movement_weight = 0.0;
    double correct_positive_movement_weight = 0.0;
    double positive_movement_correct_rate = 0.0;
    double eligible_negative_movement_weight = 0.0;
    double correct_negative_movement_weight = 0.0;
    double negative_movement_correct_rate = 0.0;

    double changed_argmax_weight = 0.0;
    double changed_argmax_weight_fraction = 0.0;

    // Each option receives weight / legal-option-count, so every retained
    // root contributes exactly its root weight regardless of branching.
    double residual_option_weight = 0.0;
    double saturated_residual_weight = 0.0;
    double residual_saturation_fraction = 0.0;

    bool operator==(const P16MechanismMetrics&) const = default;
};

// Computes newest-shard P-family mechanism diagnostics. An empty span returns
// zero metrics with undefined KL reduction. Malformed observations fail
// closed with invalid_argument.
P16MechanismMetrics evaluate_p16_mechanism_metrics(
    std::span<const P16MechanismObservation> observations);

// One root for the diagnostic centered-tanh capacity oracle. `base_scores`
// are the frozen scores before adding a residual. The target and inverse-root
// weight are the same quantities used by P-family cross-entropy fitting.
struct CenteredTanhOracleObservation {
    std::vector<double> base_scores;
    std::vector<double> target_probabilities;
    double weight = 1.0;

    bool operator==(
        const CenteredTanhOracleObservation&) const = default;
};

struct CenteredTanhOracleConfig {
    double residual_weight = 0.10;
    double policy_temperature = 0.10;
    double policy_mixture_weight = 0.90;
    double saturation_threshold =
        kP16ResidualSaturationThreshold;

    bool operator==(
        const CenteredTanhOracleConfig&) const = default;
};

struct CenteredTanhOracleBound {
    // Best feasible weighted KL found by the deterministic multistart
    // projected-gradient solver. This supplies a constructive lower bound on
    // the reduction achievable by independent per-root residuals.
    double numerical_best_kl = 0.0;
    double achievable_reduction_fraction = 0.0;

    // A data-processing lower bound on every feasible KL, after relaxing the
    // centered residuals to independent box-constrained score shifts. This
    // supplies a certified upper bound on achievable reduction.
    double certified_kl_lower_bound = 0.0;
    double certified_reduction_upper_bound = 0.0;

    double maximum_abs_squashed_residual = 0.0;
    std::size_t roots_reaching_iteration_limit = 0;

    bool operator==(
        const CenteredTanhOracleBound&) const = default;
};

struct CenteredTanhRootwiseOracleMetrics {
    std::size_t observation_count = 0;
    double total_weight = 0.0;
    double parent_kl = 0.0;
    bool reduction_defined = false;

    // `full_range` permits centered logits in [-12,+12], where tanh is within
    // 8e-11 of its mathematical range. `zero_saturation` additionally
    // requires every abs(tanh(centered_logit)) to be strictly below the
    // configured saturation threshold.
    CenteredTanhOracleBound full_range;
    CenteredTanhOracleBound zero_saturation;

    bool operator==(
        const CenteredTanhRootwiseOracleMetrics&) const = default;
};

// Diagnostic-only capacity bracket for a frozen P-family shard. Each root is
// solved independently, so the result excludes shared-head interference.
//
// The numerical side is exactly reproducible: fixed deterministic starts,
// Euclidean projection onto the zero-sum centered-logit box by 96 bisections,
// at most 512 projected-gradient iterations per start, and a 48-step Armijo
// backtrack (initial step 1, factor 1/2, coefficient 1e-4). The full-range
// centered-logit box is [-12,+12]. No RNG is used.
//
// The certified side relaxes centering and uses the maximum possible
// per-action probability interval under independent bounded score shifts.
// Bernoulli data processing then lower-bounds the root KL. Consequently:
//
//   achievable_reduction_fraction
//       <= true independent-root optimum
//       <= certified_reduction_upper_bound.
//
// Empty input returns zero metrics with undefined reductions. Malformed input
// or configuration fails closed with invalid_argument.
CenteredTanhRootwiseOracleMetrics
evaluate_centered_tanh_rootwise_oracle(
    std::span<const CenteredTanhOracleObservation> observations,
    CenteredTanhOracleConfig config = {});

// Exact n-recorded-state bootstrap. For state i, when state i+distance
// exists, the target is the equal blend of terminal_z and
// V_parent(i+distance). The final `distance` states (or the entire trace when
// it is shorter) retain the terminal target alone. Distance must be nonzero.
std::vector<double> n_state_bootstrap_targets(
    std::span<const double> chronological_parent_values,
    double terminal_z, std::size_t distance);

// Value G8's exact four-recorded-state bootstrap. Kept as the canonical
// training API and as a compatibility wrapper around
// n_state_bootstrap_targets(..., 4).
std::vector<double> four_state_bootstrap_targets(
    std::span<const double> chronological_parent_values,
    double terminal_z);

// Exact one-based G5-G8 Late-Mix50 assignment. The result is pure and
// consumes no RNG: each consecutive pair is raw first, searched second.
constexpr bool value_g8_mix50_game_uses_search(
    std::size_t published_generation,
    std::size_t zero_based_game_index) noexcept {
    return published_generation >= 5 &&
           published_generation <= 8 &&
           zero_based_game_index % 2 == 1;
}

inline constexpr std::size_t kReplayWindowGenerations = 3;

// Generations are immutable after insertion. Snapshots share const storage,
// so repeated ensemble/member training cannot move examples out of the
// replay window, and an evicted generation remains valid in old snapshots.
template <typename Example>
class ReplayWindow {
  public:
    using ImmutableExamples =
        std::shared_ptr<const std::vector<Example>>;

    struct Generation {
        std::uint64_t index = 0;
        ImmutableExamples examples;
    };

    void append_generation(
        std::uint64_t generation,
        const std::vector<Example>& examples) {
        append_storage(
            generation,
            std::make_shared<const std::vector<Example>>(
                examples));
    }

    void append_generation(
        std::uint64_t generation,
        std::vector<Example>&& examples) {
        append_storage(
            generation,
            std::make_shared<const std::vector<Example>>(
                std::move(examples)));
    }

    [[nodiscard]] std::size_t generation_count() const noexcept {
        return generations_.size();
    }

    [[nodiscard]] std::size_t example_count() const noexcept {
        std::size_t count = 0;
        for (const auto& generation : generations_) {
            count += generation.examples->size();
        }
        return count;
    }

    [[nodiscard]] std::vector<Generation> snapshot() const {
        return {generations_.begin(), generations_.end()};
    }

    template <typename Visitor>
    void for_each(Visitor&& visitor) const {
        for (const auto& generation : generations_) {
            for (const auto& example : *generation.examples) {
                std::invoke(
                    visitor, generation.index, example);
            }
        }
    }

  private:
    void append_storage(
        std::uint64_t generation,
        ImmutableExamples examples) {
        if (!generations_.empty() &&
            generation <= generations_.back().index) {
            throw std::invalid_argument(
                "replay generations must be strictly increasing");
        }
        if (generations_.size() ==
            kReplayWindowGenerations) {
            generations_.pop_front();
        }
        generations_.push_back({
            .index = generation,
            .examples = std::move(examples),
        });
    }

    std::deque<Generation> generations_;
};

} // namespace old_school::learned_iteration
