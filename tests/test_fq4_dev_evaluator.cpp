#include "old_school/fq4_dev_bundle.hpp"
#include "old_school/fq4_dev_evaluator.hpp"

#include <algorithm>
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
        throw std::runtime_error(
            std::string(message) + ": actual=" +
            std::to_string(actual) + " expected=" +
            std::to_string(expected));
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

std::string_view line_with(
    const std::string& output,
    std::string_view prefix) {
    const std::size_t begin =
        output.find(prefix);
    if (begin == std::string::npos) {
        throw std::runtime_error(
            "formatted line is missing");
    }
    const std::size_t end =
        output.find('\n', begin);
    return std::string_view(output).substr(
        begin,
        end == std::string::npos
            ? std::string::npos
            : end - begin);
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

std::uint64_t bits(double value) {
    return std::bit_cast<std::uint64_t>(value);
}

double from_bits(std::uint64_t value) {
    return std::bit_cast<double>(value);
}

bundle::SelectedRow make_row(
    bundle::Split split, std::size_t deck,
    bool positive, bool signed_zero = false) {
    const double pass_base =
        signed_zero ? 0.0 : 0.50;
    const double action_base =
        signed_zero ? -0.0 : 0.55;
    const bundle::DominanceCount pass_dominance{
        .complete = 0,
        .strict = 0,
    };
    const bundle::DominanceCount action_dominance{
        .complete =
            static_cast<std::uint8_t>(
                bundle::kWorldCount),
        .strict =
            static_cast<std::uint8_t>(
                positive
                    ? bundle::kWorldCount
                    : bundle::kWorldCount - 1),
    };

    bundle::SelectedRow result{
        .split = split,
        .census = {
            .trace_ordinal =
                static_cast<std::uint32_t>(
                    1000 +
                    (split == bundle::Split::Check
                         ? 100
                         : 0) +
                    deck),
            .owner_deck =
                static_cast<std::uint8_t>(deck),
            .opponent_deck =
                static_cast<std::uint8_t>(
                    (deck + 1) %
                    bundle::kDeckCount),
            .pass_index = 0,
            .dominance = {
                pass_dominance,
                action_dominance,
            },
        },
        .roles = static_cast<std::uint8_t>(
            positive
                ? bundle::Role::DominancePositive
                : bundle::Role::BackgroundControl),
        .production_seed =
            0xfeed0000ULL + deck,
    };

    const std::array<double, 2> base{
        pass_base,
        action_base,
    };
    for (std::size_t action = 0;
         action < base.size(); ++action) {
        bundle::ActionRow row{
            .descriptor =
                "PRIVATE_DESCRIPTOR_" +
                std::to_string(deck) + "_" +
                std::to_string(action),
            .is_pass = action == 0,
            .dominance =
                action == 0
                    ? pass_dominance
                    : action_dominance,
            .base_score_bits = bits(base[action]),
            .parent_residual_bits =
                bits(
                    signed_zero && action == 1
                        ? -0.0
                        : 0.0),
            .features = {
                {
                    .index =
                        static_cast<std::uint16_t>(
                            action),
                    .value_bits =
                        bits(
                            action == 0
                                ? 1.0
                                : -2.0),
                },
                {
                    .index = 892,
                    .value_bits = bits(-0.0),
                },
            },
        };
        for (std::size_t world = 0;
             world < bundle::kWorldCount; ++world) {
            row.raw_sample_bits[world] =
                bits(base[action]);
            row.shallow_prior_sample_bits[world] =
                bits(base[action]);
            row.continuation_sample_bits[world] =
                bits(base[action]);
        }
        result.actions.push_back(std::move(row));
    }
    return result;
}

std::vector<bundle::SelectedRow> positive_rows(
    bundle::Split split, bool signed_zero = false) {
    std::vector<bundle::SelectedRow> result;
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        result.push_back(
            make_row(split, deck, true, signed_zero));
    }
    return result;
}

evaluator::PreparedCorpus regular_corpus() {
    auto fit = positive_rows(bundle::Split::Fit);
    fit.push_back(
        make_row(
            bundle::Split::Fit, 0, false));
    return evaluator::testing::prepare_selected_rows(
        fit,
        positive_rows(bundle::Split::Check));
}

