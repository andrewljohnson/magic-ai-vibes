#include "old_school/fq4_d1_treatment.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::fq4_d1_treatment {
namespace {

namespace field = fq4_d1_field_gate;
namespace fit = fq4_priority_fit;
namespace integrity = artifact_integrity;

using Clock = std::chrono::steady_clock;

bool same_double(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) ==
           std::bit_cast<std::uint64_t>(second);
}

bool bit_identical(
    const std::vector<double>& first,
    const std::vector<double>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!same_double(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool nested_bit_identical(
    const std::vector<std::vector<double>>& first,
    const std::vector<std::vector<double>>& second) {
    if (first.size() != second.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.size(); ++index) {
        if (!bit_identical(first[index], second[index])) {
            return false;
        }
    }
    return true;
}

bool finite_vector(const std::vector<double>& values) {
    return std::all_of(
        values.begin(), values.end(),
        [](double value) {
            return std::isfinite(value);
        });
}

bool finite_matrix(
    const std::vector<std::vector<double>>& values) {
    return std::all_of(
        values.begin(), values.end(),
        [](const std::vector<double>& row) {
            return finite_vector(row);
        });
}

bool valid_deck(DeckId deck) {
    return static_cast<std::size_t>(deck) < kDeckCount;
}

std::optional<std::size_t> class_index(
    field::ParentClass classification) {
    switch (classification) {
    case field::ParentClass::Safe:
        return 0;
    case field::ParentClass::Class1:
        return 1;
    case field::ParentClass::Class2:
        return 2;
    case field::ParentClass::Class3:
        return 3;
    case field::ParentClass::Invalid:
        return std::nullopt;
    }
    return std::nullopt;
}

bool sha256_shape(std::string_view value) {
    return value.size() == 64 &&
           std::all_of(
               value.begin(), value.end(),
               [](char character) {
                   return
                       (character >= '0' &&
                        character <= '9') ||
                       (character >= 'a' &&
                        character <= 'f');
               });
}

bool components_have_sha256_shape(
    const LearnedModelComponentFingerprints& components) {
    return sha256_shape(components.critic) &&
           sha256_shape(components.priority) &&
           sha256_shape(components.attack) &&
           sha256_shape(components.block) &&
           sha256_shape(components.damage_order);
}

bool priority_only_changed(
    const LearnedModelComponentFingerprints& parent,
    const LearnedModelComponentFingerprints& candidate) {
    return
        parent.critic == candidate.critic &&
        parent.priority != candidate.priority &&
        parent.attack == candidate.attack &&
        parent.block == candidate.block &&
        parent.damage_order == candidate.damage_order;
}

void append_failure(
    TreatmentReport& report, std::string message) {
    report.infrastructure_failures.push_back(
        std::move(message));
}

bool stripped_row_shape_valid(const TreatmentRow& row) {
    const std::size_t count =
        row.canonical_descriptors.size();
    if (row.stable_id.empty() ||
        !sha256_shape(row.physical_game_id) ||
        !valid_deck(row.owner_deck) ||
        count < 2 ||
        count > field::kMaximumLegalActions ||
        row.options.size() != count ||
        row.robustly_pass_dominated.size() != count ||
        row.base_scores.size() != count ||
        row.base_samples.size() != count ||
        row.parent_residuals.size() != count ||
        row.parent_combined_scores.size() != count ||
        !std::is_sorted(
            row.canonical_descriptors.begin(),
            row.canonical_descriptors.end()) ||
        std::adjacent_find(
            row.canonical_descriptors.begin(),
            row.canonical_descriptors.end()) !=
            row.canonical_descriptors.end() ||
        !finite_vector(row.base_scores) ||
        !finite_matrix(row.base_samples) ||
        !finite_vector(row.parent_residuals) ||
        !finite_vector(row.parent_combined_scores) ||
        !row.parent_class.valid ||
        !class_index(
             row.parent_class.classification)
             .has_value()) {
        return false;
    }
    if (!std::all_of(
            row.base_samples.begin(),
            row.base_samples.end(),
            [](const std::vector<double>& samples) {
                return samples.size() ==
                       field::kDominanceWorlds;
            }) ||
        !std::any_of(
            row.robustly_pass_dominated.begin(),
            row.robustly_pass_dominated.end(),
            [](bool value) {
                return value;
            }) ||
        !std::any_of(
            row.robustly_pass_dominated.begin(),
            row.robustly_pass_dominated.end(),
            [](bool value) {
                return !value;
            })) {
        return false;
    }
    for (std::size_t index = 0;
         index < count; ++index) {
        const LabeledOption& option = row.options[index];
        if (option.descriptor !=
                row.canonical_descriptors[index] ||
            !finite_vector(option.visible_tensor) ||
            !finite_vector(option.hidden_tensor) ||
            !bit_identical(
                option.visible_tensor,
                option.hidden_tensor)) {
            return false;
        }
    }
    return true;
}

ClassCounts class_counts(
    const field::RootCounts& counts) {
    return {
        .classes = counts.parent_classes,
    };
}

bool expected_accounting(
    const field::ProductionAccounting& accounting) {
    return
        accounting.score_calls == kExpectedScoreCalls &&
        accounting.scored_actions ==
            kExpectedScoredActions &&
        accounting.sampled_worlds ==
            kExpectedSampledWorlds &&
        accounting.rollout_evaluations ==
            kExpectedRolloutEvaluations &&
        accounting.terminal_evaluations ==
            kExpectedTerminalEvaluations &&
        accounting.bootstrapped_evaluations ==
            kExpectedBootstrapEvaluations &&
        accounting.dominance_transitions ==
            kExpectedDominanceTransitions;
}

bool parent_rows_match_contract(
    const field::CensusReport& census) {
    if (census.scored_roots.size() !=
        kExpectedScoredRoots) {
        return false;
    }
    std::array<ClassCounts, kDeckCount> decks{};
    ClassCounts pooled;
    std::set<std::string> stable_ids;
    double sigma_mass = 0.0;
    for (const field::ScoredRoot& root :
         census.scored_roots) {
        const std::size_t deck =
            static_cast<std::size_t>(
                root.manifest.owner_deck);
        const std::optional<std::size_t> classification =
            class_index(
                root.parent_class.classification);
        if (deck >= kDeckCount ||
            !classification.has_value() ||
            !root.parent_class.valid ||
            !stable_ids.insert(
                 root.manifest.stable_id).second) {
            return false;
        }
        ++decks[deck].classes[*classification];
        ++pooled.classes[*classification];
        if (root.parent_class.classification ==
            field::ParentClass::Class2) {
            sigma_mass += root.parent_class.sigma;
            if (!std::isfinite(sigma_mass)) {
                return false;
            }
        }
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (decks[deck].classes !=
            kExpectedParentClassesByDeck[deck]) {
            return false;
        }
    }
    return pooled.classes ==
               kExpectedPooledParentClasses &&
           std::bit_cast<std::uint64_t>(sigma_mass) ==
               kParentClass2SigmaMassBits;
}

ParentReconstructionSummary parent_summary(
    const field::CensusReport& census,
    std::string_view artifact_sha256,
    std::string_view actual_fingerprint,
    const LearnedModelComponentFingerprints&
        actual_components) {
    ParentReconstructionSummary result{
        .artifact_sha256 =
            std::string(artifact_sha256),
        .model_fingerprint = census.parent_fingerprint,
        .model_components = census.parent_components,
        .schedule_sha256 = census.schedule_sha256,
        .trajectory_sha256 =
            census.trajectory_sha256,
        .retained_corpus_sha256 =
            census.retained_corpus_sha256,
        .dominance_corpus_sha256 =
            census.dominance_corpus_sha256,
        .scored_corpus_sha256 =
            census.scored_corpus_sha256,
        .audit_scores_sha256 =
            census.audit_scores_sha256,
        .primary_accounting =
            census.primary_accounting,
        .hidden_control_accounting =
            census.hidden_control_accounting,
        .reverse_control_accounting =
            census.reverse_control_accounting,
        .accounting = census.accounting,
        .repeat_accounting =
            census.repeat_accounting,
        .physical_games =
            census.schedule_balance.physical_games,
        .owner_perspectives =
            census.schedule_balance.owner_perspectives,
        .retained_roots = census.pooled.retained,
        .scored_roots =
            census.scored_roots.size(),
        .class2_sigma_mass =
            census.class2_sigma_mass,
        .high_confidence_games =
            census.distinct_high_confidence_games,
        .high_confidence_decks =
            census.distinct_high_confidence_decks,
        .census_passed = census.passed(),
        .hidden_replay_exact =
            census.all_replays_exact &&
            std::all_of(
                census.scored_roots.begin(),
                census.scored_roots.end(),
                [](const field::ScoredRoot& root) {
                    return
                        root.hidden_replay_bit_identical;
                }),
        .hidden_feature_bits_identical =
            census.all_hidden_feature_bits_identical &&
            std::all_of(
                census.scored_roots.begin(),
                census.scored_roots.end(),
                [](const field::ScoredRoot& root) {
                    return
                        root.hidden_feature_bits_identical &&
                        nested_bit_identical(
                            root.neutral_priority_options,
                            root
                                .hidden_neutral_priority_options);
                }),
        .reverse_score_bits_identical =
            census.first_deck_controls_bit_identical,
        .recipe_and_accounting_exact =
            census.recipe_and_accounting_exact,
        .count_cross_sums_exact =
            census.count_cross_sums_exact,
        .repeated_construction_bit_identical =
            census.repeated_construction_bit_identical,
    };
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        result.decks[deck] =
            class_counts(census.decks[deck]);
    }
    result.pooled = class_counts(census.pooled);
    bool classes_exact =
        result.pooled.classes ==
        kExpectedPooledParentClasses;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        classes_exact =
            classes_exact &&
            result.decks[deck].classes ==
                kExpectedParentClassesByDeck[deck];
    }
    result.exact =
        result.census_passed &&
        result.artifact_sha256 ==
            kRequiredParentArtifactSha256 &&
        actual_fingerprint ==
            field::kRequiredParentFingerprint &&
        result.model_fingerprint ==
            actual_fingerprint &&
        result.model_components ==
            actual_components &&
        components_have_sha256_shape(
            result.model_components) &&
        result.schedule_sha256 ==
            field::kExpectedScheduleSha256 &&
        result.trajectory_sha256 ==
            kExpectedTrajectorySha256 &&
        result.retained_corpus_sha256 ==
            kExpectedRetainedCorpusSha256 &&
        result.dominance_corpus_sha256 ==
            kExpectedDominanceCorpusSha256 &&
        result.scored_corpus_sha256 ==
            kExpectedScoredCorpusSha256 &&
        result.audit_scores_sha256 ==
            kExpectedAuditScoresSha256 &&
        result.physical_games ==
            kExpectedPhysicalGames &&
        result.owner_perspectives ==
            kExpectedOwnerPerspectives &&
        result.retained_roots ==
            kExpectedRetainedRoots &&
        result.scored_roots ==
            kExpectedScoredRoots &&
        census.pooled.dominance_positive ==
            kExpectedScoredRoots &&
        expected_accounting(result.accounting) &&
        result.repeat_accounting ==
            result.accounting &&
        std::bit_cast<std::uint64_t>(
            result.class2_sigma_mass) ==
            kParentClass2SigmaMassBits &&
        result.high_confidence_games ==
            kExpectedHighConfidenceGames &&
        result.high_confidence_decks ==
            kExpectedHighConfidenceDecks &&
        result.hidden_replay_exact &&
        result.hidden_feature_bits_identical &&
        result.reverse_score_bits_identical &&
        result.recipe_and_accounting_exact &&
        result.count_cross_sums_exact &&
        result.repeated_construction_bit_identical &&
        classes_exact &&
        parent_rows_match_contract(census);
    return result;
}

