#include "old_school/fq4_dev_background_diagnostic.hpp"

#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_evaluator.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace diagnostic =
    old_school::fq4_dev_background_diagnostic;
namespace bundle = old_school::fq4_dev_bundle;
namespace evaluator = old_school::fq4_dev_evaluator;

namespace {

class TestRunner {
  public:
    template <typename Function>
    void run(std::string_view name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[FAIL] " << name << ": "
                      << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << passed_ << " passed, " << failed_
                  << " failed\n";
        return failed_ == 0 ? 0 : 1;
    }

  private:
    std::size_t passed_ = 0;
    std::size_t failed_ = 0;
};

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void expect_close(
    double actual, double expected, double tolerance,
    std::string_view message) {
    if (!std::isfinite(actual) ||
        std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function>
void expect_rejected(
    Function function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

bundle::SelectedRow make_row(
    bundle::Split split, std::size_t deck,
    bool background) {
    const std::size_t options =
        background && deck ==
            static_cast<std::size_t>(
                old_school::DeckId::RUAggro)
            ? 3U
            : 2U;
    bundle::SelectedRow result{
        .split = split,
        .census = {
            .trace_ordinal =
                static_cast<std::uint32_t>(
                    1000U +
                    100U *
                        static_cast<std::size_t>(
                            split == bundle::Split::Check) +
                    10U *
                        static_cast<std::size_t>(
                            background) +
                    deck),
            .owner_deck =
                static_cast<std::uint8_t>(deck),
            .opponent_deck =
                static_cast<std::uint8_t>(
                    (deck + 1U) %
                    bundle::kDeckCount),
            .pass_index = 0,
            .dominance =
                std::vector<bundle::DominanceCount>(
                    options),
        },
        .roles = static_cast<std::uint8_t>(
            background
                ? bundle::Role::BackgroundControl
                : bundle::Role::DominancePositive),
        .production_seed =
            0xD1A60000ULL + deck,
    };
    const std::array<double, 3> base{
        0.50, 0.55, 0.45};
    for (std::size_t option = 0;
         option < options; ++option) {
        bundle::DominanceCount dominance;
        if (option == 1) {
            dominance = {
                .complete =
                    static_cast<std::uint8_t>(
                        bundle::kWorldCount),
                .strict =
                    static_cast<std::uint8_t>(
                        background
                            ? bundle::kWorldCount - 1U
                            : bundle::kWorldCount),
            };
        }
        result.census.dominance[option] =
            dominance;
        bundle::ActionRow action{
            .descriptor =
                "PRIVATE_" +
                std::to_string(deck) + "_" +
                std::to_string(option),
            .is_pass = option == 0,
            .dominance = dominance,
            .base_score_bits = bits(base[option]),
            .parent_residual_bits = bits(0.0),
            .features = {
                {
                    .index =
                        static_cast<std::uint16_t>(
                            option),
                    .value_bits =
                        bits(
                            option == 0
                                ? 1.0
                                : -1.0 -
                                      static_cast<double>(
                                          option)),
                },
            },
        };
        for (std::size_t world = 0;
             world < bundle::kWorldCount; ++world) {
            action.raw_sample_bits[world] =
                bits(base[option]);
            action.shallow_prior_sample_bits[world] =
                bits(base[option]);
            action.continuation_sample_bits[world] =
                bits(base[option]);
        }
        result.actions.push_back(
            std::move(action));
    }
    return result;
}

evaluator::PreparedCorpus corpus() {
    std::vector<bundle::SelectedRow> fit;
    std::vector<bundle::SelectedRow> check;
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        fit.push_back(
            make_row(
                bundle::Split::Fit,
                deck, false));
        check.push_back(
            make_row(
                bundle::Split::Check,
                deck, false));
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        fit.push_back(
            make_row(
                bundle::Split::Fit,
                deck, true));
        check.push_back(
            make_row(
                bundle::Split::Check,
                deck, true));
    }
    return evaluator::testing::
        prepare_selected_rows(fit, check);
}

evaluator::CorpusLogits zero_logits(
    const evaluator::PreparedCorpus& source) {
    evaluator::CorpusLogits result;
    for (const auto& row : source.fit) {
        result.fit.emplace_back(
            row.actions.size(), 0.0);
    }
    for (const auto& row : source.check) {
        result.check.emplace_back(
            row.actions.size(), 0.0);
    }
    return result;
}

bool background(const evaluator::PreparedRow& row) {
    return
        (row.roles &
         static_cast<std::uint8_t>(
             bundle::Role::BackgroundControl)) != 0;
}

evaluator::CorpusLogits green_white_drift(
    const evaluator::PreparedCorpus& source) {
    evaluator::CorpusLogits result =
        zero_logits(source);
    const auto apply =
        [](const std::vector<evaluator::PreparedRow>& rows,
           std::vector<std::vector<double>>& logits) {
            for (std::size_t index = 0;
                 index < rows.size(); ++index) {
                const std::size_t deck =
                    rows[index].owner_deck;
                if (!background(rows[index]) ||
                    (deck !=
                         static_cast<std::size_t>(
                             old_school::DeckId::Green) &&
                     deck !=
                         static_cast<std::size_t>(
                             old_school::DeckId::White))) {
                    continue;
                }
                logits[index][0] = 1.0;
                logits[index][1] = -1.0;
            }
        };
    apply(source.fit, result.fit);
    apply(source.check, result.check);
    return result;
}

double mixed_probability(double first, double second) {
    const double maximum =
        std::max(first, second);
    const double first_weight =
        std::exp(
            (first - maximum) /
            evaluator::kPolicyTemperature);
    const double second_weight =
        std::exp(
            (second - maximum) /
            evaluator::kPolicyTemperature);
    const double softmax =
        first_weight /
        (first_weight + second_weight);
    return
        evaluator::kBehaviorPrimaryWeight *
            softmax +
        (1.0 -
         evaluator::kBehaviorPrimaryWeight) /
            2.0;
}

double binary_kl(
    double parent_first,
    double candidate_first) {
    return
        parent_first *
            std::log(
                parent_first /
                candidate_first) +
        (1.0 - parent_first) *
            std::log(
                (1.0 - parent_first) /
                (1.0 - candidate_first));
}

std::size_t occurrence_count(
    std::string_view value,
    std::string_view token) {
    std::size_t result = 0;
    std::size_t offset = 0;
    while ((offset = value.find(token, offset)) !=
           std::string_view::npos) {
        ++result;
        offset += token.size();
    }
    return result;
}

bundle::SelectedRow make_stack_row(
    bundle::Split split,
    std::size_t deck,
    bundle::Role role,
    std::size_t options,
    std::size_t stack_size,
    bool explicit_zero = false) {
    bundle::SelectedRow result{
        .split = split,
        .census = {
            .trace_ordinal =
                static_cast<std::uint32_t>(
                    1000U + deck),
            .owner_deck =
                static_cast<std::uint8_t>(deck),
            .opponent_deck =
                static_cast<std::uint8_t>(
                    (deck + 1U) %
                    bundle::kDeckCount),
            .pass_index = 0,
            .dominance =
                std::vector<bundle::DominanceCount>(
                    options),
        },
        .roles =
            static_cast<std::uint8_t>(role),
        .production_seed =
            0x57AC0000ULL + deck,
    };
    for (std::size_t option = 0;
         option < options; ++option) {
        bundle::ActionRow action{
            .descriptor =
                "PRIVATE_STACK_" +
                std::to_string(deck) + "_" +
                std::to_string(option),
            .is_pass = option == 0,
            .base_score_bits = bits(0.5),
            .parent_residual_bits = bits(0.0),
        };
        if (stack_size != 0 || explicit_zero) {
            action.features.push_back(
                {
                    .index =
                        static_cast<std::uint16_t>(
                            diagnostic::
                                kStackSizeFeatureIndex),
                    .value_bits =
                        bits(
                            static_cast<double>(
                                stack_size) /
                            static_cast<double>(
                                diagnostic::
                                    kStackSizeEncodingDenominator)),
                });
        }
        result.actions.push_back(
            std::move(action));
    }
    return result;
}

struct FrozenStackCell {
    std::size_t empty_roots = 0;
    std::size_t empty_options = 0;
    std::size_t active_roots = 0;
    std::size_t active_options = 0;
};

constexpr std::array<FrozenStackCell, bundle::kDeckCount>
    kFitPositiveStack{{
        {11, 35, 0, 0},
        {4, 29, 0, 0},
        {20, 103, 11, 39},
        {6, 26, 7, 21},
        {29, 295, 0, 0},
    }};

constexpr std::array<FrozenStackCell, bundle::kDeckCount>
    kCheckPositiveStack{{
        {20, 54, 0, 0},
        {5, 33, 0, 0},
        {20, 107, 11, 33},
        {1, 4, 6, 18},
        {31, 322, 0, 0},
    }};

constexpr std::array<std::size_t, bundle::kDeckCount>
    kBackgroundOptions{{2, 2, 2, 2, 3}};

void append_stack_rows(
    std::vector<bundle::SelectedRow>& rows,
    bundle::Split split,
    std::size_t deck,
    bundle::Role role,
    std::size_t roots,
    std::size_t options,
    std::size_t stack_size) {
    if ((roots == 0) != (options == 0) ||
        (roots != 0 && options < roots)) {
        throw std::runtime_error(
            "invalid synthetic stack-cell shape");
    }
    if (roots == 0) {
        return;
    }
    const std::size_t options_per_root =
        options / roots;
    const std::size_t remainder =
        options % roots;
    for (std::size_t root = 0;
         root < roots; ++root) {
        rows.push_back(
            make_stack_row(
                split, deck, role,
                options_per_root +
                    static_cast<std::size_t>(
                        root < remainder),
                stack_size));
    }
}

std::pair<
    std::vector<bundle::SelectedRow>,
    std::vector<bundle::SelectedRow>>
frozen_stack_rows() {
    std::vector<bundle::SelectedRow> fit;
    std::vector<bundle::SelectedRow> check;
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        const FrozenStackCell fit_cell =
            kFitPositiveStack[deck];
        append_stack_rows(
            fit, bundle::Split::Fit, deck,
            bundle::Role::DominancePositive,
            fit_cell.empty_roots,
            fit_cell.empty_options, 0);
        append_stack_rows(
            fit, bundle::Split::Fit, deck,
            bundle::Role::DominancePositive,
            fit_cell.active_roots,
            fit_cell.active_options, 1);
        append_stack_rows(
            fit, bundle::Split::Fit, deck,
            bundle::Role::BackgroundControl,
            1, kBackgroundOptions[deck], 0);

        const FrozenStackCell check_cell =
            kCheckPositiveStack[deck];
        append_stack_rows(
            check, bundle::Split::Check, deck,
            bundle::Role::DominancePositive,
            check_cell.empty_roots,
            check_cell.empty_options, 0);
        append_stack_rows(
            check, bundle::Split::Check, deck,
            bundle::Role::DominancePositive,
            check_cell.active_roots,
            check_cell.active_options, 1);
        append_stack_rows(
            check, bundle::Split::Check, deck,
            bundle::Role::BackgroundControl,
            1, kBackgroundOptions[deck], 0);
    }
    return {std::move(fit), std::move(check)};
}

diagnostic::StackCensusReport stack_report_for(
    const diagnostic::StackCensus& census) {
    return {
        .bundle_schema =
            std::string(bundle::kBundleSchema),
        .bundle_bytes =
            bundle::kPublishedArtifactBytes,
        .bundle_sha256 =
            std::string(
                bundle::kPublishedArtifactSha256),
        .feature_schema =
            std::string(bundle::kFeatureSchema),
        .feature_count =
            bundle::kFeatureCount,
        .feature_contract_sha256 =
            std::string(
                bundle::kFeatureContractSha256),
        .stack_size_feature_index =
            diagnostic::kStackSizeFeatureIndex,
        .stack_size_encoding_denominator =
            diagnostic::
                kStackSizeEncodingDenominator,
        .census = census,
        .bundle_immutable = true,
    };
}

old_school::LearnedModelComponentFingerprints
parent_components() {
    return {
        .critic =
            std::string(
                bundle::kParentCriticFingerprint),
        .priority =
            std::string(
                bundle::kParentPriorityFingerprint),
        .attack =
            std::string(
                bundle::kParentAttackFingerprint),
        .block =
            std::string(
                bundle::kParentBlockFingerprint),
        .damage_order =
            std::string(
                bundle::kParentDamageOrderFingerprint),
    };
}

diagnostic::Report report_for(
    const diagnostic::Measurements& parent_control,
    const diagnostic::Measurements& measurements) {
    const auto parent = parent_components();
    auto candidate = parent;
    candidate.priority = std::string(64, 'd');
    return {
        .parent_fingerprint =
            std::string(
                bundle::kParentModelFingerprint),
        .candidate_fingerprint =
            std::string(
                diagnostic::
                    kRejectedCandidateFingerprint),
        .parent_components = parent,
        .candidate_components = candidate,
        .fit_accounting = {
            .fit_examples =
                diagnostic::kFitExamples,
            .fit_options =
                diagnostic::kFitOptions,
            .optimizer_calls = 1,
            .training_input_sha256 =
                std::string(
                    diagnostic::
                        kFitInputSha256),
            .optimizer =
                evaluator::kOptimizer,
        },
        .parent_control = parent_control,
        .measurements = measurements,
        .parent_immutable = true,
        .candidate_exact = true,
        .nonpriority_components_identical = true,
    };
}

void test_parent_control_is_exact_zero() {
    expect(
        diagnostic::kFitInputSha256 ==
            "586b121c3c9bdb1a61305cac86882cd20"
            "b5d2ba332b4d5a54defc2c7756393a1",
        "canonical DEV1 fit-input digest drifted");
    const evaluator::PreparedCorpus source =
        corpus();
    const evaluator::CorpusLogits parent =
        zero_logits(source);
    const diagnostic::Measurements measured =
        diagnostic::measure(
            source, parent, parent);
    expect(
        measured.fit.roots ==
                bundle::kDeckCount &&
            measured.check.roots ==
                bundle::kDeckCount &&
            measured.fit.options +
                    measured.check.options ==
                diagnostic::kBackgroundOptions &&
            !measured.material_green_or_white &&
            !measured.green_white_exceeds_blue_ru &&
            !measured.hypothesis_supported,
        "parent control count or verdict drifted");
    for (const diagnostic::SplitMetrics* split :
         {&measured.fit, &measured.check}) {
        expect(
            split->deck_balanced_parent_to_candidate_kl ==
                    0.0 &&
                split->deck_balanced_total_variation ==
                    0.0 &&
                split->exact_support_changes == 0 &&
                split->maximum_combined_score_delta ==
                    0.0,
            "parent control aggregate is not exact zero");
        for (std::size_t deck_index = 0;
             deck_index < split->decks.size();
             ++deck_index) {
            const auto& deck =
                split->decks[deck_index];
            expect(
                deck.roots == 1 &&
                    deck.options ==
                        diagnostic::
                            kBackgroundOptionsPerDeck[
                                deck_index] &&
                    deck.parent_to_candidate_kl ==
                        0.0 &&
                    deck.total_variation == 0.0 &&
                    deck.exact_support_changes == 0 &&
                    deck.maximum_combined_score_delta ==
                        0.0,
                "parent control deck metric is not exact zero");
        }
    }
}

void test_green_white_drift_matches_analytic_math() {
    const evaluator::PreparedCorpus source =
        corpus();
    const evaluator::CorpusLogits parent =
        zero_logits(source);
    const diagnostic::Measurements measured =
        diagnostic::measure(
            source, parent,
            green_white_drift(source));
    const double residual =
        evaluator::kResidualWeight *
        std::tanh(1.0);
    const double parent_pass =
        mixed_probability(0.50, 0.55);
    const double candidate_pass =
        mixed_probability(
            0.50 + residual,
            0.55 - residual);
    const double expected_kl =
        binary_kl(
            parent_pass,
            candidate_pass);
    const double expected_tv =
        std::abs(
            parent_pass -
            candidate_pass);
    for (const diagnostic::SplitMetrics* split :
         {&measured.fit, &measured.check}) {
        for (const old_school::DeckId id :
             {old_school::DeckId::Green,
              old_school::DeckId::White}) {
            const auto& deck =
                split->decks[
                    static_cast<std::size_t>(id)];
            expect_close(
                deck.parent_to_candidate_kl,
                expected_kl, 1.0e-12,
                "background KL differs from analytic oracle");
            expect_close(
                deck.total_variation,
                expected_tv, 1.0e-12,
                "background TV differs from analytic oracle");
            expect(
                deck.exact_support_changes == 1,
                "background support change was not counted");
            expect_close(
                deck.maximum_combined_score_delta,
                residual, 1.0e-15,
                "background score delta drifted");
        }
    }
    expect(
        expected_kl >= diagnostic::kMaterialKl &&
            measured.material_green_or_white &&
            measured.green_white_exceeds_blue_ru &&
            measured.hypothesis_supported &&
            measured.green_white_mean_kl ==
                expected_kl &&
            measured.blue_ru_mean_kl == 0.0,
        "directional background hypothesis drifted");
}

void test_shape_anchor_and_finite_fail_closed() {
    evaluator::PreparedCorpus source =
        corpus();
    evaluator::CorpusLogits parent =
        zero_logits(source);
    evaluator::CorpusLogits candidate =
        green_white_drift(source);

    evaluator::PreparedCorpus missing = source;
    evaluator::CorpusLogits missing_parent = parent;
    evaluator::CorpusLogits missing_candidate =
        candidate;
    missing.check.pop_back();
    missing_parent.check.pop_back();
    missing_candidate.check.pop_back();
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure(
                    missing, missing_parent,
                    missing_candidate));
        },
        "missing background deck passed");

    evaluator::PreparedCorpus extra = source;
    evaluator::CorpusLogits extra_parent = parent;
    evaluator::CorpusLogits extra_candidate =
        candidate;
    extra.fit.push_back(extra.fit.back());
    extra_parent.fit.push_back(
        extra_parent.fit.back());
    extra_candidate.fit.push_back(
        extra_candidate.fit.back());
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure(
                    extra, extra_parent,
                    extra_candidate));
        },
        "extra background deck passed");

    evaluator::PreparedCorpus bad_owner = source;
    bad_owner.fit.back().owner_deck =
        bundle::kDeckCount;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure(
                    bad_owner, parent,
                    candidate));
        },
        "out-of-range background deck passed");

    for (std::size_t index = 0;
         index < source.fit.size(); ++index) {
        if (background(source.fit[index])) {
            parent.fit[index][0] = 0.1;
            break;
        }
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure(
                    source, parent, candidate));
        },
        "parent background anchor drift passed");

    parent = zero_logits(source);
    for (std::size_t index = 0;
         index < source.check.size(); ++index) {
        if (background(source.check[index])) {
            candidate.check[index][0] =
                std::numeric_limits<double>::infinity();
            break;
        }
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure(
                    source, parent, candidate));
        },
        "nonfinite candidate background logit passed");
}

