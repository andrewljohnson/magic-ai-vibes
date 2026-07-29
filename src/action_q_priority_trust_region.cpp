#include "old_school/action_q_priority_trust_region.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace old_school::action_q_priority_trust_region {
namespace {

using Parameters = LearnedPriorityHeadParameters;

struct RecordedMetrics {
    std::size_t roots = 0;
    std::size_t options = 0;
    std::array<std::size_t, kDeckCount> deck_roots{};
    std::array<std::size_t, kDeckCount> deck_options{};
    std::array<double, kDeckCount> agreement{};
    std::array<double, kDeckCount> regret{};
    double equal_deck_agreement = 0.0;
    double equal_deck_regret = 0.0;
};

constexpr RecordedMetrics kRecordedParentTrain{
    .roots = 480,
    .options = 1568,
    .deck_roots = {96, 96, 96, 96, 96},
    .deck_options = {241, 323, 223, 307, 474},
    .agreement = {
        0.67708333333333404,
        0.67708333333333404,
        0.73958333333333393,
        0.63541666666666741,
        0.70833333333333393,
    },
    .regret = {
        0.013735929481439845,
        0.014779384478444008,
        0.017393751585290673,
        0.035796110522776786,
        0.021264723701941497,
    },
    .equal_deck_agreement = 0.68750000000000067,
    .equal_deck_regret = 0.020593979953978565,
};

constexpr RecordedMetrics kRecordedChildTrain{
    .roots = 480,
    .options = 1568,
    .deck_roots = {96, 96, 96, 96, 96},
    .deck_options = {241, 323, 223, 307, 474},
    .agreement = {
        0.85416666666666696,
        0.80208333333333370,
        0.91666666666666685,
        0.75000000000000056,
        0.82291666666666707,
    },
    .regret = {
        0.0034055529591157400,
        0.0043980532536733305,
        0.0023940615799996606,
        0.0067759391988151127,
        0.0044708167535393696,
    },
    .equal_deck_agreement = 0.82916666666666694,
    .equal_deck_regret = 0.0042888847490286427,
};

constexpr RecordedMetrics kRecordedParentDev{
    .roots = 160,
    .options = 436,
    .deck_roots = {32, 32, 32, 32, 32},
    .deck_options = {77, 101, 73, 93, 92},
    .agreement = {
        0.8125,
        0.625,
        0.8125,
        0.84375,
        0.875,
    },
    .regret = {
        0.0078170044376372379,
        0.029023654710970230,
        0.016344868957503079,
        0.019894860977584288,
        0.011834209400344558,
    },
    .equal_deck_agreement = 0.79375,
    .equal_deck_regret = 0.016982919696807881,
};

constexpr RecordedMetrics kRecordedChildDev{
    .roots = 160,
    .options = 436,
    .deck_roots = {32, 32, 32, 32, 32},
    .deck_options = {77, 101, 73, 93, 92},
    .agreement = {
        0.59375,
        0.5625,
        0.6875,
        0.6875,
        0.8125,
    },
    .regret = {
        0.015170279646879083,
        0.016053397179541035,
        0.018105806595698323,
        0.015142414645853378,
        0.010506463603908557,
    },
    .equal_deck_agreement = 0.66875,
    .equal_deck_regret = 0.014995672334376075,
};

bool same_non_priority_components(
    const LearnedModelComponentFingerprints& left,
    const LearnedModelComponentFingerprints& right) {
    return left.critic == right.critic &&
           left.attack == right.attack &&
           left.block == right.block &&
           left.damage_order == right.damage_order;
}

void require_compatible_endpoints(
    const std::shared_ptr<const LearnedModel>& warm_parent,
    const std::shared_ptr<const LearnedModel>& full_child) {
    if (!warm_parent || !full_child) {
        throw std::invalid_argument(
            "Priority trust region requires two models");
    }
    const auto warm_components =
        learned_model_component_fingerprints(warm_parent);
    const auto child_components =
        learned_model_component_fingerprints(full_child);
    if (!same_non_priority_components(
            warm_components, child_components) ||
        warm_components.priority == child_components.priority) {
        throw std::invalid_argument(
            "Priority trust-region endpoints do not differ "
            "only in Priority");
    }
}

void require_same_shape(
    const Parameters& warm,
    const Parameters& child) {
    if (warm.input_hidden.size() !=
            child.input_hidden.size() ||
        warm.hidden_bias.size() != child.hidden_bias.size() ||
        warm.hidden_output.size() !=
            child.hidden_output.size() ||
        warm.direct.size() != child.direct.size()) {
        throw std::invalid_argument(
            "Priority trust-region endpoints have different "
            "shapes");
    }
    for (std::size_t row = 0;
         row < warm.input_hidden.size(); ++row) {
        if (warm.input_hidden[row].size() !=
            child.input_hidden[row].size()) {
            throw std::invalid_argument(
                "Priority trust-region endpoints have different "
                "shapes");
        }
    }
}

double interpolate_value(
    double warm, double child, double alpha) {
    const double result = std::lerp(warm, child, alpha);
    if (!std::isfinite(result)) {
        throw std::invalid_argument(
            "Priority trust-region interpolation is non-finite");
    }
    return result;
}

void interpolate_vector(
    std::vector<double>& output,
    const std::vector<double>& child,
    double alpha) {
    for (std::size_t index = 0;
         index < output.size(); ++index) {
        output[index] = interpolate_value(
            output[index], child[index], alpha);
    }
}

bool candidate_alpha(double alpha) {
    return std::find(
               kCandidateAlphas.begin(),
               kCandidateAlphas.end(),
               alpha) != kCandidateAlphas.end();
}

void add_failure(
    std::vector<std::string>& failures,
    bool passed, std::string failure) {
    if (!passed) {
        failures.push_back(std::move(failure));
    }
}

bool metrics_match_record(
    const op1::Metrics& actual,
    const RecordedMetrics& expected) {
    if (actual.roots != expected.roots ||
        actual.options != expected.options ||
        actual.equal_deck_top_one_expected_agreement !=
            expected.equal_deck_agreement ||
        actual.equal_deck_mean_regret !=
            expected.equal_deck_regret) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const op1::DeckMetrics& row =
            actual.decks[deck];
        if (row.deck != static_cast<DeckId>(deck) ||
            row.roots != expected.deck_roots[deck] ||
            row.options != expected.deck_options[deck] ||
            row.top_one_expected_agreement !=
                expected.agreement[deck] ||
            row.mean_regret != expected.regret[deck]) {
            return false;
        }
    }
    return true;
}