std::vector<double> candidate_margins(
    const fit::FitReport& report) {
    std::vector<double> result;
    for (const fit::TrainingRootReport& root :
         report.roots) {
        for (const fit::DominanceConstraintReport& constraint :
             root.constraints) {
            result.push_back(
                constraint.candidate_margin);
        }
    }
    return result;
}

bool every_control_passed(
    const std::vector<fit::ControlReport>& controls) {
    return
        controls.size() == fit::kExpectedControls &&
        std::all_of(
            controls.begin(), controls.end(),
            [](const fit::ControlReport& control) {
                return control.passed;
            });
}

D0bQualificationSummary d0b_summary(
    const fit::D0bReport& report,
    const std::shared_ptr<const LearnedModel>& parent) {
    const auto candidate =
        report.treatment.fit.candidate;
    D0bQualificationSummary result{
        .parent_fingerprint =
            report.treatment.fit.parent_fingerprint,
        .anchor_candidate_fingerprint =
            report.anchor.fit.candidate_fingerprint,
        .candidate_fingerprint =
            report.treatment.fit.candidate_fingerprint,
        .training_input_sha256 =
            report.treatment.fit.training_input_sha256,
        .parent_components =
            report.treatment.fit.parent_components,
        .candidate_components =
            report.treatment.fit.candidate_components,
        .parent_margins = report.parent_margins,
        .candidate_margins =
            candidate_margins(report.treatment.fit),
        .anchor_controls =
            report.anchor.fit.controls,
        .treatment_controls =
            report.treatment.fit.controls,
        .anchor_epochs = report.anchor.epochs,
        .treatment_epochs =
            report.treatment.epochs,
        .discovered_constraints =
            report.treatment.fit.discovered_constraints,
        .candidate_margins_at_gate =
            report.treatment.fit
                .candidate_margins_at_gate,
        .anchor_pooled_kl =
            report.anchor
                .pooled_target_to_candidate_kl,
        .treatment_pooled_kl =
            report.treatment
                .pooled_target_to_candidate_kl,
        .report_passed = report.passed(),
        .parent_contract_qualified =
            report.parent_contract_qualified,
        .anchor_contract_qualified =
            report.anchor_contract_qualified,
        .optimizer_only_epochs_differ =
            report.optimizer_only_epochs_differ,
        .checkpoint_inputs_bit_identical =
            report.checkpoint_inputs_bit_identical,
        .target_kl_strictly_improved =
            report.target_kl_strictly_improved,
        .candidate_fingerprint_exact =
            candidate != nullptr &&
            report.treatment.fit.candidate_fingerprint ==
                fit::kD0bRequiredTreatmentFingerprint &&
            learned_model_fingerprint(candidate) ==
                report.treatment.fit
                    .candidate_fingerprint,
        .only_priority_component_changed =
            report.treatment.fit
                .only_priority_component_changed &&
            priority_only_changed(
                report.treatment.fit.parent_components,
                report.treatment.fit.candidate_components),
        .repeated_fit_bit_identical =
            report.anchor.fit
                .repeated_fit_bit_identical &&
            report.treatment.fit
                .repeated_fit_bit_identical,
        .hidden_repartition_bit_identical =
            report.anchor.fit
                .hidden_repartition_bit_identical &&
            report.treatment.fit
                .hidden_repartition_bit_identical,
        .action_order_bit_identical =
            report.anchor.fit.action_order_bit_identical &&
            report.treatment.fit.action_order_bit_identical,
        .immutable_base_and_accounting =
            report.anchor.fit
                .all_production_base_and_accounting_bit_identical &&
            report.treatment.fit
                .all_production_base_and_accounting_bit_identical &&
            report.anchor.fit.parent_immutable &&
            report.treatment.fit.parent_immutable,
        .every_control_passed =
            report.anchor.fit.every_control_passed &&
            report.treatment.fit.every_control_passed &&
            every_control_passed(
                report.anchor.fit.controls) &&
            every_control_passed(
                report.treatment.fit.controls),
    };
    const bool margins_exact =
        result.candidate_margins.size() ==
            fit::kExpectedDominanceConstraints &&
        std::all_of(
            result.candidate_margins.begin(),
            result.candidate_margins.end(),
            [](double margin) {
                return std::isfinite(margin) &&
                       margin >= fit::kGateScoreMargin;
            });
    const std::string actual_parent_fingerprint =
        parent
            ? learned_model_fingerprint(parent)
            : std::string();
    const LearnedModelComponentFingerprints
        actual_parent_components =
            parent
                ? learned_model_component_fingerprints(
                      parent)
                : LearnedModelComponentFingerprints{};
    const LearnedModelComponentFingerprints
        actual_candidate_components =
            candidate
                ? learned_model_component_fingerprints(
                      candidate)
                : LearnedModelComponentFingerprints{};
    result.exact =
        result.report_passed &&
        report.infrastructure_valid() &&
        report.scientific_failures.empty() &&
        result.parent_contract_qualified &&
        result.anchor_contract_qualified &&
        result.optimizer_only_epochs_differ &&
        result.checkpoint_inputs_bit_identical &&
        result.target_kl_strictly_improved &&
        result.anchor_candidate_fingerprint ==
            fit::kD0bRequiredAnchorFingerprint &&
        result.candidate_fingerprint_exact &&
        result.parent_fingerprint ==
            field::kRequiredParentFingerprint &&
        result.parent_fingerprint ==
            actual_parent_fingerprint &&
        result.parent_components ==
            actual_parent_components &&
        result.candidate_components ==
            actual_candidate_components &&
        result.training_input_sha256 ==
            report.anchor.fit.training_input_sha256 &&
        sha256_shape(result.training_input_sha256) &&
        result.anchor_epochs == fit::kD0bAnchorEpochs &&
        result.treatment_epochs ==
            fit::kD0bTreatmentEpochs &&
        result.discovered_constraints ==
            fit::kExpectedDominanceConstraints &&
        result.candidate_margins_at_gate ==
            fit::kExpectedDominanceConstraints &&
        result.only_priority_component_changed &&
        result.repeated_fit_bit_identical &&
        result.hidden_repartition_bit_identical &&
        result.action_order_bit_identical &&
        result.immutable_base_and_accounting &&
        result.every_control_passed &&
        margins_exact;
    return result;
}

} // namespace

