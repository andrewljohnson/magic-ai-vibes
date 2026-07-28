#include "old_school/fq4_neutral_evaluator_runner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace old_school::fq4_neutral_evaluator_runner {
namespace {

namespace bundle = fq4_dev_bundle;
namespace candidate = fq4_dev_candidate_artifact;
namespace dev = fq4_dev_evaluator;
namespace evaluator = fq4_neutral_evaluator;
namespace integrity = artifact_integrity;
namespace neutral = fq4_neutral_supplement;

[[noreturn]] void fail(std::string_view category) {
    throw std::runtime_error(std::string(category));
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

const FixedCoordinates& production_coordinates() {
    static const FixedCoordinates coordinates{
        .dev1 = {
            .path = std::string(bundle::kArtifactPath),
            .bytes = bundle::kPublishedArtifactBytes,
            .sha256 =
                std::string(
                    bundle::kPublishedArtifactSha256),
        },
        .parent = {
            .path =
                std::string(dev::kParentArtifactPath),
            .bytes = kParentArtifactBytes,
            .sha256 =
                std::string(
                    bundle::kParentArtifactSha256),
        },
        .positive_candidate = {
            .path = candidate::production_artifact_path(),
            .bytes = kPositiveCandidateBytes,
            .sha256 =
                std::string(kPositiveCandidateSha256),
        },
        .neutral_artifact = {
            .path = neutral::production_artifact_path(),
            .bytes = kNeutralArtifactBytes,
            .sha256 =
                std::string(kNeutralArtifactSha256),
        },
    };
    return coordinates;
}

void validate_coordinates(
    const FixedCoordinates& coordinates) {
    const std::array<const FrozenSource*, 4> frozen{
        &coordinates.dev1,
        &coordinates.parent,
        &coordinates.positive_candidate,
        &coordinates.neutral_artifact,
    };
    for (const FrozenSource* source : frozen) {
        if (source->path.empty() ||
            source->path.filename().empty() ||
            source->bytes == 0 ||
            !canonical_digest(source->sha256)) {
            fail("invalid_fixed_coordinates");
        }
    }
    const std::array<std::filesystem::path, 4> paths{
        coordinates.dev1.path.lexically_normal(),
        coordinates.parent.path.lexically_normal(),
        coordinates.positive_candidate.path
            .lexically_normal(),
        coordinates.neutral_artifact.path
            .lexically_normal(),
    };
    for (std::size_t first = 0;
         first < paths.size(); ++first) {
        for (std::size_t second = first + 1;
             second < paths.size(); ++second) {
            if (paths[first] == paths[second]) {
                fail("invalid_fixed_coordinates");
            }
        }
    }
    if (coordinates.dev1.path !=
            std::filesystem::path(
                bundle::kArtifactPath) ||
        coordinates.dev1.bytes !=
            bundle::kPublishedArtifactBytes ||
        coordinates.dev1.sha256 !=
            bundle::kPublishedArtifactSha256 ||
        coordinates.parent.path !=
            std::filesystem::path(
                dev::kParentArtifactPath) ||
        coordinates.parent.bytes !=
            kParentArtifactBytes ||
        coordinates.parent.sha256 !=
            bundle::kParentArtifactSha256 ||
        coordinates.positive_candidate.path !=
            candidate::production_artifact_path() ||
        coordinates.positive_candidate.bytes !=
            kPositiveCandidateBytes ||
        coordinates.positive_candidate.sha256 !=
            kPositiveCandidateSha256 ||
        coordinates.neutral_artifact.path !=
            neutral::production_artifact_path() ||
        coordinates.neutral_artifact.bytes !=
            kNeutralArtifactBytes ||
        coordinates.neutral_artifact.sha256 !=
            kNeutralArtifactSha256) {
        fail("fixed_coordinates_drifted");
    }
}

void validate_dependencies(
    const testing::Dependencies& dependencies) {
    if (!dependencies.snapshot ||
        !dependencies.load_dev1 ||
        !dependencies.prepare_dev1 ||
        !dependencies.load_parent ||
        !dependencies.load_positive_candidate ||
        !dependencies.make_neutral_contract ||
        !dependencies.load_neutral ||
        !dependencies.evaluate) {
        fail("incomplete_runner_dependencies");
    }
}

void require_source_identity(
    const FrozenSource& expected,
    const integrity::RegularFileSnapshot& observed) {
    if (observed.byte_size != expected.bytes ||
        observed.sha256 != expected.sha256) {
        fail("fixed_source_identity_mismatch");
    }
}

void validate_evaluation_identity(
    const evaluator::Report& report) {
    if (report.parent_fingerprint !=
            bundle::kParentModelFingerprint ||
        report.positive_only_candidate_fingerprint !=
            evaluator::
                kRequiredPositiveOnlyCandidateFingerprint ||
        !canonical_digest(
            report.anchored_candidate_fingerprint) ||
        !canonical_digest(
            report.anchored_training.accounting
                .training_input_sha256)) {
        fail("evaluation_identity_mismatch");
    }
}

testing::Dependencies production_dependencies() {
    return {
        .snapshot =
            [](const std::filesystem::path& path) {
                return integrity::snapshot_regular_file(path);
            },
        .load_dev1 =
            [](const std::filesystem::path& path) {
                if (path !=
                    std::filesystem::path(
                        bundle::kArtifactPath)) {
                    fail("fixed_dev1_path_mismatch");
                }
                return bundle::load_published();
            },
        .prepare_dev1 =
            [](const bundle::Bundle& artifact) {
                return dev::prepare(artifact);
            },
        .load_parent =
            [](const std::filesystem::path& path) {
                if (path !=
                    std::filesystem::path(
                        dev::kParentArtifactPath)) {
                    fail("fixed_parent_path_mismatch");
                }
                return dev::load_fixed_parent();
            },
        .load_positive_candidate =
            [](const std::filesystem::path& path,
               std::shared_ptr<const LearnedModel> parent,
               const candidate::Contract& contract,
               const candidate::FileIdentity& identity) {
                return candidate::load(
                           path, std::move(parent),
                           contract, identity)
                    .model();
            },
        .make_neutral_contract =
            [](const bundle::Manifest& manifest) {
                return neutral::make_contract(
                    manifest,
                    neutral::accepted_dev4_capacity());
            },
        .load_neutral =
            [](const std::filesystem::path& path,
               const neutral::Contract& contract,
               const neutral::FileIdentity& identity) {
                if (path !=
                    neutral::production_artifact_path()) {
                    fail("fixed_neutral_path_mismatch");
                }
                return neutral::load_published(
                    contract, identity);
            },
        .evaluate =
            [](const dev::PreparedCorpus& positive,
               const neutral::Artifact& neutral_artifact,
               std::shared_ptr<const LearnedModel> parent,
               std::shared_ptr<const LearnedModel>
                   positive_candidate) {
                return evaluator::evaluate(
                    positive, neutral_artifact,
                    std::move(parent),
                    std::move(positive_candidate));
            },
    };
}

void write_positive_split(
    std::ostream& output, std::string_view split,
    const dev::SplitMetrics& baseline,
    const dev::SplitMetrics& anchored) {
    output
        << "positive split=" << split
        << " scope=pooled"
        << " roots=" << anchored.positive_roots
        << " options=" << anchored.positive_options
        << " baseline_repairs=" << baseline.repairs
        << " anchored_repairs=" << anchored.repairs
        << " baseline_regressions="
        << baseline.regressions
        << " anchored_regressions="
        << anchored.regressions
        << " baseline_violations="
        << baseline.candidate_support_violations
               .violating_roots
        << " anchored_violations="
        << anchored.candidate_support_violations
               .violating_roots
        << " parent_kl="
        << anchored.deck_balanced_target_to_parent_kl
        << " baseline_kl="
        << baseline.deck_balanced_target_to_candidate_kl
        << " anchored_kl="
        << anchored.deck_balanced_target_to_candidate_kl
        << '\n';
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        const dev::DeckMetrics& baseline_value =
            baseline.decks[deck];
        const dev::DeckMetrics& anchored_value =
            anchored.decks[deck];
        output
            << "positive split=" << split
            << " deck="
            << deck_name(static_cast<DeckId>(deck))
            << " roots=" << anchored_value.positive_roots
            << " options=" << anchored_value.positive_options
            << " baseline_repairs="
            << baseline_value.repairs
            << " anchored_repairs="
            << anchored_value.repairs
            << " baseline_regressions="
            << baseline_value.regressions
            << " anchored_regressions="
            << anchored_value.regressions
            << " baseline_violations="
            << baseline_value
                   .candidate_support_violations
                   .violating_roots
            << " anchored_violations="
            << anchored_value
                   .candidate_support_violations
                   .violating_roots
            << " parent_kl="
            << anchored_value.target_to_parent_kl
            << " baseline_kl="
            << baseline_value.target_to_candidate_kl
            << " anchored_kl="
            << anchored_value.target_to_candidate_kl
            << '\n';
    }
}

} // namespace

