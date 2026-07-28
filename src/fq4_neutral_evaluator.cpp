#include "old_school/fq4_neutral_evaluator.hpp"

#include "old_school/artifact_integrity.hpp"
#include "old_school/fq4_priority_math.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace old_school::fq4_neutral_evaluator {
namespace {

namespace bundle = fq4_dev_bundle;
namespace dev = fq4_dev_evaluator;
namespace integrity = artifact_integrity;
namespace math = fq4_priority_math;
namespace neutral = fq4_neutral_supplement;

[[noreturn]] void fail(std::string_view message) {
    throw std::runtime_error(
        "FQ4 neutral evaluator: " +
        std::string(message));
}

bool finite(double value) {
    return std::isfinite(value);
}

std::size_t split_index(bundle::Split split) {
    switch (split) {
    case bundle::Split::Fit:
        return 0;
    case bundle::Split::Check:
        return 1;
    }
    fail("invalid split");
}

std::vector<double> dense_features(
    const neutral::NeutralAction& action) {
    std::vector<double> result(
        bundle::kFeatureCount, 0.0);
    std::uint16_t previous = 0;
    bool first = true;
    for (const bundle::SparseFeature feature :
         action.features) {
        if (feature.index >= bundle::kFeatureCount ||
            feature.value_bits == 0 ||
            (!first && feature.index <= previous)) {
            fail("neutral sparse features are noncanonical");
        }
        first = false;
        previous = feature.index;
        const double value =
            std::bit_cast<double>(feature.value_bits);
        if (!finite(value)) {
            fail("neutral sparse feature is nonfinite");
        }
        result[feature.index] = value;
    }
    return result;
}

PreparedNeutralRow prepare_row(
    const neutral::NeutralRow& row) {
    const bundle::Split split =
        row.locator.root.rank.split;
    (void)split_index(split);
    if (row.locator.root.rank.owner_deck >=
            bundle::kDeckCount ||
        row.actions.size() < 2 ||
        row.actions.size() >
            bundle::kMaximumActions ||
        row.pass_index >= row.actions.size()) {
        fail("neutral row has an invalid training shape");
    }
    PreparedNeutralRow result{
        .split = split,
        .owner_deck =
            row.locator.root.rank.owner_deck,
    };
    result.actions.reserve(row.actions.size());
    std::size_t pass_count = 0;
    for (std::size_t index = 0;
         index < row.actions.size(); ++index) {
        const neutral::NeutralAction& action =
            row.actions[index];
        pass_count += action.is_pass ? 1U : 0U;
        if (action.is_pass !=
                (index == row.pass_index) ||
            (index != row.pass_index &&
             action.dominance.complete ==
                 bundle::kWorldCount &&
             action.dominance.strict ==
                 bundle::kWorldCount)) {
            fail("neutral row is not dominance-negative");
        }
        const double base =
            std::bit_cast<double>(
                action.base_score_bits);
        const double residual =
            std::bit_cast<double>(
                action.parent_residual_bits);
        if (!finite(base) || !finite(residual)) {
            fail("neutral score anchor is nonfinite");
        }
        result.actions.push_back({
            .base_score = base,
            .parent_residual = residual,
            .features = dense_features(action),
        });
    }
    if (pass_count != 1) {
        fail("neutral row does not have exactly one Pass");
    }
    return result;
}

PreparedNeutralCorpus prepare_rows_impl(
    std::span<const neutral::NeutralRow> rows) {
    PreparedNeutralCorpus result;
    for (const neutral::NeutralRow& row : rows) {
        PreparedNeutralRow prepared =
            prepare_row(row);
        switch (prepared.split) {
        case bundle::Split::Fit:
            result.fit.push_back(
                std::move(prepared));
            break;
        case bundle::Split::Check:
            result.check.push_back(
                std::move(prepared));
            break;
        }
    }
    return result;
}

std::vector<std::vector<double>> options(
    const PreparedNeutralRow& row) {
    std::vector<std::vector<double>> result;
    result.reserve(row.actions.size());
    for (const PreparedNeutralAction& action :
         row.actions) {
        result.push_back(action.features);
    }
    return result;
}

std::vector<double> base_scores(
    const PreparedNeutralRow& row) {
    std::vector<double> result;
    result.reserve(row.actions.size());
    for (const PreparedNeutralAction& action :
         row.actions) {
        result.push_back(action.base_score);
    }
    return result;
}

std::vector<double> parent_combined_scores(
    const PreparedNeutralRow& row) {
    std::vector<double> result;
    result.reserve(row.actions.size());
    for (const PreparedNeutralAction& action :
         row.actions) {
        const double score =
            action.base_score +
            action.parent_residual;
        if (!finite(score)) {
            fail("neutral parent combined score is nonfinite");
        }
        result.push_back(score);
    }
    return result;
}

void digest_u64(
    integrity::Sha256Accumulator& digest,
    std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0;
         index < bytes.size(); ++index) {
        bytes[index] =
            static_cast<std::byte>(
                value >>
                (8U *
                 (bytes.size() - index - 1U)));
    }
    digest.update(bytes);
}