void test_format_is_aggregate_and_fail_closed() {
    const evaluator::PreparedCorpus source =
        corpus();
    const diagnostic::Measurements measured =
        diagnostic::measure(
            source, zero_logits(source),
            green_white_drift(source));
    const diagnostic::Measurements parent_control =
        diagnostic::measure(
            source, zero_logits(source),
            zero_logits(source));
    const diagnostic::Report report =
        report_for(
            parent_control, measured);
    const std::string output =
        diagnostic::format_report(report);
    expect(
        occurrence_count(
            output,
            "background split=") == 12 &&
            output.find(
                "hypothesis material_green_or_white=1") !=
                std::string::npos &&
            output.find(
                "parent_control_exact_zero=1") !=
                std::string::npos &&
            output.find(" supported=1") !=
                std::string::npos &&
            output.find("PRIVATE_") ==
                std::string::npos &&
            output.find("descriptor") ==
                std::string::npos &&
            output.find("production_seed") ==
                std::string::npos &&
            output.ends_with("result=SUPPORTED\n"),
        "background report leaked rows or lost aggregates");

    diagnostic::Report malformed = report;
    --malformed.measurements.fit.options;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::format_report(
                    malformed));
        },
        "malformed background option cross-sum passed");
    malformed = report;
    malformed.candidate_exact = false;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::format_report(
                    malformed));
        },
        "candidate identity drift passed");
    malformed = report;
    malformed.measurements
        .green_white_exceeds_blue_ru = false;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::format_report(
                    malformed));
        },
        "hypothesis recomputation drift passed");
    malformed = report;
    malformed.measurements
        .material_green_or_white = false;
    malformed.measurements
        .hypothesis_supported = false;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::format_report(
                    malformed));
        },
        "forged material-clause verdict passed");
    malformed = report;
    malformed.parent_control.fit.decks[0]
        .total_variation = 0.01;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::format_report(
                    malformed));
        },
        "nonzero parent control passed");
    malformed = report;
    malformed.measurements.fit
        .deck_balanced_total_variation += 0.01;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::format_report(
                    malformed));
        },
        "forged deck-balanced aggregate passed");

    diagnostic::ParentControlReport control_report{
        .parent_fingerprint =
            std::string(
                bundle::kParentModelFingerprint),
        .parent_components =
            parent_components(),
        .measurements = parent_control,
        .parent_immutable = true,
    };
    const std::string control_output =
        diagnostic::format_parent_control_report(
            control_report);
    expect(
        control_output.find(
            "mode=parent-control") !=
                std::string::npos &&
            control_output.find(
                "parent_control_exact_zero=1") !=
                std::string::npos &&
            control_output.ends_with("result=PASS\n"),
        "parent-control report formatting drifted");
    control_report.measurements.check.decks[0]
        .maximum_combined_score_delta = 0.01;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::
                    format_parent_control_report(
                        control_report));
        },
        "nonzero formatted parent control passed");
}

