#include "old_school/fq4_neutral_candidate_publisher.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace old_school::fq4_neutral_candidate_publisher {
namespace {

namespace artifact = fq4_dev_candidate_artifact;
namespace bundle = fq4_dev_bundle;
namespace dev = fq4_dev_evaluator;
namespace evaluator = fq4_neutral_evaluator;
namespace integrity = artifact_integrity;
namespace neutral = fq4_neutral_supplement;

constexpr std::uint64_t kParentArtifactBytes =
    3'111'437;
constexpr std::uint32_t kPriorityHiddenCount = 32;
constexpr std::uint32_t kPriorityFeatureCount = 893;
constexpr std::uint64_t kPriorityParameterCount = 29'534;
constexpr std::uint64_t kFitExamples = 248;
constexpr std::uint64_t kFitOptions = 987;
constexpr std::size_t kPositiveFitExamples = 88;
constexpr std::size_t kPositiveFitOptions = 548;
constexpr std::size_t kPositiveCheckExamples = 94;
constexpr std::size_t kPositiveCheckOptions = 571;
constexpr std::size_t kNeutralRowsPerDeck = 32;
constexpr std::size_t kNeutralRowsPerSplit = 160;
constexpr std::size_t kNeutralFitOptions = 439;
constexpr std::string_view kPositiveFitInputSha256 =
    "586b121c3c9bdb1a61305cac86882cd2"
    "0b5d2ba332b4d5a54defc2c7756393a1";
constexpr std::array<std::size_t, bundle::kDeckCount>
    kNeutralLossMassByDeck{11, 4, 31, 13, 29};
constexpr LearnedValuePriorityHeadUpdateConfig
    kFrozenOptimizer{
        .batch_size = 64,
        .epochs = 16,
        .learning_rate = 0.001,
        .beta1 = 0.9,
        .beta2 = 0.999,
        .epsilon = 1.0e-8,
        .global_gradient_norm_clip = 5.0,
        .seed = 202607280212ULL,
        .residual_weight = 0.10,
        .policy_temperature = 0.10,
    };

[[noreturn]] void fail(std::string_view category) {
    throw std::runtime_error(std::string(category));
}

bool same_real(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool same_reals(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!same_real(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool same_real_matrices(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!same_reals(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool same_optimizer(
    const LearnedValuePriorityHeadUpdateConfig& first,
    const LearnedValuePriorityHeadUpdateConfig& second) {
    return
        first.batch_size == second.batch_size &&
        first.epochs == second.epochs &&
        same_real(
            first.learning_rate,
            second.learning_rate) &&
        same_real(first.beta1, second.beta1) &&
        same_real(first.beta2, second.beta2) &&
        same_real(first.epsilon, second.epsilon) &&
        same_real(
            first.global_gradient_norm_clip,
            second.global_gradient_norm_clip) &&
        first.seed == second.seed &&
        same_real(
            first.residual_weight,
            second.residual_weight) &&
        same_real(
            first.policy_temperature,
            second.policy_temperature);
}

bool same_deployment(
    const artifact::DeploymentRecipe& first,
    const artifact::DeploymentRecipe& second) {
    return
        first == second &&
        same_real(
            first.root_exploration,
            second.root_exploration) &&
        same_real(
            first.continuation_epsilon,
            second.continuation_epsilon) &&
        same_real(
            first.priority_residual_weight,
            second.priority_residual_weight);
}

bool same_contract(
    const artifact::Contract& first,
    const artifact::Contract& second) {
    return
        first == second &&
        same_optimizer(
            first.fit.optimizer,
            second.fit.optimizer) &&
        same_deployment(
            first.deployment, second.deployment);
}

bool same_artifact_report(
    const artifact::Report& first,
    const artifact::Report& second) {
    return
        first == second &&
        same_contract(
            first.manifest.contract,
            second.manifest.contract);
}

bool same_priority_parameters(
    const LearnedPriorityHeadParameters& first,
    const LearnedPriorityHeadParameters& second) {
    return
        same_real_matrices(
            first.input_hidden, second.input_hidden) &&
        same_reals(
            first.hidden_bias, second.hidden_bias) &&
        same_reals(
            first.hidden_output, second.hidden_output) &&
        same_reals(first.direct, second.direct) &&
        same_real(
            first.output_bias, second.output_bias);
}

bool canonical_digest(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return
                       (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
               }) &&
           std::any_of(
               value.begin(), value.end(),
               [](char character) {
                   return character != '0';
               });
}

bool canonical_commit(std::string_view value) {
    return
        value.size() == 40 &&
        std::all_of(
            value.begin(), value.end(),
            [](char character) {
                return
                    (character >= '0' &&
                     character <= '9') ||
                    (character >= 'a' &&
                     character <= 'f');
            }) &&
        std::any_of(
            value.begin(), value.end(),
            [](char character) {
                return character != '0';
            });
}

bool expected_parent_components(
    const LearnedModelComponentFingerprints& components) {
    return
        components.critic ==
            "2982b155a02a4a2a3ce8442ae28f6d8c"
            "f7829103e538c60f0625b3332502e568" &&
        components.priority ==
            "32dc6688a5c970e3eda4325bea5ee4190"
            "77027e160697899e3b00c963fa1bb22" &&
        components.attack ==
            "dfd3aaa16755bee5d0c2c40956851b94"
            "ef5676a271a602eb23a57719f7358b01" &&
        components.block ==
            "d64e40796bd1587958b7386996e6a1e5"
            "660778d40ec7b40b0ee6324b8e39adbb" &&
        components.damage_order ==
            "f0a84ed549bbf95197dd00c13ab04c0a"
            "4f6b1771f14bdb30a7dca937d2d79c76";
}

bool same_nonpriority_components(
    const LearnedModelComponentFingerprints& parent,
    const LearnedModelComponentFingerprints& candidate) {
    return
        parent.critic == candidate.critic &&
        parent.attack == candidate.attack &&
        parent.block == candidate.block &&
        parent.damage_order ==
            candidate.damage_order;
}

void validate_frozen_production_contract(
    const artifact::Contract& contract) {
    if (contract.family != kFamily ||
        contract.environment != kEnvironment ||
        contract.parent.artifact_bytes !=
            kParentArtifactBytes ||
        contract.parent.artifact_sha256 !=
            "53aeb904bd87311b37201859317f05ab0"
            "66bdfe134c72460cf94bff6d1f944ca" ||
        contract.parent.model_fingerprint !=
            "68126afc5a3e3757eb1d510a056585aa9"
            "74c4f54ce1b4a789ff430f1c7413e2f" ||
        !expected_parent_components(
            contract.parent.components) ||
        contract.parent.training_games !=
            800 ||
        contract.parent.training_seed !=
            424242 ||
        contract.parent.generation !=
            16 ||
        contract.corpus.artifact_bytes !=
            2'250'909 ||
        contract.corpus.artifact_sha256 !=
            "0911fc2eb8b14ddc9165543eb1e4c4ed"
            "b0b058256a58dedf61f6c4ea4ca859df" ||
        contract.fit.input_sha256 !=
            kFitInputSha256 ||
        contract.fit.examples != kFitExamples ||
        contract.fit.options != kFitOptions ||
        contract.fit.check_examples != 0 ||
        contract.fit.background_only_examples != 0 ||
        contract.fit.optimizer_calls != 1 ||
        !same_optimizer(
            contract.fit.optimizer, kFrozenOptimizer) ||
        contract.candidate_model_fingerprint !=
            kCandidateFingerprint ||
        contract.priority_hidden_count !=
            kPriorityHiddenCount ||
        contract.priority_feature_count !=
            kPriorityFeatureCount ||
        contract.priority_parameter_count !=
            kPriorityParameterCount ||
        contract.deployment.variant !=
            LearnedVariant::ValueSearchChampion ||
        contract.deployment.training_games !=
            800 ||
        contract.deployment.worlds_per_action != 8 ||
        contract.deployment.horizon_turns != 4 ||
        contract.deployment.rollouts_per_world != 1 ||
        contract.deployment.root_search_depth != 1 ||
        !contract.deployment.shallow_prior ||
        !same_real(
            contract.deployment.root_exploration,
            0.0) ||
        !same_real(
            contract.deployment.continuation_epsilon,
            0.0) ||
        !same_real(
            contract.deployment.priority_residual_weight,
            0.10) ||
        contract.deployment.pass_dominance ||
        contract.deployment.continuation_controller !=
            LearnedContinuationController::Legacy ||
        contract.deployment.max_turns != 500) {
        fail("production_contract_mismatch");
    }
}

void validate_frozen_input(
    const testing::FrozenInput& input) {
    if (input.path.empty() ||
        input.path.filename().empty() ||
        input.bytes == 0 ||
        !canonical_digest(input.sha256)) {
        fail("invalid_frozen_input");
    }
}

void validate_recipe(
    const testing::Recipe& recipe) {
    if (!canonical_commit(recipe.producer_commit)) {
        fail("invalid_publisher_recipe");
    }
    validate_frozen_input(recipe.dev1);
    validate_frozen_input(recipe.parent);
    validate_frozen_input(recipe.positive_candidate);
    validate_frozen_input(recipe.neutral);
    const std::array<const std::filesystem::path*, 7> paths{
        &recipe.executable_path,
        &recipe.dev1.path,
        &recipe.parent.path,
        &recipe.positive_candidate.path,
        &recipe.neutral.path,
        &recipe.destination_path,
        &recipe.temporary_path,
    };
    for (const std::filesystem::path* path : paths) {
        if (path->empty() ||
            path->filename().empty() ||
            path->string().find('\0') !=
                std::string::npos) {
            fail("invalid_publisher_recipe");
        }
    }
    for (std::size_t first = 0;
         first < paths.size(); ++first) {
        for (std::size_t second = first + 1;
             second < paths.size(); ++second) {
            if (paths[first]->lexically_normal() ==
                paths[second]->lexically_normal()) {
                fail("invalid_publisher_recipe");
            }
        }
    }
    if (recipe.temporary_path !=
        artifact::temporary_path_for(
            recipe.destination_path)) {
        fail("invalid_publisher_recipe");
    }
}

void validate_dependencies(
    const testing::Dependencies& dependencies) {
    if (!dependencies.snapshot ||
        !dependencies.path_absent ||
        !dependencies.load_dev1 ||
        !dependencies.prepare_dev1 ||
        !dependencies.load_parent ||
        !dependencies.load_positive_candidate ||
        !dependencies.load_neutral ||
        !dependencies.evaluate ||
        !dependencies.publish ||
        !dependencies.reload) {
        fail("incomplete_publisher_dependencies");
    }
}

void require_absent(
    const testing::Recipe& recipe,
    const testing::Dependencies& dependencies) {
    if (!dependencies.path_absent(
            recipe.destination_path) ||
        !dependencies.path_absent(
            recipe.temporary_path)) {
        fail("publication_coordinate_not_new");
    }
}

void require_source_identity(
    const testing::FrozenInput& expected,
    const integrity::RegularFileSnapshot& observed) {
    if (observed.byte_size != expected.bytes ||
        observed.sha256 != expected.sha256) {
        fail("source_identity_mismatch");
    }
}

dev::FitAccounting expected_fit_accounting(
    const artifact::FitBoundary& fit) {
    if (fit.examples >
            std::numeric_limits<std::size_t>::max() ||
        fit.options >
            std::numeric_limits<std::size_t>::max() ||
        fit.check_examples >
            std::numeric_limits<std::size_t>::max() ||
        fit.background_only_examples >
            std::numeric_limits<std::size_t>::max() ||
        fit.optimizer_calls >
            std::numeric_limits<std::size_t>::max()) {
        fail("fit_accounting_out_of_range");
    }
    return {
        .fit_examples =
            static_cast<std::size_t>(fit.examples),
        .fit_options =
            static_cast<std::size_t>(fit.options),
        .check_examples =
            static_cast<std::size_t>(
                fit.check_examples),
        .background_only_examples =
            static_cast<std::size_t>(
                fit.background_only_examples),
        .optimizer_calls =
            static_cast<std::size_t>(
                fit.optimizer_calls),
        .training_input_sha256 = fit.input_sha256,
        .optimizer = fit.optimizer,
    };
}

bool same_fit_accounting(
    const dev::FitAccounting& first,
    const dev::FitAccounting& second) {
    return
        first == second &&
        same_optimizer(
            first.optimizer, second.optimizer);
}

bool same_training_example(
    const LearnedValuePriorityTrainingExample& first,
    const LearnedValuePriorityTrainingExample& second) {
    return
        same_real_matrices(
            first.options, second.options) &&
        same_reals(
            first.base_scores, second.base_scores) &&
        same_reals(
            first.target_probabilities,
            second.target_probabilities) &&
        same_real(first.weight, second.weight);
}

bool same_training_examples(
    const std::vector<
        LearnedValuePriorityTrainingExample>& first,
    const std::vector<
        LearnedValuePriorityTrainingExample>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!same_training_example(
                first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool same_training_batch_accounting(
    const evaluator::TrainingBatch& first,
    const evaluator::TrainingBatch& second) {
    if (!same_fit_accounting(
            first.accounting, second.accounting) ||
        first.positive_examples !=
            second.positive_examples ||
        first.positive_options !=
            second.positive_options ||
        first.neutral_examples !=
            second.neutral_examples ||
        first.neutral_options !=
            second.neutral_options ||
        first.neutral_examples_by_deck !=
            second.neutral_examples_by_deck) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (std::bit_cast<std::uint64_t>(
                first.neutral_loss_mass_by_deck[deck]) !=
            std::bit_cast<std::uint64_t>(
                second.neutral_loss_mass_by_deck[deck])) {
            return false;
        }
    }
    return true;
}

bool same_training_batch(
    const evaluator::TrainingBatch& first,
    const evaluator::TrainingBatch& second) {
    return
        same_training_batch_accounting(first, second) &&
        same_training_examples(
            first.examples, second.examples);
}

bool same_margin_metrics(
    const dev::MarginMetrics& first,
    const dev::MarginMetrics& second) {
    return
        first.roots == second.roots &&
        first.constraints == second.constraints &&
        same_real(
            first.mean_root_margin,
            second.mean_root_margin) &&
        same_real(
            first.minimum_margin,
            second.minimum_margin);
}

bool same_support_metrics(
    const dev::SupportViolationMetrics& first,
    const dev::SupportViolationMetrics& second) {
    return
        first.violating_roots ==
            second.violating_roots &&
        first.positive_roots ==
            second.positive_roots &&
        same_real(first.fraction, second.fraction);
}

bool same_deck_metrics(
    const dev::DeckMetrics& first,
    const dev::DeckMetrics& second) {
    return
        first.positive_roots ==
            second.positive_roots &&
        first.positive_options ==
            second.positive_options &&
        same_real(
            first.target_to_parent_kl,
            second.target_to_parent_kl) &&
        same_real(
            first.target_to_candidate_kl,
            second.target_to_candidate_kl) &&
        same_margin_metrics(
            first.parent_margins,
            second.parent_margins) &&
        same_margin_metrics(
            first.candidate_margins,
            second.candidate_margins) &&
        same_support_metrics(
            first.parent_support_violations,
            second.parent_support_violations) &&
        same_support_metrics(
            first.candidate_support_violations,
            second.candidate_support_violations) &&
        first.parent_classes ==
            second.parent_classes &&
        first.candidate_classes ==
            second.candidate_classes &&
        first.transitions == second.transitions &&
        first.repairs == second.repairs &&
        first.regressions == second.regressions;
}

bool same_split_metrics(
    const dev::SplitMetrics& first,
    const dev::SplitMetrics& second) {
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (!same_deck_metrics(
                first.decks[deck],
                second.decks[deck])) {
            return false;
        }
    }
    return
        first.positive_roots ==
            second.positive_roots &&
        first.positive_options ==
            second.positive_options &&
        same_real(
            first.deck_balanced_target_to_parent_kl,
            second.deck_balanced_target_to_parent_kl) &&
        same_real(
            first.deck_balanced_target_to_candidate_kl,
            second.deck_balanced_target_to_candidate_kl) &&
        same_real(
            first.deck_balanced_parent_mean_margin,
            second.deck_balanced_parent_mean_margin) &&
        same_real(
            first.deck_balanced_candidate_mean_margin,
            second.deck_balanced_candidate_mean_margin) &&
        same_real(
            first.pooled_parent_minimum_margin,
            second.pooled_parent_minimum_margin) &&
        same_real(
            first.pooled_candidate_minimum_margin,
            second.pooled_candidate_minimum_margin) &&
        same_support_metrics(
            first.parent_support_violations,
            second.parent_support_violations) &&
        same_support_metrics(
            first.candidate_support_violations,
            second.candidate_support_violations) &&
        first.parent_classes ==
            second.parent_classes &&
        first.candidate_classes ==
            second.candidate_classes &&
        first.transitions == second.transitions &&
        first.repairs == second.repairs &&
        first.regressions == second.regressions;
}

bool same_evaluation_metrics(
    const dev::EvaluationMetrics& first,
    const dev::EvaluationMetrics& second) {
    return
        same_split_metrics(first.fit, second.fit) &&
        same_split_metrics(first.check, second.check) &&
        first.parent_anchor_rows ==
            second.parent_anchor_rows &&
        first.parent_anchor_actions ==
            second.parent_anchor_actions &&
        first.parent_anchors_exact ==
            second.parent_anchors_exact &&
        first.accounting == second.accounting;
}

bool same_model_evaluation(
    const dev::ModelEvaluationReport& first,
    const dev::ModelEvaluationReport& second) {
    return
        first.parent_fingerprint ==
            second.parent_fingerprint &&
        first.candidate_fingerprint ==
            second.candidate_fingerprint &&
        first.parent_components ==
            second.parent_components &&
        first.candidate_components ==
            second.candidate_components &&
        first.parent_immutable ==
            second.parent_immutable &&
        first.nonpriority_components_identical ==
            second.nonpriority_components_identical &&
        same_evaluation_metrics(
            first.metrics, second.metrics);
}

bool same_neutral_deck_metrics(
    const evaluator::NeutralDeckMetrics& first,
    const evaluator::NeutralDeckMetrics& second) {
    return
        first.rows == second.rows &&
        first.options == second.options &&
        same_real(
            first.baseline_parent_to_candidate_kl,
            second.baseline_parent_to_candidate_kl) &&
        same_real(
            first.anchored_parent_to_candidate_kl,
            second.anchored_parent_to_candidate_kl) &&
        first.baseline_exact_support_changes ==
            second.baseline_exact_support_changes &&
        first.anchored_exact_support_changes ==
            second.anchored_exact_support_changes;
}

bool same_neutral_metrics(
    const evaluator::NeutralDriftMetrics& first,
    const evaluator::NeutralDriftMetrics& second) {
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (!same_neutral_deck_metrics(
                first.decks[deck],
                second.decks[deck])) {
            return false;
        }
    }
    return
        first.rows == second.rows &&
        first.options == second.options &&
        same_real(
            first.baseline_equal_deck_kl,
            second.baseline_equal_deck_kl) &&
        same_real(
            first.anchored_equal_deck_kl,
            second.anchored_equal_deck_kl) &&
        first.baseline_exact_support_changes ==
            second.baseline_exact_support_changes &&
        first.anchored_exact_support_changes ==
            second.anchored_exact_support_changes &&
        first.finite_probabilities ==
            second.finite_probabilities;
}

bool all_isolation_checks_pass(
    const evaluator::IsolationChecks& checks) {
    return
        checks.parent_immutable &&
        checks.positive_only_candidate_exact &&
        checks.omitted_neutral_control_exact &&
        checks.parent_anchors_exact &&
        checks.hidden_repartition_contract_exact &&
        checks.fit_check_isolated &&
        checks.nonpriority_components_identical &&
        checks.priority_component_changed;
}

bool all_gate_checks_pass(
    const evaluator::GateReport& gate) {
    return
        gate.baseline_positive_contract_exact &&
        gate.check_positive_clean &&
        gate.fit_positive_preserved &&
        gate.neutral_baseline_nonzero &&
        gate.neutral_per_deck_nonworsening &&
        gate.neutral_kl_halved &&
        gate.neutral_support_changes_halved &&
        gate.isolation_exact &&
        gate.failures.empty();
}

bool same_scientific_report(
    const evaluator::Report& first,
    const evaluator::Report& second) {
    return
        first.parent_fingerprint ==
            second.parent_fingerprint &&
        first.positive_only_candidate_fingerprint ==
            second.positive_only_candidate_fingerprint &&
        first.anchored_candidate_fingerprint ==
            second.anchored_candidate_fingerprint &&
        first.parent_components ==
            second.parent_components &&
        first.positive_only_candidate_components ==
            second.positive_only_candidate_components &&
        first.anchored_candidate_components ==
            second.anchored_candidate_components &&
        same_training_batch(
            first.omitted_neutral_control,
            second.omitted_neutral_control) &&
        same_training_batch(
            first.anchored_training,
            second.anchored_training) &&
        same_model_evaluation(
            first.positive_only_evaluation,
            second.positive_only_evaluation) &&
        same_model_evaluation(
            first.anchored_evaluation,
            second.anchored_evaluation) &&
        same_neutral_metrics(
            first.neutral_check,
            second.neutral_check) &&
        first.isolation == second.isolation &&
        first.gate == second.gate;
}

bool exact_anchored_batch(
    const evaluator::TrainingBatch& batch,
    const dev::FitAccounting& expected) {
    if (!same_fit_accounting(
            batch.accounting, expected) ||
        batch.examples.size() != kFitExamples ||
        batch.positive_examples !=
            kPositiveFitExamples ||
        batch.positive_options !=
            kPositiveFitOptions ||
        batch.neutral_examples !=
            kNeutralRowsPerSplit ||
        batch.neutral_options != kNeutralFitOptions) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (batch.neutral_examples_by_deck[deck] !=
                kNeutralRowsPerDeck ||
            batch.neutral_loss_mass_by_deck[deck] !=
                static_cast<double>(
                    kNeutralLossMassByDeck[deck])) {
            return false;
        }
    }
    return true;
}

bool exact_control_batch(
    const evaluator::TrainingBatch& batch) {
    if (!(
        batch.accounting.fit_examples ==
            kPositiveFitExamples &&
        batch.accounting.fit_options ==
            kPositiveFitOptions &&
        batch.accounting.check_examples == 0 &&
        batch.accounting.background_only_examples == 0 &&
        batch.accounting.optimizer_calls == 1 &&
        batch.accounting.training_input_sha256 ==
            kPositiveFitInputSha256 &&
        same_optimizer(
            batch.accounting.optimizer,
            kFrozenOptimizer) &&
        batch.examples.size() ==
            kPositiveFitExamples &&
        batch.positive_examples ==
            kPositiveFitExamples &&
        batch.positive_options ==
            kPositiveFitOptions &&
        batch.neutral_examples == 0 &&
        batch.neutral_options == 0)) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (batch.neutral_examples_by_deck[deck] != 0 ||
            !same_real(
                batch.neutral_loss_mass_by_deck[deck],
                0.0)) {
            return false;
        }
    }
    return true;
}

bool exact_positive_census(
    const dev::EvaluationMetrics& metrics) {
    return
        metrics.fit.positive_roots ==
            kPositiveFitExamples &&
        metrics.fit.positive_options ==
            kPositiveFitOptions &&
        metrics.check.positive_roots ==
            kPositiveCheckExamples &&
        metrics.check.positive_options ==
            kPositiveCheckOptions &&
        metrics.parent_anchors_exact &&
        metrics.accounting.zero();
}

bool exact_neutral_check_census(
    const evaluator::NeutralDriftMetrics& metrics) {
    constexpr std::array<std::size_t, bundle::kDeckCount>
        kOptionsByDeck{82, 86, 80, 103, 87};
    if (metrics.rows != kNeutralRowsPerSplit ||
        metrics.options != 438 ||
        !metrics.finite_probabilities) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (metrics.decks[deck].rows !=
                kNeutralRowsPerDeck ||
            metrics.decks[deck].options !=
                kOptionsByDeck[deck]) {
            return false;
        }
    }
    return true;
}

bool exact_model_evaluation_identity(
    const dev::ModelEvaluationReport& report,
    std::string_view parent_fingerprint,
    const LearnedModelComponentFingerprints&
        parent_components,
    std::string_view candidate_fingerprint,
    const LearnedModelComponentFingerprints&
        candidate_components) {
    return
        report.parent_fingerprint ==
            parent_fingerprint &&
        report.candidate_fingerprint ==
            candidate_fingerprint &&
        report.parent_components ==
            parent_components &&
        report.candidate_components ==
            candidate_components &&
        report.parent_immutable &&
        report.nonpriority_components_identical &&
        exact_positive_census(report.metrics);
}

struct ValidatedCandidate {
    LearnedModelComponentFingerprints components;
    LearnedPriorityHeadParameters parameters;
};

void validate_parent_model(
    const artifact::Contract& contract,
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent ||
        learned_model_fingerprint(parent) !=
            contract.parent.model_fingerprint ||
        learned_model_component_fingerprints(parent) !=
            contract.parent.components) {
        fail("loaded_parent_contract_mismatch");
    }
}