testing::RowAdaptation testing::strip_scored_roots(
    const std::vector<field::ScoredRoot>& roots) {
    RowAdaptation result;
    result.rows.reserve(roots.size());
    std::set<std::string> stable_ids;
    for (const field::ScoredRoot& root : roots) {
        const std::string prefix =
            root.manifest.stable_id.empty()
                ? std::string("<empty>")
                : root.manifest.stable_id;
        const std::size_t count =
            root.manifest.canonical_descriptors.size();
        const bool shape =
            !root.manifest.stable_id.empty() &&
            stable_ids.insert(
                root.manifest.stable_id).second &&
            valid_deck(root.manifest.owner_deck) &&
            count >= 2 &&
            count <= field::kMaximumLegalActions &&
            root.base_score.actions.size() == count &&
            root.base_scores.size() == count &&
            root.neutral_priority_options.size() == count &&
            root.hidden_neutral_priority_options.size() ==
                count &&
            root.residuals.size() == count &&
            root.combined_scores.size() == count &&
            root.dominance.robustly_pass_dominated.size() ==
                count &&
            root.dominance.strict_world_counts.size() ==
                count &&
            root.dominance.shape_valid &&
            root.manifest.pass_index < count &&
            root.dominance.pass_index ==
                root.manifest.pass_index &&
            root.base_score.stable_id ==
                root.manifest.stable_id &&
            root.base_score.decision_kind ==
                probes::DecisionKind::Priority &&
            root.base_score.score_mode ==
                oc1_action_scoring::ScoreMode::
                    ProductionPrioritySearch &&
            root.parent_class.valid &&
            root.hidden_replay_bit_identical &&
            root.hidden_feature_bits_identical &&
            root.base_score.selected_support ==
                root.base_exact_support &&
            nested_bit_identical(
                root.neutral_priority_options,
                root.hidden_neutral_priority_options);
        if (!shape) {
            result.infrastructure_failures.push_back(
                "FQ4-D1 cannot strip malformed scored root " +
                prefix);
            continue;
        }
        TreatmentRow row{
            .stable_id = root.manifest.stable_id,
            .physical_game_id =
                integrity::sha256_string(
                    "old-school-fq4-d1-treatment-physical-game-v1\n" +
                    field::physical_game_id(
                        root.manifest.locator)),
            .owner_deck = root.manifest.owner_deck,
            .canonical_descriptors =
                root.manifest.canonical_descriptors,
            .robustly_pass_dominated =
                root.dominance.robustly_pass_dominated,
            .base_scores = root.base_scores,
            .parent_residuals = root.residuals,
            .parent_combined_scores =
                root.combined_scores,
            .parent_class = root.parent_class,
        };
        row.options.reserve(count);
        row.base_samples.reserve(count);
        bool actions_exact = true;
        for (std::size_t index = 0;
             index < count; ++index) {
            const auto& action =
                root.base_score.actions[index];
            actions_exact =
                actions_exact &&
                action.descriptor ==
                    row.canonical_descriptors[index] &&
                same_double(
                    action.raw_score,
                    row.base_scores[index]) &&
                action.raw_samples.size() ==
                    field::kDominanceWorlds;
            row.options.push_back({
                .descriptor =
                    row.canonical_descriptors[index],
                .visible_tensor =
                    root.neutral_priority_options[index],
                .hidden_tensor =
                    root.hidden_neutral_priority_options[
                        index],
            });
            row.base_samples.push_back(
                action.raw_samples);
        }
        if (!actions_exact ||
            !stripped_row_shape_valid(row)) {
            result.infrastructure_failures.push_back(
                "FQ4-D1 scored-root action alignment "
                "drifted for " + prefix);
            continue;
        }
        result.rows.push_back(std::move(row));
    }
    if (result.rows.size() != roots.size()) {
        result.rows.clear();
    }
    return result;
}