bool expected_full_child_safety_signature(
    const action_q_offline_gate::ModelGateReport& report) {
    const auto& behavior = report.behavior;
    return full_child_safety_signature_exact({
        .model_gate_passed = report.gate_passed(),
        .frozen_dev_passed =
            report.frozen_dev.gate_passed(),
        .ancestral_passed =
            report.ancestral.gate_passed(),
        .descriptor_order_passed =
            report.descriptor_order.gate_passed(),
        .behavior_gate_passed =
            behavior.gate_passed(),
        .force_spike_gate_passed =
            behavior.force_spike.gate_passed(),
        .force_spike_hidden_repartition_passed =
            behavior.force_spike.hidden_repartition_passed,
        .force_spike_live_selects =
            behavior.force_spike.live_selects_force_spike(),
        .live_force_spike_preserved =
            behavior.live_force_spike_preserved,
        .one_open_payable_selects_pass =
            behavior.one_open_payable_selects_pass,
        .payable_force_spike_selects_pass =
            behavior.force_spike.payable_selects_pass(),
        .five_open_force_spike_selects_pass =
            behavior.five_open_force_spike_selects_pass,
        .redundant_counter_selects_pass =
            behavior.redundant_counter_selects_pass,
        .intervening_counter_selects_opposing_counter =
            behavior
                .intervening_counter_selects_opposing_counter,
        .sick_bear_growth_selects_pass =
            behavior.sick_bear_growth_selects_pass,
        .opponent_growth_excluded =
            behavior.opponent_growth_excluded,
        .braingeyser_x_zero_excluded =
            behavior.braingeyser_x_zero_excluded,
        .failures_exact =
            report.failures() ==
            std::vector<std::string>{
                "focused behavior gate failed"},
    });
}

