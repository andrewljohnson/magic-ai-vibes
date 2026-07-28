#include "old_school/fq4_d1_treatment.hpp"

#include "old_school/artifact_integrity.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::fq4_d1_treatment {
namespace {

namespace field = fq4_d1_field_gate;
namespace fit = fq4_priority_fit;
namespace integrity = artifact_integrity;

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
    for (std::size_t index = 0; index < first.size(); ++index) {
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
    for (std::size_t index = 0; index < first.size(); ++index) {
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

std::optional<std::size_t> severity(
    field::ParentClass classification) {
    switch (classification) {
    case field::ParentClass::Safe:
        return 0;
    case field::ParentClass::Class3:
        return 1;
    case field::ParentClass::Class2:
        return 2;
    case field::ParentClass::Class1:
        return 3;
    case field::ParentClass::Invalid:
        return std::nullopt;
    }
    return std::nullopt;
}

bool class_result_bit_identical(
    const field::ParentClassResult& first,
    const field::ParentClassResult& second) {
    return
        first.classification == second.classification &&
        first.best_dominated_index ==
            second.best_dominated_index &&
        first.best_nondominated_index ==
            second.best_nondominated_index &&
        same_double(first.margin, second.margin) &&
        same_double(
            first.paired_standard_error,
            second.paired_standard_error) &&
        same_double(first.sigma, second.sigma) &&
        first.valid == second.valid;
}

void digest_u64(
    integrity::Sha256Accumulator& digest,
    std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >>
             (8U * static_cast<unsigned>(
                        bytes.size() - 1 - index))) &
            0xffU);
    }
    digest.update(bytes);
}

void digest_bool(
    integrity::Sha256Accumulator& digest, bool value) {
    digest_u64(digest, value ? 1U : 0U);
}

void digest_string(
    integrity::Sha256Accumulator& digest,
    std::string_view value) {
    digest_u64(digest, value.size());
    digest.update(value);
}

void digest_double(
    integrity::Sha256Accumulator& digest, double value) {
    digest_u64(
        digest, std::bit_cast<std::uint64_t>(value));
}

void digest_strings(
    integrity::Sha256Accumulator& digest,
    const std::vector<std::string>& values) {
    digest_u64(digest, values.size());
    for (const std::string& value : values) {
        digest_string(digest, value);
    }
}

void digest_doubles(
    integrity::Sha256Accumulator& digest,
    const std::vector<double>& values) {
    digest_u64(digest, values.size());
    for (const double value : values) {
        digest_double(digest, value);
    }
}

void digest_double_matrix(
    integrity::Sha256Accumulator& digest,
    const std::vector<std::vector<double>>& values) {
    digest_u64(digest, values.size());
    for (const std::vector<double>& value : values) {
        digest_doubles(digest, value);
    }
}

void digest_bools(
    integrity::Sha256Accumulator& digest,
    const std::vector<bool>& values) {
    digest_u64(digest, values.size());
    for (const bool value : values) {
        digest_bool(digest, value);
    }
}

void digest_components(
    integrity::Sha256Accumulator& digest,
    const LearnedModelComponentFingerprints& components) {
    digest_string(digest, components.critic);
    digest_string(digest, components.priority);
    digest_string(digest, components.attack);
    digest_string(digest, components.block);
    digest_string(digest, components.damage_order);
}

void digest_class_result(
    integrity::Sha256Accumulator& digest,
    const field::ParentClassResult& result) {
    digest_u64(
        digest,
        static_cast<std::uint64_t>(
            result.classification));
    digest_u64(digest, result.best_dominated_index);
    digest_u64(
        digest, result.best_nondominated_index);
    digest_double(digest, result.margin);
    digest_double(
        digest, result.paired_standard_error);
    digest_double(digest, result.sigma);
    digest_bool(digest, result.valid);
}

void digest_class_counts(
    integrity::Sha256Accumulator& digest,
    const ClassCounts& counts) {
    for (const std::size_t count : counts.classes) {
        digest_u64(digest, count);
    }
}

void digest_aggregate(
    integrity::Sha256Accumulator& digest,
    const AggregateResult& aggregate) {
    digest_class_counts(digest, aggregate.parent);
    digest_class_counts(digest, aggregate.candidate);
    for (const auto& row : aggregate.transitions) {
        for (const std::size_t count : row) {
            digest_u64(digest, count);
        }
    }
}

void digest_accounting(
    integrity::Sha256Accumulator& digest,
    const field::ProductionAccounting& accounting) {
    digest_u64(digest, accounting.score_calls);
    digest_u64(digest, accounting.scored_actions);
    digest_u64(digest, accounting.sampled_worlds);
    digest_u64(digest, accounting.rollout_evaluations);
    digest_u64(digest, accounting.terminal_evaluations);
    digest_u64(
        digest, accounting.bootstrapped_evaluations);
    digest_u64(digest, accounting.dominance_transitions);
}

void digest_parent_summary(
    integrity::Sha256Accumulator& digest,
    const ParentReconstructionSummary& parent) {
    digest_string(digest, parent.artifact_sha256);
    digest_string(digest, parent.model_fingerprint);
    digest_components(digest, parent.model_components);
    digest_string(digest, parent.schedule_sha256);
    digest_string(digest, parent.trajectory_sha256);
    digest_string(digest, parent.retained_corpus_sha256);
    digest_string(digest, parent.dominance_corpus_sha256);
    digest_string(digest, parent.scored_corpus_sha256);
    digest_string(digest, parent.audit_scores_sha256);
    for (const ClassCounts& deck : parent.decks) {
        digest_class_counts(digest, deck);
    }
    digest_class_counts(digest, parent.pooled);
    digest_accounting(
        digest, parent.primary_accounting);
    digest_accounting(
        digest, parent.hidden_control_accounting);
    digest_accounting(
        digest, parent.reverse_control_accounting);
    digest_accounting(digest, parent.accounting);
    digest_accounting(
        digest, parent.repeat_accounting);
    digest_u64(digest, parent.physical_games);
    digest_u64(digest, parent.owner_perspectives);
    digest_u64(digest, parent.retained_roots);
    digest_u64(digest, parent.scored_roots);
    digest_double(digest, parent.class2_sigma_mass);
    digest_u64(digest, parent.high_confidence_games);
    digest_u64(digest, parent.high_confidence_decks);
    digest_bool(digest, parent.census_passed);
    digest_bool(digest, parent.hidden_replay_exact);
    digest_bool(
        digest, parent.hidden_feature_bits_identical);
    digest_bool(
        digest, parent.reverse_score_bits_identical);
    digest_bool(
        digest, parent.recipe_and_accounting_exact);
    digest_bool(digest, parent.count_cross_sums_exact);
    digest_bool(
        digest,
        parent.repeated_construction_bit_identical);
    digest_bool(digest, parent.exact);
}

void digest_control(
    integrity::Sha256Accumulator& digest,
    const fit::ControlReport& control) {
    digest_string(digest, control.name);
    digest_string(digest, control.stable_id);
    digest_string(
        digest, control.information_action_fingerprint);
    digest_strings(digest, control.descriptors);
    digest_strings(digest, control.parent_exact_support);
    digest_strings(
        digest, control.candidate_exact_support);
    digest_bool(digest, control.passed);
}