void digest_double(
    integrity::Sha256Accumulator& digest,
    double value) {
    digest_u64(
        digest,
        std::bit_cast<std::uint64_t>(value));
}

std::string training_input_sha256_impl(
    const std::vector<
        LearnedValuePriorityTrainingExample>& examples,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    integrity::Sha256Accumulator digest;
    digest.update(
        "old-school-fq4-priority-dev-fit-input-v1\n");
    digest_u64(digest, examples.size());
    for (const auto& example : examples) {
        digest_u64(
            digest, example.options.size());
        for (const auto& option : example.options) {
            digest_u64(digest, option.size());
            for (const double value : option) {
                digest_double(digest, value);
            }
        }
        digest_u64(
            digest, example.base_scores.size());
        for (const double value :
             example.base_scores) {
            digest_double(digest, value);
        }
        digest_u64(
            digest,
            example.target_probabilities.size());
        for (const double value :
             example.target_probabilities) {
            digest_double(digest, value);
        }
        digest_double(digest, example.weight);
    }
    digest_u64(digest, optimizer.batch_size);
    digest_u64(digest, optimizer.epochs);
    digest_double(digest, optimizer.learning_rate);
    digest_double(digest, optimizer.beta1);
    digest_double(digest, optimizer.beta2);
    digest_double(digest, optimizer.epsilon);
    digest_double(
        digest,
        optimizer.global_gradient_norm_clip);
    digest_u64(digest, optimizer.seed);
    digest_double(digest, optimizer.residual_weight);
    digest_double(
        digest, optimizer.policy_temperature);
    return digest.finish();
}

TrainingBatch build_batch_impl(
    const std::vector<
        LearnedValuePriorityTrainingExample>&
        positive_examples,
    const PreparedNeutralCorpus& neutral_corpus,
    bool include_neutral) {
    TrainingBatch result;
    result.examples = positive_examples;
    result.positive_examples =
        positive_examples.size();
    for (const auto& example : positive_examples) {
        result.positive_options +=
            example.options.size();
    }
    if (include_neutral) {
        result.examples.reserve(
            positive_examples.size() +
            neutral_corpus.fit.size());
        for (const PreparedNeutralRow& row :
             neutral_corpus.fit) {
            if (row.split != bundle::Split::Fit ||
                row.owner_deck >=
                    bundle::kDeckCount) {
                fail("non-FIT neutral row crossed the update boundary");
            }
            const double weight =
                static_cast<double>(
                    kPositiveFitExamplesByDeck[
                        row.owner_deck]) /
                static_cast<double>(
                    kNeutralRowsPerDeck);
            if (!finite(weight) ||
                weight <= 0.0) {
                fail("neutral row weight is invalid");
            }
            result.examples.push_back({
                .options = options(row),
                .base_scores =
                    base_scores(row),
                .target_probabilities =
                    neutral_behavior_target(row),
                .weight = weight,
            });
            ++result.neutral_examples;
            result.neutral_options +=
                row.actions.size();
            ++result.neutral_examples_by_deck[
                row.owner_deck];
            result.neutral_loss_mass_by_deck[
                row.owner_deck] += weight;
        }
    }
    result.accounting = {
        .fit_examples = result.examples.size(),
        .fit_options =
            result.positive_options +
            result.neutral_options,
        .check_examples = 0,
        .background_only_examples = 0,
        .optimizer_calls = 1,
        .training_input_sha256 =
            training_input_sha256_impl(
                result.examples,
                dev::kOptimizer),
        .optimizer = dev::kOptimizer,
    };
    return result;
}

bool same_nonpriority(
    const LearnedModelComponentFingerprints& first,
    const LearnedModelComponentFingerprints& second) {
    return
        first.critic == second.critic &&
        first.attack == second.attack &&
        first.block == second.block &&
        first.damage_order ==
            second.damage_order;
}

bool exact_positive_deck_counts(
    const dev::SplitMetrics& split,
    const std::array<
        std::size_t, bundle::kDeckCount>& expected) {
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (split.decks[deck].positive_roots !=
            expected[deck]) {
            return false;
        }
    }
    return true;
}