void test_stack_census_empty_active_and_encoding() {
    std::vector<bundle::SelectedRow> fit;
    fit.push_back(
        make_stack_row(
            bundle::Split::Fit, 0,
            bundle::Role::DominancePositive,
            2, 0));
    fit.push_back(
        make_stack_row(
            bundle::Split::Fit, 2,
            bundle::Role::DominancePositive,
            3, 2));
    std::vector<bundle::SelectedRow> check;
    check.push_back(
        make_stack_row(
            bundle::Split::Check, 3,
            bundle::Role::BackgroundControl,
            2, 0));

    const diagnostic::StackCensus measured =
        diagnostic::measure_stack_census(
            fit, check);
    expect(
        measured.selected_rows == 3 &&
            measured.selected_options == 7 &&
            measured.action_invariant_rows == 3 &&
            measured.exact_stack_encoding_rows == 3 &&
            measured.role_overlap_rows == 0,
        "synthetic stack census accounting drifted");
    expect(
        measured.fit.positive.decks[0]
                .empty ==
            diagnostic::StackContextCount{
                .roots = 1,
                .options = 2,
            } &&
        measured.fit.positive.decks[2]
                .active ==
            diagnostic::StackContextCount{
                .roots = 1,
                .options = 3,
            } &&
        measured.check.background.decks[3]
                .empty ==
            diagnostic::StackContextCount{
                .roots = 1,
                .options = 2,
            },
        "empty/active stack cells were misclassified");
}

