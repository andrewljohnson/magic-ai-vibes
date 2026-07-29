#include "old_school/action_q_priority_trust_region.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace trust =
    old_school::action_q_priority_trust_region;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

std::shared_ptr<const old_school::LearnedModel>
parent_model() {
    static const auto model =
        old_school::train_learned_model(
            1, 0xA024A11CEULL);
    return model;
}

std::shared_ptr<const old_school::LearnedModel>
priority_only_child() {
    static const auto model = [] {
        const auto parent = parent_model();
        auto parameters =
            old_school::learned_priority_head_parameters(
                parent);
        parameters.input_hidden[0][0] += 0.75;
        parameters.hidden_bias[0] -= 0.50;
        parameters.hidden_output[0] += 0.25;
        parameters.direct[0] -= 1.25;
        parameters.output_bias += 2.0;
        return old_school::
            with_learned_priority_head_parameters(
                parent, parameters);
    }();
    return model;
}

std::shared_ptr<const old_school::LearnedModel>
different_model() {
    static const auto model =
        old_school::train_learned_model(
            1, 0xA024D1FFULL);
    return model;
}

bool same_non_priority_components(
    const old_school::LearnedModelComponentFingerprints&
        left,
    const old_school::LearnedModelComponentFingerprints&
        right) {
    return left.critic == right.critic &&
           left.attack == right.attack &&
           left.block == right.block &&
           left.damage_order == right.damage_order;
}

void expect_vector_lerp(
    const std::vector<double>& actual,
    const std::vector<double>& parent,
    const std::vector<double>& child,
    double alpha,
    std::string_view message) {
    expect(
        actual.size() == parent.size() &&
            actual.size() == child.size(),
        message);
    for (std::size_t index = 0;
         index < actual.size(); ++index) {
        expect(
            actual[index] ==
                std::lerp(
                    parent[index], child[index], alpha),
            message);
    }
}

void expect_parameters_lerp(
    const old_school::LearnedPriorityHeadParameters&
        actual,
    const old_school::LearnedPriorityHeadParameters&
        parent,
    const old_school::LearnedPriorityHeadParameters&
        child,
    double alpha) {
    expect(
        actual.input_hidden.size() ==
                parent.input_hidden.size() &&
            actual.input_hidden.size() ==
                child.input_hidden.size(),
        "interpolated input-hidden row count drifted");
    for (std::size_t row = 0;
         row < actual.input_hidden.size(); ++row) {
        expect_vector_lerp(
            actual.input_hidden[row],
            parent.input_hidden[row],
            child.input_hidden[row], alpha,
            "interpolated input-hidden tensor drifted");
    }
    expect_vector_lerp(
        actual.hidden_bias, parent.hidden_bias,
        child.hidden_bias, alpha,
        "interpolated hidden-bias tensor drifted");
    expect_vector_lerp(
        actual.hidden_output, parent.hidden_output,
        child.hidden_output, alpha,
        "interpolated hidden-output tensor drifted");
    expect_vector_lerp(
        actual.direct, parent.direct, child.direct,
        alpha, "interpolated direct tensor drifted");
    expect(
        actual.output_bias ==
            std::lerp(
                parent.output_bias, child.output_bias,
                alpha),
        "interpolated output bias drifted");
}

trust::ArmGateInputs passing_arm_inputs() {
    trust::ArmGateInputs inputs{
        .repeated_construction_bit_identical = true,
        .only_priority_component_changed = true,
        .train_regret_strictly_improved = true,
        .dev_regret_strictly_improved = true,
        .model_gate_passed = true,
    };
    inputs.dev_deck_regret_guard.fill(true);
    return inputs;
}

trust::FullControlGateInputs
passing_full_control_inputs() {
    return {
        .endpoint_pointer_exact = true,
        .corpus_digest_exact = true,
        .fingerprint_exact = true,
        .repeated_construction_bit_identical = true,
        .only_priority_component_changed = true,
        .parent_train_metrics_exact = true,
        .candidate_train_metrics_exact = true,
        .parent_dev_metrics_exact = true,
        .candidate_dev_metrics_exact = true,
        .offline_metric_gates_exact = true,
        .expected_safety_signature_exact = true,
    };
}

