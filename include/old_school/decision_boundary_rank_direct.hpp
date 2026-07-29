#pragma once

#include "old_school/decision_boundary_critic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace old_school::decision_boundary_rank_direct {

namespace dbc = decision_boundary_critic;

inline constexpr std::size_t kFeatureCount =
    dbc::kCriticFeatureCount;
inline constexpr std::size_t kLeafCount = 2;
inline constexpr std::uint64_t kFitTag =
    202607291401ULL;
inline constexpr std::size_t kAdamSteps = 256;
inline constexpr double kAdamLearningRate = 0.001;
inline constexpr double kAdamBetaOne = 0.9;
inline constexpr double kAdamBetaTwo = 0.999;
inline constexpr double kAdamEpsilon = 1.0e-8;
inline constexpr double kGlobalGradientNormClip = 5.0;
inline constexpr double kListwiseTemperature = 0.10;
inline constexpr double kListwiseMix = 0.90;
inline constexpr double kL2Tether = 0.10;
inline constexpr double kStablePairMinimumDelta = 0.03;
inline constexpr double kStablePairNormal95CriticalValue = 1.96;
inline constexpr double kMaximumDevSuccessorBceIncrease = 0.005;
inline constexpr double kMaximumDevDeckRegretIncrease =
    dbc::kMaximumDevDeckRegretIncrease;

static_assert(
    kFeatureCount ==
    kLearnedCriticObservationFeatureCount);
static_assert(kLeafCount == kLearnedCriticLeafCount);

// DBC2 deliberately consumes only an actor-local observation, the two
// frozen-C16 leaf predictions on that observation, and the teacher return.
// It never retains a GameState or any opponent hidden-zone identity.
struct RankCell {
    std::vector<double> observation;
    std::array<double, kLeafCount> parent_leaf_values{
        0.5, 0.5};
    double teacher_target = 0.5;
    bool terminal_before_boundary = false;

    bool operator==(const RankCell&) const = default;
};

struct RankAction {
    std::vector<RankCell> worlds;

    bool operator==(const RankAction&) const = default;
};

struct RankRoot {
    std::string stable_root_id;
    DeckId deck = DeckId::Green;
    std::vector<RankAction> actions;

    bool operator==(const RankRoot&) const = default;
};

struct Dataset {
    std::vector<RankRoot> roots;
    std::array<std::size_t, kDeckCount> roots_by_deck{};

    bool operator==(const Dataset&) const = default;
};

struct Corpus {
    Dataset train;
    Dataset dev;
    std::string source_digest;
    LearnedModelComponentFingerprints parent_components;

    bool operator==(const Corpus&) const = default;
};

struct DeckMetrics {
    DeckId deck = DeckId::Green;
    std::size_t roots = 0;
    std::size_t eligible_cells = 0;
    std::size_t stable_pairs = 0;
    double successor_bce = 0.0;
    double successor_brier = 0.0;
    double successor_bias = 0.0;
    double successor_ece = 0.0;
    double listwise_cross_entropy = 0.0;
    double top_one_expected_agreement = 0.0;
    double stable_pair_agreement = 0.0;
    double mean_regret = 0.0;

    bool operator==(const DeckMetrics&) const = default;
};

struct Metrics {
    std::array<DeckMetrics, kDeckCount> decks{};
    std::size_t roots = 0;
    std::size_t eligible_cells = 0;
    std::size_t stable_pairs = 0;
    double equal_deck_successor_bce = 0.0;
    double equal_deck_successor_brier = 0.0;
    double equal_deck_successor_bias = 0.0;
    double equal_deck_successor_ece = 0.0;
    double equal_deck_listwise_cross_entropy = 0.0;
    double equal_deck_top_one_expected_agreement = 0.0;
    double equal_deck_stable_pair_agreement = 0.0;
    double equal_deck_mean_regret = 0.0;

    bool operator==(const Metrics&) const = default;
};

struct OptimizerConfig {
    std::uint64_t fit_tag = kFitTag;
    std::size_t steps = kAdamSteps;
    double learning_rate = kAdamLearningRate;
    double beta_one = kAdamBetaOne;
    double beta_two = kAdamBetaTwo;
    double epsilon = kAdamEpsilon;
    double temperature = kListwiseTemperature;
    double mix = kListwiseMix;
    double l2_tether = kL2Tether;
    double global_gradient_norm_clip =
        kGlobalGradientNormClip;

    bool operator==(const OptimizerConfig&) const = default;
};

struct OptimizerReport {
    OptimizerConfig config;
    std::vector<double> delta;
    std::size_t completed_steps = 0;
    double initial_objective = 0.0;
    double final_objective = 0.0;
    double delta_l2_norm = 0.0;
    double final_gradient_l2_norm = 0.0;
    double maximum_preclip_gradient_l2_norm = 0.0;
    std::size_t clipped_steps = 0;
    Metrics before;
    Metrics after;