void test_stack_census_rejects_malformed_rows() {
    const auto valid_row =
        make_stack_row(
            bundle::Split::Fit, 2,
            bundle::Role::DominancePositive,
            2, 1);

    auto variant = valid_row;
    variant.actions[1].features[0].value_bits =
        bits(0.4);
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "action-varying stack feature passed");

    variant = valid_row;
    for (auto& action : variant.actions) {
        action.features[0].value_bits =
            bits(0.3);
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "fractional encoded stack size passed");

    for (const double invalid :
         {std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(),
          -0.2}) {
        variant = valid_row;
        for (auto& action : variant.actions) {
            action.features[0].value_bits =
                bits(invalid);
        }
        expect_rejected(
            [&] {
                static_cast<void>(
                    diagnostic::measure_stack_census(
                        {variant}, {}));
            },
            "nonfinite or negative stack size passed");
    }

    variant = valid_row;
    const double first_unrepresentable_size =
        std::ldexp(
            1.0,
            std::numeric_limits<std::size_t>::digits);
    const double overflow_feature =
        first_unrepresentable_size /
        static_cast<double>(
            diagnostic::
                kStackSizeEncodingDenominator);
    expect(
        std::isfinite(overflow_feature),
        "overflow guard fixture is not finite");
    for (auto& action : variant.actions) {
        action.features[0].value_bits =
            bits(overflow_feature);
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "finite size_t-overflow stack size passed");

    variant = valid_row;
    for (auto& action : variant.actions) {
        action.features[0].value_bits =
            bits(0.0);
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "explicit sparse positive zero passed");

    variant = valid_row;
    for (auto& action : variant.actions) {
        action.features[0].value_bits =
            bits(-0.0);
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "negative-zero stack feature passed");

    variant = valid_row;
    for (auto& action : variant.actions) {
        action.features.push_back(
            action.features.front());
    }
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "duplicate stack feature passed");

    variant = valid_row;
    variant.roles =
        static_cast<std::uint8_t>(
            bundle::Role::DominancePositive) |
        static_cast<std::uint8_t>(
            bundle::Role::BackgroundControl);
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "overlapping selected-row roles passed");

    variant = valid_row;
    variant.split = bundle::Split::Check;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "wrong-split selected row passed");

    variant = valid_row;
    variant.census.owner_deck =
        bundle::kDeckCount;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "out-of-range selected-row deck passed");

    for (const std::uint8_t invalid_role :
         {std::uint8_t{0},
          std::uint8_t{1U << 7U}}) {
        variant = valid_row;
        variant.roles = invalid_role;
        expect_rejected(
            [&] {
                static_cast<void>(
                    diagnostic::measure_stack_census(
                        {variant}, {}));
            },
            "zero or unknown selected-row role passed");
    }

    variant = valid_row;
    variant.actions.clear();
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::measure_stack_census(
                    {variant}, {}));
        },
        "empty selected row passed");
}