ValidatedCandidate validate_evaluations(
    const artifact::Contract& contract,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> positive_candidate,
    const evaluator::Report& first,
    const evaluator::Report& second) {
    if (!parent || !positive_candidate ||
        !first.anchored_candidate ||
        !second.anchored_candidate) {
        fail("missing_evaluation_model");
    }
    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const LearnedModelComponentFingerprints
        parent_components =
            learned_model_component_fingerprints(parent);
    const std::string positive_candidate_fingerprint =
        learned_model_fingerprint(positive_candidate);
    const LearnedModelComponentFingerprints
        positive_candidate_components =
            learned_model_component_fingerprints(
                positive_candidate);
    const std::string first_fingerprint =
        learned_model_fingerprint(
            first.anchored_candidate);
    const std::string second_fingerprint =
        learned_model_fingerprint(
            second.anchored_candidate);
    const LearnedModelComponentFingerprints
        first_components =
            learned_model_component_fingerprints(
                first.anchored_candidate);
    const LearnedModelComponentFingerprints
        second_components =
            learned_model_component_fingerprints(
                second.anchored_candidate);
    const LearnedPriorityHeadParameters first_parameters =
        learned_priority_head_parameters(
            first.anchored_candidate);
    const LearnedPriorityHeadParameters second_parameters =
        learned_priority_head_parameters(
            second.anchored_candidate);
    const dev::FitAccounting expected =
        expected_fit_accounting(contract.fit);

    if (parent_fingerprint !=
            contract.parent.model_fingerprint ||
        parent_components !=
            contract.parent.components ||
        first.parent_fingerprint !=
            parent_fingerprint ||
        second.parent_fingerprint !=
            parent_fingerprint ||
        first.positive_only_candidate_fingerprint !=
            positive_candidate_fingerprint ||
        second.positive_only_candidate_fingerprint !=
            positive_candidate_fingerprint ||
        first.anchored_candidate_fingerprint !=
            contract.candidate_model_fingerprint ||
        second.anchored_candidate_fingerprint !=
            contract.candidate_model_fingerprint ||
        first_fingerprint !=
            contract.candidate_model_fingerprint ||
        second_fingerprint != first_fingerprint ||
        first_components != second_components ||
        !same_priority_parameters(
            first_parameters, second_parameters) ||
        !same_nonpriority_components(
            parent_components, first_components) ||
        first_components.priority ==
            parent_components.priority ||
        first.anchored_candidate_components !=
            first_components ||
        second.anchored_candidate_components !=
            second_components ||
        first.parent_components !=
            parent_components ||
        second.parent_components !=
            parent_components ||
        first.positive_only_candidate_components !=
            positive_candidate_components ||
        second.positive_only_candidate_components !=
            positive_candidate_components ||
        !exact_model_evaluation_identity(
            first.positive_only_evaluation,
            parent_fingerprint, parent_components,
            positive_candidate_fingerprint,
            positive_candidate_components) ||
        !exact_model_evaluation_identity(
            second.positive_only_evaluation,
            parent_fingerprint, parent_components,
            positive_candidate_fingerprint,
            positive_candidate_components) ||
        !exact_model_evaluation_identity(
            first.anchored_evaluation,
            parent_fingerprint, parent_components,
            first_fingerprint, first_components) ||
        !exact_model_evaluation_identity(
            second.anchored_evaluation,
            parent_fingerprint, parent_components,
            second_fingerprint, second_components) ||
        !same_training_batch_accounting(
            first.omitted_neutral_control,
            second.omitted_neutral_control) ||
        !same_training_batch_accounting(
            first.anchored_training,
            second.anchored_training) ||
        !exact_control_batch(
            first.omitted_neutral_control) ||
        !exact_control_batch(
            second.omitted_neutral_control) ||
        !exact_anchored_batch(
            first.anchored_training, expected) ||
        !exact_anchored_batch(
            second.anchored_training, expected) ||
        !same_scientific_report(first, second) ||
        !exact_neutral_check_census(
            first.neutral_check) ||
        !exact_neutral_check_census(
            second.neutral_check) ||
        first.isolation != second.isolation ||
        !all_isolation_checks_pass(first.isolation) ||
        !all_isolation_checks_pass(second.isolation) ||
        first.gate != second.gate ||
        !all_gate_checks_pass(first.gate) ||
        !all_gate_checks_pass(second.gate) ||
        learned_model_fingerprint(parent) !=
            parent_fingerprint ||
        learned_model_component_fingerprints(parent) !=
            parent_components ||
        learned_model_fingerprint(positive_candidate) !=
            positive_candidate_fingerprint ||
        learned_model_component_fingerprints(
            positive_candidate) !=
            positive_candidate_components) {
        fail("evaluation_reproduction_or_gate_mismatch");
    }
    return {
        .components = first_components,
        .parameters = first_parameters,
    };
}

