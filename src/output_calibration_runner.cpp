#include "old_school/output_calibration_runner.hpp"

#include "old_school/audit_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
#include <vector>

namespace old_school::output_calibration {
namespace {

namespace common = audit_common;

bool valid_digest(std::string_view digest) {
    return common::is_lower_hex_digest(digest);
}

bool valid_hashes(const CorpusHashes& hashes) {
    const std::array<std::string_view, 11> values = {
        hashes.schedule,
        hashes.outcomes,
        hashes.record_counts,
        hashes.features,
        hashes.targets,
        hashes.weights,
        hashes.optimizer_input,
        hashes.parent_leaf_predictions,
        hashes.parent_predictions,
        hashes.candidate_leaf_predictions,
        hashes.candidate_predictions,
    };
    return std::all_of(
        values.begin(), values.end(), valid_digest);
}

std::size_t checked_physical_games(
    const CollectionConfig& config) {
    if (config.balanced_blocks == 0 ||
        config.balanced_blocks >
            std::numeric_limits<std::size_t>::max() /
                learned_iteration::kBalancedScheduleGames) {
        throw std::invalid_argument(
            "OC1 runner balanced-block count is invalid");
    }
    return config.balanced_blocks *
           learned_iteration::kBalancedScheduleGames;
}

void require_new_output_path(const std::string& path) {
    if (path.empty() ||
        path.find('\0') != std::string::npos ||
        std::filesystem::path(path).filename().empty()) {
        throw std::invalid_argument(
            "OC1 output path must name a new file");
    }
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, error);
    if (error &&
        error != std::errc::no_such_file_or_directory) {
        throw std::system_error(
            error, "cannot inspect OC1 output path");
    }
    if (!error && std::filesystem::exists(status)) {
        throw std::runtime_error(
            "OC1 output path already exists: '" + path + "'");
    }
}

void validate_recipe(const testing::Recipe& recipe) {
    if (recipe.parent_path.empty() ||
        recipe.parent_path.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "OC1 runner parent path is invalid");
    }
    if (recipe.parent.byte_size == 0 ||
        !valid_digest(recipe.parent.sha256) ||
        !valid_digest(recipe.parent.model_fingerprint) ||
        recipe.parent.training_games == 0 ||
        recipe.parent.generations == 0) {
        throw std::invalid_argument(
            "OC1 runner parent identity is malformed");
    }
    const std::size_t fit_games =
        checked_physical_games(recipe.fit);
    const std::size_t holdout_games =
        checked_physical_games(recipe.holdout);
    if (fit_games != holdout_games ||
        recipe.fit.seed == recipe.holdout.seed ||
        recipe.fit.generation == recipe.holdout.generation ||
        recipe.fit.max_game_turns !=
            recipe.holdout.max_game_turns ||
        recipe.fit.pilot_training_games !=
            recipe.holdout.pilot_training_games ||
        recipe.gate.expected_fit != recipe.fit ||
        recipe.gate.expected_holdout != recipe.holdout ||
        recipe.gate.expected_physical_games != fit_games ||
        recipe.gate.expected_perspectives_per_deck >
            std::numeric_limits<std::size_t>::max() /
                kDeckCount ||
        kDeckCount *
                recipe.gate.expected_perspectives_per_deck !=
            2 * fit_games ||
        recipe.optimizer.max_iterations != 32 ||
        recipe.optimizer.l2_tether != 0.01 ||
        recipe.optimizer.gradient_tolerance != 1e-10) {
        throw std::invalid_argument(
            "OC1 runner recipe does not bind one exact "
            "fit/holdout/optimizer protocol");
    }
    require_new_output_path(recipe.output_path);
}

bool finite_estimate(
    const WeightedClusteredEstimate& estimate) {
    return estimate.records > 0 && estimate.clusters > 1 &&
           std::isfinite(estimate.total_weight) &&
           estimate.total_weight > 0.0 &&
           std::isfinite(estimate.mean) &&
           std::isfinite(estimate.standard_error) &&
           estimate.standard_error >= 0.0 &&
           std::isfinite(estimate.confidence_lower_95) &&
           std::isfinite(estimate.confidence_upper_95);
}