trust::FullChildSafetySignatureInputs
passing_full_child_safety_signature() {
    return {
        .model_gate_passed = false,
        .frozen_dev_passed = true,
        .ancestral_passed = true,
        .descriptor_order_passed = true,
        .behavior_gate_passed = false,
        .force_spike_gate_passed = false,
        .force_spike_hidden_repartition_passed = true,
        .force_spike_live_selects = false,
        .live_force_spike_preserved = false,
        .one_open_payable_selects_pass = true,
        .payable_force_spike_selects_pass = true,
        .five_open_force_spike_selects_pass = true,
        .redundant_counter_selects_pass = true,
        .intervening_counter_selects_opposing_counter = true,
        .sick_bear_growth_selects_pass = true,
        .opponent_growth_excluded = false,
        .braingeyser_x_zero_excluded = true,
        .failures_exact = true,
    };
}

old_school::BotBenchmarkSummary selector_summary(
    std::size_t wins) {
    old_school::BotBenchmarkSummary summary;
    summary.evaluation_seed = trust::kSelectorSeed;
    summary.repetitions_per_deck_pairing = 1;
    summary.total_games = 60;
    summary.challenger_stats.games = 60;
    summary.challenger_stats.wins = wins;
    summary.challenger_stats.losses = 60 - wins;
    summary.baseline_stats.games = 60;
    summary.baseline_stats.wins = 60 - wins;
    summary.baseline_stats.losses = wins;
    for (auto& deck : summary.challenger_decks) {
        deck.games = 12;
        deck.wins = 3;
        deck.losses = 9;
    }
    for (auto& deck : summary.baseline_decks) {
        deck.games = 12;
        deck.wins = 9;
        deck.losses = 3;
    }
    return summary;
}

void test_frozen_protocol_constants() {
    expect(
        trust::kCandidateAlphas ==
            std::array<double, 3>{
                0.75, 0.50, 0.25},
        "trust-region alpha order drifted");
    expect(
        trust::kSelectorSeed == 202607290211ULL,
        "manual selector seed drifted");
    expect(
        trust::kMaximumDevDeckRegretIncrease ==
            trust::op1::
                kMaximumDevDeckRegretIncrease,
        "DEV deck regret guard drifted from OP1");
    expect(
        trust::kRequiredCorpusDigest ==
            "985026631f56dceba5c42bc4c7247757640d092f92a3d030759f607cd5b8c5df",
        "required OP1 corpus digest drifted");
    expect(
        trust::kRequiredFullChildFingerprint ==
            "a4cdb8a7cf53cea58d79a7591eafd76c8b4724bce457c8d0ab7e533ba19b036f",
        "required rejected-child fingerprint drifted");
}

void test_interpolation_endpoints_validation_and_isolation() {
    const auto parent = parent_model();
    const auto child = priority_only_child();
    expect(
        trust::interpolate_priority_head(
            parent, child, 0.0) == parent,
        "alpha zero did not return the exact parent");
    expect(
        trust::interpolate_priority_head(
            parent, child, 1.0) == child,
        "alpha one did not return the exact child");

    const double alpha = 0.75;
    const auto first =
        trust::interpolate_priority_head(
            parent, child, alpha);
    const auto second =
        trust::interpolate_priority_head(
            parent, child, alpha);
    expect(
        first != nullptr && second != nullptr &&
            first != parent && first != child,
        "interior alpha did not construct a new model");
    expect(
        old_school::learned_model_fingerprint(first) ==
            old_school::learned_model_fingerprint(second),
        "repeated interpolation changed fingerprint");
    expect(
        old_school::learned_priority_head_parameters(
            first) ==
            old_school::learned_priority_head_parameters(
                second),
        "repeated interpolation changed Priority bits");
    expect_parameters_lerp(
        old_school::learned_priority_head_parameters(
            first),
        old_school::learned_priority_head_parameters(
            parent),
        old_school::learned_priority_head_parameters(
            child),
        alpha);

    const auto parent_components =
        old_school::learned_model_component_fingerprints(
            parent);
    const auto child_components =
        old_school::learned_model_component_fingerprints(
            child);
    const auto result_components =
        old_school::learned_model_component_fingerprints(
            first);
    expect(
        same_non_priority_components(
            parent_components, child_components) &&
            same_non_priority_components(
                parent_components, result_components),
        "interpolation changed a non-Priority component");
    expect(
        parent_components.priority !=
                child_components.priority &&
            parent_components.priority !=
                result_components.priority &&
            child_components.priority !=
                result_components.priority,
        "interior interpolation did not isolate a distinct Priority head");

    const std::array<double, 4> invalid_alphas{
        -0.01,
        1.01,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
    };
    for (const double invalid : invalid_alphas) {
        expect_rejected(
            [&] {
                static_cast<void>(
                    trust::interpolate_priority_head(
                        parent, child, invalid));
            },
            "invalid interpolation alpha was accepted");
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                trust::interpolate_priority_head(
                    nullptr, child, 0.5));
        },
        "null trust-region parent was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                trust::interpolate_priority_head(
                    parent, nullptr, 0.5));
        },
        "null trust-region child was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                trust::interpolate_priority_head(
                    parent, different_model(), 0.5));
        },
        "a child differing outside Priority was accepted");
}