void digest_d0b_summary(
    integrity::Sha256Accumulator& digest,
    const D0bQualificationSummary& d0b) {
    digest_string(digest, d0b.parent_fingerprint);
    digest_string(
        digest, d0b.anchor_candidate_fingerprint);
    digest_string(digest, d0b.candidate_fingerprint);
    digest_string(digest, d0b.training_input_sha256);
    digest_components(digest, d0b.parent_components);
    digest_components(digest, d0b.candidate_components);
    digest_doubles(digest, d0b.parent_margins);
    digest_doubles(digest, d0b.candidate_margins);
    digest_u64(digest, d0b.anchor_controls.size());
    for (const fit::ControlReport& control :
         d0b.anchor_controls) {
        digest_control(digest, control);
    }
    digest_u64(digest, d0b.treatment_controls.size());
    for (const fit::ControlReport& control :
         d0b.treatment_controls) {
        digest_control(digest, control);
    }
    digest_u64(digest, d0b.anchor_epochs);
    digest_u64(digest, d0b.treatment_epochs);
    digest_u64(digest, d0b.discovered_constraints);
    digest_u64(digest, d0b.candidate_margins_at_gate);
    digest_double(digest, d0b.anchor_pooled_kl);
    digest_double(digest, d0b.treatment_pooled_kl);
    digest_bool(digest, d0b.report_passed);
    digest_bool(digest, d0b.parent_contract_qualified);
    digest_bool(digest, d0b.anchor_contract_qualified);
    digest_bool(
        digest, d0b.optimizer_only_epochs_differ);
    digest_bool(
        digest, d0b.checkpoint_inputs_bit_identical);
    digest_bool(
        digest, d0b.target_kl_strictly_improved);
    digest_bool(digest, d0b.candidate_fingerprint_exact);
    digest_bool(
        digest, d0b.only_priority_component_changed);
    digest_bool(
        digest, d0b.repeated_fit_bit_identical);
    digest_bool(
        digest, d0b.hidden_repartition_bit_identical);
    digest_bool(digest, d0b.action_order_bit_identical);
    digest_bool(
        digest, d0b.immutable_base_and_accounting);
    digest_bool(digest, d0b.every_control_passed);
    digest_bool(digest, d0b.exact);
}

struct CanonicalOptions {
    std::vector<std::vector<double>> visible;
    std::vector<std::vector<double>> hidden;
};

struct ParentDigestAnchor {
    std::vector<double> logits;
    std::vector<std::string> exact_support;
};

CanonicalOptions canonical_options(
    const TreatmentRow& row) {
    if (row.options.size() !=
            row.canonical_descriptors.size() ||
        row.canonical_descriptors.empty() ||
        !std::is_sorted(
            row.canonical_descriptors.begin(),
            row.canonical_descriptors.end()) ||
        std::adjacent_find(
            row.canonical_descriptors.begin(),
            row.canonical_descriptors.end()) !=
            row.canonical_descriptors.end()) {
        throw std::invalid_argument(
            "D1 treatment option descriptor shape is invalid");
    }
    std::map<std::string, const LabeledOption*> by_descriptor;
    for (const LabeledOption& option : row.options) {
        if (option.descriptor.empty() ||
            !by_descriptor
                 .emplace(option.descriptor, &option)
                 .second) {
            throw std::invalid_argument(
                "D1 treatment option descriptors are duplicated");
        }
    }
    CanonicalOptions result;
    result.visible.reserve(row.options.size());
    result.hidden.reserve(row.options.size());
    for (const std::string& descriptor :
         row.canonical_descriptors) {
        const auto found = by_descriptor.find(descriptor);
        if (found == by_descriptor.end()) {
            throw std::invalid_argument(
                "D1 treatment option descriptor is missing");
        }
        result.visible.push_back(
            found->second->visible_tensor);
        result.hidden.push_back(
            found->second->hidden_tensor);
        by_descriptor.erase(found);
    }
    if (!by_descriptor.empty()) {
        throw std::invalid_argument(
            "D1 treatment option descriptor is unknown");
    }
    return result;
}

std::string input_digest(
    const ParentReconstructionSummary& parent,
    const std::vector<TreatmentRow>& rows,
    const std::vector<ParentDigestAnchor>& anchors) {
    if (rows.size() != anchors.size()) {
        throw std::invalid_argument(
            "D1 treatment parent-anchor count is invalid");
    }
    integrity::Sha256Accumulator digest;
    digest_string(digest, kTreatmentInputSchema);
    digest_parent_summary(digest, parent);
    digest_u64(digest, rows.size());
    for (std::size_t row_index = 0;
         row_index < rows.size(); ++row_index) {
        const TreatmentRow& row = rows[row_index];
        const CanonicalOptions options =
            canonical_options(row);
        digest_string(digest, row.stable_id);
        digest_string(digest, row.physical_game_id);
        digest_u64(
            digest,
            static_cast<std::uint64_t>(
                row.owner_deck));
        digest_strings(
            digest, row.canonical_descriptors);
        digest_u64(
            digest, row.canonical_descriptors.size());
        for (std::size_t index = 0;
             index < row.canonical_descriptors.size();
             ++index) {
            digest_string(
                digest,
                row.canonical_descriptors[index]);
            digest_doubles(digest, options.visible[index]);
            digest_doubles(digest, options.hidden[index]);
        }
        digest_bools(
            digest, row.robustly_pass_dominated);
        digest_doubles(digest, row.base_scores);
        digest_double_matrix(digest, row.base_samples);
        digest_doubles(digest, row.parent_residuals);
        digest_doubles(
            digest, row.parent_combined_scores);
        digest_class_result(digest, row.parent_class);
        digest_doubles(
            digest, anchors[row_index].logits);
        digest_strings(
            digest, anchors[row_index].exact_support);
    }
    return digest.finish();
}

std::vector<std::string> exact_support(
    const std::vector<std::string>& descriptors,
    const std::vector<double>& scores) {
    if (descriptors.empty() ||
        descriptors.size() != scores.size() ||
        !finite_vector(scores)) {
        throw std::invalid_argument(
            "D1 treatment support input is invalid");
    }
    double maximum = scores.front();
    for (std::size_t index = 1;
         index < scores.size(); ++index) {
        if (scores[index] > maximum) {
            maximum = scores[index];
        }
    }
    std::vector<std::string> result;
    for (std::size_t index = 0;
         index < scores.size(); ++index) {
        if (scores[index] == maximum) {
            result.push_back(descriptors[index]);
        }
    }
    return result;
}

struct HeadScore {
    std::vector<double> logits;
    std::vector<double> residuals;
};

HeadScore score_head(
    const std::vector<std::vector<double>>& options,
    const std::shared_ptr<const LearnedModel>& model) {
    HeadScore result{
        .logits =
            learned_policy_head_logits(
                options,
                LearnedPolicyDecisionKind::Priority,
                model),
    };
    if (result.logits.empty() ||
        !finite_vector(result.logits)) {
        throw std::invalid_argument(
            "D1 treatment policy logits are invalid");
    }
    double total = 0.0;
    for (const double logit : result.logits) {
        total += logit;
    }
    const double mean =
        total /
        static_cast<double>(result.logits.size());
    if (!std::isfinite(mean)) {
        throw std::invalid_argument(
            "D1 treatment policy-logit mean is invalid");
    }
    result.residuals.reserve(result.logits.size());
    for (const double logit : result.logits) {
        const double residual =
            field::kParentResidualWeight *
            std::tanh(logit - mean);
        if (!std::isfinite(residual)) {
            throw std::invalid_argument(
                "D1 treatment residual is invalid");
        }
        result.residuals.push_back(residual);
    }
    return result;
}