bool finite_model_metrics(const ModelMetrics& metrics) {
    return finite_estimate(metrics.brier) &&
           finite_estimate(metrics.soft_log_loss) &&
           finite_estimate(metrics.signed_bias) &&
           std::isfinite(metrics.prediction_mean) &&
           std::isfinite(metrics.saturated_weight) &&
           metrics.saturated_weight >= 0.0 &&
           std::isfinite(metrics.saturation_fraction) &&
           metrics.saturation_fraction >= 0.0 &&
           metrics.saturation_fraction <= 1.0;
}

bool finite_scope(const ScopeReport& scope) {
    return scope.records > 0 &&
           scope.physical_games > 1 &&
           scope.actor_perspectives > 0 &&
           std::isfinite(scope.total_weight) &&
           scope.total_weight > 0.0 &&
           std::isfinite(scope.target_mean) &&
           finite_model_metrics(scope.parent) &&
           finite_model_metrics(scope.candidate) &&
           finite_estimate(
               scope.candidate_minus_parent.brier_delta) &&
           finite_estimate(
               scope.candidate_minus_parent
                   .soft_log_loss_delta);
}

bool finite_scientific_report(
    const HoldoutReport& report,
    const OutputCalibrationArtifactReport& artifact) {
    if (!finite_scope(report.pooled) ||
        !std::isfinite(
            artifact.optimizer_diagnostics
                .before_weighted_bce) ||
        !std::isfinite(
            artifact.optimizer_diagnostics
                .after_weighted_bce) ||
        !std::isfinite(
            artifact.optimizer_diagnostics
                .max_parameter_delta) ||
        !std::isfinite(
            artifact.output_parameters
                .maximum_absolute_delta)) {
        return false;
    }
    return std::all_of(
        report.by_deck.begin(), report.by_deck.end(),
        finite_scope);
}

bool component_isolation_exact(
    const OutputCalibrationArtifactReport& report,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate) {
    const auto parent_components =
        learned_model_component_fingerprints(parent);
    const auto candidate_components =
        learned_model_component_fingerprints(candidate);
    const auto parent_tensors =
        learned_critic_tensor_fingerprints(parent);
    const auto candidate_tensors =
        learned_critic_tensor_fingerprints(candidate);
    return report.parent_components == parent_components &&
           report.candidate_components ==
               candidate_components &&
           report.parent_tensors == parent_tensors &&
           report.candidate_tensors == candidate_tensors &&
           parent_components.priority ==
               candidate_components.priority &&
           parent_components.attack ==
               candidate_components.attack &&
           parent_components.block ==
               candidate_components.block &&
           parent_components.damage_order ==
               candidate_components.damage_order &&
           parent_tensors.input_hidden ==
               candidate_tensors.input_hidden &&
           parent_tensors.direct_paths ==
               candidate_tensors.direct_paths;
}

bool artifact_roundtrip_exact(
    const OutputCalibrationArtifact& created,
    const OutputCalibrationArtifact& loaded) {
    return created.report() == loaded.report() &&
           learned_model_fingerprint(created.model()) ==
               learned_model_fingerprint(loaded.model()) &&
           learned_output_calibration_parameters(
               created.model()) ==
               learned_output_calibration_parameters(
                   loaded.model());
}

HoldoutReport score_hidden_repartitioned_records(
    const HoldoutCorpus& corpus) {
    if (!corpus.accounting.hidden.bit_identical() ||
        !corpus.accounting.hidden.nonvacuous_all_decks()) {
        throw std::runtime_error(
            "OC1 hidden holdout rows are not a nonvacuous "
            "bit-identical scientific input");
    }
    const std::vector<HoldoutRecord> repartitioned =
        repartitioned_holdout_records(corpus.records);
    return score_holdout_records(repartitioned);
}