void test_arm_gate_is_conjunctive() {
    const trust::ArmGateInputs passing =
        passing_arm_inputs();
    expect(
        trust::arm_gate_passed(passing),
        "complete arm eligibility did not pass");

    constexpr std::array<
        bool trust::ArmGateInputs::*, 5>
        scalar_gates{
            &trust::ArmGateInputs::
                repeated_construction_bit_identical,
            &trust::ArmGateInputs::
                only_priority_component_changed,
            &trust::ArmGateInputs::
                train_regret_strictly_improved,
            &trust::ArmGateInputs::
                dev_regret_strictly_improved,
            &trust::ArmGateInputs::model_gate_passed,
        };
    for (const auto gate : scalar_gates) {
        auto mutation = passing;
        mutation.*gate = false;
        expect(
            !trust::arm_gate_passed(mutation),
            "a false scalar arm gate was accepted");
    }
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto mutation = passing;
        mutation.dev_deck_regret_guard[deck] = false;
        expect(
            !trust::arm_gate_passed(mutation),
            "a failed per-deck regret guard was accepted");
    }
}

void test_full_control_gate_is_conjunctive() {
    const trust::FullControlGateInputs passing =
        passing_full_control_inputs();
    expect(
        trust::full_control_gate_passed(passing),
        "complete alpha-one control did not pass");

    constexpr std::array<
        bool trust::FullControlGateInputs::*, 11>
        gates{
            &trust::FullControlGateInputs::
                endpoint_pointer_exact,
            &trust::FullControlGateInputs::
                corpus_digest_exact,
            &trust::FullControlGateInputs::
                fingerprint_exact,
            &trust::FullControlGateInputs::
                repeated_construction_bit_identical,
            &trust::FullControlGateInputs::
                only_priority_component_changed,
            &trust::FullControlGateInputs::
                parent_train_metrics_exact,
            &trust::FullControlGateInputs::
                candidate_train_metrics_exact,
            &trust::FullControlGateInputs::
                parent_dev_metrics_exact,
            &trust::FullControlGateInputs::
                candidate_dev_metrics_exact,
            &trust::FullControlGateInputs::
                offline_metric_gates_exact,
            &trust::FullControlGateInputs::
                expected_safety_signature_exact,
        };
    for (const auto gate : gates) {
        auto mutation = passing;
        mutation.*gate = false;
        expect(
            !trust::full_control_gate_passed(mutation),
            "a mutated alpha-one control was accepted");
    }
}