    bool operator==(const OptimizerReport&) const = default;
};

struct ExactEvaluationReport {
    std::string parent_fingerprint;
    std::string candidate_fingerprint;
    Metrics parent_train;
    Metrics candidate_train;
    Metrics parent_dev;
    Metrics candidate_dev;
    double maximum_surrogate_engine_cell_difference = 0.0;

    bool operator==(const ExactEvaluationReport&) const = default;
};

struct ModelIsolationReport {
    std::shared_ptr<const LearnedModel> candidate;
    std::string parent_fingerprint_before;
    std::string parent_fingerprint_after;
    std::string candidate_fingerprint;
    std::string repeated_candidate_fingerprint;
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    LearnedCriticTensorFingerprints parent_critic_tensors;
    LearnedCriticTensorFingerprints candidate_critic_tensors;
    bool parent_immutable = false;
    bool repeated_application_bit_identical = false;
    bool critic_changed = false;
    bool input_hidden_frozen = false;
    bool output_layer_frozen = false;
    bool context_direct_path_frozen = false;
    bool direct_path_changed = false;
    bool shared_delta_exact = false;
    std::size_t changed_coordinates = 0;
    bool all_policy_heads_frozen = false;

    bool passed() const;
};

struct OfflineGate {
    bool repeated_optimizer_bit_identical = false;
    bool optimizer_recipe_exact = false;
    bool objective_strictly_improved = false;
    bool surrogate_engine_agreement = false;
    bool exact_model_identity = false;
    bool model_isolation_passed = false;
    bool train_listwise_strictly_improved = false;
    bool train_regret_strictly_improved = false;
    bool dev_listwise_strictly_improved = false;
    bool dev_regret_strictly_improved = false;
    bool dev_top_one_non_decreasing = false;
    bool dev_stable_pair_non_decreasing = false;
    bool dev_successor_bce_guard = false;
    std::array<bool, kDeckCount> dev_deck_regret_guard{};
    std::vector<std::string> failures;

    bool passed() const;
    bool operator==(const OfflineGate&) const = default;
};

// Converts the cached owner-safe DBC1 successor corpus into action groups.
// Loaded cache cells have no boundary_state; this projection intentionally
// scores their neutral observations directly through the frozen parent.
Corpus project_corpus(
    const dbc::Corpus& source,
    std::shared_ptr<const LearnedModel> parent);

void validate_dataset(const Dataset& dataset);
void validate_corpus(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);

// A shared direct-path delta d changes both critic leaves by
//   logit(V_leaf(x)) <- logit(V_leaf(x)) + dot(d, x).
// Terminal-before-boundary cells remain fixed at their aligned utility.
double candidate_cell_value(
    const RankCell& cell,
    std::span<const double> delta);
Metrics evaluate(
    const Dataset& dataset,
    std::span<const double> delta,
    double temperature = kListwiseTemperature,
    double mix = kListwiseMix);
// Exact post-fit metrics: nonterminal cells are scored through the immutable
// applied model, avoiding logit-inversion roundoff in the analytic optimizer
// surrogate. Promotion gates must use this function for both parent and
// candidate.
Metrics evaluate_model(
    const Dataset& dataset,
    std::shared_ptr<const LearnedModel> model,
    double temperature = kListwiseTemperature,
    double mix = kListwiseMix);
ExactEvaluationReport evaluate_exact(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    std::span<const double> delta);

// Deterministic full-batch Adam. The sole trainable object is one shared
// kFeatureCount-vector; no shuffle, random sample, policy tensor, critic
// trunk, or output layer enters this optimizer.
OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config = {});

// Applies the fitted vector through the narrow engine seam and verifies the
// intended component boundary. The engine operation is required to add the
// same vector to both leaves' direct paths.
ModelIsolationReport apply_delta(
    std::shared_ptr<const LearnedModel> parent,
    std::span<const double> delta);

OfflineGate evaluate_offline_gate(
    const OptimizerReport& fit,
    const OptimizerReport& repeated_fit,
    const ExactEvaluationReport& exact,
    const ModelIsolationReport& isolation);

namespace testing {

// Pure constructor used by synthetic optimizer tests. Production always
// enters through project_corpus and the authenticated DBC1 cache.
Dataset make_dataset(std::vector<RankRoot> roots);
Corpus make_corpus(
    Dataset train, Dataset dev,
    LearnedModelComponentFingerprints parent_components);

} // namespace testing

} // namespace old_school::decision_boundary_rank_direct