bool exact_positive_only_contract(
    const dev::EvaluationMetrics& metrics) {
    if (!metrics.parent_anchors_exact ||
        !metrics.accounting.zero() ||
        metrics.fit.positive_roots !=
            kPositiveFitExamples ||
        metrics.fit.positive_options !=
            kPositiveFitOptions ||
        metrics.check.positive_roots !=
            kPositiveCheckExamples ||
        metrics.check.positive_options !=
            kPositiveCheckOptions ||
        !exact_positive_deck_counts(
            metrics.fit,
            kPositiveFitExamplesByDeck) ||
        !exact_positive_deck_counts(
            metrics.check,
            kPositiveCheckExamplesByDeck) ||
        metrics.fit.repairs !=
            kRequiredFitRepairs ||
        metrics.fit.regressions != 0 ||
        metrics.fit.candidate_support_violations
                .violating_roots !=
            kRequiredFitSupportViolations ||
        metrics.check.repairs !=
            kRequiredCheckRepairs ||
        metrics.check.regressions != 0 ||
        metrics.check.candidate_support_violations
                .violating_roots != 0 ||
        metrics.check.candidate_classes.values !=
            std::array<std::size_t, 4>{
                kPositiveCheckExamples, 0, 0, 0}) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        const dev::DeckMetrics& fit =
            metrics.fit.decks[deck];
        const dev::DeckMetrics& check =
            metrics.check.decks[deck];
        if (fit.candidate_support_violations
                    .violating_roots !=
                kPositiveFitSupportViolationCeilings[
                    deck] ||
            fit.regressions != 0 ||
            check.candidate_support_violations
                    .violating_roots != 0 ||
            check.regressions != 0 ||
            check.candidate_classes.values !=
                std::array<std::size_t, 4>{
                    kPositiveCheckExamplesByDeck[
                        deck],
                    0, 0, 0}) {
            return false;
        }
    }
    return true;
}

bool check_positive_clean(
    const dev::EvaluationMetrics& metrics) {
    if (!metrics.parent_anchors_exact ||
        !metrics.accounting.zero() ||
        metrics.check.positive_roots !=
            kPositiveCheckExamples ||
        metrics.check.positive_options !=
            kPositiveCheckOptions ||
        !exact_positive_deck_counts(
            metrics.check,
            kPositiveCheckExamplesByDeck) ||
        metrics.check.repairs !=
            kRequiredCheckRepairs ||
        metrics.check.regressions != 0 ||
        metrics.check.candidate_support_violations
                .violating_roots != 0 ||
        metrics.check.candidate_classes.values !=
            std::array<std::size_t, 4>{
                kPositiveCheckExamples, 0, 0, 0}) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        const dev::DeckMetrics& value =
            metrics.check.decks[deck];
        if (value.repairs !=
                kPositiveCheckExamplesByDeck[deck] -
                    value.parent_classes.values[0] -
                    value.parent_classes.values[3] ||
            value.regressions != 0 ||
            value.candidate_support_violations
                    .violating_roots != 0 ||
            value.candidate_classes.values !=
                std::array<std::size_t, 4>{
                    kPositiveCheckExamplesByDeck[
                        deck],
                    0, 0, 0}) {
            return false;
        }
    }
    return true;
}

bool fit_positive_preserved(
    const dev::EvaluationMetrics& metrics) {
    if (metrics.fit.positive_roots !=
            kPositiveFitExamples ||
        metrics.fit.positive_options !=
            kPositiveFitOptions ||
        !exact_positive_deck_counts(
            metrics.fit,
            kPositiveFitExamplesByDeck) ||
        metrics.fit.repairs <
            kRequiredFitRepairs ||
        metrics.fit.regressions != 0 ||
        metrics.fit.candidate_support_violations
                .violating_roots >
            kRequiredFitSupportViolations) {
        return false;
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (metrics.fit.decks[deck].regressions != 0 ||
            metrics.fit.decks[deck]
                    .candidate_support_violations
                    .violating_roots >
                kPositiveFitSupportViolationCeilings[
                    deck]) {
            return false;
        }
    }
    return true;
}

std::vector<double> behavior(
    const std::vector<double>& combined_scores) {
    return math::behavior_mixture(
        math::stable_softmax(
            combined_scores,
            dev::kPolicyTemperature),
        dev::kBehaviorPrimaryWeight);
}

double forward_kl_binary64(
    const std::vector<double>& target,
    const std::vector<double>& candidate) {
    if (target.empty() ||
        target.size() != candidate.size()) {
        fail("neutral KL shapes disagree");
    }
    double target_total = 0.0;
    double candidate_total = 0.0;
    double result = 0.0;
    for (std::size_t index = 0;
         index < target.size(); ++index) {
        const double left = target[index];
        const double right = candidate[index];
        if (!finite(left) || !finite(right) ||
            left <= 0.0 || right <= 0.0) {
            fail("neutral KL distribution is not finite and positive");
        }
        target_total += left;
        candidate_total += right;
        result +=
            left * std::log(left / right);
    }
    if (!finite(result) ||
        std::abs(target_total - 1.0) >
            1.0e-12 ||
        std::abs(candidate_total - 1.0) >
            1.0e-12 ||
        result < -1.0e-15) {
        fail("neutral KL distribution or result is invalid");
    }
    return result < 0.0 ? 0.0 : result;
}