void append_model_gate_failures(
    std::vector<std::string>& failures,
    const action_q_offline_gate::ModelGateReport& report) {
    add_failure(
        failures, report.frozen_dev.gate_passed(),
        "frozen DevV3 model gate failed");
    add_failure(
        failures, report.ancestral.gate_passed(),
        "Ancestral target model gate failed");
    add_failure(
        failures, report.descriptor_order.gate_passed(),
        "descriptor-order model gate failed");

    const auto& behavior = report.behavior;
    add_failure(
        failures,
        behavior.force_spike.hidden_repartition_passed,
        "Force Spike hidden-repartition control failed");
    add_failure(
        failures, behavior.live_force_spike_preserved,
        "live Force Spike control failed");
    add_failure(
        failures,
        behavior.five_open_force_spike_selects_pass,
        "five-open Force Spike control failed");
    add_failure(
        failures, behavior.redundant_counter_selects_pass,
        "redundant Counterspell control failed");
    add_failure(
        failures,
        behavior
            .intervening_counter_selects_opposing_counter,
        "intervening Counterspell control failed");
    add_failure(
        failures, behavior.sick_bear_growth_selects_pass,
        "summoning-sick Giant Growth control failed");
    add_failure(
        failures, behavior.opponent_growth_excluded,
        "opponent Giant Growth control failed");
    add_failure(
        failures, behavior.braingeyser_x_zero_excluded,
        "Braingeyser X=0 control failed");
    add_failure(
        failures, behavior.gate_passed(),
        "focused behavior consistency gate failed");
    add_failure(
        failures, report.gate_passed(),
        "complete model-only safety battery failed");
}

void require_exact_coordinate(
    const op1::Corpus& corpus,
    const std::shared_ptr<const LearnedModel>& warm_parent,
    const std::shared_ptr<const LearnedModel>& full_child) {
    op1::validate_corpus(corpus);
    op1::require_frozen_census(corpus.census);
    require_compatible_endpoints(warm_parent, full_child);
    if (corpus.digest != kRequiredCorpusDigest ||
        corpus.census.parent_fingerprint !=
            op1::kRequiredWarmParentFingerprint ||
        learned_model_fingerprint(warm_parent) !=
            op1::kRequiredWarmParentFingerprint ||
        learned_model_fingerprint(full_child) !=
            kRequiredFullChildFingerprint ||
        corpus.parent_components !=
            learned_model_component_fingerprints(
                warm_parent)) {
        throw std::invalid_argument(
            "Priority trust-region coordinate is not the exact "
            "frozen OP1 coordinate");
    }
}