void write_corpus_hashes(
    std::ostream& output, std::string_view prefix,
    const CorpusHashes& hashes) {
    output << prefix
           << " schedule=" << hashes.schedule
           << " outcomes=" << hashes.outcomes
           << " counts=" << hashes.record_counts << '\n'
           << prefix
           << " features=" << hashes.features
           << " targets=" << hashes.targets
           << " weights=" << hashes.weights
           << " optimizer=" << hashes.optimizer_input << '\n'
           << prefix
           << " parent-leaf="
           << hashes.parent_leaf_predictions
           << " parent=" << hashes.parent_predictions
           << " candidate-leaf="
           << hashes.candidate_leaf_predictions
           << " candidate=" << hashes.candidate_predictions
           << '\n';
}

void write_collection_accounting(
    std::ostream& output, std::string_view name,
    const CollectionAccounting& accounting) {
    output << name << " deck accounting:\n";
    for (std::size_t index = 0; index < kDeckCount;
         ++index) {
        const DeckAccounting& deck =
            accounting.by_deck[index];
        const HiddenDeckCounts& hidden =
            accounting.hidden.by_deck[index];
        const auto& cells =
            accounting.schedule.deck_seat_start[index];
        output
            << "  "
            << deck_name(static_cast<DeckId>(index))
            << ": perspectives=" << deck.perspectives
            << " records=" << deck.records
            << " mass="
            << common::format_real(deck.total_weight)
            << " seat/start=["
            << cells[0][0] << ',' << cells[0][1]
            << ';' << cells[1][0] << ','
            << cells[1][1] << "] hidden="
            << hidden.changed << '/' << hidden.attempted
            << '\n';
    }
}

void write_scope(
    std::ostream& output, std::string_view name,
    const ScopeReport& scope) {
    const auto real = [](double value) {
        return common::format_real(value);
    };
    output << "  " << name
           << ": records=" << scope.records
           << " games=" << scope.physical_games
           << " perspectives=" << scope.actor_perspectives
           << " mass=" << real(scope.total_weight)
           << " target=" << real(scope.target_mean) << '\n'
           << "    Brier C16="
           << real(scope.parent.brier.mean)
           << " OC1=" << real(scope.candidate.brier.mean)
           << " delta="
           << real(
                  scope.candidate_minus_parent
                      .brier_delta.mean)
           << " CI=["
           << real(
                  scope.candidate_minus_parent
                      .brier_delta.confidence_lower_95)
           << ","
           << real(
                  scope.candidate_minus_parent
                      .brier_delta.confidence_upper_95)
           << "]\n"
           << "    LogLoss C16="
           << real(scope.parent.soft_log_loss.mean)
           << " OC1="
           << real(scope.candidate.soft_log_loss.mean)
           << " delta="
           << real(
                  scope.candidate_minus_parent
                      .soft_log_loss_delta.mean)
           << " CI=["
           << real(
                  scope.candidate_minus_parent
                      .soft_log_loss_delta
                      .confidence_lower_95)
           << ","
           << real(
                  scope.candidate_minus_parent
                      .soft_log_loss_delta
                      .confidence_upper_95)
           << "]\n"
           << "    Bias C16="
           << real(scope.parent.signed_bias.mean)
           << " OC1="
           << real(scope.candidate.signed_bias.mean)
           << " prediction C16="
           << real(scope.parent.prediction_mean)
           << " OC1="
           << real(scope.candidate.prediction_mean)
           << " saturation C16="
           << real(scope.parent.saturation_fraction)
           << " OC1="
           << real(scope.candidate.saturation_fraction)
           << '\n';
}