std::vector<double> combine(
    const std::vector<double>& base,
    const std::vector<double>& residuals) {
    if (base.size() != residuals.size()) {
        throw std::invalid_argument(
            "D1 treatment score vectors are misaligned");
    }
    std::vector<double> result;
    result.reserve(base.size());
    for (std::size_t index = 0;
         index < base.size(); ++index) {
        const double value = base[index] + residuals[index];
        if (!std::isfinite(value)) {
            throw std::invalid_argument(
                "D1 treatment combined score is invalid");
        }
        result.push_back(value);
    }
    return result;
}

field::ParentClassResult classify(
    const TreatmentRow& row,
    const std::vector<double>& combined) {
    const field::ParentClassResult result =
        field::classify_parent({
            .canonical_descriptors =
                row.canonical_descriptors,
            .base_scores = row.base_scores,
            .combined_scores = combined,
            .base_samples = row.base_samples,
            .robustly_pass_dominated =
                row.robustly_pass_dominated,
        });
    if (!result.valid) {
        throw std::invalid_argument(
            "D1 treatment classification is invalid");
    }
    return result;
}

std::string selected_descriptor(
    const TreatmentRow& row, std::size_t index) {
    if (index >= row.canonical_descriptors.size()) {
        throw std::invalid_argument(
            "D1 treatment selected index is invalid");
    }
    return row.canonical_descriptors[index];
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

} // namespace

namespace {

struct EvaluationContext {
    bool production_contracts_required = false;
    ParentReconstructionSummary parent;
    D0bQualificationSummary d0b;
};

bool valid_deck(DeckId deck);
bool root_shape_valid(const TreatmentRow& row);
bool sha256_shape(std::string_view value);
bool components_have_sha256_shape(
    const LearnedModelComponentFingerprints& components);
std::string evidence_digest(
    const TreatmentReport& report);
bool aggregates_valid(const TreatmentReport& report);
TreatmentReport evaluate_repeated(
    const std::vector<TreatmentRow>& rows,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    const EvaluationContext& context);
std::vector<ParentDigestAnchor> parent_digest_anchors(
    const std::vector<TreatmentRow>& rows,
    const std::shared_ptr<const LearnedModel>& parent);

} // namespace

ScientificGates testing::evaluate_scientific_gates(
    const GateInput& input) {
    ScientificGates result{
        .repair_root_floor =
            input.full_repairs >=
                kRequiredFullRepairs,
        .repair_game_floor =
            input.distinct_repair_games >=
                kRequiredRepairGames,
        .repair_deck_floor =
            input.distinct_repair_decks >=
                kRequiredRepairDecks,
        .zero_severity_regressions =
            input.severity_regressions == 0,
        .per_deck_nonregression = true,
        .pooled_high_confidence_bound =
            input.pooled.candidate.high_confidence() <=
                kMaximumCandidateHighConfidence,
        .pooled_unsafe_bound =
            input.pooled.candidate.unsafe() <=
                kMaximumCandidateUnsafe,
        .pooled_class1_bound =
            input.pooled.candidate.class1() <=
                kMaximumCandidateClass1,
        .class2_sigma_nonregression =
            std::isfinite(
                input.candidate_class2_sigma_mass) &&
            input.candidate_class2_sigma_mass >= 0.0 &&
            input.candidate_class2_sigma_mass <=
                kParentClass2SigmaMass,
        .strict_registered_improvement =
            input.pooled.candidate.class1() <
                kExpectedPooledParentClasses[1] ||
            (std::isfinite(
                 input.candidate_class2_sigma_mass) &&
             input.candidate_class2_sigma_mass >= 0.0 &&
             input.candidate_class2_sigma_mass <
                 kParentClass2SigmaMass),
    };
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const ClassCounts expected{
            .classes =
                kExpectedParentClassesByDeck[deck],
        };
        const ClassCounts& candidate =
            input.decks[deck].candidate;
        result.per_deck_nonregression =
            result.per_deck_nonregression &&
            candidate.unsafe() <= expected.unsafe() &&
            candidate.high_confidence() <=
                expected.high_confidence() &&
            candidate.class1() <= expected.class1();
    }
    const std::size_t red =
        static_cast<std::size_t>(DeckId::Red);
    result.red_protected =
        red < kDeckCount &&
        input.decks[red].candidate.classes ==
            kExpectedParentClassesByDeck[red];
    return result;
}

TreatmentReport testing::evaluate(
    const std::vector<TreatmentRow>& rows,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate) {
    EvaluationContext context;
    if (parent) {
        try {
            context.parent.model_fingerprint =
                learned_model_fingerprint(parent);
            context.parent.model_components =
                learned_model_component_fingerprints(
                    parent);
        } catch (const std::exception&) {
            // The shared evaluator records the actionable identity failure.
        }
    }
    return evaluate_repeated(
        rows, parent, candidate, context);
}

TreatmentReport production_detail::evaluate_stripped_rows(
    const std::vector<TreatmentRow>& rows,
    std::shared_ptr<const LearnedModel> parent,
    std::shared_ptr<const LearnedModel> candidate,
    const ParentReconstructionSummary& parent_reconstruction,
    const D0bQualificationSummary& d0b) {
    return evaluate_repeated(
        rows, parent, candidate,
        {
            .production_contracts_required = true,
            .parent = parent_reconstruction,
            .d0b = d0b,
        });
}

std::string testing::treatment_input_sha256(
    const ParentReconstructionSummary& parent,
    const std::vector<TreatmentRow>& rows,
    std::shared_ptr<const LearnedModel> parent_model) {
    return input_digest(
        parent, rows,
        parent_digest_anchors(rows, parent_model));
}

std::string testing::treatment_evidence_sha256(
    const TreatmentReport& report) {
    return evidence_digest(report);
}

bool TreatmentReport::infrastructure_valid() const {
    if (!infrastructure_failures.empty() ||
        !row_shape_valid ||
        !parent_reproduced ||
        !only_priority_component_changed ||
        !hidden_bit_identical ||
        !reverse_order_bit_identical ||
        !repeated_evaluation_bit_identical ||
        !zero_treatment_rollout_accounting ||
        !treatment_accounting.zero() ||
        roots.empty() ||
        roots.size() != pooled.parent.total() ||
        roots.size() != pooled.candidate.total() ||
        !std::isfinite(
            candidate_class2_sigma_mass) ||
        !sha256_shape(parent_fingerprint) ||
        !sha256_shape(candidate_fingerprint) ||
        parent_fingerprint == candidate_fingerprint ||
        !components_have_sha256_shape(
            parent_components) ||
        !components_have_sha256_shape(
            candidate_components) ||
        !priority_only_changed(
            parent_components,
            candidate_components) ||
        !sha256_shape(treatment_input_sha256) ||
        !sha256_shape(evidence_sha256) ||
        evidence_sha256 != evidence_digest(*this) ||
        !aggregates_valid(*this) ||
        scientific_failures.empty() ==
            !gates.passed()) {
        return false;
    }
    const auto finite_nonnegative =
        [](double seconds) {
            return std::isfinite(seconds) &&
                   seconds >= 0.0;
        };
    if (!finite_nonnegative(
            timings.parent_reconstruction_seconds) ||
        !finite_nonnegative(timings.d0b_fit_seconds) ||
        !finite_nonnegative(
            timings.tensor_evaluation_seconds) ||
        !finite_nonnegative(timings.total_seconds)) {
        return false;
    }
    if (!production_contracts_required) {
        return true;
    }
    if (!parent_reconstruction.exact ||
        !d0b.exact ||
        roots.size() != kExpectedScoredRoots ||
        parent_fingerprint !=
            parent_reconstruction.model_fingerprint ||
        parent_components !=
            parent_reconstruction.model_components ||
        parent_fingerprint != d0b.parent_fingerprint ||
        candidate_fingerprint !=
            d0b.candidate_fingerprint ||
        candidate_components !=
            d0b.candidate_components ||
        candidate_fingerprint !=
            fit::kD0bRequiredTreatmentFingerprint ||
        parent_reconstruction.pooled.classes !=
            kExpectedPooledParentClasses ||
        pooled.parent.classes !=
            kExpectedPooledParentClasses ||
        std::bit_cast<std::uint64_t>(
            parent_reconstruction
                .class2_sigma_mass) !=
            kParentClass2SigmaMassBits) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        if (parent_reconstruction
                .decks[deck].classes !=
                kExpectedParentClassesByDeck[deck] ||
            decks[deck].parent.classes !=
                kExpectedParentClassesByDeck[deck]) {
            return false;
        }
    }
    return true;
}