ArmEvaluation evaluate_model(
    const op1::Corpus& corpus,
    const std::shared_ptr<const LearnedModel>& warm_parent,
    const std::shared_ptr<const LearnedModel>& full_child,
    double alpha) {
    require_exact_coordinate(
        corpus, warm_parent, full_child);
    ArmEvaluation report;
    report.alpha = alpha;
    report.model = interpolate_priority_head(
        warm_parent, full_child, alpha);
    report.fingerprint =
        learned_model_fingerprint(report.model);
    const auto repeated = interpolate_priority_head(
        warm_parent, full_child, alpha);
    report.repeated_fingerprint =
        learned_model_fingerprint(repeated);
    report.repeated_construction_bit_identical =
        report.fingerprint ==
        report.repeated_fingerprint;

    const auto warm_components =
        learned_model_component_fingerprints(warm_parent);
    const auto candidate_components =
        learned_model_component_fingerprints(report.model);
    report.only_priority_component_changed =
        same_non_priority_components(
            warm_components, candidate_components) &&
        warm_components.priority !=
            candidate_components.priority;

    report.parent_train =
        op1::evaluate(corpus.train, warm_parent);
    report.candidate_train =
        op1::evaluate(corpus.train, report.model);
    report.parent_dev =
        op1::evaluate(corpus.dev, warm_parent);
    report.candidate_dev =
        op1::evaluate(corpus.dev, report.model);
    report.model_gates =
        action_q_offline_gate::evaluate_model_gates(
            warm_parent, report.model);

    report.train_regret_strictly_improved =
        report.candidate_train.equal_deck_mean_regret <
        report.parent_train.equal_deck_mean_regret;
    report.dev_regret_strictly_improved =
        report.candidate_dev.equal_deck_mean_regret <
        report.parent_dev.equal_deck_mean_regret;
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        report.dev_deck_regret_guard[deck] =
            report.candidate_dev.decks[deck].mean_regret <=
            report.parent_dev.decks[deck].mean_regret +
                kMaximumDevDeckRegretIncrease;
    }

    add_failure(
        report.failures,
        report.repeated_construction_bit_identical,
        "repeated interpolation was not bit-identical");
    add_failure(
        report.failures,
        report.only_priority_component_changed,
        "interpolation changed a non-Priority component");
    add_failure(
        report.failures,
        report.train_regret_strictly_improved,
        "TRAIN equal-deck regret did not improve");
    add_failure(
        report.failures,
        report.dev_regret_strictly_improved,
        "DEV equal-deck regret did not improve");
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        add_failure(
            report.failures,
            report.dev_deck_regret_guard[deck],
            "DEV regret guard failed for " +
                std::string(
                    deck_name(static_cast<DeckId>(deck))));
    }
    append_model_gate_failures(
        report.failures, report.model_gates);
    return report;
}

void validate_arm_prefix(
    std::span<const ArmEvaluation> arms) {
    if (arms.size() > kCandidateAlphas.size()) {
        throw std::invalid_argument(
            "Priority trust-region arm prefix is too long");
    }
    for (std::size_t index = 0;
         index < arms.size(); ++index) {
        if (arms[index].alpha != kCandidateAlphas[index]) {
            throw std::invalid_argument(
                "Priority trust-region arms are not a fixed "
                "descending prefix");
        }
    }
}

void validate_gate_prefix(std::span<const bool> gates) {
    if (gates.size() > kCandidateAlphas.size()) {
        throw std::invalid_argument(
            "Priority trust-region gate prefix is too long");
    }
}

} // namespace

bool arm_gate_passed(const ArmGateInputs& inputs) {
    return inputs.repeated_construction_bit_identical &&
           inputs.only_priority_component_changed &&
           inputs.train_regret_strictly_improved &&
           inputs.dev_regret_strictly_improved &&
           std::all_of(
               inputs.dev_deck_regret_guard.begin(),
               inputs.dev_deck_regret_guard.end(),
               [](bool passed) { return passed; }) &&
           inputs.model_gate_passed;
}

bool full_control_gate_passed(
    const FullControlGateInputs& inputs) {
    return inputs.endpoint_pointer_exact &&
           inputs.corpus_digest_exact &&
           inputs.fingerprint_exact &&
           inputs.repeated_construction_bit_identical &&
           inputs.only_priority_component_changed &&
           inputs.parent_train_metrics_exact &&
           inputs.candidate_train_metrics_exact &&
           inputs.parent_dev_metrics_exact &&
           inputs.candidate_dev_metrics_exact &&
           inputs.offline_metric_gates_exact &&
           inputs.expected_safety_signature_exact;
}