const FixedCoordinates& testing::fixed_coordinates() {
    return production_coordinates();
}

RunReport testing::run_fixed(
    const Dependencies& dependencies) {
    validate_dependencies(dependencies);
    const FixedCoordinates& coordinates =
        production_coordinates();
    validate_coordinates(coordinates);

    RunReport result{
        .coordinates = coordinates,
    };
    result.dev1_before =
        dependencies.snapshot(
            coordinates.dev1.path);
    result.parent_before =
        dependencies.snapshot(
            coordinates.parent.path);
    result.positive_candidate_before =
        dependencies.snapshot(
            coordinates.positive_candidate.path);
    result.neutral_before =
        dependencies.snapshot(
            coordinates.neutral_artifact.path);
    require_source_identity(
        coordinates.dev1, result.dev1_before);
    require_source_identity(
        coordinates.parent, result.parent_before);
    require_source_identity(
        coordinates.positive_candidate,
        result.positive_candidate_before);
    require_source_identity(
        coordinates.neutral_artifact,
        result.neutral_before);
    result.neutral_identity = {
        .bytes = coordinates.neutral_artifact.bytes,
        .sha256 = coordinates.neutral_artifact.sha256,
    };

    const bundle::Bundle dev1_artifact =
        dependencies.load_dev1(
            coordinates.dev1.path);
    const dev::PreparedCorpus positive =
        dependencies.prepare_dev1(dev1_artifact);
    const auto parent =
        dependencies.load_parent(
            coordinates.parent.path);
    const auto positive_candidate =
        dependencies.load_positive_candidate(
            coordinates.positive_candidate.path,
            parent, candidate::production_contract(),
            {
                .bytes =
                    coordinates.positive_candidate.bytes,
                .sha256 =
                    coordinates.positive_candidate.sha256,
            });
    const neutral::Contract neutral_contract =
        dependencies.make_neutral_contract(
            dev1_artifact.manifest);
    const neutral::Artifact neutral_artifact =
        dependencies.load_neutral(
            coordinates.neutral_artifact.path,
            neutral_contract,
            result.neutral_identity);
    result.evaluation =
        dependencies.evaluate(
            positive, neutral_artifact,
            parent, positive_candidate);

    result.dev1_after =
        dependencies.snapshot(
            coordinates.dev1.path);
    result.parent_after =
        dependencies.snapshot(
            coordinates.parent.path);
    result.positive_candidate_after =
        dependencies.snapshot(
            coordinates.positive_candidate.path);
    result.neutral_after =
        dependencies.snapshot(
            coordinates.neutral_artifact.path);
    if (result.dev1_after != result.dev1_before ||
        result.parent_after != result.parent_before ||
        result.positive_candidate_after !=
            result.positive_candidate_before ||
        result.neutral_after !=
            result.neutral_before) {
        fail("fixed_source_changed_during_evaluation");
    }
    validate_evaluation_identity(result.evaluation);
    return result;
}