evaluator::CorpusLogits logits_for(
    const evaluator::PreparedCorpus& corpus,
    bool candidate) {
    evaluator::CorpusLogits result;
    const auto add =
        [candidate](
            const std::vector<evaluator::PreparedRow>& rows,
            std::vector<std::vector<double>>& target) {
            for (const auto& row : rows) {
                expect(
                    row.actions.size() == 2,
                    "synthetic logit fixture drifted");
                const bool signed_zero =
                    bits(row.actions[1].base_score) ==
                    bits(-0.0);
                target.push_back(
                    candidate
                        ? std::vector<double>{0.3, -0.3}
                        : std::vector<double>{
                              0.0,
                              signed_zero ? -0.0 : 0.0});
            }
        };
    add(corpus.fit, result.fit);
    add(corpus.check, result.check);
    return result;
}

evaluator::PreparedCorpus transition_corpus(
    double pass_base, double action_base) {
    evaluator::PreparedCorpus corpus =
        evaluator::testing::prepare_selected_rows(
            positive_rows(bundle::Split::Fit),
            positive_rows(bundle::Split::Check));
    const auto prepare_split =
        [pass_base, action_base](
            std::vector<evaluator::PreparedRow>& rows) {
            for (auto& row : rows) {
                row.actions[0].base_score =
                    pass_base;
                row.actions[1].base_score =
                    action_base;
                for (std::size_t world = 0;
                     world < bundle::kWorldCount;
                     ++world) {
                    row.actions[0].raw_samples[world] =
                        0.0;
                    row.actions[1].raw_samples[world] =
                        world % 2 == 0
                            ? 0.04
                            : -0.04;
                }
            }
        };
    prepare_split(corpus.fit);
    prepare_split(corpus.check);
    return corpus;
}

evaluator::CorpusLogits constant_logits(
    const evaluator::PreparedCorpus& corpus,
    double pass, double action) {
    evaluator::CorpusLogits result;
    result.fit.assign(
        corpus.fit.size(), {pass, action});
    result.check.assign(
        corpus.check.size(), {pass, action});
    return result;
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) {
    constexpr std::uint64_t kPrime =
        1099511628211ULL;
    for (std::size_t byte = 0; byte < 8; ++byte) {
        hash ^=
            (value >> (byte * 8U)) & 0xffU;
        hash *= kPrime;
    }
}

std::string boundary_token(
    const std::vector<
        old_school::LearnedValuePriorityTrainingExample>&
        examples,
    const old_school::LearnedValuePriorityHeadUpdateConfig&
        config) {
    expect(
        config == evaluator::kOptimizer,
        "FIT boundary changed the frozen optimizer");
    std::uint64_t hash = 1469598103934665603ULL;
    hash_u64(hash, examples.size());
    for (const auto& example : examples) {
        hash_u64(hash, example.options.size());
        for (const auto& option : example.options) {
            hash_u64(hash, option.size());
            for (const double value : option) {
                hash_u64(hash, bits(value));
            }
        }
        hash_u64(hash, example.base_scores.size());
        for (const double value : example.base_scores) {
            hash_u64(hash, bits(value));
        }
        hash_u64(
            hash,
            example.target_probabilities.size());
        for (const double value :
             example.target_probabilities) {
            hash_u64(hash, bits(value));
        }
        hash_u64(hash, bits(example.weight));
    }
    return std::to_string(hash);
}

double probability_for_score_pair(
    double first, double second) {
    const double first_weight =
        std::exp(
            (first - std::max(first, second)) /
            evaluator::kPolicyTemperature);
    const double second_weight =
        std::exp(
            (second - std::max(first, second)) /
            evaluator::kPolicyTemperature);
    return first_weight /
           (first_weight + second_weight);
}

double mixed_probability(double raw) {
    return
        evaluator::kBehaviorPrimaryWeight * raw +
        (1.0 -
         evaluator::kBehaviorPrimaryWeight) /
            2.0;
}