bool TreatmentReport::passed() const {
    return infrastructure_valid() && gates.passed();
}

ExitClassification classify_exit(
    const TreatmentReport& report) {
    if (!report.infrastructure_valid()) {
        return ExitClassification::InfrastructureFailure;
    }
    return report.passed()
               ? ExitClassification::Pass
               : ExitClassification::ScientificReject;
}

namespace {

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

bool aggregates_valid(const TreatmentReport& report) {
    std::array<AggregateResult, kDeckCount> decks{};
    AggregateResult pooled;
    std::set<std::string> stable_ids;
    std::set<std::string> repair_games;
    std::set<std::size_t> repair_decks;
    std::size_t repairs = 0;
    std::size_t regressions = 0;
    double sigma_mass = 0.0;
    for (const TreatmentRootResult& root :
         report.roots) {
        const std::optional<std::size_t> parent =
            class_index(root.parent_class.classification);
        const std::optional<std::size_t> candidate =
            class_index(root.candidate_class.classification);
        const std::optional<std::size_t> parent_level =
            severity(root.parent_class.classification);
        const std::optional<std::size_t> candidate_level =
            severity(root.candidate_class.classification);
        const std::size_t deck =
            static_cast<std::size_t>(root.owner_deck);
        if (!parent.has_value() ||
            !candidate.has_value() ||
            !parent_level.has_value() ||
            !candidate_level.has_value() ||
            deck >= kDeckCount ||
            !stable_ids.insert(root.stable_id).second ||
            root.parent_severity != *parent_level ||
            root.candidate_severity !=
                *candidate_level ||
            root.severity_regression !=
                (*candidate_level > *parent_level) ||
            !root.hidden_bit_identical ||
            !root.reverse_order_bit_identical) {
            return false;
        }
        const bool repair =
            (root.parent_class.classification ==
                 field::ParentClass::Class1 ||
             root.parent_class.classification ==
                 field::ParentClass::Class2) &&
            root.candidate_class.classification ==
                field::ParentClass::Safe;
        if (root.full_repair != repair) {
            return false;
        }
        ++decks[deck].parent.classes[*parent];
        ++decks[deck].candidate.classes[*candidate];
        ++decks[deck].transitions[*parent][*candidate];
        ++pooled.parent.classes[*parent];
        ++pooled.candidate.classes[*candidate];
        ++pooled.transitions[*parent][*candidate];
        if (repair) {
            ++repairs;
            repair_games.insert(root.physical_game_id);
            repair_decks.insert(deck);
        }
        if (root.severity_regression) {
            ++regressions;
        }
        if (root.candidate_class.classification ==
            field::ParentClass::Class2) {
            sigma_mass += root.candidate_class.sigma;
            if (!std::isfinite(sigma_mass)) {
                return false;
            }
        }
    }
    return decks == report.decks &&
           pooled == report.pooled &&
           repairs == report.full_repairs &&
           repair_games.size() ==
               report.distinct_repair_games &&
           repair_decks.size() ==
               report.distinct_repair_decks &&
           regressions == report.severity_regressions &&
           same_double(
               sigma_mass,
               report.candidate_class2_sigma_mass);
}

} // namespace

std::size_t ClassCounts::safe() const {
    return classes[0];
}

std::size_t ClassCounts::class1() const {
    return classes[1];
}

std::size_t ClassCounts::class2() const {
    return classes[2];
}

std::size_t ClassCounts::class3() const {
    return classes[3];
}

std::size_t ClassCounts::high_confidence() const {
    return class1() + class2();
}

std::size_t ClassCounts::unsafe() const {
    return class1() + class2() + class3();
}

std::size_t ClassCounts::total() const {
    return std::accumulate(
        classes.begin(), classes.end(),
        std::size_t{0});
}

bool ScientificGates::passed() const {
    return repair_root_floor &&
           repair_game_floor &&
           repair_deck_floor &&
           zero_severity_regressions &&
           per_deck_nonregression &&
           red_protected &&
           pooled_high_confidence_bound &&
           pooled_unsafe_bound &&
           pooled_class1_bound &&
           class2_sigma_nonregression &&
           strict_registered_improvement;
}

bool TreatmentRolloutAccounting::zero() const {
    return search_calls == 0 &&
           sampled_worlds == 0 &&
           rollout_evaluations == 0 &&
           terminal_leaves == 0 &&
           bootstrap_leaves == 0 &&
           dominance_transitions == 0;
}

bool testing::RowAdaptation::valid() const {
    return infrastructure_failures.empty();
}