void write_report(
    const OutputCalibrationRunReport& report,
    std::ostream& output) {
    const auto real = [](double value) {
        return common::format_real(value);
    };
    output
        << "OC1 frozen-representation output calibration\n"
        << "Parent: bytes=" << report.parent_before.byte_size
        << " sha256=" << report.parent_before.sha256
        << " model=" << report.artifact.parent.model_fingerprint
        << '\n'
        << "Fit: games="
        << report.artifact.fit_accounting
               .schedule.physical_games
        << " perspectives="
        << report.artifact.fit_accounting.actor_perspectives
        << " records="
        << report.artifact.fit_accounting.records
        << " mass="
        << real(report.artifact.fit_accounting.total_weight)
        << '\n';
    write_collection_accounting(
        output, "Fit", report.artifact.fit_accounting);
    write_corpus_hashes(
        output, "Fit hashes:",
        report.artifact.fit_hashes);
    output
        << "Optimizer: iterations="
        << report.artifact.optimizer_diagnostics.iterations
        << " before-bce="
        << real(
               report.artifact.optimizer_diagnostics
                   .before_weighted_bce)
        << " after-bce="
        << real(
               report.artifact.optimizer_diagnostics
                   .after_weighted_bce)
        << " max-delta="
        << real(
               report.artifact.optimizer_diagnostics
                   .max_parameter_delta)
        << '\n'
        << "Fit hidden parameter hashes: original="
        << report.original_fit_parameters_hash
        << " repartitioned="
        << report.repartitioned_fit_parameters_hash
        << '\n'
        << "Artifact: " << report.output_path
        << " bytes=" << report.artifact_published.byte_size
        << " sha256=" << report.artifact_published.sha256
        << " candidate="
        << report.artifact.candidate_fingerprint << '\n'
        << "Holdout: games="
        << report.holdout_accounting
               .schedule.physical_games
        << " perspectives="
        << report.holdout_accounting.actor_perspectives
        << " records=" << report.holdout_accounting.records
        << " mass="
        << real(report.holdout_accounting.total_weight)
        << '\n';
    write_collection_accounting(
        output, "Holdout", report.holdout_accounting);
    write_corpus_hashes(
        output, "Holdout hashes:", report.holdout_hashes);
    output
        << "Hidden scientific report hashes: original="
        << report.original_scientific_report_hash
        << " repartitioned="
        << report.repartitioned_scientific_report_hash
        << '\n';

    write_scope(output, "Pooled", report.scientific.pooled);
    for (std::size_t index = 0; index < kDeckCount;
         ++index) {
        write_scope(
            output,
            deck_name(static_cast<DeckId>(index)),
            report.scientific.by_deck[index]);
    }

    const int status =
        output_calibration_exit_code(report.gate);
    output << "Gate: "
           << (status == 0
                   ? "PASS"
                   : status == 1
                         ? "SCIENTIFIC REJECT"
                         : "INFRASTRUCTURE FAILURE")
           << '\n';
    for (const std::string& failure :
         report.gate.failures) {
        output << "  - " << failure << '\n';
    }
}

testing::Recipe canonical_recipe_impl(
    const std::string& output_path) {
    const CollectionConfig fit = canonical_fit_config();
    const CollectionConfig holdout =
        canonical_holdout_config();
    return {
        .parent_path =
            std::string(kCanonicalParentArtifactPath),
        .parent =
            {
                .byte_size =
                    kCanonicalParentArtifactByteSize,
                .sha256 =
                    std::string(
                        kCanonicalParentArtifactSha256),
                .model_fingerprint =
                    std::string(
                        kCanonicalParentModelFingerprint),
                .training_games =
                    kCanonicalParentTrainingGames,
                .training_seed =
                    kCanonicalParentTrainingSeed,
                .generations =
                    kCanonicalParentGenerations,
            },
        .fit = fit,
        .holdout = holdout,
        .optimizer =
            {
                .max_iterations = 32,
                .l2_tether = 0.01,
                .gradient_tolerance = 1e-10,
            },
        .gate =
            {
                .expected_fit = fit,
                .expected_holdout = holdout,
                .expected_physical_games =
                    kPhysicalGames,
                .expected_perspectives_per_deck =
                    kPerspectivesPerDeck,
                .deck_loss_guard = kDeckLossGuard,
                .other_deck_bias_guard =
                    kOtherDeckBiasGuard,
                .material_bias_threshold =
                    kMaterialBiasThreshold,
            },
        .output_path = output_path,
    };
}

} // namespace