double binary_kl(
    double target_first, double behavior_first) {
    return
        target_first *
            std::log(
                target_first / behavior_first) +
        (1.0 - target_first) *
            std::log(
                (1.0 - target_first) /
                (1.0 - behavior_first));
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

evaluator::ModelEvaluationReport report_for(
    const evaluator::EvaluationMetrics& metrics,
    bool candidate_is_parent) {
    const auto parent = parent_components();
    auto candidate = parent;
    if (!candidate_is_parent) {
        candidate.priority = std::string(64, 'd');
    }
    return {
        .parent_fingerprint =
            std::string(
                bundle::kParentModelFingerprint),
        .candidate_fingerprint =
            candidate_is_parent
                ? std::string(
                      bundle::kParentModelFingerprint)
                : std::string(64, 'c'),
        .parent_components = parent,
        .candidate_components = candidate,
        .parent_immutable = true,
        .nonpriority_components_identical = true,
        .metrics = metrics,
    };
}

void test_prepare_expands_sparse_features() {
    const evaluator::PreparedCorpus corpus =
        regular_corpus();
    expect(
        corpus.fit.size() == 6 &&
            corpus.check.size() == 5,
        "five-deck FIT/CHECK fixture shape drifted");
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        expect(
            corpus.fit[deck].owner_deck == deck &&
                corpus.check[deck].owner_deck == deck &&
                corpus.fit[deck].dominance_positive() &&
                corpus.check[deck].dominance_positive(),
            "prepared five-deck coverage drifted");
    }
    expect(
        !corpus.fit.back().dominance_positive(),
        "background-only FIT row became positive");

    const auto& pass = corpus.fit[0].actions[0].features;
    const auto& action =
        corpus.fit[0].actions[1].features;
    expect(
        pass.size() == bundle::kFeatureCount &&
            action.size() == bundle::kFeatureCount,
        "sparse features did not expand to 893");
    expect(
        pass[0] == 1.0 && pass[1] == 0.0 &&
            action[0] == 0.0 && action[1] == -2.0,
        "expanded sparse values drifted");
    expect(
        bits(pass[892]) == bits(-0.0) &&
            bits(action[892]) == bits(-0.0),
        "negative-zero sparse values lost their bits");

    auto malformed = positive_rows(bundle::Split::Fit);
    malformed[0].actions[0].features[0].value_bits = 0;
    expect_rejected(
        [&] {
            (void)evaluator::testing::
                prepare_selected_rows(
                    malformed,
                    positive_rows(
                        bundle::Split::Check));
        },
        "noncanonical positive-zero sparse feature passed");
}

void test_fit_boundary_is_fit_positive_only() {
    const evaluator::PreparedCorpus corpus =
        regular_corpus();
    const auto examples =
        evaluator::fit_examples(corpus);
    expect(
        examples.size() == bundle::kDeckCount,
        "FIT boundary admitted CHECK/background rows");
    for (const auto& example : examples) {
        expect(
            example.options.size() == 2 &&
                example.options[0].size() ==
                    bundle::kFeatureCount &&
                example.base_scores ==
                    std::vector<double>{0.50, 0.55} &&
                example.target_probabilities.size() == 2 &&
                example.weight == 1.0,
            "FIT training example drifted");
        const double target_total =
            std::accumulate(
                example.target_probabilities.begin(),
                example.target_probabilities.end(),
                0.0);
        expect_close(
            target_total, 1.0, 1.0e-12,
            "FIT target is not normalized");
        expect(
            example.target_probabilities[0] >
                example.target_probabilities[1],
            "FIT target did not repair Pass dominance");
    }

    const std::string baseline =
        evaluator::testing::invoke_fit_boundary_token(
            corpus, boundary_token);
    evaluator::PreparedCorpus changed_check = corpus;
    changed_check.check[0]
        .actions[0].features[0] += 99.0;
    evaluator::PreparedCorpus changed_background =
        corpus;
    changed_background.fit.back()
        .actions[0].features[0] += 99.0;
    evaluator::PreparedCorpus changed_fit = corpus;
    changed_fit.fit[0]
        .actions[0].features[0] += 99.0;
    expect(
        evaluator::testing::invoke_fit_boundary_token(
            changed_check, boundary_token) == baseline,
        "CHECK mutation crossed the FIT boundary");
    expect(
        evaluator::testing::invoke_fit_boundary_token(
            changed_background, boundary_token) ==
            baseline,
        "background-only mutation crossed the FIT boundary");
    expect(
        evaluator::testing::invoke_fit_boundary_token(
            changed_fit, boundary_token) != baseline,
        "FIT-positive mutation did not reach the update boundary");
}