TreatmentReport run_production(
    std::shared_ptr<const LearnedModel> frozen_c16,
    std::string_view parent_artifact_sha256) {
    const auto total_start = Clock::now();
    TreatmentReport early{
        .production_contracts_required = true,
    };
    if (!frozen_c16) {
        append_failure(
            early,
            "FQ4-D1 production requires the frozen C16 parent");
        return early;
    }
    std::string actual_parent_fingerprint;
    LearnedModelComponentFingerprints
        actual_parent_components;
    try {
        actual_parent_fingerprint =
            learned_model_fingerprint(frozen_c16);
        actual_parent_components =
            learned_model_component_fingerprints(
                frozen_c16);
    } catch (const std::exception& error) {
        append_failure(
            early,
            "FQ4-D1 parent identity failed: " +
                std::string(error.what()));
        return early;
    }
    if (parent_artifact_sha256 !=
            kRequiredParentArtifactSha256 ||
        actual_parent_fingerprint !=
            field::kRequiredParentFingerprint) {
        append_failure(
            early,
            "FQ4-D1 frozen parent artifact/model contract drifted");
        return early;
    }

    const auto parent_start = Clock::now();
    field::CensusReport census;
    try {
        census =
            field::run_parent_census(frozen_c16);
    } catch (const std::exception& error) {
        append_failure(
            early,
            "FQ4-D1 parent census threw: " +
                std::string(error.what()));
        early.timings.parent_reconstruction_seconds =
            std::chrono::duration<double>(
                Clock::now() - parent_start)
                .count();
        early.timings.total_seconds =
            std::chrono::duration<double>(
                Clock::now() - total_start)
                .count();
        return early;
    }
    early.timings.parent_reconstruction_seconds =
        std::chrono::duration<double>(
            Clock::now() - parent_start)
            .count();
    early.parent_reconstruction =
        parent_summary(
            census, parent_artifact_sha256,
            actual_parent_fingerprint,
            actual_parent_components);
    if (!early.parent_reconstruction.exact) {
        append_failure(
            early,
            "FQ4-D1 exact P0 parent reconstruction drifted");
        early.timings.total_seconds =
            std::chrono::duration<double>(
                Clock::now() - total_start)
                .count();
        return early;
    }

    testing::RowAdaptation adaptation =
        testing::strip_scored_roots(
            census.scored_roots);
    if (!adaptation.valid() ||
        adaptation.rows.size() !=
            kExpectedScoredRoots) {
        for (std::string& failure :
             adaptation.infrastructure_failures) {
            early.infrastructure_failures.push_back(
                std::move(failure));
        }
        if (early.infrastructure_failures.empty()) {
            append_failure(
                early,
                "FQ4-D1 stripped-row count drifted");
        }
        early.timings.total_seconds =
            std::chrono::duration<double>(
                Clock::now() - total_start)
                .count();
        return early;
    }

    const auto d0b_start = Clock::now();
    fit::D0bReport d0b_report;
    try {
        // This is the sole D0b construction in the production worker.
        d0b_report =
            fit::fit_d0b_production(frozen_c16);
    } catch (const std::exception& error) {
        append_failure(
            early,
            "FQ4-D1 D0b reconstruction threw: " +
                std::string(error.what()));
        early.timings.d0b_fit_seconds =
            std::chrono::duration<double>(
                Clock::now() - d0b_start)
                .count();
        early.timings.total_seconds =
            std::chrono::duration<double>(
                Clock::now() - total_start)
                .count();
        return early;
    }
    early.timings.d0b_fit_seconds =
        std::chrono::duration<double>(
            Clock::now() - d0b_start)
            .count();
    early.d0b =
        d0b_summary(d0b_report, frozen_c16);
    if (!early.d0b.exact ||
        !d0b_report.treatment.fit.candidate) {
        append_failure(
            early,
            "FQ4-D1 exact D0b qualification drifted");
        early.timings.total_seconds =
            std::chrono::duration<double>(
                Clock::now() - total_start)
                .count();
        return early;
    }

    const auto evaluation_start = Clock::now();
    TreatmentReport report =
        production_detail::evaluate_stripped_rows(
            adaptation.rows,
            frozen_c16,
            d0b_report.treatment.fit.candidate,
            early.parent_reconstruction,
            early.d0b);
    report.timings.parent_reconstruction_seconds =
        early.timings.parent_reconstruction_seconds;
    report.timings.d0b_fit_seconds =
        early.timings.d0b_fit_seconds;
    report.timings.tensor_evaluation_seconds =
        std::chrono::duration<double>(
            Clock::now() - evaluation_start)
            .count();
    report.timings.total_seconds =
        std::chrono::duration<double>(
            Clock::now() - total_start)
            .count();
    if (report.roots.size() != kExpectedScoredRoots ||
        report.parent_fingerprint !=
            actual_parent_fingerprint ||
        report.candidate_fingerprint !=
            fit::kD0bRequiredTreatmentFingerprint) {
        append_failure(
            report,
            "FQ4-D1 production treatment identity/count drifted");
        report.evidence_sha256.clear();
    }
    return report;
}

} // namespace old_school::fq4_d1_treatment