void require_same_sources(
    const RunReport& report,
    const testing::Dependencies& dependencies,
    const testing::Recipe& recipe) {
    if (dependencies.snapshot(
            recipe.executable_path) !=
            report.executable_before ||
        dependencies.snapshot(recipe.dev1.path) !=
            report.dev1_before ||
        dependencies.snapshot(recipe.parent.path) !=
            report.parent_before ||
        dependencies.snapshot(
            recipe.positive_candidate.path) !=
            report.positive_candidate_before ||
        dependencies.snapshot(recipe.neutral.path) !=
            report.neutral_before) {
        fail("source_changed_during_publication");
    }
}

bool path_absent(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error ==
        std::make_error_code(
            std::errc::no_such_file_or_directory)) {
        return true;
    }
    if (error) {
        throw std::system_error(
            error,
            "publication_coordinate_inspection_failed");
    }
    return !std::filesystem::exists(status);
}

testing::Dependencies production_dependencies() {
    return {
        .snapshot =
            [](const std::filesystem::path& path) {
                return integrity::snapshot_regular_file(path);
            },
        .path_absent =
            [](const std::filesystem::path& path) {
                return path_absent(path);
            },
        .load_dev1 =
            [] { return bundle::load_published(); },
        .prepare_dev1 =
            [](const bundle::Bundle& source) {
                return dev::prepare(source);
            },
        .load_parent =
            [] { return dev::load_fixed_parent(); },
        .load_positive_candidate =
            [](std::shared_ptr<const LearnedModel> parent) {
                const artifact::LoadedCandidate loaded =
                    artifact::load(
                        artifact::production_artifact_path(),
                        std::move(parent),
                        artifact::production_contract(),
                        {
                            .bytes =
                                kPositiveCandidateBytes,
                            .sha256 =
                                std::string(
                                    kPositiveCandidateSha256),
                        });
                return testing::LoadedPositiveCandidate{
                    .model = loaded.model(),
                    .report = loaded.report(),
                };
            },
        .load_neutral =
            [](const bundle::Manifest& manifest) {
                const neutral::Contract contract =
                    neutral::make_contract(
                        manifest,
                        neutral::accepted_dev4_capacity());
                const neutral::FileIdentity identity{
                    .bytes = kNeutralArtifactBytes,
                    .sha256 =
                        std::string(
                            kNeutralArtifactSha256),
                };
                return testing::LoadedNeutral{
                    .artifact =
                        neutral::load_published(
                            contract, identity),
                    .identity = identity,
                };
            },
        .evaluate =
            [](const dev::PreparedCorpus& corpus,
               const neutral::Artifact& neutral_artifact,
               std::shared_ptr<const LearnedModel> parent,
               std::shared_ptr<const LearnedModel>
                   positive_candidate) {
                return evaluator::evaluate(
                    corpus, neutral_artifact,
                    std::move(parent),
                    std::move(positive_candidate));
            },
        .publish =
            [](const std::filesystem::path& destination,
               std::shared_ptr<const LearnedModel> parent,
               std::shared_ptr<const LearnedModel> candidate_model,
               const artifact::Contract& contract) {
                return artifact::publish_atomic_no_replace(
                    destination, std::move(parent),
                    std::move(candidate_model), contract);
            },
        .reload =
            [](const std::filesystem::path& path,
               std::shared_ptr<const LearnedModel> parent,
               const artifact::Contract& contract,
               const artifact::FileIdentity& identity) {
                const artifact::LoadedCandidate loaded =
                    artifact::load(
                        path, std::move(parent),
                        contract, identity);
                return testing::ReloadedCandidate{
                    .model = loaded.model(),
                    .report = loaded.report(),
                };
            },
    };
}