void test_metrics_repair_class1_and_match_exact_math() {
    const evaluator::PreparedCorpus corpus =
        regular_corpus();
    const evaluator::CorpusLogits parent =
        logits_for(corpus, false);
    const evaluator::CorpusLogits candidate =
        logits_for(corpus, true);
    const evaluator::EvaluationMetrics metrics =
        evaluator::evaluate_logits(
            corpus, parent, candidate);

    expect(
        metrics.parent_anchor_rows == 11 &&
            metrics.parent_anchor_actions == 22 &&
            metrics.parent_anchors_exact &&
            metrics.accounting.zero(),
        "anchor or zero-game accounting drifted");

    const double residual =
        evaluator::kResidualWeight *
        std::tanh(0.3);
    const double expected_parent_margin = -0.05;
    const double expected_candidate_margin =
        -0.05 + 2.0 * residual;
    const double projected_pass =
        evaluator::kProjectionRatio /
        (1.0 + evaluator::kProjectionRatio);
    const double target_pass =
        mixed_probability(projected_pass);
    const double parent_pass =
        mixed_probability(
            probability_for_score_pair(
                0.50, 0.55));
    const double candidate_pass =
        mixed_probability(
            probability_for_score_pair(
                0.50 + residual,
                0.55 - residual));
    const double expected_parent_kl =
        binary_kl(target_pass, parent_pass);
    const double expected_candidate_kl =
        binary_kl(target_pass, candidate_pass);

    for (const evaluator::SplitMetrics* split :
         {&metrics.fit, &metrics.check}) {
        expect(
            split->positive_roots ==
                    bundle::kDeckCount &&
                split->positive_options ==
                    2 * bundle::kDeckCount &&
                split->repairs ==
                    bundle::kDeckCount &&
                split->regressions == 0 &&
                split->parent_classes.values ==
                    std::array<std::size_t, 4>{
                        0, bundle::kDeckCount, 0, 0} &&
                split->candidate_classes.values ==
                    std::array<std::size_t, 4>{
                        bundle::kDeckCount, 0, 0, 0} &&
                split->transitions[1][0] ==
                    bundle::kDeckCount,
            "aggregate C1-to-Safe repair counts drifted");
        expect(
            split->parent_support_violations
                    .violating_roots ==
                    bundle::kDeckCount &&
                split->candidate_support_violations
                    .violating_roots == 0,
            "aggregate exact-support counts drifted");
        expect_close(
            split->deck_balanced_target_to_parent_kl,
            expected_parent_kl, 1.0e-12,
            "deck-balanced parent KL drifted");
        expect_close(
            split->deck_balanced_target_to_candidate_kl,
            expected_candidate_kl, 1.0e-12,
            "deck-balanced candidate KL drifted");
        expect_close(
            split->deck_balanced_parent_mean_margin,
            expected_parent_margin, 1.0e-15,
            "deck-balanced parent margin drifted");
        expect_close(
            split->deck_balanced_candidate_mean_margin,
            expected_candidate_margin, 1.0e-15,
            "deck-balanced candidate margin drifted");
        expect(
            split->deck_balanced_target_to_candidate_kl <
                split->deck_balanced_target_to_parent_kl,
            "synthetic repair did not improve KL");

        for (const auto& deck : split->decks) {
            expect(
                deck.positive_roots == 1 &&
                    deck.positive_options == 2 &&
                    deck.parent_margins.roots == 1 &&
                    deck.parent_margins.constraints == 1 &&
                    deck.candidate_margins.roots == 1 &&
                    deck.candidate_margins.constraints == 1 &&
                    deck.parent_classes.values[1] == 1 &&
                    deck.candidate_classes.values[0] == 1 &&
                    deck.transitions[1][0] == 1 &&
                    deck.repairs == 1 &&
                    deck.regressions == 0,
                "per-deck C1-to-Safe metrics drifted");
            expect_close(
                deck.target_to_parent_kl,
                expected_parent_kl, 1.0e-12,
                "per-deck parent KL drifted");
            expect_close(
                deck.target_to_candidate_kl,
                expected_candidate_kl, 1.0e-12,
                "per-deck candidate KL drifted");
            expect_close(
                deck.parent_margins.minimum_margin,
                expected_parent_margin, 1.0e-15,
                "per-deck parent minimum margin drifted");
            expect_close(
                deck.candidate_margins.minimum_margin,
                expected_candidate_margin, 1.0e-15,
                "per-deck candidate minimum margin drifted");
        }
    }
}