std::vector<std::size_t> exact_support_bits(
    const std::vector<double>& scores) {
    if (scores.empty() ||
        !std::all_of(
            scores.begin(), scores.end(),
            finite)) {
        fail("exact support requires finite scores");
    }
    const double maximum =
        *std::max_element(
            scores.begin(), scores.end());
    const std::uint64_t maximum_bits =
        std::bit_cast<std::uint64_t>(maximum);
    std::vector<std::size_t> result;
    for (std::size_t index = 0;
         index < scores.size(); ++index) {
        if (std::bit_cast<std::uint64_t>(
                scores[index]) ==
            maximum_bits) {
            result.push_back(index);
        }
    }
    return result;
}

bool valid_neutral_metrics(
    const NeutralDriftMetrics& metrics) {
    if (!metrics.finite_probabilities ||
        metrics.rows != kNeutralRowsPerSplit ||
        !finite(metrics.baseline_equal_deck_kl) ||
        !finite(metrics.anchored_equal_deck_kl) ||
        metrics.baseline_equal_deck_kl < 0.0 ||
        metrics.anchored_equal_deck_kl < 0.0) {
        return false;
    }
    std::size_t rows = 0;
    std::size_t options = 0;
    std::size_t baseline_changes = 0;
    std::size_t anchored_changes = 0;
    double baseline_equal = 0.0;
    double anchored_equal = 0.0;
    for (const NeutralDeckMetrics& deck :
         metrics.decks) {
        if (deck.rows != kNeutralRowsPerDeck ||
            !finite(
                deck.baseline_parent_to_candidate_kl) ||
            !finite(
                deck.anchored_parent_to_candidate_kl) ||
            deck.baseline_parent_to_candidate_kl <
                0.0 ||
            deck.anchored_parent_to_candidate_kl <
                0.0 ||
            deck.baseline_exact_support_changes >
                deck.rows ||
            deck.anchored_exact_support_changes >
                deck.rows) {
            return false;
        }
        rows += deck.rows;
        options += deck.options;
        baseline_changes +=
            deck.baseline_exact_support_changes;
        anchored_changes +=
            deck.anchored_exact_support_changes;
        baseline_equal +=
            deck.baseline_parent_to_candidate_kl;
        anchored_equal +=
            deck.anchored_parent_to_candidate_kl;
    }
    baseline_equal /=
        static_cast<double>(bundle::kDeckCount);
    anchored_equal /=
        static_cast<double>(bundle::kDeckCount);
    return
        rows == metrics.rows &&
        options == metrics.options &&
        baseline_changes ==
            metrics.baseline_exact_support_changes &&
        anchored_changes ==
            metrics.anchored_exact_support_changes &&
        baseline_equal ==
            metrics.baseline_equal_deck_kl &&
        anchored_equal ==
            metrics.anchored_equal_deck_kl;
}

struct NeutralCombinedScores {
    std::vector<std::vector<double>> fit;
    std::vector<std::vector<double>> check;
    bool parent_anchors_exact = false;
};

NeutralCombinedScores score_neutral(
    const PreparedNeutralCorpus& corpus,
    std::shared_ptr<const LearnedModel> model,
    bool require_parent_anchors) {
    if (!model) {
        throw std::invalid_argument(
            "neutral scoring requires a model");
    }
    NeutralCombinedScores result{
        .parent_anchors_exact =
            require_parent_anchors,
    };
    const auto score_split =
        [&](const std::vector<PreparedNeutralRow>& rows,
            std::vector<std::vector<double>>& target) {
            target.reserve(rows.size());
            for (const PreparedNeutralRow& row : rows) {
                const std::vector<double> base =
                    base_scores(row);
                const std::vector<double> logits =
                    learned_policy_head_logits(
                        options(row),
                        LearnedPolicyDecisionKind::Priority,
                        model);
                const math::CenteredResidualScores scores =
                    math::centered_tanh_scores(
                        base, logits,
                        dev::kResidualWeight);
                if (require_parent_anchors) {
                    for (std::size_t action = 0;
                         action < row.actions.size();
                         ++action) {
                        if (std::bit_cast<std::uint64_t>(
                                scores.residuals[
                                    action]) !=
                            std::bit_cast<std::uint64_t>(
                                row.actions[action]
                                    .parent_residual)) {
                            fail("neutral parent residual anchor drifted");
                        }
                    }
                }
                target.push_back(
                    scores.combined_scores);
            }
        };
    score_split(corpus.fit, result.fit);
    score_split(corpus.check, result.check);
    return result;
}