bool full_child_safety_signature_exact(
    const FullChildSafetySignatureInputs& inputs) {
    return !inputs.model_gate_passed &&
           inputs.frozen_dev_passed &&
           inputs.ancestral_passed &&
           inputs.descriptor_order_passed &&
           !inputs.behavior_gate_passed &&
           !inputs.force_spike_gate_passed &&
           inputs.force_spike_hidden_repartition_passed &&
           !inputs.force_spike_live_selects &&
           !inputs.live_force_spike_preserved &&
           inputs.one_open_payable_selects_pass &&
           inputs.payable_force_spike_selects_pass &&
           inputs.five_open_force_spike_selects_pass &&
           inputs.redundant_counter_selects_pass &&
           inputs
               .intervening_counter_selects_opposing_counter &&
           inputs.sick_bear_growth_selects_pass &&
           !inputs.opponent_growth_excluded &&
           inputs.braingeyser_x_zero_excluded &&
           inputs.failures_exact;
}

bool ArmEvaluation::gate_passed() const {
    return arm_gate_passed({
               .repeated_construction_bit_identical =
                   repeated_construction_bit_identical,
               .only_priority_component_changed =
                   only_priority_component_changed,
               .train_regret_strictly_improved =
                   train_regret_strictly_improved,
               .dev_regret_strictly_improved =
                   dev_regret_strictly_improved,
               .dev_deck_regret_guard =
                   dev_deck_regret_guard,
               .model_gate_passed =
                   model_gates.gate_passed(),
           }) &&
           failures.empty();
}

bool FullControlReport::control_exact() const {
    const bool metric_gates =
        arm.train_regret_strictly_improved &&
        arm.dev_regret_strictly_improved &&
        std::all_of(
            arm.dev_deck_regret_guard.begin(),
            arm.dev_deck_regret_guard.end(),
            [](bool passed) { return passed; });
    return full_control_gate_passed({
               .endpoint_pointer_exact =
                   endpoint_pointer_exact,
               .corpus_digest_exact = corpus_digest_exact,
               .fingerprint_exact =
                   arm.alpha == 1.0 &&
                   arm.fingerprint ==
                       kRequiredFullChildFingerprint &&
                   arm.repeated_fingerprint ==
                       kRequiredFullChildFingerprint,
               .repeated_construction_bit_identical =
                   arm.repeated_construction_bit_identical,
               .only_priority_component_changed =
                   arm.only_priority_component_changed,
               .parent_train_metrics_exact =
                   parent_train_metrics_exact,
               .candidate_train_metrics_exact =
                   candidate_train_metrics_exact,
               .parent_dev_metrics_exact =
                   parent_dev_metrics_exact,
               .candidate_dev_metrics_exact =
                   candidate_dev_metrics_exact,
               .offline_metric_gates_exact = metric_gates,
               .expected_safety_signature_exact =
                   expected_safety_signature_exact,
           }) &&
           failures.empty();
}

std::shared_ptr<const LearnedModel> interpolate_priority_head(
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> full_child,
    double alpha) {
    if (!std::isfinite(alpha) ||
        alpha < 0.0 || alpha > 1.0) {
        throw std::invalid_argument(
            "Priority trust-region alpha must be in [0, 1]");
    }
    require_compatible_endpoints(warm_parent, full_child);
    if (alpha == 0.0) {
        return warm_parent;
    }
    if (alpha == 1.0) {
        return full_child;
    }

    Parameters interpolated =
        learned_priority_head_parameters(warm_parent);
    const Parameters child =
        learned_priority_head_parameters(full_child);
    require_same_shape(interpolated, child);
    for (std::size_t row = 0;
         row < interpolated.input_hidden.size(); ++row) {
        interpolate_vector(
            interpolated.input_hidden[row],
            child.input_hidden[row], alpha);
    }
    interpolate_vector(
        interpolated.hidden_bias,
        child.hidden_bias, alpha);
    interpolate_vector(
        interpolated.hidden_output,
        child.hidden_output, alpha);
    interpolate_vector(
        interpolated.direct, child.direct, alpha);
    interpolated.output_bias = interpolate_value(
        interpolated.output_bias,
        child.output_bias, alpha);

    auto result = with_learned_priority_head_parameters(
        warm_parent, interpolated);
    const auto warm_components =
        learned_model_component_fingerprints(warm_parent);
    const auto result_components =
        learned_model_component_fingerprints(result);
    if (!same_non_priority_components(
            warm_components, result_components)) {
        throw std::logic_error(
            "Priority interpolation changed a non-Priority "
            "component");
    }
    return result;
}