testing::Recipe make_fixed_recipe(
    const std::filesystem::path& executable,
    std::string_view producer_commit) {
    return {
        .executable_path = executable,
        .producer_commit = std::string(producer_commit),
        .dev1 = {
            .path =
                "data/"
                "old-school-fq4-priority-dev-v2.fq4dev",
            .bytes = 2'250'909,
            .sha256 =
                "0911fc2eb8b14ddc9165543eb1e4c4ed"
                "b0b058256a58dedf61f6c4ea4ca859df",
        },
        .parent = {
            .path =
                "build/model-cache/"
                "old-school-value-challenger-v3-"
                "c16-t800-s424242.bin",
            .bytes = kParentArtifactBytes,
            .sha256 =
                "53aeb904bd87311b37201859317f05ab0"
                "66bdfe134c72460cf94bff6d1f944ca",
        },
        .positive_candidate = {
            .path =
                "data/"
                "old-school-fq4-dev1-priority-"
                "candidate-v1.fq4candidate",
            .bytes = kPositiveCandidateBytes,
            .sha256 =
                std::string(kPositiveCandidateSha256),
        },
        .neutral = {
            .path =
                "data/"
                "old-school-fq4-priority-neutral-"
                "supplement-v1.fq4neutral",
            .bytes = kNeutralArtifactBytes,
            .sha256 =
                std::string(kNeutralArtifactSha256),
        },
        .destination_path = production_artifact_path(),
        .temporary_path = production_temporary_path(),
        .contract = production_contract(),
    };
}