std::vector<NeutralScoreTriplet> check_triplets(
    const PreparedNeutralCorpus& corpus,
    const NeutralCombinedScores& parent,
    const NeutralCombinedScores& baseline,
    const NeutralCombinedScores& anchored) {
    if (corpus.check.size() != parent.check.size() ||
        corpus.check.size() != baseline.check.size() ||
        corpus.check.size() != anchored.check.size()) {
        fail("neutral CHECK score shape drifted");
    }
    std::vector<NeutralScoreTriplet> result;
    result.reserve(corpus.check.size());
    for (std::size_t index = 0;
         index < corpus.check.size(); ++index) {
        result.push_back({
            .owner_deck =
                corpus.check[index].owner_deck,
            .parent_combined_scores =
                parent.check[index],
            .baseline_combined_scores =
                baseline.check[index],
            .anchored_combined_scores =
                anchored.check[index],
        });
    }
    return result;
}

void require_production_training_batch(
    const TrainingBatch& batch,
    bool include_neutral) {
    if (batch.positive_examples !=
            kPositiveFitExamples ||
        batch.positive_options !=
            kPositiveFitOptions ||
        batch.accounting.optimizer !=
            dev::kOptimizer ||
        batch.accounting.optimizer_calls != 1 ||
        batch.accounting.check_examples != 0 ||
        batch.accounting.background_only_examples != 0) {
        fail("positive training boundary contract drifted");
    }
    if (!include_neutral) {
        if (batch.neutral_examples != 0 ||
            batch.neutral_options != 0 ||
            batch.accounting.fit_examples !=
                kPositiveFitExamples ||
            batch.accounting.fit_options !=
                kPositiveFitOptions ||
            batch.accounting.training_input_sha256 !=
                kRequiredPositiveOnlyTrainingInputSha256) {
            fail("omitted-neutral control input drifted");
        }
        return;
    }
    if (batch.neutral_examples !=
            kNeutralRowsPerSplit ||
        batch.accounting.fit_examples !=
            kPositiveFitExamples +
                kNeutralRowsPerSplit ||
        batch.accounting.fit_options !=
            batch.positive_options +
                batch.neutral_options) {
        fail("anchored training boundary census drifted");
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        if (batch.neutral_examples_by_deck[deck] !=
                kNeutralRowsPerDeck ||
            batch.neutral_loss_mass_by_deck[deck] !=
                static_cast<double>(
                    kPositiveFitExamplesByDeck[deck])) {
            fail("neutral deck loss mass is not exactly 1:1");
        }
    }
}

void require_exact_dev1_corpus(
    const dev::PreparedCorpus& corpus) {
    const integrity::RegularFileSnapshot before =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    if (before.byte_size !=
            bundle::kPublishedArtifactBytes ||
        before.sha256 !=
            bundle::kPublishedArtifactSha256) {
        fail("immutable DEV1 bundle identity drifted");
    }
    const bundle::Bundle source =
        bundle::load_published();
    const dev::PreparedCorpus exact =
        dev::prepare(source);
    const integrity::RegularFileSnapshot after =
        integrity::snapshot_regular_file(
            std::string(bundle::kArtifactPath));
    if (before != after || corpus != exact) {
        fail("evaluator did not receive the exact DEV1 prepared corpus");
    }
}

} // namespace

PreparedNeutralCorpus prepare(
    const neutral::Artifact& artifact) {
    neutral::validate(artifact);
    PreparedNeutralCorpus result =
        prepare_rows_impl(artifact.rows);
    if (result.fit.size() !=
            kNeutralRowsPerSplit ||
        result.check.size() !=
            kNeutralRowsPerSplit) {
        fail("published neutral artifact split census drifted");
    }
    for (std::size_t deck = 0;
         deck < bundle::kDeckCount; ++deck) {
        for (std::size_t row = 0;
             row < kNeutralRowsPerDeck; ++row) {
            const std::size_t index =
                deck * kNeutralRowsPerDeck + row;
            if (result.fit[index].owner_deck !=
                    deck ||
                result.check[index].owner_deck !=
                    deck) {
                fail("published neutral artifact order drifted");
            }
        }
    }
    return result;
}

std::vector<double> neutral_behavior_target(
    const PreparedNeutralRow& row) {
    if (row.actions.size() < 2 ||
        row.actions.size() >
            bundle::kMaximumActions ||
        row.owner_deck >= bundle::kDeckCount) {
        fail("neutral target row has an invalid shape");
    }
    for (const PreparedNeutralAction& action :
         row.actions) {
        if (action.features.size() !=
                bundle::kFeatureCount ||
            !finite(action.base_score) ||
            !finite(action.parent_residual)) {
            fail("neutral target action is malformed");
        }
    }
    return behavior(parent_combined_scores(row));
}