ArmEvaluation evaluate_arm(
    const op1::Corpus& corpus,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> full_child,
    double alpha) {
    if (!candidate_alpha(alpha)) {
        throw std::invalid_argument(
            "Priority trust-region arm is not preregistered");
    }
    return evaluate_model(
        corpus, warm_parent, full_child, alpha);
}

FullControlReport evaluate_full_control(
    const op1::Corpus& corpus,
    std::shared_ptr<const LearnedModel> warm_parent,
    std::shared_ptr<const LearnedModel> full_child) {
    FullControlReport report;
    report.arm = evaluate_model(
        corpus, warm_parent, full_child, 1.0);
    report.endpoint_pointer_exact =
        report.arm.model == full_child;
    report.corpus_digest_exact =
        corpus.digest == kRequiredCorpusDigest;
    report.parent_train_metrics_exact =
        metrics_match_record(
            report.arm.parent_train,
            kRecordedParentTrain);
    report.candidate_train_metrics_exact =
        metrics_match_record(
            report.arm.candidate_train,
            kRecordedChildTrain);
    report.parent_dev_metrics_exact =
        metrics_match_record(
            report.arm.parent_dev,
            kRecordedParentDev);
    report.candidate_dev_metrics_exact =
        metrics_match_record(
            report.arm.candidate_dev,
            kRecordedChildDev);
    report.expected_safety_signature_exact =
        expected_full_child_safety_signature(
            report.arm.model_gates);

    add_failure(
        report.failures,
        report.endpoint_pointer_exact,
        "alpha-one did not preserve the exact child endpoint");
    add_failure(
        report.failures,
        report.corpus_digest_exact,
        "alpha-one corpus digest did not reproduce OP1");
    add_failure(
        report.failures,
        report.arm.alpha == 1.0 &&
            report.arm.fingerprint ==
                kRequiredFullChildFingerprint &&
            report.arm.repeated_fingerprint ==
                kRequiredFullChildFingerprint,
        "alpha-one child fingerprint did not reproduce OP1");
    add_failure(
        report.failures,
        report.arm.repeated_construction_bit_identical,
        "alpha-one reconstruction was not bit-identical");
    add_failure(
        report.failures,
        report.arm.only_priority_component_changed,
        "alpha-one changed a non-Priority component");
    add_failure(
        report.failures,
        report.parent_train_metrics_exact,
        "alpha-one parent TRAIN metrics did not reproduce OP1");
    add_failure(
        report.failures,
        report.candidate_train_metrics_exact,
        "alpha-one child TRAIN metrics did not reproduce OP1");
    add_failure(
        report.failures,
        report.parent_dev_metrics_exact,
        "alpha-one parent DEV metrics did not reproduce OP1");
    add_failure(
        report.failures,
        report.candidate_dev_metrics_exact,
        "alpha-one child DEV metrics did not reproduce OP1");
    add_failure(
        report.failures,
        report.arm.train_regret_strictly_improved &&
            report.arm.dev_regret_strictly_improved &&
            std::all_of(
                report.arm.dev_deck_regret_guard.begin(),
                report.arm.dev_deck_regret_guard.end(),
                [](bool passed) { return passed; }),
        "alpha-one offline metric gates did not reproduce OP1");
    add_failure(
        report.failures,
        report.expected_safety_signature_exact,
        "alpha-one safety failure did not reproduce OP1");
    return report;
}