namespace {

bool valid_deck(DeckId deck) {
    return static_cast<std::size_t>(deck) < kDeckCount;
}

bool root_shape_valid(const TreatmentRow& row) {
    const std::size_t count =
        row.canonical_descriptors.size();
    if (row.stable_id.empty() ||
        row.physical_game_id.empty() ||
        !valid_deck(row.owner_deck) ||
        count < 2 ||
        count > field::kMaximumLegalActions ||
        row.options.size() != count ||
        row.robustly_pass_dominated.size() != count ||
        row.base_scores.size() != count ||
        row.base_samples.size() != count ||
        row.parent_residuals.size() != count ||
        row.parent_combined_scores.size() != count ||
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
    try {
        const CanonicalOptions options =
            canonical_options(row);
        if (!finite_matrix(options.visible) ||
            !finite_matrix(options.hidden) ||
            !nested_bit_identical(
                options.visible, options.hidden)) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool head_bit_identical(
    const HeadScore& first, const HeadScore& second) {
    return bit_identical(first.logits, second.logits) &&
           bit_identical(
               first.residuals, second.residuals);
}

struct ParentEvaluation {
    CanonicalOptions options;
    HeadScore visible;
    std::vector<double> combined;
    field::ParentClassResult classification;
    std::vector<std::string> exact_support;
    bool hidden_bit_identical = false;
    bool reverse_order_bit_identical = false;
};

ParentEvaluation evaluate_parent_root(
    const TreatmentRow& row,
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!root_shape_valid(row)) {
        throw std::invalid_argument(
            "stripped treatment row has invalid shape");
    }
    const CanonicalOptions options =
        canonical_options(row);

    const std::vector<double> stored_combined =
        combine(row.base_scores, row.parent_residuals);
    if (!bit_identical(
            stored_combined,
            row.parent_combined_scores)) {
        throw std::invalid_argument(
            "stored parent combined scores are inconsistent");
    }
    const field::ParentClassResult stored_class =
        classify(row, row.parent_combined_scores);
    if (!class_result_bit_identical(
            stored_class, row.parent_class)) {
        throw std::invalid_argument(
            "stored parent class is inconsistent");
    }

    const HeadScore parent_visible =
        score_head(options.visible, parent);
    const HeadScore parent_hidden =
        score_head(options.hidden, parent);
    const std::vector<double> parent_combined =
        combine(row.base_scores, parent_visible.residuals);
    const field::ParentClassResult parent_class =
        classify(row, parent_combined);
    const bool parent_reproduced =
        head_bit_identical(
            parent_visible, parent_hidden) &&
        bit_identical(
            parent_visible.residuals,
            row.parent_residuals) &&
        bit_identical(
            parent_combined,
            row.parent_combined_scores) &&
        class_result_bit_identical(
            parent_class, row.parent_class);
    if (!parent_reproduced) {
        throw std::invalid_argument(
            "parent tensor reproduction drifted");
    }

    TreatmentRow reversed = row;
    std::reverse(
        reversed.options.begin(),
        reversed.options.end());
    const CanonicalOptions reversed_options =
        canonical_options(reversed);
    const HeadScore reversed_parent_visible =
        score_head(reversed_options.visible, parent);
    const HeadScore reversed_parent_hidden =
        score_head(reversed_options.hidden, parent);
    const std::vector<double> reversed_parent_combined =
        combine(
            row.base_scores,
            reversed_parent_visible.residuals);
    const field::ParentClassResult reversed_parent_class =
        classify(row, reversed_parent_combined);
    const bool reverse_exact =
        nested_bit_identical(
            options.visible,
            reversed_options.visible) &&
        nested_bit_identical(
            options.hidden,
            reversed_options.hidden) &&
        head_bit_identical(
            parent_visible,
            reversed_parent_visible) &&
        head_bit_identical(
            parent_hidden,
            reversed_parent_hidden) &&
        bit_identical(
            parent_combined,
            reversed_parent_combined) &&
        class_result_bit_identical(
            parent_class,
            reversed_parent_class);
    if (!reverse_exact) {
        throw std::invalid_argument(
            "reversed parent labeled-option control drifted");
    }

    return {
        .options = options,
        .visible = parent_visible,
        .combined = parent_combined,
        .classification = parent_class,
        .exact_support =
            exact_support(
                row.canonical_descriptors,
                parent_combined),
        .hidden_bit_identical = true,
        .reverse_order_bit_identical = true,
    };
}

std::vector<ParentDigestAnchor> parent_digest_anchors(
    const std::vector<TreatmentRow>& rows,
    const std::shared_ptr<const LearnedModel>& parent) {
    if (!parent) {
        throw std::invalid_argument(
            "D1 treatment parent model is missing");
    }
    if (rows.empty()) {
        throw std::invalid_argument(
            "D1 treatment requires at least one row");
    }
    std::set<std::string> stable_ids;
    std::vector<ParentDigestAnchor> anchors;
    anchors.reserve(rows.size());
    for (const TreatmentRow& row : rows) {
        if (!stable_ids.insert(row.stable_id).second) {
            throw std::invalid_argument(
                "D1 treatment stable IDs are duplicated");
        }
        const ParentEvaluation evaluation =
            evaluate_parent_root(row, parent);
        anchors.push_back({
            .logits = evaluation.visible.logits,
            .exact_support = evaluation.exact_support,
        });
    }
    return anchors;
}

TreatmentRootResult evaluate_candidate_root(
    const TreatmentRow& row,
    const ParentEvaluation& parent_evaluation,
    const std::shared_ptr<const LearnedModel>& candidate) {
    const CanonicalOptions& options =
        parent_evaluation.options;
    const HeadScore candidate_visible =
        score_head(options.visible, candidate);
    const HeadScore candidate_hidden =
        score_head(options.hidden, candidate);
    const std::vector<double> candidate_combined =
        combine(
            row.base_scores,
            candidate_visible.residuals);
    const field::ParentClassResult candidate_class =
        classify(row, candidate_combined);
    const bool hidden_exact =
        parent_evaluation.hidden_bit_identical &&
        head_bit_identical(
            candidate_visible, candidate_hidden);
    if (!hidden_exact) {
        throw std::invalid_argument(
            "hidden tensor scoring drifted");
    }

    TreatmentRow reversed = row;
    std::reverse(
        reversed.options.begin(),
        reversed.options.end());
    const CanonicalOptions reversed_options =
        canonical_options(reversed);
    const HeadScore reversed_candidate_visible =
        score_head(reversed_options.visible, candidate);
    const HeadScore reversed_candidate_hidden =
        score_head(reversed_options.hidden, candidate);
    const std::vector<double> reversed_candidate_combined =
        combine(
            row.base_scores,
            reversed_candidate_visible.residuals);
    const field::ParentClassResult reversed_candidate_class =
        classify(row, reversed_candidate_combined);
    const bool reverse_exact =
        parent_evaluation.reverse_order_bit_identical &&
        nested_bit_identical(
            options.visible,
            reversed_options.visible) &&
        nested_bit_identical(
            options.hidden,
            reversed_options.hidden) &&
        head_bit_identical(
            candidate_visible,
            reversed_candidate_visible) &&
        head_bit_identical(
            candidate_hidden,
            reversed_candidate_hidden) &&
        bit_identical(
            candidate_combined,
            reversed_candidate_combined) &&
        class_result_bit_identical(
            candidate_class,
            reversed_candidate_class);
    if (!reverse_exact) {
        throw std::invalid_argument(
            "reversed labeled-option control drifted");
    }

    const std::optional<std::size_t> parent_severity =
        severity(
            parent_evaluation
                .classification.classification);
    const std::optional<std::size_t> candidate_severity =
        severity(candidate_class.classification);
    if (!parent_severity.has_value() ||
        !candidate_severity.has_value()) {
        throw std::invalid_argument(
            "treatment severity is invalid");
    }
    const bool repair =
        (parent_evaluation.classification.classification ==
             field::ParentClass::Class1 ||
         parent_evaluation.classification.classification ==
             field::ParentClass::Class2) &&
        candidate_class.classification ==
            field::ParentClass::Safe;
    return {
        .stable_id = row.stable_id,
        .physical_game_id = row.physical_game_id,
        .owner_deck = row.owner_deck,
        .parent_class =
            parent_evaluation.classification,
        .candidate_class = candidate_class,
        .parent_dominated_descriptor =
            selected_descriptor(
                row,
                parent_evaluation.classification
                    .best_dominated_index),
        .parent_nondominated_descriptor =
            selected_descriptor(
                row,
                parent_evaluation.classification
                    .best_nondominated_index),
        .candidate_dominated_descriptor =
            selected_descriptor(
                row,
                candidate_class.best_dominated_index),
        .candidate_nondominated_descriptor =
            selected_descriptor(
                row,
                candidate_class.best_nondominated_index),
        .parent_logits =
            parent_evaluation.visible.logits,
        .parent_residuals =
            parent_evaluation.visible.residuals,
        .parent_combined_scores =
            parent_evaluation.combined,
        .parent_exact_support =
            parent_evaluation.exact_support,
        .candidate_logits = candidate_visible.logits,
        .candidate_residuals =
            candidate_visible.residuals,
        .candidate_combined_scores =
            candidate_combined,
        .candidate_exact_support =
            exact_support(
                row.canonical_descriptors,
                candidate_combined),
        .parent_severity = *parent_severity,
        .candidate_severity = *candidate_severity,
        .full_repair = repair,
        .severity_regression =
            *candidate_severity > *parent_severity,
        .hidden_bit_identical = hidden_exact,
        .reverse_order_bit_identical = reverse_exact,
    };
}

void add_root_to_aggregates(
    TreatmentReport& report,
    const TreatmentRootResult& root,
    std::set<std::string>& repair_games,
    std::set<std::size_t>& repair_decks) {
    const std::optional<std::size_t> parent_index =
        class_index(root.parent_class.classification);
    const std::optional<std::size_t> candidate_index =
        class_index(root.candidate_class.classification);
    const std::size_t deck =
        static_cast<std::size_t>(root.owner_deck);
    if (!parent_index.has_value() ||
        !candidate_index.has_value() ||
        deck >= kDeckCount) {
        throw std::invalid_argument(
            "treatment aggregate class/deck is invalid");
    }
    ++report.decks[deck]
          .parent.classes[*parent_index];
    ++report.decks[deck]
          .candidate.classes[*candidate_index];
    ++report.decks[deck]
          .transitions[*parent_index][*candidate_index];
    ++report.pooled.parent.classes[*parent_index];
    ++report.pooled.candidate.classes[*candidate_index];
    ++report.pooled
          .transitions[*parent_index][*candidate_index];
    if (root.full_repair) {
        ++report.full_repairs;
        repair_games.insert(root.physical_game_id);
        repair_decks.insert(deck);
    }
    if (root.severity_regression) {
        ++report.severity_regressions;
    }
    if (root.candidate_class.classification ==
        field::ParentClass::Class2) {
        report.candidate_class2_sigma_mass +=
            root.candidate_class.sigma;
        if (!std::isfinite(
                report.candidate_class2_sigma_mass)) {
            throw std::invalid_argument(
                "candidate Class-2 sigma mass is invalid");
        }
    }
}

void add_gate_failures(TreatmentReport& report) {
    const auto add =
        [&](bool passed, std::string message) {
            if (!passed) {
                report.scientific_failures.push_back(
                    std::move(message));
            }
        };
    add(
        report.gates.repair_root_floor,
        "FQ4-D1 repaired fewer than five roots");
    add(
        report.gates.repair_game_floor,
        "FQ4-D1 repairs cover fewer than five games");
    add(
        report.gates.repair_deck_floor,
        "FQ4-D1 repairs cover fewer than two decks");
    add(
        report.gates.zero_severity_regressions,
        "FQ4-D1 has a root-level severity regression");
    add(
        report.gates.per_deck_nonregression,
        "FQ4-D1 has a per-deck aggregate regression");
    add(
        report.gates.red_protected,
        "FQ4-D1 Red protection gate failed");
    add(
        report.gates.pooled_high_confidence_bound,
        "FQ4-D1 pooled high-confidence count exceeds 22");
    add(
        report.gates.pooled_unsafe_bound,
        "FQ4-D1 pooled unsafe count exceeds 28");
    add(
        report.gates.pooled_class1_bound,
        "FQ4-D1 pooled Class-1 count exceeds 10");
    add(
        report.gates.class2_sigma_nonregression,
        "FQ4-D1 Class-2 sigma mass regressed");
    add(
        report.gates.strict_registered_improvement,
        "FQ4-D1 has no strict registered improvement");
}

void digest_root_result(
    integrity::Sha256Accumulator& digest,
    const TreatmentRootResult& root) {
    digest_string(digest, root.stable_id);
    digest_string(digest, root.physical_game_id);
    digest_u64(
        digest,
        static_cast<std::uint64_t>(root.owner_deck));
    digest_class_result(digest, root.parent_class);
    digest_class_result(digest, root.candidate_class);
    digest_string(
        digest, root.parent_dominated_descriptor);
    digest_string(
        digest, root.parent_nondominated_descriptor);
    digest_string(
        digest, root.candidate_dominated_descriptor);
    digest_string(
        digest, root.candidate_nondominated_descriptor);
    digest_doubles(digest, root.parent_logits);
    digest_doubles(digest, root.parent_residuals);
    digest_doubles(
        digest, root.parent_combined_scores);
    digest_strings(digest, root.parent_exact_support);
    digest_doubles(digest, root.candidate_logits);
    digest_doubles(digest, root.candidate_residuals);
    digest_doubles(
        digest, root.candidate_combined_scores);
    digest_strings(
        digest, root.candidate_exact_support);
    digest_u64(digest, root.parent_severity);
    digest_u64(digest, root.candidate_severity);
    digest_bool(digest, root.full_repair);
    digest_bool(digest, root.severity_regression);
    digest_bool(digest, root.hidden_bit_identical);
    digest_bool(
        digest, root.reverse_order_bit_identical);
}

void digest_gates(
    integrity::Sha256Accumulator& digest,
    const ScientificGates& gates) {
    digest_bool(digest, gates.repair_root_floor);
    digest_bool(digest, gates.repair_game_floor);
    digest_bool(digest, gates.repair_deck_floor);
    digest_bool(
        digest, gates.zero_severity_regressions);
    digest_bool(digest, gates.per_deck_nonregression);
    digest_bool(digest, gates.red_protected);
    digest_bool(
        digest, gates.pooled_high_confidence_bound);
    digest_bool(digest, gates.pooled_unsafe_bound);
    digest_bool(digest, gates.pooled_class1_bound);
    digest_bool(
        digest, gates.class2_sigma_nonregression);
    digest_bool(
        digest, gates.strict_registered_improvement);
}

void digest_treatment_accounting(
    integrity::Sha256Accumulator& digest,
    const TreatmentRolloutAccounting& accounting) {
    digest_u64(digest, accounting.search_calls);
    digest_u64(digest, accounting.sampled_worlds);
    digest_u64(digest, accounting.rollout_evaluations);
    digest_u64(digest, accounting.terminal_leaves);
    digest_u64(digest, accounting.bootstrap_leaves);
    digest_u64(
        digest, accounting.dominance_transitions);
}

std::string evidence_digest(
    const TreatmentReport& report) {
    integrity::Sha256Accumulator digest;
    digest_string(digest, kTreatmentEvidenceSchema);
    digest_bool(
        digest, report.production_contracts_required);
    digest_parent_summary(
        digest, report.parent_reconstruction);
    digest_d0b_summary(digest, report.d0b);
    digest_string(digest, report.parent_fingerprint);
    digest_string(digest, report.candidate_fingerprint);
    digest_components(digest, report.parent_components);
    digest_components(
        digest, report.candidate_components);
    digest_string(
        digest, report.treatment_input_sha256);
    digest_u64(digest, report.roots.size());
    for (const TreatmentRootResult& root :
         report.roots) {
        digest_root_result(digest, root);
    }
    for (const AggregateResult& deck : report.decks) {
        digest_aggregate(digest, deck);
    }
    digest_aggregate(digest, report.pooled);
    digest_u64(digest, report.full_repairs);
    digest_u64(
        digest, report.distinct_repair_games);
    digest_u64(
        digest, report.distinct_repair_decks);
    digest_u64(digest, report.severity_regressions);
    digest_double(
        digest,
        report.candidate_class2_sigma_mass);
    digest_gates(digest, report.gates);
    digest_treatment_accounting(
        digest, report.treatment_accounting);
    digest_bool(digest, report.row_shape_valid);
    digest_bool(digest, report.parent_reproduced);
    digest_bool(
        digest,
        report.only_priority_component_changed);
    digest_bool(digest, report.hidden_bit_identical);
    digest_bool(
        digest, report.reverse_order_bit_identical);
    digest_bool(
        digest,
        report.repeated_evaluation_bit_identical);
    digest_bool(
        digest,
        report.zero_treatment_rollout_accounting);
    return digest.finish();
}

bool root_result_bit_identical(
    const TreatmentRootResult& first,
    const TreatmentRootResult& second) {
    return first.stable_id == second.stable_id &&
           first.physical_game_id ==
               second.physical_game_id &&
           first.owner_deck == second.owner_deck &&
           class_result_bit_identical(
               first.parent_class,
               second.parent_class) &&
           class_result_bit_identical(
               first.candidate_class,
               second.candidate_class) &&
           first.parent_dominated_descriptor ==
               second.parent_dominated_descriptor &&
           first.parent_nondominated_descriptor ==
               second.parent_nondominated_descriptor &&
           first.candidate_dominated_descriptor ==
               second.candidate_dominated_descriptor &&
           first.candidate_nondominated_descriptor ==
               second.candidate_nondominated_descriptor &&
           bit_identical(
               first.parent_logits,
               second.parent_logits) &&
           bit_identical(
               first.parent_residuals,
               second.parent_residuals) &&
           bit_identical(
               first.parent_combined_scores,
               second.parent_combined_scores) &&
           first.parent_exact_support ==
               second.parent_exact_support &&
           bit_identical(
               first.candidate_logits,
               second.candidate_logits) &&
           bit_identical(
               first.candidate_residuals,
               second.candidate_residuals) &&
           bit_identical(
               first.candidate_combined_scores,
               second.candidate_combined_scores) &&
           first.candidate_exact_support ==
               second.candidate_exact_support &&
           first.parent_severity ==
               second.parent_severity &&
           first.candidate_severity ==
               second.candidate_severity &&
           first.full_repair == second.full_repair &&
           first.severity_regression ==
               second.severity_regression &&
           first.hidden_bit_identical ==
               second.hidden_bit_identical &&
           first.reverse_order_bit_identical ==
               second.reverse_order_bit_identical;
}

bool parent_summary_bit_identical(
    const ParentReconstructionSummary& first,
    const ParentReconstructionSummary& second) {
    return
        first.artifact_sha256 == second.artifact_sha256 &&
        first.model_fingerprint ==
            second.model_fingerprint &&
        first.model_components ==
            second.model_components &&
        first.schedule_sha256 == second.schedule_sha256 &&
        first.trajectory_sha256 ==
            second.trajectory_sha256 &&
        first.retained_corpus_sha256 ==
            second.retained_corpus_sha256 &&
        first.dominance_corpus_sha256 ==
            second.dominance_corpus_sha256 &&
        first.scored_corpus_sha256 ==
            second.scored_corpus_sha256 &&
        first.audit_scores_sha256 ==
            second.audit_scores_sha256 &&
        first.decks == second.decks &&
        first.pooled == second.pooled &&
        first.primary_accounting ==
            second.primary_accounting &&
        first.hidden_control_accounting ==
            second.hidden_control_accounting &&
        first.reverse_control_accounting ==
            second.reverse_control_accounting &&
        first.accounting == second.accounting &&
        first.repeat_accounting ==
            second.repeat_accounting &&
        first.physical_games == second.physical_games &&
        first.owner_perspectives ==
            second.owner_perspectives &&
        first.retained_roots == second.retained_roots &&
        first.scored_roots == second.scored_roots &&
        same_double(
            first.class2_sigma_mass,
            second.class2_sigma_mass) &&
        first.high_confidence_games ==
            second.high_confidence_games &&
        first.high_confidence_decks ==
            second.high_confidence_decks &&
        first.census_passed == second.census_passed &&
        first.hidden_replay_exact ==
            second.hidden_replay_exact &&
        first.hidden_feature_bits_identical ==
            second.hidden_feature_bits_identical &&
        first.reverse_score_bits_identical ==
            second.reverse_score_bits_identical &&
        first.recipe_and_accounting_exact ==
            second.recipe_and_accounting_exact &&
        first.count_cross_sums_exact ==
            second.count_cross_sums_exact &&
        first.repeated_construction_bit_identical ==
            second.repeated_construction_bit_identical &&
        first.exact == second.exact;
}

bool d0b_summary_bit_identical(
    const D0bQualificationSummary& first,
    const D0bQualificationSummary& second) {
    return
        first.parent_fingerprint ==
            second.parent_fingerprint &&
        first.anchor_candidate_fingerprint ==
            second.anchor_candidate_fingerprint &&
        first.candidate_fingerprint ==
            second.candidate_fingerprint &&
        first.training_input_sha256 ==
            second.training_input_sha256 &&
        first.parent_components ==
            second.parent_components &&
        first.candidate_components ==
            second.candidate_components &&
        bit_identical(
            first.parent_margins,
            second.parent_margins) &&
        bit_identical(
            first.candidate_margins,
            second.candidate_margins) &&
        first.anchor_controls ==
            second.anchor_controls &&
        first.treatment_controls ==
            second.treatment_controls &&
        first.anchor_epochs == second.anchor_epochs &&
        first.treatment_epochs ==
            second.treatment_epochs &&
        first.discovered_constraints ==
            second.discovered_constraints &&
        first.candidate_margins_at_gate ==
            second.candidate_margins_at_gate &&
        same_double(
            first.anchor_pooled_kl,
            second.anchor_pooled_kl) &&
        same_double(
            first.treatment_pooled_kl,
            second.treatment_pooled_kl) &&
        first.report_passed == second.report_passed &&
        first.parent_contract_qualified ==
            second.parent_contract_qualified &&
        first.anchor_contract_qualified ==
            second.anchor_contract_qualified &&
        first.optimizer_only_epochs_differ ==
            second.optimizer_only_epochs_differ &&
        first.checkpoint_inputs_bit_identical ==
            second.checkpoint_inputs_bit_identical &&
        first.target_kl_strictly_improved ==
            second.target_kl_strictly_improved &&
        first.candidate_fingerprint_exact ==
            second.candidate_fingerprint_exact &&
        first.only_priority_component_changed ==
            second.only_priority_component_changed &&
        first.repeated_fit_bit_identical ==
            second.repeated_fit_bit_identical &&
        first.hidden_repartition_bit_identical ==
            second.hidden_repartition_bit_identical &&
        first.action_order_bit_identical ==
            second.action_order_bit_identical &&
        first.immutable_base_and_accounting ==
            second.immutable_base_and_accounting &&
        first.every_control_passed ==
            second.every_control_passed &&
        first.exact == second.exact;
}

bool evaluation_bit_identical(
    const TreatmentReport& first,
    const TreatmentReport& second) {
    if (first.production_contracts_required !=
            second.production_contracts_required ||
        !parent_summary_bit_identical(
            first.parent_reconstruction,
            second.parent_reconstruction) ||
        !d0b_summary_bit_identical(
            first.d0b, second.d0b) ||
        first.parent_fingerprint !=
            second.parent_fingerprint ||
        first.candidate_fingerprint !=
            second.candidate_fingerprint ||
        first.parent_components !=
            second.parent_components ||
        first.candidate_components !=
            second.candidate_components ||
        first.treatment_input_sha256 !=
            second.treatment_input_sha256 ||
        first.roots.size() != second.roots.size() ||
        first.decks != second.decks ||
        first.pooled != second.pooled ||
        first.full_repairs != second.full_repairs ||
        first.distinct_repair_games !=
            second.distinct_repair_games ||
        first.distinct_repair_decks !=
            second.distinct_repair_decks ||
        first.severity_regressions !=
            second.severity_regressions ||
        !same_double(
            first.candidate_class2_sigma_mass,
            second.candidate_class2_sigma_mass) ||
        first.gates != second.gates ||
        first.treatment_accounting !=
            second.treatment_accounting ||
        first.row_shape_valid !=
            second.row_shape_valid ||
        first.parent_reproduced !=
            second.parent_reproduced ||
        first.only_priority_component_changed !=
            second.only_priority_component_changed ||
        first.hidden_bit_identical !=
            second.hidden_bit_identical ||
        first.reverse_order_bit_identical !=
            second.reverse_order_bit_identical ||
        first.zero_treatment_rollout_accounting !=
            second.zero_treatment_rollout_accounting ||
        first.infrastructure_failures !=
            second.infrastructure_failures ||
        first.scientific_failures !=
            second.scientific_failures) {
        return false;
    }
    for (std::size_t index = 0;
         index < first.roots.size(); ++index) {
        if (!root_result_bit_identical(
                first.roots[index],
                second.roots[index])) {
            return false;
        }
    }
    return true;
}

TreatmentReport evaluate_once(
    const std::vector<TreatmentRow>& rows,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    const EvaluationContext& context) {
    TreatmentReport report{
        .production_contracts_required =
            context.production_contracts_required,
        .parent_reconstruction = context.parent,
        .d0b = context.d0b,
        .candidate_class2_sigma_mass = 0.0,
        .row_shape_valid = true,
        .parent_reproduced = true,
        .hidden_bit_identical = true,
        .reverse_order_bit_identical = true,
    };
    if (!parent || !candidate) {
        append_failure(
            report,
            "FQ4-D1 treatment requires parent and candidate models");
        report.row_shape_valid = false;
        report.parent_reproduced = false;
        report.hidden_bit_identical = false;
        report.reverse_order_bit_identical = false;
        return report;
    }
    try {
        report.parent_fingerprint =
            learned_model_fingerprint(parent);
        report.candidate_fingerprint =
            learned_model_fingerprint(candidate);
        report.parent_components =
            learned_model_component_fingerprints(parent);
        report.candidate_components =
            learned_model_component_fingerprints(candidate);
    } catch (const std::exception& error) {
        append_failure(
            report,
            "FQ4-D1 model identity failed: " +
                std::string(error.what()));
        report.row_shape_valid = false;
        return report;
    }
    report.only_priority_component_changed =
        priority_only_changed(
            report.parent_components,
            report.candidate_components);
    if (!report.only_priority_component_changed) {
        append_failure(
            report,
            "FQ4-D1 candidate changed a non-Priority component "
            "or left Priority unchanged");
    }
    if (rows.empty()) {
        append_failure(
            report,
            "FQ4-D1 treatment requires at least one row");
        report.row_shape_valid = false;
        return report;
    }

    std::set<std::string> stable_ids;
    std::vector<ParentEvaluation> parent_evaluations;
    std::vector<ParentDigestAnchor> parent_anchors;
    parent_evaluations.reserve(rows.size());
    parent_anchors.reserve(rows.size());
    for (const TreatmentRow& row : rows) {
        if (!stable_ids.insert(row.stable_id).second) {
            append_failure(
                report,
                "FQ4-D1 duplicate stable ID: " +
                    row.stable_id);
            report.row_shape_valid = false;
        }
        try {
            ParentEvaluation evaluation =
                evaluate_parent_root(row, parent);
            report.hidden_bit_identical =
                report.hidden_bit_identical &&
                evaluation.hidden_bit_identical;
            report.reverse_order_bit_identical =
                report.reverse_order_bit_identical &&
                evaluation.reverse_order_bit_identical;
            parent_anchors.push_back({
                .logits = evaluation.visible.logits,
                .exact_support =
                    evaluation.exact_support,
            });
            parent_evaluations.push_back(
                std::move(evaluation));
        } catch (const std::exception& error) {
            append_failure(
                report,
                "FQ4-D1 parent row " + row.stable_id +
                    " failed: " + error.what());
            report.row_shape_valid = false;
            report.parent_reproduced = false;
            report.hidden_bit_identical = false;
            report.reverse_order_bit_identical = false;
        }
    }
    if (!report.row_shape_valid ||
        !report.parent_reproduced ||
        !report.hidden_bit_identical ||
        !report.reverse_order_bit_identical ||
        parent_evaluations.size() != rows.size() ||
        parent_anchors.size() != rows.size()) {
        return report;
    }
    try {
        report.treatment_input_sha256 =
            input_digest(
                context.parent, rows, parent_anchors);
    } catch (const std::exception& error) {
        append_failure(
            report,
            "FQ4-D1 input digest failed: " +
                std::string(error.what()));
        report.row_shape_valid = false;
        return report;
    }

    std::set<std::string> repair_games;
    std::set<std::size_t> repair_decks;
    report.roots.reserve(rows.size());
    for (std::size_t index = 0;
         index < rows.size(); ++index) {
        const TreatmentRow& row = rows[index];
        try {
            TreatmentRootResult result =
                evaluate_candidate_root(
                    row, parent_evaluations[index],
                    candidate);
            report.hidden_bit_identical =
                report.hidden_bit_identical &&
                result.hidden_bit_identical;
            report.reverse_order_bit_identical =
                report.reverse_order_bit_identical &&
                result.reverse_order_bit_identical;
            add_root_to_aggregates(
                report, result,
                repair_games, repair_decks);
            report.roots.push_back(std::move(result));
        } catch (const std::exception& error) {
            append_failure(
                report,
                "FQ4-D1 candidate row " + row.stable_id +
                    " failed: " + error.what());
            report.row_shape_valid = false;
            report.hidden_bit_identical = false;
            report.reverse_order_bit_identical = false;
        }
    }
    report.distinct_repair_games =
        repair_games.size();
    report.distinct_repair_decks =
        repair_decks.size();
    report.zero_treatment_rollout_accounting =
        report.treatment_accounting.zero();
    report.gates =
        testing::evaluate_scientific_gates({
            .decks = report.decks,
            .pooled = report.pooled,
            .full_repairs = report.full_repairs,
            .distinct_repair_games =
                report.distinct_repair_games,
            .distinct_repair_decks =
                report.distinct_repair_decks,
            .severity_regressions =
                report.severity_regressions,
            .candidate_class2_sigma_mass =
                report.candidate_class2_sigma_mass,
        });
    return report;
}

TreatmentReport evaluate_repeated(
    const std::vector<TreatmentRow>& rows,
    const std::shared_ptr<const LearnedModel>& parent,
    const std::shared_ptr<const LearnedModel>& candidate,
    const EvaluationContext& context) {
    TreatmentReport first =
        evaluate_once(rows, parent, candidate, context);
    const TreatmentReport second =
        evaluate_once(rows, parent, candidate, context);
    first.repeated_evaluation_bit_identical =
        evaluation_bit_identical(first, second);
    if (!first.repeated_evaluation_bit_identical) {
        append_failure(
            first,
            "FQ4-D1 repeated tensor evaluation drifted");
    }
    if (first.infrastructure_failures.empty()) {
        add_gate_failures(first);
        first.evidence_sha256 = evidence_digest(first);
    }
    return first;
}

} // namespace

} // namespace old_school::fq4_d1_treatment