void write_report(
    const RunReport& report, std::ostream& output) {
    output
        << "schema=" << artifact::kSchema
        << " family=" << kFamily
        << " result=PUBLISHED"
        << " artifact_bytes="
        << report.artifact.artifact.bytes
        << " artifact_sha256="
        << report.artifact.artifact.sha256
        << " candidate_model="
        << report.artifact.manifest.contract
               .candidate_model_fingerprint
        << " training_sha256="
        << report.first_evaluation.anchored_training
               .accounting.training_input_sha256
        << " gate=PASS\n";
}

} // namespace

const artifact::Contract& production_contract() {
    static const artifact::Contract contract{
        .family = std::string(kFamily),
        .environment = std::string(kEnvironment),
        .parent = {
            .artifact_bytes = kParentArtifactBytes,
            .artifact_sha256 =
                "53aeb904bd87311b37201859317f05ab0"
                "66bdfe134c72460cf94bff6d1f944ca",
            .model_fingerprint =
                "68126afc5a3e3757eb1d510a056585aa9"
                "74c4f54ce1b4a789ff430f1c7413e2f",
            .components = {
                .critic =
                    "2982b155a02a4a2a3ce8442ae28f6d8c"
                    "f7829103e538c60f0625b3332502e568",
                .priority =
                    "32dc6688a5c970e3eda4325bea5ee4190"
                    "77027e160697899e3b00c963fa1bb22",
                .attack =
                    "dfd3aaa16755bee5d0c2c40956851b94"
                    "ef5676a271a602eb23a57719f7358b01",
                .block =
                    "d64e40796bd1587958b7386996e6a1e5"
                    "660778d40ec7b40b0ee6324b8e39adbb",
                .damage_order =
                    "f0a84ed549bbf95197dd00c13ab04c0a"
                    "4f6b1771f14bdb30a7dca937d2d79c76",
            },
            .training_games = 800,
            .training_seed = 424242,
            .generation = 16,
        },
        .corpus = {
            .artifact_bytes = 2'250'909,
            .artifact_sha256 =
                "0911fc2eb8b14ddc9165543eb1e4c4ed"
                "b0b058256a58dedf61f6c4ea4ca859df",
        },
        .fit = {
            .input_sha256 =
                std::string(kFitInputSha256),
            .examples = kFitExamples,
            .options = kFitOptions,
            .check_examples = 0,
            .background_only_examples = 0,
            .optimizer_calls = 1,
            .optimizer = {
                .batch_size = 64,
                .epochs = 16,
                .learning_rate = 0.001,
                .beta1 = 0.9,
                .beta2 = 0.999,
                .epsilon = 1.0e-8,
                .global_gradient_norm_clip = 5.0,
                .seed = 202607280212ULL,
                .residual_weight = 0.10,
                .policy_temperature = 0.10,
            },
        },
        .candidate_model_fingerprint =
            std::string(kCandidateFingerprint),
        .priority_hidden_count =
            kPriorityHiddenCount,
        .priority_feature_count =
            kPriorityFeatureCount,
        .priority_parameter_count =
            kPriorityParameterCount,
        .deployment = {
            .variant =
                LearnedVariant::ValueSearchChampion,
            .training_games = 800,
            .worlds_per_action = 8,
            .horizon_turns = 4,
            .rollouts_per_world = 1,
            .root_search_depth = 1,
            .shallow_prior = true,
            .root_exploration = 0.0,
            .continuation_epsilon = 0.0,
            .priority_residual_weight = 0.10,
            .pass_dominance = false,
            .continuation_controller =
                LearnedContinuationController::Legacy,
            .max_turns = 500,
        },
    };
    static const bool validated = [] {
        validate_frozen_production_contract(contract);
        return true;
    }();
    static_cast<void>(validated);
    return contract;
}