std::optional<std::size_t> first_passing_gate_index(
    std::span<const bool> gate_results) {
    validate_gate_prefix(gate_results);
    for (std::size_t index = 0;
         index < gate_results.size(); ++index) {
        if (gate_results[index]) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<double> first_passing_gate_alpha(
    std::span<const bool> gate_results) {
    const auto index =
        first_passing_gate_index(gate_results);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return kCandidateAlphas[*index];
}

SweepDisposition classify_gate_prefix(
    std::span<const bool> gate_results) {
    validate_gate_prefix(gate_results);
    const auto selected =
        first_passing_gate_index(gate_results);
    if (selected.has_value()) {
        if (*selected + 1 != gate_results.size()) {
            throw std::invalid_argument(
                "Priority trust-region inspected a smaller arm "
                "after a pass");
        }
        return SweepDisposition::Selected;
    }
    return gate_results.size() < kCandidateAlphas.size()
               ? SweepDisposition::Continue
               : SweepDisposition::Reject;
}

std::optional<std::size_t> first_passing_arm_index(
    std::span<const ArmEvaluation> arms) {
    validate_arm_prefix(arms);
    for (std::size_t index = 0;
         index < arms.size(); ++index) {
        if (arms[index].gate_passed()) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<double> first_passing_alpha(
    std::span<const ArmEvaluation> arms) {
    const auto index = first_passing_arm_index(arms);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return kCandidateAlphas[*index];
}

SweepDisposition classify_sweep(
    std::span<const ArmEvaluation> arms) {
    validate_arm_prefix(arms);
    const auto selected = first_passing_arm_index(arms);
    if (selected.has_value()) {
        if (*selected + 1 != arms.size()) {
            throw std::invalid_argument(
                "Priority trust-region inspected a smaller arm "
                "after a pass");
        }
        return SweepDisposition::Selected;
    }
    return arms.size() < kCandidateAlphas.size()
               ? SweepDisposition::Continue
               : SweepDisposition::Reject;
}

SelectorDisposition classify_selector(
    const BotBenchmarkSummary& summary) {
    const auto accounted =
        [](const auto& stats) {
            return stats.games ==
                stats.wins + stats.losses + stats.draws;
        };
    if (summary.evaluation_seed != kSelectorSeed ||
        summary.repetitions_per_deck_pairing !=
            action_q_nested_actor_distill::
                kSelectorRepetitions ||
        summary.total_games !=
            action_q_nested_actor_distill::
                kExpectedSelectorGames ||
        !accounted(summary.challenger_stats) ||
        !accounted(summary.baseline_stats) ||
        summary.challenger_stats.games !=
            action_q_nested_actor_distill::
                kExpectedSelectorGames ||
        summary.baseline_stats.games !=
            action_q_nested_actor_distill::
                kExpectedSelectorGames ||
        summary.challenger_stats.wins !=
            summary.baseline_stats.losses ||
        summary.challenger_stats.losses !=
            summary.baseline_stats.wins ||
        summary.challenger_stats.draws !=
            summary.baseline_stats.draws) {
        return SelectorDisposition::Reject;
    }
    for (std::size_t deck = 0;
         deck < kDeckCount; ++deck) {
        const auto& challenger =
            summary.challenger_decks[deck];
        const auto& baseline =
            summary.baseline_decks[deck];
        if (!accounted(challenger) ||
            !accounted(baseline) ||
            challenger.games !=
                action_q_nested_actor_distill::
                    kExpectedSelectorGamesPerDeck ||
            baseline.games !=
                action_q_nested_actor_distill::
                    kExpectedSelectorGamesPerDeck) {
            return SelectorDisposition::Reject;
        }
    }
    const bool deck_floor =
        std::all_of(
            summary.challenger_decks.begin(),
            summary.challenger_decks.end(),
            [](const DeckSimulationStats& deck) {
                return deck.wins >=
                    action_q_nested_actor_distill::
                        kMinimumDeckWins;
            });
    return deck_floor &&
                   summary.challenger_stats.wins >=
                       action_q_nested_actor_distill::
                           kManualOnlyWins
               ? SelectorDisposition::ManualPilot
               : SelectorDisposition::Reject;
}

} // namespace old_school::action_q_priority_trust_region