namespace testing {

Recipe canonical_recipe(const std::string& output_path) {
    return canonical_recipe_impl(output_path);
}

OutputCalibrationRunReport run_output_calibration(
    const Recipe& recipe, std::ostream& progress) {
    validate_recipe(recipe);

    OutputCalibrationRunReport result;
    result.output_path = recipe.output_path;
    result.parent_before =
        artifact_integrity::snapshot_regular_file(
            recipe.parent_path);
    if (result.parent_before.byte_size !=
            recipe.parent.byte_size ||
        result.parent_before.sha256 !=
            recipe.parent.sha256) {
        throw std::runtime_error(
            "OC1 runner parent preflight identity mismatch");
    }

    progress << "OC1: verifying exact frozen parent...\n";
    const auto loaded_parent =
        load_learned_value_challenger_artifact(
            recipe.parent_path,
            recipe.parent.training_games,
            recipe.parent.training_seed,
            recipe.parent.generations);
    const VerifiedParentArtifact parent =
        verify_output_calibration_parent(
            recipe.parent_path, loaded_parent.model(),
            recipe.parent);

    progress << "OC1: collecting frozen-parent fit corpus...\n";
    const TrainingCorpus fit =
        collect_training_corpus(parent.model(), recipe.fit);

    progress
        << "OC1: reproducing fit, calibrating twice, and "
           "building artifact...\n";
    const OutputCalibrationArtifact created =
        make_output_calibration_artifact(
            parent, fit, recipe.optimizer);

    progress << "OC1: publishing new artifact atomically...\n";
    write_output_calibration_artifact_atomic_no_replace(
        recipe.output_path, created);
    result.artifact_published =
        artifact_integrity::snapshot_regular_file(
            recipe.output_path);

    progress << "OC1: reloading and verifying artifact...\n";
    const OutputCalibrationArtifact reloaded =
        load_output_calibration_artifact(
            recipe.output_path, parent, recipe.fit,
            recipe.optimizer);
    result.artifact_reloaded =
        artifact_integrity::snapshot_regular_file(
            recipe.output_path);
    if (result.artifact_published !=
        result.artifact_reloaded) {
        throw std::runtime_error(
            "OC1 artifact changed across reload");
    }

    progress
        << "OC1: collecting untouched holdout twice...\n";
    const HoldoutCorpus holdout =
        collect_holdout_corpus(
            parent.model(), reloaded.model(),
            recipe.holdout);
    const HoldoutCorpus reproduced_holdout =
        collect_holdout_corpus(
            parent.model(), reloaded.model(),
            recipe.holdout);
    if (holdout != reproduced_holdout) {
        throw std::runtime_error(
            "OC1 holdout reproduction is not bit-identical");
    }

    const HoldoutReport scientific =
        score_holdout_records(holdout.records);
    const HoldoutReport reproduced_scientific =
        score_holdout_records(reproduced_holdout.records);
    const HoldoutReport hidden_scientific =
        score_hidden_repartitioned_records(holdout);
    const std::string original_scientific_hash =
        hash_holdout_report(scientific);
    const std::string repartitioned_scientific_hash =
        hash_holdout_report(hidden_scientific);

    const OutputCalibrationArtifactReport& artifact =
        reloaded.report();
    result.artifact = artifact;
    result.holdout_accounting = holdout.accounting;
    result.holdout_hashes = holdout.hashes;
    result.scientific = scientific;
    result.original_fit_parameters_hash =
        artifact.hidden_refit.original_parameters_sha256;
    result.repartitioned_fit_parameters_hash =
        artifact.hidden_refit
            .repartitioned_parameters_sha256;
    result.original_scientific_report_hash =
        original_scientific_hash;
    result.repartitioned_scientific_report_hash =
        repartitioned_scientific_hash;

    const bool parent_identity =
        parent.identity() == recipe.parent &&
        parent.model() &&
        learned_model_fingerprint(parent.model()) ==
            recipe.parent.model_fingerprint;
    const bool fit_provenance =
        fit.accounting == artifact.fit_accounting &&
        fit.hashes == artifact.fit_hashes &&
        artifact.fit_config == recipe.fit &&
        artifact.parent == recipe.parent &&
        valid_hashes(fit.hashes);
    const bool holdout_provenance =
        holdout.accounting.config == recipe.holdout &&
        holdout.tasks == collection_schedule(recipe.holdout) &&
        !holdout.records.empty() &&
        valid_hashes(holdout.hashes) &&
        holdout.accounting.hidden.bit_identical() &&
        holdout.accounting.hidden.nonvacuous_all_decks();
    const bool component_isolation =
        component_isolation_exact(
            artifact, parent.model(), reloaded.model());
    const bool artifact_exact =
        result.artifact_published ==
            result.artifact_reloaded &&
        artifact_roundtrip_exact(created, reloaded) &&
        valid_digest(result.artifact_published.sha256) &&
        learned_model_fingerprint(reloaded.model()) ==
            artifact.candidate_fingerprint;
    const bool deterministic =
        holdout == reproduced_holdout &&
        scientific == reproduced_scientific &&
        scientific == hidden_scientific &&
        valid_digest(original_scientific_hash) &&
        original_scientific_hash ==
            repartitioned_scientific_hash &&
        holdout.accounting.hidden.bit_identical() &&
        artifact.hidden_refit
                .original_parameters_sha256 ==
            artifact.hidden_refit
                .repartitioned_parameters_sha256 &&
        artifact.hidden_refit
                .original_candidate_fingerprint ==
            artifact.hidden_refit
                .repartitioned_candidate_fingerprint;
    const bool finite_values =
        finite_scientific_report(scientific, artifact);

    result.integrity = {
        .parent_identity = parent_identity,
        .fit_provenance = fit_provenance,
        .holdout_provenance = holdout_provenance,
        .component_isolation = component_isolation,
        .artifact = artifact_exact,
        .determinism = deterministic,
        .finite_values = finite_values,
        .original_fit_parameters_hash =
            result.original_fit_parameters_hash,
        .repartitioned_fit_parameters_hash =
            result.repartitioned_fit_parameters_hash,
        .original_scientific_report_hash =
            result.original_scientific_report_hash,
        .repartitioned_scientific_report_hash =
            result.repartitioned_scientific_report_hash,
    };

    progress << "OC1: evaluating conjunctive holdout gate...\n";
    result.gate = evaluate_gate(
        scientific, fit.accounting, holdout.accounting,
        result.integrity, recipe.gate);

    result.parent_after =
        artifact_integrity::snapshot_regular_file(
            recipe.parent_path);
    result.artifact_after =
        artifact_integrity::snapshot_regular_file(
            recipe.output_path);
    if (result.parent_before != result.parent_after) {
        throw std::runtime_error(
            "OC1 parent changed during the one-shot run");
    }
    if (result.artifact_published !=
        result.artifact_after) {
        throw std::runtime_error(
            "OC1 artifact changed during the one-shot run");
    }

    write_report(result, progress);
    return result;
}

} // namespace testing

OutputCalibrationRunReport run_output_calibration(
    const std::string& output_path, std::ostream& progress) {
    return testing::run_output_calibration(
        canonical_recipe_impl(output_path), progress);
}

OutputCalibrationRunReport run_output_calibration(
    std::ostream& progress) {
    return run_output_calibration(
        std::string(kCanonicalOutputArtifactPath), progress);
}

int output_calibration_exit_code(const GateReport& gate) {
    if (!gate.integrity_passed ||
        !gate.collection_accounting_exact) {
        return 2;
    }
    return gate.passed ? 0 : 1;
}

int run_output_calibration_cli(
    int argc, char* argv[], std::ostream& output,
    std::ostream& error) {
    if (argc < 1 || argv == nullptr || argc > 2) {
        error
            << "Usage: old-school-output-calibration "
               "[NEW_OUTPUT_PATH]\n";
        return 2;
    }
    try {
        const OutputCalibrationRunReport report =
            argc == 2
                ? run_output_calibration(argv[1], output)
                : run_output_calibration(output);
        return output_calibration_exit_code(report.gate);
    } catch (const std::exception& exception) {
        error << "OC1 infrastructure failure: "
              << exception.what() << '\n';
        return 2;
    }
}

} // namespace old_school::output_calibration
