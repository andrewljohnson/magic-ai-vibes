#include "old_school/fq4_dev_candidate_publisher.hpp"

#include "old_school/fq4_dev_bundle.hpp"

#include <array>
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

namespace old_school::fq4_dev_candidate_publisher {
namespace {

namespace artifact = fq4_dev_candidate_artifact;
namespace bundle = fq4_dev_bundle;
namespace evaluator = fq4_dev_evaluator;
namespace integrity = artifact_integrity;

inline constexpr std::string_view kExpectedCandidateFingerprint =
    "712600783152e89ff1a53394149764db227e55289a656530342226b7e1ee6151";
inline constexpr std::string_view kExpectedFitInputSha256 =
    "586b121c3c9bdb1a61305cac86882cd20b5d2ba332b4d5a54defc2c7756393a1";
inline constexpr std::string_view kExpectedFamily =
    "FQ4-DEV1";
inline constexpr std::string_view kExpectedEnvironment =
    "old-school-environment-v3-cleanup-discard";
inline constexpr std::uint64_t kExpectedParentArtifactBytes =
    3111437;
inline constexpr std::uint32_t kExpectedPriorityHiddenCount =
    32;
inline constexpr std::uint64_t kExpectedPriorityParameterCount =
    29534;

[[noreturn]] void fail(std::string_view category) {
    throw std::runtime_error(std::string(category));
}

bool canonical_digest(std::string_view value) {
    if (value.size() != 64) {
        return false;
    }
    for (const char character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool expected_parent_components(
    const LearnedModelComponentFingerprints& components) {
    return
        components.critic ==
            bundle::kParentCriticFingerprint &&
        components.priority ==
            bundle::kParentPriorityFingerprint &&
        components.attack ==
            bundle::kParentAttackFingerprint &&
        components.block ==
            bundle::kParentBlockFingerprint &&
        components.damage_order ==
            bundle::kParentDamageOrderFingerprint;
}

bool same_nonpriority_components(
    const LearnedModelComponentFingerprints& parent,
    const LearnedModelComponentFingerprints& candidate) {
    return candidate.critic == parent.critic &&
           candidate.attack == parent.attack &&
           candidate.block == parent.block &&
           candidate.damage_order == parent.damage_order;
}

void validate_frozen_production_contract(
    const artifact::Contract& contract) {
    if (contract.family != kExpectedFamily ||
        contract.environment != kExpectedEnvironment ||
        contract.parent.artifact_bytes !=
            kExpectedParentArtifactBytes ||
        contract.parent.artifact_sha256 !=
            bundle::kParentArtifactSha256 ||
        contract.parent.model_fingerprint !=
            bundle::kParentModelFingerprint ||
        !expected_parent_components(
            contract.parent.components) ||
        contract.parent.training_games !=
            evaluator::kParentTrainingGames ||
        contract.parent.training_seed !=
            evaluator::kParentTrainingSeed ||
        contract.parent.generation !=
            evaluator::kParentGenerations ||
        contract.corpus.artifact_bytes !=
            bundle::kPublishedArtifactBytes ||
        contract.corpus.artifact_sha256 !=
            bundle::kPublishedArtifactSha256 ||
        contract.fit.input_sha256 !=
            kExpectedFitInputSha256 ||
        contract.fit.examples != 88 ||
        contract.fit.options != 548 ||
        contract.fit.check_examples != 0 ||
        contract.fit.background_only_examples != 0 ||
        contract.fit.optimizer_calls != 1 ||
        contract.fit.optimizer != evaluator::kOptimizer ||
        contract.candidate_model_fingerprint !=
            kExpectedCandidateFingerprint ||
        contract.priority_hidden_count !=
            kExpectedPriorityHiddenCount ||
        contract.priority_feature_count !=
            bundle::kFeatureCount ||
        contract.priority_parameter_count !=
            kExpectedPriorityParameterCount ||
        contract.deployment.variant !=
            LearnedVariant::ValueSearchChampion ||
        contract.deployment.training_games != 800 ||
        contract.deployment.worlds_per_action != 8 ||
        contract.deployment.horizon_turns != 4 ||
        contract.deployment.rollouts_per_world != 1 ||
        contract.deployment.root_search_depth != 1 ||
        !contract.deployment.shallow_prior ||
        contract.deployment.root_exploration != 0.0 ||
        contract.deployment.continuation_epsilon != 0.0 ||
        contract.deployment.priority_residual_weight != 0.10 ||
        contract.deployment.pass_dominance ||
        contract.deployment.continuation_controller !=
            LearnedContinuationController::Legacy ||
        contract.deployment.max_turns != 500) {
        fail("production_contract_mismatch");
    }
}

void validate_recipe(const testing::Recipe& recipe) {
    const std::array<const std::filesystem::path*, 5> paths = {
        &recipe.executable_path,
        &recipe.corpus_path,
        &recipe.parent_path,
        &recipe.destination_path,
        &recipe.temporary_path,
    };
    for (const std::filesystem::path* path : paths) {
        if (path->empty() ||
            path->string().find('\0') != std::string::npos ||
            path->filename().empty()) {
            fail("invalid_publisher_recipe");
        }
    }
    for (std::size_t first = 0; first < paths.size();
         ++first) {
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
                recipe.destination_path) ||
        recipe.contract.parent.artifact_bytes == 0 ||
        recipe.contract.corpus.artifact_bytes == 0 ||
        !canonical_digest(
            recipe.contract.parent.artifact_sha256) ||
        !canonical_digest(
            recipe.contract.parent.model_fingerprint) ||
        !canonical_digest(
            recipe.contract.corpus.artifact_sha256) ||
        !canonical_digest(
            recipe.contract.fit.input_sha256) ||
        !canonical_digest(
            recipe.contract.candidate_model_fingerprint) ||
        recipe.contract.fit.examples == 0 ||
        recipe.contract.fit.options == 0 ||
        recipe.contract.fit.optimizer_calls != 1 ||
        recipe.contract.priority_hidden_count == 0 ||
        recipe.contract.priority_feature_count == 0 ||
        recipe.contract.priority_parameter_count == 0) {
        fail("invalid_publisher_recipe");
    }
}

void validate_dependencies(
    const testing::Dependencies& dependencies) {
    if (!dependencies.snapshot ||
        !dependencies.path_absent ||
        !dependencies.load_corpus ||
        !dependencies.load_parent ||
        !dependencies.fit_candidate ||
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

evaluator::FitAccounting expected_fit_accounting(
    const artifact::FitBoundary& boundary) {
    if (boundary.examples >
            std::numeric_limits<std::size_t>::max() ||
        boundary.options >
            std::numeric_limits<std::size_t>::max() ||
        boundary.check_examples >
            std::numeric_limits<std::size_t>::max() ||
        boundary.background_only_examples >
            std::numeric_limits<std::size_t>::max() ||
        boundary.optimizer_calls >
            std::numeric_limits<std::size_t>::max()) {
        fail("fit_boundary_out_of_range");
    }
    return {
        .fit_examples =
            static_cast<std::size_t>(boundary.examples),
        .fit_options =
            static_cast<std::size_t>(boundary.options),
        .check_examples =
            static_cast<std::size_t>(
                boundary.check_examples),
        .background_only_examples =
            static_cast<std::size_t>(
                boundary.background_only_examples),
        .optimizer_calls =
            static_cast<std::size_t>(
                boundary.optimizer_calls),
        .training_input_sha256 = boundary.input_sha256,
        .optimizer = boundary.optimizer,
    };
}

void validate_source_identity(
    const testing::Recipe& recipe,
    const integrity::RegularFileSnapshot& corpus,
    const integrity::RegularFileSnapshot& parent) {
    if (corpus.byte_size !=
            recipe.contract.corpus.artifact_bytes ||
        corpus.sha256 !=
            recipe.contract.corpus.artifact_sha256 ||
        parent.byte_size !=
            recipe.contract.parent.artifact_bytes ||
        parent.sha256 !=
            recipe.contract.parent.artifact_sha256) {
        fail("source_identity_mismatch");
    }
}

struct ValidatedModels {
    LearnedModelComponentFingerprints parent_components;
    LearnedModelComponentFingerprints candidate_components;
    LearnedPriorityHeadParameters candidate_parameters;
};

ValidatedModels validate_reproduced_fits(
    const artifact::Contract& contract,
    std::shared_ptr<const LearnedModel> parent,
    const evaluator::CandidateFit& first,
    const evaluator::CandidateFit& second) {
    if (!parent || !first.model || !second.model) {
        fail("missing_fit_model");
    }
    const evaluator::FitAccounting expected =
        expected_fit_accounting(contract.fit);
    if (first.accounting != expected ||
        second.accounting != expected) {
        fail("fit_accounting_mismatch");
    }

    const std::string parent_fingerprint =
        learned_model_fingerprint(parent);
    const LearnedModelComponentFingerprints parent_components =
        learned_model_component_fingerprints(parent);
    if (parent_fingerprint !=
            contract.parent.model_fingerprint ||
        parent_components != contract.parent.components) {
        fail("parent_model_identity_mismatch");
    }

    const std::string first_fingerprint =
        learned_model_fingerprint(first.model);
    const std::string second_fingerprint =
        learned_model_fingerprint(second.model);
    const LearnedModelComponentFingerprints first_components =
        learned_model_component_fingerprints(first.model);
    const LearnedModelComponentFingerprints second_components =
        learned_model_component_fingerprints(second.model);
    const LearnedPriorityHeadParameters first_parameters =
        learned_priority_head_parameters(first.model);
    const LearnedPriorityHeadParameters second_parameters =
        learned_priority_head_parameters(second.model);
    if (first_fingerprint !=
            contract.candidate_model_fingerprint ||
        second_fingerprint !=
            contract.candidate_model_fingerprint ||
        first_fingerprint != second_fingerprint ||
        first_components != second_components ||
        first_parameters != second_parameters ||
        !same_nonpriority_components(
            parent_components, first_components) ||
        !same_nonpriority_components(
            parent_components, second_components) ||
        first_components.priority ==
            parent_components.priority ||
        learned_model_fingerprint(parent) !=
            parent_fingerprint ||
        learned_model_component_fingerprints(parent) !=
            parent_components) {
        fail("fit_reproduction_or_isolation_mismatch");
    }
    return {
        .parent_components = parent_components,
        .candidate_components = first_components,
        .candidate_parameters = first_parameters,
    };
}

void require_same_source_snapshots(
    const RunReport& report,
    const integrity::RegularFileSnapshot& executable,
    const integrity::RegularFileSnapshot& corpus,
    const integrity::RegularFileSnapshot& parent) {
    if (report.executable_before != executable ||
        report.corpus_before != corpus ||
        report.parent_before != parent) {
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
            error, "publication_coordinate_inspection_failed");
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
        .load_corpus =
            [] {
                return evaluator::prepare(
                    bundle::load_published());
            },
        .load_parent =
            [] {
                return evaluator::load_fixed_parent();
            },
        .fit_candidate =
            [](const evaluator::PreparedCorpus& corpus,
               std::shared_ptr<const LearnedModel> parent) {
                return evaluator::fit_candidate(
                    corpus, std::move(parent));
            },
        .publish =
            [](const std::filesystem::path& path,
               std::shared_ptr<const LearnedModel> parent,
               std::shared_ptr<const LearnedModel> candidate,
               const artifact::Contract& contract) {
                return artifact::publish_atomic_no_replace(
                    path, std::move(parent),
                    std::move(candidate), contract);
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

testing::Recipe production_recipe(
    const std::filesystem::path& executable) {
    return {
        .executable_path = executable,
        .corpus_path = std::string(bundle::kArtifactPath),
        .parent_path =
            std::string(evaluator::kParentArtifactPath),
        .destination_path =
            artifact::production_artifact_path(),
        .temporary_path =
            artifact::production_temporary_path(),
        .contract = artifact::production_contract(),
    };
}

void write_report(
    const RunReport& report, std::ostream& output) {
    output
        << "schema=" << artifact::kSchema
        << " result=PUBLISHED"
        << " artifact_bytes="
        << report.artifact.artifact.bytes
        << " artifact_sha256="
        << report.artifact.artifact.sha256
        << " candidate_model="
        << report.artifact.manifest.contract
               .candidate_model_fingerprint
        << '\n';
}

} // namespace

namespace testing {

void validate_frozen_contract(
    const artifact::Contract& contract) {
    validate_frozen_production_contract(contract);
}

RunReport publish_candidate(
    const Recipe& recipe,
    const Dependencies& dependencies) {
    validate_recipe(recipe);
    validate_dependencies(dependencies);

    // These checks intentionally precede every load and fit.
    require_absent(recipe, dependencies);

    RunReport result;
    result.executable_before =
        dependencies.snapshot(recipe.executable_path);
    result.corpus_before =
        dependencies.snapshot(recipe.corpus_path);
    result.parent_before =
        dependencies.snapshot(recipe.parent_path);
    validate_source_identity(
        recipe, result.corpus_before,
        result.parent_before);

    const evaluator::PreparedCorpus corpus =
        dependencies.load_corpus();
    const std::shared_ptr<const LearnedModel> parent =
        dependencies.load_parent();
    const evaluator::CandidateFit first =
        dependencies.fit_candidate(corpus, parent);
    const evaluator::CandidateFit second =
        dependencies.fit_candidate(corpus, parent);
    const ValidatedModels models =
        validate_reproduced_fits(
            recipe.contract, parent, first, second);
    result.first_fit = first.accounting;
    result.second_fit = second.accounting;

    // Fail closed on source drift or a newly occupied publication coordinate
    // before the no-replace writer can create anything.
    require_same_source_snapshots(
        result,
        dependencies.snapshot(recipe.executable_path),
        dependencies.snapshot(recipe.corpus_path),
        dependencies.snapshot(recipe.parent_path));
    require_absent(recipe, dependencies);

    result.artifact = dependencies.publish(
        recipe.destination_path, parent, first.model,
        recipe.contract);
    result.artifact_published =
        dependencies.snapshot(recipe.destination_path);
    if (result.artifact.artifact.bytes == 0 ||
        !canonical_digest(
            result.artifact.artifact.sha256) ||
        result.artifact.artifact.bytes !=
            result.artifact_published.byte_size ||
        result.artifact.artifact.sha256 !=
            result.artifact_published.sha256 ||
        result.artifact.manifest.contract !=
            recipe.contract ||
        result.artifact.manifest.candidate_components !=
            models.candidate_components ||
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

    const ReloadedCandidate loaded =
        dependencies.reload(
            recipe.destination_path, parent,
            recipe.contract, result.artifact.artifact);
    result.artifact_reloaded =
        dependencies.snapshot(recipe.destination_path);
    if (!loaded.model ||
        loaded.report != result.artifact ||
        result.artifact_reloaded !=
            result.artifact_published ||
        learned_model_fingerprint(loaded.model) !=
            recipe.contract.candidate_model_fingerprint ||
        learned_model_component_fingerprints(
            loaded.model) !=
            models.candidate_components ||
        learned_priority_head_parameters(loaded.model) !=
            models.candidate_parameters ||
        !dependencies.path_absent(
            recipe.temporary_path)) {
        fail("reloaded_artifact_mismatch");
    }

    result.executable_after =
        dependencies.snapshot(recipe.executable_path);
    result.corpus_after =
        dependencies.snapshot(recipe.corpus_path);
    result.parent_after =
        dependencies.snapshot(recipe.parent_path);
    require_same_source_snapshots(
        result, result.executable_after,
        result.corpus_after, result.parent_after);
    if (dependencies.path_absent(
            recipe.destination_path) ||
        !dependencies.path_absent(
            recipe.temporary_path)) {
        fail("published_coordinate_mismatch");
    }
    return result;
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, const FixedPublisher& publisher) {
    if (argc != 1 || argv == nullptr ||
        argv[0] == nullptr ||
        std::string_view(argv[0]).empty() ||
        !publisher) {
        error
            << "Usage: old-school-fq4-dev1-candidate-publish\n";
        return 2;
    }
    try {
        const RunReport report =
            publisher(std::filesystem::path(argv[0]));
        write_report(report, output);
        output.flush();
        return output.good() ? 0 : 2;
    } catch (const std::exception&) {
        error
            << "result=ERROR"
               " reason=fixed_candidate_publication_failed\n";
        return 2;
    }
}

} // namespace testing

RunReport publish_fixed_candidate(
    const std::filesystem::path& executable) {
    const artifact::Contract& contract =
        artifact::production_contract();
    validate_frozen_production_contract(contract);
    return testing::publish_candidate(
        production_recipe(executable),
        production_dependencies());
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error) {
    return testing::run_cli(
        argc, argv, output, error,
        [](const std::filesystem::path& executable) {
            return publish_fixed_candidate(executable);
        });
}

} // namespace old_school::fq4_dev_candidate_publisher