void test_full_child_safety_signature_is_exact() {
    const auto passing =
        passing_full_child_safety_signature();
    expect(
        trust::full_child_safety_signature_exact(passing),
        "exact observed alpha-one safety signature did not pass");

    constexpr std::array<
        bool trust::FullChildSafetySignatureInputs::*, 12>
        expected_true{
            &trust::FullChildSafetySignatureInputs::
                frozen_dev_passed,
            &trust::FullChildSafetySignatureInputs::
                ancestral_passed,
            &trust::FullChildSafetySignatureInputs::
                descriptor_order_passed,
            &trust::FullChildSafetySignatureInputs::
                force_spike_hidden_repartition_passed,
            &trust::FullChildSafetySignatureInputs::
                one_open_payable_selects_pass,
            &trust::FullChildSafetySignatureInputs::
                payable_force_spike_selects_pass,
            &trust::FullChildSafetySignatureInputs::
                five_open_force_spike_selects_pass,
            &trust::FullChildSafetySignatureInputs::
                redundant_counter_selects_pass,
            &trust::FullChildSafetySignatureInputs::
                intervening_counter_selects_opposing_counter,
            &trust::FullChildSafetySignatureInputs::
                sick_bear_growth_selects_pass,
            &trust::FullChildSafetySignatureInputs::
                braingeyser_x_zero_excluded,
            &trust::FullChildSafetySignatureInputs::
                failures_exact,
        };
    for (const auto member : expected_true) {
        auto mutation = passing;
        mutation.*member = false;
        expect(
            !trust::full_child_safety_signature_exact(
                mutation),
            "missing required alpha-one safety fact was accepted");
    }

    constexpr std::array<
        bool trust::FullChildSafetySignatureInputs::*, 6>
        expected_false{
            &trust::FullChildSafetySignatureInputs::
                model_gate_passed,
            &trust::FullChildSafetySignatureInputs::
                behavior_gate_passed,
            &trust::FullChildSafetySignatureInputs::
                force_spike_gate_passed,
            &trust::FullChildSafetySignatureInputs::
                force_spike_live_selects,
            &trust::FullChildSafetySignatureInputs::
                live_force_spike_preserved,
            &trust::FullChildSafetySignatureInputs::
                opponent_growth_excluded,
        };
    for (const auto member : expected_false) {
        auto mutation = passing;
        mutation.*member = true;
        expect(
            !trust::full_child_safety_signature_exact(
                mutation),
            "unexpected alpha-one safety fact was accepted");
    }
}

void test_first_pass_selection_and_stop_semantics() {
    const std::array<bool, 0> none{};
    expect(
        !trust::first_passing_gate_index(none)
             .has_value() &&
            !trust::first_passing_gate_alpha(none)
                 .has_value() &&
            trust::classify_gate_prefix(none) ==
                trust::SweepDisposition::Continue,
        "empty sweep prefix must continue");

    const std::array<bool, 1> first_pass{true};
    expect(
        trust::first_passing_gate_index(first_pass) ==
                std::optional<std::size_t>{0} &&
            trust::first_passing_gate_alpha(first_pass) ==
                std::optional<double>{0.75} &&
            trust::classify_gate_prefix(first_pass) ==
                trust::SweepDisposition::Selected,
        "first passing arm must select alpha 0.75");

    const std::array<bool, 2> second_pass{false, true};
    expect(
        trust::first_passing_gate_index(second_pass) ==
                std::optional<std::size_t>{1} &&
            trust::first_passing_gate_alpha(second_pass) ==
                std::optional<double>{0.50} &&
            trust::classify_gate_prefix(second_pass) ==
                trust::SweepDisposition::Selected,
        "second passing arm must select alpha 0.50");

    const std::array<bool, 3> third_pass{
        false, false, true};
    expect(
        trust::first_passing_gate_index(third_pass) ==
                std::optional<std::size_t>{2} &&
            trust::first_passing_gate_alpha(third_pass) ==
                std::optional<double>{0.25} &&
            trust::classify_gate_prefix(third_pass) ==
                trust::SweepDisposition::Selected,
        "third passing arm must select alpha 0.25");

    const std::array<bool, 2> incomplete_failures{
        false, false};
    expect(
        !trust::first_passing_gate_index(
             incomplete_failures)
             .has_value() &&
            trust::classify_gate_prefix(
                incomplete_failures) ==
                trust::SweepDisposition::Continue,
        "incomplete all-failure prefix must continue");

    const std::array<bool, 3> complete_failure{
        false, false, false};
    expect(
        !trust::first_passing_gate_index(
             complete_failure)
             .has_value() &&
            trust::classify_gate_prefix(
                complete_failure) ==
                trust::SweepDisposition::Reject,
        "three failed arms must reject the fixed sweep");

    const std::array<bool, 4> oversized{
        false, false, false, false};
    expect_rejected(
        [&] {
            static_cast<void>(
                trust::first_passing_gate_index(
                    oversized));
        },
        "an oversized sweep prefix was accepted");
    expect_rejected(
        [&] {
            static_cast<void>(
                trust::classify_gate_prefix(
                    oversized));
        },
        "an oversized sweep was classified");

    const std::array<bool, 2> post_pass_peek{
        true, false};
    expect_rejected(
        [&] {
            static_cast<void>(
                trust::classify_gate_prefix(
                    post_pass_peek));
        },
        "a sweep that continued after its first pass was classified");

    std::array<trust::ArmEvaluation, 1> arm_prefix{};
    arm_prefix[0].alpha = 0.75;
    expect(
        trust::classify_sweep(arm_prefix) ==
            trust::SweepDisposition::Continue,
        "one failed fixed-order arm must continue");
    arm_prefix[0].alpha = 0.50;
    expect_rejected(
        [&] {
            static_cast<void>(
                trust::classify_sweep(arm_prefix));
        },
        "an arm prefix starting out of order was accepted");
}

