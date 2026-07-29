#pragma once

#include "old_school/decision_boundary_rank_direct.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace old_school::decision_boundary_rank_hidden {

namespace direct = decision_boundary_rank_direct;

inline constexpr std::size_t kFeatureCount =
    direct::kFeatureCount;
inline constexpr std::size_t kLeafCount =
    kLearnedCriticLeafCount;
inline constexpr std::size_t kHiddenCount =
    kLearnedCriticHiddenCount;
inline constexpr std::size_t kTrainableCoordinateCount =
    kLeafCount * kHiddenCount;
inline constexpr std::uint64_t kFitTag =
    202607291501ULL;
inline constexpr std::uint64_t kSelectorSeed =
    202607291511ULL;
inline constexpr std::size_t kAdamSteps =
    direct::kAdamSteps;
inline constexpr double kAdamLearningRate =
    direct::kAdamLearningRate;
inline constexpr double kAdamBetaOne =
    direct::kAdamBetaOne;
inline constexpr double kAdamBetaTwo =
    direct::kAdamBetaTwo;
inline constexpr double kAdamEpsilon =
    direct::kAdamEpsilon;
inline constexpr double kGlobalGradientNormClip =
    direct::kGlobalGradientNormClip;
inline constexpr double kListwiseTemperature =
    direct::kListwiseTemperature;
inline constexpr double kListwiseMix =
    direct::kListwiseMix;
inline constexpr double kL2Tether =
    direct::kL2Tether;

static_assert(kFeatureCount == 674);
static_assert(kLeafCount == 2);
static_assert(kHiddenCount == 16);
static_assert(kTrainableCoordinateCount == 32);

using Delta = LearnedCriticHiddenOutputParameters;
using Metrics = direct::Metrics;
using OfflineGate = direct::OfflineGate;

// The source DBC2 cell preserves the owner-safe observation, frozen parent
// leaf probabilities, teacher target, and terminal semantics. Hidden
// activations are frozen per leaf at projection time; terminal cells retain
// no critic activation.
struct RankHiddenCell {
    direct::RankCell source;
    LearnedCriticHiddenActivations hidden{};

    bool operator==(const RankHiddenCell&) const = default;
};

struct RankHiddenAction {
    std::vector<RankHiddenCell> worlds;

    bool operator==(const RankHiddenAction&) const = default;
};

struct RankHiddenRoot {
    std::string stable_root_id;
    DeckId deck = DeckId::Green;
    std::vector<RankHiddenAction> actions;

    bool operator==(const RankHiddenRoot&) const = default;
};

struct Dataset {
    std::vector<RankHiddenRoot> roots;
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
    Delta delta{};
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
    bool topology_frozen = false;
    bool input_hidden_frozen = false;
    bool hidden_output_changed = false;
    bool output_bias_frozen = false;
    bool direct_path_frozen = false;
    bool context_direct_path_frozen = false;
    bool independent_delta_exact = false;
    std::size_t changed_coordinates = 0;
    bool all_policy_heads_frozen = false;

    bool passed() const;
};

// Adds exact per-leaf hidden activations to the already authenticated DBC2
// owner-safe projection. No GameState or opponent hidden identity is retained.
Corpus project_corpus(
    const direct::Corpus& source,
    std::shared_ptr<const LearnedModel> parent);

void validate_dataset(const Dataset& dataset);
void validate_corpus(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent);

double candidate_cell_value(
    const RankHiddenCell& cell,
    const Delta& delta);
Metrics evaluate(
    const Dataset& dataset,
    const Delta& delta,
    double temperature = kListwiseTemperature,
    double mix = kListwiseMix);
Metrics evaluate_model(
    const Dataset& dataset,
    std::shared_ptr<const LearnedModel> model,
    double temperature = kListwiseTemperature,
    double mix = kListwiseMix);
ExactEvaluationReport evaluate_exact(
    const Corpus& corpus,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const Delta& delta);

OptimizerReport optimize(
    const Dataset& train,
    OptimizerConfig config = {});

ModelIsolationReport apply_delta(
    std::shared_ptr<const LearnedModel> parent,
    const Delta& delta);

OfflineGate evaluate_offline_gate(
    const OptimizerReport& fit,
    const OptimizerReport& repeated_fit,
    const ExactEvaluationReport& exact,
    const ModelIsolationReport& isolation);

namespace testing {

struct ObjectiveProbe {
    double objective = 0.0;
    Delta gradient{};

    bool operator==(const ObjectiveProbe&) const = default;
};

Dataset make_dataset(
    std::vector<RankHiddenRoot> roots);
Corpus make_corpus(
    Dataset train, Dataset dev,
    LearnedModelComponentFingerprints parent_components);
ObjectiveProbe objective_probe(
    const Dataset& dataset,
    const Delta& delta);

} // namespace testing

} // namespace old_school::decision_boundary_rank_hidden