void test_anchor_shape_and_nonfinite_inputs_fail_closed() {
    const evaluator::PreparedCorpus corpus =
        regular_corpus();
    const evaluator::CorpusLogits parent =
        logits_for(corpus, false);
    const evaluator::CorpusLogits candidate =
        logits_for(corpus, true);

    evaluator::CorpusLogits anchor_drift = parent;
    anchor_drift.fit[0][0] = 0.01;
    expect_rejected(
        [&] {
            (void)evaluator::evaluate_logits(
                corpus, anchor_drift, candidate);
        },
        "parent residual anchor mismatch passed");

    evaluator::CorpusLogits corpus_shape = candidate;
    corpus_shape.check.pop_back();
    expect_rejected(
        [&] {
            (void)evaluator::evaluate_logits(
                corpus, parent, corpus_shape);
        },
        "candidate corpus shape mismatch passed");

    evaluator::CorpusLogits action_shape = candidate;
    action_shape.fit[0].pop_back();
    expect_rejected(
        [&] {
            (void)evaluator::evaluate_logits(
                corpus, parent, action_shape);
        },
        "candidate action shape mismatch passed");

    evaluator::CorpusLogits nonfinite = candidate;
    nonfinite.check[0][0] =
        std::numeric_limits<double>::infinity();
    expect_rejected(
        [&] {
            (void)evaluator::evaluate_logits(
                corpus, parent, nonfinite);
        },
        "nonfinite candidate logit passed");
}

void test_nonordinal_severity_and_repair_transitions() {
    const auto verify =
        [](double pass_base, double action_base,
           double candidate_pass,
           double candidate_action,
           std::size_t parent_class,
           std::size_t candidate_class,
           std::size_t repairs,
           std::size_t regressions) {
            const evaluator::PreparedCorpus corpus =
                transition_corpus(
                    pass_base, action_base);
            const evaluator::EvaluationMetrics metrics =
                evaluator::evaluate_logits(
                    corpus,
                    constant_logits(
                        corpus, 0.0, 0.0),
                    constant_logits(
                        corpus, candidate_pass,
                        candidate_action));
            for (const evaluator::SplitMetrics* split :
                 {&metrics.fit, &metrics.check}) {
                expect(
                    split->parent_classes
                            .values[parent_class] ==
                            bundle::kDeckCount &&
                        split->candidate_classes
                            .values[candidate_class] ==
                            bundle::kDeckCount &&
                        split->transitions
                            [parent_class]
                            [candidate_class] ==
                            bundle::kDeckCount &&
                        split->repairs == repairs &&
                        split->regressions ==
                            regressions,
                    "nonordinal class severity transition drifted");
            }
        };

    // Safe=0 -> Class3=1 is a regression even though Class3's enum index is
    // larger than Class1 and Class2.
    verify(
        0.55, 0.50, -0.3, 0.3,
        0, 3, 0, bundle::kDeckCount);
    // Class3=1 -> Class2=2 is also a regression.
    verify(
        0.50, 0.51, -0.3, 0.3,
        3, 2, 0, bundle::kDeckCount);
    // Class2=2 -> Safe=0 is a repair, not a regression.
    verify(
        0.50, 0.55, 0.5, -0.5,
        2, 0, bundle::kDeckCount, 0);
}