std::filesystem::path production_artifact_path() {
    return std::filesystem::path(kArtifactPath);
}

std::filesystem::path production_temporary_path() {
    return artifact::temporary_path_for(
        production_artifact_path());
}

const testing::Recipe& testing::fixed_recipe() {
    static const testing::Recipe recipe =
        make_fixed_recipe(
            "old-school-fq4-dev5-candidate-publish",
            "1111111111111111111111111111111111111111");
    return recipe;
}

bool testing::publication_coordinate_absent(
    const std::filesystem::path& path) {
    return path_absent(path);
}

void testing::validate_frozen_contract(
    const artifact::Contract& contract) {
    validate_frozen_production_contract(contract);
}

RunReport testing::publish_candidate(
    const Recipe& recipe,
    const Dependencies& dependencies) {
    validate_recipe(recipe);
    validate_dependencies(dependencies);
    require_absent(recipe, dependencies);

    RunReport result;
    result.executable_before =
        dependencies.snapshot(recipe.executable_path);
    result.dev1_before =
        dependencies.snapshot(recipe.dev1.path);
    result.parent_before =
        dependencies.snapshot(recipe.parent.path);
    result.positive_candidate_before =
        dependencies.snapshot(
            recipe.positive_candidate.path);
    result.neutral_before =
        dependencies.snapshot(recipe.neutral.path);
    require_source_identity(
        recipe.dev1, result.dev1_before);
    require_source_identity(
        recipe.parent, result.parent_before);
    require_source_identity(
        recipe.positive_candidate,
        result.positive_candidate_before);
    require_source_identity(
        recipe.neutral, result.neutral_before);

    const bundle::Bundle dev1 =
        dependencies.load_dev1();
    const dev::PreparedCorpus corpus =
        dependencies.prepare_dev1(dev1);
    const std::shared_ptr<const LearnedModel> parent =
        dependencies.load_parent();
    validate_parent_model(recipe.contract, parent);
    const LoadedPositiveCandidate positive =
        dependencies.load_positive_candidate(parent);
    const LoadedNeutral neutral_source =
        dependencies.load_neutral(dev1.manifest);
    const std::string positive_fingerprint =
        positive.model
            ? learned_model_fingerprint(positive.model)
            : std::string{};
    if (!positive.model ||
        positive.report.artifact.bytes !=
            recipe.positive_candidate.bytes ||
        positive.report.artifact.sha256 !=
            recipe.positive_candidate.sha256 ||
        positive.report.manifest.contract
                .candidate_model_fingerprint !=
            positive_fingerprint ||
        neutral_source.identity.bytes !=
            recipe.neutral.bytes ||
        neutral_source.identity.sha256 !=
            recipe.neutral.sha256) {
        fail("loaded_source_contract_mismatch");
    }

    result.first_evaluation =
        dependencies.evaluate(
            corpus, neutral_source.artifact,
            parent, positive.model);
    result.second_evaluation =
        dependencies.evaluate(
            corpus, neutral_source.artifact,
            parent, positive.model);
    const ValidatedCandidate candidate_model =
        validate_evaluations(
            recipe.contract, parent,
            positive.model,
            result.first_evaluation,
            result.second_evaluation);

    require_same_sources(
        result, dependencies, recipe);
    require_absent(recipe, dependencies);

    const std::shared_ptr<const LearnedModel>
        publication_parent =
            dependencies.load_parent();
    validate_parent_model(
        recipe.contract, publication_parent);
    result.artifact =
        dependencies.publish(
            recipe.destination_path, publication_parent,
            result.first_evaluation.anchored_candidate,
            recipe.contract);
    result.artifact_published =
        dependencies.snapshot(
            recipe.destination_path);
    if (result.artifact.artifact.bytes == 0 ||
        !canonical_digest(
            result.artifact.artifact.sha256) ||
        result.artifact.artifact.bytes !=
            result.artifact_published.byte_size ||
        result.artifact.artifact.sha256 !=
            result.artifact_published.sha256 ||
        !same_contract(
            result.artifact.manifest.contract,
            recipe.contract) ||
        result.artifact.manifest.candidate_components !=
            candidate_model.components ||
        result.artifact.manifest.tensors.hidden_count !=
            recipe.contract.priority_hidden_count ||
        result.artifact.manifest.tensors.feature_count !=
            recipe.contract.priority_feature_count ||
        result.artifact.manifest.tensors.parameter_count !=
            recipe.contract.priority_parameter_count ||
        !canonical_digest(
            result.artifact.manifest.tensors
                .parent_sha256) ||
        !canonical_digest(
            result.artifact.manifest.tensors
                .candidate_sha256) ||
        !canonical_digest(
            result.artifact.manifest.tensors
                .xor_delta_sha256)) {
        fail("published_artifact_identity_mismatch");
    }

    const std::shared_ptr<const LearnedModel>
        reload_parent =
            dependencies.load_parent();
    validate_parent_model(
        recipe.contract, reload_parent);
    const ReloadedCandidate loaded =
        dependencies.reload(
            recipe.destination_path, reload_parent,
            recipe.contract, result.artifact.artifact);
    result.artifact_reloaded =
        dependencies.snapshot(
            recipe.destination_path);
    if (!loaded.model ||
        !same_artifact_report(
            loaded.report, result.artifact) ||
        result.artifact_reloaded !=
            result.artifact_published ||
        learned_model_fingerprint(loaded.model) !=
            recipe.contract.candidate_model_fingerprint ||
        learned_model_component_fingerprints(
            loaded.model) !=
            candidate_model.components ||
        !same_priority_parameters(
            learned_priority_head_parameters(
                loaded.model),
            candidate_model.parameters) ||
        !dependencies.path_absent(
            recipe.temporary_path)) {
        fail("reloaded_artifact_mismatch");
    }

    result.executable_after =
        dependencies.snapshot(recipe.executable_path);
    result.dev1_after =
        dependencies.snapshot(recipe.dev1.path);
    result.parent_after =
        dependencies.snapshot(recipe.parent.path);
    result.positive_candidate_after =
        dependencies.snapshot(
            recipe.positive_candidate.path);
    result.neutral_after =
        dependencies.snapshot(recipe.neutral.path);
    if (result.executable_after !=
            result.executable_before ||
        result.dev1_after != result.dev1_before ||
        result.parent_after != result.parent_before ||
        result.positive_candidate_after !=
            result.positive_candidate_before ||
        result.neutral_after !=
            result.neutral_before ||
        dependencies.path_absent(
            recipe.destination_path) ||
        !dependencies.path_absent(
            recipe.temporary_path)) {
        fail("final_publication_state_mismatch");
    }
    return result;
}