void test_selector_manual_gate_and_all_deck_floors() {
    auto summary = selector_summary(31);
    expect(
        trust::classify_selector(summary) ==
            trust::SelectorDisposition::ManualPilot,
        "31/60 with all five deck floors must license only a manual pilot");

    summary = selector_summary(60);
    for (auto& deck : summary.challenger_decks) {
        deck.wins = 12;
        deck.losses = 0;
    }
    for (auto& deck : summary.baseline_decks) {
        deck.wins = 0;
        deck.losses = 12;
    }
    expect(
        trust::classify_selector(summary) ==
            trust::SelectorDisposition::ManualPilot,
        "even 60/60 may license only a manual pilot");

    expect(
        trust::classify_selector(
            selector_summary(30)) ==
            trust::SelectorDisposition::Reject,
        "30/60 must reject");

    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto failed_deck = selector_summary(31);
        failed_deck.challenger_decks[deck].wins = 2;
        failed_deck.challenger_decks[deck].losses = 10;
        failed_deck.baseline_decks[deck].wins = 10;
        failed_deck.baseline_decks[deck].losses = 2;
        expect(
            trust::classify_selector(failed_deck) ==
                trust::SelectorDisposition::Reject,
            "one deck below 3/12 was accepted");
    }

    auto wrong_seed = selector_summary(31);
    ++wrong_seed.evaluation_seed;
    expect(
        trust::classify_selector(wrong_seed) ==
            trust::SelectorDisposition::Reject,
        "an unregistered selector seed was accepted");
    auto wrong_total = selector_summary(31);
    --wrong_total.total_games;
    expect(
        trust::classify_selector(wrong_total) ==
            trust::SelectorDisposition::Reject,
        "a non-60-game selector was accepted");
    auto wrong_repetitions = selector_summary(31);
    ++wrong_repetitions.repetitions_per_deck_pairing;
    expect(
        trust::classify_selector(wrong_repetitions) ==
            trust::SelectorDisposition::Reject,
        "an unregistered selector repetition count was accepted");
    auto wrong_accounting = selector_summary(31);
    --wrong_accounting.challenger_stats.games;
    expect(
        trust::classify_selector(wrong_accounting) ==
            trust::SelectorDisposition::Reject,
        "inconsistent aggregate selector accounting was accepted");
    for (std::size_t deck = 0;
         deck < old_school::kDeckCount; ++deck) {
        auto wrong_deck_size = selector_summary(31);
        --wrong_deck_size.challenger_decks[deck].games;
        expect(
            trust::classify_selector(wrong_deck_size) ==
                trust::SelectorDisposition::Reject,
            "a non-12-game deck slice was accepted");
    }
}

} // namespace

int main() {
    try {
        test_frozen_protocol_constants();
        test_interpolation_endpoints_validation_and_isolation();
        test_arm_gate_is_conjunctive();
        test_full_control_gate_is_conjunctive();
        test_full_child_safety_signature_is_exact();
        test_first_pass_selection_and_stop_semantics();
        test_selector_manual_gate_and_all_deck_floors();
        std::cout
            << "7 action-Q Priority trust-region tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "action-Q Priority trust-region test failure: "
            << error.what() << '\n';
        return 1;
    }
}