void test_exact_support_distinguishes_positive_negative_zero() {
    const evaluator::PreparedCorpus corpus =
        evaluator::testing::prepare_selected_rows(
            positive_rows(
                bundle::Split::Fit, true),
            positive_rows(
                bundle::Split::Check, true));
    const evaluator::CorpusLogits parent =
        logits_for(corpus, false);
    const evaluator::EvaluationMetrics metrics =
        evaluator::evaluate_logits(
            corpus, parent, parent);

    expect(
        bits(corpus.fit[0].actions[0].base_score) ==
                bits(0.0) &&
            bits(corpus.fit[0].actions[1].base_score) ==
                bits(-0.0) &&
            bits(corpus.fit[0].actions[0]
                     .parent_residual) ==
                bits(0.0) &&
            bits(corpus.fit[0].actions[1]
                     .parent_residual) ==
                bits(-0.0),
        "signed-zero fixture lost its binary64 anchors");
    for (const evaluator::SplitMetrics* split :
         {&metrics.fit, &metrics.check}) {
        expect(
            split->parent_support_violations
                    .violating_roots == 0 &&
                split->candidate_support_violations
                    .violating_roots == 0 &&
                split->parent_support_violations
                    .positive_roots ==
                    bundle::kDeckCount &&
                split->candidate_support_violations
                    .positive_roots ==
                    bundle::kDeckCount,
            "numeric +0/-0 tie was used instead of bit-exact support");
    }
}

void test_census_and_offline_accounting() {
    bundle::Bundle source;
    source.fit_rows =
        positive_rows(bundle::Split::Fit);
    source.fit_rows.push_back(
        make_row(
            bundle::Split::Fit, 0, false));
    source.check_rows =
        positive_rows(bundle::Split::Check);
    const evaluator::ConstraintCensus census =
        evaluator::constraint_census(source);
    expect(
        census.positive_rows ==
                2 * bundle::kDeckCount &&
            census.maximum_constraints == 1 &&
            census.rows_by_constraint_count[1] ==
                2 * bundle::kDeckCount,
        "constraint census included background or miscounted");
    for (const auto& split :
         census.positive_rows_by_split_deck) {
        expect(
            split ==
                std::array<std::size_t,
                           bundle::kDeckCount>{
                    1, 1, 1, 1, 1},
            "constraint census lost deck balance");
    }
    const std::string formatted =
        evaluator::format_constraint_census(census);
    expect(
        formatted.find("positive_rows=10") !=
                std::string::npos &&
            formatted.find(
                "constraint_count=1 roots=10") !=
                std::string::npos &&
            formatted.find(
                "accounting games=0 determinizations=0") !=
                std::string::npos &&
            formatted.find("PRIVATE_DESCRIPTOR") ==
                std::string::npos &&
            formatted.ends_with("result=PASS\n"),
        "constraint census output or privacy drifted");

    evaluator::OfflineAccounting accounting;
    expect(
        accounting.zero(),
        "default offline accounting is nonzero");
    accounting.rollout_evaluations = 1;
    expect(
        !accounting.zero(),
        "nonzero offline work was reported as zero");
}