RunReport run_fixed() {
    return testing::run_fixed(
        production_dependencies());
}

std::string format_report(const RunReport& report) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(10);
    output
        << "FQ4 DEV5 neutral anchoring gate="
        << (report.evaluation.gate.passed()
                ? "PASS"
                : "FAIL")
        << '\n'
        << "inputs dev1_bytes="
        << report.coordinates.dev1.bytes
        << " dev1_sha256="
        << report.coordinates.dev1.sha256
        << " parent_bytes="
        << report.coordinates.parent.bytes
        << " parent_sha256="
        << report.coordinates.parent.sha256
        << " positive_candidate_bytes="
        << report.coordinates.positive_candidate.bytes
        << " positive_candidate_sha256="
        << report.coordinates.positive_candidate.sha256
        << " neutral_bytes="
        << report.coordinates.neutral_artifact.bytes
        << " neutral_sha256="
        << report.coordinates.neutral_artifact.sha256
        << '\n'
        << "gates baseline_positive_contract_exact="
        << report.evaluation.gate
               .baseline_positive_contract_exact
        << " check_positive_clean="
        << report.evaluation.gate.check_positive_clean
        << " fit_positive_preserved="
        << report.evaluation.gate.fit_positive_preserved
        << " neutral_baseline_nonzero="
        << report.evaluation.gate.neutral_baseline_nonzero
        << " neutral_per_deck_nonworsening="
        << report.evaluation.gate
               .neutral_per_deck_nonworsening
        << " neutral_kl_halved="
        << report.evaluation.gate.neutral_kl_halved
        << " neutral_support_changes_halved="
        << report.evaluation.gate
               .neutral_support_changes_halved
        << " isolation_exact="
        << report.evaluation.gate.isolation_exact
        << '\n'
        << "model anchored="
        << report.evaluation
               .anchored_candidate_fingerprint
        << " training_sha256="
        << report.evaluation.anchored_training
               .accounting.training_input_sha256
        << '\n';
    write_positive_split(
        output, "FIT",
        report.evaluation
            .positive_only_evaluation.metrics.fit,
        report.evaluation
            .anchored_evaluation.metrics.fit);
    write_positive_split(
        output, "CHECK",
        report.evaluation
            .positive_only_evaluation.metrics.check,
        report.evaluation
            .anchored_evaluation.metrics.check);

    const evaluator::NeutralDriftMetrics& neutral_metrics =
        report.evaluation.neutral_check;
    output
        << "neutral scope=pooled"
        << " rows=" << neutral_metrics.rows
        << " options=" << neutral_metrics.options
        << " baseline_kl="
        << neutral_metrics.baseline_equal_deck_kl
        << " anchored_kl="
        << neutral_metrics.anchored_equal_deck_kl
        << " baseline_support_changes="
        << neutral_metrics
               .baseline_exact_support_changes
        << " anchored_support_changes="
        << neutral_metrics
               .anchored_exact_support_changes
        << '\n';
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        const evaluator::NeutralDeckMetrics& value =
            neutral_metrics.decks[deck];
        output
            << "neutral deck="
            << deck_name(static_cast<DeckId>(deck))
            << " rows=" << value.rows
            << " options=" << value.options
            << " baseline_kl="
            << value.baseline_parent_to_candidate_kl
            << " anchored_kl="
            << value.anchored_parent_to_candidate_kl
            << " baseline_support_changes="
            << value.baseline_exact_support_changes
            << " anchored_support_changes="
            << value.anchored_exact_support_changes
            << '\n';
    }
    return output.str();
}

int testing::run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error, const FixedRunner& runner) {
    static_cast<void>(argv);
    if (argc != 1) {
        error
            << "Usage: old-school-fq4-neutral-evaluate\n";
        return 2;
    }
    try {
        if (!runner) {
            fail("missing_fixed_runner");
        }
        const RunReport report = runner();
        output << format_report(report);
        output.flush();
        if (!output.good()) {
            return 2;
        }
        return report.evaluation.gate.passed()
                   ? 0
                   : 1;
    } catch (const std::exception&) {
        error
            << "result=ERROR"
               " reason=fixed_neutral_evaluation_failed\n";
        return 2;
    }
}

int run_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error) {
    return testing::run_cli(
        argc, argv, output, error,
        [] { return run_fixed(); });
}

} // namespace old_school::fq4_neutral_evaluator_runner