NeutralDriftMetrics measure_neutral_check(
    std::span<const NeutralScoreTriplet> rows) {
    if (rows.size() != kNeutralRowsPerSplit) {
        fail("neutral CHECK metric requires exactly 160 rows");
    }
    NeutralDriftMetrics result{
        .finite_probabilities = true,
    };
    for (std::size_t index = 0;
         index < rows.size(); ++index) {
        const std::size_t expected_deck =
            index / kNeutralRowsPerDeck;
        const NeutralScoreTriplet& row =
            rows[index];
        if (row.owner_deck != expected_deck ||
            row.parent_combined_scores.size() < 2 ||
            row.parent_combined_scores.size() >
                bundle::kMaximumActions ||
            row.baseline_combined_scores.size() !=
                row.parent_combined_scores.size() ||
            row.anchored_combined_scores.size() !=
                row.parent_combined_scores.size()) {
            fail("neutral CHECK metric order or shape drifted");
        }
        const std::vector<double> parent =
            behavior(
                row.parent_combined_scores);
        const std::vector<double> baseline =
            behavior(
                row.baseline_combined_scores);
        const std::vector<double> anchored =
            behavior(
                row.anchored_combined_scores);
        NeutralDeckMetrics& deck =
            result.decks[expected_deck];
        ++deck.rows;
        deck.options +=
            row.parent_combined_scores.size();
        deck.baseline_parent_to_candidate_kl +=
            forward_kl_binary64(
                parent, baseline);
        deck.anchored_parent_to_candidate_kl +=
            forward_kl_binary64(
                parent, anchored);
        deck.baseline_exact_support_changes +=
            exact_support_bits(
                row.parent_combined_scores) !=
                    exact_support_bits(
                        row.baseline_combined_scores)
                ? 1U
                : 0U;
        deck.anchored_exact_support_changes +=
            exact_support_bits(
                row.parent_combined_scores) !=
                    exact_support_bits(
                        row.anchored_combined_scores)
                ? 1U
                : 0U;
    }
    for (NeutralDeckMetrics& deck : result.decks) {
        deck.baseline_parent_to_candidate_kl /=
            static_cast<double>(
                kNeutralRowsPerDeck);
        deck.anchored_parent_to_candidate_kl /=
            static_cast<double>(
                kNeutralRowsPerDeck);
        result.rows += deck.rows;
        result.options += deck.options;
        result.baseline_equal_deck_kl +=
            deck.baseline_parent_to_candidate_kl;
        result.anchored_equal_deck_kl +=
            deck.anchored_parent_to_candidate_kl;
        result.baseline_exact_support_changes +=
            deck.baseline_exact_support_changes;
        result.anchored_exact_support_changes +=
            deck.anchored_exact_support_changes;
    }
    result.baseline_equal_deck_kl /=
        static_cast<double>(bundle::kDeckCount);
    result.anchored_equal_deck_kl /=
        static_cast<double>(bundle::kDeckCount);
    if (!valid_neutral_metrics(result)) {
        fail("neutral CHECK metric cross-sums are invalid");
    }
    return result;
}