void test_formatting_is_aggregate_only_and_fail_closed() {
    const evaluator::PreparedCorpus corpus =
        regular_corpus();
    const evaluator::EvaluationMetrics metrics =
        evaluator::evaluate_logits(
            corpus, logits_for(corpus, false),
            logits_for(corpus, true));

    evaluator::FitAccounting parent_accounting{
        .optimizer = evaluator::kOptimizer,
    };
    const auto parent_report =
        report_for(metrics, true);
    const std::string parent_output =
        evaluator::format_evaluation_report(
            "evaluate-parent", parent_report,
            parent_accounting);
    expect(
        parent_output.find(
            "mode=evaluate-parent") !=
                std::string::npos &&
            parent_output.find(
                "split=fit deck=Green") !=
                std::string::npos &&
            parent_output.find(
                "split=check deck=RU_Aggro") !=
                std::string::npos &&
            parent_output.find(
                "accounting games=0 determinizations=0") !=
                std::string::npos &&
            parent_output.find("PRIVATE_DESCRIPTOR") ==
                std::string::npos &&
            parent_output.find("production_seed") ==
                std::string::npos &&
            parent_output.find("stable_root") ==
                std::string::npos &&
            parent_output.ends_with("result=PASS\n"),
        "evaluation formatter leaked private rows or lost aggregates");
    for (const std::string_view split :
         {"fit", "check"}) {
        const std::string prefix =
            "split=" + std::string(split) +
            " aggregate=deck_balanced";
        expect(
            occurrence_count(
                line_with(parent_output, prefix),
                "kl_parent=") == 1,
            "aggregate line duplicated kl_parent");
    }

    evaluator::FitAccounting fit{
        .fit_examples = bundle::kDeckCount,
        .fit_options = 2 * bundle::kDeckCount,
        .optimizer_calls = 1,
        .training_input_sha256 =
            std::string(64, 'a'),
        .optimizer = evaluator::kOptimizer,
    };
    const auto candidate_report =
        report_for(metrics, false);
    const std::string fit_output =
        evaluator::format_evaluation_report(
            "fit", candidate_report, fit);
    expect(
        fit_output.find("mode=fit") !=
                std::string::npos &&
            fit_output.find("fit_examples=5") !=
                std::string::npos &&
            fit_output.find("fit_options=10") !=
                std::string::npos &&
            fit_output.find("check_examples=0") !=
                std::string::npos &&
            fit_output.find(
                "background_only_examples=0") !=
                std::string::npos,
        "FIT formatter lost boundary accounting");

    auto malformed = fit;
    malformed.check_examples = 1;
    expect_rejected(
        [&] {
            (void)evaluator::format_evaluation_report(
                "fit", candidate_report, malformed);
        },
        "CHECK example accounting passed");
    malformed = fit;
    malformed.background_only_examples = 1;
    expect_rejected(
        [&] {
            (void)evaluator::format_evaluation_report(
                "fit", candidate_report, malformed);
        },
        "background-only example accounting passed");
    malformed = fit;
    --malformed.fit_options;
    expect_rejected(
        [&] {
            (void)evaluator::format_evaluation_report(
                "fit", candidate_report, malformed);
        },
        "FIT option cross-sum mismatch passed");
    malformed = fit;
    malformed.training_input_sha256.pop_back();
    expect_rejected(
        [&] {
            (void)evaluator::format_evaluation_report(
                "fit", candidate_report, malformed);
        },
        "malformed training hash passed");

    auto wrong_parent = parent_report;
    wrong_parent.candidate_fingerprint =
        "not-the-parent";
    expect_rejected(
        [&] {
            (void)evaluator::format_evaluation_report(
                "evaluate-parent", wrong_parent,
                parent_accounting);
        },
        "evaluate-parent accepted candidate identity drift");

    auto nonzero = candidate_report;
    nonzero.metrics.accounting.games = 1;
    expect_rejected(
        [&] {
            (void)evaluator::format_evaluation_report(
                "fit", nonzero, fit);
        },
        "nonzero game accounting passed");
    expect_rejected(
        [&] {
            (void)evaluator::format_evaluation_report(
                "unexpected", candidate_report, fit);
        },
        "unknown evaluator report mode passed");
}

void test_signed_zero_helper_is_exact() {
    expect(
        bits(0.0) != bits(-0.0) &&
            from_bits(bits(-0.0)) == 0.0 &&
            std::signbit(from_bits(bits(-0.0))),
        "test platform does not preserve binary64 signed zero");
}

} // namespace

int main() {
    TestRunner runner;
    runner.run(
        "sparse 893 expansion",
        test_prepare_expands_sparse_features);
    runner.run(
        "FIT-positive-only boundary",
        test_fit_boundary_is_fit_positive_only);
    runner.run(
        "C1-to-Safe exact metrics",
        test_metrics_repair_class1_and_match_exact_math);
    runner.run(
        "anchor shape and finite guards",
        test_anchor_shape_and_nonfinite_inputs_fail_closed);
    runner.run(
        "nonordinal severity and repair transitions",
        test_nonordinal_severity_and_repair_transitions);
    runner.run(
        "binary64 signed-zero support",
        test_exact_support_distinguishes_positive_negative_zero);
    runner.run(
        "constraint census and zero accounting",
        test_census_and_offline_accounting);
    runner.run(
        "aggregate-only formatting guards",
        test_formatting_is_aggregate_only_and_fail_closed);
    runner.run(
        "signed-zero platform fixture",
        test_signed_zero_helper_is_exact);
    return runner.finish();
}