void test_stack_census_format_is_exact_and_aggregate_only() {
    auto [fit, check] = frozen_stack_rows();
    const diagnostic::StackCensus measured =
        diagnostic::measure_stack_census(
            fit, check);
    expect(
        measured.selected_rows ==
                diagnostic::kStackCensusSelectedRows &&
            measured.selected_options ==
                diagnostic::kStackCensusSelectedOptions,
        "frozen synthetic stack totals drifted");
    const diagnostic::StackCensusReport report =
        stack_report_for(measured);
    const std::string output =
        diagnostic::format_stack_census_report(
            report);
    expect(
        occurrence_count(
            output,
            "stack_census split=") == 24 &&
            output.find(
                "stack_census split=fit role=positive "
                "aggregate=pooled empty_roots=70 "
                "empty_options=488 active_roots=18 "
                "active_options=60") !=
                std::string::npos &&
            output.find(
                "stack_census split=check role=positive "
                "aggregate=pooled empty_roots=77 "
                "empty_options=520 active_roots=17 "
                "active_options=51") !=
                std::string::npos &&
            output.find(
                "stack_size_feature_index=20 "
                "encoding=stack_size_over_5") !=
                std::string::npos &&
            output.find("models_loaded=0 fits=0 games=0") !=
                std::string::npos &&
            output.find("PRIVATE_STACK_") ==
                std::string::npos &&
            output.find("descriptor") ==
                std::string::npos &&
            output.find("production_seed") ==
                std::string::npos &&
            output.ends_with("result=PASS\n"),
        "stack census report leaked rows or lost aggregates");

    diagnostic::StackCensusReport malformed = report;
    constexpr std::size_t kBlue =
        static_cast<std::size_t>(
            old_school::DeckId::Blue);
    --malformed.census.fit.positive.decks[kBlue]
          .active.roots;
    malformed.census.fit.positive.decks[kBlue]
        .active.options -= 3;
    ++malformed.census.fit.positive.decks[kBlue]
          .empty.roots;
    malformed.census.fit.positive.decks[kBlue]
        .empty.options += 3;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::
                    format_stack_census_report(
                        malformed));
        },
        "self-consistent frozen count drift passed");

    malformed = report;
    malformed.census.role_overlap_rows = 1;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::
                    format_stack_census_report(
                        malformed));
        },
        "nonzero role-overlap accounting passed");

    malformed = report;
    malformed.stack_size_feature_index = 21;
    expect_rejected(
        [&] {
            static_cast<void>(
                diagnostic::
                    format_stack_census_report(
                        malformed));
        },
        "stack feature-contract index drift passed");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "parent control exact zero",
        test_parent_control_is_exact_zero);
    runner.run(
        "Green White analytic drift",
        test_green_white_drift_matches_analytic_math);
    runner.run(
        "shape anchor and finite guards",
        test_shape_anchor_and_finite_fail_closed);
    runner.run(
        "aggregate-only format",
        test_format_is_aggregate_and_fail_closed);
    runner.run(
        "stack census empty active encoding",
        test_stack_census_empty_active_and_encoding);
    runner.run(
        "stack census malformed rows",
        test_stack_census_rejects_malformed_rows);
    runner.run(
        "stack census exact aggregate format",
        test_stack_census_format_is_exact_and_aggregate_only);
    return runner.finish();
}