int testing::run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error,
    std::string_view producer_commit,
    const FixedPublisher& publisher) {
    if (argc != 1 || argv == nullptr ||
        argv[0] == nullptr ||
        std::string_view(argv[0]).empty() ||
        !canonical_commit(producer_commit) ||
        !publisher) {
        error
            << "Usage: "
               "old-school-fq4-dev5-candidate-publish\n";
        return 2;
    }
    try {
        const RunReport report =
            publisher(
                std::filesystem::path(argv[0]),
                producer_commit);
        write_report(report, output);
        output.flush();
        return output.good() ? 0 : 2;
    } catch (const std::exception&) {
        error
            << "result=ERROR"
               " reason=fixed_anchored_candidate_"
               "publication_failed\n";
        return 2;
    }
}

RunReport publish_fixed_candidate(
    const std::filesystem::path& executable,
    std::string_view producer_commit) {
    validate_frozen_production_contract(
        production_contract());
    return testing::publish_candidate(
        make_fixed_recipe(executable, producer_commit),
        production_dependencies());
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, std::string_view producer_commit) {
    return testing::run_cli(
        argc, argv, output, error, producer_commit,
        [](const std::filesystem::path& executable,
           std::string_view commit) {
            return publish_fixed_candidate(
                executable, commit);
        });
}

} // namespace old_school::fq4_neutral_candidate_publisher