GateReport evaluate_gate(
    const dev::EvaluationMetrics& positive_only,
    const dev::EvaluationMetrics& anchored,
    const NeutralDriftMetrics& neutral_metrics,
    const IsolationChecks& isolation) {
    GateReport result;
    result.baseline_positive_contract_exact =
        exact_positive_only_contract(
            positive_only);
    result.check_positive_clean =
        check_positive_clean(anchored);
    result.fit_positive_preserved =
        fit_positive_preserved(anchored);
    const bool neutral_valid =
        valid_neutral_metrics(neutral_metrics);
    result.neutral_baseline_nonzero =
        neutral_valid &&
        neutral_metrics.baseline_equal_deck_kl >
            0.0 &&
        neutral_metrics.baseline_exact_support_changes >
            0;
    result.neutral_per_deck_nonworsening =
        neutral_valid;
    if (neutral_valid) {
        for (std::size_t deck = 0;
             deck < bundle::kDeckCount; ++deck) {
            const NeutralDeckMetrics& value =
                neutral_metrics.decks[deck];
            result.neutral_per_deck_nonworsening =
                result.neutral_per_deck_nonworsening &&
                value.anchored_parent_to_candidate_kl <=
                    value.baseline_parent_to_candidate_kl &&
                value.anchored_exact_support_changes <=
                    value.baseline_exact_support_changes;
        }
    }
    result.neutral_kl_halved =
        neutral_valid &&
        2.0 *
                neutral_metrics
                    .anchored_equal_deck_kl <=
            neutral_metrics
                .baseline_equal_deck_kl;
    result.neutral_support_changes_halved =
        neutral_valid &&
        2U *
                neutral_metrics
                    .anchored_exact_support_changes <=
            neutral_metrics
                .baseline_exact_support_changes;
    result.isolation_exact =
        isolation.parent_immutable &&
        isolation.positive_only_candidate_exact &&
        isolation.omitted_neutral_control_exact &&
        isolation.parent_anchors_exact &&
        isolation.hidden_repartition_contract_exact &&
        isolation.fit_check_isolated &&
        isolation.nonpriority_components_identical &&
        isolation.priority_component_changed;

    const auto require =
        [&](bool condition, std::string_view message) {
            if (!condition) {
                result.failures.emplace_back(message);
            }
        };
    require(
        result.baseline_positive_contract_exact,
        "positive-only spillover baseline contract failed");
    require(
        result.check_positive_clean,
        "positive CHECK was not 94/94 clean");
    require(
        result.fit_positive_preserved,
        "positive FIT repair/support gate failed");
    require(
        result.neutral_baseline_nonzero,
        "neutral spillover baseline was zero or invalid");
    require(
        result.neutral_per_deck_nonworsening,
        "a neutral deck worsened");
    require(
        result.neutral_kl_halved,
        "equal-deck neutral KL was not halved");
    require(
        result.neutral_support_changes_halved,
        "neutral exact-support changes were not halved");
    require(
        result.isolation_exact,
        "model/training isolation contract failed");
    return result;
}

Report evaluate(
    const dev::PreparedCorpus& positive_corpus,
    const neutral::Artifact& neutral_artifact,
    std::shared_ptr<const LearnedModel> frozen_c16,
    std::shared_ptr<const LearnedModel>
        exact_positive_only_candidate,
    FitBoundaryObserver observer) {
    if (!frozen_c16 ||
        !exact_positive_only_candidate) {
        throw std::invalid_argument(
            "FQ4 neutral evaluation requires C16 and its exact positive-only candidate");
    }
    require_exact_dev1_corpus(
        positive_corpus);
    const std::string parent_fingerprint =
        learned_model_fingerprint(frozen_c16);
    const std::string baseline_fingerprint =
        learned_model_fingerprint(
            exact_positive_only_candidate);
    if (parent_fingerprint !=
            bundle::kParentModelFingerprint ||
        baseline_fingerprint !=
            kRequiredPositiveOnlyCandidateFingerprint) {
        fail("model identity preflight failed");
    }
    const LearnedModelComponentFingerprints
        parent_components =
            learned_model_component_fingerprints(
                frozen_c16);
    const LearnedModelComponentFingerprints
        baseline_components =
            learned_model_component_fingerprints(
                exact_positive_only_candidate);
    if (!same_nonpriority(
            parent_components,
            baseline_components) ||
        parent_components.priority ==
            baseline_components.priority) {
        fail("positive-only candidate isolation preflight failed");
    }

    const PreparedNeutralCorpus neutral_corpus =
        prepare(neutral_artifact);
    const std::vector<
        LearnedValuePriorityTrainingExample>
        positive_examples =
            dev::fit_examples(positive_corpus);
    const TrainingBatch control =
        build_batch_impl(
            positive_examples,
            neutral_corpus, false);
    const TrainingBatch anchored_batch =
        build_batch_impl(
            positive_examples,
            neutral_corpus, true);
    require_production_training_batch(
        control, false);
    require_production_training_batch(
        anchored_batch, true);

    const dev::CorpusLogits positive_parent_logits =
        dev::score_logits(
            positive_corpus, frozen_c16);
    const dev::EvaluationMetrics positive_anchors =
        dev::evaluate_logits(
            positive_corpus,
            positive_parent_logits,
            positive_parent_logits);
    const NeutralCombinedScores neutral_parent_scores =
        score_neutral(
            neutral_corpus, frozen_c16, true);

    if (observer) {
        observer(
            false, control.examples,
            dev::kOptimizer);
    }
    const std::shared_ptr<const LearnedModel>
        reproduced_control =
            update_learned_value_priority_head(
                frozen_c16,
                control.examples,
                dev::kOptimizer);
    if (learned_model_fingerprint(
            reproduced_control) !=
            kRequiredPositiveOnlyCandidateFingerprint ||
        learned_model_component_fingerprints(
            reproduced_control) !=
            baseline_components) {
        fail("omitted-neutral control did not reproduce DEV1");
    }

    if (observer) {
        observer(
            true, anchored_batch.examples,
            dev::kOptimizer);
    }
    const std::shared_ptr<const LearnedModel>
        anchored =
            update_learned_value_priority_head(
                frozen_c16,
                anchored_batch.examples,
                dev::kOptimizer);
    const std::string anchored_fingerprint =
        learned_model_fingerprint(anchored);
    const LearnedModelComponentFingerprints
        anchored_components =
            learned_model_component_fingerprints(
                anchored);
    const bool parent_immutable =
        learned_model_fingerprint(frozen_c16) ==
            parent_fingerprint;
    const bool all_nonpriority_identical =
        same_nonpriority(
            parent_components,
            baseline_components) &&
        same_nonpriority(
            parent_components,
            anchored_components);
    if (!parent_immutable ||
        !all_nonpriority_identical) {
        fail("fit mutated C16 or a non-Priority component");
    }

    const dev::ModelEvaluationReport baseline_evaluation =
        dev::evaluate_models(
            positive_corpus, frozen_c16,
            exact_positive_only_candidate);
    const dev::ModelEvaluationReport anchored_evaluation =
        dev::evaluate_models(
            positive_corpus, frozen_c16,
            anchored);
    const NeutralCombinedScores baseline_scores =
        score_neutral(
            neutral_corpus,
            exact_positive_only_candidate,
            false);
    const NeutralCombinedScores anchored_scores =
        score_neutral(
            neutral_corpus, anchored, false);
    const std::vector<NeutralScoreTriplet> triplets =
        check_triplets(
            neutral_corpus,
            neutral_parent_scores,
            baseline_scores,
            anchored_scores);
    const NeutralDriftMetrics neutral_metrics =
        measure_neutral_check(triplets);

    const IsolationChecks isolation{
        .parent_immutable = parent_immutable,
        .positive_only_candidate_exact =
            baseline_fingerprint ==
                kRequiredPositiveOnlyCandidateFingerprint &&
            baseline_components ==
                learned_model_component_fingerprints(
                    reproduced_control),
        .omitted_neutral_control_exact =
            control.accounting.training_input_sha256 ==
                kRequiredPositiveOnlyTrainingInputSha256 &&
            learned_model_fingerprint(
                reproduced_control) ==
                kRequiredPositiveOnlyCandidateFingerprint,
        .parent_anchors_exact =
            positive_anchors.parent_anchors_exact &&
            neutral_parent_scores
                .parent_anchors_exact,
        .hidden_repartition_contract_exact =
            neutral_artifact.manifest.accounting
                .canonical_hidden_bit_identical,
        .fit_check_isolated =
            control.accounting.check_examples == 0 &&
            control.accounting
                    .background_only_examples == 0 &&
            anchored_batch.accounting
                    .check_examples == 0 &&
            anchored_batch.accounting
                    .background_only_examples == 0 &&
            anchored_batch.neutral_examples ==
                neutral_corpus.fit.size(),
        .nonpriority_components_identical =
            all_nonpriority_identical,
        .priority_component_changed =
            anchored_components.priority !=
                parent_components.priority,
    };
    const GateReport gate =
        evaluate_gate(
            baseline_evaluation.metrics,
            anchored_evaluation.metrics,
            neutral_metrics,
            isolation);
    return {
        .anchored_candidate = anchored,
        .parent_fingerprint =
            parent_fingerprint,
        .positive_only_candidate_fingerprint =
            baseline_fingerprint,
        .anchored_candidate_fingerprint =
            anchored_fingerprint,
        .parent_components =
            parent_components,
        .positive_only_candidate_components =
            baseline_components,
        .anchored_candidate_components =
            anchored_components,
        .omitted_neutral_control = control,
        .anchored_training = anchored_batch,
        .positive_only_evaluation =
            baseline_evaluation,
        .anchored_evaluation =
            anchored_evaluation,
        .neutral_check = neutral_metrics,
        .isolation = isolation,
        .gate = gate,
    };
}

PreparedNeutralCorpus testing::prepare_rows(
    std::span<const neutral::NeutralRow> rows) {
    return prepare_rows_impl(rows);
}

TrainingBatch testing::build_training_batch(
    const std::vector<
        LearnedValuePriorityTrainingExample>&
        positive_examples,
    const PreparedNeutralCorpus& neutral_corpus,
    bool include_neutral) {
    return build_batch_impl(
        positive_examples,
        neutral_corpus,
        include_neutral);
}

std::string testing::training_input_sha256(
    const std::vector<
        LearnedValuePriorityTrainingExample>& examples,
    const LearnedValuePriorityHeadUpdateConfig& optimizer) {
    return training_input_sha256_impl(
        examples, optimizer);
}

} // namespace old_school::fq4_neutral_evaluator
